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
#define WEB_TASK_STACK   12288
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
static FlexWebXfer       gWebEv[WEB_EV_N];
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
static void whEvent(void*, const FlexWebXfer* x){
  uint8_t w = gWebEvW, used = (uint8_t)(w - gWebEvR);
  bool progress = x->ev == FLEXWEB_EV_UP_PROGRESS || x->ev == FLEXWEB_EV_DL_PROGRESS;
  if(used >= WEB_EV_N || (progress && used >= WEB_EV_N - WEB_EV_KEEP)) return;
  gWebEv[w % WEB_EV_N] = *x;
  gWebEvW = (uint8_t)(w + 1);
}
static void whYield(void*){ vTaskDelay(1); }

// ---- Conexion (WiFiClient) ----
static WiFiServer* gWebSrv = NULL;
static bool whOthers(void*){ return gWebSrv && gWebSrv->hasClient(); }

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
  while(!gWebStop){
    if(!gNetOnline) break;
    if(gWebSrv->hasClient()){
      WiFiClient c = gWebSrv->accept();
      if(c){
        FlexWebConn conn = { wcRead, wcWrite, &c };
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
  if(!gWebHdr || !gWebIo){ gWebState = WEBS_FAIL; return false; }
  memset(&gWebCtx, 0, sizeof(gWebCtx));
  gWebCtx.fs = { msfOpen, msfRead, msfWrite, msfSeek, msfClose, msfSize, msfRemove, wfsFree, wfsTotal, NULL };
  gWebCtx.host = { whSnapshot, whGet, whRev, whDup, whCommit, whSetThumb, whThumbPath, whVerify, whLockType,
                   whNow, whEpoch, whRandom, whEvent, whYield, whOthers, NULL };
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
                  : gWebState == WEBS_STARTING ? "Arrancando\xE2\x80\xA6"
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
    int ns = flexWebSessionCount(&gWebCtx);
    char st[80];
    snprintf(st, sizeof(st), "%d m\xC3\xB3vil%s conectado%s  \xC2\xB7  %lu recibidos  \xC2\xB7  %lu enviados",
             ns, ns == 1 ? "" : "es", ns == 1 ? "" : "s", (unsigned long)gWebCtx.uploads, (unsigned long)gWebCtx.downloads);
    drawTextC(x + w / 2, cy, st, 1, TH_TXT2);
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
    } else if(c->state == WCS_CHECK) snprintf(line, sizeof(line), "Comprobando el archivo\xE2\x80\xA6");
    else snprintf(line, sizeof(line), "%s", c->msg);
    drawTextClip(x + pad + 14, cy + 30, line, 1, c->state == WCS_FAIL ? TH_ERR : TH_TXT2, x + w - pad - 20);
    int pw = w - 2 * pad - 28, pct = c->total ? (int)((uint64_t)c->done * 100 / c->total) : 0;
    if(c->state >= WCS_CHECK) pct = 100;
    fillRoundRect(x + pad + 14, cy + 46, pw, 5, 2, TH_SURF2);
    fillRoundRect(x + pad + 14, cy + 46, pw * pct / 100, 5, 2, c->state == WCS_FAIL ? TH_ERR : c->state == WCS_OK ? TH_OK : TH_PRIM);
    cy += 66;
  }
  flxFlush(WIN_TOP, WIN_BOT);
  gWebSheetMs = millis();
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
  if(millis() - gWebSheetMs > 400) webSheetRender();    // estado y progreso vivos
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
static void webTick(){
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
  while(gWebEvR != gWebEvW){
    FlexWebXfer x = gWebEv[gWebEvR % WEB_EV_N];
    gWebEvR = (uint8_t)(gWebEvR + 1);
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
        snprintf(c->msg, sizeof(c->msg), "%s", x.msg[0] ? x.msg : x.kind == FML_K_AUDIO ? "Guardado en M\xC3\xBAsica" : "Guardado en Galer\xC3\xAD" "a y Multimedia");
        char t[64]; snprintf(t, sizeof(t), "Recibido: %.40s", x.name);
        if(!webSheetOn) sysNotify("Flex Web Server", t);
        break;
      }
      case FLEXWEB_EV_UP_FAIL: {
        WebCard* c = webCardFor(x.name, true);
        c->state = WCS_FAIL; snprintf(c->msg, sizeof(c->msg), "%s", x.msg);
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
      case FLEXWEB_EV_DL_FAIL: { WebCard* c = webCardFor(x.name, false); c->state = WCS_FAIL; snprintf(c->msg, sizeof(c->msg), "%s", x.msg); break; }
      case FLEXWEB_EV_PAIRED:     sysNotify("Flex Web Server", "Un m\xC3\xB3vil se ha conectado"); break;
      case FLEXWEB_EV_OWNER:      sysNotify("Contenido protegido", "Se ha abierto desde un m\xC3\xB3vil con tu clave"); break;
      case FLEXWEB_EV_OWNER_FAIL: sysNotify("Contenido protegido", "Intento fallido desde un m\xC3\xB3vil"); break;
    }
  }
  (void)changed;
}
