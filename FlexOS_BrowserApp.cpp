// #############################################################
// ##  FlexOS_BrowserApp.cpp  ·  LADO DISPOSITIVO DEL NAVEGADOR
// ##  ------------------------------------------------------
// ##  Todo lo que solo tiene sentido dentro de la placa: memoria,
// ##  capacidades medidas, persistencia, transporte WebSocket,
// ##  decodificacion de frames, pestanas, interfaz y gestos.
// ##
// ##  El fichero entero esta dentro de #if ARDUINO: en el PC no se
// ##  compila nada de aqui (las pruebas de host usan el nucleo puro
// ##  de FlexOS_Browser.cpp, que si es portable).
// ##
// ##  ORGANIZACION
// ##    1  Estado global
// ##    2  Memoria y capacidades REALES (medidas, no supuestas)
// ##    3  Persistencia: ajustes, historial, favoritos
// ##    4  Transporte: WebSocket sobre TLS + tarea de red
// ##    5  Frames: recepcion, contrapresion, decodificacion, volcado
// ##    6  Navegacion y pestanas
// ##    7  Interfaz: dibujo
// ##    8  Interfaz: tactil
// ##    9  Ciclo de vida (Begin / Enter / Tick / Exit)
// #############################################################

#include "FlexOS_Browser.h"

#if FLEXBR_ON_DEVICE && FLEXBR_ON

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_random.h"
#include <strings.h>
#include "FlexOS_JPEG.h"

// =============================================================
//  1) ESTADO GLOBAL
// =============================================================
static bool        gInit    = false;
static bool        gActive  = false;      // la app esta abierta
static bool        gWantClose = false;
static BrSettings  gSt;
static BrStats     gStats;
static uint32_t    gCaps = 0;
static BrTab       gTabs[FLEXBR_MAX_TABS];
static int         gTab   = 0;
static int         gView  = BRV_PAGE;
static char        gErr[FLEXBR_ERRMSG_MAX] = "";
static uint32_t    gErrMs = 0;
static bool        gNeedFullRedraw = true;
static bool        gNeedChromeRedraw = true;

// Edicion de texto (omnibox, campos de Ajustes y escritura en la pagina).
enum { BRE_NONE = 0, BRE_OMNIBOX, BRE_SERVER, BRE_TOKEN, BRE_SEARCHTPL, BRE_PAGEKEY };
static int   gEditTarget = BRE_NONE;
static char  gEdit[FLEXBR_INPUT_MAX] = "";

// -------------------------------------------------------------
//  Declaraciones adelantadas. El fichero esta ordenado por capas
//  (memoria -> persistencia -> red -> dibujo), pero la capa de red
//  necesita saber el tamano del area de pagina y la de dibujo
//  necesita mandar comandos, asi que unas pocas firmas suben aqui.
// -------------------------------------------------------------
static int  brChromeH();
static int  brContentPageH();
static void brPageRect(int* px, int* py, int* pw, int* ph);
static void brRepaintPageFromCache();
static void brRenderAll();
static void brRenderChrome();
static void brSettingsDraw(int px, int py, int pw, int ph, int* yio);
static void brNavigateTo(const char* url, bool addHistory);
static void brOpenInternal(int page);
static bool brPushCmd(uint8_t type, uint8_t ch, int16_t a, int16_t b,
                      uint8_t u1, uint8_t u2, uint8_t u3, uint32_t arg, const char* text);

// Desplazamiento de las paginas internas (historial, favoritos...).
static int   gIntScroll = 0;
static int   gIntMaxScroll = 0;

// Historial y favoritos.
static BrHistEntry* gHist = NULL;
static int          gHistN = 0;
static BrBookmark*  gBm = NULL;
static int          gBmN = 0;
static bool         gHistDirty = false, gBmDirty = false;
static uint32_t     gLastPersistMs = 0;
#define BR_PERSIST_MIN_MS 20000       // nunca se escribe flash mas a menudo que esto

// Multimedia detectada en la pagina actual.
static FbpMedia gMedia;
static bool     gMediaAvail = false;
static bool     gMediaPlaying = false;
static uint32_t gMediaPosMs = 0;

// Descargas: en esta version el dispositivo NO descarga ficheros (no
// hay sitio para ellos ni gestor de archivos remoto). Se registra lo
// que el servicio anuncia para poder explicarlo en flex://downloads.
#define BR_DL_MAX 6
typedef struct { char name[48]; char url[FLEXBR_URL_MAX]; uint32_t size; } BrDownload;
static BrDownload gDl[BR_DL_MAX];
static int        gDlN = 0;

// =============================================================
//  2) MEMORIA Y CAPACIDADES REALES
//  -------------------------------------------------------------
//  Nada de esto se decide por modelo de placa: se MIDE. Si el heap
//  libre no da, la sesion remota se apaga y la interfaz lo explica en
//  vez de reiniciarse a mitad de una pagina.
// =============================================================
static uint8_t* gRx  = NULL;                 // buffer de recepcion (mensaje completo)
static size_t   gRxCap = 0;
static uint16_t* gScaleLine = NULL;          // una fila ya ampliada (ver brBlitRowCb)
static int       gScaleLineCap = 0;
static uint8_t* gKey = NULL;                 // cache de bandas del ultimo keyframe
static size_t   gKeyCap = 0, gKeyUsed = 0;
#define BR_STRIPS_MAX 10
typedef struct { uint32_t off, len; uint16_t x, y, w, h; } BrStrip;
static BrStrip gStrips[BR_STRIPS_MAX];
static int     gStripN = 0;
static bool    gStripsValid = false;
static bool    gStripsStale = false;         // hubo actualizaciones parciales encima

static const char* gCapWhy[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static int capIdx(uint32_t cap){ int i = 0; while(cap > 1){ cap >>= 1; i++; } return i; }
static void capOff(uint32_t cap, const char* why){ gCaps &= ~cap; gCapWhy[capIdx(cap)] = why; }
static void capOn(uint32_t cap){ gCaps |= cap; gCapWhy[capIdx(cap)] = NULL; }

uint32_t    flexBrowserCaps(){ return gCaps; }
const char* flexBrowserCapReason(uint32_t cap){
  if(gCaps & cap) return NULL;
  const char* w = gCapWhy[capIdx(cap)];
  return w ? w : "No disponible en esta placa";
}
const BrStats*    flexBrowserStats(){ return &gStats; }
const BrSettings* flexBrowserSettings(){ return &gSt; }

static void brMeasure(){
  gStats.freeHeap  = (uint32_t)brHostFreeHeap();
  gStats.freePsram = (uint32_t)brHostFreePsram();
  if(gStats.minFreeHeap == 0 || gStats.freeHeap < gStats.minFreeHeap)
    gStats.minFreeHeap = gStats.freeHeap;
}

// Decide que se puede hacer AHORA, con la memoria que hay AHORA.
static void brComputeCaps(){
  brMeasure();
  gCaps = 0;
  for(int i = 0; i < 8; i++) gCapWhy[i] = NULL;

  if(gStats.freePsram > 0) capOn(FLEXBR_CAP_PSRAM);
  else capOff(FLEXBR_CAP_PSRAM, "Esta placa no lleva PSRAM");

  if(brHostFsReady()) capOn(FLEXBR_CAP_FS);
  else capOff(FLEXBR_CAP_FS, "Sin sistema de archivos montado");

  capOn(FLEXBR_CAP_TLS);

#if FLEXBR_REMOTE_ON
  if(!gSt.server[0]){
    capOff(FLEXBR_CAP_REMOTE, "Falta configurar el servidor en Ajustes");
  } else if(gStats.freeHeap < FLEXBR_MIN_FREE_HEAP){
    // Se explica con el numero real: es lo unico que permite decidir
    // si merece la pena cerrar otra app y volver a intentarlo.
    static char why[80];
    snprintf(why, sizeof(why), "Memoria insuficiente: %u KB libres, hacen falta %u KB",
             (unsigned)(gStats.freeHeap / 1024), (unsigned)(FLEXBR_MIN_FREE_HEAP / 1024));
    capOff(FLEXBR_CAP_REMOTE, why);
  } else {
    capOn(FLEXBR_CAP_REMOTE);
  }
#else
  capOff(FLEXBR_CAP_REMOTE, "Sesion remota desactivada en este build");
#endif

#if FLEXBR_TILES_ON
  if(FLEXBR_TILE_MAX > 0 && (gCaps & FLEXBR_CAP_PSRAM)) capOn(FLEXBR_CAP_TILES);
  else capOff(FLEXBR_CAP_TILES, "Sin PSRAM solo se usan frames completos");
#else
  capOff(FLEXBR_CAP_TILES, "Regiones sucias desactivadas en este build");
#endif

#if FLEXBR_MEDIA_ON
  // El reproductor propio necesita sitio para el frame de video Y una
  // sesion remota que lo alimente. Ademas: NINGUNA de las tres placas
  // tiene salida de audio cableada (no hay I2S ni DAC en los .ino), asi
  // que el reproductor es de VIDEO. Eso se dice, no se disimula.
  if((gCaps & FLEXBR_CAP_REMOTE) && (gCaps & FLEXBR_CAP_PSRAM)) capOn(FLEXBR_CAP_MEDIA);
  else if(!(gCaps & FLEXBR_CAP_PSRAM)) capOff(FLEXBR_CAP_MEDIA, "El reproductor necesita PSRAM");
  else capOff(FLEXBR_CAP_MEDIA, "Necesita una sesion remota activa");
#else
  capOff(FLEXBR_CAP_MEDIA, "Multimedia desactivada en este build");
#endif

  if(FLEXBR_MAX_TABS > 1 && (gCaps & FLEXBR_CAP_PSRAM)) capOn(FLEXBR_CAP_TABS);
  else capOff(FLEXBR_CAP_TABS, "Sin PSRAM solo cabe una pesta\xC3\xB1" "a");
}

// Cuantas pestanas se permiten AHORA (memoria medida, no un numero fijo).
static int brTabLimit(){
  if(!(gCaps & FLEXBR_CAP_TABS)) return 1;
  size_t ps = brHostFreePsram();
  int byMem = (int)(ps / (200u * 1024u));      // ~200 KB de holgura por pestana
  if(byMem < 1) byMem = 1;
  return byMem < FLEXBR_MAX_TABS ? byMem : FLEXBR_MAX_TABS;
}

static bool brAllocBuffers(){
  if(gRx) return true;
  gRxCap = FBP_PAYLOAD_MAX + FBP_HDR_SIZE + 64;
  gRx = (uint8_t*)brHostAlloc(gRxCap, true);
  if(!gRx){ gRxCap = 0; return false; }
  // Fila de trabajo para ampliar un frame que viene a menor escala.
  // Se dimensiona al ancho REAL del area de contenido, no a SCR_W: en
  // Modo PC/DeX la ventana puede ser mas estrecha y no hace falta mas.
  {
    int cx, cy, cw, chh; brHostContentRect(&cx, &cy, &cw, &chh);
    gScaleLineCap = cw > 0 ? cw : brHostScrW();
    gScaleLine = (uint16_t*)brHostAlloc((size_t)gScaleLineCap * sizeof(uint16_t), true);
    if(!gScaleLine) gScaleLineCap = 0;
  }
#if FLEXBR_KEYFRAME_MAX > 0
  if(gCaps & FLEXBR_CAP_PSRAM){
    gKeyCap = FLEXBR_KEYFRAME_MAX;
    gKey = (uint8_t*)brHostAlloc(gKeyCap, true);
    if(!gKey) gKeyCap = 0;                     // sin cache: se repintara pidiendo frame
  }
#endif
  return true;
}

static void brFreeBuffers(){
  if(gRx){ brHostFree(gRx); gRx = NULL; gRxCap = 0; }
  if(gKey){ brHostFree(gKey); gKey = NULL; gKeyCap = 0; }
  if(gScaleLine){ brHostFree(gScaleLine); gScaleLine = NULL; gScaleLineCap = 0; }
  gKeyUsed = 0; gStripN = 0; gStripsValid = false; gStripsStale = false;
}

// =============================================================
//  3) PERSISTENCIA
//  -------------------------------------------------------------
//  Los ajustes van a NVS (son pocos y pequenos). El historial y los
//  favoritos van a LittleFS, en un formato binario de longitud fija
//  por registro para poder leerlos de una pasada.
//
//  REGLA: NUNCA se escribe flash por frame, por pulsacion ni por
//  desplazamiento. Se marca "sucio" y se vuelca como mucho cada
//  BR_PERSIST_MIN_MS, y siempre al cerrar la app.
// =============================================================
// Carpeta del sistema donde viven los datos del navegador. Se toma la
// del modulo de ficheros si el .ino ya la definio; si no, la de siempre.
#ifndef FLEXFS_SYS_DIR
#define FLEXFS_SYS_DIR "/System"
#endif
#define BR_HIST_PATH  FLEXFS_SYS_DIR "/br_hist.bin"
#define BR_BM_PATH    FLEXFS_SYS_DIR "/br_bm.bin"

static void brLoadSettings(){
  flexBrSettingsDefaults(&gSt);
  brHostPrefsGetStr("brsrv",  gSt.server, sizeof(gSt.server), "");
  brHostPrefsGetStr("brtok",  gSt.token,  sizeof(gSt.token),  "");
  brHostPrefsGetStr("brtpl",  gSt.searchCustom, sizeof(gSt.searchCustom), "");
  gSt.searchEngine = (uint8_t)brHostPrefsGetInt("breng", 0);
  gSt.profile      = (uint8_t)brHostPrefsGetInt("brprof", BRQ_BALANCED);
  gSt.quality      = (uint8_t)brHostPrefsGetInt("brqual", 62);
  gSt.scalePct     = (uint8_t)brHostPrefsGetInt("brscale", 100);
  gSt.allowLocal   = brHostPrefsGetInt("brlocal", 0) != 0;
  gSt.allowInsecure= brHostPrefsGetInt("brinsec", 0) != 0;
  gSt.saveHistory  = brHostPrefsGetInt("brhist", 1) != 0;
  gSt.telemetry    = brHostPrefsGetInt("brtel", 0) != 0;
  gSt.mediaNative  = brHostPrefsGetInt("brmed", 1) != 0;
  if(gSt.searchEngine > 3) gSt.searchEngine = 0;
  if(gSt.profile >= BRQ_N) gSt.profile = BRQ_BALANCED;
  if(gSt.quality < 20 || gSt.quality > 90) gSt.quality = 62;
  if(gSt.scalePct < 50 || gSt.scalePct > 100) gSt.scalePct = 100;
}

static void brSaveSettings(){
  brHostPrefsPutStr("brsrv", gSt.server);
  brHostPrefsPutStr("brtok", gSt.token);
  brHostPrefsPutStr("brtpl", gSt.searchCustom);
  brHostPrefsPutInt("breng", gSt.searchEngine);
  brHostPrefsPutInt("brprof", gSt.profile);
  brHostPrefsPutInt("brqual", gSt.quality);
  brHostPrefsPutInt("brscale", gSt.scalePct);
  brHostPrefsPutInt("brlocal", gSt.allowLocal ? 1 : 0);
  brHostPrefsPutInt("brinsec", gSt.allowInsecure ? 1 : 0);
  brHostPrefsPutInt("brhist", gSt.saveHistory ? 1 : 0);
  brHostPrefsPutInt("brtel", gSt.telemetry ? 1 : 0);
  brHostPrefsPutInt("brmed", gSt.mediaNative ? 1 : 0);
}

// Cabecera de los ficheros de datos: magia + version + numero de
// registros. Si la version no cuadra, el fichero se ignora (y se
// reescribe), que es mejor que interpretar registros de otro formato.
#define BR_FILE_MAGIC 0x31524246u    /* "FBR1" */
typedef struct { uint32_t magic; uint16_t ver; uint16_t count; } BrFileHdr;

static void brLoadHistory(){
  gHistN = 0;
  if(!gHist || !brHostFsReady()) return;
  size_t need = sizeof(BrFileHdr) + sizeof(BrHistEntry) * FLEXBR_HIST_MAX;
  uint8_t* tmp = (uint8_t*)brHostAlloc(need, true);
  if(!tmp) return;
  int rd = brHostFileRead(BR_HIST_PATH, tmp, need);
  if(rd >= (int)sizeof(BrFileHdr)){
    BrFileHdr h; memcpy(&h, tmp, sizeof(h));
    if(h.magic == BR_FILE_MAGIC && h.ver == 1){
      int n = h.count;
      if(n > FLEXBR_HIST_MAX) n = FLEXBR_HIST_MAX;
      if((int)(sizeof(BrFileHdr) + sizeof(BrHistEntry) * n) <= rd){
        memcpy(gHist, tmp + sizeof(BrFileHdr), sizeof(BrHistEntry) * (size_t)n);
        gHistN = n;
        // Saneado: un fichero manipulado no puede dejar cadenas sin
        // terminar dentro de la RAM del sistema.
        for(int i = 0; i < gHistN; i++){
          gHist[i].url[FLEXBR_URL_MAX - 1] = 0;
          gHist[i].title[FLEXBR_TITLE_MAX - 1] = 0;
        }
      }
    }
  }
  brHostFree(tmp);
}

static void brSaveHistory(){
  if(!gHist || !brHostFsReady()) return;
  size_t need = sizeof(BrFileHdr) + sizeof(BrHistEntry) * (size_t)gHistN;
  uint8_t* tmp = (uint8_t*)brHostAlloc(need, true);
  if(!tmp) return;
  BrFileHdr h = { BR_FILE_MAGIC, 1, (uint16_t)gHistN };
  memcpy(tmp, &h, sizeof(h));
  if(gHistN) memcpy(tmp + sizeof(h), gHist, sizeof(BrHistEntry) * (size_t)gHistN);
  brHostFileWrite(BR_HIST_PATH, tmp, need);
  brHostFree(tmp);
  gHistDirty = false;
}

static void brLoadBookmarks(){
  gBmN = 0;
  if(!gBm || !brHostFsReady()) return;
  size_t need = sizeof(BrFileHdr) + sizeof(BrBookmark) * FLEXBR_BM_MAX;
  uint8_t* tmp = (uint8_t*)brHostAlloc(need, true);
  if(!tmp) return;
  int rd = brHostFileRead(BR_BM_PATH, tmp, need);
  if(rd >= (int)sizeof(BrFileHdr)){
    BrFileHdr h; memcpy(&h, tmp, sizeof(h));
    if(h.magic == BR_FILE_MAGIC && h.ver == 1){
      int n = h.count;
      if(n > FLEXBR_BM_MAX) n = FLEXBR_BM_MAX;
      if((int)(sizeof(BrFileHdr) + sizeof(BrBookmark) * n) <= rd){
        memcpy(gBm, tmp + sizeof(BrFileHdr), sizeof(BrBookmark) * (size_t)n);
        gBmN = n;
        for(int i = 0; i < gBmN; i++){
          gBm[i].url[FLEXBR_URL_MAX - 1] = 0;
          gBm[i].title[FLEXBR_TITLE_MAX - 1] = 0;
        }
      }
    }
  }
  brHostFree(tmp);
}

static void brSaveBookmarks(){
  if(!gBm || !brHostFsReady()) return;
  size_t need = sizeof(BrFileHdr) + sizeof(BrBookmark) * (size_t)gBmN;
  uint8_t* tmp = (uint8_t*)brHostAlloc(need, true);
  if(!tmp) return;
  BrFileHdr h = { BR_FILE_MAGIC, 1, (uint16_t)gBmN };
  memcpy(tmp, &h, sizeof(h));
  if(gBmN) memcpy(tmp + sizeof(h), gBm, sizeof(BrBookmark) * (size_t)gBmN);
  brHostFileWrite(BR_BM_PATH, tmp, need);
  brHostFree(tmp);
  gBmDirty = false;
}

void flexBrowserFlushPersist(bool force){
  uint32_t now = brHostMillis();
  if(!force && (now - gLastPersistMs) < BR_PERSIST_MIN_MS) return;
  if(!gHistDirty && !gBmDirty) return;
  if(gHistDirty) brSaveHistory();
  if(gBmDirty) brSaveBookmarks();
  gLastPersistMs = now;
}

// Anade o refresca una entrada de historial. Sin duplicados: si la URL
// ya estaba, sube al principio y suma una visita.
static void brHistoryAdd(const char* url, const char* title){
  if(!gSt.saveHistory || !gHist || !url || !url[0]) return;
  if(flexBrInternalPage(url) != BRP_NONE) return;      // las paginas internas no se guardan
  for(int i = 0; i < gHistN; i++){
    if(!strcmp(gHist[i].url, url)){
      gHist[i].lastMs = brHostMillis();
      if(gHist[i].visits < 0xFFFF) gHist[i].visits++;
      if(title && title[0]) snprintf(gHist[i].title, FLEXBR_TITLE_MAX, "%s", title);
      if(i > 0){ BrHistEntry t = gHist[i]; memmove(&gHist[1], &gHist[0], sizeof(BrHistEntry) * (size_t)i); gHist[0] = t; }
      gHistDirty = true;
      return;
    }
  }
  if(gHistN < FLEXBR_HIST_MAX) gHistN++;
  if(gHistN > 1) memmove(&gHist[1], &gHist[0], sizeof(BrHistEntry) * (size_t)(gHistN - 1));
  memset(&gHist[0], 0, sizeof(BrHistEntry));
  snprintf(gHist[0].url, FLEXBR_URL_MAX, "%s", url);
  snprintf(gHist[0].title, FLEXBR_TITLE_MAX, "%s", (title && title[0]) ? title : url);
  gHist[0].lastMs = brHostMillis();
  gHist[0].visits = 1;
  gHistDirty = true;
}

static int brBookmarkIndex(const char* url){
  if(!gBm || !url) return -1;
  for(int i = 0; i < gBmN; i++) if(!strcmp(gBm[i].url, url)) return i;
  return -1;
}

static bool brBookmarkToggle(const char* url, const char* title){
  if(!gBm || !url || !url[0]) return false;
  int i = brBookmarkIndex(url);
  if(i >= 0){
    if(i + 1 < gBmN) memmove(&gBm[i], &gBm[i + 1], sizeof(BrBookmark) * (size_t)(gBmN - i - 1));
    gBmN--; gBmDirty = true;
    return false;
  }
  if(gBmN >= FLEXBR_BM_MAX) return false;
  memset(&gBm[gBmN], 0, sizeof(BrBookmark));
  snprintf(gBm[gBmN].url, FLEXBR_URL_MAX, "%s", url);
  snprintf(gBm[gBmN].title, FLEXBR_TITLE_MAX, "%s", (title && title[0]) ? title : url);
  gBmN++; gBmDirty = true;
  return true;
}

void flexBrowserClearData(uint32_t mask){
  if(mask & BRCLR_HISTORY){ gHistN = 0; gHistDirty = true; if(brHostFsReady()) brHostFileDelete(BR_HIST_PATH); gHistDirty = false; }
  if(mask & BRCLR_BOOKMARKS){ gBmN = 0; gBmDirty = true; if(brHostFsReady()) brHostFileDelete(BR_BM_PATH); gBmDirty = false; }
  if(mask & BRCLR_CACHE){ gKeyUsed = 0; gStripN = 0; gStripsValid = false; gStripsStale = false; gDlN = 0; }
  gNeedFullRedraw = true;
}

// =============================================================
//  4) TRANSPORTE
//  -------------------------------------------------------------
//  Un WebSocket (RFC 6455) sobre TLS, escrito a mano por el mismo
//  motivo que el decodificador JPEG: no hay una biblioteca de
//  WebSocket garantizada en el core de las tres placas, y el
//  protocolo que hace falta aqui es pequeno (binario, sin
//  extensiones, sin compresion).
//
//  TODO lo de red vive en su PROPIA TAREA. El hilo grafico nunca
//  llama a connect(), read() ni write(): esa es la regla que impide
//  que una pagina lenta congele la interfaz del sistema.
// =============================================================
static TaskHandle_t      gNetTask = NULL;
static volatile bool     gNetStop = false;
static volatile int      gNetState = BRN_OFF;
static SemaphoreHandle_t gMux = NULL;
static SemaphoreHandle_t gFrDone = NULL;
static QueueHandle_t     gCmdQ = NULL;
static QueueHandle_t     gUrlQ = NULL;

int flexBrowserNetState(){ return gNetState; }

typedef struct {
  uint8_t  type, ch, u1, u2, u3;
  int16_t  a, b;
  uint32_t arg;
  char     text[8];
} BrCmd;

// Frame pendiente de dibujar. Apunta DENTRO de gRx: no se copia.
static volatile bool gFrReady = false;
static FbpFrame      gFrMeta;
static uint16_t      gFrSeq = 0;
static uint16_t      gFrLastDrawn = 0;
static uint8_t       gFrCh = 0;

static inline void brLock(){ if(gMux) xSemaphoreTake(gMux, portMAX_DELAY); }
static inline void brUnlock(){ if(gMux) xSemaphoreGive(gMux); }

static void brSetError(const char* msg){
  brLock();
  snprintf(gErr, sizeof(gErr), "%s", msg ? msg : "");
  gErrMs = brHostMillis();
  brUnlock();
  gStats.errCount++;
  gNeedChromeRedraw = true;
}

static bool brPushCmd(uint8_t type, uint8_t ch, int16_t a, int16_t b,
                      uint8_t u1, uint8_t u2, uint8_t u3, uint32_t arg, const char* text){
  if(!gCmdQ) return false;
  BrCmd c; memset(&c, 0, sizeof(c));
  c.type = type; c.ch = ch; c.a = a; c.b = b;
  c.u1 = u1; c.u2 = u2; c.u3 = u3; c.arg = arg;
  if(text) snprintf(c.text, sizeof(c.text), "%s", text);
  return xQueueSend(gCmdQ, &c, 0) == pdTRUE;
}

static bool brPushNavigate(const char* url){
  if(!gUrlQ || !gCmdQ || !url) return false;
  static char slot[FLEXBR_URL_MAX];
  snprintf(slot, sizeof(slot), "%s", url);
  if(xQueueSend(gUrlQ, slot, 0) != pdTRUE) return false;
  return brPushCmd(FBP_C_NAVIGATE, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
}

// ---- socket ----
static WiFiClient*       gSock = NULL;
static WiFiClientSecure* gTls  = NULL;
static WiFiClient*       gPlain = NULL;

static void wsDisconnect(){
  if(gSock) gSock->stop();
  if(gTls){ delete gTls; gTls = NULL; }
  if(gPlain){ delete gPlain; gPlain = NULL; }
  gSock = NULL;
}

// Lee exactamente `n` bytes o falla. Cede CPU mientras espera: nunca
// hace espera activa (dispararia el watchdog de la tarea).
static bool sockReadFull(uint8_t* p, size_t n, uint32_t toMs){
  uint32_t t0 = brHostMillis();
  size_t got = 0;
  while(got < n){
    if(!gSock || !gSock->connected()) return false;
    int av = gSock->available();
    if(av > 0){
      int r = gSock->read(p + got, n - got);
      if(r > 0){ got += (size_t)r; t0 = brHostMillis(); continue; }
    }
    if(brHostMillis() - t0 > toMs) return false;
    if(gNetStop) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  gStats.bytesIn += (uint32_t)n;
  return true;
}

static bool sockWriteAll(const uint8_t* p, size_t n){
  size_t sent = 0;
  uint32_t t0 = brHostMillis();
  while(sent < n){
    if(!gSock || !gSock->connected()) return false;
    int w = gSock->write(p + sent, n - sent);
    if(w > 0){ sent += (size_t)w; t0 = brHostMillis(); continue; }
    if(brHostMillis() - t0 > 5000) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  gStats.bytesOut += (uint32_t)n;
  return true;
}

// Envia una trama WebSocket. El cliente SIEMPRE enmascara (RFC 6455
// 5.3): un servidor conforme cierra la conexion si no lo hace.
static bool wsSendFrame(uint8_t opcode, const uint8_t* data, size_t n){
  uint8_t hdr[14]; size_t h = 0;
  hdr[h++] = (uint8_t)(0x80 | opcode);
  uint8_t mask[4];
  for(int i = 0; i < 4; i++) mask[i] = (uint8_t)esp_random();
  if(n < 126) hdr[h++] = (uint8_t)(0x80 | n);
  else if(n <= 0xFFFF){
    hdr[h++] = 0x80 | 126;
    hdr[h++] = (uint8_t)(n >> 8); hdr[h++] = (uint8_t)(n & 0xFF);
  } else {
    hdr[h++] = 0x80 | 127;
    for(int i = 7; i >= 0; i--) hdr[h++] = (uint8_t)(((uint64_t)n >> (8 * i)) & 0xFF);
  }
  memcpy(hdr + h, mask, 4); h += 4;
  if(!sockWriteAll(hdr, h)) return false;
  uint8_t chunk[128];
  for(size_t i = 0; i < n; ){
    size_t c = (n - i) < sizeof(chunk) ? (n - i) : sizeof(chunk);
    for(size_t j = 0; j < c; j++) chunk[j] = (uint8_t)(data[i + j] ^ mask[(i + j) & 3]);
    if(!sockWriteAll(chunk, c)) return false;
    i += c;
  }
  return true;
}

// Lee UN mensaje de aplicacion completo (juntando fragmentos) en
// `buf`. Devuelve los bytes o -1. Responde a ping y a close por su
// cuenta. Un mensaje mas grande que el buffer se descarta ENTERO en
// vez de truncarse: medio JPEG no es medio dibujo, es basura.
static int wsReadMessage(uint8_t* buf, size_t cap, uint32_t toMs){
  size_t total = 0;
  for(;;){
    uint8_t h[2];
    if(!sockReadFull(h, 2, toMs)) return -1;
    bool fin = (h[0] & 0x80) != 0;
    uint8_t op = (uint8_t)(h[0] & 0x0F);
    bool masked = (h[1] & 0x80) != 0;
    uint64_t len = (uint64_t)(h[1] & 0x7F);
    if(len == 126){
      uint8_t e[2]; if(!sockReadFull(e, 2, toMs)) return -1;
      len = ((uint64_t)e[0] << 8) | e[1];
    } else if(len == 127){
      uint8_t e[8]; if(!sockReadFull(e, 8, toMs)) return -1;
      len = 0; for(int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    if(masked){ uint8_t m[4]; if(!sockReadFull(m, 4, toMs)) return -1; }  // no deberia venir
    if(len > (uint64_t)(FBP_PAYLOAD_MAX + FBP_HDR_SIZE + 1024)) return -1;

    if(op == 0x8){                                     // CLOSE
      wsSendFrame(0x8, NULL, 0);
      return -1;
    }
    if(op == 0x9){                                     // PING -> PONG con el mismo cuerpo
      uint8_t pp[125]; size_t pl = (size_t)(len < sizeof(pp) ? len : sizeof(pp));
      if(pl && !sockReadFull(pp, pl, toMs)) return -1;
      for(uint64_t rem = len - pl; rem > 0; ){
        uint8_t junk[64]; size_t c = (size_t)(rem < sizeof(junk) ? rem : sizeof(junk));
        if(!sockReadFull(junk, c, toMs)) return -1;
        rem -= c;
      }
      wsSendFrame(0xA, pp, pl);
      continue;
    }
    if(op == 0xA){                                     // PONG: se descarta el cuerpo
      for(uint64_t rem = len; rem > 0; ){
        uint8_t junk[64]; size_t c = (size_t)(rem < sizeof(junk) ? rem : sizeof(junk));
        if(!sockReadFull(junk, c, toMs)) return -1;
        rem -= c;
      }
      continue;
    }
    if(total + (size_t)len > cap){                     // no cabe: se tira entero
      for(uint64_t rem = len; rem > 0; ){
        uint8_t junk[128]; size_t c = (size_t)(rem < sizeof(junk) ? rem : sizeof(junk));
        if(!sockReadFull(junk, c, toMs)) return -1;
        rem -= c;
      }
      gStats.framesBad++;
      if(fin) return -2;                               // -2 = mensaje descartado, sesion viva
      continue;
    }
    if(len && !sockReadFull(buf + total, (size_t)len, toMs)) return -1;
    total += (size_t)len;
    if(fin) return (int)total;
    (void)op;
  }
}

static bool wsHandshake(const char* host, uint16_t port, const char* path){
  uint8_t nonce[16];
  for(int i = 0; i < 16; i++) nonce[i] = (uint8_t)esp_random();
  char key[32];
  if(flexBrBase64(nonce, 16, key, sizeof(key)) < 0) return false;

  char req[512];
  int n = snprintf(req, sizeof(req),
      "GET %s HTTP/1.1\r\n"
      "Host: %s:%u\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: %s\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Protocol: fbp.v1\r\n"
      "User-Agent: FlexOS-Browser/1.0 (%s)\r\n"
      "\r\n", path, host, (unsigned)port, key, FLEXBR_PLAT_NAME);
  if(n <= 0 || n >= (int)sizeof(req)) return false;
  if(!sockWriteAll((const uint8_t*)req, (size_t)n)) return false;

  char want[40];
  if(!flexBrWsAccept(key, want, sizeof(want))) return false;

  // Respuesta: se lee linea a linea hasta la linea en blanco, con un
  // tope duro de bytes para que un servidor hostil no pueda tenernos
  // leyendo cabeceras para siempre.
  char line[256];
  bool ok101 = false, okAccept = false;
  int lines = 0;
  for(;;){
    size_t li = 0;
    for(;;){
      uint8_t c;
      if(!sockReadFull(&c, 1, 6000)) return false;
      if(c == '\n') break;
      if(c != '\r' && li + 1 < sizeof(line)) line[li++] = (char)c;
    }
    line[li] = 0;
    if(li == 0) break;                                  // fin de cabeceras
    if(++lines > 40) return false;
    if(lines == 1){
      ok101 = (strstr(line, " 101") != NULL);
    } else if(!strncasecmp(line, "Sec-WebSocket-Accept:", 21)){
      const char* v = line + 21;
      while(*v == ' ') v++;
      okAccept = (strcmp(v, want) == 0);
    }
  }
  return ok101 && okAccept;
}

// ---- manejo de mensajes FBP recibidos ----
static void brOnState(const FbpState* s, uint8_t ch){
  int ti = (ch >= 1 && ch <= FLEXBR_MAX_TABS) ? (ch - 1) : gTab;
  if(ti < 0 || ti >= FLEXBR_MAX_TABS || !gTabs[ti].used) return;
  brLock();
  gTabs[ti].stateFlags = s->flags;
  gTabs[ti].progress = s->progress;
  if(s->title[0]) snprintf(gTabs[ti].title, FLEXBR_TITLE_MAX, "%s", s->title);
  if(s->url[0])   snprintf(gTabs[ti].url, FLEXBR_URL_MAX, "%s", s->url);
  brUnlock();
  // Si la pagina remota ha puesto el foco en un campo de texto, se abre
  // el teclado del sistema en modo "escribir en la pagina": las pulsaciones
  // van directas al servicio y NO se guardan en el dispositivo (ni un
  // buffer de lo tecleado, que es la unica forma seria de no filtrar una
  // contrasena escrita en un formulario).
  if(ti == gTab){
    bool editing = (s->flags & FBP_ST_EDITING) != 0;
    if(editing && gEditTarget == BRE_NONE){ gEditTarget = BRE_PAGEKEY; gEdit[0] = 0; gNeedFullRedraw = true; }
    else if(!editing && gEditTarget == BRE_PAGEKEY){ gEditTarget = BRE_NONE; gNeedFullRedraw = true; }
  }
  gNeedChromeRedraw = true;
}

static void brOnNavigated(const char* url, uint8_t ch){
  int ti = (ch >= 1 && ch <= FLEXBR_MAX_TABS) ? (ch - 1) : gTab;
  if(ti < 0 || ti >= FLEXBR_MAX_TABS || !gTabs[ti].used) return;
  // El servicio ya valido el destino, pero se vuelve a comprobar aqui:
  // dos barreras independientes. Si una redireccion acabo en un destino
  // que este dispositivo no acepta, se corta y se avisa.
  if(flexBrCheckUrl(url, &gSt) != BRB_NONE){
    brSetError("Redirecci\xC3\xB3n a un destino bloqueado");
    brPushCmd(FBP_C_STOP, (uint8_t)(ti + 1), 0, 0, 0, 0, 0, 0, NULL);
    return;
  }
  brLock();
  snprintf(gTabs[ti].url, FLEXBR_URL_MAX, "%s", url);
  brUnlock();
  brHistoryAdd(url, gTabs[ti].title);
  gStats.navCount++;
  gNeedChromeRedraw = true;
}

static void brOnFrame(const FbpHeader* h, const uint8_t* payload){
  FbpFrame f;
  if(!fbpParseFrame(payload, h->len, &f)){ gStats.framesBad++; return; }
  // Contrapresion: si la interfaz aun no ha dibujado el frame anterior,
  // NO se acumula otro. El de ahora es mas util que el de antes.
  if(gFrReady){ gStats.framesDropped++; }
  if(gFrDone) xSemaphoreTake(gFrDone, 0);      // limpia un aviso viejo
  gFrMeta = f;
  gFrSeq = h->seq;
  gFrCh = h->channel;
  gFrReady = true;
  // Espera a que la interfaz lo consuma: mientras tanto no se lee mas
  // del socket, asi que la ventana TCP frena al servidor sola. Es la
  // contrapresion real, sin colas que crezcan.
  uint32_t t0 = brHostMillis();
  while(gFrReady && !gNetStop){
    if(gFrDone && xSemaphoreTake(gFrDone, pdMS_TO_TICKS(50)) == pdTRUE) break;
    if(brHostMillis() - t0 > 5000){
      // La interfaz no responde (app minimizada, dialogo del sistema...).
      // Se cierra la sesion y se reconecta: es lo unico que NO puede
      // dejar el buffer a medio reutilizar mientras alguien lo lee.
      gNetState = BRN_ERROR;
      wsDisconnect();
      return;
    }
  }
}

static void brOnMedia(const uint8_t* p, uint32_t n){
#if FLEXBR_MEDIA_ON
  FbpMedia m;
  if(!fbpParseMedia(p, n, &m)) return;
  brLock();
  gMedia = m;
  gMediaAvail = (m.kind != 0);
  brUnlock();
  gNeedChromeRedraw = true;
#else
  (void)p; (void)n;
#endif
}

static void brOnDownload(const uint8_t* p, uint32_t n){
  // Solo se registra el anuncio: este dispositivo no descarga ficheros
  // (no hay espacio ni gestor remoto). flex://downloads lo explica.
  if(n < 6) return;
  uint32_t o = 0;
  char name[48]; char url[FLEXBR_URL_MAX];
  uint16_t l = (uint16_t)(p[0] | (p[1] << 8)); o = 2;
  if(o + l > n) return;
  size_t c = l < sizeof(name) - 1 ? l : sizeof(name) - 1;
  memcpy(name, p + o, c); name[c] = 0; o += l;
  if(o + 4 > n) return;
  uint32_t size = (uint32_t)p[o] | ((uint32_t)p[o+1] << 8) | ((uint32_t)p[o+2] << 16) | ((uint32_t)p[o+3] << 24);
  o += 4;
  url[0] = 0;
  if(o + 2 <= n){
    uint16_t ul = (uint16_t)(p[o] | (p[o+1] << 8)); o += 2;
    if(o + ul <= n){ size_t uc = ul < sizeof(url) - 1 ? ul : sizeof(url) - 1; memcpy(url, p + o, uc); url[uc] = 0; }
  }
  brLock();
  if(gDlN < BR_DL_MAX){
    snprintf(gDl[gDlN].name, sizeof(gDl[gDlN].name), "%s", name);
    snprintf(gDl[gDlN].url, sizeof(gDl[gDlN].url), "%s", url);
    gDl[gDlN].size = size;
    gDlN++;
  }
  brUnlock();
}

static void brHandleMessage(int len){
  FbpHeader h;
  if(len < FBP_HDR_SIZE || !fbpReadHeader(gRx, (size_t)len, &h)){
    gStats.framesBad++;
    return;
  }
  if(h.len != (uint32_t)(len - FBP_HDR_SIZE)){ gStats.framesBad++; return; }
  const uint8_t* p = gRx + FBP_HDR_SIZE;

  switch(h.type){
    case FBP_S_WELCOME: {
      FbpWelcome w;
      if(!fbpParseWelcome(p, h.len, &w)){ brSetError(flexBrErrorText(FBP_ERR_PROTOCOL)); gNetState = BRN_ERROR; return; }
      gNetState = BRN_READY;
      gNeedFullRedraw = true;
      break;
    }
    case FBP_S_STATE: { FbpState s; if(fbpParseState(p, h.len, &s)) brOnState(&s, h.channel); break; }
    case FBP_S_FRAME:   brOnFrame(&h, p); break;
    case FBP_S_MEDIA:   brOnMedia(p, h.len); break;
    case FBP_S_DOWNLOAD:brOnDownload(p, h.len); break;
    case FBP_S_PONG:    gStats.rttMs = brHostMillis() - gStats.rttMs; break;
    case FBP_S_NAVIGATED: {
      char url[FLEXBR_URL_MAX];
      if(h.len >= 2){
        uint16_t l = (uint16_t)(p[0] | (p[1] << 8));
        if(2u + l <= h.len){
          size_t c = l < sizeof(url) - 1 ? l : sizeof(url) - 1;
          memcpy(url, p + 2, c); url[c] = 0;
          for(size_t i = 0; i < c; i++) if((unsigned char)url[i] < 0x20) url[i] = ' ';
          brOnNavigated(url, h.channel);
        }
      }
      break;
    }
    case FBP_S_ERROR: {
      FbpError e;
      if(fbpParseError(p, h.len, &e)){
        // Se muestra SIEMPRE el texto propio segun el codigo; el mensaje
        // del servidor solo se anade si es corto. Un servicio
        // comprometido no puede escribir lo que quiera en la interfaz.
        char msg[FLEXBR_ERRMSG_MAX];
        snprintf(msg, sizeof(msg), "%s", flexBrErrorText(e.code));
        brSetError(msg);
        if(e.code == FBP_ERR_AUTH || e.code == FBP_ERR_EXPIRED) gNetState = BRN_ERROR;
      }
      break;
    }
    default: break;
  }
}

// ---- envio de los comandos encolados ----
static bool brSendPending(uint8_t* tx, size_t txCap){
  BrCmd c;
  static uint16_t seq = 1;
  while(gCmdQ && xQueueReceive(gCmdQ, &c, 0) == pdTRUE){
    int n = -1;
    switch(c.type){
      case FBP_C_NAVIGATE: {
        char url[FLEXBR_URL_MAX];
        if(!gUrlQ || xQueueReceive(gUrlQ, url, 0) != pdTRUE) break;
        n = fbpBuildNavigate(tx, txCap, seq++, c.ch, url);
        break;
      }
      case FBP_C_POINTER: n = fbpBuildPointer(tx, txCap, seq++, c.ch, c.u1, (uint16_t)c.a, (uint16_t)c.b, c.u2); break;
      case FBP_C_SCROLL:  n = fbpBuildScroll(tx, txCap, seq++, c.ch, c.a, c.b); break;
      case FBP_C_KEY:     n = fbpBuildKey(tx, txCap, seq++, c.ch, c.u1, c.u2, c.u3, c.text); break;
      case FBP_C_VIEWPORT:n = fbpBuildViewport(tx, txCap, seq++, c.ch, (uint16_t)c.a, (uint16_t)c.b, c.u1, c.u2, c.u3); break;
      case FBP_C_MEDIA:   n = fbpBuildMedia(tx, txCap, seq++, c.ch, c.u1, c.arg); break;
      default:            n = fbpBuildSimple(tx, txCap, seq++, c.ch, c.type); break;
    }
    if(n > 0 && !wsSendFrame(0x2, tx, (size_t)n)) return false;
  }
  return true;
}

static void brNetTask(void*){
  uint8_t tx[FLEXBR_URL_MAX + 64];
  uint32_t backoff = 1000;
  uint32_t lastPing = 0, lastAck = 0;

  while(!gNetStop){
    // ---- conexion ----
    if(gNetState != BRN_READY && gNetState != BRN_HANDSHAKE){
      if(!brHostOnline()){ gNetState = BRN_NOWIFI; vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
      if(!gSt.server[0]){ gNetState = BRN_OFF; vTaskDelay(pdMS_TO_TICKS(1000)); continue; }

      bool tls; char host[FLEXBR_HOST_MAX]; uint16_t port; char path[128];
      if(!flexBrParseWsUrl(gSt.server, &tls, host, sizeof(host), &port, path, sizeof(path))){
        brSetError("La direcci\xC3\xB3n del servidor no es v\xC3\xA1lida");
        gNetState = BRN_ERROR;
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
#if FLEXBR_REQUIRE_TLS
      if(!tls && !gSt.allowInsecure){
        brSetError("El servidor debe usar wss:// (TLS)");
        gNetState = BRN_ERROR;
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
#endif
      // Aqui se compara contra FLEXBR_TLS_HEAP y no contra
      // FLEXBR_MIN_FREE_HEAP: los buffers del navegador ya estan
      // reservados, asi que lo que queda por caber es solo TLS.
      if(brHostFreeHeap() < FLEXBR_TLS_HEAP){
        brSetError("Memoria insuficiente para abrir la sesi\xC3\xB3n");
        gNetState = BRN_ERROR;
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }

      gNetState = BRN_CONNECTING;
      wsDisconnect();
      if(tls){
        gTls = new WiFiClientSecure();
        if(!gTls){ gNetState = BRN_ERROR; vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        // Sin certificado fijado se acepta cualquiera: comodo para
        // montar el servicio en casa, NO para produccion. Ver
        // server/README.md ("Certificados").
        gTls->setInsecure();
        gTls->setTimeout(10);
        gSock = gTls;
      } else {
        gPlain = new WiFiClient();
        if(!gPlain){ gNetState = BRN_ERROR; vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        gSock = gPlain;
      }
      if(!gSock->connect(host, port)){
        wsDisconnect();
        gNetState = BRN_ERROR;
        brSetError("No se pudo conectar con el servicio");
        gStats.reconnects++;
        vTaskDelay(pdMS_TO_TICKS(backoff));
        if(backoff < 16000) backoff *= 2;         // retroceso exponencial acotado
        continue;
      }
      gNetState = BRN_HANDSHAKE;
      if(!wsHandshake(host, port, path)){
        wsDisconnect();
        gNetState = BRN_ERROR;
        brSetError("El servicio rechaz\xC3\xB3 la conexi\xC3\xB3n");
        gStats.reconnects++;
        vTaskDelay(pdMS_TO_TICKS(backoff));
        if(backoff < 16000) backoff *= 2;
        continue;
      }
      // HELLO con la credencial del dispositivo y el tamano real.
      int cx, cy, cw, chh; brHostContentRect(&cx, &cy, &cw, &chh);
      int n = fbpBuildHello(tx, sizeof(tx), 0, gSt.token, brHostDeviceName(),
                            (uint16_t)cw, (uint16_t)brContentPageH(),
                            gSt.quality, gSt.profile, gCaps);
      if(n <= 0 || !wsSendFrame(0x2, tx, (size_t)n)){
        wsDisconnect(); gNetState = BRN_ERROR; continue;
      }
      backoff = 1000;
      lastPing = lastAck = brHostMillis();
    }

    // ---- comandos salientes ----
    if(!brSendPending(tx, sizeof(tx))){
      wsDisconnect(); gNetState = BRN_ERROR; gStats.reconnects++; continue;
    }

    // ---- mensaje entrante ----
    int r = wsReadMessage(gRx, gRxCap, 120);
    if(r == -1){
      if(gSock && !gSock->connected()){
        wsDisconnect(); gNetState = BRN_ERROR; gStats.reconnects++;
      }
    } else if(r >= 0){
      brHandleMessage(r);
    }

    uint32_t now = brHostMillis();
    if(gNetState == BRN_READY){
      if(now - lastPing > 15000){
        lastPing = now;
        gStats.rttMs = now;
        int n = fbpBuildSimple(tx, sizeof(tx), 0, 0, FBP_C_PING);
        if(n > 0) wsSendFrame(0x2, tx, (size_t)n);
      }
      if(now - lastAck > 250){
        lastAck = now;
        int n = fbpBuildAck(tx, sizeof(tx), 0, gFrLastDrawn, gFrReady ? 1 : 0);
        if(n > 0) wsSendFrame(0x2, tx, (size_t)n);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }

  wsDisconnect();
  gNetState = BRN_OFF;
  gNetTask = NULL;
  vTaskDelete(NULL);
}

// =============================================================
//  5) FRAMES: decodificacion y volcado
//  -------------------------------------------------------------
//  PRESUPUESTO DE TIEMPO. Decodificar una banda es la operacion mas
//  larga del hilo grafico, y por eso el servicio manda BANDAS y no
//  paginas enteras: una banda de unos cientos de filas se decodifica
//  en decenas de milisegundos y entre banda y banda se vuelve a
//  atender el tactil. El presupuesto de abajo NO es el objetivo, es
//  el TOPE: si una imagen corrupta o absurdamente grande lo supera,
//  la decodificacion se corta y se pide un frame nuevo, en vez de
//  dejar el sistema colgado. En funcionamiento normal no se alcanza.
// =============================================================
#define BR_DECODE_BUDGET_MS 700

// -------------------------------------------------------------
//  ESCALADO AL VOLCAR
//  ------------------------------------------------------------
//  El servicio puede rasterizar la pagina a MENOS pixeles de los que
//  ocupa en pantalla (ajuste "escala del viewport"), y aqui se estira
//  al volcarla. No es un adorno:
//    · En Flex OS Pro el lienzo logico es 480 de ancho pero el panel
//      fisico solo tiene 240. Pidiendo el 50 % se envia justo lo que
//      el panel puede ensenar: la mitad de bytes y la mitad de trabajo
//      de decodificacion, con el MISMO resultado en pantalla.
//    · En el perfil "ahorro de datos" hace lo propio en cualquier placa.
//  La ampliacion es por vecino mas proximo: es lo unico que se puede
//  hacer fila a fila sin guardar la imagen entera, y para texto grande
//  y fondos planos (que es de lo que va una pagina) se ve bien.
// -------------------------------------------------------------
typedef struct {
  int  dx, dy;              // destino en el lienzo
  int  clipW, clipH;        // recorte disponible
  int  dstW, dstH;          // tamano al que hay que estirar
  int  srcW, srcH;          // tamano real de la imagen
  uint16_t* line;           // fila ya estirada (NULL = sin estirar)
  uint32_t t0, budget;
  bool aborted;
  int  nextDy;              // primera fila de destino aun sin pintar
} BrBlitCtx;

static bool brBlitRowCb(void* user, int y, int w, const uint16_t* px){
  BrBlitCtx* c = (BrBlitCtx*)user;

  if(!c->line || (c->dstW == c->srcW && c->dstH == c->srcH)){
    if(y >= c->clipH) return true;
    int ww = w < c->clipW ? w : c->clipW;
    brHostBlitRow(c->dx, c->dy + y, ww, px);
  } else {
    // Horizontal: vecino mas proximo a dstW.
    int outW = c->dstW < c->clipW ? c->dstW : c->clipW;
    if(outW > gScaleLineCap) outW = gScaleLineCap;
    for(int i = 0; i < outW; i++){
      int sx = (int)((long)i * c->srcW / c->dstW);
      if(sx >= w) sx = w - 1;
      c->line[i] = px[sx];
    }
    // Vertical: la fila de origen `y` cubre [y*dstH/srcH, (y+1)*dstH/srcH).
    int y0 = (int)((long)y * c->dstH / c->srcH);
    int y1 = (int)((long)(y + 1) * c->dstH / c->srcH);
    if(y1 <= y0) y1 = y0 + 1;
    if(y0 < c->nextDy) y0 = c->nextDy;            // sin huecos ni repintados
    for(int oy = y0; oy < y1 && oy < c->clipH; oy++)
      brHostBlitRow(c->dx, c->dy + oy, outW, c->line);
    if(y1 > c->nextDy) c->nextDy = y1;
  }

  // Cada 32 filas se comprueba el presupuesto de tiempo. Un frame
  // corrupto que se decodifique eternamente no puede colgar el sistema.
  if((y & 31) == 0 && (brHostMillis() - c->t0) > c->budget){ c->aborted = true; return false; }
  return true;
}

// `dstW`/`dstH` son el tamano que la region debe ocupar en pantalla; si
// la imagen viene mas pequena, se estira. Con dstW <= 0 se dibuja 1:1.
static int brDecodeInto(const uint8_t* jpg, size_t n, int dx, int dy, int cw, int chh,
                        int dstW, int dstH){
  FlexJpegInfo probe;
  if(flexJpegProbe(jpg, n, &probe) != FLEXJPG_OK) return FLEXJPG_ERR_BADMARKER;

  BrBlitCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.dx = dx; ctx.dy = dy; ctx.clipW = cw; ctx.clipH = chh;
  ctx.srcW = probe.width; ctx.srcH = probe.height;
  ctx.dstW = (dstW > 0) ? dstW : probe.width;
  ctx.dstH = (dstH > 0) ? dstH : probe.height;
  ctx.line = NULL;
  ctx.t0 = brHostMillis(); ctx.budget = BR_DECODE_BUDGET_MS; ctx.aborted = false;

  if(ctx.dstW != ctx.srcW || ctx.dstH != ctx.srcH){
    if(gScaleLine && gScaleLineCap > 0) ctx.line = gScaleLine;
    else { ctx.dstW = ctx.srcW; ctx.dstH = ctx.srcH; }   // sin buffer: 1:1
  }

  FlexJpegInfo info;
  // El tope de pixeles protege contra una imagen absurda: como mucho el
  // area visible mas un margen. Por encima, el decodificador la reduce.
  uint32_t maxPx = (uint32_t)(cw > 0 ? cw : 1) * (uint32_t)(chh > 0 ? chh : 1) * 2u;
  int rc = flexJpegDecode(jpg, n, ctx.srcW, 0, maxPx, &info, brBlitRowCb, &ctx, NULL, NULL);
  uint32_t ms = brHostMillis() - ctx.t0;
  gStats.decodeMsLast = ms;
  if(ms > gStats.decodeMsMax) gStats.decodeMsMax = ms;
  if(ctx.aborted) return FLEXJPG_ERR_ABORTED;
  return rc;
}

// Guarda la banda en la cache para poder repintar sin pedir nada.
static void brCacheStrip(const FbpFrame* f){
  if(!gKey || !gKeyCap) return;
  if(f->flags & FBP_FR_KEYFRAME){ gKeyUsed = 0; gStripN = 0; gStripsValid = true; gStripsStale = false; }
  if(!gStripsValid) return;
  if(gStripN >= BR_STRIPS_MAX || gKeyUsed + f->imgLen > gKeyCap){
    gStripsValid = false; gStripN = 0; gKeyUsed = 0;   // no cabe: se renuncia a la cache
    return;
  }
  memcpy(gKey + gKeyUsed, f->img, f->imgLen);
  gStrips[gStripN].off = (uint32_t)gKeyUsed;
  gStrips[gStripN].len = f->imgLen;
  gStrips[gStripN].x = f->x; gStrips[gStripN].y = f->y;
  gStrips[gStripN].w = f->w; gStrips[gStripN].h = f->h;
  gStripN++;
  gKeyUsed += f->imgLen;
}

// Altura util de la zona de pagina (contenido menos la barra propia).
static int brContentPageH(){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int ph = h - brChromeH();
  return ph < 32 ? 32 : ph;
}
static void brPageRect(int* px, int* py, int* pw, int* ph){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  *px = x; *py = y + brChromeH(); *pw = w; *ph = h - brChromeH();
  if(*ph < 32) *ph = 32;
}

// Rectangulo del video dentro del reproductor a pantalla completa.
static void brVideoRect(int* vx, int* vy, int* vw, int* vh){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int ctrl = 96;                                   // franja de controles
  int availH = h - ctrl - 40;
  if(availH < 60) availH = 60;
  int rw = w, rh = availH;
  // Se respeta 16:9 salvo que el servicio diga otra proporcion.
  int aw = gMedia.w ? gMedia.w : 16, ah = gMedia.h ? gMedia.h : 9;
  if((long)rw * ah > (long)rh * aw) rw = (int)((long)rh * aw / ah);
  else                              rh = (int)((long)rw * ah / aw);
  *vx = x + (w - rw) / 2;
  *vy = y + 40 + (availH - rh) / 2;
  *vw = rw; *vh = rh;
}

// Dibuja el frame pendiente. Se llama SOLO desde el hilo grafico.
static void brDrawPendingFrame(){
  if(!gFrReady) return;
  int px, py, pw, ph; brPageRect(&px, &py, &pw, &ph);
  const FbpFrame f = gFrMeta;
  bool drawn = false;
  if(gFrCh == FBP_CH_MEDIA){
    // Fotograma del reproductor: va a su propio rectangulo y NUNCA al
    // area de pagina, aunque llegue tarde y la vista ya haya cambiado.
    if(gView == BRV_MEDIA){
      int vx, vy, vw, vh; brVideoRect(&vx, &vy, &vw, &vh);
      int rc = brDecodeInto(f.img, f.imgLen, vx, vy, vw, vh, vw, vh);
      if(rc == FLEXJPG_OK){ gStats.framesOk++; drawn = true; brHostFlush(vy, vy + vh - 1); }
      else gStats.framesBad++;
    } else {
      gStats.framesDropped++;
    }
    gFrLastDrawn = gFrSeq;
    gFrReady = false;
    if(gFrDone) xSemaphoreGive(gFrDone);
    return;
  }
  if(gView == BRV_PAGE && gTabs[gTab].used && gTabs[gTab].view == BRV_PAGE){
    int dy = py + (int)f.y;
    int dx = px + (int)f.x;
    if((int)f.y < ph && (int)f.x < pw){
      // f.w/f.h son el tamano que la region ocupa EN PANTALLA; la imagen
      // puede venir mas pequena si el viewport va a escala reducida.
      int rc = brDecodeInto(f.img, f.imgLen, dx, dy, pw - (int)f.x, ph - (int)f.y,
                            (int)f.w, (int)f.h);
      if(rc == FLEXJPG_OK){ gStats.framesOk++; drawn = true; gTabs[gTab].hasFrame = true; }
      else {
        gStats.framesBad++;
        // Un frame ilegible no se deja a medias en pantalla: se pide uno
        // completo. Si el fallo se repite, el contador de la telemetria
        // lo delata (flex://about) en vez de tapar el problema.
        brPushCmd(FBP_C_REQ_FRAME, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
      }
    }
    if(drawn){
      int y1 = dy + (int)f.h - 1;
      int lim = py + ph - 1;
      brHostFlush(dy, y1 < lim ? y1 : lim);
    }
  }
  if(drawn){
    if(f.flags & FBP_FR_KEYFRAME) brCacheStrip(&f);
    else if(gStripsValid) gStripsStale = true;
  }
  gFrLastDrawn = gFrSeq;
  gFrReady = false;
  if(gFrDone) xSemaphoreGive(gFrDone);
}

// Repinta la pagina desde la cache. Si no hay cache, pide un frame.
static void brRepaintPageFromCache(){
  int px, py, pw, ph; brPageRect(&px, &py, &pw, &ph);
  brHostFillRect(px, py, pw, ph, brHostColor(BRC_PAGE));
  if(gStripsValid && gStripN > 0 && gKey){
    for(int i = 0; i < gStripN; i++){
      if(gStrips[i].y >= ph) continue;
      brDecodeInto(gKey + gStrips[i].off, gStrips[i].len,
                   px + gStrips[i].x, py + gStrips[i].y,
                   pw - gStrips[i].x, ph - gStrips[i].y,
                   gStrips[i].w, gStrips[i].h);
    }
    if(gStripsStale) brPushCmd(FBP_C_REQ_FRAME, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
  } else {
    brPushCmd(FBP_C_REQ_FRAME, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
  }
  brHostFlush(py, py + ph - 1);
}

// =============================================================
//  6) NAVEGACION Y PESTANAS
// =============================================================
static int brTabCount(){
  int n = 0;
  for(int i = 0; i < FLEXBR_MAX_TABS; i++) if(gTabs[i].used) n++;
  return n;
}

static void brTabInit(int i){
  memset(&gTabs[i], 0, sizeof(BrTab));
  gTabs[i].used = true;
  gTabs[i].remoteId = (uint8_t)(i + 1);
  gTabs[i].view = BRV_INTERNAL;
  gTabs[i].page = BRP_NEWTAB;
  gTabs[i].lastUseMs = brHostMillis();
  snprintf(gTabs[i].url, FLEXBR_URL_MAX, "flex://newtab");
  snprintf(gTabs[i].title, FLEXBR_TITLE_MAX, "%s", flexBrPageTitle(BRP_NEWTAB));
}

static int brTabNew(){
  int lim = brTabLimit();
  if(brTabCount() >= lim){
    // Se cierra la MENOS usada en vez de negarse: es lo que hace un
    // navegador cuando el limite es de memoria, no de capricho.
    int oldest = -1; uint32_t best = 0xFFFFFFFFu;
    for(int i = 0; i < FLEXBR_MAX_TABS; i++)
      if(gTabs[i].used && i != gTab && gTabs[i].lastUseMs < best){ best = gTabs[i].lastUseMs; oldest = i; }
    if(oldest < 0) return -1;
    brPushCmd(FBP_C_TAB_CLOSE, (uint8_t)(oldest + 1), 0, 0, 0, 0, 0, 0, NULL);
    gTabs[oldest].used = false;
  }
  for(int i = 0; i < FLEXBR_MAX_TABS; i++){
    if(!gTabs[i].used){
      brTabInit(i);
      brPushCmd(FBP_C_TAB_NEW, (uint8_t)(i + 1), 0, 0, 0, 0, 0, 0, NULL);
      return i;
    }
  }
  return -1;
}

static void brTabClose(int i){
  if(i < 0 || i >= FLEXBR_MAX_TABS || !gTabs[i].used) return;
  if(brTabCount() <= 1){                 // la ultima no se cierra: se vacia
    brPushCmd(FBP_C_NAVIGATE, (uint8_t)(i + 1), 0, 0, 0, 0, 0, 0, NULL);
    brTabInit(i);
    gView = BRV_INTERNAL;
    gNeedFullRedraw = true;
    return;
  }
  brPushCmd(FBP_C_TAB_CLOSE, (uint8_t)(i + 1), 0, 0, 0, 0, 0, 0, NULL);
  gTabs[i].used = false;
  if(gTab == i){
    for(int k = 0; k < FLEXBR_MAX_TABS; k++) if(gTabs[k].used){ gTab = k; break; }
    gView = gTabs[gTab].view;
  }
  gNeedFullRedraw = true;
}

static void brTabSelect(int i){
  if(i < 0 || i >= FLEXBR_MAX_TABS || !gTabs[i].used || i == gTab) return;
  gTab = i;
  gTabs[i].lastUseMs = brHostMillis();
  gView = gTabs[i].view;
  brPushCmd(FBP_C_TAB_SELECT, (uint8_t)(i + 1), 0, 0, 0, 0, 0, 0, NULL);
  gNeedFullRedraw = true;
}

static void brOpenInternal(int page){
  BrTab* t = &gTabs[gTab];
  t->view = BRV_INTERNAL;
  t->page = page;
  t->progress = 0;
  t->stateFlags = 0;
  snprintf(t->url, FLEXBR_URL_MAX, "flex://%s", flexBrPageName(page));
  snprintf(t->title, FLEXBR_TITLE_MAX, "%s", flexBrPageTitle(page));
  gView = BRV_INTERNAL;
  gIntScroll = 0;
  gNeedFullRedraw = true;
}

static void brNavigateTo(const char* url, bool addHistory){
  if(!url || !url[0]) return;
  int page = flexBrInternalPage(url);
  if(page != BRP_NONE){ brOpenInternal(page); return; }

  if(!(gCaps & FLEXBR_CAP_REMOTE)){
    brSetError("Sin sesi\xC3\xB3n remota: revisa Ajustes");
    brOpenInternal(BRP_OFFLINE);
    return;
  }
  if(!brHostOnline()){
    brOpenInternal(BRP_OFFLINE);
    return;
  }
  BrTab* t = &gTabs[gTab];
  t->view = BRV_PAGE;
  t->page = BRP_NONE;
  t->progress = 1;
  t->stateFlags = FBP_ST_LOADING;
  t->hasFrame = false;
  t->lastUseMs = brHostMillis();
  snprintf(t->url, FLEXBR_URL_MAX, "%s", url);
  flexBrUrlLabel(url, t->title, FLEXBR_TITLE_MAX);
  gStripsValid = false; gStripN = 0; gKeyUsed = 0; gStripsStale = false;
  gMediaAvail = false;
  gView = BRV_PAGE;
  if(addHistory) brHistoryAdd(url, NULL);
  brPushNavigate(url);
  gNeedFullRedraw = true;
}

// Confirmacion del omnibox: TODO pasa por el nucleo portable.
static void brCommitOmnibox(){
  BrResolved r;
  int kind = flexBrResolveInput(gEdit, &gSt, &r);
  gEditTarget = BRE_NONE;
  switch(kind){
    case BRI_EMPTY:
      gView = gTabs[gTab].view;
      gNeedFullRedraw = true;
      break;
    case BRI_INTERNAL: brOpenInternal(r.page); break;
    case BRI_URL:
    case BRI_SEARCH:   brNavigateTo(r.url, true); break;
    case BRI_BLOCKED:
      brSetError(flexBrBlockReason(r.block));
      gView = gTabs[gTab].view;
      gNeedFullRedraw = true;
      break;
  }
}

// =============================================================
//  7) INTERFAZ: DIBUJO
//  -------------------------------------------------------------
//  Toda la maquetacion sale de brHostContentRect(), que en pantalla
//  completa es la ventana de la app y dentro de una ventana de Modo
//  PC/DeX es el area de cliente. Por eso el navegador se adapta a los
//  dos sin una sola rama especifica: es el mismo patron que siguen
//  las demas apps APP_FLEX del sistema.
// =============================================================
static int brToolH(){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int t = h / 13;
  if(t < 40) t = 40;                 // objetivo tactil minimo
  if(t > 56) t = 56;
  if(t > h / 4) t = h / 4;
  return t;
}
static bool brTabBarShown(){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  return (gCaps & FLEXBR_CAP_TABS) && w >= 360 && h >= 420 && brTabLimit() > 1;
}
static int brTabBarH(){
  if(!brTabBarShown()) return 0;
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int t = h / 22;
  if(t < 28) t = 28;
  if(t > 38) t = 38;
  return t;
}
#define BR_PROG_H 3
static int brChromeH(){ return brTabBarH() + brToolH() + BR_PROG_H; }

// Botones de la barra: [atras] [adelante] [recargar] [omnibox] [menu]
enum { BRBTN_BACK = 0, BRBTN_FWD, BRBTN_RELOAD, BRBTN_OMNI, BRBTN_MENU, BRBTN_N };
static bool brBtnRect(int which, int* bx, int* by, int* bw, int* bh){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int th = brToolH();
  int top = y + brTabBarH();
  int pad = 4;
  int s = th - 2 * pad;                        // lado de los botones cuadrados
  bool showFwd = (w >= 320);
  int nLeft = showFwd ? 3 : 2;
  int leftW = nLeft * (s + pad);
  int rightW = s + pad;
  *by = top + pad; *bh = s;
  switch(which){
    case BRBTN_BACK:   *bx = x + pad;                 *bw = s; return true;
    case BRBTN_FWD:    if(!showFwd) return false;
                       *bx = x + pad + (s + pad);     *bw = s; return true;
    case BRBTN_RELOAD: *bx = x + pad + (nLeft - 1) * (s + pad); *bw = s; return true;
    case BRBTN_MENU:   *bx = x + w - pad - s;         *bw = s; return true;
    case BRBTN_OMNI: {
      int ox = x + pad + leftW;
      int ow = w - pad - leftW - rightW - pad;
      if(ow < 60) ow = 60;
      *bx = ox; *bw = ow;
      return true;
    }
  }
  return false;
}

static void brDrawChevron(int cx, int cy, int s, bool right, uint16_t c){
  float k = s * 0.30f;
  if(right){
    brHostStroke(cx - k * 0.5f, cy - k, cx + k * 0.5f, cy, 2.4f, c);
    brHostStroke(cx + k * 0.5f, cy, cx - k * 0.5f, cy + k, 2.4f, c);
  } else {
    brHostStroke(cx + k * 0.5f, cy - k, cx - k * 0.5f, cy, 2.4f, c);
    brHostStroke(cx - k * 0.5f, cy, cx + k * 0.5f, cy + k, 2.4f, c);
  }
}

// Icono de recarga (arco abierto con punta) o de detener (cuadrado).
static void brDrawReload(int cx, int cy, int s, bool loading, uint16_t c){
  if(loading){
    int k = s / 5; if(k < 4) k = 4;
    brHostFillRoundRect(cx - k, cy - k, 2 * k, 2 * k, 2, c);
    return;
  }
  int r = s / 4; if(r < 5) r = 5;
  brHostDrawCircle(cx, cy, r, c);
  brHostDrawCircle(cx, cy, r - 1, c);
  brHostFillRect(cx + r - 2, cy - r - 1, 5, 5, brHostColor(BRC_WIN));
  brHostTriangle(cx + r - 4, cy - r + 1, cx + r + 4, cy - r + 1, cx + r, cy - r + 6, c);
}

// Candado (https) o aviso (http). Es informacion de SEGURIDAD, asi que
// se dibuja con el color semantico del sistema, no con uno decorativo.
static void brDrawLock(int x, int cy, int s, bool secure, uint16_t c){
  int w = s * 2 / 3, hh = s / 2;
  if(secure){
    brHostFillRoundRect(x, cy - hh / 2, w, hh, 2, c);
    for(int i = 0; i < 2; i++)
      brHostDrawCircle(x + w / 2, cy - hh / 2, w / 3 - i, c);
  } else {
    brHostTriangle(x + w / 2, cy - hh, x, cy + hh / 2, x + w, cy + hh / 2, c);
  }
}

static void brDrawTabBar(){
  int th = brTabBarH();
  if(!th) return;
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  uint16_t bg = brHostColor(BRC_SURF2), fg = brHostColor(BRC_TXT2);
  brHostFillRect(x, y, w, th, bg);
  int n = brTabCount();
  int plusW = th;
  int avail = w - plusW - 8;
  int tw = n > 0 ? avail / n : avail;
  if(tw > 150) tw = 150;
  if(tw < 54) tw = 54;
  int tx = x + 4;
  for(int i = 0; i < FLEXBR_MAX_TABS; i++){
    if(!gTabs[i].used) continue;
    if(tx + tw > x + w - plusW - 4) break;
    bool act = (i == gTab);
    brHostFillRoundRect(tx, y + 3, tw - 4, th - 5, 6,
                        act ? brHostColor(BRC_WIN) : brHostColor(BRC_SURF));
    char lbl[40];
    flexBrUrlLabel(gTabs[i].url, lbl, sizeof(lbl));
    if(!lbl[0]) snprintf(lbl, sizeof(lbl), "%s", flexBrPageTitle(BRP_NEWTAB));
    brHostTextClip(tx + 8, y + th / 2 - 5, lbl, 1, act ? brHostColor(BRC_TXT) : fg, tx + tw - 20);
    // Aspa de cierre solo en la activa: en las demas seria imposible de
    // acertar con el dedo y ademas se cerrarian sin querer.
    if(act){
      int cxp = tx + tw - 14, cyp = y + th / 2;
      brHostStroke((float)(cxp - 4), (float)(cyp - 4), (float)(cxp + 4), (float)(cyp + 4), 1.8f, fg);
      brHostStroke((float)(cxp + 4), (float)(cyp - 4), (float)(cxp - 4), (float)(cyp + 4), 1.8f, fg);
    }
    tx += tw;
  }
  int pcx = x + w - plusW / 2 - 2, pcy = y + th / 2;
  brHostStroke((float)(pcx - 6), (float)pcy, (float)(pcx + 6), (float)pcy, 2.0f, fg);
  brHostStroke((float)pcx, (float)(pcy - 6), (float)pcx, (float)(pcy + 6), 2.0f, fg);
}

static void brDrawToolbar(){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int th = brToolH();
  int top = y + brTabBarH();
  const BrTab* t = &gTabs[gTab];
  bool loading = (t->stateFlags & FBP_ST_LOADING) != 0;

  brHostFillRect(x, top, w, th, brHostColor(BRC_WIN));

  int bx, by, bw, bh;
  uint16_t on = brHostColor(BRC_TXT), off = brHostColor(BRC_MUTE);
  if(brBtnRect(BRBTN_BACK, &bx, &by, &bw, &bh))
    brDrawChevron(bx + bw / 2, by + bh / 2, bh, false,
                  (t->stateFlags & FBP_ST_CAN_BACK) ? on : off);
  if(brBtnRect(BRBTN_FWD, &bx, &by, &bw, &bh))
    brDrawChevron(bx + bw / 2, by + bh / 2, bh, true,
                  (t->stateFlags & FBP_ST_CAN_FWD) ? on : off);
  if(brBtnRect(BRBTN_RELOAD, &bx, &by, &bw, &bh))
    brDrawReload(bx + bw / 2, by + bh / 2, bh, loading, on);
  if(brBtnRect(BRBTN_MENU, &bx, &by, &bw, &bh)){
    int cx = bx + bw / 2, cy = by + bh / 2, r = 2;
    for(int i = -1; i <= 1; i++) brHostFillCircle(cx, cy + i * (r * 3), r, on);
  }

  // ---- omnibox ----
  if(brBtnRect(BRBTN_OMNI, &bx, &by, &bw, &bh)){
    bool editing = (gEditTarget == BRE_OMNIBOX);
    brHostFillRoundRect(bx, by, bw, bh, bh / 2, brHostColor(editing ? BRC_SURF2 : BRC_SURF));
    if(editing) brHostDrawRoundRect(bx, by, bw, bh, bh / 2, brHostColor(BRC_PRIM));
    int tx0 = bx + 10;
    if(!editing){
      bool secure = (t->stateFlags & FBP_ST_SECURE) != 0;
      bool insec  = (t->stateFlags & FBP_ST_INSECURE) != 0;
      if(t->view == BRV_PAGE && (secure || insec)){
        brDrawLock(tx0, by + bh / 2, bh / 2, secure,
                   secure ? brHostColor(BRC_OK) : brHostColor(BRC_WARN));
        tx0 += bh / 2 + 8;
      }
      char lbl[FLEXBR_URL_MAX];
      flexBrUrlLabel(t->url, lbl, sizeof(lbl));
      if(!lbl[0]) snprintf(lbl, sizeof(lbl), "Buscar o escribir direcci\xC3\xB3n");
      brHostTextClip(tx0, by + bh / 2 - 7, lbl, 2,
                     t->url[0] ? brHostColor(BRC_TXT) : brHostColor(BRC_MUTE), bx + bw - 10);
    } else {
      // En edicion se muestra el TEXTO CRUDO (con esquema), alineado a
      // la derecha cuando no cabe: asi se ve siempre el final, que es
      // donde esta el cursor.
      int tw = brHostTextW(gEdit, 2);
      int avail = bw - 20;
      if(tw <= avail) brHostText(tx0, by + bh / 2 - 7, gEdit, 2, brHostColor(BRC_TXT));
      else            brHostTextR(bx + bw - 10, by + bh / 2 - 7, gEdit, 2, brHostColor(BRC_TXT));
      int cxp = (tw <= avail) ? tx0 + tw + 2 : bx + bw - 8;
      if((brHostMillis() / 500) & 1)
        brHostFillRect(cxp, by + 8, 2, bh - 16, brHostColor(BRC_PRIM));
    }
  }

  // ---- linea de progreso ----
  int py = top + th;
  brHostFillRect(x, py, w, BR_PROG_H, brHostColor(BRC_DIV));
  if(loading){
    int pw = w * (t->progress < 3 ? 3 : t->progress) / 100;
    brHostFillRect(x, py, pw, BR_PROG_H, brHostColor(BRC_PRIM));
  }
}

// Texto centrado con salto por palabras, acotado a `maxLines`.
static int brWrapText(int x, int y, int w, const char* s, int fs, uint16_t col, int maxLines){
  int lh = fs <= 1 ? 10 : fs * 11;
  int line = 0;
  const char* p = s;
  char buf[128];
  while(*p && line < maxLines){
    size_t take = 0, lastSpace = 0;
    while(p[take] && take + 1 < sizeof(buf)){
      buf[take] = p[take];
      if(p[take] == ' ') lastSpace = take;
      buf[take + 1] = 0;
      if(brHostTextW(buf, fs) > w){
        if(lastSpace){ take = lastSpace; }
        break;
      }
      take++;
    }
    if(take == 0) take = 1;
    memcpy(buf, p, take); buf[take] = 0;
    brHostTextC(x + w / 2, y + line * lh, buf, fs, col);
    p += take;
    while(*p == ' ') p++;
    line++;
  }
  return y + line * lh;
}

// ---- paginas internas ----
static void brDrawInternal(){
  int px, py, pw, ph; brPageRect(&px, &py, &pw, &ph);
  brHostFillRect(px, py, pw, ph, brHostColor(BRC_PAGE));
  int page = gTabs[gTab].page;
  int pad = 14;
  int y = py + pad - gIntScroll;
  uint16_t tx = brHostColor(BRC_TXT), t2 = brHostColor(BRC_TXT2), mu = brHostColor(BRC_MUTE);
  int rowH = 46;
  gIntMaxScroll = 0;

  brHostTextC(px + pw / 2, y, flexBrPageTitle(page), 3, tx);
  y += 40;

  switch(page){
    case BRP_NEWTAB: {
      brHostTextC(px + pw / 2, y, "Navegador de Flex OS Ultra", 2, t2);
      y += 34;
      const char* st;
      if(!(gCaps & FLEXBR_CAP_REMOTE))      st = flexBrowserCapReason(FLEXBR_CAP_REMOTE);
      else if(gNetState == BRN_READY)       st = "Sesi\xC3\xB3n remota lista";
      else if(gNetState == BRN_CONNECTING || gNetState == BRN_HANDSHAKE) st = "Conectando con el servicio...";
      else if(gNetState == BRN_NOWIFI)      st = "Sin Wi-Fi";
      else                                  st = "Servicio no disponible";
      y = brWrapText(px + pad, y, pw - 2 * pad, st, 2,
                     gNetState == BRN_READY ? brHostColor(BRC_OK) : mu, 3) + 16;
      // Accesos rapidos: los favoritos, que es lo que de verdad usa el
      // usuario, y no una rejilla de sitios inventada.
      if(gBmN > 0){
        brHostText(px + pad, y, "Favoritos", 2, t2); y += 26;
        for(int i = 0; i < gBmN && i < 6; i++){
          brHostFillRoundRect(px + pad, y, pw - 2 * pad, rowH - 6, 8, brHostColor(BRC_SURF));
          brHostTextClip(px + pad + 12, y + 6, gBm[i].title, 2, tx, px + pw - pad - 12);
          char lbl[64]; flexBrUrlLabel(gBm[i].url, lbl, sizeof(lbl));
          brHostTextClip(px + pad + 12, y + 24, lbl, 1, mu, px + pw - pad - 12);
          y += rowH;
        }
      } else {
        y = brWrapText(px + pad, y, pw - 2 * pad,
                       "Escribe una direcci\xC3\xB3n o una b\xC3\xBAsqueda en la barra de arriba.",
                       2, mu, 3);
      }
      break;
    }
    case BRP_HISTORY: {
      if(gHistN == 0){ brHostTextC(px + pw / 2, y + 20, "No hay nada en el historial", 2, mu); break; }
      for(int i = 0; i < gHistN; i++){
        if(y > py + ph){ gIntMaxScroll += rowH; continue; }
        if(y + rowH >= py){
          brHostFillRoundRect(px + pad, y, pw - 2 * pad, rowH - 6, 8, brHostColor(BRC_SURF));
          brHostTextClip(px + pad + 12, y + 5, gHist[i].title, 2, tx, px + pw - pad - 50);
          char lbl[80]; flexBrUrlLabel(gHist[i].url, lbl, sizeof(lbl));
          brHostTextClip(px + pad + 12, y + 24, lbl, 1, mu, px + pw - pad - 50);
          char cnt[12]; snprintf(cnt, sizeof(cnt), "%u", (unsigned)gHist[i].visits);
          brHostTextR(px + pw - pad - 12, y + 14, cnt, 1, mu);
        }
        y += rowH;
      }
      break;
    }
    case BRP_BOOKMARKS: {
      if(gBmN == 0){ brHostTextC(px + pw / 2, y + 20, "A\xC3\xBAn no hay favoritos", 2, mu); break; }
      for(int i = 0; i < gBmN; i++){
        if(y > py + ph){ gIntMaxScroll += rowH; continue; }
        if(y + rowH >= py){
          brHostFillRoundRect(px + pad, y, pw - 2 * pad, rowH - 6, 8, brHostColor(BRC_SURF));
          brHostTextClip(px + pad + 12, y + 5, gBm[i].title, 2, tx, px + pw - pad - 50);
          char lbl[80]; flexBrUrlLabel(gBm[i].url, lbl, sizeof(lbl));
          brHostTextClip(px + pad + 12, y + 24, lbl, 1, mu, px + pw - pad - 50);
        }
        y += rowH;
      }
      break;
    }
    case BRP_DOWNLOADS: {
      y = brWrapText(px + pad, y, pw - 2 * pad,
            "Este dispositivo no guarda descargas: no hay espacio para ficheros grandes "
            "y el contenido lo sirve el servicio remoto. Aqu\xC3\xAD se listan los archivos "
            "que las p\xC3\xA1ginas han intentado descargar.", 1, mu, 6) + 12;
      if(gDlN == 0){ brHostTextC(px + pw / 2, y + 10, "Ninguna descarga en esta sesi\xC3\xB3n", 2, mu); break; }
      for(int i = 0; i < gDlN; i++){
        brHostFillRoundRect(px + pad, y, pw - 2 * pad, rowH - 6, 8, brHostColor(BRC_SURF));
        brHostTextClip(px + pad + 12, y + 5, gDl[i].name, 2, tx, px + pw - pad - 12);
        char sz[32];
        if(gDl[i].size >= 1024u * 1024u) snprintf(sz, sizeof(sz), "%.1f MB", gDl[i].size / 1048576.0);
        else if(gDl[i].size >= 1024u)    snprintf(sz, sizeof(sz), "%u KB", (unsigned)(gDl[i].size / 1024));
        else                             snprintf(sz, sizeof(sz), "%u B", (unsigned)gDl[i].size);
        brHostTextClip(px + pad + 12, y + 24, sz, 1, mu, px + pw - pad - 12);
        y += rowH;
      }
      break;
    }
    case BRP_OFFLINE: {
      brHostDrawCircle(px + pw / 2, y + 40, 30, brHostColor(BRC_WARN));
      brHostStroke((float)(px + pw / 2 - 16), (float)(y + 24), (float)(px + pw / 2 + 16), (float)(y + 56), 3.0f, brHostColor(BRC_WARN));
      y += 92;
      const char* why = brHostOnline()
          ? (gCaps & FLEXBR_CAP_REMOTE ? "No se puede hablar con el servicio de navegaci\xC3\xB3n."
                                       : flexBrowserCapReason(FLEXBR_CAP_REMOTE))
          : "El dispositivo no est\xC3\xA1 conectado a ninguna red Wi-Fi.";
      y = brWrapText(px + pad, y, pw - 2 * pad, why, 2, t2, 4) + 14;
      y = brWrapText(px + pad, y, pw - 2 * pad,
            "El historial, los favoritos y las p\xC3\xA1ginas internas siguen funcionando sin conexi\xC3\xB3n.",
            1, mu, 3);
      break;
    }
    case BRP_SETTINGS: {
      // Las filas se dibujan con brSettingsRow y su geometria la comparte
      // el manejador tactil (brSettingsHit) para que no puedan
      // desincronizarse.
      brSettingsDraw(px, py, pw, ph, &y);
      break;
    }
    case BRP_ABOUT: {
      char line[96];
      brHostText(px + pad, y, "Navegador de Flex OS", 2, tx); y += 26;
      snprintf(line, sizeof(line), "Plataforma: %s", FLEXBR_PLAT_NAME);
      brHostText(px + pad, y, line, 1, t2); y += 18;
      snprintf(line, sizeof(line), "Protocolo: FBP/%d", FBP_VERSION);
      brHostText(px + pad, y, line, 1, t2); y += 18;
      snprintf(line, sizeof(line), "Pesta\xC3\xB1" "as: %d de %d", brTabCount(), brTabLimit());
      brHostText(px + pad, y, line, 1, t2); y += 24;

      brHostText(px + pad, y, "Capacidades", 2, tx); y += 24;
      struct { uint32_t cap; const char* name; } caps[] = {
        { FLEXBR_CAP_REMOTE, "Sesi\xC3\xB3n remota" },
        { FLEXBR_CAP_TILES,  "Regiones sucias" },
        { FLEXBR_CAP_MEDIA,  "Reproductor de v\xC3\xAD" "deo" },
        { FLEXBR_CAP_TABS,   "Varias pesta\xC3\xB1" "as" },
        { FLEXBR_CAP_PSRAM,  "PSRAM" },
        { FLEXBR_CAP_FS,     "Almacenamiento" }
      };
      for(unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); i++){
        bool on2 = (gCaps & caps[i].cap) != 0;
        brHostFillCircle(px + pad + 5, y + 6, 4, on2 ? brHostColor(BRC_OK) : brHostColor(BRC_MUTE));
        brHostText(px + pad + 18, y, caps[i].name, 1, on2 ? t2 : mu);
        if(!on2){
          const char* why = flexBrowserCapReason(caps[i].cap);
          if(why) brHostTextClip(px + pad + 18, y + 12, why, 1, mu, px + pw - pad);
          y += 12;
        }
        y += 18;
      }
      y += 8;
      brHostText(px + pad, y, "Audio", 2, tx); y += 22;
      y = brWrapText(px + pad, y, pw - 2 * pad,
            "Ninguna de las placas de Flex OS lleva salida de audio cableada (no hay I2S "
            "ni DAC en el firmware), as\xC3\xAD que el reproductor es solo de v\xC3\xAD" "deo.",
            1, mu, 4) + 10;

      if(gSt.telemetry){
        brHostText(px + pad, y, "Telemetr\xC3\xAD" "a local", 2, tx); y += 24;
        brMeasure();
        struct { const char* k; unsigned v; const char* u; } rows[] = {
          { "Heap libre",        (unsigned)(gStats.freeHeap / 1024),  "KB" },
          { "Heap m\xC3\xADnimo",(unsigned)(gStats.minFreeHeap / 1024), "KB" },
          { "PSRAM libre",       (unsigned)(gStats.freePsram / 1024), "KB" },
          { "Frames dibujados",  (unsigned)gStats.framesOk,           "" },
          { "Frames descartados",(unsigned)gStats.framesDropped,      "" },
          { "Frames corruptos",  (unsigned)gStats.framesBad,          "" },
          { "Decodificaci\xC3\xB3n", (unsigned)gStats.decodeMsLast,   "ms" },
          { "Decodif. m\xC3\xA1xima", (unsigned)gStats.decodeMsMax,   "ms" },
          { "Ida y vuelta",      (unsigned)gStats.rttMs,              "ms" },
          { "Recibido",          (unsigned)(gStats.bytesIn / 1024),   "KB" },
          { "Enviado",           (unsigned)(gStats.bytesOut / 1024),  "KB" },
          { "Reconexiones",      (unsigned)gStats.reconnects,         "" },
          { "Navegaciones",      (unsigned)gStats.navCount,           "" },
          { "Errores",           (unsigned)gStats.errCount,           "" }
        };
        for(unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++){
          brHostText(px + pad, y, rows[i].k, 1, mu);
          snprintf(line, sizeof(line), "%u %s", rows[i].v, rows[i].u);
          brHostTextR(px + pw - pad, y, line, 1, t2);
          y += 16;
        }
      } else {
        y = brWrapText(px + pad, y, pw - 2 * pad,
              "La telemetr\xC3\xAD" "a esta desactivada. Se puede encender en Ajustes; "
              "no sale del dispositivo.", 1, mu, 3);
      }
      break;
    }
  }
  int bottom = y + gIntScroll;
  int total = bottom - py;
  gIntMaxScroll = total > ph ? total - ph + 20 : 0;
  brHostFlush(py, py + ph - 1);
}

// ---- Ajustes del navegador (flex://settings) ----
// La lista de filas es UNA sola tabla: la usan tanto el dibujo como el
// tactil, asi que no pueden quedar descolocados entre si.
enum { SR_SERVER = 0, SR_TOKEN, SR_TEST, SR_ENGINE, SR_TPL, SR_PROFILE, SR_QUALITY,
       SR_SCALE, SR_HISTORY, SR_TELEM, SR_MEDIA, SR_LOCAL, SR_INSECURE,
       SR_CLEARHIST, SR_CLEARBM, SR_CLEARALL, SR_N };
#define BR_SET_ROWH 44

static const char* brEngineName(int e){
  switch(e){ case 0: return "DuckDuckGo"; case 1: return "Google"; case 2: return "Bing"; default: return "Personalizado"; }
}
static const char* brProfileName(int p){
  switch(p){ case BRQ_LOWLAT: return "Baja latencia"; case BRQ_DATASAVER: return "Ahorro de datos"; default: return "Equilibrado"; }
}

static void brSettingsDraw(int px, int py, int pw, int ph, int* yio){
  int y = *yio, pad = 14;
  uint16_t tx = brHostColor(BRC_TXT), t2 = brHostColor(BRC_TXT2), mu = brHostColor(BRC_MUTE);
  char val[FLEXBR_SERVER_MAX + 8];
  for(int r = 0; r < SR_N; r++){
    const char* name = ""; const char* sub = NULL;
    val[0] = 0;
    switch(r){
      case SR_SERVER:  name = "Servidor de navegaci\xC3\xB3n";
                       snprintf(val, sizeof(val), "%s", gSt.server[0] ? gSt.server : "sin configurar"); break;
      case SR_TOKEN:   name = "Credencial del dispositivo";
                       snprintf(val, sizeof(val), "%s", gSt.token[0] ? "configurada" : "sin configurar"); break;
      case SR_TEST:    name = "Probar el servidor";
                       sub = "Comprueba /v1/health por HTTPS"; break;
      case SR_ENGINE:  name = "Buscador"; snprintf(val, sizeof(val), "%s", brEngineName(gSt.searchEngine)); break;
      case SR_TPL:     name = "Plantilla personalizada";
                       snprintf(val, sizeof(val), "%s", gSt.searchCustom[0] ? gSt.searchCustom : "usar %s"); break;
      case SR_PROFILE: name = "Perfil de rendimiento"; snprintf(val, sizeof(val), "%s", brProfileName(gSt.profile)); break;
      case SR_QUALITY: name = "Calidad de imagen"; snprintf(val, sizeof(val), "%u", (unsigned)gSt.quality); break;
      case SR_SCALE:   name = "Escala del viewport"; snprintf(val, sizeof(val), "%u %%", (unsigned)gSt.scalePct); break;
      case SR_HISTORY: name = "Guardar historial"; snprintf(val, sizeof(val), "%s", gSt.saveHistory ? "S\xC3\xAD" : "No"); break;
      case SR_TELEM:   name = "Telemetr\xC3\xAD" "a local"; snprintf(val, sizeof(val), "%s", gSt.telemetry ? "S\xC3\xAD" : "No"); break;
      case SR_MEDIA:   name = "Reproductor propio"; snprintf(val, sizeof(val), "%s", gSt.mediaNative ? "S\xC3\xAD" : "No"); break;
      case SR_LOCAL:   name = "Permitir red local";
                       sub = "Solo para desarrollo: abre IP y nombres internos";
                       snprintf(val, sizeof(val), "%s", gSt.allowLocal ? "S\xC3\xAD" : "No"); break;
      case SR_INSECURE:name = "Permitir ws:// sin TLS";
                       sub = "Solo para desarrollo: el trafico viaja en claro";
                       snprintf(val, sizeof(val), "%s", gSt.allowInsecure ? "S\xC3\xAD" : "No"); break;
      case SR_CLEARHIST: name = "Borrar historial"; break;
      case SR_CLEARBM:   name = "Borrar favoritos"; break;
      case SR_CLEARALL:  name = "Borrar todos los datos de navegaci\xC3\xB3n"; break;
    }
    int rh = sub ? BR_SET_ROWH + 12 : BR_SET_ROWH;
    if(y + rh >= py && y <= py + ph){
      brHostFillRoundRect(px + pad, y, pw - 2 * pad, rh - 6, 8, brHostColor(BRC_SURF));
      bool danger = (r >= SR_CLEARHIST);
      brHostTextClip(px + pad + 12, y + 6, name, 2, danger ? brHostColor(BRC_DANGER) : tx,
                     px + pw - pad - 100);
      if(sub) brHostTextClip(px + pad + 12, y + 26, sub, 1, mu, px + pw - pad - 12);
      if(val[0]) brHostTextR(px + pw - pad - 12, y + (sub ? 26 : 14), val, 1, t2);
    }
    y += rh;
  }
  *yio = y;
}

static int brSettingsHit(int ty, int py){
  int y = py + 14 + 40;      // mismo origen que brDrawInternal
  y -= gIntScroll;
  for(int r = 0; r < SR_N; r++){
    bool sub = (r == SR_TEST || r == SR_LOCAL || r == SR_INSECURE);
    int rh = sub ? BR_SET_ROWH + 12 : BR_SET_ROWH;
    if(ty >= y && ty < y + rh) return r;
    y += rh;
  }
  return -1;
}

// ---- menu propio ----
enum { BRM_NEWTAB = 0, BRM_BOOKMARK, BRM_BOOKMARKS, BRM_HISTORY, BRM_DOWNLOADS,
       BRM_MEDIA, BRM_KEYBOARD, BRM_SETTINGS, BRM_ABOUT, BRM_CLOSE, BRM_N };
static const char* brMenuLabel(int i){
  switch(i){
    case BRM_NEWTAB:    return "Nueva pesta\xC3\xB1" "a";
    case BRM_BOOKMARK:  return brBookmarkIndex(gTabs[gTab].url) >= 0 ? "Quitar de favoritos" : "A\xC3\xB1" "adir a favoritos";
    case BRM_BOOKMARKS: return "Favoritos";
    case BRM_HISTORY:   return "Historial";
    case BRM_DOWNLOADS: return "Descargas";
    case BRM_MEDIA:     return "Reproducir v\xC3\xAD" "deo de la p\xC3\xA1gina";
    case BRM_KEYBOARD:  return "Teclado para la p\xC3\xA1gina";
    case BRM_SETTINGS:  return "Ajustes del navegador";
    case BRM_ABOUT:     return "Acerca de";
    case BRM_CLOSE:     return "Cerrar el navegador";
  }
  return "";
}
static bool brMenuEnabled(int i){
  if(i == BRM_MEDIA) return gMediaAvail && (gCaps & FLEXBR_CAP_MEDIA) && gSt.mediaNative;
  if(i == BRM_KEYBOARD) return gTabs[gTab].view == BRV_PAGE && gNetState == BRN_READY;
  if(i == BRM_BOOKMARK) return gTabs[gTab].url[0] && flexBrInternalPage(gTabs[gTab].url) == BRP_NONE;
  if(i == BRM_NEWTAB) return brTabLimit() > 1;
  return true;
}
#define BR_MENU_ROWH 42
static void brMenuRect(int* mx, int* my, int* mw, int* mh){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int mwid = w * 3 / 4; if(mwid > 300) mwid = 300; if(mwid > w - 16) mwid = w - 16;
  int mhei = BRM_N * BR_MENU_ROWH + 12;
  int top = y + brTabBarH() + brToolH() + BR_PROG_H + 4;
  if(top + mhei > y + h) mhei = y + h - top - 4;
  *mx = x + w - mwid - 6; *my = top; *mw = mwid; *mh = mhei;
}
static void brDrawMenu(){
  int mx, my, mw, mh; brMenuRect(&mx, &my, &mw, &mh);
  brHostFillRoundRect(mx, my, mw, mh, 12, brHostColor(BRC_SURF2));
  brHostDrawRoundRect(mx, my, mw, mh, 12, brHostColor(BRC_BORDER));
  for(int i = 0; i < BRM_N; i++){
    int ry = my + 6 + i * BR_MENU_ROWH;
    if(ry + BR_MENU_ROWH > my + mh) break;
    bool en = brMenuEnabled(i);
    uint16_t c = (i == BRM_CLOSE) ? brHostColor(BRC_DANGER)
                                  : (en ? brHostColor(BRC_TXT) : brHostColor(BRC_MUTE));
    brHostTextClip(mx + 14, ry + BR_MENU_ROWH / 2 - 7, brMenuLabel(i), 2, c, mx + mw - 12);
    if(i + 1 < BRM_N) brHostFillRect(mx + 10, ry + BR_MENU_ROWH - 1, mw - 20, 1, brHostColor(BRC_DIV));
  }
  brHostFlush(my, my + mh - 1);
}

// ---- selector de pestanas a pantalla completa ----
static void brDrawTabsView(){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  brHostFillRect(x, y, w, h, brHostColor(BRC_PAGE));
  brHostTextC(x + w / 2, y + 14, "Pesta\xC3\xB1" "as", 3, brHostColor(BRC_TXT));
  int rowH = 62, ry = y + 56;
  for(int i = 0; i < FLEXBR_MAX_TABS; i++){
    if(!gTabs[i].used) continue;
    if(ry + rowH > y + h - 60) break;
    brHostFillRoundRect(x + 12, ry, w - 24, rowH - 8, 10,
                        i == gTab ? brHostColor(BRC_SURF2) : brHostColor(BRC_SURF));
    brHostTextClip(x + 24, ry + 8, gTabs[i].title, 2, brHostColor(BRC_TXT), x + w - 60);
    char lbl[80]; flexBrUrlLabel(gTabs[i].url, lbl, sizeof(lbl));
    brHostTextClip(x + 24, ry + 30, lbl, 1, brHostColor(BRC_MUTE), x + w - 60);
    int cxp = x + w - 34, cyp = ry + rowH / 2 - 4;
    brHostStroke((float)(cxp - 7), (float)(cyp - 7), (float)(cxp + 7), (float)(cyp + 7), 2.0f, brHostColor(BRC_TXT2));
    brHostStroke((float)(cxp + 7), (float)(cyp - 7), (float)(cxp - 7), (float)(cyp + 7), 2.0f, brHostColor(BRC_TXT2));
    ry += rowH;
  }
  int by = y + h - 52;
  brHostFillRoundRect(x + 12, by, w - 24, 44, 12, brHostColor(BRC_PRIM));
  brHostTextC(x + w / 2, by + 14, "Nueva pesta\xC3\xB1" "a", 2, brHostColor(BRC_ONACC));
  brHostFlush(y, y + h - 1);
}

// ---- reproductor ----
static void brDrawMediaPlayer(bool full){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  if(full){
    brHostFillRect(x, y, w, h, brHostRGB(0, 0, 0));
    brHostTextClip(x + 12, y + 12, gMedia.title[0] ? gMedia.title : "V\xC3\xAD" "deo", 2,
                   brHostRGB(230, 234, 245), x + w - 12);
    int vx, vy, vw, vh; brVideoRect(&vx, &vy, &vw, &vh);
    brHostDrawRoundRect(vx - 1, vy - 1, vw + 2, vh + 2, 4, brHostColor(BRC_BORDER));
  }
  int cy = y + h - 66;
  brHostFillRect(x, cy - 10, w, 76, brHostRGB(14, 16, 22));
  // barra de progreso
  int sbx = x + 16, sbw = w - 32, sby = cy;
  brHostFillRoundRect(sbx, sby, sbw, 6, 3, brHostColor(BRC_TRACK));
  uint32_t dur = gMedia.durationMs ? gMedia.durationMs : 1;
  int filled = (int)((uint64_t)sbw * gMediaPosMs / dur);
  if(filled > sbw) filled = sbw;
  brHostFillRoundRect(sbx, sby, filled, 6, 3, brHostColor(BRC_PRIM));
  brHostFillCircle(sbx + filled, sby + 3, 8, brHostRGB(240, 242, 248));
  char tc[32];
  snprintf(tc, sizeof(tc), "%u:%02u / %u:%02u",
           (unsigned)(gMediaPosMs / 60000), (unsigned)((gMediaPosMs / 1000) % 60),
           (unsigned)(gMedia.durationMs / 60000), (unsigned)((gMedia.durationMs / 1000) % 60));
  brHostText(sbx, sby + 14, tc, 1, brHostRGB(160, 168, 186));
  // play/pausa
  int pcx = x + w / 2, pcy = cy + 40;
  brHostFillCircle(pcx, pcy, 20, brHostColor(BRC_PRIM));
  if(gMediaPlaying){
    brHostFillRect(pcx - 6, pcy - 8, 4, 16, brHostColor(BRC_ONACC));
    brHostFillRect(pcx + 2, pcy - 8, 4, 16, brHostColor(BRC_ONACC));
  } else {
    brHostTriangle(pcx - 5, pcy - 9, pcx - 5, pcy + 9, pcx + 9, pcy, brHostColor(BRC_ONACC));
  }
  // volumen: se dibuja apagado a proposito -- no hay salida de audio
  brHostTextR(x + w - 16, pcy - 6, "sin audio", 1, brHostRGB(120, 126, 142));
  // cerrar
  brHostText(x + 16, pcy - 6, "Cerrar", 2, brHostRGB(230, 234, 245));
  brHostFlush(cy - 10, y + h - 1);
}

// ---- barra de error flotante ----
static void brDrawErrorBar(){
  if(!gErr[0]) return;
  if(brHostMillis() - gErrMs > 6000){ gErr[0] = 0; gNeedFullRedraw = true; return; }
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int bh = 40, by = y + h - bh - 8;
  brHostFillRoundRect(x + 10, by, w - 20, bh, 10, brHostColor(BRC_ERR));
  brHostTextClip(x + 22, by + bh / 2 - 7, gErr, 2, brHostRGB(255, 255, 255), x + w - 22);
  brHostFlush(by, by + bh);
}

static void brRenderChrome(){
  brDrawTabBar();
  brDrawToolbar();
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  brHostFlush(y, y + brChromeH());
  gNeedChromeRedraw = false;
}

static void brRenderAll(){
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  brHostClip(y, y + h - 1);
  if(gView == BRV_TABS){ brDrawTabsView(); gNeedFullRedraw = false; return; }
  if(gView == BRV_MEDIA){ brDrawMediaPlayer(true); gNeedFullRedraw = false; return; }

  brHostFillRect(x, y, w, h, brHostColor(BRC_WIN));
  brDrawTabBar();
  brDrawToolbar();
  if(gTabs[gTab].view == BRV_INTERNAL) brDrawInternal();
  else brRepaintPageFromCache();
  if(gView == BRV_MENU) brDrawMenu();
  brDrawErrorBar();
  brHostFlush(y, y + h - 1);
  gNeedFullRedraw = false;
  gNeedChromeRedraw = false;
}

// =============================================================
//  8) INTERFAZ: TACTIL
// =============================================================
static bool     gDragging = false;
static int      gDragLastY = 0, gDragLastX = 0;
static uint32_t gLastScrollMs = 0;
static int      gScrollAccX = 0, gScrollAccY = 0;

static void brOpenOmnibox(){
  gEditTarget = BRE_OMNIBOX;
  const char* u = gTabs[gTab].url;
  if(flexBrInternalPage(u) != BRP_NONE) gEdit[0] = 0;
  else snprintf(gEdit, sizeof(gEdit), "%s", u);
  gNeedFullRedraw = true;
}

static void brMenuAction(int i){
  gView = gTabs[gTab].view;
  switch(i){
    case BRM_NEWTAB: {
      int t = brTabNew();
      if(t >= 0){ gTab = t; gView = BRV_INTERNAL; brOpenOmnibox(); }
      else brSetError("No hay memoria para otra pesta\xC3\xB1" "a");
      break;
    }
    case BRM_BOOKMARK:
      if(brMenuEnabled(BRM_BOOKMARK)){
        bool added = brBookmarkToggle(gTabs[gTab].url, gTabs[gTab].title);
        brSetError(added ? "A\xC3\xB1" "adido a favoritos" : "Quitado de favoritos");
        flexBrowserFlushPersist(false);
      }
      break;
    case BRM_BOOKMARKS: brOpenInternal(BRP_BOOKMARKS); break;
    case BRM_HISTORY:   brOpenInternal(BRP_HISTORY); break;
    case BRM_DOWNLOADS: brOpenInternal(BRP_DOWNLOADS); break;
    case BRM_SETTINGS:  brOpenInternal(BRP_SETTINGS); break;
    case BRM_ABOUT:     brOpenInternal(BRP_ABOUT); break;
    case BRM_MEDIA:
      if(brMenuEnabled(BRM_MEDIA)){
        gView = BRV_MEDIA; gMediaPlaying = true; gMediaPosMs = 0;
        brPushCmd(FBP_C_MEDIA, FBP_CH_MEDIA, 0, 0, FBP_MED_OPEN, 0, 0, 0, NULL);
      } else {
        brSetError(gMediaAvail ? flexBrowserCapReason(FLEXBR_CAP_MEDIA)
                               : "La p\xC3\xA1gina no tiene v\xC3\xAD" "deo reproducible");
      }
      break;
    case BRM_KEYBOARD:
      if(brMenuEnabled(BRM_KEYBOARD)){ gEditTarget = BRE_PAGEKEY; gEdit[0] = 0; }
      break;
    case BRM_CLOSE: gWantClose = true; break;
  }
  gNeedFullRedraw = true;
}

static void brSettingsAction(int row){
  switch(row){
    case SR_SERVER: gEditTarget = BRE_SERVER; snprintf(gEdit, sizeof(gEdit), "%s", gSt.server); break;
    case SR_TOKEN:  gEditTarget = BRE_TOKEN;  gEdit[0] = 0; break;
    case SR_TEST: {
      // Comprobacion de control por HTTPS. Es una operacion puntual, no
      // por frame, asi que se hace aqui mismo con un tiempo de espera
      // corto en vez de montar otra tarea.
      char url[FLEXBR_SERVER_MAX + 16];
      bool tls; char host[FLEXBR_HOST_MAX]; uint16_t port; char path[128];
      if(!gSt.server[0] || !flexBrParseWsUrl(gSt.server, &tls, host, sizeof(host), &port, path, sizeof(path))){
        brSetError("Configura antes la direcci\xC3\xB3n del servidor"); break;
      }
      snprintf(url, sizeof(url), "%s://%s:%u/v1/health", tls ? "https" : "http", host, (unsigned)port);
      if(!brHostOnline()){ brSetError("Sin Wi-Fi"); break; }
      WiFiClientSecure sec; WiFiClient plain; HTTPClient http;
      bool ok;
      if(tls){ sec.setInsecure(); sec.setTimeout(6); ok = http.begin(sec, url); }
      else                                            ok = http.begin(plain, url);
      if(!ok){ brSetError("No se pudo abrir la conexi\xC3\xB3n"); break; }
      http.setTimeout(6000); http.setConnectTimeout(6000);
      int code = http.GET();
      if(code == 200) brSetError("Servidor accesible");
      else if(code > 0){ char m[48]; snprintf(m, sizeof(m), "El servidor respondi\xC3\xB3 %d", code); brSetError(m); }
      else brSetError("El servidor no responde");
      http.end();
      break;
    }
    case SR_ENGINE:  gSt.searchEngine = (uint8_t)((gSt.searchEngine + 1) % 4); brSaveSettings(); break;
    case SR_TPL:     gEditTarget = BRE_SEARCHTPL; snprintf(gEdit, sizeof(gEdit), "%s", gSt.searchCustom); break;
    case SR_PROFILE: {
      gSt.profile = (uint8_t)((gSt.profile + 1) % BRQ_N);
      // El perfil NO promete una tasa de frames: ajusta calidad y escala,
      // que es lo unico que se puede garantizar sin medir la red.
      if(gSt.profile == BRQ_LOWLAT){    gSt.quality = 45; gSt.scalePct = 85; }
      else if(gSt.profile == BRQ_DATASAVER){ gSt.quality = 38; gSt.scalePct = 70; }
      else {                             gSt.quality = 62; gSt.scalePct = 100; }
      brSaveSettings();
      brPushCmd(FBP_C_VIEWPORT, (uint8_t)(gTab + 1), 0, 0, gSt.quality, gSt.profile, gSt.scalePct, 0, NULL);
      break;
    }
    case SR_QUALITY: gSt.quality = (uint8_t)(gSt.quality >= 85 ? 25 : gSt.quality + 10); brSaveSettings();
                     brPushCmd(FBP_C_VIEWPORT, (uint8_t)(gTab + 1), 0, 0, gSt.quality, gSt.profile, gSt.scalePct, 0, NULL); break;
    case SR_SCALE:   gSt.scalePct = (uint8_t)(gSt.scalePct >= 100 ? 50 : gSt.scalePct + 10); brSaveSettings();
                     brPushCmd(FBP_C_VIEWPORT, (uint8_t)(gTab + 1), 0, 0, gSt.quality, gSt.profile, gSt.scalePct, 0, NULL); break;
    case SR_HISTORY: gSt.saveHistory = !gSt.saveHistory; brSaveSettings(); break;
    case SR_TELEM:   gSt.telemetry = !gSt.telemetry; brSaveSettings(); break;
    case SR_MEDIA:   gSt.mediaNative = !gSt.mediaNative; brSaveSettings(); break;
    case SR_LOCAL:   gSt.allowLocal = !gSt.allowLocal; brSaveSettings();
                     brSetError(gSt.allowLocal ? "Red local permitida (solo desarrollo)" : "Red local bloqueada"); break;
    case SR_INSECURE:gSt.allowInsecure = !gSt.allowInsecure; brSaveSettings();
                     brSetError(gSt.allowInsecure ? "TLS no obligatorio (solo desarrollo)" : "TLS obligatorio"); break;
    case SR_CLEARHIST: flexBrowserClearData(BRCLR_HISTORY); brSetError("Historial borrado"); break;
    case SR_CLEARBM:   flexBrowserClearData(BRCLR_BOOKMARKS); brSetError("Favoritos borrados"); break;
    case SR_CLEARALL:  flexBrowserClearData(BRCLR_ALL); brSetError("Datos de navegaci\xC3\xB3n borrados"); break;
  }
  brComputeCaps();
  gNeedFullRedraw = true;
}

static void brTouchPage(const BrTouch* t, int px, int py, int pw, int ph){
  if(gTabs[gTab].view != BRV_PAGE) return;
  uint8_t ch = (uint8_t)(gTab + 1);
  if(t->pressed){ gDragging = false; gDragLastX = t->x; gDragLastY = t->y; gScrollAccX = gScrollAccY = 0; }
  if(t->down && !t->pressed){
    int dx = t->x - gDragLastX, dy = t->y - gDragLastY;
    if(!gDragging && (dy > 8 || dy < -8 || dx > 10 || dx < -10)) gDragging = true;
    if(gDragging){
      gScrollAccX += dx; gScrollAccY += dy;
      gDragLastX = t->x; gDragLastY = t->y;
      uint32_t now = brHostMillis();
      // Se agrupa el desplazamiento: un mensaje cada ~50 ms en vez de uno
      // por muestra del tactil. Menos trafico y el gesto se siente igual.
      if(now - gLastScrollMs >= 50 && (gScrollAccX || gScrollAccY)){
        brPushCmd(FBP_C_SCROLL, ch, (int16_t)(-gScrollAccX), (int16_t)(-gScrollAccY), 0, 0, 0, 0, NULL);
        gScrollAccX = gScrollAccY = 0;
        gLastScrollMs = now;
      }
    }
  }
  if(t->released){
    if(gDragging){
      if(gScrollAccX || gScrollAccY)
        brPushCmd(FBP_C_SCROLL, ch, (int16_t)(-gScrollAccX), (int16_t)(-gScrollAccY), 0, 0, 0, 0, NULL);
    } else {
      int rx = t->x - px, ry = t->y - py;
      if(rx >= 0 && ry >= 0 && rx < pw && ry < ph)
        brPushCmd(FBP_C_POINTER, ch, (int16_t)rx, (int16_t)ry, FBP_PTR_TAP, 0, 0, 0, NULL);
    }
    gDragging = false;
    gScrollAccX = gScrollAccY = 0;
  }
}

static void brHandleTouch(){
  BrTouch t; brHostGetTouch(&t);
  int x, y, w, h; brHostContentRect(&x, &y, &w, &h);
  int px, py, pw, ph; brPageRect(&px, &py, &pw, &ph);

  // --- reproductor ---
  if(gView == BRV_MEDIA){
    if(t.tap){
      int cy = y + h - 66, pcx = x + w / 2, pcy = cy + 40;
      if(t.x >= x + 10 && t.x <= x + 100 && t.y >= pcy - 20 && t.y <= pcy + 20){
        brPushCmd(FBP_C_MEDIA, FBP_CH_MEDIA, 0, 0, FBP_MED_STOP, 0, 0, 0, NULL);
        gView = gTabs[gTab].view; gMediaPlaying = false; gNeedFullRedraw = true;
      } else if(t.x >= pcx - 26 && t.x <= pcx + 26 && t.y >= pcy - 26 && t.y <= pcy + 26){
        gMediaPlaying = !gMediaPlaying;
        brPushCmd(FBP_C_MEDIA, FBP_CH_MEDIA, 0, 0, gMediaPlaying ? FBP_MED_PLAY : FBP_MED_PAUSE, 0, 0, 0, NULL);
        brDrawMediaPlayer(false);
      } else if(t.y >= cy - 12 && t.y <= cy + 18){
        int sbx = x + 16, sbw = w - 32;
        int pos = (t.x - sbx) * 100 / (sbw > 0 ? sbw : 1);
        if(pos < 0) pos = 0;
        if(pos > 100) pos = 100;
        gMediaPosMs = (uint32_t)((uint64_t)gMedia.durationMs * pos / 100);
        brPushCmd(FBP_C_MEDIA, FBP_CH_MEDIA, 0, 0, FBP_MED_SEEK, 0, 0, gMediaPosMs, NULL);
        brDrawMediaPlayer(false);
      }
      brHostConsumeTouch();
    }
    return;
  }

  // --- selector de pestanas ---
  if(gView == BRV_TABS){
    if(t.tap){
      int rowH = 62, ry = y + 56;
      int by = y + h - 52;
      if(t.y >= by && t.y <= by + 44){
        int nt = brTabNew();
        if(nt >= 0){ gTab = nt; gView = BRV_INTERNAL; brOpenOmnibox(); }
        else brSetError("No hay memoria para otra pesta\xC3\xB1" "a");
      } else {
        for(int i = 0; i < FLEXBR_MAX_TABS; i++){
          if(!gTabs[i].used) continue;
          if(ry + rowH > y + h - 60) break;
          if(t.y >= ry && t.y < ry + rowH){
            if(t.x > x + w - 52) brTabClose(i);
            else { brTabSelect(i); gView = gTabs[gTab].view; }
            break;
          }
          ry += rowH;
        }
      }
      gNeedFullRedraw = true;
      brHostConsumeTouch();
    }
    return;
  }

  // --- menu abierto: consume TODO ---
  if(gView == BRV_MENU){
    if(t.tap){
      int mx, my, mw, mh; brMenuRect(&mx, &my, &mw, &mh);
      if(t.x >= mx && t.x < mx + mw && t.y >= my && t.y < my + mh){
        int i = (t.y - my - 6) / BR_MENU_ROWH;
        if(i >= 0 && i < BRM_N && brMenuEnabled(i)) brMenuAction(i);
        else gNeedFullRedraw = true;
      } else {
        gView = gTabs[gTab].view;
        gNeedFullRedraw = true;
      }
      brHostConsumeTouch();
    }
    return;
  }

  // --- barra superior ---
  int th = brToolH(), tabH = brTabBarH();
  if(t.tap && t.y >= y && t.y < y + tabH){
    // barra de pestanas
    int n = brTabCount(), plusW = tabH;
    int avail = w - plusW - 8;
    int tw = n > 0 ? avail / n : avail;
    if(tw > 150) tw = 150;
    if(tw < 54) tw = 54;
    if(t.x >= x + w - plusW - 2){
      int nt = brTabNew();
      if(nt >= 0){ gTab = nt; gView = BRV_INTERNAL; brOpenOmnibox(); }
      else brSetError("No hay memoria para otra pesta\xC3\xB1" "a");
    } else {
      int tx2 = x + 4;
      for(int i = 0; i < FLEXBR_MAX_TABS; i++){
        if(!gTabs[i].used) continue;
        if(tx2 + tw > x + w - plusW - 4) break;
        if(t.x >= tx2 && t.x < tx2 + tw){
          if(i == gTab && t.x > tx2 + tw - 26) brTabClose(i);
          else brTabSelect(i);
          break;
        }
        tx2 += tw;
      }
    }
    gNeedFullRedraw = true;
    brHostConsumeTouch();
    return;
  }
  if(t.tap && t.y >= y + tabH && t.y < y + tabH + th){
    int bx, by, bw, bh;
    if(brBtnRect(BRBTN_BACK, &bx, &by, &bw, &bh) && t.x >= bx - 4 && t.x < bx + bw + 4){
      if(gTabs[gTab].view == BRV_INTERNAL){ gTabs[gTab].view = BRV_PAGE; gView = BRV_PAGE; gNeedFullRedraw = true; }
      else brPushCmd(FBP_C_BACK, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
      brHostConsumeTouch(); return;
    }
    if(brBtnRect(BRBTN_FWD, &bx, &by, &bw, &bh) && t.x >= bx - 4 && t.x < bx + bw + 4){
      brPushCmd(FBP_C_FORWARD, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
      brHostConsumeTouch(); return;
    }
    if(brBtnRect(BRBTN_RELOAD, &bx, &by, &bw, &bh) && t.x >= bx - 4 && t.x < bx + bw + 4){
      bool loading = (gTabs[gTab].stateFlags & FBP_ST_LOADING) != 0;
      if(gTabs[gTab].view == BRV_INTERNAL) gNeedFullRedraw = true;
      else brPushCmd(loading ? FBP_C_STOP : FBP_C_RELOAD, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
      brHostConsumeTouch(); return;
    }
    if(brBtnRect(BRBTN_MENU, &bx, &by, &bw, &bh) && t.x >= bx - 6 && t.x < bx + bw + 6){
      gView = BRV_MENU; gNeedFullRedraw = true;
      brHostConsumeTouch(); return;
    }
    if(brBtnRect(BRBTN_OMNI, &bx, &by, &bw, &bh) && t.x >= bx && t.x < bx + bw){
      brOpenOmnibox();
      brHostConsumeTouch(); return;
    }
    brHostConsumeTouch();
    return;
  }

  // --- contenido ---
  if(gTabs[gTab].view == BRV_INTERNAL){
    // Desplazamiento de la lista interna.
    if(t.down && !t.pressed && t.y >= py){
      int dy = t.y - gDragLastY;
      if(!gDragging && (dy > 6 || dy < -6)) gDragging = true;
      if(gDragging){
        gIntScroll -= dy;
        if(gIntScroll < 0) gIntScroll = 0;
        if(gIntScroll > gIntMaxScroll) gIntScroll = gIntMaxScroll;
        gDragLastY = t.y;
        brDrawInternal();
      }
    }
    if(t.pressed){ gDragging = false; gDragLastY = t.y; }
    if(t.tap && !gDragging && t.y >= py){
      int page = gTabs[gTab].page;
      int rowH = 46;
      int base = py + 14 + 40 - gIntScroll;
      if(page == BRP_SETTINGS){
        int r = brSettingsHit(t.y, py);
        if(r >= 0) brSettingsAction(r);
      } else if(page == BRP_HISTORY && gHistN > 0){
        int i = (t.y - base) / rowH;
        if(i >= 0 && i < gHistN) brNavigateTo(gHist[i].url, true);
      } else if(page == BRP_BOOKMARKS && gBmN > 0){
        int i = (t.y - base) / rowH;
        if(i >= 0 && i < gBmN) brNavigateTo(gBm[i].url, true);
      } else if(page == BRP_NEWTAB && gBmN > 0){
        int listTop = base + 34 + 3 * 12 + 16 + 26;
        int i = (t.y - listTop) / rowH;
        if(i >= 0 && i < gBmN && i < 6) brNavigateTo(gBm[i].url, true);
      }
      brHostConsumeTouch();
    }
    if(t.released) gDragging = false;
    return;
  }
  brTouchPage(&t, px, py, pw, ph);
}

// =============================================================
//  9) CICLO DE VIDA
// =============================================================
void flexBrowserBegin(){
  if(gInit) return;
  gInit = true;
  memset(&gStats, 0, sizeof(gStats));
  memset(gTabs, 0, sizeof(gTabs));
  memset(&gMedia, 0, sizeof(gMedia));
  brLoadSettings();
  brComputeCaps();
  // OJO: el historial y los favoritos NO se reservan aqui. Begin() corre
  // en el arranque del sistema y no se deshace nunca, asi que reservar
  // ahi las listas dejaria memoria ocupada de forma permanente por una
  // app que quiza no se abra: en Flex OS Pro son ~21 KB de RAM INTERNA,
  // que es justo la que necesita TLS despues. Se reservan en Enter() y
  // se sueltan en Exit(); el contenido vive en LittleFS, no en RAM.
  brTabInit(0);
  gTab = 0;
  gView = BRV_INTERNAL;
}

void flexBrowserEnter(){
  if(!gInit) flexBrowserBegin();
  gActive = true;
  gWantClose = false;
  gEditTarget = BRE_NONE;
  gErr[0] = 0;
  gIntScroll = 0;
  // Los ajustes se releen al ABRIR la app, no solo en el arranque: asi
  // un restablecimiento de fabrica, una restauracion o un cambio hecho
  // desde otra pantalla del sistema se ven sin reiniciar. Son una docena
  // de claves de NVS y solo al abrir, no por frame.
  brLoadSettings();
  brComputeCaps();

  // Listas en memoria mientras la app este abierta (ver Begin).
  if(!gHist){
    gHist = (BrHistEntry*)brHostAlloc(sizeof(BrHistEntry) * FLEXBR_HIST_MAX, true);
    if(gHist){ memset(gHist, 0, sizeof(BrHistEntry) * FLEXBR_HIST_MAX); brLoadHistory(); }
  }
  if(!gBm){
    gBm = (BrBookmark*)brHostAlloc(sizeof(BrBookmark) * FLEXBR_BM_MAX, true);
    if(gBm){ memset(gBm, 0, sizeof(BrBookmark) * FLEXBR_BM_MAX); brLoadBookmarks(); }
  }
  if(!brAllocBuffers()){
    brSetError("Sin memoria para el navegador");
    gCaps &= ~FLEXBR_CAP_REMOTE;
  }
  if(!gMux)    gMux    = xSemaphoreCreateMutex();
  if(!gFrDone) gFrDone = xSemaphoreCreateBinary();
  if(!gCmdQ)   gCmdQ   = xQueueCreate(16, sizeof(BrCmd));
  if(!gUrlQ)   gUrlQ   = xQueueCreate(2, FLEXBR_URL_MAX);

  // Restauracion minima de sesion: la ultima direccion de la pestana 0.
  char last[FLEXBR_URL_MAX];
  brHostPrefsGetStr("brlast", last, sizeof(last), "");

#if FLEXBR_REMOTE_ON
  if((gCaps & FLEXBR_CAP_REMOTE) && gRx && !gNetTask){
    gNetStop = false;
    gNetState = BRN_OFF;
    xTaskCreatePinnedToCore(brNetTask, "flexbrNet", FLEXBR_NET_STACK, NULL, 1, &gNetTask, 1);
  }
#endif

  if(last[0] && flexBrCheckUrl(last, &gSt) == BRB_NONE && (gCaps & FLEXBR_CAP_REMOTE))
    brNavigateTo(last, false);
  else
    brOpenInternal(BRP_NEWTAB);
  gNeedFullRedraw = true;
  brRenderAll();
}

void flexBrowserTick(){
  if(!gActive) return;

  // 1) El tactil SIEMPRE primero: es lo que hace que la interfaz se
  //    sienta viva aunque este llegando una pagina pesada.
  brHandleTouch();

  // 2) Un frame por tick como mucho. Nunca dos: dos decodificaciones
  //    seguidas serian el doble de tiempo sin atender al dedo.
  if(gFrReady) brDrawPendingFrame();

  // 3) Repintados pendientes.
  if(gNeedFullRedraw) brRenderAll();
  else if(gNeedChromeRedraw) brRenderChrome();
  else if(gEditTarget == BRE_OMNIBOX) brDrawToolbar();   // cursor parpadeante

  // 4) Estado de red: un cambio se refleja en la barra.
  static int lastNet = -1;
  if(gNetState != lastNet){ lastNet = gNetState; gNeedChromeRedraw = true; }

  // 5) Reloj del reproductor. La posicion se estima con el reloj local
  //    entre avisos del servicio: es una barra de progreso, no una
  //    referencia de sincronizacion, y se recoloca en cada MEDIA.
  if(gView == BRV_MEDIA && gMediaPlaying){
    static uint32_t lastMediaMs = 0;
    uint32_t now = brHostMillis();
    if(lastMediaMs && now > lastMediaMs){
      gMediaPosMs += (now - lastMediaMs);
      if(gMedia.durationMs && gMediaPosMs > gMedia.durationMs){
        gMediaPosMs = gMedia.durationMs;
        gMediaPlaying = false;
      }
    }
    lastMediaMs = now;
    static uint32_t lastBar = 0;
    if(now - lastBar > 500){ lastBar = now; brDrawMediaPlayer(false); }
  }

  // 6) Persistencia agrupada.
  flexBrowserFlushPersist(false);
}

void flexBrowserExit(){
  if(!gActive) return;
  gActive = false;

  // Guardar ANTES de soltar nada.
  if(gTabs[gTab].used && gTabs[gTab].view == BRV_PAGE && gTabs[gTab].url[0])
    brHostPrefsPutStr("brlast", gTabs[gTab].url);
  else
    brHostPrefsPutStr("brlast", "");
  flexBrowserFlushPersist(true);

  // Parar la tarea de red y esperar a que muera de verdad: si se
  // liberaran los buffers con la tarea viva, leeria memoria ya devuelta.
  gNetStop = true;
  if(gFrDone) xSemaphoreGive(gFrDone);       // desbloquea una espera de frame
  uint32_t t0 = brHostMillis();
  while(gNetTask && brHostMillis() - t0 < 3000) vTaskDelay(pdMS_TO_TICKS(10));
  gNetState = BRN_OFF;

  gFrReady = false;
  brFreeBuffers();
  // Las listas se sueltan tambien: ya estan en disco y volver a leerlas
  // al abrir cuesta una lectura de LittleFS, no memoria permanente.
  if(gHist){ brHostFree(gHist); gHist = NULL; gHistN = 0; }
  if(gBm){ brHostFree(gBm); gBm = NULL; gBmN = 0; }
  if(gCmdQ){ vQueueDelete(gCmdQ); gCmdQ = NULL; }
  if(gUrlQ){ vQueueDelete(gUrlQ); gUrlQ = NULL; }
  if(gFrDone){ vSemaphoreDelete(gFrDone); gFrDone = NULL; }
  if(gMux){ vSemaphoreDelete(gMux); gMux = NULL; }

  gEditTarget = BRE_NONE;
  gMediaAvail = false; gMediaPlaying = false;
  gDlN = 0;
  brMeasure();
}

bool flexBrowserWantsClose(){
  if(!gWantClose) return false;
  gWantClose = false;
  return true;
}

bool flexBrowserHandleSystemBack(){
  if(gEditTarget != BRE_NONE){ flexBrowserKeyCancel(); return true; }
  if(gView == BRV_MENU || gView == BRV_TABS){ gView = gTabs[gTab].view; gNeedFullRedraw = true; return true; }
  if(gView == BRV_MEDIA){
    brPushCmd(FBP_C_MEDIA, FBP_CH_MEDIA, 0, 0, FBP_MED_STOP, 0, 0, 0, NULL);
    gView = gTabs[gTab].view; gMediaPlaying = false; gNeedFullRedraw = true;
    return true;
  }
  if(gTabs[gTab].view == BRV_INTERNAL && gTabs[gTab].page != BRP_NEWTAB){
    brOpenInternal(BRP_NEWTAB);
    return true;
  }
  if(gTabs[gTab].view == BRV_PAGE && (gTabs[gTab].stateFlags & FBP_ST_CAN_BACK)){
    brPushCmd(FBP_C_BACK, (uint8_t)(gTab + 1), 0, 0, 0, 0, 0, 0, NULL);
    return true;
  }
  return false;                       // el sistema cierra la app
}

// ---- entrada de teclado ----
bool flexBrowserKeyboardOpen(){ return gActive && gEditTarget != BRE_NONE; }
const char* flexBrowserEditText(){ return gEdit; }
const char* flexBrowserEditLabel(){
  switch(gEditTarget){
    case BRE_OMNIBOX:   return "Direcci\xC3\xB3n o b\xC3\xBAsqueda";
    case BRE_SERVER:    return "Servidor (wss://...)";
    case BRE_TOKEN:     return "Credencial del dispositivo";
    case BRE_SEARCHTPL: return "Plantilla del buscador (%s)";
    case BRE_PAGEKEY:   return "Escribir en la p\xC3\xA1gina";
    default:            return "";
  }
}

void flexBrowserKeyText(const char* utf8){
  if(!gActive || gEditTarget == BRE_NONE || !utf8 || !utf8[0]) return;
  if(gEditTarget == BRE_PAGEKEY){
    // Escritura directa en la pagina remota: no se acumula nada en el
    // dispositivo (y por tanto no queda ni rastro de lo tecleado).
    brPushCmd(FBP_C_KEY, (uint8_t)(gTab + 1), 0, 0, FBP_KEY_TEXT, 0, 0, 0, utf8);
    return;
  }
  size_t l = strlen(gEdit), a = strlen(utf8);
  if(l + a >= sizeof(gEdit) - 1) return;      // limite duro de entrada
  memcpy(gEdit + l, utf8, a);
  gEdit[l + a] = 0;
  gNeedChromeRedraw = true;
}

void flexBrowserKeyBackspace(){
  if(!gActive || gEditTarget == BRE_NONE) return;
  if(gEditTarget == BRE_PAGEKEY){
    brPushCmd(FBP_C_KEY, (uint8_t)(gTab + 1), 0, 0, FBP_KEY_DOWN, FBP_VK_BACKSPACE, 0, 0, NULL);
    return;
  }
  int l = (int)strlen(gEdit);
  if(l <= 0) return;
  int q = l - 1;
  while(q > 0 && (gEdit[q] & 0xC0) == 0x80) q--;   // retrocede un caracter UTF-8 entero
  gEdit[q] = 0;
  gNeedChromeRedraw = true;
}

void flexBrowserKeyEnter(){
  if(!gActive || gEditTarget == BRE_NONE) return;
  switch(gEditTarget){
    case BRE_OMNIBOX: brCommitOmnibox(); break;
    case BRE_SERVER: {
      bool tls; char host[FLEXBR_HOST_MAX]; uint16_t port; char path[128];
      // Se RECHAZA lo que no quepa en vez de recortarlo: una direccion de
      // servidor truncada apuntaria a otro sitio (o a ninguno) y el usuario
      // veria un fallo de conexion sin entender por que.
      if(strlen(gEdit) >= sizeof(gSt.server)){
        brSetError("La direcci\xC3\xB3n del servidor es demasiado larga");
      } else if(gEdit[0] && !flexBrParseWsUrl(gEdit, &tls, host, sizeof(host), &port, path, sizeof(path))){
        brSetError("Debe ser wss://host:puerto/ruta");
      } else {
        memcpy(gSt.server, gEdit, strlen(gEdit) + 1);
        brSaveSettings(); brComputeCaps();
        brSetError(gSt.server[0] ? "Servidor guardado" : "Servidor borrado");
      }
      gEditTarget = BRE_NONE;
      break;
    }
    case BRE_TOKEN:
      // Igual que el servidor: una credencial recortada seria rechazada por
      // el servicio y el mensaje de error no diria la verdad.
      if(strlen(gEdit) >= sizeof(gSt.token)){
        brSetError("La credencial es demasiado larga");
      } else {
        memcpy(gSt.token, gEdit, strlen(gEdit) + 1);
        brSaveSettings(); brComputeCaps();
        brSetError("Credencial guardada");
      }
      gEditTarget = BRE_NONE;
      break;
    case BRE_SEARCHTPL:
      if(strlen(gEdit) >= sizeof(gSt.searchCustom)){
        brSetError("La plantilla es demasiado larga");
      } else if(gEdit[0] && !strstr(gEdit, "%s")){
        brSetError("La plantilla debe contener %s");
      } else {
        memcpy(gSt.searchCustom, gEdit, strlen(gEdit) + 1);
        if(gSt.searchCustom[0]) gSt.searchEngine = 3;
        brSaveSettings();
      }
      gEditTarget = BRE_NONE;
      break;
    case BRE_PAGEKEY:
      brPushCmd(FBP_C_KEY, (uint8_t)(gTab + 1), 0, 0, FBP_KEY_DOWN, FBP_VK_ENTER, 0, 0, NULL);
      break;
  }
  gEdit[0] = 0;
  gNeedFullRedraw = true;
}

void flexBrowserKeyCancel(){
  if(!gActive) return;
  gEditTarget = BRE_NONE;
  gEdit[0] = 0;
  gView = gTabs[gTab].view;
  gNeedFullRedraw = true;
}

#else  // ---------------- app desactivada en compilacion ----------------

// Con FLEXBR_ON = 0 (o fuera del dispositivo) la app compila a nada y
// el .ino sigue enlazando: mismo patron que FLEXOS_OTA_ON.
#if FLEXBR_ON_DEVICE
void flexBrowserBegin(){}
void flexBrowserEnter(){}
void flexBrowserTick(){}
void flexBrowserExit(){}
bool flexBrowserWantsClose(){ return true; }
bool flexBrowserHandleSystemBack(){ return false; }
void flexBrowserKeyText(const char*){}
void flexBrowserKeyBackspace(){}
void flexBrowserKeyEnter(){}
void flexBrowserKeyCancel(){}
bool flexBrowserKeyboardOpen(){ return false; }
const char* flexBrowserEditText(){ return ""; }
const char* flexBrowserEditLabel(){ return ""; }
const BrStats* flexBrowserStats(){ static BrStats s; return &s; }
uint32_t flexBrowserCaps(){ return 0; }
int flexBrowserNetState(){ return BRN_OFF; }
const BrSettings* flexBrowserSettings(){ static BrSettings s; return &s; }
const char* flexBrowserCapReason(uint32_t){ return "Navegador desactivado en este build"; }
void flexBrowserFlushPersist(bool){}
void flexBrowserClearData(uint32_t){}
#endif

#endif // FLEXBR_ON_DEVICE && FLEXBR_ON
