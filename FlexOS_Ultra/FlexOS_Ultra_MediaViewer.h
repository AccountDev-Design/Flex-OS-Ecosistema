// #############################################################
// ##  FLEX OS ULTRA  ·  VISOR DE MEDIOS  ·  Galeria y Multimedia
// ##  ----------------------------------------------------------
// ##  El UNICO visor de fotos, dibujos y videos del sistema. Lo usan
// ##  la Galeria (dentro de la propia app, sin saltar a otra) y
// ##  Multimedia. No es una app: es un componente con un solo estado,
// ##  que pertenece a la app que esta en primer plano.
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
#include "FlexOS_Ultra_MediaKit.h"   // eslabon anterior de la cadena

// #############################################################
// ##  QUE ARREGLA Y COMO
// ##  ------------------------------------------------------
// ##  1. LA FOTO NO LLENABA SU HUECO. El decodificador solo reduce a
// ##     1/2, 1/4 u 1/8, y el visor anterior pintaba ese resultado tal
// ##     cual desde la esquina de arriba a la izquierda: una foto de
// ##     4000x3000 salia de 500x375 pegada a la esquina. Ahora la foto
// ##     se decodifica una vez (en la tarea de medios, leyendo por
// ##     trozos) a una resolucion suficiente y se ESCALA con precision
// ##     al tamano exacto que le toca, centrada y sin deformar.
// ##
// ##  2. EL VIDRIO SE APILABA. Cada Play/Pausa o toque volvia a pintar
// ##     el panel de controles ENCIMA de su propio dibujo, y el vidrio
// ##     desenfoca lo que tiene debajo: desenfocaba el vidrio anterior.
// ##     Ahora el contenido limpio (la foto o el fotograma, sin barras)
// ##     vive en su propio lienzo (vwClean) y las barras se componen
// ##     SIEMPRE sobre una copia limpia: dibujarlas cien veces da cien
// ##     veces los mismos pixeles.
// ##
// ##  3. HORIZONTAL A MEDIAS. gLand se ponia solo mientras se dibujaba
// ##     la imagen y se quitaba despues; los controles se pintaban con
// ##     gLand apagado y la barra del sistema se estampaba en vertical
// ##     encima: la "barra doble". Mientras el visor esta en horizontal,
// ##     gLand SE QUEDA puesto (como en Juegos y Modo PC): todo --imagen,
// ##     barras, tacto-- usa el mismo lienzo logico de 800x480.
// ##
// ##  4. NADA PESADO EN LA INTERFAZ. Decodificar una foto de varios MB
// ##     lo hace la tarea de medios (con el cerrojo de trabajo pesado),
// ##     mientras la interfaz ensena la miniatura ampliada. Cancelar
// ##     (cerrar, pasar a otra) es subir un numero de generacion.
// #############################################################

#define VW_BARS_HIDE_MS   3000u     // las barras se ocultan solas tras 3 s sin tocar
#define VW_FADE_MS        180u      // aparecer / desaparecer las barras
#define VW_OPEN_MS        240u      // expansion desde la miniatura
#define VW_ZOOM_MAX       5.0f      // sobre el tamano ajustado
#define VW_ZOOM_TAP       2.5f      // doble toque
#define VW_SRC_MAX_PX     (2u * 1024u * 1024u)   // foto decodificada: 2 MP como mucho (4 MB)
#define VW_WORK_BYTES     (1024u * 1024u)        // trabajo del decodificador en flujo
#define VW_FRAME_CAP      (192u * 1024u)         // un fotograma MJPEG comprimido
#define VW_SEEK_STEP_MS   10000u
#define VW_MAX_CATCHUP    4
#define VW_TILE_ROWS      8
#define VW_TAP2_MS        260u      // ventana del doble toque
#define VW_LIVE_MS        40u       // repintado durante un gesto (vecino mas cercano)
#define VW_CHECK_MS       400u      // comprobacion del elemento abierto
#define VW_TOP_H          52
#define VW_BOTP_H         66        // barra de la foto
#define VW_BOTV_H         106       // barra del video
#define VW_BTN_W          112       // boton de la barra de la foto
#define VW_GLT_MAX        ((LW - 20) * VW_TOP_H)
#define VW_GLB_MAX        (620 * VW_BOTV_H)
// Las barras del visor flotan sobre fotos de CUALQUIER color. El tinte
// adaptativo del vidrio (27 % como mucho) basta sobre un fondo de la
// interfaz, pero sobre una playa clara dejaba el texto claro ilegible. Aqui
// el tinte del tema cubre al menos el 59 %: sigue siendo el mismo material
// (fondo desenfocado, tinte del tema, especular y borde), con el texto
// siempre legible en las dos apariencias.
#define VW_GLASS_MIN_MIX  150

enum { VWK_NONE = 0, VWK_PHOTO, VWK_VIDEO, VWK_ERROR };
enum { VWB_NONE = 0, VWB_BACK, VWB_ROTATE, VWB_EDIT, VWB_TRASH, VWB_PLAY, VWB_BACK10, VWB_FWD10, VWB_SEEK };

// Lo que el visor recuerda de CADA app mientras ella esta en segundo plano:
// al volver, se reabre donde estaba. Lo protegido no se recuerda.
struct VwSession { bool open; uint32_t id; char path[FML_PATH_MAX]; uint32_t frame; };
// Lo que pone la app que abre el visor.
struct VwHost {
  uint8_t    app;                                   // IC_GALERIA / IC_MULTIMEDIA
  VwSession* sess;
  void     (*closed)();                             // el visor se cerro: la app vuelve a su lista
  uint32_t (*neighbour)(uint32_t id, int delta);    // anterior/siguiente en SU lista, sin protegidos (0 = no hay)
  void     (*edit)(uint32_t id);                    // NULL = sin "Editar"
  bool     (*editable)(uint32_t id);                // el editor puede con este elemento (NULL = no)
};

// ---- Estado (uno: solo hay un visor activo a la vez) ----
static const VwHost* vwHost = NULL;
static bool      vwOn = false;                      // activo, con sus recursos
static uint32_t  vwId = 0;                          // 0 = abierto por ruta (sin registro)
static char      vwPath[FML_PATH_MAX] = "";
static char      vwName[FML_NAME_MAX] = "";
static uint8_t   vwKind = VWK_NONE;
static char      vwErr[80] = "";
static bool      vwLocked = false;                  // protegido: se abrio con la clave del sistema
static bool      vwCanEdit = false, vwCanTrash = false;
static uint32_t  vwRecSize = 0, vwRecCrc = 0;
static uint32_t  vwSeenRev = 0, vwCheckMs = 0;
static bool      vwLand = false;                    // orientacion EFECTIVA del visor
static int       vwMediaW = 0, vwMediaH = 0;        // medidas reales del medio

static uint16_t* vwClean = NULL;                    // contenido limpio (sin barras), disposicion FISICA
static uint16_t* vwSrc = NULL;                      // foto decodificada (RGB565)
static int       vwSrcW = 0, vwSrcH = 0;
static uint16_t* vwThumb = NULL;                    // copia de la miniatura (ML_SIDE x ML_SIDE)
static bool      vwLoading = false;                 // esperando a la tarea de medios

// Transformacion: tamano ajustado (fit) * escala, en coordenadas LOGICAS.
static int   vwVX = 0, vwVY = 0, vwVW = 1, vwVH = 1; // hueco visible (sin la barra del sistema)
static int   vwFitW = 1, vwFitH = 1;
static float vwScale = 1.0f;
static float vwOffX = 0.0f, vwOffY = 0.0f;          // esquina de la imagen mostrada

// Video
static MediaStream vwStream;
static FlexAviCtx  vwAvi;
static uint8_t*  vwFrameBuf = NULL;
static uint32_t  vwFrameLen = 0;
static uint16_t* vwTile = NULL;
static uint16_t* vwXMap = NULL;
static bool      vwPlaying = false, vwEnded = false;
static uint32_t  vwCurFrame = 0;
static unsigned long vwNextUs = 0;

// Barras
static float     vwBarsA = 1.0f, vwBarsA0 = 1.0f;
static int8_t    vwBarsWant = 1;
static uint32_t  vwBarsT0 = 0;
static bool      vwBarsAnim = false;
static uint32_t  vwTouchMs = 0;                     // ultima interaccion (auto-ocultado)
static uint32_t  vwBarsDrawMs = 0;                  // ultimo repintado del progreso durante la reproduccion
static uint16_t* vwGlTop = NULL;                    // fondo YA desenfocado de cada barra
static uint16_t* vwGlBot = NULL;
static bool      vwGlOk = false;

// Gestos
static bool      vwPinchOn = false;
static float     vwPinD0 = 1.0f, vwPinS0 = 1.0f, vwPinCx0 = 0, vwPinCy0 = 0, vwPinOx0 = 0, vwPinOy0 = 0;
static bool      vwPanOn = false;
static int       vwPanTx0 = 0, vwPanTy0 = 0;
static float     vwPanOx0 = 0, vwPanOy0 = 0;
static uint32_t  vwLiveMs = 0;
static bool      vwLiveDirty = false;               // hay que pintar suave al acabar el gesto
static int       vwPressBtn = VWB_NONE;             // el dedo bajo en un boton de las barras
static bool      vwPressBar = false;
static int       vwDownX = 0, vwDownY = 0;
static uint32_t  vwTap1Ms = 0;
static int       vwTap1X = 0, vwTap1Y = 0;
static bool      vwTapPending = false;

// Expansion desde la miniatura
static bool      vwAnimOn = false;
static uint32_t  vwAnimT0 = 0;
static int       vwAnimFx = 0, vwAnimFy = 0, vwAnimFw = 0, vwAnimFh = 0;

// Gesto de dos dedos consumido por un pellizco (veto de la suspension).
// Se declara en FlexOS_Ultra_Touch.h: gTouchPinchUsed.

// -------------------------------------------------------------
//  POSICION DE CADA VIDEO (reanudar donde se dejo)
//  ------------------------------------------------------------
//  Huella de la ruta -> fotograma. Una tabla corta y fija: recordar los
//  ocho ultimos es lo util. Multimedia la guarda en su sesion.
// -------------------------------------------------------------
#define VW_RESUME_N 8
struct VwResumeSlot { uint32_t key; uint32_t frame; uint32_t whenMs; };
static VwResumeSlot vwResumeTab[VW_RESUME_N];
static uint32_t vwKeyOf(const char* path){
  uint32_t h = 2166136261u;
  for(const char* p = path; p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h ? h : 1u;
}
static uint32_t vwResumeGet(const char* path){
  uint32_t k = vwKeyOf(path);
  for(int i = 0; i < VW_RESUME_N; i++) if(vwResumeTab[i].key == k) return vwResumeTab[i].frame;
  return 0;
}
static void vwResumeSet(const char* path, uint32_t frame){
  uint32_t k = vwKeyOf(path);
  int slot = -1;
  for(int i = 0; i < VW_RESUME_N; i++) if(vwResumeTab[i].key == k){ slot = i; break; }
  if(slot < 0){
    slot = 0;
    for(int i = 1; i < VW_RESUME_N; i++)
      if(vwResumeTab[i].key == 0 || vwResumeTab[i].whenMs < vwResumeTab[slot].whenMs) slot = i;
  }
  vwResumeTab[slot].key = k; vwResumeTab[slot].frame = frame; vwResumeTab[slot].whenMs = millis();
}

// #############################################################
// ##  TRABAJO EN LA TAREA DE MEDIOS: decodificar la foto
// ##  ------------------------------------------------------
// ##  Un solo trabajo a la vez. La interfaz lo pide y lo recoge; la tarea
// ##  lo hace. Solo se comparten el estado (atomico) y la generacion: la
// ##  interfaz escribe los datos de la peticion cuando el trabajo esta
// ##  LIBRE, y la tarea escribe el resultado antes de publicarlo. Cancelar
// ##  es cambiar gVwWantGen: la decodificacion lo mira en cada fila y
// ##  para; lo que tuviera reservado lo suelta ella misma.
// #############################################################
enum { VWJ_IDLE = 0, VWJ_REQ, VWJ_BUSY, VWJ_DONE };
enum { VWJK_JPEG = 1, VWJK_DRAW };
struct VwJob {
  uint8_t   state;                 // VWJ_*: solo con __atomic
  uint32_t  gen;
  uint8_t   kind;
  int       wantW, wantH;
  char      path[FML_PATH_MAX];
  uint16_t* px; int w, h;          // resultado
  int       srcW, srcH;            // medidas reales del medio
  char      why[72];               // "" = bien
};
static VwJob gVwJob;
static volatile uint32_t gVwWantGen = 0;           // la generacion que la interfaz quiere AHORA (0 = ninguna)

// Divisor 1/2/4/8: el MAYOR que aun deja la imagen al doble del tamano
// ajustado (margen para ampliar con detalle), sin pasar de maxPx.
static int vwPickDiv(int w, int h, int wantW, int wantH, uint32_t maxPx){
  if(wantW < 1) wantW = 1;
  if(wantH < 1) wantH = 1;
  int S = 1;
  for(int d = 8; d >= 2; d >>= 1){
    if((w + d - 1) / d >= 2 * wantW && (h + d - 1) / d >= 2 * wantH){ S = d; break; }
  }
  while(S < 8 && (uint32_t)((w + S - 1) / S) * (uint32_t)((h + S - 1) / S) > maxPx) S <<= 1;
  return S;
}

struct VwRd { FlexFsStream* s; uint32_t last; };
static int vwRdFn(void* c, uint8_t* buf, size_t n){
  VwRd* r = (VwRd*)c;
  uint32_t now = millis();
  if(now - r->last >= 20u){ vTaskDelay(1); r->last = millis(); }   // comparte nucleo con la interfaz
  return flexFsStreamRead(r->s, buf, n);
}
struct VwDec { uint16_t* px; int w, h; uint32_t gen; int wantW, wantH; int srcW, srcH; bool noMem; };
static int vwDecPick(void* u, int w, int h){
  VwDec* d = (VwDec*)u;
  d->srcW = w; d->srcH = h;
  int S = vwPickDiv(w, h, d->wantW, d->wantH, VW_SRC_MAX_PX);
  const uint32_t keep = FLEXMEM_RESERVE_BYTES + ML_PSRAM_MARGIN;
  for(;;){
    int ow = (w + S - 1) / S, oh = (h + S - 1) / S;
    uint32_t bytes = (uint32_t)ow * (uint32_t)oh * 2u;
    if(memFreePsram() >= bytes + keep){
      d->px = (uint16_t*)mediaAlloc(bytes);
      if(d->px){ d->w = ow; d->h = oh; return S; }
    }
    if(S >= 8){ d->noMem = true; return 0; }        // ni a 1/8: no hay memoria
    S <<= 1;                                        // mas pequena, pero se ve
  }
}
static bool vwDecRow(void* u, int y, int w, const uint16_t* rgb){
  VwDec* d = (VwDec*)u;
  if(__atomic_load_n(&gVwWantGen, __ATOMIC_ACQUIRE) != d->gen) return false;   // cancelado
  if(y >= 0 && y < d->h) memcpy(d->px + (size_t)y * d->w, rgb, (size_t)(w < d->w ? w : d->w) * 2);
  return true;
}
static void vwJobJpeg(VwJob* j, uint32_t gen){
  if(memFreePsram() < VW_WORK_BYTES + FLEXMEM_RESERVE_BYTES){ snprintf(j->why, sizeof(j->why), "No hay memoria libre para abrir esta foto"); return; }
  FlexFsStream* s = flexFsOpenRead(j->path);
  if(!s){ snprintf(j->why, sizeof(j->why), "No se pudo leer el archivo"); return; }
  VwRd rd = { s, (uint32_t)millis() };
  VwDec d; memset(&d, 0, sizeof(d));
  d.gen = gen; d.wantW = j->wantW; d.wantH = j->wantH;
  int rc = flexJpegDecodeStream(vwRdFn, &rd, 0, 0, 0, vwDecPick, NULL, vwDecRow, &d, mediaAlloc, mediaFree);
  flexFsStreamClose(s);
  j->srcW = d.srcW; j->srcH = d.srcH;
  if(rc == FLEXJPG_OK && d.px){ j->px = d.px; j->w = d.w; j->h = d.h; return; }
  if(d.px) mediaFree(d.px);
  if(rc == FLEXJPG_ERR_ABORTED && !d.noMem) return;                  // cancelado: nada que decir
  snprintf(j->why, sizeof(j->why), "%s",
           d.noMem ? "No hay memoria libre para abrir esta foto"
                   : rc == FLEXJPG_ERR_UNSUPPORTED ? "Este JPEG (progresivo o CMYK) no se puede abrir aqu\xC3\xAD"
                                                   : flexJpegErrStr(rc));
}

// ---- Dibujo de Paint a su tamano real, con el grosor de cada trazo ----
struct VwPaint { uint16_t* px; int w, h; uint32_t gen; bool stop; };
static void vwPaintDot(VwPaint* p, int cx, int cy, int r, uint16_t c){
  for(int dy = -r; dy <= r; dy++){
    int y = cy + dy;
    if((unsigned)y >= (unsigned)p->h) continue;
    int span = isqrt32(r * r - dy * dy);
    int x0 = cx - span, x1 = cx + span;
    if(x0 < 0) x0 = 0;
    if(x1 > p->w - 1) x1 = p->w - 1;
    uint16_t* row = p->px + (size_t)y * p->w;
    for(int x = x0; x <= x1; x++) row[x] = c;
  }
}
static void vwPaintSeg(int x0, int y0, int x1, int y1, uint16_t color, int radius, void* user){
  VwPaint* p = (VwPaint*)user;
  if(p->stop) return;
  if(__atomic_load_n(&gVwWantGen, __ATOMIC_ACQUIRE) != p->gen){ p->stop = true; return; }
  int r = radius < 1 ? 1 : (radius > 60 ? 60 : radius);
  int step = r > 3 ? r / 2 : 1;                     // discos solapados: trazo continuo
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  int n = 0;
  for(int guard = 0; guard < 8192; guard++){
    if(n++ % step == 0) vwPaintDot(p, x0, y0, r, color);
    if(x0 == x1 && y0 == y1){ vwPaintDot(p, x0, y0, r, color); break; }
    int e2 = 2 * err;
    if(e2 >= dy){ err += dy; x0 += sx; }
    if(e2 <= dx){ err += dx; y0 += sy; }
  }
}
static void vwJobDraw(VwJob* j, uint32_t gen){
  FlexPaintHdr hd;
  if(!flexPaintHeader(j->path, &hd) || !hd.w || !hd.h){ snprintf(j->why, sizeof(j->why), "No es un dibujo que se pueda abrir"); return; }
  uint32_t bytes = (uint32_t)hd.w * hd.h * 2u;
  if(memFreePsram() < bytes + FLEXMEM_RESERVE_BYTES){ snprintf(j->why, sizeof(j->why), "No hay memoria libre para abrir este dibujo"); return; }
  uint16_t* px = (uint16_t*)mediaAlloc(bytes);
  if(!px){ snprintf(j->why, sizeof(j->why), "No hay memoria libre para abrir este dibujo"); return; }
  for(uint32_t i = 0; i < (uint32_t)hd.w * hd.h; i++) px[i] = 0xFFFF;
  VwPaint p = { px, hd.w, hd.h, gen, false };
  bool ok = flexPaintReplay(j->path, 1.0f, 0, 0, vwPaintSeg, &p);
  j->srcW = hd.w; j->srcH = hd.h;
  if(!ok || p.stop){
    mediaFree(px);
    if(!p.stop) snprintf(j->why, sizeof(j->why), "El dibujo est\xC3\xA1 da\xC3\xB1" "ado");
    return;
  }
  j->px = px; j->w = hd.w; j->h = hd.h;
}

// La llama la tarea de medios en cada vuelta (y la interfaz si esa tarea no
// existe). true = habia trabajo y se hizo.
static bool vwJobRunIfAny(){
  uint8_t st = VWJ_REQ;
  if(!__atomic_compare_exchange_n(&gVwJob.state, &st, (uint8_t)VWJ_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return false;
  VwJob* j = &gVwJob;
  const uint32_t gen = j->gen;
  j->px = NULL; j->w = j->h = 0; j->srcW = j->srcH = 0; j->why[0] = 0;
  // Turno de trabajo pesado: si una subida se esta comprobando, se espera a
  // que termine (segundos), no se decodifican dos fotos grandes a la vez.
  if(!mediaHeavyBegin(10000)) snprintf(j->why, sizeof(j->why), "Flex OS est\xC3\xA1 ocupado con otra imagen; vuelve a intentarlo");
  else {
    if(j->kind == VWJK_DRAW) vwJobDraw(j, gen); else vwJobJpeg(j, gen);
    mediaHeavyEnd();
  }
  if(__atomic_load_n(&gVwWantGen, __ATOMIC_ACQUIRE) != gen){
    if(j->px){ mediaFree(j->px); j->px = NULL; }
    __atomic_store_n(&j->state, (uint8_t)VWJ_IDLE, __ATOMIC_RELEASE);
    return true;
  }
  __atomic_store_n(&j->state, (uint8_t)VWJ_DONE, __ATOMIC_RELEASE);
  return true;
}

// ---- Lado de la interfaz ----
static uint32_t vwGen = 0, vwGenNext = 0;
static bool     vwJobQueued = false;
static uint8_t  vwQKind = VWJK_JPEG;
static int      vwQW = 0, vwQH = 0;

static void vwJobIssue(){
  VwJob* j = &gVwJob;
  j->gen = vwGen; j->kind = vwQKind; j->wantW = vwQW; j->wantH = vwQH;
  snprintf(j->path, sizeof(j->path), "%s", vwPath);
  vwJobQueued = false;
  __atomic_store_n(&j->state, (uint8_t)VWJ_REQ, __ATOMIC_RELEASE);
  if(gMlTask) mlWake();
  else vwJobRunIfAny();                             // sin tarea de medios: aqui mismo
}
// true = acaba de llegar el resultado de la peticion en curso (vwSrc o vwErr).
static bool vwJobPoll(){
  uint8_t st = __atomic_load_n(&gVwJob.state, __ATOMIC_ACQUIRE);
  bool got = false;
  if(st == VWJ_DONE){
    VwJob* j = &gVwJob;
    if(vwGen && j->gen == vwGen && __atomic_load_n(&gVwWantGen, __ATOMIC_ACQUIRE) == vwGen){
      if(vwSrc) mediaFree(vwSrc);
      vwSrc = j->px; vwSrcW = j->w; vwSrcH = j->h;
      if(j->srcW > 0 && j->srcH > 0){ vwMediaW = j->srcW; vwMediaH = j->srcH; }
      if(!vwSrc) snprintf(vwErr, sizeof(vwErr), "%s", j->why[0] ? j->why : "No se pudo abrir");
      vwLoading = false;
      got = true;
    } else if(j->px) mediaFree(j->px);             // de una peticion vieja
    j->px = NULL;
    __atomic_store_n(&j->state, (uint8_t)VWJ_IDLE, __ATOMIC_RELEASE);
    st = VWJ_IDLE;
  }
  if(st == VWJ_IDLE && vwJobQueued) vwJobIssue();
  if(got) return true;
  // La peticion recien emitida pudo resolverse en el acto (sin tarea de medios).
  if(!vwJobQueued && __atomic_load_n(&gVwJob.state, __ATOMIC_ACQUIRE) == VWJ_DONE && gVwJob.gen == vwGen) return vwJobPoll();
  return false;
}
// Recoge el resultado de la tarea, si lo hay, y deja el visor coherente con
// el: sin pixeles y con motivo, lo que se ensena es el motivo.
static bool vwCollect(){
  if(!vwJobPoll()) return false;
  if(!vwSrc && vwErr[0]) vwKind = VWK_ERROR;
  return true;
}
static void vwJobRequest(uint8_t kind, int wantW, int wantH){
  vwGen = ++vwGenNext;
  if(!vwGen) vwGen = ++vwGenNext;
  __atomic_store_n(&gVwWantGen, vwGen, __ATOMIC_RELEASE);
  vwQKind = kind; vwQW = wantW; vwQH = wantH;
  vwJobQueued = true;
  vwLoading = true;
}
static void vwJobCancel(){
  __atomic_store_n(&gVwWantGen, 0u, __ATOMIC_RELEASE);
  vwJobQueued = false; vwLoading = false; vwGen = 0;
  uint8_t st = VWJ_REQ;                             // aun sin recoger: se retira
  __atomic_compare_exchange_n(&gVwJob.state, &st, (uint8_t)VWJ_IDLE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  vwJobPoll();                                      // un resultado ya hecho se suelta
}

// #############################################################
// ##  LIENZO LOGICO
// #############################################################
static inline bool vwHostedNow(){ return gHosted || gRtTarget != NULL; }
static inline int vwCW(){ return vwHostedNow() ? gAppW : (vwLand ? LW : SCR_W); }
static inline int vwCH(){ return vwHostedNow() ? gAppH : (vwLand ? LH : SCR_H); }
// Pixel logico -> posicion en un buffer con la disposicion del panel.
static inline size_t vwIdx(int lx, int ly){
  return vwLand ? (size_t)lx * SCR_W + (size_t)(SCR_W - 1 - ly) : (size_t)ly * SCR_W + (size_t)lx;
}
static inline uint16_t* vwOut(){ return gRtTarget ? gRtTarget : fb; }
// Toque fisico -> lienzo logico (el MISMO camino que mediaTouchXY).
static inline void vwTouchXY(int px, int py, int &x, int &y){
  if(vwLand){ x = py; y = (SCR_W - 1) - px; }
  else      { x = px; y = py; }
}
// Filas FISICAS que ocupa un rectangulo logico.
static void vwRowsOf(int x, int y, int w, int h, int &r0, int &r1){
  if(vwLand){ r0 = x; r1 = x + w - 1; } else { r0 = y; r1 = y + h - 1; }
  if(r0 < 0) r0 = 0;
  if(r1 > SCR_H - 1) r1 = SCR_H - 1;
}
// El motor de dibujo en la orientacion del visor (lo que usan barras y textos).
static void vwEngine(){
  gLand = vwLand && !vwHostedNow();
  uiClipFull();
}

static void vwClamp(){
  if(vwScale < 1.0f) vwScale = 1.0f;
  if(vwScale > VW_ZOOM_MAX) vwScale = VW_ZOOM_MAX;
  float dw = vwFitW * vwScale, dh = vwFitH * vwScale;
  if(dw <= vwVW) vwOffX = vwVX + (vwVW - dw) * 0.5f;
  else {
    if(vwOffX > vwVX) vwOffX = (float)vwVX;
    if(vwOffX < vwVX + vwVW - dw) vwOffX = vwVX + vwVW - dw;
  }
  if(dh <= vwVH) vwOffY = vwVY + (vwVH - dh) * 0.5f;
  else {
    if(vwOffY > vwVY) vwOffY = (float)vwVY;
    if(vwOffY < vwVY + vwVH - dh) vwOffY = vwVY + vwVH - dh;
  }
}
// Hueco visible y ajuste. En vertical se descuenta la barra del sistema,
// que se estampa encima en cada volcado: la imagen no se mete debajo.
static int vwNavH(){ return (!vwLand && !vwHostedNow() && navBarVisible()) ? NAV_H : 0; }
static void vwLayout(){
  vwVX = 0; vwVY = 0; vwVW = vwCW();
  vwVH = vwCH() - vwNavH();
  int mw = vwMediaW > 0 ? vwMediaW : vwVW, mh = vwMediaH > 0 ? vwMediaH : vwVH;
  mediaFitBox(mw, mh, vwVW, vwVH, vwFitW, vwFitH);
  vwClamp();
}
static void vwResetView(){ vwScale = 1.0f; vwOffX = 0; vwOffY = 0; vwLayout(); }
// Cambia la escala conservando bajo (ax,ay) el mismo punto de la imagen.
static void vwZoomAt(float ns, float ax, float ay){
  if(ns < 1.0f) ns = 1.0f;
  if(ns > VW_ZOOM_MAX) ns = VW_ZOOM_MAX;
  float rx = (ax - vwOffX) / (vwFitW * vwScale), ry = (ay - vwOffY) / (vwFitH * vwScale);
  vwScale = ns;
  vwOffX = ax - rx * vwFitW * vwScale;
  vwOffY = ay - ry * vwFitH * vwScale;
  vwClamp();
}

// #############################################################
// ##  CONTENIDO LIMPIO (vwClean): foto, miniatura, fotograma o error
// #############################################################
static void vwClearClean(){ if(vwClean) memset(vwClean, 0, (size_t)SCR_W * SCR_H * 2); }

// Bilineal en RGB565 con pesos de 8 bits. Coordenadas 16.16.
static inline uint16_t vwBilerp(const uint16_t* src, int sw, int sh, int32_t fx, int32_t fy){
  if(fx < 0) fx = 0;
  if(fy < 0) fy = 0;
  int x = fx >> 16, y = fy >> 16;
  if(x >= sw - 1){ x = sw - 1; fx = (int32_t)x << 16; }
  if(y >= sh - 1){ y = sh - 1; fy = (int32_t)y << 16; }
  uint32_t ax = ((uint32_t)fx >> 8) & 0xFFu, ay = ((uint32_t)fy >> 8) & 0xFFu;
  const uint16_t* p = src + (size_t)y * sw + x;
  uint16_t c00 = p[0];
  uint16_t c10 = (x + 1 < sw) ? p[1] : c00;
  uint16_t c01 = (y + 1 < sh) ? p[sw] : c00;
  uint16_t c11 = (x + 1 < sw && y + 1 < sh) ? p[sw + 1] : c01;
  if(c00 == c10 && c00 == c01 && c00 == c11) return c00;
  uint32_t w00 = (256u - ax) * (256u - ay), w10 = ax * (256u - ay), w01 = (256u - ax) * ay, w11 = ax * ay;
  uint32_t r = ((c00 >> 11) * w00 + (c10 >> 11) * w10 + (c01 >> 11) * w01 + (c11 >> 11) * w11) >> 16;
  uint32_t g = (((c00 >> 5) & 63u) * w00 + ((c10 >> 5) & 63u) * w10 + ((c01 >> 5) & 63u) * w01 + ((c11 >> 5) & 63u) * w11) >> 16;
  uint32_t b = ((c00 & 31u) * w00 + (c10 & 31u) * w10 + (c01 & 31u) * w01 + (c11 & 31u) * w11) >> 16;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
static inline uint16_t vwNearest(const uint16_t* src, int sw, int sh, int32_t fx, int32_t fy){
  int x = (fx + 32768) >> 16, y = (fy + 32768) >> 16;
  if(x < 0) x = 0;
  if(x > sw - 1) x = sw - 1;
  if(y < 0) y = 0;
  if(y > sh - 1) y = sh - 1;
  return src[(size_t)y * sw + x];
}
// `src` (sw x sh) escalado al rectangulo logico (rx,ry,rw,rh), recortado al
// hueco visible, sobre vwClean. Recorre la memoria en el orden del panel:
// en vertical por filas; en horizontal por columnas logicas (= filas fisicas),
// asi las escrituras siempre son contiguas.
static void vwBlitScaled(const uint16_t* src, int sw, int sh, float rx, float ry, float rw, float rh, bool smooth){
  if(!vwClean || !src || sw <= 0 || sh <= 0 || rw < 1.0f || rh < 1.0f) return;
  int x0 = (int)floorf(rx), x1 = (int)ceilf(rx + rw), y0 = (int)floorf(ry), y1 = (int)ceilf(ry + rh);
  if(x0 < vwVX) x0 = vwVX;
  if(y0 < vwVY) y0 = vwVY;
  if(x1 > vwVX + vwVW) x1 = vwVX + vwVW;
  if(y1 > vwVY + vwVH) y1 = vwVY + vwVH;
  if(x1 > vwCW()) x1 = vwCW();
  if(y1 > vwCH()) y1 = vwCH();
  if(x0 >= x1 || y0 >= y1) return;
  const float kx = (float)sw / rw, ky = (float)sh / rh;
  const int32_t stepX = (int32_t)(kx * 65536.0f), stepY = (int32_t)(ky * 65536.0f);
  const int32_t fx0 = (int32_t)(((x0 + 0.5f - rx) * kx - 0.5f) * 65536.0f);
  const int32_t fy0 = (int32_t)(((y0 + 0.5f - ry) * ky - 0.5f) * 65536.0f);
  if(!vwLand){
    for(int ly = y0; ly < y1; ly++){
      int32_t fy = fy0 + (ly - y0) * stepY, fx = fx0;
      uint16_t* d = vwClean + (size_t)ly * SCR_W + x0;
      if(smooth) for(int lx = x0; lx < x1; lx++, fx += stepX) *d++ = vwBilerp(src, sw, sh, fx, fy);
      else       for(int lx = x0; lx < x1; lx++, fx += stepX) *d++ = vwNearest(src, sw, sh, fx, fy);
    }
  } else {
    for(int lx = x0; lx < x1; lx++){
      int32_t fx = fx0 + (lx - x0) * stepX, fy = fy0;
      uint16_t* d = vwClean + (size_t)lx * SCR_W + (size_t)(SCR_W - 1 - y0);   // ly crece -> x fisica decrece
      if(smooth) for(int ly = y0; ly < y1; ly++, fy += stepY) *d-- = vwBilerp(src, sw, sh, fx, fy);
      else       for(int ly = y0; ly < y1; ly++, fy += stepY) *d-- = vwNearest(src, sw, sh, fx, fy);
    }
  }
}

// Texto sobre vwClean (errores, "Abriendo..."). Mismas primitivas del
// sistema, con el lienzo del visor y su orientacion.
static void vwCleanText(int y, const char* s, int size, uint16_t col){
  uint16_t* ob = gBuf;
  gBuf = vwClean;
  vwEngine();
  drawTextC(vwVX + vwVW / 2, y, s, size, col);
  gBuf = ob;
}

// ---- Video: fotograma decodificado directamente al tamano exacto ----
struct VwVidMap { int decW, decH, x0, x1, y0, y1, nextLy; float offY, dh; };
static VwVidMap vwVm;
static int vwTileBase = 0, vwTileRows = 0;
static void vwTileFlush(){
  if(!vwTile || vwTileRows <= 0) return;
  const int w = vwVm.x1 - vwVm.x0;
  const int xStart = (SCR_W - 1) - (vwTileBase + vwTileRows - 1);
  for(int i = 0; i < w; i++){
    int lx = vwVm.x0 + i;
    if((unsigned)lx >= (unsigned)SCR_H || xStart < 0) continue;
    uint16_t* dst = vwClean + (size_t)lx * SCR_W + xStart;
    for(int k = 0; k < vwTileRows; k++) dst[k] = vwTile[(size_t)(vwTileRows - 1 - k) * LW + i];
  }
  vwTileRows = 0;
}
static void vwVidEmit(int ly, const uint16_t* rgb, int w){
  const int n = vwVm.x1 - vwVm.x0;
  if(!vwLand){
    uint16_t* d = vwClean + (size_t)ly * SCR_W + vwVm.x0;
    for(int i = 0; i < n; i++){ int sx = vwXMap[i]; d[i] = rgb[sx < w ? sx : w - 1]; }
    return;
  }
  if(vwTile){
    if(vwTileRows > 0 && ly != vwTileBase + vwTileRows) vwTileFlush();
    if(vwTileRows == 0) vwTileBase = ly;
    uint16_t* t = vwTile + (size_t)vwTileRows * LW;
    for(int i = 0; i < n; i++){ int sx = vwXMap[i]; t[i] = rgb[sx < w ? sx : w - 1]; }
    if(++vwTileRows >= VW_TILE_ROWS) vwTileFlush();
    return;
  }
  for(int i = 0; i < n; i++){                       // sin tira: correcto, mas lento
    int sx = vwXMap[i];
    vwClean[vwIdx(vwVm.x0 + i, ly)] = rgb[sx < w ? sx : w - 1];
  }
}
static bool vwVidRow(void*, int y, int w, const uint16_t* rgb){
  while(vwVm.nextLy < vwVm.y1){
    int sy = (int)(((vwVm.nextLy + 0.5f - vwVm.offY) * vwVm.decH) / vwVm.dh);
    if(sy < 0) sy = 0;
    if(sy >= vwVm.decH) sy = vwVm.decH - 1;
    if(sy > y) return true;                         // esta fila de salida sale de una fila posterior
    vwVidEmit(vwVm.nextLy, rgb, w);
    vwVm.nextLy++;
  }
  return false;                                     // ya estan todas las filas visibles: no se decodifica mas
}
// Decodifica el fotograma de vwFrameBuf sobre vwClean con la transformacion
// actual. El divisor es el MAYOR que aun cubre el tamano mostrado.
static int vwDecodeFrame(){
  if(!vwClean || !vwFrameBuf || !vwFrameLen || !vwXMap) return FLEXJPG_ERR_ARG;
  int W = vwAvi.width ? vwAvi.width : vwMediaW, H = vwAvi.height ? vwAvi.height : vwMediaH;
  if(W <= 0 || H <= 0) return FLEXJPG_ERR_ARG;
  float dw = vwFitW * vwScale, dh = vwFitH * vwScale;
  int d = 1;
  for(int c = 8; c >= 2; c >>= 1) if((float)((W + c - 1) / c) >= dw && (float)((H + c - 1) / c) >= dh){ d = c; break; }
  int mw = (W + d - 1) / d, mh = (H + d - 1) / d;
  int x0 = (int)floorf(vwOffX), x1 = (int)ceilf(vwOffX + dw), y0 = (int)floorf(vwOffY), y1 = (int)ceilf(vwOffY + dh);
  if(x0 < vwVX) x0 = vwVX;
  if(y0 < vwVY) y0 = vwVY;
  if(x1 > vwVX + vwVW) x1 = vwVX + vwVW;
  if(y1 > vwVY + vwVH) y1 = vwVY + vwVH;
  if(x1 - x0 > LW) x1 = x0 + LW;
  if(x0 >= x1 || y0 >= y1) return FLEXJPG_OK;
  vwVm.decW = mw; vwVm.decH = mh; vwVm.x0 = x0; vwVm.x1 = x1; vwVm.y0 = y0; vwVm.y1 = y1;
  vwVm.nextLy = y0; vwVm.offY = vwOffY; vwVm.dh = dh;
  for(int lx = x0; lx < x1; lx++){
    int sx = (int)(((lx + 0.5f - vwOffX) * mw) / dw);
    vwXMap[lx - x0] = (uint16_t)(sx < 0 ? 0 : (sx >= mw ? mw - 1 : sx));
  }
  vwTileRows = 0;
  int r = flexJpegDecode(vwFrameBuf, vwFrameLen, mw, mh, 0, NULL, vwVidRow, NULL, mediaAlloc, mediaFree);
  if(vwLand) vwTileFlush();
  if(r == FLEXJPG_ERR_ABORTED && vwVm.nextLy >= vwVm.y1) r = FLEXJPG_OK;
  return r;
}
// Lee el SIGUIENTE fotograma del archivo. false al final o con error.
static bool vwReadFrame(){
  if(!vwFrameBuf) return false;
  uint32_t fn = 0;
  int n = flexAviReadFrame(&vwAvi, vwFrameBuf, VW_FRAME_CAP, &fn);
  if(n == FLEXAVI_ERR_EOF){ vwEnded = true; vwPlaying = false; return false; }
  if(n < 0) return false;
  vwFrameLen = (uint32_t)n; vwCurFrame = fn;
  return true;
}

// Rehace vwClean entero. smooth = bilineal (en reposo); false = vecino mas
// cercano (durante un gesto, para seguir al dedo).
static void vwRenderContent(bool smooth){
  if(!vwClean) return;
  vwClearClean();
  if(vwKind == VWK_PHOTO){
    if(vwSrc) vwBlitScaled(vwSrc, vwSrcW, vwSrcH, vwOffX, vwOffY, vwFitW * vwScale, vwFitH * vwScale, smooth);
    else if(vwThumb){
      // La miniatura es el recorte CUADRADO del centro de la foto: va en el
      // cuadrado central del hueco que ocupara la foto entera.
      float dw = vwFitW * vwScale, dh = vwFitH * vwScale, s = dw < dh ? dw : dh;
      vwBlitScaled(vwThumb, ML_SIDE, ML_SIDE, vwOffX + (dw - s) * 0.5f, vwOffY + (dh - s) * 0.5f, s, s, true);
    } else vwCleanText(vwVY + vwVH / 2 - 8, "Abriendo...", 2, TH_TXT2);
  } else if(vwKind == VWK_VIDEO){
    int r = vwDecodeFrame();
    if(r != FLEXJPG_OK && vwFrameLen) vwCleanText(vwVY + vwVH / 2 - 8, flexJpegErrStr(r), 1, TH_TXT2);
  } else if(vwKind == VWK_ERROR){
    vwCleanText(vwVY + vwVH / 2 - 44, "No se puede abrir", 3, TH_ERR);
    vwCleanText(vwVY + vwVH / 2, vwName, 2, TH_TXT2);
    vwCleanText(vwVY + vwVH / 2 + 30, vwErr, 1, TH_MUTE);
  }
  vwGlOk = false;                                   // el fondo de las barras cambio
}

// #############################################################
// ##  BARRAS FLOTANTES
// ##  ------------------------------------------------------
// ##  Arriba: volver, nombre y orientacion. Abajo: en una foto, Editar
// ##  (si la app lo ofrece y se puede) y Papelera; en un video, progreso,
// ##  -10 s, reproducir/pausa, +10 s y Papelera. Geometria en el lienzo
// ##  LOGICO: dibujo y tacto leen las MISMAS funciones.
// #############################################################
static void vwTopGeom(int &x, int &y, int &w, int &h){ x = vwVX + 10; y = vwVY + 10; w = vwVW - 20; h = VW_TOP_H; }
static int  vwPhotoBtns(uint8_t* b){
  int n = 0;
  if(vwCanEdit)  b[n++] = VWB_EDIT;
  if(vwCanTrash) b[n++] = VWB_TRASH;
  return n;
}
static void vwBotGeom(int &x, int &y, int &w, int &h){
  if(vwKind == VWK_VIDEO){ w = vwVW - 20; if(w > 620) w = 620; h = VW_BOTV_H; }
  else { uint8_t b[2]; int n = vwPhotoBtns(b); w = n ? n * VW_BTN_W + 16 : 0; h = n ? VW_BOTP_H : 0; }
  x = vwVX + (vwVW - w) / 2;
  y = vwVY + vwVH - h - 12;
}
static bool vwHasBot(){ int x, y, w, h; vwBotGeom(x, y, w, h); return w > 0 && h > 0; }
// Pista de progreso del video.
static void vwTrackGeom(int &sx, int &sy, int &sw){
  int x, y, w, h; vwBotGeom(x, y, w, h);
  sx = x + 60; sw = w - 120; sy = y + 26;
}
// Centros de los botones del video.
struct VwVidBtns { int cy, playX, backX, fwdX, trashX; };
static void vwVidBtns(VwVidBtns &b){
  int x, y, w, h; vwBotGeom(x, y, w, h);
  b.cy = y + 72;
  b.playX = x + w / 2;
  b.backX = b.playX - 78;
  b.fwdX  = b.playX + 78;
  b.trashX = vwCanTrash ? x + w - 40 : -1000;
}

// Fondo YA desenfocado de cada barra, leido del contenido limpio.
static bool vwGlassPrepOne(uint16_t** dst, uint32_t cap, int x, int y, int w, int h){
  if(w <= 0 || h <= 0 || (uint32_t)w * h > cap) return false;
  if(!*dst) *dst = (uint16_t*)mediaAlloc((size_t)cap * 2);
  if(!*dst) return false;
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glassBuf) return false;
  for(int j = 0; j < h; j++){
    int ly = y + j;
    for(int i = 0; i < w; i++){
      int lx = x + i;
      glassBuf[(size_t)j * w + i] = (lx >= 0 && ly >= 0 && lx < vwCW() && ly < vwCH()) ? vwClean[vwIdx(lx, ly)] : 0;
    }
  }
  glassBlur(w, h, glassBlurR());
  memcpy(*dst, glassBuf, (size_t)w * h * 2);
  return true;
}
static void vwGlassPrep(){
  vwGlOk = false;
  if(!uiGlass || !vwClean) return;
  int x, y, w, h;
  vwTopGeom(x, y, w, h);
  bool a = vwGlassPrepOne(&vwGlTop, VW_GLT_MAX, x, y, w, h);
  bool b = true;
  if(vwHasBot()){ vwBotGeom(x, y, w, h); b = vwGlassPrepOne(&vwGlBot, VW_GLB_MAX, x, y, w, h); }
  vwGlOk = a && b;
}

// Superficie de una barra con el material del sistema: Liquid Glass (fondo
// desenfocado + tinte adaptativo + especular + borde) o Plano (solido).
// Siempre sobre el contenido LIMPIO de vwClean: nunca sobre si misma.
static void vwSurface(const uint16_t* blur, int x, int y, int w, int h, int rad, uint8_t a){
  if(a == 0 || w <= 0 || h <= 0) return;
  uint16_t* out = vwOut();
  const bool glass = uiGlass;
  const uint16_t tint = glass ? TH_GLASS2 : TH_SURF2;
  const int cw = vwCW(), ch = vwCH();
  uint8_t mix = 255;
  if(glass){
    uint32_t lumaSum = 0; int lumaN = 0;
    for(int j = 0; j < h; j += 4) for(int i = 0; i < w; i += 8){
      int lx = x + i, ly = y + j;
      if(lx < 0 || ly < 0 || lx >= cw || ly >= ch) continue;
      lumaSum += (uint32_t)glassLuma(blur ? blur[(size_t)j * w + i] : vwClean[vwIdx(lx, ly)]); lumaN++;
    }
    mix = glassTintMix(lumaSum, lumaN, tint);
    if(mix < VW_GLASS_MIN_MIX) mix = VW_GLASS_MIN_MIX;
  }
  if(2 * rad > w) rad = w / 2;
  if(2 * rad > h) rad = h / 2;
  for(int j = 0; j < h; j++){
    int ly = y + j;
    if(ly < 0 || ly >= ch) continue;
    int ins = glInset(j, h, rad);
    uint16_t shCol = 0; uint8_t shA = 0;
    if(glass) glassShadeRow(j, h, shCol, shA);
    uint16_t bcol = (j < 3) ? rgb565(255, 255, 255) : (j < h / 2 ? rgb565(205, 214, 228) : rgb565(22, 28, 40));
    bool topZone = (j < h / 2);
    uint8_t sL = topZone ? gGlCornS : gGlCornW, sR = topZone ? gGlCornW : gGlCornS;
    for(int i = ins; i < w - ins; i++){
      int lx = x + i;
      if(lx < 0 || lx >= cw) continue;
      int prow = vwLand ? lx : ly;
      if(prow < gClipY0 || prow > gClipY1) continue;
      size_t k = vwIdx(lx, ly);
      uint16_t c;
      if(glass){
        c = mix565(blur ? blur[(size_t)j * w + i] : vwClean[k], tint, mix);
        if(shA) c = mix565(c, shCol, shA);
        if(i == ins) c = mix565(c, bcol, sL);
        else if(i == w - 1 - ins) c = mix565(c, bcol, sR);
      } else c = tint;
      out[k] = (a >= 255) ? c : mix565(vwClean[k], c, a);
    }
  }
}

// ---- Iconos (vectoriales, como el resto del sistema) ----
static void vwIcoPencil(int cx, int cy, uint16_t col){
  strokeSegAA(cx - 8, cy + 8, cx + 6, cy - 6, 3.2f, col);
  strokeSegAA(cx + 5, cy - 9, cx + 9, cy - 5, 2.0f, col);
  fillTriangle(cx - 11, cy + 11, cx - 10, cy + 5, cx - 5, cy + 10, col);
}
static void vwIcoArc(int cx, int cy, int r, bool cw, uint16_t col){
  // Tres cuartos de circulo con la punta de flecha al final.
  const float a0 = cw ? -2.4f : -0.74f, a1 = cw ? 1.6f : 3.88f;
  const int N = 10;
  float px0 = cx + r * cosf(a0), py0 = cy + r * sinf(a0);
  for(int k = 1; k <= N; k++){
    float a = cw ? a0 + (a1 - a0) * k / N : a1 - (a1 - a0) * k / N;
    if(!cw && k == 1){ px0 = cx + r * cosf(a1); py0 = cy + r * sinf(a1); a = a1 - (a1 - a0) / N; }
    float x1 = cx + r * cosf(a), y1 = cy + r * sinf(a);
    strokeSegAA(px0, py0, x1, y1, 1.6f, col);
    px0 = x1; py0 = y1;
  }
  float ae = cw ? a1 : a0, tx = cx + r * cosf(ae), ty = cy + r * sinf(ae);
  float dx = -sinf(ae) * (cw ? 1.0f : -1.0f), dy = cosf(ae) * (cw ? 1.0f : -1.0f);
  fillTriangle((int)(tx + dx * 6), (int)(ty + dy * 6), (int)(tx - dy * 5), (int)(ty + dx * 5), (int)(tx + dy * 5), (int)(ty - dx * 5), col);
}

// Contenido de las barras. `a` = opacidad de la aparicion: el texto se
// acerca al color de la barra en vez de mezclarse pixel a pixel.
static void vwDrawTopBar(uint8_t a){
  int x, y, w, h; vwTopGeom(x, y, w, h);
  vwSurface(vwGlOk ? vwGlTop : NULL, x, y, w, h, 18, a);
  uint16_t base = uiGlass ? TH_GLASS2 : TH_SURF2;
  uint16_t fg = mix565(base, TH_TXT, a), fg2 = mix565(base, TH_TXT2, a);
  int cy = y + h / 2;
  strokeSegAA(x + 30, cy - 9, x + 20, cy, 2.6f, fg);         // volver
  strokeSegAA(x + 20, cy, x + 30, cy + 9, 2.6f, fg);
  if(!vwHostedNow()){                                        // orientacion (en una ventana de DeX no se gira)
    vwIcoArc(x + w - 30, cy, 11, true, fg);
    const char* om = gMediaOriMode == MORI_AUTO ? "Auto" : gMediaOriMode == MORI_PORT ? "Vertical" : "Horizontal";
    drawTextR(x + w - 50, cy - 4, om, 1, fg2);
  }
  int tx0 = x + 48, tx1 = x + w - (vwHostedNow() ? 16 : 130);
  int tw = textW(vwName, 1);
  int tx = tx0 + ((tx1 - tx0) - tw) / 2;
  if(tx < tx0) tx = tx0;
  drawTextClip(tx, cy - 4, vwName, 1, fg, tx1);
}
static void vwFmtTime(uint32_t ms, char* out, size_t n){
  uint32_t s = ms / 1000u;
  snprintf(out, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}
static uint32_t vwPosMs(){
  if(vwKind != VWK_VIDEO || !vwAvi.usPerFrame) return 0;
  return (uint32_t)(((uint64_t)vwCurFrame * vwAvi.usPerFrame) / 1000ull);
}
static void vwDrawBotBar(uint8_t a){
  int x, y, w, h; vwBotGeom(x, y, w, h);
  if(w <= 0) return;
  vwSurface(vwGlOk ? vwGlBot : NULL, x, y, w, h, 22, a);
  uint16_t base = uiGlass ? TH_GLASS2 : TH_SURF2;
  uint16_t fg = mix565(base, TH_TXT, a), fg2 = mix565(base, TH_TXT2, a);
  uint16_t acc = mix565(base, wallAccent(), a), onAcc = mix565(wallAccent(), TH_ONACC, a);
  if(vwKind != VWK_VIDEO){
    uint8_t b[2]; int n = vwPhotoBtns(b);
    for(int k = 0; k < n; k++){
      int cx = x + 8 + k * VW_BTN_W + VW_BTN_W / 2, cy = y + 24;
      if(b[k] == VWB_EDIT){ vwIcoPencil(cx, cy, fg); drawTextC(cx, y + 44, "Editar", 1, fg2); }
      else { mmGlyph(MA_TRASH, cx, cy + 2, fg); drawTextC(cx, y + 44, "Papelera", 1, fg2); }
    }
    return;
  }
  // ---- progreso REAL ----
  int sx, sy, sw; vwTrackGeom(sx, sy, sw);
  uint32_t dur = flexAviDurationMs(&vwAvi), pos = vwPosMs();
  fillRoundRect(sx, sy - 3, sw, 6, 3, mix565(base, TH_TRACK, a));
  int fw = dur > 0 ? (int)((uint64_t)sw * (pos > dur ? dur : pos) / dur) : 0;
  if(fw > 0) fillRoundRect(sx, sy - 3, fw, 6, 3, acc);
  if(dur > 0) fillCircle(sx + fw, sy, 8, fg);
  char t1[16], t2[16];
  vwFmtTime(pos, t1, sizeof(t1));
  if(dur > 0) vwFmtTime(dur, t2, sizeof(t2)); else snprintf(t2, sizeof(t2), "--:--");
  drawTextR(sx - 10, sy - 4, t1, 1, fg2);
  drawText(sx + sw + 10, sy - 4, t2, 1, fg2);
  // ---- transporte ----
  VwVidBtns bt; vwVidBtns(bt);
  fillCircle(bt.playX, bt.cy, 24, acc);
  if(vwPlaying){
    fillRect(bt.playX - 8, bt.cy - 10, 5, 20, onAcc);
    fillRect(bt.playX + 3, bt.cy - 10, 5, 20, onAcc);
  } else fillTriangle(bt.playX - 6, bt.cy - 11, bt.playX - 6, bt.cy + 11, bt.playX + 11, bt.cy, onAcc);
  vwIcoArc(bt.backX, bt.cy, 13, false, fg);
  drawTextC(bt.backX, bt.cy - 3, "10", 1, fg);
  vwIcoArc(bt.fwdX, bt.cy, 13, true, fg);
  drawTextC(bt.fwdX, bt.cy - 3, "10", 1, fg);
  if(vwCanTrash) mmGlyph(MA_TRASH, bt.trashX, bt.cy + 2, fg);
}
// Barras sobre lo que acaba de copiarse de vwClean, recortadas a las filas
// FISICAS [r0, r1] (las que se van a publicar).
static void vwDrawOverlays(int r0, int r1){
  uint8_t a = (uint8_t)(vwBarsA * 255.0f + 0.5f);
  if(a == 0) return;
  uint16_t* ob = gBuf;
  setBuf(fb);
  vwEngine();
  gClipY0 = r0; gClipY1 = r1;
  vwDrawTopBar(a);
  if(vwHasBot()) vwDrawBotBar(a);
  uiClipFull();
  gBuf = ob;
}

// ---- Publicar ----
// Copia las filas [r0, r1] del contenido limpio al panel, compone encima las
// barras y publica ESAS filas. Nada se dibuja nunca sobre un dibujo anterior.
static void vwPresent(int r0, int r1){
  if(!vwClean) return;
  if(r0 < 0) r0 = 0;
  if(r1 > SCR_H - 1) r1 = SCR_H - 1;
  if(r0 > r1) return;
  uint16_t* out = vwOut();
  memcpy(out + (size_t)r0 * SCR_W, vwClean + (size_t)r0 * SCR_W, (size_t)(r1 - r0 + 1) * SCR_W * 2);
  vwDrawOverlays(r0, r1);
  flxFlush(r0, r1);
}
static void vwPresentAll(){ vwPresent(0, SCR_H - 1); }
// Solo lo que ocupan las barras (aparecer/desaparecer, un boton que cambia).
static void vwPresentBars(){
  int x, y, w, h, r0, r1;
  vwTopGeom(x, y, w, h); vwRowsOf(x, y, w, h, r0, r1);
  if(vwHasBot()){
    int bx, by, bw, bh, b0, b1;
    vwBotGeom(bx, by, bw, bh); vwRowsOf(bx, by, bw, bh, b0, b1);
    if(b0 > r1 + 1 || r0 > b1 + 1){ vwPresent(r0, r1); vwPresent(b0, b1); return; }
    if(b0 < r0) r0 = b0;
    if(b1 > r1) r1 = b1;
  }
  vwPresent(r0, r1);
}
// Filas del contenido de la imagen (un fotograma nuevo).
static void vwPresentImage(){
  float dw = vwFitW * vwScale, dh = vwFitH * vwScale;
  int x0 = (int)floorf(vwOffX), y0 = (int)floorf(vwOffY);
  int x1 = (int)ceilf(vwOffX + dw), y1 = (int)ceilf(vwOffY + dh);
  if(x0 < vwVX) x0 = vwVX;
  if(y0 < vwVY) y0 = vwVY;
  if(x1 > vwVX + vwVW) x1 = vwVX + vwVW;
  if(y1 > vwVY + vwVH) y1 = vwVY + vwVH;
  int r0, r1; vwRowsOf(x0, y0, x1 - x0, y1 - y0, r0, r1);
  vwPresent(r0, r1);
}

// ---- Aparecer / desaparecer ----
static bool vwCanAutoHide(){
  if(vwKind == VWK_PHOTO) return !vwLoading;
  if(vwKind == VWK_VIDEO) return vwPlaying;
  return false;
}
static void vwBarsShow(bool on){
  int8_t want = on ? 1 : 0;
  vwTouchMs = millis();
  if(vwBarsWant == want && !vwBarsAnim) return;
  if(on && !vwGlOk) vwGlassPrep();
  vwBarsWant = want; vwBarsA0 = vwBarsA; vwBarsT0 = millis(); vwBarsAnim = true;
}
static void vwBarsTick(){
  if(!vwBarsAnim){
    if(vwBarsWant == 1 && vwCanAutoHide() && !T.down && !vwPinchOn && millis() - vwTouchMs >= VW_BARS_HIDE_MS)
      vwBarsShow(false);
    return;
  }
  float t = (millis() - vwBarsT0) / (float)VW_FADE_MS;
  if(t >= 1.0f){ t = 1.0f; vwBarsAnim = false; }
  float e = 1.0f - (1.0f - t) * (1.0f - t);
  float target = vwBarsWant ? 1.0f : 0.0f;
  vwBarsA = vwBarsA0 + (target - vwBarsA0) * e;
  if(!vwBarsAnim) vwBarsA = target;
  vwPresentBars();
}

// #############################################################
// ##  EXPANSION DESDE LA MINIATURA
// ##  ------------------------------------------------------
// ##  vwClean guarda UNA copia de la pantalla de donde se viene (la
// ##  rejilla). Cada cuadro se compone desde esa copia: fondo que se
// ##  oscurece y la miniatura creciendo desde su celda hasta el cuadrado
// ##  central de la foto. Nada se pinta sobre el cuadro anterior.
// #############################################################
static void vwAnimStep(){
  float t = (millis() - vwAnimT0) / (float)VW_OPEN_MS;
  if(t > 1.0f) t = 1.0f;
  float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
  uint16_t* out = vwOut();
  const uint8_t dark = (uint8_t)(e * 255.0f);
  const size_t N = (size_t)SCR_W * SCR_H;
  for(size_t i = 0; i < N; i++) out[i] = mix565(vwClean[i], 0, dark);
  // Destino: cuadrado central del ajuste en VERTICAL (la rejilla es vertical).
  int vh = SCR_H - vwNavH();
  int fw, fh; mediaFitBox(vwMediaW > 0 ? vwMediaW : SCR_W, vwMediaH > 0 ? vwMediaH : vh, SCR_W, vh, fw, fh);
  float s = (float)(fw < fh ? fw : fh);
  float tx = (SCR_W - s) * 0.5f, ty = (vh - s) * 0.5f;
  float rx = vwAnimFx + (tx - vwAnimFx) * e, ry = vwAnimFy + (ty - vwAnimFy) * e;
  float rw = vwAnimFw + (s - vwAnimFw) * e, rh = vwAnimFh + (s - vwAnimFh) * e;
  if(vwThumb && rw >= 1.0f && rh >= 1.0f){
    int x0 = (int)rx, y0 = (int)ry, x1 = (int)(rx + rw), y1 = (int)(ry + rh);
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > SCR_W) x1 = SCR_W;
    if(y1 > SCR_H) y1 = SCR_H;
    const float k = ML_SIDE / rw, kk = ML_SIDE / rh;
    for(int y = y0; y < y1; y++){
      int sy = (int)((y + 0.5f - ry) * kk); if(sy < 0) sy = 0; if(sy >= ML_SIDE) sy = ML_SIDE - 1;
      const uint16_t* row = vwThumb + (size_t)sy * ML_SIDE;
      uint16_t* d = out + (size_t)y * SCR_W;
      for(int x = x0; x < x1; x++){
        int sx = (int)((x + 0.5f - rx) * k); if(sx < 0) sx = 0; if(sx >= ML_SIDE) sx = ML_SIDE - 1;
        d[x] = row[sx];
      }
    }
  }
  flxFlushAll();
  if(t >= 1.0f) vwAnimOn = false;
}

// #############################################################
// ##  RECURSOS
// #############################################################
static void vwFreeBufs(){
  if(vwSrc){ mediaFree(vwSrc); vwSrc = NULL; }
  vwSrcW = vwSrcH = 0;
  if(vwClean){ mediaFree(vwClean); vwClean = NULL; }
  if(vwThumb){ memset(vwThumb, 0, (size_t)ML_SIDE * ML_SIDE * 2); mediaFree(vwThumb); vwThumb = NULL; }
  if(vwFrameBuf){ mediaFree(vwFrameBuf); vwFrameBuf = NULL; }
  vwFrameLen = 0;
  if(vwTile){ heap_caps_free(vwTile); vwTile = NULL; }
  if(vwXMap){ mediaFree(vwXMap); vwXMap = NULL; }
  if(vwGlTop){ mediaFree(vwGlTop); vwGlTop = NULL; }
  if(vwGlBot){ mediaFree(vwGlBot); vwGlBot = NULL; }
  vwGlOk = false;
}
// Suelta TODO lo del elemento abierto. keepPos = guardar por donde iba el video.
static void vwRelease(bool keepPos){
  if(vwKind == VWK_VIDEO && keepPos && vwPath[0] && !vwEnded) vwResumeSet(vwPath, vwCurFrame);
  if(vwHost && vwHost->sess && vwKind == VWK_VIDEO) vwHost->sess->frame = vwEnded ? 0 : vwCurFrame;
  vwJobCancel();
  vwPlaying = false;
  mediaStreamClose(&vwStream);
  memset(&vwAvi, 0, sizeof(vwAvi));
  vwFreeBufs();
  vwKind = VWK_NONE; vwEnded = false; vwCurFrame = 0; vwLoading = false;
  vwAnimOn = false; vwPinchOn = false; vwPanOn = false; vwTapPending = false;
  vwOn = false;
}

// Datos del elemento: del catalogo (id) o de la ruta.
static bool vwLoadItem(uint32_t id, const char* path){
  vwId = id; vwErr[0] = 0; vwLocked = false; vwCanEdit = false; vwCanTrash = false;
  vwMediaW = vwMediaH = 0; vwRecSize = vwRecCrc = 0;
  FlexMlRec r;
  bool have = id && mlGet(id, &r);
  if(id && !have) return false;
  if(have){
    snprintf(vwPath, sizeof(vwPath), "%s", r.path);
    flexMlDisplayName(&r, vwName, sizeof(vwName));
    vwLocked = (r.flags & FML_R_LOCKED) != 0;
    vwMediaW = r.w; vwMediaH = r.h; vwRecSize = r.size; vwRecCrc = r.crc;
    vwCanTrash = !vwLocked;
    vwCanEdit = vwHost && vwHost->edit && vwHost->editable && vwHost->editable(id);
    if(r.kind == FML_K_VIDEO) vwKind = (r.fmt == FML_F_AVI_MJPEG) ? VWK_VIDEO : VWK_ERROR;
    else if(r.kind == FML_K_PHOTO || r.kind == FML_K_DRAW) vwKind = (r.fmt == FML_F_JPEG || r.kind == FML_K_DRAW) ? VWK_PHOTO : VWK_ERROR;
    else vwKind = VWK_ERROR;
    if(vwKind == VWK_ERROR){
      const char* why = r.state == FML_S_ERROR ? "El archivo est\xC3\xA1 da\xC3\xB1" "ado" : flexMlWhyUnplayable(r.fmt);
      snprintf(vwErr, sizeof(vwErr), "%s", why ? why : "Formato no compatible");
    }
  } else {
    snprintf(vwPath, sizeof(vwPath), "%s", path ? path : "");
    const char* nm = strrchr(vwPath, '/');
    mlCopyText(vwName, sizeof(vwName), nm ? nm + 1 : vwPath);   // recorta sin partir un caracter UTF-8
    int k = flexMediaClassify(vwName);
    vwKind = (k == FLEXMED_PHOTO || k == FLEXMED_DRAW) ? VWK_PHOTO : k == FLEXMED_VIDEO ? VWK_VIDEO : VWK_ERROR;
    if(vwKind == VWK_ERROR){
      const char* why = k == FLEXMED_AUDIO ? "El audio se escucha en M\xC3\xBAsica" : flexMediaUnsupportedReason(vwName);
      snprintf(vwErr, sizeof(vwErr), "%s", why ? why : "Formato no compatible");
    }
  }
  if(!vwPath[0] || !mediaVolReady(vwPath)){ vwKind = VWK_ERROR; snprintf(vwErr, sizeof(vwErr), "Sin almacenamiento"); }
  return true;
}
// Copia de la miniatura (para la expansion y la vista previa).
static void vwGrabThumb(){
  if(!vwId || vwLocked) return;
  if(!vwThumb) vwThumb = (uint16_t*)mediaAlloc((size_t)ML_SIDE * ML_SIDE * 2);
  if(!vwThumb) return;
  bool ok = false;
  mlLock();
  int i = flexMlFindId(&gMs.lib, vwId);
  if(i >= 0){
    int budget = 1; bool more = false;
    const uint16_t* t = mlThumbGetLocked(&gMs.lib.recs[i], &budget, &more);
    if(t && gMs.lib.recs[i].kind != FML_K_DRAW){ memcpy(vwThumb, t, (size_t)ML_SIDE * ML_SIDE * 2); ok = true; }
  }
  mlUnlock();
  if(!ok){ mediaFree(vwThumb); vwThumb = NULL; }
}
// Tamano que se le pide a la decodificacion: el mayor ajuste de las dos
// orientaciones (girar no obliga a decodificar otra vez).
static void vwWantSize(int &w, int &h){
  int mw = vwMediaW > 0 ? vwMediaW : SCR_H, mh = vwMediaH > 0 ? vwMediaH : SCR_H;
  int pw, ph, lw, lh;
  mediaFitBox(mw, mh, SCR_W, SCR_H - NAV_H, pw, ph);
  mediaFitBox(mw, mh, LW, LH, lw, lh);
  w = pw > lw ? pw : lw; h = ph > lh ? ph : lh;
}
static void vwApplyOrientation(){
  vwLand = !vwHostedNow() && mediaOriLandscape(vwMediaW, vwMediaH);
  if(vwMediaW > 0 && vwMediaW == vwMediaH) gMediaSquareLand = vwLand;
  vwEngine();
}
// Arranca el contenido: pide la foto o abre el video (sin pintar).
static void vwStartContent(uint32_t frame){
  if(vwKind == VWK_PHOTO){
    FlexMlRec r;
    bool draw = (vwId && mlGet(vwId, &r)) ? (r.kind == FML_K_DRAW) : (flexMediaClassify(vwName) == FLEXMED_DRAW);
    int ww, wh; vwWantSize(ww, wh);
    vwJobRequest(draw ? VWJK_DRAW : VWJK_JPEG, ww, wh);
    return;
  }
  if(vwKind != VWK_VIDEO) return;
  memset(&vwStream, 0, sizeof(vwStream));
  if(!mediaStreamOpen(&vwStream, vwPath)){ vwKind = VWK_ERROR; snprintf(vwErr, sizeof(vwErr), "No se pudo abrir el archivo"); return; }
  FlexMediaIO io; mediaBindIO(&io, &vwStream);
  int r = flexAviOpen(&vwAvi, &io);
  if(r != FLEXAVI_OK){ vwKind = VWK_ERROR; snprintf(vwErr, sizeof(vwErr), "%s", flexAviErrStr(r)); return; }
  vwFrameBuf = (uint8_t*)mediaAlloc(VW_FRAME_CAP);
  vwXMap = (uint16_t*)mediaAlloc((size_t)LW * 2);
  if(!vwFrameBuf || !vwXMap){ vwKind = VWK_ERROR; snprintf(vwErr, sizeof(vwErr), "Sin memoria para el v\xC3\xAD" "deo"); return; }
  // La tira del volcado girado: RAM interna si sobra (se escribe por columnas).
  size_t tb = (size_t)LW * VW_TILE_ROWS * 2;
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > tb + 64u * 1024u)
    vwTile = (uint16_t*)heap_caps_malloc(tb, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if(!vwTile) vwTile = (uint16_t*)mediaAlloc(tb);
  vwMediaW = vwAvi.width; vwMediaH = vwAvi.height;
  if(frame == 0) frame = vwResumeGet(vwPath);
  vwCurFrame = 0;
  if(frame > 0 && vwAvi.frames > 0 && frame < vwAvi.frames - 1){
    int landed = flexAviSeekFrame(&vwAvi, frame);
    if(landed >= 0) vwCurFrame = (uint32_t)landed;
  }
  vwPlaying = false; vwEnded = false;
  vwNextUs = micros();
  vwReadFrame();                                    // el cuadro con el que se abre (en pausa)
}

// Activa el visor para el elemento de la sesion del anfitrion. `from` = celda
// de origen (expansion) o NULL. false = no se pudo (se dijo por que).
static bool vwActivate(const int* from, uint32_t frame){
  VwSession* s = vwHost->sess;
  vwClean = (uint16_t*)mediaAlloc((size_t)SCR_W * SCR_H * 2);
  if(!vwClean){
    sysNotify(vwHost->app == IC_GALERIA ? "Galer\xC3\xAD" "a" : "Multimedia", "No hay memoria libre para abrir el visor");
    s->open = false;
    return false;
  }
  if(!vwLoadItem(s->id, s->path)){
    mediaFree(vwClean); vwClean = NULL;
    s->open = false;
    return false;
  }
  vwOn = true;
  vwSeenRev = mlRev(); vwCheckMs = millis();
  vwScale = 1.0f;
  vwBarsA = 1.0f; vwBarsWant = 1; vwBarsAnim = false; vwTouchMs = millis();
  vwPressBtn = VWB_NONE; vwPressBar = false; vwTapPending = false; vwPinchOn = false; vwPanOn = false;
  vwGrabThumb();
  // Expansion: solo desde una celda visible y con miniatura (lo protegido
  // llega despues de la clave, sin rejilla detras: se abre directamente).
  vwAnimOn = false;
  if(from && vwThumb && !vwHostedNow() && from[2] > 0 && from[3] > 0){
    memcpy(vwClean, fb, (size_t)SCR_W * SCR_H * 2);
    vwAnimFx = from[0]; vwAnimFy = from[1]; vwAnimFw = from[2]; vwAnimFh = from[3];
    vwAnimT0 = millis(); vwAnimOn = true;
    gLand = false; uiClipFull();
  }
  vwStartContent(frame);
  vwCollect();                                      // emite la peticion (y la recoge si ya se resolvio)
  if(!vwAnimOn){
    vwApplyOrientation();
    vwResetView();
    vwRenderContent(true);
    vwGlassPrep();
    vwPresentAll();
  }
  return true;
}

// #############################################################
// ##  API PARA LAS APPS
// #############################################################
static bool vwHostOpen(const VwHost* h){ return h && h->sess && h->sess->open; }
static bool vwActiveFor(const VwHost* h){ return vwOn && vwHost == h; }

// Reactiva el visor de `h` si su sesion sigue abierta (volver de segundo
// plano, cerrar el editor). true = activo.
static bool vwEnsure(const VwHost* h){
  if(!vwHostOpen(h)) return false;
  if(vwActiveFor(h)) return true;
  if(vwOn) vwRelease(true);                         // otra app lo tenia: se suelta
  vwHost = h;
  return vwActivate(NULL, h->sess->frame);
}

static void vwOpen(const VwHost* h, uint32_t id, const char* path, const int* from){
  if(!h || !h->sess) return;
  if(vwOn){
    // Otra app con su visor abierto: su sesion se conserva (sin recursos).
    if(vwHost && vwHost != h && vwHost->sess) vwHost->sess->frame = vwCurFrame;
    vwRelease(true);
  }
  vwHost = h;
  VwSession* s = h->sess;
  s->open = true; s->id = id; s->frame = 0;
  if(id){ FlexMlRec r; if(mlGet(id, &r)) snprintf(s->path, sizeof(s->path), "%s", r.path); }
  else snprintf(s->path, sizeof(s->path), "%s", path ? path : "");
  gMediaOriMode = MORI_AUTO;                        // cada archivo empieza en Auto
  vwActivate(from, 0);
}

// Cierra el visor y devuelve la pantalla a la app.
static void vwClose(){
  const VwHost* h = vwHost;
  vwRelease(true);
  if(h && h->sess) h->sess->open = false;
  vwHost = NULL;
  gLand = false;
  uiClipFull();
  setBuf(fb);
  if(h && h->closed) h->closed();
}
// Cierra SIN avisar a la app (la app se esta cerrando).
static void vwForget(const VwHost* h){
  if(h && h->sess) h->sess->open = false;
  if(vwActiveFor(h)){ vwRelease(true); vwHost = NULL; gLand = false; uiClipFull(); }
}
// A segundo plano: se suelta todo y se recuerda donde estaba. Lo protegido
// NO se recuerda: al volver hace falta la clave otra vez.
static void vwSuspend(){
  if(!vwOn) return;
  const VwHost* h = vwHost;
  bool forget = vwLocked;
  vwRelease(true);
  if(h && h->sess && forget) h->sess->open = false;
  gLand = false;
  uiClipFull();
}
// Memoria justa: el visor en segundo plano no guarda nada (vwSuspend ya lo
// solto). En primer plano no se toca lo que se esta viendo.
static size_t vwShed(){ return 0; }

// Recompone la pantalla entera (volver de una capa, repintado de la app).
static void vwRender(){
  if(!vwOn) return;
  if(vwAnimOn){ vwAnimStep(); return; }
  vwEngine();
  vwLayout();
  vwRenderContent(true);
  if(vwBarsA > 0.0f) vwGlassPrep();
  vwPresentAll();
}

// ---- Acciones ----
static void vwOpenOther(uint32_t id){
  if(!id || !vwHost) return;
  const VwHost* h = vwHost;
  vwRelease(true);
  vwHost = h;
  VwSession* s = h->sess;
  s->open = true; s->id = id; s->frame = 0;
  FlexMlRec r;
  if(mlGet(id, &r)) snprintf(s->path, sizeof(s->path), "%s", r.path);
  gMediaOriMode = MORI_AUTO;
  vwActivate(NULL, 0);
}
static void vwGoNeighbour(int delta){
  if(!vwHost || !vwHost->neighbour || !vwId) return;
  uint32_t id = vwHost->neighbour(vwId, delta);
  if(id && id != vwId) vwOpenOther(id);
}
// A la papelera (recuperable). Lo protegido no: su boton ni aparece.
static void vwDoTrash(){
  if(!vwCanTrash || !vwId) return;
  uint32_t id = vwId;
  uint32_t next = vwHost && vwHost->neighbour ? vwHost->neighbour(id, +1) : 0;
  if(!next && vwHost && vwHost->neighbour) next = vwHost->neighbour(id, -1);
  char nm[FML_NAME_MAX]; snprintf(nm, sizeof(nm), "%s", vwName);
  mediaStreamClose(&vwStream);                      // nadie con el archivo abierto mientras se mueve
  bool ok = mlTrash(id);
  sysNotify(nm, ok ? "Movido a la papelera" : "No se pudo mover a la papelera");
  if(!ok){ vwRender(); return; }
  if(next && next != id) vwOpenOther(next);
  else vwClose();
}
static void vwDoEdit(){
  if(!vwCanEdit || !vwHost || !vwHost->edit || !vwId) return;
  uint32_t id = vwId;
  const VwHost* h = vwHost;
  vwRelease(true);                                  // el editor necesita la memoria; la sesion sigue abierta
  gLand = false; uiClipFull();
  h->edit(id);
}
static void vwCycleOrientation(){
  if(vwHostedNow()) return;
  gMediaOriMode = (uint8_t)((gMediaOriMode + 1) % 3);
  float cxr = (vwVX + vwVW * 0.5f - vwOffX) / (vwFitW * vwScale);   // punto de la imagen en el centro
  float cyr = (vwVY + vwVH * 0.5f - vwOffY) / (vwFitH * vwScale);
  vwApplyOrientation();
  vwLayout();
  vwOffX = vwVX + vwVW * 0.5f - cxr * vwFitW * vwScale;
  vwOffY = vwVY + vwVH * 0.5f - cyr * vwFitH * vwScale;
  vwClamp();
  vwRenderContent(true);
  vwGlassPrep();
  vwPresentAll();
}
static void vwSeekMs(uint32_t ms){
  if(vwKind != VWK_VIDEO || !vwAvi.usPerFrame) return;
  uint32_t target = (uint32_t)(((uint64_t)ms * 1000ull) / vwAvi.usPerFrame);
  if(vwAvi.frames && target >= vwAvi.frames) target = vwAvi.frames - 1;
  int landed = flexAviSeekFrame(&vwAvi, target);
  if(landed < 0){ vwKind = VWK_ERROR; snprintf(vwErr, sizeof(vwErr), "Se perdi\xC3\xB3 el acceso al archivo"); vwRender(); return; }
  vwEnded = false;
  vwNextUs = micros();
  if(vwReadFrame()) vwRenderContent(false);
  if(!vwPlaying) vwGlassPrep();
  vwPresentAll();
}
static void vwTogglePlay(){
  if(vwKind != VWK_VIDEO) return;
  if(vwEnded){ vwSeekMs(0); vwEnded = false; }
  vwPlaying = !vwPlaying;
  vwNextUs = micros();
  if(!vwPlaying) vwGlassPrep();                     // en pausa el fondo es fijo: vidrio completo
  vwPresentBars();
}

// ---- Que boton hay en (x,y) del lienzo logico ----
// -1 = fuera de las barras; VWB_NONE = en una barra, sin boton; >0 = boton.
static int vwHitBtn(int x, int y){
  if(vwBarsA < 0.5f) return -1;
  int bx, by, bw, bh;
  vwTopGeom(bx, by, bw, bh);
  if(y >= by - 6 && y <= by + bh + 6 && x >= bx && x <= bx + bw){
    if(x < bx + 64) return VWB_BACK;
    if(!vwHostedNow() && x > bx + bw - 140) return VWB_ROTATE;
    return VWB_NONE;
  }
  if(!vwHasBot()) return -1;
  vwBotGeom(bx, by, bw, bh);
  if(y < by - 6 || y > by + bh + 6 || x < bx - 6 || x > bx + bw + 6) return -1;
  if(vwKind != VWK_VIDEO){
    uint8_t b[2]; int n = vwPhotoBtns(b);
    int k = (x - bx - 8) / VW_BTN_W;
    if(k < 0) k = 0;
    if(k >= n) k = n - 1;
    return n ? (int)b[k] : (int)VWB_NONE;
  }
  int sx, sy, sw; vwTrackGeom(sx, sy, sw);
  if(y <= sy + 16 && x >= sx - 16 && x <= sx + sw + 16) return VWB_SEEK;
  VwVidBtns t; vwVidBtns(t);
  if(abs(x - t.playX) <= 30) return VWB_PLAY;
  if(abs(x - t.backX) <= 30) return VWB_BACK10;
  if(abs(x - t.fwdX) <= 30) return VWB_FWD10;
  if(vwCanTrash && abs(x - t.trashX) <= 30) return VWB_TRASH;
  return VWB_NONE;
}
static void vwDoBtn(int b, int x){
  vwTouchMs = millis();
  switch(b){
    case VWB_BACK:   vwClose(); return;
    case VWB_ROTATE: vwCycleOrientation(); return;
    case VWB_EDIT:   vwDoEdit(); return;
    case VWB_TRASH:  vwDoTrash(); return;
    case VWB_PLAY:   vwTogglePlay(); return;
    case VWB_BACK10: { uint32_t p = vwPosMs(); vwSeekMs(p > VW_SEEK_STEP_MS ? p - VW_SEEK_STEP_MS : 0); return; }
    case VWB_FWD10:  vwSeekMs(vwPosMs() + VW_SEEK_STEP_MS); return;
    case VWB_SEEK: {
      int sx, sy, sw; vwTrackGeom(sx, sy, sw);
      uint32_t dur = flexAviDurationMs(&vwAvi);
      if(dur > 0 && sw > 0){
        int rel = x - sx; if(rel < 0) rel = 0; if(rel > sw) rel = sw;
        vwSeekMs((uint32_t)((uint64_t)dur * (uint32_t)rel / (uint32_t)sw));
      }
      return;
    }
  }
}

// ---- Repintado durante un gesto ----
static void vwLive(bool force){
  if(!force && millis() - vwLiveMs < VW_LIVE_MS){ vwLiveDirty = true; return; }
  vwLiveMs = millis();
  vwRenderContent(false);                           // vecino mas cercano: sigue al dedo
  vwPresentAll();
  vwLiveDirty = true;                               // al soltar se pinta suave
}
static void vwLiveEnd(){
  if(!vwLiveDirty) return;
  vwLiveDirty = false;
  vwRenderContent(true);
  if(vwBarsA > 0.0f) vwGlassPrep();
  vwPresentAll();
}

// ---- Pellizco (dos dedos, lectura multipunto real del GT911) ----
// La cuenta, separada de la lectura del chip: centro de los dedos (cx,cy) y
// distancia entre ellos (d), ya en el lienzo logico.
static void vwPinchBegin(float cx, float cy, float d){
  if(d < 10.0f) d = 10.0f;
  vwPinchOn = true; vwPinD0 = d; vwPinS0 = vwScale;
  vwPinCx0 = cx; vwPinCy0 = cy; vwPinOx0 = vwOffX; vwPinOy0 = vwOffY;
}
// true = la vista cambio. El punto de la imagen que estaba bajo el centro de
// los dedos al empezar sigue bajo el centro de los dedos ahora: ampliar y
// desplazar a la vez, sin saltos.
static bool vwPinchApply(float cx, float cy, float d){
  if(d < 10.0f) d = 10.0f;
  float ns = vwPinS0 * d / vwPinD0;
  if(ns < 1.0f) ns = 1.0f;
  if(ns > VW_ZOOM_MAX) ns = VW_ZOOM_MAX;
  float rx = (vwPinCx0 - vwPinOx0) / (vwFitW * vwPinS0), ry = (vwPinCy0 - vwPinOy0) / (vwFitH * vwPinS0);
  float os = vwScale, ox = vwOffX, oy = vwOffY;
  vwScale = ns;
  vwOffX = cx - rx * vwFitW * ns;
  vwOffY = cy - ry * vwFitH * ns;
  vwClamp();
  return fabsf(vwScale - os) > 0.001f || fabsf(vwOffX - ox) > 0.5f || fabsf(vwOffY - oy) > 0.5f;
}
static bool vwPinchStep(){
  if(vwKind != VWK_PHOTO && vwKind != VWK_VIDEO) return false;
  uint8_t fingers = (millis() - gtFingersMs > 150) ? 0 : gtFingers;
  if(fingers < 2){
    if(vwPinchOn && fingers == 0){ vwPinchOn = false; vwLiveEnd(); }
    return vwPinchOn;                               // hasta soltar los dos dedos, el toque es del gesto
  }
  int n = gtPollMulti();
  if(n < 2) return vwPinchOn;
  int ax = 0, ay = 0, bx = 0, by = 0, cnt = 0;
  for(int i = 0; i < KB_MAXPOINTS && cnt < 2; i++){
    if(!gKbPoints[i].active) continue;
    if(cnt == 0) vwTouchXY(gKbPoints[i].x, gKbPoints[i].y, ax, ay);
    else         vwTouchXY(gKbPoints[i].x, gKbPoints[i].y, bx, by);
    cnt++;
  }
  if(cnt < 2) return vwPinchOn;
  float dx = (float)(ax - bx), dy = (float)(ay - by);
  float d = sqrtf(dx * dx + dy * dy);
  float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f;
  vwTouchMs = millis();
  vwTapPending = false; vwPanOn = false; vwPressBtn = VWB_NONE; vwPressBar = false;
  if(!vwPinchOn){ vwPinchBegin(cx, cy, d); return true; }
  if(fabsf(d - vwPinD0) > 12.0f || fabsf(cx - vwPinCx0) > 12.0f || fabsf(cy - vwPinCy0) > 12.0f) gTouchPinchUsed = true;
  if(vwPinchApply(cx, cy, d)) vwLive(false);
  return true;
}

// ---- Un dedo: botones, desplazar, deslizar, toque y doble toque ----
static void vwTouch(){
  int tx, ty; vwTouchXY(T.x, T.y, tx, ty);
  if(T.pressed){
    vwDownX = tx; vwDownY = ty; vwPanOn = false;
    int b = vwHitBtn(tx, ty);
    vwPressBar = (b >= 0);
    vwPressBtn = b > 0 ? b : VWB_NONE;
    if(vwPressBtn) vwTouchMs = millis();
    vwPanOx0 = vwOffX; vwPanOy0 = vwOffY; vwPanTx0 = tx; vwPanTy0 = ty;
  }
  if(T.down && !vwPressBtn && !vwPressBar){
    int mdx = tx - vwDownX, mdy = ty - vwDownY;
    if(!vwPanOn && vwScale > 1.001f && (abs(mdx) > 8 || abs(mdy) > 8)){ vwPanOn = true; vwTapPending = false; }
    if(vwPanOn){
      float ox = vwOffX, oy = vwOffY;
      vwOffX = vwPanOx0 + (tx - vwPanTx0);
      vwOffY = vwPanOy0 + (ty - vwPanTy0);
      vwClamp();
      vwTouchMs = millis();
      if(fabsf(vwOffX - ox) > 0.5f || fabsf(vwOffY - oy) > 0.5f) vwLive(false);
      return;
    }
  }
  if(T.released){
    if(vwPanOn){ vwPanOn = false; vwLiveEnd(); return; }
    int dx = tx - vwDownX, dy = ty - vwDownY;
    if(!vwPressBtn && !vwPressBar && vwScale <= 1.001f && abs(dx) > 70 && abs(dx) > 2 * abs(dy)){
      vwGoNeighbour(dx < 0 ? +1 : -1);             // deslizar: anterior / siguiente
      return;
    }
  }
  if(!T.tap) return;
  if(vwPressBtn){
    int b = vwHitBtn(tx, ty);
    int pb = vwPressBtn; vwPressBtn = VWB_NONE;
    if(b == pb) vwDoBtn(b, tx);
    return;
  }
  if(vwPressBar){ vwPressBar = false; vwTouchMs = millis(); return; }   // en la barra, sin boton
  // Toque en la imagen: el primero espera por si llega el segundo.
  uint32_t now = millis();
  if(vwTapPending && now - vwTap1Ms < VW_TAP2_MS && abs(tx - vwTap1X) < 40 && abs(ty - vwTap1Y) < 40){
    vwTapPending = false;
    if(vwKind == VWK_PHOTO || vwKind == VWK_VIDEO){
      if(vwScale > 1.001f){ vwScale = 1.0f; vwClamp(); }
      else vwZoomAt(VW_ZOOM_TAP, (float)tx, (float)ty);
      vwRenderContent(true);
      if(vwBarsA > 0.0f) vwGlassPrep();
      vwPresentAll();
    }
    return;
  }
  vwTapPending = true; vwTap1Ms = now; vwTap1X = tx; vwTap1Y = ty;
}
// El toque simple que no fue doble: muestra u oculta las barras.
static void vwTapTick(){
  if(!vwTapPending || millis() - vwTap1Ms < VW_TAP2_MS) return;
  vwTapPending = false;
  vwBarsShow(vwBarsWant == 0);
}

// ---- Reproduccion: un fotograma como mucho por vuelta, sin delay() ----
static void vwPlayTick(){
  if(!vwPlaying || vwKind != VWK_VIDEO) return;
  unsigned long now = micros();
  if((long)(now - vwNextUs) < 0) return;
  const uint32_t spf = vwAvi.usPerFrame ? vwAvi.usPerFrame : 40000;
  long late = (long)(now - vwNextUs);
  int drop = (int)(late / (long)spf);
  if(drop > VW_MAX_CATCHUP) drop = VW_MAX_CATCHUP;
  for(int i = 0; i < drop; i++){
    int r = flexAviSkipFrame(&vwAvi);
    if(r == FLEXAVI_ERR_EOF){ vwEnded = true; break; }
    if(r < 0) break;
    vwCurFrame++;
  }
  if(!vwEnded && vwReadFrame()){
    int r = vwDecodeFrame();
    (void)r;
    vwGlOk = false;                                 // el fondo de las barras se mueve: tinte sin desenfoque
    vwPresentImage();
    // El progreso de la barra se mueve cada 250 ms, no en cada fotograma.
    if(vwBarsA > 0.0f && millis() - vwBarsDrawMs >= 250u){ vwBarsDrawMs = millis(); vwPresentBars(); }
  }
  vwNextUs += (unsigned long)spf * (unsigned long)(drop + 1);
  if((long)(micros() - vwNextUs) > (long)(spf * 8)) vwNextUs = micros() + spf;
  if(vwEnded){
    vwPlaying = false;
    vwResumeSet(vwPath, 0);                         // terminado: la proxima vez, desde el principio
    vwBarsShow(true);
    vwGlassPrep();
    vwPresentBars();
  }
}

// ---- El elemento abierto cambio por fuera (movil, editor, papelera) ----
static void vwCheckItem(){
  if(!vwId || millis() - vwCheckMs < VW_CHECK_MS) return;
  vwCheckMs = millis();
  uint32_t rev = mlRev();
  if(rev == vwSeenRev) return;
  vwSeenRev = rev;
  FlexMlRec r;
  if(!mlGet(vwId, &r)){
    char nm[FML_NAME_MAX]; snprintf(nm, sizeof(nm), "%s", vwName);
    vwClose();
    sysNotify(nm, "Ya no est\xC3\xA1 en la biblioteca");
    return;
  }
  bool nowLocked = (r.flags & FML_R_LOCKED) != 0;
  if(nowLocked && !vwLocked){ vwClose(); return; }  // se protegio desde otro sitio: fuera los pixeles
  if(strcmp(r.path, vwPath)){
    snprintf(vwPath, sizeof(vwPath), "%s", r.path);
    if(vwHost && vwHost->sess) snprintf(vwHost->sess->path, sizeof(vwHost->sess->path), "%s", r.path);
  }
  if(r.size != vwRecSize || r.crc != vwRecCrc){     // contenido sustituido (editor): se vuelve a abrir
    uint32_t id = vwId;
    const VwHost* h = vwHost;
    vwRelease(false);
    vwHost = h;
    h->sess->open = true; h->sess->id = id;
    snprintf(h->sess->path, sizeof(h->sess->path), "%s", r.path);
    vwActivate(NULL, 0);
  }
}

// ---- Una vuelta del visor ----
static void vwTick(){
  if(!vwOn) return;
  if(vwAnimOn){
    vwAnimStep();
    if(!vwAnimOn){                                  // termino la expansion: ya es el visor
      vwCollect();
      vwApplyOrientation();
      vwResetView();
      vwRenderContent(true);
      vwGlassPrep();
      vwPresentAll();
    }
    return;
  }
  vwEngine();
  if(vwCollect()){                                  // llego la foto (o el motivo de que no)
    bool land = vwLand;
    vwApplyOrientation();
    if(land != vwLand) vwScale = 1.0f;
    vwLayout();
    vwRenderContent(true);
    vwGlassPrep();
    vwPresentAll();
  }
  if(!vwOn) return;
  vwPlayTick();
  if(!vwOn) return;
  vwCheckItem();
  if(!vwOn) return;
  if(vwPinchStep()){ vwBarsTick(); return; }
  vwTouch();
  if(!vwOn) return;
  vwTapTick();
  vwBarsTick();
}

// Con el sistema BLOQUEADO no se queda en memoria nada protegido: ni la foto,
// ni su miniatura, ni el fotograma. Lo llama loop() en cada vuelta (igual que
// Musica olvida una pista protegida). Al desbloquear, la app vuelve a su lista
// y abrirlo otra vez pide la clave.
static void vwLockTick(){
  if(vwOn && vwLocked && gState == ST_LOCK){
    const VwHost* h = vwHost;
    vwRelease(false);
    if(h && h->sess) h->sess->open = false;
    vwHost = NULL;
    gLand = false;
    uiClipFull();
  }
}
