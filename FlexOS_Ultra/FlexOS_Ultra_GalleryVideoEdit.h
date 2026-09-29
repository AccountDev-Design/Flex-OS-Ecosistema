// #############################################################
// ##  FLEX OS ULTRA  ·  GALERIA  ·  EDITOR DE VIDEO
// ##  ----------------------------------------------------------
// ##  Recortar la duracion, dividir y quitar partes, encuadre libre y
// ##  con proporcion, girar, velocidad, volumen del audio ORIGINAL,
// ##  texto, filtros y portada, sobre un video de la biblioteca; y
// ##  "Guardar como copia", "Reemplazar original" o "Guardar las partes".
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo. No lo incluyas por tu cuenta desde otro sitio.
// #############################################################
#pragma once
#include "FlexOS_Ultra_GalleryEdit.h"   // eslabon anterior de la cadena
#include "FlexOS_VidEdit.h"

// #############################################################
// ##  ES DE LA GALERIA, Y SOLO DE LA GALERIA
// ##  ------------------------------------------------------
// ##  Una capa de la app Galeria, igual que el editor de fotos: mientras
// ##  esta abierto, el render, el tick, ATRAS y el ciclo de vida de la
// ##  Galeria pasan primero por el. Se entra por dos sitios y los dos abren
// ##  ESTE mismo editor (vedOpen): la pulsacion larga sobre un video de la
// ##  rejilla (menu > Editar) y el boton Editar de la barra del visor
// ##  cuando lo abrio la Galeria. Multimedia usa el mismo visor, pero su
// ##  anfitrion no ofrece Editar: alli no hay editor.
// ##
// ##  QUIEN HACE CADA COSA
// ##  ------------------------------------------------------
// ##  · LO QUE SALE lo decide FlexOS_VidEdit (portable, probado en el PC):
// ##    tramos, velocidad, recorte, giro, filtro, texto, volumen, portada y
// ##    la exportacion por fotogramas. Editar es cambiar parametros: ni un
// ##    fotograma se procesa hasta exportar.
// ##  · UN TRABAJADOR por sesion ("flexVEd", nucleo 1, prioridad 1, como el
// ##    del editor de fotos) hace todo lo que lee el archivo: analizarlo,
// ##    decodificar el fotograma de la vista previa, las miniaturas de la
// ##    linea de tiempo y exportar. Reglas (check_wiring.py las vigila):
// ##      - el trabajador NO pinta, NO avisa por la isla y NO toca el
// ##        catalogo; deja el resultado y lo publica con __atomic;
// ##      - la vista previa decodificada es de UNO a la vez: el que la tiene
// ##        la escribe, el otro espera (vedS.baseOwner). La interfaz nunca
// ##        pinta desde una base que se esta escribiendo;
// ##      - cancelar y cerrar son banderas que mira entre fotogramas; al
// ##        cerrar se le espera (acotado) antes de soltar memoria;
// ##      - cede la CPU cada ~20 ms (vTaskDelay), como el editor de fotos.
// ##  · LA INTERFAZ solo compone y publica por ZONAS (barra, pozo del
// ##    video, linea de tiempo, panel, herramientas): cada zona se rehace
// ##    entera sobre su fondo y se publica sola. Nada se pinta sobre un
// ##    dibujo anterior del mismo sitio.
// ##
// ##  EXPORTAR SIN PERDER NADA
// ##  ------------------------------------------------------
// ##  El video editado se escribe en un temporal de /System/Media/tmp, se
// ##  COMPRUEBA (flexVeVerify: cabecera, medidas, fotogramas, indice y el
// ##  primer y el ultimo fotograma legibles) y solo entonces se publica en
// ##  loopTask. El original no se toca nunca durante el proceso; "Reemplazar"
// ##  lo aparta junto a si mismo y solo lo borra cuando el nuevo ya esta en
// ##  su sitio (FlexOS_MediaStore). Cancelar, un error o un corte de luz
// ##  dejan el original intacto y ningun temporal publicado.
// ##
// ##  MEMORIA
// ##  ------------------------------------------------------
// ##  Todo se reserva UNA vez al abrir (con comprobacion previa de PSRAM y
// ##  de RAM interna para la pila del trabajador) y se suelta al cerrar; el
// ##  buffer del fotograma comprimido, de su tamano REAL en cuanto el
// ##  analisis lo sabe (nada crece luego fotograma a fotograma). La vista
// ##  previa decodifica a la escala justa (tope VED_PREVIEW_PX) y la linea
// ##  de tiempo son 8 miniaturas pequenas. Exportar pide su memoria al
// ##  empezar y la suelta al acabar (FlexOS_VidEdit, con arena fija).
// #############################################################

#define VED_TASK_STACK     8192
#define VED_TASK_INTERNAL  (8192u + 48u * 1024u)   // pila del trabajador + el suelo de la RAM interna
#define VED_PSRAM_MARGIN   (2u * 1024u * 1024u)    // ademas de la reserva del sistema
#define VED_PREVIEW_PX     (640u * 480u)           // vista previa decodificada: 900 KB en RGB888 como mucho
#define VED_THUMBS         8
#define VED_LIVE_MS        40                      // en vivo: como mucho un repintado cada tanto
#define VED_WAIT_TRIES     600                     // x 5 ms: lo que se espera al trabajador al cerrar
#define VED_IDLE_MS        300                     // sin trabajo tanto tiempo, el trabajador suelta el archivo
#define VED_BAR_H          54
#define VED_TL_H           100
#define VED_PANEL_H        124
#define VED_TABS_H         62
#define VED_STRIP_H        46
#define VED_BAND           16
#define VED_BAND_W         480
#define VED_PARTS_MAX      FLEXVE_SEG_MAX
#define VED_MIN_CROP_PX    24
#define VED_RES_N          4

enum { VED_OFF = 0, VED_OPENING, VED_EDIT, VED_EXPORTING };
enum { VT_CUT = 0, VT_CROP, VT_ROT, VT_SPEED, VT_VOL, VT_TEXT, VT_FILTER, VT_COVER, VT_N };
enum { VJ_NONE = 0, VJ_OPEN, VJ_EXPORT };
enum { VJS_IDLE = 0, VJS_REQ, VJS_BUSY, VJS_DONE };
enum { VD_NONE = 0, VD_TRIM_A, VD_TRIM_B, VD_HEAD, VD_CROP, VD_TEXT };
enum { VS_COPY = 0, VS_REPLACE, VS_PARTS };
enum { VA_NONE = 0, VA_DISCARD, VA_REPLACE, VA_INFO };

static const char* const VED_TOOL[VT_N] = {
  "Cortar", "Encuadre", "Girar", "Velocidad", "Volumen", "Texto", "Filtros", "Portada" };
static const char* const VED_ASPECT[FLEXVE_ASP_N] = { "Libre", "Original", "16:9", "4:3", "1:1", "9:16" };
static const char* const VED_SPEED[FLEXVE_SPEED_N] = {
  "0,25\xC3\x97", "0,5\xC3\x97", "0,75\xC3\x97", "1\xC3\x97", "1,25\xC3\x97", "1,5\xC3\x97", "2\xC3\x97" };
static const uint8_t VED_VOL[5] = { 0, 25, 50, 75, 100 };
static const int VED_RES[VED_RES_N] = { 0, 1080, 720, 480 };   // 0 = original (el del recorte)
static const char* const VED_RES_L[VED_RES_N] = { "Original", "1080p", "720p", "480p" };

// Colores: los del TEMA (Claro/Oscuro). Solo lo que va ENCIMA del video
// (marco del recorte, cabezal, marco del texto) es blanco/negro fijo, como
// en el visor y en el editor de fotos.
#define VED_BG    TH_PAGE
#define VED_CHIP  TH_SURF2
#define VED_TXT   TH_TXT
#define VED_TXT2  TH_TXT2
#define VED_DIS   TH_DIS

// #############################################################
// ##  ESTADO
// #############################################################
// Lo que se comparte con el trabajador y se mira con __atomic: pocas
// variables sueltas en la RAM INTERNA (las operaciones atomicas no se
// hacen sobre la PSRAM). Todo lo grande va en la PSRAM (VedMem).
struct VedSync {
  TaskHandle_t task;
  uint8_t  started;          // hay un trabajador lanzado (de esta sesion)
  uint8_t  quit, exited;     // cerrar / ya salio
  uint8_t  release;          // que suelte el archivo (va a moverse, borrarse o sustituirse)
  uint8_t  hold;             // ...y que no lo vuelva a abrir hasta que el cambio termine (lo quita loopTask)
  uint8_t  srcOpen;          // el trabajador tiene el archivo abierto
  uint8_t  job, jobState;    // trabajo exclusivo: abrir o exportar
  uint8_t  cancel;
  uint8_t  baseOwner;        // vista previa decodificada: 0 = interfaz, 1 = trabajador
  uint8_t  thumbsWant;
  int      thumbsDone;
  int      pct;              // exportar: 0..1000
  uint32_t frameGen;         // sube con cada fotograma decodificado
};
static VedSync vedS;

// Una sesion: todo en PSRAM, reservado al abrir.
struct VedMem {
  // ---- interfaz ----
  char         path[FML_PATH_MAX], name[FML_NAME_MAX];
  FlexVeSource src;
  FlexVeEdit   ed;                     // parametros + historial
  FlexImgEdit  ie;                     // motor de pintar de la vista previa (solo la interfaz)
  // ---- interfaz <-> trabajador (datos; se publican con vedS) ----
  char         wpath[FML_PATH_MAX];    // ruta que abre el trabajador
  FlexVeSource wsrc;                   // resultado de abrir
  int          rc;                     // 0 bien, 1 cancelado, <0 error
  char         why[160];
  uint32_t     wantFrame; int needW, needH;          // peticion de vista previa
  uint32_t     gotFrame; int gotW, gotH, gotDiv, frameRc;
  FlexVeParams ep;                     // exportar: lo que se exporta
  int          eSeg0, eSeg1, eOutW, eOutH, nOut;
  bool         eParts;
  char         tmp[VED_PARTS_MAX][FML_PATH_MAX];
  uint32_t     bytes[VED_PARTS_MAX], frames[VED_PARTS_MAX];
  int          outW, outH;
  bool         audio, audioDropped;
  uint32_t     badFrames, tookMs;
  // ---- solo el trabajador ----
  MediaStream  st;
  bool         stOpen, srcReady;
  FlexAviCtx   avi;
  FlexVeArena  arena;
  uint32_t     yieldMs;
};
static VedMem*   vedM = NULL;

static uint8_t   vedPhase = VED_OFF;
static uint8_t   vedTool = VT_CUT;
static uint32_t  vedId = 0;
static FlexVeEdit* vedE = NULL;                  // = &vedM->ed
static uint32_t  vedHead = 0;                    // fotograma del original en el cabezal
// Vista previa: la base (RGB888, la escribe el trabajador) y la imagen ya
// pintada (RGB565, la de la interfaz).
static uint8_t*  vedBase = NULL;  static uint32_t vedBaseCap = 0;
static int       vedBaseW = 0, vedBaseH = 0, vedBaseDiv = 0;
static uint32_t  vedBaseFrame = 0xFFFFFFFFu, vedBaseGen = 0;
static uint16_t* vedView = NULL;  static size_t vedViewCap = 0;
static int       vedVX = 0, vedVY = 0, vedVW = 0, vedVH = 0;
static bool      vedViewOk = false, vedViewNoText = false;
static bool      vedViewHas = false;             // vedView tiene una imagen buena (aunque sea de antes)
static uint8_t*  vedBand = NULL;  static uint8_t* vedCov = NULL;
static uint16_t* vedThumbs = NULL; static int vedThW = 0, vedThH = 0, vedThSeen = 0;
static uint16_t* vedFt = NULL;    static int vedFtSide = 0; static bool vedFtOk = false;
static uint8_t*  vedFrameBuf = NULL; static uint32_t vedFrameCap = 0;   // trabajador: fotograma comprimido
static uint8_t*  vedArenaMem = NULL; static uint32_t vedArenaCap = 0;   // trabajador: arena de decodificar
static bool      vedReopen = false;              // se solto por memoria: se vuelve a abrir al volver
static bool      vedLeak = false;                // el trabajador no termino: su memoria no se toca
static bool      vedPathPending = false;         // la ruta cambio (renombrado) y el trabajador estaba ocupado
static bool      vedExtCancel = false;           // la exportacion se corto porque el video cambio por fuera
static uint32_t  vedSeenRev = 0;
static uint8_t   vedAsk = VA_NONE;
static bool      vedSheet = false;               // hoja "Exportar"
static bool      vedTextAsk = false;             // el teclado es del editor
static uint8_t   vedSaveMode = VS_COPY;
static int       vedRes = 0;
static int       vedShownPct = -1;
static uint32_t  vedShownMs = 0, vedExpT0 = 0;
// Interaccion en curso
static uint8_t   vedDrag = VD_NONE;
static int       vedCropH = -1;
static float     vedC0x, vedC0y, vedC1x, vedC1y;
static float     vedK0x, vedK0y, vedK1x, vedK1y;
static int       vedDX0 = 0, vedDY0 = 0;
static uint32_t  vedLiveMs = 0;
static bool      vedLivePending = false;
static uint32_t  vedTlMs = 0;
static bool      vedTlPending = false;
// Reproducir la vista previa (sin sonido: el P4 no reproduce el audio de un AVI)
static bool      vedPlaying = false;
static uint32_t  vedPlayMs0 = 0, vedPlayOut0 = 0;

static bool vedActive(){ return vedPhase != VED_OFF; }
static void galRender();

// #############################################################
// ##  EL TRABAJADOR (tarea "flexVEd")
// #############################################################
static inline uint8_t vedLd(uint8_t* p){ return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline void vedSt(uint8_t* p, uint8_t v){ __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static void vedWake(){ if(vedS.task) xTaskNotifyGive(vedS.task); }

// Cede la CPU cada ~20 ms y dice si hay que parar (cancelar o cerrar).
static bool vedWorkTick(void*){
  uint32_t now = millis();
  if(vedM && now - vedM->yieldMs >= 20u){ vTaskDelay(1); vedM->yieldMs = millis(); }
  return !vedLd(&vedS.cancel) && !vedLd(&vedS.quit);
}

static void vedSrcClose(){
  if(!vedM) return;
  mediaStreamClose(&vedM->st);
  vedM->stOpen = false;
  vedSt(&vedS.srcOpen, 0);
}
static bool vedSrcOpen(){
  if(vedM->stOpen) return true;
  if(!mediaStreamOpen(&vedM->st, vedM->wpath)) return false;
  // Si el archivo ya no mide lo mismo, lo que se sabia de el no vale.
  if(vedM->srcReady && vedM->st.size != vedM->wsrc.fileBytes){ mediaStreamClose(&vedM->st); return false; }
  vedM->stOpen = true;
  vedSt(&vedS.srcOpen, 1);
  return true;
}
static bool vedReadAt(uint32_t off, uint8_t* buf, uint32_t n){
  FlexMediaIO io; mediaBindIO(&io, &vedM->st);
  if(!io.seek(io.ctx, off)) return false;
  uint32_t got = 0;
  while(got < n){
    int r = io.read(io.ctx, buf + got, n - got);
    if(r <= 0) return false;
    got += (uint32_t)r;
  }
  return true;
}
// Datos de la IMAGEN que se ve en el fotograma `f`: la del ultimo fotograma
// con datos hasta el (un trozo vacio repite el anterior). Se busca por el
// indice y se avanza leyendo solo cabeceras.
static bool vedLocate(uint32_t f, uint32_t* off, uint32_t* len){
  FlexAviCtx* a = &vedM->avi;
  uint32_t start = f > 16u ? f - 16u : 0u;
  int landed = -1, prev = -2;
  for(int k = 0; k < 64; k++){
    landed = flexAviSeekFrameMax(a, start, 4096u);
    if(landed < 0 || (uint32_t)landed >= start || landed == prev) break;
    prev = landed;
    if(!vedWorkTick(NULL)) return false;
  }
  if(landed < 0) return false;
  uint32_t lo = 0, ll = 0;
  for(uint32_t guard = 0; a->frameNo <= f && guard < 200000u; guard++){
    uint32_t o = 0, n = 0;
    int r = flexAviNextFrameInfo(a, &o, &n);
    if(r == FLEXAVI_ERR_EOF) break;
    if(r != FLEXAVI_OK) return false;
    if(n){ lo = o; ll = n; }
  }
  for(int k = 0; k < 256 && !ll; k++){                    // el video empieza con trozos vacios
    uint32_t o = 0, n = 0;
    if(flexAviNextFrameInfo(a, &o, &n) != FLEXAVI_OK) break;
    if(n){ lo = o; ll = n; }
  }
  *off = lo; *len = ll;
  return ll > 0;
}
// El buffer del fotograma comprimido y la arena de decodificar se dejan de su
// tamano REAL en cuanto el analisis lo sabe (vedDoOpen): una reserva, y nada
// crece luego fotograma a fotograma. Solo el trabajador los usa.
static bool vedGrowFrame(uint32_t len){
  if(len <= vedFrameCap && vedFrameBuf) return true;
  if(len > FLEXAVI_FRAME_MAX) return false;
  uint32_t cap = (len + 4095u) & ~4095u;
  if(cap > FLEXAVI_FRAME_MAX) cap = FLEXAVI_FRAME_MAX;
  uint8_t* nb = (uint8_t*)mediaAlloc(cap);
  if(!nb) return false;
  mediaFree(vedFrameBuf);
  vedFrameBuf = nb; vedFrameCap = cap;
  return true;
}
static bool vedGrowArena(uint32_t need){
  if(need <= vedArenaCap && vedArenaMem) return true;
  uint8_t* nb = (uint8_t*)mediaAlloc(need);
  if(!nb) return false;
  mediaFree(vedArenaMem);
  vedArenaMem = nb; vedArenaCap = need;
  flexVeArenaInit(&vedM->arena, vedArenaMem, vedArenaCap);
  return true;
}
// Lee los datos de un fotograma al buffer del trabajador.
static bool vedReadFrame(uint32_t off, uint32_t len){
  return vedGrowFrame(len) && vedReadAt(off, vedFrameBuf, len);
}
// Divisor de la vista previa: el mayor que aun cubre lo que se va a ensenar,
// sin pasar de la base reservada.
static int vedPreviewDiv(int W, int H, int needW, int needH, uint32_t cap){
  int d = 1;
  for(int dd = 8; dd >= 2; dd >>= 1)
    if((W + dd - 1) / dd >= needW && (H + dd - 1) / dd >= needH){ d = dd; break; }
  while(d < 8 && (uint64_t)((W + d - 1) / d) * (uint64_t)((H + d - 1) / d) * 3u > cap) d <<= 1;
  if((uint64_t)((W + d - 1) / d) * (uint64_t)((H + d - 1) / d) * 3u > cap) return 0;
  return d;
}

// ---- Vista previa: el fotograma pedido, decodificado a la escala justa ----
static void vedDoFrame(){
  VedMem* m = vedM;
  const FlexVeSource* s = &m->wsrc;
  int rc = FLEXVE_ERR_IO;
  uint32_t f = m->wantFrame;
  if(f >= s->frames) f = s->frames ? s->frames - 1 : 0;
  int d = vedPreviewDiv(s->w, s->h, m->needW, m->needH, vedBaseCap);
  uint32_t off = 0, len = 0;
  m->gotFrame = f; m->gotDiv = d;                      // lo intentado (tambien si falla: no se reintenta sin fin)
  if(!d) rc = FLEXVE_ERR_MEMORY;
  else if(vedSrcOpen() && vedLocate(f, &off, &len) && vedReadFrame(off, len)){
    int bw = (s->w + d - 1) / d, bh = (s->h + d - 1) / d;
    rc = flexVeDecode(vedFrameBuf, len, s->w, s->h, d, 0, 0, bw, bh, vedBase, &m->arena);
    if(rc == FLEXVE_OK){ m->gotW = bw; m->gotH = bh; }
  }
  m->frameRc = rc;
  __atomic_add_fetch(&vedS.frameGen, 1u, __ATOMIC_RELEASE);
}

// ---- Miniaturas de la linea de tiempo ----
static void vedDoThumb(int i){
  VedMem* m = vedM;
  const FlexVeSource* s = &m->wsrc;
  uint16_t* dst = vedThumbs + (size_t)i * vedThW * vedThH;
  uint32_t f = (uint32_t)(((uint64_t)(2 * i + 1) * s->frames) / (2u * VED_THUMBS));
  uint32_t off = 0, len = 0;
  bool ok = vedSrcOpen() && vedLocate(f, &off, &len) && vedReadFrame(off, len) &&
            flexVeThumb(vedFrameBuf, len, s->w, s->h, vedThW, vedThH, dst, &m->arena) == FLEXVE_OK;
  if(!ok) for(int k = 0; k < vedThW * vedThH; k++) dst[k] = rgb565(60, 64, 76);   // sin imagen: se dice con un gris
}

// ---- Abrir: analizar el archivo ----
static void vedDoOpen(){
  VedMem* m = vedM;
  m->rc = 0; m->why[0] = 0; m->srcReady = false;
  if(!vedSrcOpen()){ m->rc = -1; snprintf(m->why, sizeof(m->why), "No se pudo leer el v\xC3\xAD" "deo"); return; }
  FlexMediaIO io; mediaBindIO(&io, &m->st);
  int rc = flexVeProbe(&io, &m->wsrc, &m->avi, vedWorkTick, NULL);
  if(rc != FLEXVE_OK){
    m->rc = rc == FLEXVE_ERR_CANCEL ? 1 : -1;
    const char* w = rc == FLEXVE_ERR_CODEC ? "Este AVI no es MJPEG: el P4 no lo puede editar"
                  : rc == FLEXVE_ERR_FRAME ? "Tiene fotogramas de m\xC3\xA1s de 1 MB: el P4 no los puede editar"
                  : rc == FLEXVE_ERR_IO    ? "No se pudo leer el v\xC3\xAD" "deo"
                  : "El v\xC3\xAD" "deo est\xC3\xA1 da\xC3\xB1" "ado o incompleto";
    snprintf(m->why, sizeof(m->why), "%s", w);
    return;
  }
  if(!vedGrowFrame(m->wsrc.maxFrame) || !vedGrowArena(flexVeDecodeWork(m->wsrc.w))){
    m->rc = -1; snprintf(m->why, sizeof(m->why), "No hay memoria libre para los fotogramas de este v\xC3\xAD" "deo");
    return;
  }
  m->srcReady = true;
  vedDoFrame();                                        // el primer fotograma, para ensenar algo ya
}

// ---- Exportar ----
static bool vedOutWrite(void* c, const void* d, size_t n){ return flexFsStreamWrite((FlexFsStream*)c, d, n); }
static bool vedOutSeek(void* c, uint32_t off){ return flexFsStreamSeek((FlexFsStream*)c, off); }
static void vedFailWhy(const char* why){ vedM->rc = -1; snprintf(vedM->why, sizeof(vedM->why), "%s", why); }
static void vedDoExport(){
  VedMem* m = vedM;
  m->rc = 0; m->why[0] = 0; m->badFrames = 0; m->audio = false; m->audioDropped = false;
  __atomic_store_n(&vedS.pct, 0, __ATOMIC_RELEASE);
  uint32_t t0 = millis();
  if(!vedSrcOpen()){ vedFailWhy("No se pudo leer el v\xC3\xAD" "deo original"); return; }
  FlexMediaIO io; mediaBindIO(&io, &m->st);
  FlexVeExport* x = (FlexVeExport*)mediaAlloc(sizeof(FlexVeExport));
  FlexAviCtx* vc = (FlexAviCtx*)mediaAlloc(sizeof(FlexAviCtx));
  FlexImgEdit* ie = NULL;
  if(!flexVeCopyMode(&m->wsrc, &m->ep, m->eOutW, m->eOutH)){
    ie = (FlexImgEdit*)mediaAlloc(sizeof(FlexImgEdit));
    if(ie){ flexIeInit(ie, NULL, 0, 0); flexIeSetTextFn(ie, gedTextFn, NULL); }
  }
  if(!x || !vc || (!ie && !flexVeCopyMode(&m->wsrc, &m->ep, m->eOutW, m->eOutH))){
    mediaFree(x); mediaFree(vc); mediaFree(ie);
    vedFailWhy("No hay memoria para exportar a esta resoluci\xC3\xB3n");
    return;
  }
  int done = 0;
  for(int k = 0; k < m->nOut && m->rc == 0; k++){
    int s0 = m->eParts ? m->eSeg0 + k : m->eSeg0, s1 = m->eParts ? m->eSeg0 + k : m->eSeg1;
    const char* tmp = m->tmp[k];
    flexFsDelete(tmp);
    FlexFsStream* f = flexFsOpenWrite(tmp);
    if(!f){ vedFailWhy("No se pudo crear el archivo temporal"); break; }
    FlexVeExportCfg c; memset(&c, 0, sizeof(c));
    c.io = io; c.src = &m->wsrc; c.params = &m->ep; c.seg0 = s0; c.seg1 = s1;
    c.write = vedOutWrite; c.seek = vedOutSeek; c.outCtx = f;
    c.outW = m->eOutW; c.outH = m->eOutH; c.quality = FLEXVE_QUALITY; c.maxBytes = FML_LIMIT_VIDEO;
    c.ie = ie; c.alloc = mediaAlloc; c.free = mediaFree; c.tick = vedWorkTick; c.tickCtx = NULL;
    int rc = flexVeExportBegin(x, &c);
    while(rc == FLEXVE_OK){
      int st = flexVeExportStep(x);
      if(st <= 0){ rc = st; break; }
      __atomic_store_n(&vedS.pct, (k * 1000 + flexVeExportProgress(x)) / m->nOut, __ATOMIC_RELEASE);
    }
    if(rc == 0) rc = flexVeExportFinish(x);
    m->frames[k] = x->outTotal; m->bytes[k] = x->pos;
    m->outW = x->outW; m->outH = x->outH;
    m->audio = x->audioOn; m->audioDropped = x->audioDropped; m->badFrames += x->badFrames;
    flexVeExportFree(x);                                 // la memoria de la exportacion, fuera ya
    flexFsStreamClose(f);
    if(rc != FLEXVE_OK){
      flexFsDelete(tmp);
      if(rc == FLEXVE_ERR_CANCEL){ m->rc = 1; break; }
      vedFailWhy(flexVeErrStr(rc));
      break;
    }
    // Lo escrito se vuelve a leer ANTES de publicarlo.
    MediaStream vs; memset(&vs, 0, sizeof(vs));
    int vr = FLEXVE_ERR_VERIFY;
    if(mediaStreamOpen(&vs, tmp)){
      FlexMediaIO vio; mediaBindIO(&vio, &vs);
      vr = flexVeVerify(&vio, m->bytes[k], m->frames[k], m->outW, m->outH, m->audio, vc, mediaAlloc, mediaFree);
      mediaStreamClose(&vs);
    }
    if(vr != FLEXVE_OK){ flexFsDelete(tmp); vedFailWhy("El v\xC3\xAD" "deo exportado no pasa la comprobaci\xC3\xB3n: no se ha publicado"); break; }
    done = k + 1;
  }
  if(m->rc != 0) for(int k = 0; k < done; k++) flexFsDelete(m->tmp[k]);   // todo o nada
  mediaFree(x); mediaFree(vc); mediaFree(ie);
  m->tookMs = millis() - t0;
  if(m->rc == 0) __atomic_store_n(&vedS.pct, 1000, __ATOMIC_RELEASE);
}

// Una vuelta de trabajo. true = habia algo y se hizo. La tarea la llama en
// bucle; las pruebas de host, directamente (alli no hay hilos).
static bool vedWorkStep(){
  if(!vedM) return false;
  if(vedLd(&vedS.release)){ vedSrcClose(); vedSt(&vedS.release, 0); }
  if(vedLd(&vedS.hold)) return false;                     // el archivo se esta moviendo o borrando: se espera
  uint8_t st = VJS_REQ;
  if(__atomic_compare_exchange_n(&vedS.jobState, &st, (uint8_t)VJS_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)){
    uint8_t job = vedLd(&vedS.job);
    if(job == VJ_OPEN){ vedDoOpen(); vedSt(&vedS.baseOwner, 0); }
    else if(job == VJ_EXPORT) vedDoExport();
    vedSt(&vedS.jobState, VJS_DONE);
    return true;
  }
  if(!vedM->srcReady) return false;
  if(vedLd(&vedS.baseOwner) == 1){ vedDoFrame(); vedSt(&vedS.baseOwner, 0); return true; }
  int td = __atomic_load_n(&vedS.thumbsDone, __ATOMIC_ACQUIRE);
  if(vedLd(&vedS.thumbsWant) && td < VED_THUMBS){
    vedDoThumb(td);
    __atomic_store_n(&vedS.thumbsDone, td + 1, __ATOMIC_RELEASE);
    return true;
  }
  return false;
}
static void vedWorker(void*){
  uint32_t idleSince = millis();
  for(;;){
    if(vedLd(&vedS.quit)) break;
    if(vedWorkStep()){ idleSince = millis(); continue; }
    // Sin trabajo un rato: el archivo se suelta (nadie lo tiene abierto si
    // otra app lo mueve o lo borra).
    if(vedM && vedM->stOpen && millis() - idleSince >= VED_IDLE_MS) vedSrcClose();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VED_IDLE_MS));
  }
  vedSrcClose();
  vedSt(&vedS.exited, 1);
  vTaskDelete(NULL);
}

// Lanza un trabajo exclusivo ya preparado en vedM.
static void vedJobStart(uint8_t job){
  vedSt(&vedS.cancel, 0);
  __atomic_store_n(&vedS.pct, 0, __ATOMIC_RELEASE);
  vedSt(&vedS.job, job);
  vedSt(&vedS.jobState, VJS_REQ);
  vedShownPct = -1;
  vedWake();
}
static bool vedJobDone(){ return vedLd(&vedS.jobState) == VJS_DONE; }
static bool vedJobRunning(){ uint8_t s = vedLd(&vedS.jobState); return s == VJS_REQ || s == VJS_BUSY; }

static bool vedWorkerStart(){
  memset(&vedS, 0, sizeof(vedS));
  vedS.started = 1;
  if(xTaskCreatePinnedToCore(vedWorker, "flexVEd", VED_TASK_STACK, NULL, 1, &vedS.task, 1) != pdPASS){
    vedS.started = 0; vedS.task = NULL;
    return false;
  }
  return true;
}
// Pide al trabajador que salga y lo ESPERA (acotado). false = no termino: lo
// que usa no se puede soltar (se recoge despues, en vedReclaim).
static bool vedWorkerStopWait(){
  if(!vedS.started) return true;
  vedSt(&vedS.cancel, 1);
  vedSt(&vedS.quit, 1);
  vedWake();
  if(!vedS.task){                                        // sin hilo (arnes de pruebas): no hay a quien esperar
    vedSrcClose();
    vedSt(&vedS.exited, 1);
  }
  for(int i = 0; i < VED_WAIT_TRIES && !vedLd(&vedS.exited); i++) vTaskDelay(pdMS_TO_TICKS(5));
  if(!vedLd(&vedS.exited)){
    Serial.println(F("[video] el trabajador no termino a tiempo: su memoria se queda reservada"));
    vedLeak = true;
    return false;
  }
  // Una exportacion que acabo bien y nadie recogio deja temporales: fuera.
  if(vedM && vedLd(&vedS.job) == VJ_EXPORT && vedJobDone() && vedM->rc == 0)
    for(int k = 0; k < vedM->nOut; k++) flexFsDelete(vedM->tmp[k]);
  vedS.started = 0; vedS.task = NULL;
  return true;
}
// Que el trabajador suelte el archivo AHORA (otra parte del sistema lo va a
// mover, borrar o sustituir). Cancela una exportacion en curso. Acotado.
static void vedReleaseSource(bool cancelExport){
  if(!vedS.started || !vedM) return;
  if(cancelExport && vedJobRunning() && vedLd(&vedS.job) == VJ_EXPORT) vedSt(&vedS.cancel, 1);
  // Soltarlo no basta: entre que lo suelta y que el cambio termina, una
  // miniatura pendiente lo volveria a abrir. Se retiene hasta la proxima
  // vuelta de loopTask (vedHoldEnd), cuando el cambio ya se hizo.
  if(cancelExport) vedSt(&vedS.hold, 1);
  if(!vedS.task){ vedSrcClose(); return; }               // arnes: sin hilo
  vedSt(&vedS.release, 1);
  vedWake();
  for(int i = 0; i < 400 && (vedLd(&vedS.srcOpen) || vedLd(&vedS.release)); i++) vTaskDelay(pdMS_TO_TICKS(5));
}

// #############################################################
// ##  GEOMETRIA (una sola fuente para dibujo y tacto)
// ##  ------------------------------------------------------
// ##  A pantalla completa (480x800) el editor usa la franja de la cabecera
// ##  del sistema para su barra ("< Editar video   deshacer rehacer  ✓") y
// ##  la caja de la app para: el video, la linea de tiempo, el panel de la
// ##  herramienta y las ocho herramientas. En una ventana de DeX (sin
// ##  cabecera del sistema) la barra va dentro de la caja.
// #############################################################
struct VedGeom {
  int bx, by, bw, bh, pad;
  int barY, barH;
  int wellX, wellY, wellW, wellH;
  int tlX, tlY, tlW, tlH;
  int stripX, stripY, stripW;
  int panelX, panelY, panelW;
  int tabsY;
};
static VedGeom vedGeom(){
  VedGeom g;
  uiBox(g.bx, g.by, g.bw, g.bh);
  g.pad = uiPad();
  int top;
  if(!vwHostedNow() && g.by >= 90){ g.barY = 42; g.barH = g.by - 42; top = g.by; }
  else { g.barY = g.by; g.barH = VED_BAR_H - 4; top = g.by + g.barH; }
  g.tabsY = g.by + g.bh - VED_TABS_H;
  g.panelX = g.bx + g.pad / 2; g.panelW = g.bw - g.pad;
  g.panelY = g.tabsY - VED_PANEL_H;
  g.tlX = g.panelX; g.tlW = g.panelW; g.tlH = VED_TL_H;
  g.tlY = g.panelY - g.tlH;
  g.wellX = g.bx; g.wellW = g.bw; g.wellY = top;
  g.wellH = g.tlY - 4 - top;
  if(g.wellH < 80) g.wellH = 80;
  g.stripX = g.tlX + 14; g.stripW = g.tlW - 28;
  if(g.stripW < 16) g.stripW = 16;                       // nunca 0: se divide por el al tocar la tira
  g.stripY = g.tlY + 44;
  return g;
}
static bool vedCropView(){ return vedTool == VT_CROP && vedPhase == VED_EDIT; }
// Lo que se ve en el pozo: el fotograma entero girado (Encuadre) o la salida.
static void vedShownDims(float* aw, float* ah){
  int sw = vedM && vedM->src.w ? vedM->src.w : 4, sh = vedM && vedM->src.h ? vedM->src.h : 3;
  if(!vedE){ *aw = (float)sw; *ah = (float)sh; return; }
  if(vedCropView()){ bool r = vedE->p.rot & 1; *aw = (float)(r ? sh : sw); *ah = (float)(r ? sw : sh); return; }
  int rw, rh; flexVeRegionSize(&vedE->p, sw, sh, &rw, &rh);
  *aw = (float)rw; *ah = (float)rh;
}
static void vedImgRect(const VedGeom& g, int& x, int& y, int& w, int& h){
  float aw, ah; vedShownDims(&aw, &ah);
  int mw = g.wellW - 2 * g.pad, mh = g.wellH - 12;
  if(mw > VED_BAND_W) mw = VED_BAND_W;
  if(mw < 16) mw = 16;
  if(mh < 16) mh = 16;
  float k = mw / aw;
  if(ah * k > mh) k = mh / ah;
  w = (int)(aw * k + 0.5f); h = (int)(ah * k + 0.5f);
  if(w < 1) w = 1;
  if(h < 1) h = 1;
  x = g.wellX + (g.wellW - w) / 2; y = g.wellY + (g.wellH - h) / 2;
}
// Pixeles del ORIGINAL (sin girar) que hacen falta para la vista de ahora:
// con un recorte pequeno se ve ampliado y se pide mas detalle.
static void vedNeedDims(int* nw, int* nh){
  VedGeom g = vedGeom();
  int x, y, w, h; vedImgRect(g, x, y, w, h);
  float fw = 1.0f, fh = 1.0f;
  if(!vedCropView() && vedE){
    fw = vedE->p.c1x - vedE->p.c0x; fh = vedE->p.c1y - vedE->p.c0y;
    if(fw < 0.05f) fw = 0.05f;
    if(fh < 0.05f) fh = 0.05f;
  }
  int ow = (int)(w / fw + 0.5f), oh = (int)(h / fh + 0.5f);          // ya girado
  bool r = vedE && (vedE->p.rot & 1);
  *nw = r ? oh : ow; *nh = r ? ow : oh;
}

// ---- Tiempo <-> linea de tiempo ----
static uint32_t vedFrames(){ return vedM && vedM->src.frames ? vedM->src.frames : 1u; }
static int vedFrameX(const VedGeom& g, uint32_t f){ return g.stripX + (int)((uint64_t)f * g.stripW / vedFrames()); }
static uint32_t vedXFrame(const VedGeom& g, int x){
  if(x <= g.stripX) return 0;
  if(x >= g.stripX + g.stripW) return vedFrames() - 1;
  uint32_t f = (uint32_t)((uint64_t)(x - g.stripX) * vedFrames() / g.stripW);
  return f >= vedFrames() ? vedFrames() - 1 : f;
}
static uint32_t vedMsOf(uint32_t frames){
  uint32_t us = vedM && vedM->src.usPerFrame ? vedM->src.usPerFrame : 40000u;
  return (uint32_t)((uint64_t)frames * us / 1000u);
}
static void vedFmt(uint32_t ms, char* out, size_t n){ mlFmtDur(ms, out, n); }
static uint32_t vedFinalMs(){
  if(!vedE || !vedM) return 0;
  return (uint32_t)(flexVeOutDurationUs(&vedE->p, vedM->src.usPerFrame, 0, vedE->p.nSeg - 1) / 1000u);
}

// #############################################################
// ##  VISTA PREVIA
// ##  Se CALCULA una vez por cambio (fotograma nuevo o parametro) con el
// ##  motor del editor de fotos y se guarda en RGB565: arrastrar el recorte
// ##  o el texto solo recompone encima, sin volver a calcular el fotograma.
// #############################################################
static void vedViewBuild(){
  vedViewOk = false;
  if(!vedM || !vedE || !vedBase || !vedBaseW || !vedView || !vedBand || !vedCov) return;
  if(vedLd(&vedS.baseOwner) != 0) return;               // la base se esta escribiendo: se sigue viendo la de antes
  VedGeom g = vedGeom();
  int x, y, w, h; vedImgRect(g, x, y, w, h);
  if((size_t)w * h * 2 > vedViewCap){
    // La caja crecio (una ventana de DeX mas grande): la vista se rehace a su
    // medida una vez; si no hay memoria, se sigue viendo la de antes.
    uint16_t* nv = (uint16_t*)mediaAlloc((size_t)w * h * 2);
    if(!nv) return;
    mediaFree(vedView); vedView = nv; vedViewCap = (size_t)w * h * 2; vedViewHas = false;
  }
  FlexVeParams p = vedE->p;
  if(vedViewNoText) p.text[0] = 0;                        // el texto se esta arrastrando: va encima, en vivo
  flexVeSetupRender(&vedM->ie, vedBase, vedBaseW, vedBaseH, &p, NULL, 0, vedCropView());
  for(int y0 = 0; y0 < h; y0 += VED_BAND){
    int rows = h - y0 < VED_BAND ? h - y0 : VED_BAND;
    flexIeRenderRows(&vedM->ie, w, h, y0, rows, vedBand, vedCov);
    for(int r = 0; r < rows; r++){
      const uint8_t* s = vedBand + (size_t)r * w * 3;
      uint16_t* d = vedView + (size_t)(y0 + r) * w;
      for(int i = 0; i < w; i++, s += 3) d[i] = rgb565(s[0], s[1], s[2]);
    }
  }
  vedVX = x; vedVY = y; vedVW = w; vedVH = h;
  vedViewOk = true; vedViewHas = true;
}
static void vedChanged(){ vedViewOk = false; vedFtOk = false; }

// Miniaturas de los filtros, desde la base y con el encuadre de la salida.
static void vedFilterThumbs(int side){
  if(vedFtOk && vedFtSide == side) return;
  if(!vedFt || vedFtSide != side){
    mediaFree(vedFt);
    vedFt = (uint16_t*)mediaAlloc((size_t)FLEXIE_FILTER_N * side * side * 2);
    vedFtSide = side;
  }
  if(!vedFt || !vedBase || !vedBaseW || !vedM || vedLd(&vedS.baseOwner) != 0) return;
  FlexVeParams p = vedE->p;
  p.text[0] = 0;
  // Cuadrada y centrada dentro del recorte.
  float cw = p.c1x - p.c0x, ch = p.c1y - p.c0y;
  float aw, ah; vedShownDims(&aw, &ah);
  if(!vedCropView()){
    if(aw > ah){ float k = ah / aw * cw; p.c0x = p.c0x + (cw - k) / 2; p.c1x = p.c0x + k; }
    else if(ah > aw){ float k = aw / ah * ch; p.c0y = p.c0y + (ch - k) / 2; p.c1y = p.c0y + k; }
  }
  for(int f = 0; f < FLEXIE_FILTER_N; f++){
    p.filter = (uint8_t)f;
    flexVeSetupRender(&vedM->ie, vedBase, vedBaseW, vedBaseH, &p, NULL, 0, false);
    uint16_t* dst = vedFt + (size_t)f * side * side;
    for(int y0 = 0; y0 < side; y0 += VED_BAND){
      int rows = side - y0 < VED_BAND ? side - y0 : VED_BAND;
      flexIeRenderRows(&vedM->ie, side, side, y0, rows, vedBand, NULL);
      for(int r = 0; r < rows; r++){
        const uint8_t* s = vedBand + (size_t)r * side * 3;
        for(int i = 0; i < side; i++, s += 3) dst[(size_t)(y0 + r) * side + i] = rgb565(s[0], s[1], s[2]);
      }
    }
  }
  vedFtOk = true;
}

// #############################################################
// ##  DIBUJO POR ZONAS
// #############################################################
// ---- Iconos (vectoriales, como el resto del sistema) ----
static void vedIcoTool(int t, int cx, int cy, uint16_t col){
  switch(t){
    case VT_CUT:                                              // tijeras
      drawCircle(cx - 6, cy + 6, 4, col); drawCircle(cx + 6, cy + 6, 4, col);
      strokeSegAA(cx - 4, cy + 3, cx + 7, cy - 10, 1.4f, col);
      strokeSegAA(cx + 4, cy + 3, cx - 7, cy - 10, 1.4f, col);
      break;
    case VT_CROP: gedIcoTool(GT_CROP, cx, cy, col); break;
    case VT_ROT:  gedIcoRotate(cx, cy, col, true); break;
    case VT_SPEED:                                            // rayo
      strokeSegAA(cx + 4, cy - 11, cx - 4, cy + 1, 1.6f, col);
      strokeSegAA(cx - 4, cy + 1, cx + 4, cy - 1, 1.6f, col);
      strokeSegAA(cx + 4, cy - 1, cx - 4, cy + 11, 1.6f, col);
      break;
    case VT_VOL:                                              // altavoz
      fillRect(cx - 10, cy - 4, 5, 8, col);
      fillTriangle(cx - 5, cy - 4, cx + 2, cy - 10, cx + 2, cy + 10, col);
      fillTriangle(cx - 5, cy - 4, cx + 2, cy + 10, cx - 5, cy + 4, col);
      strokeSegAA(cx + 6, cy - 5, cx + 8, cy, 1.2f, col); strokeSegAA(cx + 8, cy, cx + 6, cy + 5, 1.2f, col);
      break;
    case VT_TEXT: gedIcoTool(GT_TEXT, cx, cy, col); break;
    case VT_FILTER: gedIcoTool(GT_FILTER, cx, cy, col); break;
    default:                                                  // portada: marco con un monte
      drawRoundRect(cx - 11, cy - 9, 22, 18, 3, col);
      fillTriangle(cx - 8, cy + 6, cx - 1, cy - 2, cx + 6, cy + 6, col);
      fillCircle(cx + 5, cy - 4, 2, col);
      break;
  }
}
static void vedIcoCheck(int cx, int cy, uint16_t col){
  strokeSegAA(cx - 8, cy, cx - 2, cy + 6, 2.4f, col);
  strokeSegAA(cx - 2, cy + 6, cx + 9, cy - 6, 2.4f, col);
}

// ---- Barra de arriba: volver, titulo, deshacer, rehacer y exportar ----
static void vedBarBtns(const VedGeom& g, int& undoX, int& redoX, int& okX, int& cy){
  cy = g.barY + g.barH / 2 + (vwHostedNow() ? 0 : 2);
  okX = g.bx + g.bw - g.pad - 22;
  redoX = okX - 58; undoX = redoX - 46;
}
static bool vedCanExport(){ return vedPhase == VED_EDIT && vedE && !flexVeIsIdentity(vedE); }
static void vedDrawBar(const VedGeom& g){
  fillRect(g.bx, g.barY, g.bw, g.barH, vwHostedNow() ? VED_BG : WIN_BG);
  int ux, rx, okx, cy; vedBarBtns(g, ux, rx, okx, cy);
  // Volver: el mismo chevron que la cabecera del sistema (y en el mismo sitio).
  int chx = vwHostedNow() ? g.bx + 12 : 18, chy = vwHostedNow() ? cy - 8 : 50;
  strokeSegAA(chx + 12, chy + 16, chx, chy + 8, 2.4f, TH_TXT);
  strokeSegAA(chx, chy + 8, chx + 12, chy, 2.4f, TH_TXT);
  int fs = uiFontFit("Editar v\xC3\xAD" "deo", ux - 30 - (chx + 28), 3);
  drawText(chx + 28, cy - uiLineH(fs) / 2 - 1, "Editar v\xC3\xAD" "deo", fs, TH_TXT);
  bool edit = vedPhase == VED_EDIT && vedE;
  bool cu = edit && flexVeCanUndo(vedE), cr = edit && flexVeCanRedo(vedE);
  fillCircleAA((float)ux, (float)cy, 18.0f, VED_CHIP); gedIcoUndo(ux, cy, cu ? VED_TXT : VED_DIS, false);
  fillCircleAA((float)rx, (float)cy, 18.0f, VED_CHIP); gedIcoUndo(rx, cy, cr ? VED_TXT : VED_DIS, true);
  bool can = vedCanExport();
  fillCircleAA((float)okx, (float)cy, 21.0f, can ? TH_PRIM : VED_CHIP);
  vedIcoCheck(okx, cy, can ? TH_ONACC : VED_DIS);
}

// ---- El video ----
static void vedDrawCropUi(){
  float a0 = vedDrag == VD_CROP ? vedC0x : vedE->p.c0x, b0 = vedDrag == VD_CROP ? vedC0y : vedE->p.c0y;
  float a1 = vedDrag == VD_CROP ? vedC1x : vedE->p.c1x, b1 = vedDrag == VD_CROP ? vedC1y : vedE->p.c1y;
  int x0 = vedVX + (int)(a0 * vedVW + 0.5f), x1 = vedVX + (int)(a1 * vedVW + 0.5f);
  int y0 = vedVY + (int)(b0 * vedVH + 0.5f), y1 = vedVY + (int)(b1 * vedVH + 0.5f);
  const uint16_t dark = rgb565(0, 0, 0), W = rgb565(255, 255, 255);
  fillRectA(vedVX, vedVY, vedVW, y0 - vedVY, dark, 150);
  fillRectA(vedVX, y1, vedVW, vedVY + vedVH - y1, dark, 150);
  fillRectA(vedVX, y0, x0 - vedVX, y1 - y0, dark, 150);
  fillRectA(x1, y0, vedVX + vedVW - x1, y1 - y0, dark, 150);
  for(int k = 1; k < 3; k++){                              // tercios
    hLineA(x0, y0 + (y1 - y0) * k / 3, x1 - x0, W, 110);
    for(int y = y0; y < y1; y++) pxA(x0 + (x1 - x0) * k / 3, y, W, 110);
  }
  drawRect(x0, y0, x1 - x0, y1 - y0, W);
  drawRect(x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2, rgb565(200, 204, 214));
  const int L = 20, T = 4;
  fillRect(x0 - 2, y0 - 2, L, T, W); fillRect(x0 - 2, y0 - 2, T, L, W);
  fillRect(x1 - L + 2, y0 - 2, L, T, W); fillRect(x1 - T + 2, y0 - 2, T, L, W);
  fillRect(x0 - 2, y1 - T + 2, L, T, W); fillRect(x0 - 2, y1 - L + 2, T, L, W);
  fillRect(x1 - L + 2, y1 - T + 2, L, T, W); fillRect(x1 - T + 2, y1 - L + 2, T, L, W);
}
// El texto mientras se arrastra, con la MISMA cobertura que saldra al exportar.
static void vedDrawTextLive(){
  if(!vedE->p.text[0]) return;
  int L = vedVW > vedVH ? vedVW : vedVH;
  int hpx = (int)(FLEXVE_TEXT_H[vedE->p.textSize < 3 ? vedE->p.textSize : 1] * L + 0.5f);
  if(hpx < 4) hpx = 4;
  int x = vedVX + (int)(vedE->p.textU * vedVW), y = vedVY + (int)(vedE->p.textV * vedVH);
  int tw = gedTextFn(NULL, vedE->p.text, hpx, -1, 0, NULL, 0);
  uint32_t c24 = vedE->p.textRgb;
  uint16_t c = rgb565((c24 >> 16) & 255, (c24 >> 8) & 255, c24 & 255);
  uint8_t cov[VED_BAND_W];
  int n = tw < VED_BAND_W ? tw : VED_BAND_W;
  for(int r = 0; r < hpx; r++){
    int yy = y + r;
    if(yy < vedVY || yy >= vedVY + vedVH) continue;
    gedTextFn(NULL, vedE->p.text, hpx, r, 0, cov, n);
    for(int i = 0; i < n; i++){
      int xx = x + i;
      if(cov[i] && xx >= vedVX && xx < vedVX + vedVW) pxA(xx, yy, c, cov[i]);
    }
  }
  const uint16_t W = rgb565(255, 255, 255);                 // marco de "se puede mover", sobre el video
  for(int i = 0; i < tw; i += 6) if(x + i < vedVX + vedVW){ px(x + i, y - 3, W); px(x + i, y + hpx + 2, W); }
}
static void vedDrawWell(bool flush){
  VedGeom g = vedGeom();
  setBuf(fb);
  fillRect(g.wellX, g.wellY, g.wellW, g.wellH, VED_BG);
  if(vedPhase == VED_EDIT || vedPhase == VED_EXPORTING){
    if(!vedViewOk) vedViewBuild();
    if(vedViewOk || vedViewHas){
      gedBlit565(vedView, vedVX, vedVY, vedVW, vedVH);
      int oy0 = gClipY0, oy1 = gClipY1;
      gClipY0 = g.wellY; gClipY1 = g.wellY + g.wellH - 1;
      if(vedPhase == VED_EDIT){
        if(vedCropView()) vedDrawCropUi();
        else if(vedTool == VT_TEXT && vedDrag == VD_TEXT) vedDrawTextLive();
      }
      gClipY0 = oy0; gClipY1 = oy1;
    } else if(vedBaseGen == 0) drawTextC(g.wellX + g.wellW / 2, g.wellY + g.wellH / 2 - 8, "Cargando...", 2, VED_TXT2);
    else drawTextC(g.wellX + g.wellW / 2, g.wellY + g.wellH / 2 - 8, "Sin vista previa de este fotograma", 1, VED_TXT2);
  }
  if(flush) flxFlush(g.wellY, g.wellY + g.wellH - 1);
}

// ---- Linea de tiempo ----
static void vedDrawTimeline(const VedGeom& g){
  fillRect(g.bx, g.tlY, g.bw, g.tlH, VED_BG);
  gedPanelBg(g.tlX, g.tlY + 2, g.tlW, g.tlH - 6, 18);
  if(!vedE || !vedM) return;
  const FlexVeParams* p = &vedE->p;
  // Fila de arriba: reproducir, posicion y duracion.
  int cy = g.tlY + 22, px0 = g.tlX + 28;
  fillCircleAA((float)px0, (float)cy, 15.0f, TH_PRIM);
  if(vedPlaying){ fillRect(px0 - 6, cy - 7, 4, 14, TH_ONACC); fillRect(px0 + 2, cy - 7, 4, 14, TH_ONACC); }
  else fillTriangle(px0 - 4, cy - 8, px0 - 4, cy + 8, px0 + 8, cy, TH_ONACC);
  char a[16], b[16], t[48];
  vedFmt(vedMsOf(vedHead), a, sizeof(a)); vedFmt(flexVeSrcDurationMs(&vedM->src), b, sizeof(b));
  snprintf(t, sizeof(t), "%s / %s", a, b);
  drawText(px0 + 24, cy - uiLineH(2) / 2, t, 2, VED_TXT);
  vedFmt(vedFinalMs(), a, sizeof(a));
  snprintf(t, sizeof(t), "Final %s", a);
  drawTextR(g.tlX + g.tlW - 14, cy - 4, t, 1, VED_TXT2);
  // Tira de miniaturas.
  int sy = g.stripY, sh = VED_STRIP_H;
  fillRoundRect(g.stripX, sy, g.stripW, sh, 6, VED_CHIP);
  int ready = __atomic_load_n(&vedS.thumbsDone, __ATOMIC_ACQUIRE);
  if(vedThumbs){
    uint16_t row[VED_BAND_W];                            // una fila de una celda, escalada; se publica con recorte
    for(int i = 0; i < VED_THUMBS && i < ready; i++){
      int x = g.stripX + i * g.stripW / VED_THUMBS, w = g.stripX + (i + 1) * g.stripW / VED_THUMBS - x;
      if(w <= 0) continue;
      if(w > VED_BAND_W) w = VED_BAND_W;
      const uint16_t* th = vedThumbs + (size_t)i * vedThW * vedThH;
      for(int r = 0; r < sh; r++){
        const uint16_t* src = th + (size_t)(r * vedThH / sh) * vedThW;
        for(int c = 0; c < w; c++) row[c] = src[c * vedThW / w];
        gedBlit565(row, x, sy + r, w, 1);
      }
    }
  }
  vedThSeen = ready;
  // Lo que NO se conserva, velado; las divisiones, en blanco.
  const uint16_t dark = rgb565(0, 0, 0), W = rgb565(255, 255, 255), acc = TH_PRIM;
  uint32_t prevB = 0;
  for(int i = 0; i <= p->nSeg; i++){
    uint32_t a0 = i < p->nSeg ? p->seg[i].a : vedFrames();
    if(a0 > prevB){ int x0 = vedFrameX(g, prevB), x1 = vedFrameX(g, a0); fillRectA(x0, sy, x1 - x0, sh, dark, 165); }
    if(i > 0 && i < p->nSeg && p->seg[i].a == p->seg[i - 1].b){ int x = vedFrameX(g, p->seg[i].a); fillRect(x - 1, sy - 2, 3, sh + 4, W); }
    if(i < p->nSeg) prevB = p->seg[i].b;
  }
  // El tramo del cabezal (el que se quitaria), marcado.
  int si = flexVeSegAt(p, vedHead);
  if(si >= 0 && p->nSeg > 1){
    int x0 = vedFrameX(g, p->seg[si].a), x1 = vedFrameX(g, p->seg[si].b);
    drawRoundRect(x0, sy - 1, x1 - x0, sh + 2, 4, acc);
  }
  // Extremos de lo que se conserva: se arrastran para recortar.
  int ax = vedFrameX(g, flexVeFirstKept(p)), bx = vedFrameX(g, flexVeLastKept(p) + 1);
  fillRoundRect(ax - 5, sy - 3, 10, sh + 6, 4, acc);
  fillRoundRect(bx - 5, sy - 3, 10, sh + 6, 4, acc);
  fillRect(ax - 1, sy + sh / 2 - 7, 2, 14, TH_ONACC); fillRect(bx - 1, sy + sh / 2 - 7, 2, 14, TH_ONACC);
  // Portada elegida: una marca bajo la tira.
  if(p->cover != FLEXVE_NO_COVER){ int cx = vedFrameX(g, p->cover); fillRoundRect(cx - 6, sy + sh + 2, 12, 4, 2, acc); }
  // Cabezal.
  int hx = vedFrameX(g, vedHead);
  fillRect(hx - 2, sy - 6, 4, sh + 12, dark);
  fillRect(hx - 1, sy - 5, 2, sh + 10, W);
  fillCircleAA((float)hx, (float)(sy - 6), 5.0f, W);
}

// ---- Panel de la herramienta ----
static void vedRowCell(const VedGeom& g, int n, int i, int& x, int& w){
  const int gap = 8, inner = g.panelW - 24;
  w = (inner - (n - 1) * gap) / n;
  x = g.panelX + 12 + i * (w + gap);
}
static int vedRowHit(const VedGeom& g, int n, int tx){
  for(int i = 0; i < n; i++){ int x, w; vedRowCell(g, n, i, x, w); if(tx >= x - 4 && tx <= x + w + 4) return i; }
  return -1;
}
static void vedHint(const VedGeom& g, int y, const char* s){
  int fs = uiFontFit(s, g.panelW - 24, 1);
  drawTextC(g.panelX + g.panelW / 2, y, s, fs, VED_TXT2);
}
// Colores del texto y sus tres tamanos: la MISMA paleta que el editor de fotos.
static void vedColorGeom(const VedGeom& g, int i, int& cx, int& cy){ cy = g.panelY + 66; cx = g.panelX + 26 + i * 30; }
static int vedColorHit(const VedGeom& g, int tx, int ty){
  for(int i = 0; i < GED_COLORS; i++){ int cx, cy; vedColorGeom(g, i, cx, cy); if(abs(tx - cx) <= 15 && abs(ty - cy) <= 18) return i; }
  return -1;
}
static void vedTrioGeom(const VedGeom& g, int i, int& x, int& y, int& w, int& h){
  int x0 = g.panelX + 26 + GED_COLORS * 30;
  w = (g.panelX + g.panelW - 12 - x0 - 8) / 3; h = 34;
  x = x0 + i * (w + 4); y = g.panelY + 49;
}
static int vedTrioHit(const VedGeom& g, int tx, int ty){
  for(int i = 0; i < 3; i++){ int x, y, w, h; vedTrioGeom(g, i, x, y, w, h); if(tx >= x && tx <= x + w && ty >= y - 4 && ty <= y + h + 4) return i; }
  return -1;
}
static void vedFilterGeom(const VedGeom& g, int f, int& x, int& y, int& side){
  const int gap = 6;
  side = (g.panelW - 24 - (FLEXIE_FILTER_N - 1) * gap) / FLEXIE_FILTER_N;
  if(side > 52) side = 52;
  int total = FLEXIE_FILTER_N * side + (FLEXIE_FILTER_N - 1) * gap;
  x = g.panelX + (g.panelW - total) / 2 + f * (side + gap);
  y = g.panelY + 10;
}
static const char* vedAudioName(){
  const FlexVeAudio* a = &vedM->src.aud;
  switch(a->tag){
    case 0x0055: return "MP3";
    case 0x00FF: case 0x1610: return "AAC";
    case 0x2000: return "AC-3";
    case 0x0002: return "MS ADPCM";
    case 0x0006: case 0x0007: return "G.711";
  }
  return "comprimido";
}
static void vedDrawPanel(const VedGeom& g){
  fillRect(g.bx, g.panelY - 4, g.bw, VED_PANEL_H + 4, VED_BG);
  gedPanelBg(g.panelX, g.panelY, g.panelW, VED_PANEL_H - 6, 20);
  if(!vedE || !vedM) return;
  const FlexVeParams* p = &vedE->p;
  char b[96];
  switch(vedTool){
    case VT_CUT: {
      char s0[16], s1[16], s2[16];
      vedFmt(vedMsOf(flexVeFirstKept(p)), s0, sizeof(s0)); vedFmt(vedMsOf(flexVeLastKept(p) + 1), s1, sizeof(s1));
      vedFmt(vedFinalMs(), s2, sizeof(s2));
      snprintf(b, sizeof(b), "Inicio %s  \xC2\xB7  Fin %s  \xC2\xB7  Final %s%s", s0, s1, s2, p->nSeg > 1 ? "" : "");
      vedHint(g, g.panelY + 10, b);
      bool canSplit = flexVeCanSplitAt(vedE, vedHead);
      bool canDel = p->nSeg > 1 && flexVeSegAt(p, vedHead) >= 0;
      bool timeMod = p->nSeg != 1 || p->seg[0].a != 0 || p->seg[0].b != vedFrames();
      static const char* const L[3] = { "Dividir aqu\xC3\xAD", "Quitar parte", "Restablecer" };
      bool en[3] = { canSplit, canDel, timeMod };
      for(int i = 0; i < 3; i++){ int x, w; vedRowCell(g, 3, i, x, w); gedChip(x, g.panelY + 32, w, 40, L[i], false, en[i]); }
      if(p->nSeg > 1) snprintf(b, sizeof(b), "%d partes: toca una y pulsa Quitar parte para borrarla", p->nSeg);
      else snprintf(b, sizeof(b), "Arrastra los extremos de color para recortar");
      vedHint(g, g.panelY + 90, b);
      break;
    }
    case VT_CROP: {
      for(int i = 0; i < FLEXVE_ASP_N; i++){ int x, w; vedRowCell(g, FLEXVE_ASP_N, i, x, w); gedChip(x, g.panelY + 10, w, 32, VED_ASPECT[i], i == p->aspect, true); }
      bool full = p->c0x <= 0.0f && p->c0y <= 0.0f && p->c1x >= 1.0f && p->c1y >= 1.0f;
      int x, w; vedRowCell(g, 1, 0, x, w);
      gedChip(x, g.panelY + 52, w, 32, "Restablecer encuadre", false, !full || p->aspect != FLEXVE_ASP_FREE);
      int rw, rh; flexVeRegionSize(p, vedM->src.w, vedM->src.h, &rw, &rh);
      snprintf(b, sizeof(b), "Arrastra las esquinas, los lados o el centro  \xC2\xB7  %d x %d", rw, rh);
      vedHint(g, g.panelY + 96, b);
      break;
    }
    case VT_ROT: {
      static const char* const R[4] = { "0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0" };
      for(int i = 0; i < 4; i++){ int x, w; vedRowCell(g, 4, i, x, w); gedChip(x, g.panelY + 10, w, 36, R[i], (p->rot & 3) == i, true); }
      int x, w; vedRowCell(g, 1, 0, x, w);
      fillRoundRect(x, g.panelY + 56, w, 36, 18, VED_CHIP);
      gedIcoRotate(x + w / 2 - 60, g.panelY + 74, VED_TXT, true);
      drawTextC(x + w / 2 + 12, g.panelY + 56 + (36 - uiLineH(2)) / 2, "Girar 90\xC2\xB0", 2, VED_TXT);
      vedHint(g, g.panelY + 100, "El encuadre gira con la imagen");
      break;
    }
    case VT_SPEED: {
      for(int i = 0; i < FLEXVE_SPEED_N; i++){ int x, w; vedRowCell(g, FLEXVE_SPEED_N, i, x, w); gedChip(x, g.panelY + 10, w, 34, VED_SPEED[i], p->speedPct == FLEXVE_SPEEDS[i], true); }
      char d[16]; vedFmt(vedFinalMs(), d, sizeof(d));
      uint32_t per = flexVeOutPeriodUs(p, vedM->src.usPerFrame);
      snprintf(b, sizeof(b), "Duraci\xC3\xB3n final %s  \xC2\xB7  %.1f fps", d, per ? 1000000.0f / per : 0.0f);
      drawTextC(g.panelX + g.panelW / 2, g.panelY + 56, b, 2, VED_TXT);
      bool aud = vedM->src.aud.kind == FLEXVE_AUD_PCM || vedM->src.aud.kind == FLEXVE_AUD_IMA;
      vedHint(g, g.panelY + 92, p->speedPct == 100 ? "Sin fotogramas inventados: m\xC3\xA1s lento = menos fps"
                                : aud ? "El sonido acompa\xC3\xB1" "a a la imagen y cambia de tono" : "M\xC3\xA1s r\xC3\xA1pido quita fotogramas; m\xC3\xA1s lento, menos fps");
      break;
    }
    case VT_VOL: {
      const FlexVeAudio* a = &vedM->src.aud;
      if(a->kind == FLEXVE_AUD_NONE){
        drawTextC(g.panelX + g.panelW / 2, g.panelY + 34, "Este v\xC3\xAD" "deo no tiene sonido", 2, VED_TXT2);
        vedHint(g, g.panelY + 70, "Solo se ajusta el audio que ya trae el v\xC3\xAD" "deo");
        break;
      }
      if(a->kind == FLEXVE_AUD_OTHER){
        snprintf(b, sizeof(b), "Audio %s", vedAudioName());
        drawTextC(g.panelX + g.panelW / 2, g.panelY + 26, b, 2, VED_TXT);
        vedHint(g, g.panelY + 60, "El P4 no puede ajustar este audio:");
        vedHint(g, g.panelY + 78, "el v\xC3\xAD" "deo editado se guardar\xC3\xA1 sin sonido");
        break;
      }
      for(int i = 0; i < 5; i++){
        int x, w; vedRowCell(g, 5, i, x, w);
        snprintf(b, sizeof(b), "%u %%", (unsigned)VED_VOL[i]);
        gedChip(x, g.panelY + 10, w, 34, b, !p->mute && p->volPct == VED_VOL[i], !p->mute);
      }
      int x, w; vedRowCell(g, 1, 0, x, w);
      gedChip(x, g.panelY + 54, w, 32, p->mute ? "Silenciado (toca para quitar)" : "Silenciar", p->mute, true);
      snprintf(b, sizeof(b), "Audio original: %s  \xC2\xB7  %u Hz  \xC2\xB7  %s", a->kind == FLEXVE_AUD_IMA ? "IMA ADPCM" : "PCM",
               (unsigned)a->rate, a->channels == 2 ? "est\xC3\xA9reo" : "mono");
      vedHint(g, g.panelY + 98, b);
      break;
    }
    case VT_TEXT: {
      if(!p->text[0]){
        int x, w; vedRowCell(g, 1, 0, x, w);
        fillRoundRect(x, g.panelY + 8, w, 32, 16, TH_PRIM);
        drawTextC(x + w / 2, g.panelY + 8 + (32 - uiLineH(2)) / 2, "+ A\xC3\xB1" "adir texto", 2, TH_ONACC);
      } else {
        int x, w; vedRowCell(g, 2, 0, x, w); gedChip(x, g.panelY + 8, w, 32, "Cambiar texto", false, true);
        vedRowCell(g, 2, 1, x, w); gedChip(x, g.panelY + 8, w, 32, "Quitar texto", false, true);
      }
      for(int i = 0; i < GED_COLORS; i++){
        int cx, cy; vedColorGeom(g, i, cx, cy);
        uint16_t c = rgb565((GED_RGB[i] >> 16) & 255, (GED_RGB[i] >> 8) & 255, GED_RGB[i] & 255);
        if(GED_RGB[i] == p->textRgb) fillCircleAA((float)cx, (float)cy, 15.0f, VED_TXT);
        fillCircleAA((float)cx, (float)cy, 12.0f, c);
        if(i <= 1) drawCircle(cx, cy, 12, VED_TXT2);
      }
      for(int i = 0; i < 3; i++){
        int x, y, w, h; vedTrioGeom(g, i, x, y, w, h);
        bool on = p->textSize == i;
        fillRoundRect(x, y, w, h, 12, on ? TH_PRIM : VED_CHIP);
        drawTextC(x + w / 2, y + (h - uiLineH(1 + i)) / 2 + (i == 2 ? 2 : 0), "A", 1 + i, on ? TH_ONACC : VED_TXT);
      }
      vedHint(g, g.panelY + 98, p->text[0] ? "Arrastra el texto sobre el v\xC3\xAD" "deo para colocarlo" : "Un texto sobre todo el v\xC3\xAD" "deo");
      break;
    }
    case VT_FILTER: {
      int x, y, side; vedFilterGeom(g, 0, x, y, side);
      vedFilterThumbs(side);
      for(int f = 0; f < FLEXIE_FILTER_N; f++){
        vedFilterGeom(g, f, x, y, side);
        bool on = p->filter == f;
        if(on) fillRoundRect(x - 3, y - 3, side + 6, side + 6, 12, TH_PRIM);
        if(vedFt && vedFtOk) gedBlit565(vedFt + (size_t)f * side * side, x, y, side, side);
        else fillRoundRect(x, y, side, side, 10, VED_CHIP);
        int fs = uiFontFit(GED_FILTER[f], side + 4, 1);
        drawTextC(x + side / 2, y + side + 8, GED_FILTER[f], fs, on ? TH_PRIM : VED_TXT2);
      }
      vedHint(g, g.panelY + 98, "Se aplica a todo el v\xC3\xAD" "deo al exportar");
      break;
    }
    default: {                                            // Portada
      char t[16];
      if(p->cover == FLEXVE_NO_COVER) snprintf(b, sizeof(b), "Portada: el primer fotograma");
      else { vedFmt(vedMsOf(p->cover), t, sizeof(t)); snprintf(b, sizeof(b), "Portada: el fotograma de %s", t); }
      int fs = uiFontFit(b, g.panelW - 24, 2);
      drawTextC(g.panelX + g.panelW / 2, g.panelY + 10, b, fs, VED_TXT);
      bool kept = flexVeSegAt(p, vedHead) >= 0, set = p->cover != FLEXVE_NO_COVER;
      int x, w; vedRowCell(g, 2, 0, x, w);
      fillRoundRect(x, g.panelY + 40, w, 38, 19, kept ? TH_PRIM : VED_CHIP);
      int f2 = uiFontFit("Usar este fotograma", w - 12, 2);
      drawTextC(x + w / 2, g.panelY + 40 + (38 - uiLineH(f2)) / 2, "Usar este fotograma", f2, kept ? TH_ONACC : VED_DIS);
      vedRowCell(g, 2, 1, x, w);
      gedChip(x, g.panelY + 40, w, 38, "Primer fotograma", false, set);
      vedHint(g, g.panelY + 92, kept ? "Mueve la l\xC3\xAD" "nea blanca hasta el fotograma que quieras" : "El cabezal est\xC3\xA1 en una parte que se quita");
      break;
    }
  }
}

// ---- Herramientas ----
static void vedDrawTabs(const VedGeom& g){
  fillRect(g.bx, g.tabsY, g.bw, VED_TABS_H, VED_BG);
  int tw = g.bw / VT_N;
  for(int t = 0; t < VT_N; t++){
    int cx = g.bx + t * tw + tw / 2;
    bool on = t == vedTool;
    uint16_t c = on ? TH_PRIM : VED_TXT2;
    vedIcoTool(t, cx, g.tabsY + 20, c);
    int fs = uiFontFit(VED_TOOL[t], tw - 4, 1);
    drawTextC(cx, g.tabsY + 38, VED_TOOL[t], fs, c);
    if(on) fillRoundRect(cx - 12, g.tabsY + 52, 24, 4, 2, TH_PRIM);
  }
}

// ---- Progreso (abrir / exportar) ----
static void vedCardGeom(const VedGeom& g, int& x, int& y, int& w, int& h){
  w = g.bw - 2 * g.pad - 24; if(w > 380) w = 380;
  h = 170; x = g.bx + (g.bw - w) / 2; y = g.wellY + (g.wellH - h) / 2;
  // Abriendo: debajo de la miniatura del catalogo (no encima de ella).
  if(vedPhase == VED_OPENING){ int yo = g.wellY + 20 + ML_SIDE + 16, ymax = g.tabsY - h - 8; y = yo < ymax ? yo : ymax; }
}
static void vedDrawProgress(const VedGeom& g){
  int x, y, w, h; vedCardGeom(g, x, y, w, h);
  gedPanelBg(x, y, w, h, 24);
  bool exp = vedPhase == VED_EXPORTING;
  int pct = __atomic_load_n(&vedS.pct, __ATOMIC_ACQUIRE);
  if(pct < 0) pct = 0;
  if(pct > 1000) pct = 1000;
  bool canceling = vedLd(&vedS.cancel);
  drawTextC(x + w / 2, y + 22, canceling ? "Cancelando..." : exp ? "Exportando..." : "Abriendo v\xC3\xAD" "deo...", 3, VED_TXT);
  int bx = x + 24, bw = w - 48, by = y + 76;
  fillRoundRect(bx, by, bw, 8, 4, VED_CHIP);
  if(pct > 0){ int fw = (int)((int64_t)bw * pct / 1000); fillRoundRect(bx, by, fw < 8 ? 8 : fw, 8, 4, TH_PRIM); }
  char b[48];
  if(exp && vedM && vedM->nOut > 1) snprintf(b, sizeof(b), "%d %%  \xC2\xB7  %d partes", pct / 10, vedM->nOut);
  else snprintf(b, sizeof(b), "%d %%", pct / 10);
  drawTextC(x + w / 2, by + 16, b, 1, VED_TXT2);
  fillRoundRect(x + w / 2 - 70, y + h - 50, 140, 36, 18, VED_CHIP);
  drawTextC(x + w / 2, y + h - 50 + (36 - uiLineH(2)) / 2, "Cancelar", 2, canceling ? VED_DIS : VED_TXT);
  vedShownPct = pct; vedShownMs = millis();
}
static bool vedProgressCancelHit(const VedGeom& g, int tx, int ty){
  int x, y, w, h; vedCardGeom(g, x, y, w, h);
  return tx >= x + w / 2 - 80 && tx <= x + w / 2 + 80 && ty >= y + h - 56 && ty <= y + h - 8;
}

// ---- Hoja "Exportar" ----
// Tamano de salida de cada opcion (false = mayor que el original: no se ofrece).
static bool vedResDims(int i, int* ow, int* oh){
  int rw, rh; flexVeRegionSize(&vedE->p, vedM->src.w, vedM->src.h, &rw, &rh);
  return flexVeFitShort(rw, rh, VED_RES[i], ow, oh);
}
// Memoria que pedira exportar con esa opcion, y si la hay ahora.
static uint32_t vedExportNeed(int i){
  int ow, oh; if(!vedResDims(i, &ow, &oh)) return 0xFFFFFFFFu;
  uint32_t w = flexVeExportWorkBytes(&vedM->src, &vedE->p, 0, vedE->p.nSeg - 1, ow, oh);
  return w + (uint32_t)(sizeof(FlexVeExport) + sizeof(FlexImgEdit) + sizeof(FlexAviCtx)) + 64u * 1024u;
}
static bool vedResOk(int i){
  int ow, oh; if(!vedResDims(i, &ow, &oh)) return false;
  return memFreePsram() >= vedExportNeed(i) + FLEXMEM_RESERVE_BYTES + ML_PSRAM_MARGIN;
}
static bool vedCanParts(){ return vedE && vedE->p.nSeg >= 2; }
static int vedSheetBtnN(){ return vedCanParts() ? 4 : 3; }
static void vedSheetGeom(const VedGeom& g, int& x, int& y, int& w, int& h){
  w = g.bw - 2 * g.pad; x = g.bx + g.pad;
  h = 150 + vedSheetBtnN() * 52;
  y = g.by + g.bh - h - 8;
}
static void vedSheetBtn(const VedGeom& g, int i, int& x, int& y, int& w, int& h){
  int sx, sy, sw, sh; vedSheetGeom(g, sx, sy, sw, sh);
  int n = vedSheetBtnN();
  x = sx + 20; w = sw - 40; h = 44;
  y = sy + sh - 18 - (n - i) * 52;
}
static void vedResGeom(const VedGeom& g, int i, int& x, int& y, int& w, int& h){
  int sx, sy, sw, sh; vedSheetGeom(g, sx, sy, sw, sh);
  const int gap = 6, inner = sw - 40;
  w = (inner - (VED_RES_N - 1) * gap) / VED_RES_N; h = 40;
  x = sx + 20 + i * (w + gap); y = sy + 64;
}
static void vedDrawSheet(const VedGeom& g){
  int x, y, w, h; vedSheetGeom(g, x, y, w, h);
  fillRectA(g.bx, g.by, g.bw, g.bh, TH_SCRIM, 120);
  // Superficie ELEVADA del sistema. Solo la pinta vedRender, que acaba de
  // rehacer toda la pantalla: el vidrio desenfoca contenido limpio y no se
  // apila sobre si mismo.
  uiSurface(x, y, w, h, 26, UIS_ELEVATED);
  drawTextC(x + w / 2, y + 14, "Exportar v\xC3\xAD" "deo", 3, VED_TXT);
  int ow = 0, oh = 0; vedResDims(vedRes, &ow, &oh);
  uint32_t est = flexVeEstimateBytes(&vedM->src, &vedE->p, 0, vedE->p.nSeg - 1, ow, oh);
  char d[16], sz[16], b[120];
  vedFmt(vedFinalMs(), d, sizeof(d)); flexFsFmtSize(est, sz, sizeof(sz));
  snprintf(b, sizeof(b), "AVI MJPEG  \xC2\xB7  %d x %d  \xC2\xB7  %s  \xC2\xB7  aprox. %s", ow, oh, d, sz);
  int fs = uiFontFit(b, w - 30, 1);
  drawTextC(x + w / 2, y + 44, b, fs, VED_TXT2);
  for(int i = 0; i < VED_RES_N; i++){
    int rx, ry, rw, rh; vedResGeom(g, i, rx, ry, rw, rh);
    int cw, ch; bool fit = vedResDims(i, &cw, &ch);
    bool en = fit && vedResOk(i);
    // "Original" es el tamano del encuadre; la medida exacta de la elegida
    // va en la linea de arriba.
    gedChip(rx, ry, rw, rh, VED_RES_L[i], en && i == vedRes, en);
  }
  // Avisos honestos: lo que va a pasar de verdad.
  const char* n1 = NULL; char n2[96] = "";
  bool copy = flexVeCopyMode(&vedM->src, &vedE->p, ow, oh);
  if(vedM->src.aud.kind == FLEXVE_AUD_OTHER) n1 = "El audio de este v\xC3\xAD" "deo no se puede procesar: saldr\xC3\xA1 sin sonido";
  else if(copy) n1 = "Sin cambios en la imagen: los fotogramas se copian sin p\xC3\xA9rdida";
  else n1 = "La imagen se vuelve a codificar (JPEG, calidad alta)";
  if(est > FML_LIMIT_VIDEO) snprintf(n2, sizeof(n2), "Puede pasar de %u MB: si pasa, se detiene sin guardar nada", (unsigned)(FML_LIMIT_VIDEO >> 20));
  else if(!vedResOk(vedRes)) snprintf(n2, sizeof(n2), "No hay memoria libre para esta resoluci\xC3\xB3n: elige una menor");
  fs = uiFontFit(n1, w - 30, 1);
  drawTextC(x + w / 2, y + 114, n1, fs, VED_TXT2);
  if(n2[0]){ fs = uiFontFit(n2, w - 30, 1); drawTextC(x + w / 2, y + 132, n2, fs, TH_WARN); }
  bool en = vedResOk(vedRes);
  int n = vedSheetBtnN();
  for(int i = 0; i < n; i++){
    int bx, by, bw, bh; vedSheetBtn(g, i, bx, by, bw, bh);
    const char* L; bool prim = i == 0, cancel = i == n - 1;
    char pl[40];
    if(i == 0) L = "Guardar como copia";
    else if(i == 1) L = "Reemplazar original";
    else if(!cancel){ snprintf(pl, sizeof(pl), "Guardar las %d partes por separado", vedE->p.nSeg); L = pl; }
    else L = "Cancelar";
    if(!cancel) fillRoundRect(bx, by, bw, bh, bh / 2, prim ? (en ? TH_PRIM : VED_CHIP) : TH_SURF);
    uint16_t col = cancel ? VED_TXT2 : !en ? VED_DIS : prim ? TH_ONACC : VED_TXT;
    int f2 = uiFontFit(L, bw - 20, 2);
    drawTextC(bx + bw / 2, by + (bh - uiLineH(f2)) / 2, L, f2, col);
  }
}

// ---- Pantalla entera ----
static void vedRender(){
  if(!vedActive()) return;
  if(fkNameOn && vedTextAsk){ fkNameDraw(); return; }  // el teclado del texto ocupa la pantalla
  VedGeom g = vedGeom();
  setBuf(fb);
  fillRect(g.bx, g.by, g.bw, g.bh, VED_BG);
  vedDrawBar(g);
  if(vedPhase == VED_OPENING){
    // Abriendo: la miniatura del catalogo (si la hay) y el progreso real.
    if(gMlOk){
      mlLock();
      int i = flexMlFindId(&gMs.lib, vedId), budget = 1;
      const uint16_t* th = i >= 0 ? mlThumbGetLocked(&gMs.lib.recs[i], &budget, NULL) : NULL;
      if(th) mlBlitThumb(th, g.bx + (g.bw - ML_SIDE) / 2, g.wellY + 20, ML_SIDE, ML_SIDE, 16, VED_BG);
      mlUnlock();
    }
    vedDrawProgress(g);
  } else {
    vedDrawWell(false);
    vedDrawTimeline(g);
    vedDrawPanel(g);
    if(vedPhase == VED_EXPORTING) vedDrawProgress(g);
  }
  vedDrawTabs(g);
  if(vedSheet && vedPhase == VED_EDIT) vedDrawSheet(g);
  if(mmDlgOn) mmDlgDraw();                               // un aviso abierto sigue encima (sobre contenido limpio)
  flxFlush(g.barY, WIN_BOT);
}
// Solo lo que cambio: cada zona sobre su propio fondo, publicada sola.
static void vedPaintWell(){ vedDrawWell(true); }
static void vedPaintTimeline(){
  VedGeom g = vedGeom(); setBuf(fb);
  vedDrawTimeline(g);
  flxFlush(g.tlY, g.tlY + g.tlH - 1);
  vedTlMs = millis(); vedTlPending = false;
}
static void vedPaintPanel(){
  VedGeom g = vedGeom(); setBuf(fb);
  vedDrawPanel(g);
  flxFlush(g.panelY - 4, g.tabsY - 1);
}
static void vedPaintBar(){
  VedGeom g = vedGeom(); setBuf(fb);
  vedDrawBar(g);
  flxFlush(g.barY, g.barY + g.barH - 1);
}
// Tras un cambio de parametros: barra (deshacer/exportar), video, linea de
// tiempo y panel, publicados de una vez.
static void vedAfterChange(){
  vedChanged();
  VedGeom g = vedGeom();
  setBuf(fb);
  vedDrawBar(g);
  vedDrawWell(false);
  vedDrawTimeline(g);
  vedDrawPanel(g);
  flxFlush(g.barY, g.tabsY - 1);
}

// #############################################################
// ##  ABRIR, CERRAR, SOLTAR MEMORIA
// #############################################################
// Lo que el menu y el visor ofrecen editar: un video AVI MJPEG abierto (no
// protegido), que el P4 reproduce y que no pasa del tope de los videos.
static bool vedEditable(const FlexMlRec* r){
  if(!r || r->kind != FML_K_VIDEO || (r->flags & FML_R_LOCKED) || r->state == FML_S_ERROR) return false;
  if(r->fmt != FML_F_AVI_MJPEG || r->size > FML_LIMIT_VIDEO) return false;
  return (r->flags & FML_R_PLAYABLE) || (r->flags & FML_R_NEED_THUMB);
}

// Lo que se reserva al abrir, sin contar la sesion (VedMem).
struct VedSizes { uint32_t base, view, band, cov, thumbs, arena, frame; int thW, thH; };
static VedSizes vedSizesFor(int srcW, int srcH, uint32_t fileBytes){
  VedSizes z; memset(&z, 0, sizeof(z));
  VedGeom g = vedGeom();
  uint32_t px = (srcW > 0 && srcH > 0) ? (uint32_t)srcW * (uint32_t)srcH : VED_PREVIEW_PX;
  if(px > VED_PREVIEW_PX) px = VED_PREVIEW_PX;
  z.base = px * 3u;
  int vw = g.wellW - 2 * g.pad; if(vw > VED_BAND_W) vw = VED_BAND_W;
  z.view = (uint32_t)vw * (uint32_t)(g.wellH > 12 ? g.wellH - 12 : 1) * 2u;
  z.band = (uint32_t)VED_BAND_W * VED_BAND * 3u;
  z.cov = (uint32_t)VED_BAND_W * VED_BAND;
  z.thW = g.stripW / VED_THUMBS; z.thH = VED_STRIP_H;
  if(z.thW < 8) z.thW = 8;
  z.thumbs = (uint32_t)VED_THUMBS * z.thW * z.thH * 2u;
  z.arena = flexVeDecodeWork(srcW > 0 ? srcW : 1920);
  // El mayor fotograma aun no se sabe: aqui solo se ESTIMA para comprobar la
  // memoria. Se reserva de su tamano real al analizar el archivo.
  uint32_t fr = fileBytes / 8u;
  if(fr < 64u * 1024u) fr = 64u * 1024u;
  if(fr > FLEXAVI_FRAME_MAX) fr = FLEXAVI_FRAME_MAX;
  z.frame = fr;
  return z;
}
static void vedFreeBuffers(){
  if(vedM) flexIeSetBase(&vedM->ie, NULL, 0, 0);
  mediaFree(vedBase); vedBase = NULL; vedBaseCap = 0; vedBaseW = vedBaseH = vedBaseDiv = 0;
  vedBaseFrame = 0xFFFFFFFFu; vedBaseGen = 0;
  mediaFree(vedView); vedView = NULL; vedViewCap = 0; vedViewOk = false; vedViewHas = false;
  mediaFree(vedBand); vedBand = NULL;
  mediaFree(vedCov); vedCov = NULL;
  mediaFree(vedThumbs); vedThumbs = NULL; vedThSeen = 0;
  mediaFree(vedFt); vedFt = NULL; vedFtOk = false; vedFtSide = 0;
  mediaFree(vedFrameBuf); vedFrameBuf = NULL; vedFrameCap = 0;
  mediaFree(vedArenaMem); vedArenaMem = NULL; vedArenaCap = 0;
  if(vedM) flexVeArenaInit(&vedM->arena, NULL, 0);
}
static void vedFreeAll(){
  vedFreeBuffers();
  mediaFree(vedM); vedM = NULL; vedE = NULL;
}
static bool vedAllocBuffers(const VedSizes& z){
  vedBase = (uint8_t*)mediaAlloc(z.base);       vedBaseCap = vedBase ? z.base : 0;
  vedView = (uint16_t*)mediaAlloc(z.view);      vedViewCap = vedView ? z.view : 0;
  vedBand = (uint8_t*)mediaAlloc(z.band);
  vedCov = (uint8_t*)mediaAlloc(z.cov);
  vedThumbs = (uint16_t*)mediaAlloc(z.thumbs);  vedThW = z.thW; vedThH = z.thH;
  vedArenaMem = (uint8_t*)mediaAlloc(z.arena);  vedArenaCap = vedArenaMem ? z.arena : 0;
  vedFrameBuf = NULL; vedFrameCap = 0;             // lo reserva el analisis, de su tamano real
  if(!vedBase || !vedView || !vedBand || !vedCov || !vedThumbs || !vedArenaMem){ vedFreeBuffers(); return false; }
  flexVeArenaInit(&vedM->arena, vedArenaMem, vedArenaCap);
  return true;
}

// ¿Se esta viendo? A pantalla completa lo dice el estado de la app; dentro
// de una ventana de DeX, que su tick haya corrido hace nada.
static uint32_t vedTickMs = 0;
static bool vedTicking(){ return vedTickMs && (int32_t)(millis() - vedTickMs) < 300; }
static bool vedForeground(){
  return (gState == ST_APP && gAppId == IC_GALERIA && gAppState[IC_GALERIA] == ALIFE_RUNNING) || vedTicking();
}
// Volver a la Galeria (rejilla o visor) es un CAMBIO DE PANTALLA: el marco
// entero, una vez (lo mismo que el editor de fotos y el visor).
static void vedToGallery(){ mkRedrawAll(); }

static void vedBeforeChange(uint32_t id);
static void vedListen(bool on){
  if(on) gMlBeforeChange2 = vedBeforeChange;
  else if(gMlBeforeChange2 == vedBeforeChange) gMlBeforeChange2 = NULL;
}

// Suelta TODO y deja el editor cerrado. No publica nada.
static void vedCloseNow(){
  vedPlaying = false;
  if(vedTextAsk){ fkNameOn = false; vedTextAsk = false; }   // ni el teclado del texto se queda abierto
  if(!vedWorkerStopWait()){ vedPhase = VED_OFF; return; }   // el trabajador sigue con lo suyo: no se toca
  vedFreeAll();
  vedListen(false);
  vedPhase = VED_OFF; vedId = 0;
  vedSheet = false; vedAsk = VA_NONE; vedDrag = VD_NONE; vedTextAsk = false;
  vedReopen = false; vedLivePending = false; vedTlPending = false; vedPathPending = false;
  vedViewNoText = false;
}
// Si el trabajador tardo mas que la espera al cerrar, lo que usaba se recoge
// cuando por fin acaba (nunca antes).
static void vedReclaim(){
  if(!vedLeak || !vedLd(&vedS.exited)) return;
  vedSrcClose();                                         // ya no lo usa nadie (el trabajador lo cierra al salir; por si acaso)
  if(vedM && vedLd(&vedS.job) == VJ_EXPORT && vedJobDone() && vedM->rc == 0)
    for(int k = 0; k < vedM->nOut; k++) flexFsDelete(vedM->tmp[k]);
  vedS.started = 0; vedS.task = NULL; vedLeak = false;
  if(vedPhase == VED_OFF){ vedFreeAll(); vedListen(false); }
  Serial.println(F("[video] el trabajador termino: memoria recogida"));
}
// Se cierra por algo de fuera (el video se protegio, se borro o cambio):
// fuera los pixeles YA, y se dice por que.
static bool galViewerOpen();                             // la Galeria tiene su visor abierto (AppGallery)
// Un aviso con el editor YA cerrado. Sobre la rejilla, un dialogo (lo atiende
// la propia Galeria, mkTick); sobre el visor, por la isla: el visor ocupa la
// pantalla entera, no lleva dialogos encima y un dialogo alli se quedaria sin
// nadie que lo atendiera.
static void vedTellClosed(const char* title, const char* why){
  if(galViewerOpen()) sysNotify(title, why);
  else mmDlgOpen(title, why, "Aceptar", "", false);
}
static void vedAbort(const char* why){
  Serial.printf("[video] cerrado: %s\n", why);
  vedCloseNow();
  if(vedForeground()){ vedToGallery(); vedTellClosed("Editor cerrado", why); }
  else sysNotify("Galer\xC3\xAD" "a", why);
}
static void vedOpenFail(const char* title, const char* why){
  galRender();
  vedTellClosed(title, why);
}

// Pide el analisis del archivo y su primer fotograma.
static void vedStartOpen(){
  int nw, nh; vedNeedDims(&nw, &nh);
  vedM->wantFrame = vedHead; vedM->needW = nw; vedM->needH = nh;
  vedSt(&vedS.thumbsWant, 0);
  __atomic_store_n(&vedS.thumbsDone, 0, __ATOMIC_RELEASE);
  vedSt(&vedS.baseOwner, 1);                         // la base es del trabajador mientras abre
  vedPhase = VED_OPENING;
  vedJobStart(VJ_OPEN);
}
// Memoria para la sesion: comprobada ANTES de reservar nada.
static bool vedMemoryOk(const VedSizes& z, bool withSession, char* why, size_t cap){
  uint32_t need = z.base + z.view + z.band + z.cov + z.thumbs + z.arena + z.frame + (withSession ? (uint32_t)sizeof(VedMem) : 0u);
  uint32_t fr = memFreePsram();
  if(fr < need + FLEXMEM_RESERVE_BYTES + VED_PSRAM_MARGIN){
    Serial.printf("[video] sin memoria: libre %lu KB, hace falta %lu KB + reserva\n", (unsigned long)(fr / 1024u), (unsigned long)(need / 1024u));
    snprintf(why, cap, "No hay memoria libre para abrir el editor de v\xC3\xAD" "deo. Cierra alguna app e int\xC3\xA9ntalo de nuevo.");
    return false;
  }
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < VED_TASK_INTERNAL){
    snprintf(why, cap, "La memoria interna del sistema est\xC3\xA1 al l\xC3\xADmite: el editor no puede empezar ahora.");
    return false;
  }
  return true;
}

static bool vedOpen(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r)) return false;
  if(!vedEditable(&r)){
    vedOpenFail("No se puede editar", "El editor abre v\xC3\xAD" "deos AVI MJPEG que el P4 reproduce (no protegidos).");
    return false;
  }
  vedReclaim();
  if(vedLeak){
    vedOpenFail("Editor ocupado", "El \xC3\xBAltimo trabajo del editor no ha terminado. Int\xC3\xA9ntalo en un momento.");
    return false;
  }
  if(vedActive()){
    // UNA sola instancia: el mismo video reutiliza la sesion abierta.
    if(vedId == id && vedPhase != VED_OFF){ vedRender(); return true; }
    vedCloseNow();
    if(vedActive() || vedLeak){ vedOpenFail("Editor ocupado", "El editor de v\xC3\xAD" "deo est\xC3\xA1 terminando otro trabajo."); return false; }
  }
  if(gedActive()) gedCloseNow();                     // un solo editor a la vez en la Galeria
  VedSizes z = vedSizesFor(r.w, r.h, r.size);
  char why[160];
  if(!vedMemoryOk(z, true, why, sizeof(why))){ vedOpenFail("Sin memoria", why); return false; }
  vedM = (VedMem*)mediaAlloc(sizeof(VedMem));
  if(!vedM){ vedOpenFail("Sin memoria", "No hay memoria libre para abrir el editor de v\xC3\xAD" "deo."); return false; }
  memset(vedM, 0, sizeof(*vedM));
  if(!vedAllocBuffers(z)){
    vedFreeAll();
    vedOpenFail("Sin memoria", "No hay memoria libre para abrir el editor de v\xC3\xAD" "deo.");
    return false;
  }
  vedE = &vedM->ed;
  flexVeInit(vedE, 1, 40000u, r.w, r.h);
  flexIeInit(&vedM->ie, NULL, 0, 0);
  flexIeSetTextFn(&vedM->ie, gedTextFn, NULL);
  vedM->src.w = r.w; vedM->src.h = r.h;                // para la geometria hasta que llegue el analisis
  snprintf(vedM->path, sizeof(vedM->path), "%s", r.path);
  snprintf(vedM->name, sizeof(vedM->name), "%s", r.name);
  snprintf(vedM->wpath, sizeof(vedM->wpath), "%s", r.path);
  vedId = id; vedTool = VT_CUT; vedHead = 0;
  vedSheet = false; vedAsk = VA_NONE; vedDrag = VD_NONE; vedTextAsk = false; vedPlaying = false;
  vedReopen = false; vedPathPending = false; vedLivePending = false; vedTlPending = false;
  vedViewNoText = false; vedRes = 0; vedSaveMode = VS_COPY; vedExtCancel = false;
  vedSeenRev = mlRev();
  if(!vedWorkerStart()){
    vedFreeAll();
    vedOpenFail("No se puede editar", "No se pudo iniciar el trabajo en segundo plano.");
    return false;
  }
  vedListen(true);
  Serial.printf("[video] abriendo id=%lu (%lu KB)\n", (unsigned long)id, (unsigned long)(r.size / 1024u));
  vedStartOpen();
  // CAMBIO DE PANTALLA: el marco ENTERO, una vez (la franja de abajo y la
  // cabecera no pueden heredar nada de la rejilla ni del visor).
  mkRedrawAll();
  return true;
}
// Volver despues de soltar la memoria (vedShed): se reserva otra vez y se
// relee el archivo; los parametros y el historial se conservan.
static bool vedRestart(){
  VedSizes z = vedSizesFor(vedM->src.w, vedM->src.h, vedM->src.fileBytes);
  char why[160];
  if(!vedMemoryOk(z, false, why, sizeof(why)) || !vedAllocBuffers(z) || !vedWorkerStart()){ vedFreeBuffers(); return false; }
  // Renombrado mientras estaba soltado: el trabajador aun no ha empezado, asi
  // que la ruta nueva se le da ya (si no, abriria la vieja y fallaria).
  if(vedPathPending){ snprintf(vedM->wpath, sizeof(vedM->wpath), "%s", vedM->path); vedPathPending = false; }
  vedStartOpen();
  return true;
}

// #############################################################
// ##  LO QUE DEVUELVE EL TRABAJADOR (loopTask)
// #############################################################
static uint32_t vedOutTotal(){ return vedE ? flexVeOutFrames(&vedE->p, 0, vedE->p.nSeg - 1) : 0; }
static uint32_t vedSrcOfOut(uint32_t k){
  const FlexVeParams* p = &vedE->p;
  for(int i = 0; i < p->nSeg; i++){
    uint32_t K = flexVeSegOutFrames(p, &p->seg[i]);
    if(k < K) return flexVeSrcFrameOf(p, &p->seg[i], k);
    k -= K;
  }
  return flexVeLastKept(p);
}
// La base volvio del trabajador: si trae un fotograma nuevo, la vista se rehace.
static bool vedTakeFrame(){
  if(vedLd(&vedS.baseOwner) != 0) return false;          // aun es del trabajador
  uint32_t gen = __atomic_load_n(&vedS.frameGen, __ATOMIC_ACQUIRE);
  if(gen == vedBaseGen) return false;
  vedBaseGen = gen;
  vedBaseFrame = vedM->gotFrame; vedBaseDiv = vedM->gotDiv;
  if(vedM->frameRc == FLEXVE_OK){ vedBaseW = vedM->gotW; vedBaseH = vedM->gotH; }
  else {
    vedBaseW = vedBaseH = 0;                             // se sigue viendo la ultima imagen buena
    if(vedM->frameRc == FLEXVE_ERR_MEMORY) Serial.println(F("[video] vista previa: sin memoria para este fotograma"));
  }
  vedChanged();
  return true;
}
// La vista quiere el fotograma del CABEZAL a la escala de ahora. Si la base
// esta en el trabajador no se encola nada: al volver se pide lo que haga
// falta ENTONCES (arrastrar o reproducir piden siempre el ultimo).
static void vedSyncFrame(){
  if(!vedM || !vedBase || vedPhase != VED_EDIT || !vedM->src.frames) return;
  if(vedLd(&vedS.baseOwner) != 0) return;
  if(__atomic_load_n(&vedS.frameGen, __ATOMIC_ACQUIRE) != vedBaseGen) return;   // hay uno sin recoger: primero se ve
  int nw, nh; vedNeedDims(&nw, &nh);
  int d = vedPreviewDiv(vedM->src.w, vedM->src.h, nw, nh, vedBaseCap);
  if(vedBaseGen && vedBaseFrame == vedHead && (!d || d == vedBaseDiv)) return;   // ya esta (o ya fallo asi)
  vedM->wantFrame = vedHead; vedM->needW = nw; vedM->needH = nh;
  vedSt(&vedS.baseOwner, 1);
  vedWake();
}

static bool vedNameTaken(const char* nm){ return gedNameTaken(nm); }
// "video (editado).avi", "video (editado 2).avi" / "video (parte 1).avi"...:
// un nombre que no ensena ya otro elemento de la biblioteca.
static void vedFreeName(char* out, size_t cap, int part){
  char stem[FML_NAME_MAX];
  snprintf(stem, sizeof(stem), "%s", vedM->name);
  char* dot = strrchr(stem, '.');
  if(dot && dot != stem) *dot = 0;
  char* ed = strstr(stem, " (editado");
  if(!ed) ed = strstr(stem, " (parte");
  if(ed && ed != stem) *ed = 0;
  size_t maxStem = cap > 26 ? cap - 26 : 0, L = strlen(stem);
  if(L > maxStem){ L = maxStem; while(L > 0 && (stem[L] & 0xC0) == 0x80) L--; stem[L] = 0; }
  for(int n = 1; n < 1000; n++){
    int k;
    if(part > 0) k = n == 1 ? snprintf(out, cap, "%s (parte %d).avi", stem, part) : snprintf(out, cap, "%s (parte %d-%d).avi", stem, part, n);
    else k = n == 1 ? snprintf(out, cap, "%s (editado).avi", stem) : snprintf(out, cap, "%s (editado %d).avi", stem, n);
    if(k < 0 || (size_t)k >= cap) break;
    if(!vedNameTaken(out)) return;
  }
}
// El trabajador dejo los temporales comprobados: se publican o se descartan.
static bool vedCommit(char* msg, size_t cap){
  VedMem* m = vedM;
  FlexMlRec r;
  bool have = mlGet(vedId, &r);
  char why[96] = "";
  // El video se protegio o se borro mientras se exportaba: no sale nada (una
  // copia de algo protegido no puede quedar sin proteger).
  if(!have || (r.flags & FML_R_LOCKED)){
    for(int k = 0; k < m->nOut; k++) flexFsDelete(m->tmp[k]);
    snprintf(msg, cap, "%s", !have ? "El v\xC3\xAD" "deo original ya no existe: no se ha guardado nada"
                                   : "El v\xC3\xAD" "deo se ha protegido: no se ha guardado nada");
    return false;
  }
  char dur[16]; vedFmt((uint32_t)((uint64_t)m->frames[0] * flexVeOutPeriodUs(&m->ep, m->src.usPerFrame) / 1000u), dur, sizeof(dur));
  if(vedSaveMode == VS_REPLACE){
    if(!mlReplaceFile(vedId, m->tmp[0], why, sizeof(why))){
      flexFsDelete(m->tmp[0]);
      snprintf(msg, cap, "No se pudo reemplazar: %s", why);
      return false;
    }
    snprintf(msg, cap, "V\xC3\xAD" "deo reemplazado (%d x %d, %s)", m->outW, m->outH, dur);
  } else if(vedSaveMode == VS_COPY){
    char nm[FML_NAME_MAX]; vedFreeName(nm, sizeof(nm), 0);
    if(!mlAddFile(m->tmp[0], FML_K_VIDEO, nm, FML_O_EDIT, vedId, why, sizeof(why))){
      flexFsDelete(m->tmp[0]);
      snprintf(msg, cap, "No se pudo guardar la copia: %s", why);
      return false;
    }
    snprintf(msg, cap, "Copia guardada: %s", nm);
  } else {
    int ok = 0;
    for(int k = 0; k < m->nOut; k++){
      char nm[FML_NAME_MAX]; vedFreeName(nm, sizeof(nm), k + 1);
      if(mlAddFile(m->tmp[k], FML_K_VIDEO, nm, FML_O_EDIT, vedId, why, sizeof(why))) ok++;
      else flexFsDelete(m->tmp[k]);
    }
    if(!ok){ snprintf(msg, cap, "No se pudo guardar ninguna parte: %s", why); return false; }
    if(ok < m->nOut) snprintf(msg, cap, "Se guardaron %d de %d partes (%s)", ok, m->nOut, why);
    else snprintf(msg, cap, "%d partes guardadas en la Galer\xC3\xAD" "a", ok);
  }
  uint32_t kb = 0; for(int k = 0; k < m->nOut; k++) kb += m->bytes[k] / 1024u;
  Serial.printf("[video] %s id=%lu %dx%d %lu KB en %lu ms%s\n",
                vedSaveMode == VS_REPLACE ? "reemplazado" : vedSaveMode == VS_COPY ? "copia de" : "partes de",
                (unsigned long)vedId, m->outW, m->outH, (unsigned long)kb, (unsigned long)m->tookMs,
                m->badFrames ? " (con fotogramas danados)" : "");
  return true;
}
// Recoge lo que termino el trabajador. true = algo cambio (repintar).
static bool vedCollect(){
  if(!vedS.started || !vedJobDone() || !vedM) return false;
  uint8_t job = vedLd(&vedS.job);
  bool canceled = vedLd(&vedS.cancel) != 0;
  vedSt(&vedS.jobState, VJS_IDLE);
  vedSt(&vedS.cancel, 0);                                // si no, la vista previa y las miniaturas se pararian
  VedMem* m = vedM;
  if(job == VJ_OPEN && m->rc == 0 && canceled) m->rc = 1;   // Cancelar llego cuando ya estaba abriendo el primer fotograma
  if(job == VJ_OPEN){
    if(m->rc == 0){
      m->src = m->wsrc;
      if(!vedReopen) flexVeInit(vedE, m->src.frames, m->src.usPerFrame, m->src.w, m->src.h);
      else {
        // La memoria se solto y el archivo se volvio a leer: los parametros
        // siguen (son fotogramas y fracciones), acotados a lo que hay.
        vedE->frames = m->src.frames;
        FlexVeParams* p = &vedE->p;
        bool bad = false;
        for(int i = 0; i < p->nSeg; i++){ if(p->seg[i].b > m->src.frames) p->seg[i].b = m->src.frames; if(p->seg[i].a >= p->seg[i].b) bad = true; }
        if(bad) flexVeInit(vedE, m->src.frames, m->src.usPerFrame, m->src.w, m->src.h);
      }
      vedReopen = false;
      if(vedHead >= m->src.frames) vedHead = 0;
      vedPhase = VED_EDIT;
      vedTakeFrame();
      vedSt(&vedS.thumbsWant, 1);
      vedWake();
      const char* aud = m->src.aud.kind == FLEXVE_AUD_PCM ? "PCM" : m->src.aud.kind == FLEXVE_AUD_IMA ? "IMA ADPCM"
                      : m->src.aud.kind == FLEXVE_AUD_OTHER ? "no editable" : "no";
      Serial.printf("[video] listo: %dx%d, %lu fotogramas, %lu ms, audio %s\n", m->src.w, m->src.h,
                    (unsigned long)m->src.frames, (unsigned long)flexVeSrcDurationMs(&m->src), aud);
    } else if(m->rc == 1){
      vedCloseNow();
      if(vedForeground()) vedToGallery();
    } else {
      char why[sizeof(m->why)]; memcpy(why, m->why, sizeof(why)); why[sizeof(why) - 1] = 0;
      Serial.printf("[video] no se puede editar: %s\n", why);
      vedCloseNow();
      if(vedForeground()){ vedToGallery(); vedTellClosed("No se puede editar", why); }
      else sysNotify("Galer\xC3\xAD" "a", why);
    }
    return true;
  }
  if(job == VJ_EXPORT){
    vedPhase = VED_EDIT;
    bool ext = vedExtCancel; vedExtCancel = false;
    char msg[200] = "";
    if(m->rc == 0){
      if(vedCommit(msg, sizeof(msg))){
        vedCloseNow();                                   // hecho: se vuelve a la Galeria
        if(vedForeground()) vedToGallery();
        sysNotify("Galer\xC3\xAD" "a", msg);
        return true;
      }
      FlexMlRec r;
      if(!mlGet(vedId, &r) || (r.flags & FML_R_LOCKED)){ vedSeenRev = mlRev(); vedAbort(msg); return true; }
    }
    else if(ext) snprintf(msg, sizeof(msg), "El v\xC3\xAD" "deo se ha cambiado fuera del editor mientras se exportaba: no se ha guardado nada");
    else if(m->rc == 1) snprintf(msg, sizeof(msg), "Exportaci\xC3\xB3n cancelada: el v\xC3\xAD" "deo no ha cambiado");
    else { memcpy(msg, m->why, sizeof(m->why)); msg[sizeof(m->why) - 1] = 0; }
    Serial.printf("[video] no exportado: %s\n", msg);
    vedSyncFrame();
    if(vedForeground()){ vedRender(); mmDlgOpen(m->rc == 1 && !ext ? "Cancelado" : "No se ha exportado", msg, "Aceptar", "", false); vedAsk = VA_INFO; }
    else sysNotify("Galer\xC3\xAD" "a", msg);
    return true;
  }
  return false;
}

// Si el video que se edita se protege, se borra o cambia por fuera, el
// editor lo nota en cuanto cambia el catalogo.
static void vedValidate(){
  if(!vedActive() || !gMlOk || !vedM) return;
  uint32_t rv = mlRev();
  if(rv == vedSeenRev) return;
  vedSeenRev = rv;
  FlexMlRec r;
  bool have = mlGet(vedId, &r);
  if(have && !(r.flags & FML_R_LOCKED)){
    if(strcmp(r.path, vedM->path)){                      // renombrado: se sigue editando
      snprintf(vedM->path, sizeof(vedM->path), "%s", r.path);
      snprintf(vedM->name, sizeof(vedM->name), "%s", r.name);
      vedPathPending = true;
    }
    if(vedPhase == VED_EDIT && vedM->src.fileBytes && r.size && r.size != vedM->src.fileBytes)
      vedAbort("El v\xC3\xAD" "deo ha cambiado fuera del editor: se ha cerrado sin guardar.");
    return;
  }
  vedAbort(have ? "El v\xC3\xAD" "deo se ha protegido: el editor se ha cerrado sin guardar."
                : "El v\xC3\xAD" "deo ya no existe: el editor se ha cerrado.");
}
// La ruta nueva (renombrado) se le pasa al trabajador cuando no esta leyendo.
static void vedApplyPath(){
  if(!vedPathPending || !vedM) return;
  int td = __atomic_load_n(&vedS.thumbsDone, __ATOMIC_ACQUIRE);
  if(vedJobRunning() || vedLd(&vedS.baseOwner) || (vedLd(&vedS.thumbsWant) && td < VED_THUMBS)) return;
  vedReleaseSource(false);
  snprintf(vedM->wpath, sizeof(vedM->wpath), "%s", vedM->path);
  vedPathPending = false;
}
// Fin de la retencion: el cambio que la pidio ya termino (corre en loopTask,
// igual que quien lo hizo), asi que el trabajador puede seguir.
static void vedHoldEnd(){
  if(vedS.started && vedLd(&vedS.hold)){ vedSt(&vedS.hold, 0); vedWake(); }
}
// Otra parte del sistema va a mover, borrar o sustituir el archivo que se
// edita: el trabajador lo suelta antes (y una exportacion en curso se cancela).
static void vedBeforeChange(uint32_t id){
  if(!vedActive() || id != vedId) return;
  if(vedJobRunning() && vedLd(&vedS.job) == VJ_EXPORT) vedExtCancel = true;
  vedReleaseSource(true);
}

// Desde loop(): publica una exportacion que termino con la Galeria en segundo
// plano y vigila el catalogo. Con el editor cerrado no cuesta nada.
static void vedBgTick(){
  vedReclaim();
  if(!vedActive()) return;
  vedHoldEnd();
  if(vedTicking()) return;                               // su tick corre: lo lleva vedTick
  vedCollect();
  vedValidate();
}

// ¿Hay trabajo real en curso? (APP_BG_KEEP: no se desaloja la Galeria
// mientras se exporta.)
static bool vedBusy(){ return vedS.started && vedLd(&vedS.job) == VJ_EXPORT && (vedJobRunning() || vedJobDone()); }
static bool vedDirty(){ return vedPhase == VED_EDIT && vedE && flexVeCanUndo(vedE) && !flexVeIsIdentity(vedE); }

// SOLTAR (Galeria suspendida): los fotogramas y las miniaturas se sueltan y el
// trabajador se va; los parametros y el historial se quedan.
static size_t vedShed(){
  if(vedPhase != VED_EDIT || vedJobRunning() || vedLeak || !vedBase) return 0;
  if(!vedWorkerStopWait()) return 0;                     // primero que pare: lo que mide y suelta ya no lo toca nadie
  size_t n = vedBaseCap + vedViewCap + (size_t)VED_BAND_W * VED_BAND * 4u + (size_t)VED_THUMBS * vedThW * vedThH * 2u +
             vedFrameCap + vedArenaCap + (vedFt ? (size_t)FLEXIE_FILTER_N * vedFtSide * vedFtSide * 2u : 0u);
  vedFreeBuffers();
  vedM->srcReady = false;                                // lo que se sabia del archivo se vuelve a leer al volver
  vedReopen = true;
  return n;
}
static void vedSuspend(){
  vedDrag = VD_NONE; vedLivePending = false; vedTlPending = false; vedPlaying = false; vedViewNoText = false;
  if(vedTextAsk){ fkNameOn = false; vedTextAsk = false; }   // el teclado del texto no sobrevive al segundo plano
}
static void vedResume(){
  if(!vedActive()) return;
  vedValidate();
  if(!vedActive()) return;
  if(vedPhase == VED_EDIT && vedReopen && !vedRestart()){
    vedAbort("No hay memoria libre para volver al editor de v\xC3\xAD" "deo. Los cambios no exportados se han perdido.");
    return;
  }
  vedChanged();
  vedRender();
}

// ---- Reproducir la vista previa (con los tramos y la velocidad) ----
static void vedPlayToggle(){
  if(vedPlaying){ vedPlaying = false; vedPaintTimeline(); vedPaintPanel(); return; }
  uint32_t total = vedOutTotal();
  if(!total) return;
  int32_t o = flexVeOutFrameOf(&vedE->p, 0, vedE->p.nSeg - 1, vedHead);
  if(o < 0 || (uint32_t)o + 1u >= total) o = 0;          // al final (o en lo quitado del final): desde el principio
  vedPlayOut0 = (uint32_t)o; vedPlayMs0 = millis(); vedPlaying = true;
  vedHead = vedSrcOfOut((uint32_t)o);
  vedSyncFrame();
  vedPaintTimeline();
}
static void vedPlayTick(){
  if(!vedPlaying) return;
  uint32_t per = flexVeOutPeriodUs(&vedE->p, vedM->src.usPerFrame);
  if(!per) per = 40000u;
  uint32_t total = vedOutTotal();
  uint32_t k = vedPlayOut0 + (uint32_t)((uint64_t)(millis() - vedPlayMs0) * 1000u / per);
  if(k >= total){
    vedPlaying = false;
    vedHead = vedSrcOfOut(total ? total - 1 : 0);
    vedSyncFrame();
    vedPaintTimeline(); vedPaintPanel();
    return;
  }
  uint32_t f = vedSrcOfOut(k);
  if(f != vedHead){
    vedHead = f;
    vedSyncFrame();                                      // si la base esta ocupada, se pide la ULTIMA al volver
    if(millis() - vedTlMs >= 120u) vedPaintTimeline();
  }
}

// #############################################################
// ##  ACCIONES
// ##  Cada una cambia PARAMETROS (y el historial); ninguna procesa un
// ##  fotograma. Tras el cambio se repinta con la base que hay y, si la
// ##  vista nueva pide otra escala, se pide el fotograma otra vez.
// #############################################################
static void vedApplied(){
  vedPlaying = false;
  vedAfterChange();
  vedSyncFrame();
}

static void vedSetTool(int t){
  if(t < 0 || t >= VT_N || t == vedTool || vedPhase != VED_EDIT) return;
  bool viewChanges = (t == VT_CROP) != (vedTool == VT_CROP);
  vedTool = (uint8_t)t;
  vedDrag = VD_NONE; vedViewNoText = false;
  VedGeom g = vedGeom();
  setBuf(fb);
  if(viewChanges){ vedViewOk = false; vedDrawWell(false); }
  vedDrawPanel(g);
  vedDrawTabs(g);
  flxFlush(viewChanges ? g.wellY : g.panelY - 4, g.tabsY + VED_TABS_H - 1);
  if(viewChanges) vedSyncFrame();                        // Encuadre ve el fotograma entero; lo demas, el recorte
}

static void vedUndoRedo(bool redo){
  if(vedPhase != VED_EDIT || vedDrag != VD_NONE) return;
  if(!(redo ? flexVeRedo(vedE) : flexVeUndo(vedE))) return;
  vedApplied();
}

// ---- Tiempo ----
static void vedSplitHere(){ if(flexVeSplit(vedE, vedHead)) vedApplied(); }
static void vedRemoveHere(){
  int i = flexVeSegAt(&vedE->p, vedHead);
  if(i < 0 || !flexVeRemoveSeg(vedE, i)) return;
  // El cabezal pasa al principio de lo que seguia (o al final de lo anterior).
  const FlexVeParams* p = &vedE->p;
  if(i < p->nSeg) vedHead = p->seg[i].a;
  else vedHead = p->seg[p->nSeg - 1].b ? p->seg[p->nSeg - 1].b - 1 : 0;
  vedApplied();
}
static void vedResetTime(){
  FlexVeParams* p = &vedE->p;
  p->nSeg = 1; p->seg[0].a = 0; p->seg[0].b = vedE->frames;
  flexVeCommit(vedE);
  vedApplied();
}

// ---- Encuadre y giro ----
static void vedResetCrop(){
  FlexVeParams* p = &vedE->p;
  p->aspect = FLEXVE_ASP_FREE;
  p->c0x = p->c0y = 0.0f; p->c1x = p->c1y = 1.0f;
  flexVeCommit(vedE);
  vedApplied();
}

// ---- Texto ----
// El texto no se sale de la imagen: lo que se ve es lo que se exporta.
static void vedClampText(){
  FlexVeParams* p = &vedE->p;
  if(!p->text[0] || !vedM) return;
  int rw, rh; flexVeRegionSize(p, vedM->src.w, vedM->src.h, &rw, &rh);
  if(rw < 1 || rh < 1) return;
  int L = rw > rh ? rw : rh;
  int hpx = (int)(FLEXVE_TEXT_H[p->textSize < 3 ? p->textSize : 1] * L + 0.5f);
  if(hpx < 4) hpx = 4;
  int tw = gedTextFn(NULL, p->text, hpx, -1, 0, NULL, 0);
  float mu = 1.0f - (float)tw / rw, mv = 1.0f - (float)hpx / rh;
  if(mu < 0.0f) mu = 0.0f;
  if(mv < 0.0f) mv = 0.0f;
  if(p->textU < 0.0f) p->textU = 0.0f;
  if(p->textU > mu) p->textU = mu;
  if(p->textV < 0.0f) p->textV = 0.0f;
  if(p->textV > mv) p->textV = mv;
}
static void vedSetText(const char* s){
  FlexVeParams* p = &vedE->p;
  // Se corta en un limite de caracter UTF-8, nunca a mitad.
  size_t n = strlen(s);
  if(n >= sizeof(p->text)){ n = sizeof(p->text) - 1; while(n > 0 && (s[n] & 0xC0) == 0x80) n--; }
  bool fresh = !p->text[0];
  memcpy(p->text, s, n); p->text[n] = 0;
  if(fresh){ p->textU = 0.08f; p->textV = 0.78f; }       // abajo a la izquierda, como un rotulo
  vedClampText();
  flexVeCommit(vedE);
  vedChanged();
}
static void vedAskText(){
  vedPlaying = false;
  vedTextAsk = true;
  fkNameOpenHint("Texto", vedE->p.text, "Escribe el texto y pulsa Guardar");
}
// Color y tamano: con texto, un paso del historial; sin el, se quedan
// preparados para cuando se anada.
static void vedTextStyle(){
  vedClampText();
  if(vedE->p.text[0]){ flexVeCommit(vedE); vedApplied(); }
  else vedPaintPanel();
}

// ---- Exportar ----
static void vedOpenSheet(){
  if(!vedCanExport()) return;
  vedPlaying = false; vedDrag = VD_NONE;
  // La resolucion elegida tiene que existir para ESTE recorte y caber en memoria.
  int ow, oh;
  if(!vedResDims(vedRes, &ow, &oh) || !vedResOk(vedRes)){
    vedRes = 0;
    for(int i = 0; i < VED_RES_N; i++) if(vedResDims(i, &ow, &oh) && vedResOk(i)){ vedRes = i; break; }
  }
  vedSheet = true;
  vedRender();
}
static void vedInfo(const char* title, const char* why){
  vedRender();
  mmDlgOpen(title, why, "Aceptar", "", false);
  vedAsk = VA_INFO;
}
static void vedStartExport(uint8_t mode){
  VedMem* m = vedM;
  const FlexVeParams* p = &vedE->p;
  vedSheet = false;
  int ow = 0, oh = 0;
  if(!vedResDims(vedRes, &ow, &oh)){ vedInfo("No se puede exportar", "Esa resoluci\xC3\xB3n es mayor que el encuadre."); return; }
  bool parts = mode == VS_PARTS && p->nSeg >= 2;
  int nOut = parts ? p->nSeg : 1;
  // Espacio: lo estimado (con el tope de un video por salida) y la reserva
  // que la particion no cede nunca. "Reemplazar" aparta el original sin
  // copiarlo: no necesita mas.
  uint64_t need = FML_RESERVE_BYTES + 64u * 1024u;
  for(int k = 0; k < nOut; k++){
    uint32_t e = flexVeEstimateBytes(&m->src, p, parts ? k : 0, parts ? k : p->nSeg - 1, ow, oh);
    need += e > FML_LIMIT_VIDEO ? FML_LIMIT_VIDEO : e;
  }
  uint32_t tot = flexFsTotalBytes(), used = flexFsUsedBytes(), fr = used < tot ? tot - used : 0;
  if(fr < need){
    char b[160];
    snprintf(b, sizeof(b), "Hacen falta unos %u KB libres y quedan %u KB. Libera espacio en Almacenamiento.",
             (unsigned)(need / 1024u), (unsigned)(fr / 1024u));
    vedInfo("No hay espacio", b);
    return;
  }
  if(!vedResOk(vedRes)){ vedInfo("Sin memoria", "No hay memoria libre para exportar a esta resoluci\xC3\xB3n. Elige una menor."); return; }
  m->ep = *p;
  m->eSeg0 = 0; m->eSeg1 = p->nSeg - 1;
  m->eParts = parts; m->nOut = nOut;
  m->eOutW = vedRes ? ow : 0; m->eOutH = vedRes ? oh : 0;          // 0 = el tamano del encuadre
  for(int k = 0; k < nOut; k++) snprintf(m->tmp[k], sizeof(m->tmp[k]), FML_DIR_TMP "/ve-%lu-%d.avi", (unsigned long)vedId, k);
  vedSaveMode = parts ? (uint8_t)VS_PARTS : mode;
  vedPlaying = false; vedDrag = VD_NONE; vedExtCancel = false;
  vedPhase = VED_EXPORTING;
  vedExpT0 = millis();
  Serial.printf("[video] exportando id=%lu %dx%d, %d salida(s), %s\n", (unsigned long)vedId, ow, oh, nOut,
                flexVeCopyMode(&m->src, p, m->eOutW, m->eOutH) ? "copia sin recodificar" : "recodificando");
  vedJobStart(VJ_EXPORT);
  vedRender();
}
static void vedAskReplace(){
  int ow = 0, oh = 0; vedResDims(vedRes, &ow, &oh);
  char b[240];
  snprintf(b, sizeof(b), "El v\xC3\xAD" "deo original se sustituir\xC3\xA1 por la versi\xC3\xB3n editada (%d x %d). No se puede deshacer.", ow, oh);
  vedSheet = false;
  vedRender();
  mmDlgOpen("\xC2\xBF" "Reemplazar el original?", b, "Reemplazar", "Cancelar", true);
  vedAsk = VA_REPLACE;
}

// ATRAS: primero las capas propias; luego, salir (preguntando si hay cambios).
static void vedPaintProgress(){
  VedGeom g = vedGeom(); setBuf(fb);
  int x, y, w, h; vedCardGeom(g, x, y, w, h);
  vedDrawProgress(g);
  flxFlush(y - 2, y + h + 2);
}
static bool vedBack(){
  if(!vedActive()) return false;
  if(vedPhase == VED_OPENING || vedPhase == VED_EXPORTING){
    if(!vedLd(&vedS.cancel)){ vedSt(&vedS.cancel, 1); vedPaintProgress(); }
    return true;
  }
  if(vedSheet){ vedSheet = false; vedRender(); return true; }
  vedDrag = VD_NONE; vedViewNoText = false; vedPlaying = false;
  if(vedDirty()){
    mmDlgOpen("\xC2\xBF" "Descartar los cambios?", "Se perder\xC3\xA1n los cambios que no has exportado.", "Descartar", "Seguir editando", true);
    vedAsk = VA_DISCARD;
    return true;
  }
  vedCloseNow();
  vedToGallery();
  return true;
}

static void vedDlgResult(int r){
  uint8_t a = vedAsk; vedAsk = VA_NONE;
  if(a == VA_DISCARD && r == 1){ vedCloseNow(); vedToGallery(); return; }
  if(a == VA_REPLACE && r == 1){ vedStartExport(VS_REPLACE); return; }
  if(a == VA_REPLACE) vedSheet = true;                   // "Cancelar": de vuelta a la hoja
  if(vedActive()) vedRender(); else galRender();
}

// #############################################################
// ##  TOQUES
// #############################################################
// ---- Linea de tiempo: extremos (recortar) y cabezal ----
static bool vedTrimMoved = false;
static bool vedPlayHit(const VedGeom& g, int x, int y){ return abs(x - (g.tlX + 28)) <= 26 && abs(y - (g.tlY + 22)) <= 20; }
static void vedTimelineDrag(const VedGeom& g){
  if(vedDrag == VD_HEAD){
    uint32_t f = vedXFrame(g, T.x);
    if(f == vedHead) return;
    vedHead = f;
  } else {
    if(!vedTrimMoved && abs(T.x - T.startX) < 4) return;   // un toque sobre el asa no recorta nada
    vedTrimMoved = true;
    uint32_t f = vedXFrame(g, T.x);
    FlexVeParams before = vedE->p;
    if(vedDrag == VD_TRIM_A){ flexVeTrimLive(vedE, 0, f); vedHead = flexVeFirstKept(&vedE->p); }
    else { flexVeTrimLive(vedE, 1, f + 1); vedHead = flexVeLastKept(&vedE->p); }
    if(!memcmp(&before.seg, &vedE->p.seg, sizeof(before.seg)) && vedHead == vedBaseFrame) return;
  }
  vedSyncFrame();
  vedTlPending = true;
  if(millis() - vedTlMs >= VED_LIVE_MS) vedPaintTimeline();
}
static bool vedTimelinePress(const VedGeom& g){
  if(vedPlayHit(g, T.x, T.y)) return false;               // el boton va con el toque
  int sy = g.stripY;
  if(T.y < sy - 12 || T.y > sy + VED_STRIP_H + 14) return false;
  if(T.x < g.stripX - 22 || T.x > g.stripX + g.stripW + 22) return false;
  const FlexVeParams* p = &vedE->p;
  int ax = vedFrameX(g, flexVeFirstKept(p)), bx = vedFrameX(g, flexVeLastKept(p) + 1);
  int da = abs(T.x - ax), db = abs(T.x - bx);
  vedPlaying = false;
  vedTrimMoved = false;
  if(da <= 20 || db <= 20) vedDrag = (da < db || (da == db && T.x <= ax)) ? VD_TRIM_A : VD_TRIM_B;
  else vedDrag = VD_HEAD;
  vedTimelineDrag(g);
  return true;
}
static void vedTimelineRelease(){
  uint8_t d = vedDrag; vedDrag = VD_NONE;
  if((d == VD_TRIM_A || d == VD_TRIM_B) && vedTrimMoved){
    vedTrimMoved = false;
    flexVeCommit(vedE);
    vedApplied();
    return;
  }
  vedTrimMoved = false;
  vedSyncFrame();
  vedPaintTimeline();
  vedPaintPanel();                                       // Dividir / Quitar / Portada dependen del cabezal
}

// ---- El video: encuadre y texto ----
static void vedCropRectPx(float a0, float b0, float a1, float b1, int& x0, int& y0, int& x1, int& y1){
  x0 = vedVX + (int)(a0 * vedVW + 0.5f); x1 = vedVX + (int)(a1 * vedVW + 0.5f);
  y0 = vedVY + (int)(b0 * vedVH + 0.5f); y1 = vedVY + (int)(b1 * vedVH + 0.5f);
}
static bool vedWellPress(const VedGeom& g){
  if((!vedViewOk && !vedViewHas) || T.y < g.wellY || T.y >= g.wellY + g.wellH) return false;
  const FlexVeParams* p = &vedE->p;
  if(vedTool == VT_CROP){
    int x0, y0, x1, y1; vedCropRectPx(p->c0x, p->c0y, p->c1x, p->c1y, x0, y0, x1, y1);
    const int cx[4] = { x0, x1, x0, x1 }, cy[4] = { y0, y0, y1, y1 };
    vedCropH = -1;
    for(int k = 0; k < 4 && vedCropH < 0; k++) if(abs(T.x - cx[k]) <= 30 && abs(T.y - cy[k]) <= 30) vedCropH = k;
    if(vedCropH < 0 && p->aspect == FLEXVE_ASP_FREE){      // lados: solo sin proporcion fija
      if(abs(T.y - y0) <= 18 && T.x > x0 && T.x < x1) vedCropH = 4;
      else if(abs(T.y - y1) <= 18 && T.x > x0 && T.x < x1) vedCropH = 5;
      else if(abs(T.x - x0) <= 18 && T.y > y0 && T.y < y1) vedCropH = 6;
      else if(abs(T.x - x1) <= 18 && T.y > y0 && T.y < y1) vedCropH = 7;
    }
    if(vedCropH < 0 && T.x > x0 && T.x < x1 && T.y > y0 && T.y < y1) vedCropH = 8;
    if(vedCropH < 0) return false;
    vedDrag = VD_CROP; vedDX0 = T.x; vedDY0 = T.y;
    vedK0x = vedC0x = p->c0x; vedK0y = vedC0y = p->c0y;
    vedK1x = vedC1x = p->c1x; vedK1y = vedC1y = p->c1y;
    vedPlaying = false;
    return true;
  }
  if(vedTool == VT_TEXT && p->text[0]){
    bool inside = T.x >= vedVX && T.x < vedVX + vedVW && T.y >= vedVY && T.y < vedVY + vedVH;
    // La base tiene que estar libre para rehacer la vista sin el texto.
    if(!inside || vedLd(&vedS.baseOwner) != 0) return false;
    vedDrag = VD_TEXT; vedDX0 = T.x; vedDY0 = T.y; vedK0x = p->textU; vedK0y = p->textV;
    vedPlaying = false;
    vedViewNoText = true; vedViewOk = false;
    vedPaintWell();                                      // una vez sin el texto; luego solo se recompone
    return true;
  }
  return false;
}
// Igual que el editor de fotos: esquinas, lados (sin proporcion fija) o mover.
static void vedCropMove(){
  float dx = (float)(T.x - vedDX0) / vedVW, dy = (float)(T.y - vedDY0) / vedVH;
  float a0 = vedK0x, b0 = vedK0y, a1 = vedK1x, b1 = vedK1y;
  const float mnx = (float)VED_MIN_CROP_PX / vedVW, mny = (float)VED_MIN_CROP_PX / vedVH;
  switch(vedCropH){
    case 0: a0 += dx; b0 += dy; break;
    case 1: a1 += dx; b0 += dy; break;
    case 2: a0 += dx; b1 += dy; break;
    case 3: a1 += dx; b1 += dy; break;
    case 4: b0 += dy; break;
    case 5: b1 += dy; break;
    case 6: a0 += dx; break;
    case 7: a1 += dx; break;
    default: {
      float w = a1 - a0, h = b1 - b0;
      a0 += dx; b0 += dy;
      if(a0 < 0) a0 = 0;
      if(b0 < 0) b0 = 0;
      if(a0 + w > 1) a0 = 1 - w;
      if(b0 + h > 1) b0 = 1 - h;
      a1 = a0 + w; b1 = b0 + h;
      break;
    }
  }
  if(vedCropH < 8){
    if(a0 < 0) a0 = 0;
    if(b0 < 0) b0 = 0;
    if(a1 > 1) a1 = 1;
    if(b1 > 1) b1 = 1;
    if(a1 - a0 < mnx){ if(vedCropH == 0 || vedCropH == 2 || vedCropH == 6) a0 = a1 - mnx; else a1 = a0 + mnx; }
    if(b1 - b0 < mny){ if(vedCropH <= 1 || vedCropH == 4) b0 = b1 - mny; else b1 = b0 + mny; }
    // Proporcion fija: manda el ancho; el alto se ajusta desde la esquina opuesta.
    float r = flexVeAspectRatio(vedE, vedE->p.aspect);
    if(r > 0.0f && vedCropH < 4){
      bool rot = vedE->p.rot & 1;
      float ow = (float)(rot ? vedE->srcH : vedE->srcW), oh = (float)(rot ? vedE->srcW : vedE->srcH);
      float h = (a1 - a0) * ow / (r * oh);
      bool top = vedCropH <= 1;
      if(top){ b0 = b1 - h; if(b0 < 0){ b0 = 0; h = b1; float w = h * r * oh / ow; if(vedCropH == 0) a0 = a1 - w; else a1 = a0 + w; } }
      else   { b1 = b0 + h; if(b1 > 1){ b1 = 1; h = 1 - b0; float w = h * r * oh / ow; if(vedCropH == 2) a0 = a1 - w; else a1 = a0 + w; } }
    }
  }
  vedC0x = a0; vedC0y = b0; vedC1x = a1; vedC1y = b1;
}
static void vedWellDrag(){
  if(vedDrag == VD_CROP) vedCropMove();
  else if(vedDrag == VD_TEXT){
    FlexVeParams* p = &vedE->p;
    p->textU = vedK0x + (float)(T.x - vedDX0) / (vedVW > 0 ? vedVW : 1);
    p->textV = vedK0y + (float)(T.y - vedDY0) / (vedVH > 0 ? vedVH : 1);
    vedClampText();
  } else return;
  if(millis() - vedLiveMs < VED_LIVE_MS){ vedLivePending = true; return; }
  vedLiveMs = millis(); vedLivePending = false;
  vedPaintWell();
}
static void vedWellRelease(){
  uint8_t d = vedDrag; vedDrag = VD_NONE;
  vedLivePending = false;
  if(d == VD_CROP){
    const FlexVeParams* p = &vedE->p;
    if(vedC0x != p->c0x || vedC0y != p->c0y || vedC1x != p->c1x || vedC1y != p->c1y){
      flexVeCropLive(vedE, vedC0x, vedC0y, vedC1x, vedC1y);
      flexVeCommit(vedE);
      vedApplied();
    } else vedPaintWell();
    return;
  }
  if(d == VD_TEXT){
    vedViewNoText = false;
    flexVeCommit(vedE);                                  // solo si se movio de verdad
    vedApplied();
  }
}

// ---- Barra de arriba ----
static void vedBarTap(const VedGeom& g){
  int ux, rx, okx, cy; vedBarBtns(g, ux, rx, okx, cy);
  if(vwHostedNow() && T.x < g.bx + 64){ vedBack(); return; }   // a pantalla completa, el chevron es del sistema
  if(abs(T.y - cy) > 26) return;
  if(abs(T.x - ux) <= 22){ vedUndoRedo(false); return; }
  if(abs(T.x - rx) <= 22){ vedUndoRedo(true); return; }
  if(abs(T.x - okx) <= 28) vedOpenSheet();
}

// ---- Panel de la herramienta ----
static void vedPanelTap(const VedGeom& g){
  FlexVeParams* p = &vedE->p;
  int tx = T.x, ty = T.y;
  switch(vedTool){
    case VT_CUT:
      if(ty >= g.panelY + 28 && ty <= g.panelY + 76){
        int i = vedRowHit(g, 3, tx);
        if(i == 0) vedSplitHere();
        else if(i == 1) vedRemoveHere();
        else if(i == 2 && (p->nSeg != 1 || p->seg[0].a != 0 || p->seg[0].b != vedE->frames)) vedResetTime();
      }
      return;
    case VT_CROP:
      if(ty >= g.panelY + 6 && ty <= g.panelY + 46){
        int i = vedRowHit(g, FLEXVE_ASP_N, tx);
        if(i >= 0 && (i != p->aspect || i != FLEXVE_ASP_FREE)){ flexVeSetAspect(vedE, i); vedApplied(); }
      } else if(ty >= g.panelY + 48 && ty <= g.panelY + 88){
        bool full = p->c0x <= 0.0f && p->c0y <= 0.0f && p->c1x >= 1.0f && p->c1y >= 1.0f;
        if(!full || p->aspect != FLEXVE_ASP_FREE) vedResetCrop();
      }
      return;
    case VT_ROT:
      if(ty >= g.panelY + 6 && ty <= g.panelY + 50){
        int i = vedRowHit(g, 4, tx);
        if(i >= 0 && i != (p->rot & 3)){ flexVeSetRot(vedE, i); vedApplied(); }
      } else if(ty >= g.panelY + 52 && ty <= g.panelY + 96){
        flexVeRotate(vedE, +1); vedApplied();
      }
      return;
    case VT_SPEED:
      if(ty >= g.panelY + 6 && ty <= g.panelY + 48){
        int i = vedRowHit(g, FLEXVE_SPEED_N, tx);
        if(i >= 0 && p->speedPct != FLEXVE_SPEEDS[i]){ p->speedPct = FLEXVE_SPEEDS[i]; flexVeCommit(vedE); vedApplied(); }
      }
      return;
    case VT_VOL: {
      uint8_t k = vedM->src.aud.kind;
      if(k != FLEXVE_AUD_PCM && k != FLEXVE_AUD_IMA) return;   // sin audio, o uno que el P4 no procesa
      if(ty >= g.panelY + 6 && ty <= g.panelY + 48){
        int i = vedRowHit(g, 5, tx);
        if(i >= 0 && !p->mute && p->volPct != VED_VOL[i]){ p->volPct = VED_VOL[i]; flexVeCommit(vedE); vedApplied(); }
      } else if(ty >= g.panelY + 50 && ty <= g.panelY + 90){
        p->mute = p->mute ? 0 : 1; flexVeCommit(vedE); vedApplied();
      }
      return;
    }
    case VT_TEXT: {
      if(ty >= g.panelY + 4 && ty <= g.panelY + 44){
        if(!p->text[0]){ vedAskText(); return; }
        int i = vedRowHit(g, 2, tx);
        if(i == 0) vedAskText();
        else if(i == 1){ p->text[0] = 0; flexVeCommit(vedE); vedApplied(); }
        return;
      }
      int c = vedColorHit(g, tx, ty);
      if(c >= 0){ if(p->textRgb != GED_RGB[c]){ p->textRgb = GED_RGB[c]; vedTextStyle(); } return; }
      int z = vedTrioHit(g, tx, ty);
      if(z >= 0 && p->textSize != z){ p->textSize = (uint8_t)z; vedTextStyle(); }
      return;
    }
    case VT_FILTER:
      for(int f = 0; f < FLEXIE_FILTER_N; f++){
        int x, y, side; vedFilterGeom(g, f, x, y, side);
        if(tx >= x - 3 && tx <= x + side + 3 && ty >= y - 3 && ty <= y + side + 20){
          if(f != p->filter){ p->filter = (uint8_t)f; flexVeCommit(vedE); vedApplied(); }
          return;
        }
      }
      return;
    default:                                              // Portada
      if(ty >= g.panelY + 36 && ty <= g.panelY + 82){
        int i = vedRowHit(g, 2, tx);
        if(i == 0 && flexVeSegAt(p, vedHead) >= 0 && p->cover != vedHead){ p->cover = vedHead; flexVeCommit(vedE); vedApplied(); }
        else if(i == 1 && p->cover != FLEXVE_NO_COVER){ p->cover = FLEXVE_NO_COVER; flexVeCommit(vedE); vedApplied(); }
      }
      return;
  }
}

// ---- Hoja "Exportar" ----
static void vedSheetTouch(const VedGeom& g){
  if(!T.tap) return;
  for(int i = 0; i < VED_RES_N; i++){
    int x, y, w, h; vedResGeom(g, i, x, y, w, h);
    if(T.x >= x - 2 && T.x <= x + w + 2 && T.y >= y - 6 && T.y <= y + h + 6){
      int ow, oh;
      if(i != vedRes && vedResDims(i, &ow, &oh) && vedResOk(i)){ vedRes = i; vedRender(); }
      return;
    }
  }
  int n = vedSheetBtnN();
  for(int i = 0; i < n; i++){
    int x, y, w, h; vedSheetBtn(g, i, x, y, w, h);
    if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + h){
      if(i == n - 1){ vedSheet = false; vedRender(); return; }   // Cancelar
      if(!vedResOk(vedRes)) return;                     // deshabilitado: se dice en la propia hoja
      if(i == 0) vedStartExport(VS_COPY);
      else if(i == 1) vedAskReplace();
      else vedStartExport(VS_PARTS);
      return;
    }
  }
  int x, y, w, h; vedSheetGeom(g, x, y, w, h);
  if(T.y < y){ vedSheet = false; vedRender(); }         // fuera de la hoja: se cierra
}

// #############################################################
// ##  TICK (Galeria en primer plano)
// #############################################################
static void vedTick(){
  vedTickMs = millis() | 1u;
  vedHoldEnd();
  bool got = vedCollect();
  vedValidate();                                         // en la MISMA vuelta: si el video ya no esta, se cierra ya
  if(!vedActive()) return;
  if(got){ if(!mmDlgOn && !fkNameOn) vedRender(); return; }
  vedApplyPath();
  // Teclado del texto.
  if(fkNameOn && vedTextAsk){
    int r = fkNameTick();
    if(r == 1 && vedPhase == VED_EDIT) vedSetText(fkNameBuf);
    if(r != 0){ vedTextAsk = false; mkRedrawAll(); }
    return;
  }
  if(mmDlgOn){ int r = mmDlgTick(); if(r) vedDlgResult(r); return; }
  VedGeom g = vedGeom();
  if(vedPhase == VED_OPENING || vedPhase == VED_EXPORTING){
    if(T.tap && !vedLd(&vedS.cancel) && vedProgressCancelHit(g, T.x, T.y)){ vedSt(&vedS.cancel, 1); vedPaintProgress(); return; }
    int pct = __atomic_load_n(&vedS.pct, __ATOMIC_ACQUIRE);
    if(pct != vedShownPct && millis() - vedShownMs >= 100u) vedPaintProgress();
    return;
  }
  // ---- Lo que devolvio el trabajador ----
  if(vedTakeFrame()){
    vedPaintWell();
    if(vedTool == VT_FILTER) vedPaintPanel();            // las miniaturas de los filtros salen del fotograma
  }
  if(vedDrag != VD_TEXT && vedDrag != VD_CROP) vedSyncFrame();   // el cabezal que se ve es el ultimo pedido
  int td = __atomic_load_n(&vedS.thumbsDone, __ATOMIC_ACQUIRE);
  if(td != vedThSeen && (td >= VED_THUMBS || millis() - vedTlMs >= 150u)) vedPaintTimeline();
  else if(vedTlPending && millis() - vedTlMs >= VED_LIVE_MS) vedPaintTimeline();
  if(vedLivePending && millis() - vedLiveMs >= VED_LIVE_MS){ vedLiveMs = millis(); vedLivePending = false; vedPaintWell(); }
  vedPlayTick();
  if(vedSheet){ vedSheetTouch(g); return; }

  // ---- Arrastres ----
  if(T.pressed && vedDrag == VD_NONE){
    if(T.y >= g.tlY && T.y < g.tlY + g.tlH) vedTimelinePress(g);
    else if(T.y >= g.wellY && T.y < g.wellY + g.wellH) vedWellPress(g);
  }
  if(vedDrag != VD_NONE){
    bool tl = vedDrag == VD_HEAD || vedDrag == VD_TRIM_A || vedDrag == VD_TRIM_B;
    if(T.down){ if(tl) vedTimelineDrag(g); else vedWellDrag(); return; }
    if(tl) vedTimelineRelease(); else vedWellRelease();
    return;
  }
  if(!T.tap) return;
  // ---- Toques ----
  if(T.y >= g.barY && T.y < g.barY + g.barH){ vedBarTap(g); return; }
  if(T.y >= g.tabsY){
    int tw = g.bw / VT_N;
    vedSetTool(tw > 0 ? (T.x - g.bx) / tw : -1);
    return;
  }
  if(T.y >= g.panelY){ vedPanelTap(g); return; }
  if(T.y >= g.tlY){ if(vedPlayHit(g, T.x, T.y)) vedPlayToggle(); return; }
  if(T.y >= g.wellY && vedTool != VT_CROP) vedPlayToggle();   // tocar el video: reproducir / pausa
}
