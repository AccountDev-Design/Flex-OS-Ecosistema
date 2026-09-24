// #############################################################
// ##  FLEX OS ULTRA  ·  GALERIA  ·  EDITOR DE IMAGENES
// ##  ----------------------------------------------------------
// ##  Recortar, girar, voltear, ajustes de color, filtros, dibujo,
// ##  formas y texto sobre una foto de la biblioteca, con deshacer y
// ##  rehacer, y "Guardar como copia" o "Reemplazar original".
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
#include "FlexOS_Ultra_WebServer.h"   // eslabon anterior de la cadena
#include "FlexOS_ImgEdit.h"

// #############################################################
// ##  QUE HACE Y QUIEN HACE CADA COSA
// ##  ------------------------------------------------------
// ##  · LO QUE SALE lo decide FlexOS_ImgEdit (portable, probado en el
// ##    PC): edicion NO destructiva. La foto abierta (la base, RGB888
// ##    en PSRAM) no se toca; se guarda un estado y su historial, y la
// ##    imagen editada se calcula por filas a cualquier tamano.
// ##  · ABRIR y GUARDAR corren en un trabajador de UN SOLO USO
// ##    ("flexEdit"), como el NTP o el escaneo Wi-Fi: leer y decodificar
// ##    una foto de varios megas, o codificar la editada, lleva segundos
// ##    y la interfaz no puede quedarse parada. Reglas del trabajador:
// ##      - mientras trabaja, la interfaz NO toca el estado del editor
// ##        (la pantalla solo ensena el progreso y el boton Cancelar);
// ##      - el trabajador NO pinta, NO avisa por la isla y NO cambia el
// ##        catalogo: deja el resultado y lo publica con una barrera
// ##        (__atomic release/acquire);
// ##      - cancelar es una bandera que mira entre filas; al cerrar se le
// ##        espera (acotado) antes de soltar la memoria que usa.
// ##  · PUBLICAR el resultado (mlReplaceFile / mlAddFile) va SIEMPRE en
// ##    loopTask (gedBgTick, desde loop()), tambien si la Galeria esta en
// ##    segundo plano: el catalogo, la cache de miniaturas y el reproductor
// ##    de Musica son de ese hilo.
// ##
// ##  GUARDAR SIN PERDER NADA
// ##  ------------------------------------------------------
// ##  La foto editada se escribe en un temporal de /System/Media/tmp, se
// ##  COMPRUEBA (tamano en disco, cabecera JPEG con las medidas esperadas y
// ##  marca de fin) y solo entonces se publica. "Reemplazar" aparta el
// ##  original junto a si mismo, pone el nuevo y solo despues borra el
// ##  apartado (FlexOS_MediaStore; un corte de luz a mitad lo resuelve el
// ##  recorrido del arranque). "Copia" crea un elemento nuevo con su id,
// ##  un nombre libre y su miniatura.
// ##
// ##  PROTEGIDOS
// ##  ------------------------------------------------------
// ##  Lo bloqueado no se edita (el menu ni lo ofrece). Y si la foto se
// ##  bloquea o se borra mientras el editor esta abierto (desde Multimedia,
// ##  por ejemplo), el editor se cierra y suelta sus pixeles en cuanto el
// ##  catalogo cambia: una copia de algo protegido no puede salir sin
// ##  proteger por esta puerta.
// ##
// ##  LIMITE REAL DEL P4
// ##  ------------------------------------------------------
// ##  La base tiene que caber en PSRAM: el editor trabaja como mucho a
// ##  FLEXIE_BASE_MAX_PX (~3,1 MP; una foto de 12 MP se abre a la mitad de
// ##  lado, lo hace el propio decodificador) y la hoja de guardar dice a
// ##  que resolucion sale. Solo JPEG baseline: es lo que el P4 decodifica.
// #############################################################

#define GED_QUALITY      92                     // JPEG de salida (lo normal en un movil)
#define GED_PROXY_LONG   720                    // lado largo de la copia reducida (vista previa)
#define GED_TASK_STACK   8192
#define GED_PSRAM_MARGIN (2u * 1024u * 1024u)   // ademas de la reserva del sistema
#define GED_BAND         16                     // filas por banda al pintar
#define GED_BAND_W       800                    // ancho maximo de la vista previa
#define GED_LIVE_MS      40                     // en vivo: como mucho un repintado cada tanto
#define GED_TOP_H        54
#define GED_TABS_H       62
#define GED_PANEL_H      128
#define GED_WAIT_TRIES   600                    // x 5 ms: lo que se espera al trabajador al cerrar
#define GED_MIN_CROP_PX  24                     // un recorte no baja de esto en pantalla

enum { GED_OFF = 0, GED_OPENING, GED_EDIT, GED_SAVING };
enum { GT_CROP = 0, GT_ADJ, GT_FILTER, GT_DRAW, GT_TEXT, GT_SIZE, GT_N };
enum { GJ_NONE = 0, GJ_OPEN, GJ_SAVE };
enum { GS_NONE = 0, GS_COPY, GS_REPLACE };
enum { GA_NONE = 0, GA_DISCARD, GA_REPLACE, GA_FAIL_CLOSE, GA_INFO };
enum { GD_NONE = 0, GD_SLIDER, GD_STROKE, GD_SHAPE, GD_CROP, GD_TEXT };

static const char* const GED_TOOL[GT_N] = { "Recortar", "Ajustes", "Filtros", "Dibujo", "Texto", "Tama\xC3\xB1o" };
static const char* const GED_ADJ[FLEXIE_ADJ_N] = {
  "Brillo", "Contraste", "Saturaci\xC3\xB3n", "Exposici\xC3\xB3n", "Temperatura", "Sombras", "Luces", "Nitidez" };
static const char* const GED_FILTER[FLEXIE_FILTER_N] = {
  "Original", "B/N", "Sepia", "V\xC3\xADvido", "Fr\xC3\xAD" "o", "C\xC3\xA1lido", "Vintage" };
#define GED_KINDS 5
static const char* const GED_KIND[GED_KINDS] = { "Pincel", "L\xC3\xADnea", "Flecha", "Rect.", "Elipse" };
static const uint8_t GED_KIND_OV[GED_KINDS] = { FLEXIE_OV_STROKE, FLEXIE_OV_LINE, FLEXIE_OV_ARROW, FLEXIE_OV_RECT, FLEXIE_OV_ELLIPSE };
#define GED_COLORS 8
static const uint32_t GED_RGB[GED_COLORS] = { 0xFFFFFF, 0x111111, 0xE53935, 0xFB8C00, 0xFDD835, 0x43A047, 0x1E88E5, 0x8E24AA };
static const float GED_WIDTH[3] = { 0.006f, 0.012f, 0.024f };          // fraccion del lado largo de la salida
static const float GED_TEXT_H[3] = { 0.05f, 0.08f, 0.12f };
static const char* const GED_TEXT_SZ[3] = { "Peque\xC3\xB1o", "Mediano", "Grande" };
#define GED_ASPECTS 6
static const char* const GED_ASPECT[GED_ASPECTS] = { "Libre", "1:1", "4:3", "3:4", "16:9", "Original" };
static const float GED_ASPECT_R[GED_ASPECTS] = { 0.0f, 1.0f, 4.0f / 3.0f, 3.0f / 4.0f, 16.0f / 9.0f, -1.0f };
#define GED_SIZES 4
static const char* const GED_SIZE[GED_SIZES] = { "Original", "75 %", "50 %", "25 %" };
static const uint8_t GED_SIZE_PCT[GED_SIZES] = { 100, 75, 50, 25 };

// Colores propios del editor: la foto se mira sobre un fondo neutro y
// oscuro, sea cual sea el tema (como en el editor de un movil).
#define GED_BG      rgb565(10, 11, 14)
#define GED_PANEL   rgb565(28, 30, 38)
#define GED_GLASS   rgb565(44, 48, 62)
#define GED_CHIP    rgb565(48, 52, 66)
#define GED_TXT     rgb565(240, 242, 248)
#define GED_TXT2    rgb565(168, 174, 190)
#define GED_DIS     rgb565(92, 96, 110)

// ---- Estado ----
static uint8_t      gedPhase = GED_OFF;
static uint8_t      gedTool = GT_CROP;
static uint32_t     gedId = 0;
static char         gedPath[FML_PATH_MAX] = "";
static char         gedName[FML_NAME_MAX] = "";
static FlexImgEdit* gedE = NULL;                  // estado + historial (PSRAM)
static uint8_t*     gedBase = NULL;  static int gedW = 0, gedH = 0;
static uint8_t*     gedProxy = NULL; static int gedPW = 0, gedPH = 0;
static int          gedSrcW = 0, gedSrcH = 0;     // medidas del ARCHIVO (antes de reducir)
static uint8_t*     gedBand = NULL;               // banda RGB888 de la vista previa
static uint8_t*     gedCov = NULL;                // coberturas de trazos y textos de esa banda
static uint16_t*    gedView = NULL;               // vista previa ya pintada (RGB565)
static size_t       gedViewCap = 0;
static int          gedVX = 0, gedVY = 0, gedVW = 0, gedVH = 0;   // donde esta en pantalla
static bool         gedViewOk = false;
static bool         gedReopen = false;            // la base se solto por memoria: se relee al volver
static bool         gedLeak = false;              // el trabajador no termino: su memoria no se toca
static uint32_t     gedSeenRev = 0;
static uint8_t      gedSaveMode = GS_NONE;
static uint8_t      gedAsk = GA_NONE;
static bool         gedSheet = false;             // hoja "Guardar"
static int          gedShownPct = -1;
static uint32_t     gedShownMs = 0;
// Herramientas
static uint8_t      gedAdj = FLEXIE_ADJ_BRIGHT;
static uint8_t      gedKind = 0, gedColor = 2, gedWidth = 1, gedTextSz = 1, gedAspect = 0;
static bool         gedFill = false;
static uint16_t*    gedFilterThumb = NULL;        // 7 miniaturas de filtro (RGB565)
static int          gedFtSide = 0;
static bool         gedFtOk = false;
// Interaccion en curso
static uint8_t      gedDrag = GD_NONE;
static int          gedStrokeOv = -1;
static int          gedPrevX = 0, gedPrevY = 0;
static int          gedCropH = -1;                // asa del recorte: 0..3 esquinas, 4..7 lados, 8 mover
static float        gedC0x, gedC0y, gedC1x, gedC1y;   // recorte (o forma) en curso
static float        gedK0x, gedK0y, gedK1x, gedK1y;   // recorte al empezar a arrastrar
static int          gedDX0 = 0, gedDY0 = 0;
static uint32_t     gedLiveMs = 0;
static bool         gedLivePending = false;
// Texto a medio colocar
static bool         gedTextOn = false;
static char         gedText[FLEXIE_TEXT_MAX] = "";
static float        gedTextU = 0.1f, gedTextV = 0.4f;
static bool         gedTextAsk = false;           // el teclado es del editor (no del kit)

// ---- El trabajo en segundo plano ----
struct GedJob {
  uint8_t          kind;
  volatile uint8_t cancel;
  int              done;                          // se publica con __atomic (release / acquire)
  volatile int     pct;                           // 0..100, progreso REAL
  int              rc;                            // 0 bien, 1 cancelado, <0 error
  char             why[120];
  char             path[FML_PATH_MAX];            // abrir: la foto; guardar: el temporal
  uint8_t*         base; int W, H, srcW, srcH;    // abrir
  uint8_t*         proxy; int PW, PH;
  uint32_t         bytes; int outW, outH;         // guardar
};
static GedJob gedJob;
static bool   gedJobOn = false;                   // lanzado y aun sin recoger

static bool gedActive(){ return gedPhase != GED_OFF; }
static bool gedJobDone(){ return __atomic_load_n(&gedJob.done, __ATOMIC_ACQUIRE) != 0; }

// #############################################################
// ##  EL TRABAJADOR (tarea "flexEdit")
// #############################################################
static void gedYield(uint32_t* last){
  if(millis() - *last >= 20){ vTaskDelay(1); *last = millis(); }   // la interfaz y el IDLE respiran
}
static void gedJobFail(GedJob* j, const char* why){
  snprintf(j->why, sizeof(j->why), "%s", why ? why : "Error");
  j->rc = -1;
}

struct GedRd { GedJob* j; FlexJpegInfo* inf; uint8_t* dst; uint32_t last; };
static bool gedRowCb(void* u, int y, int w, const uint8_t* rgb){
  GedRd* c = (GedRd*)u;
  if(c->j->cancel) return false;
  int W = c->inf->outWidth;
  if(w > W) w = W;
  memcpy(c->dst + (size_t)y * W * 3, rgb, (size_t)w * 3);
  int oh = c->inf->outHeight > 0 ? c->inf->outHeight : 1;
  c->j->pct = 15 + (int)(75LL * (y + 1) / oh);
  gedYield(&c->last);
  return true;
}

static void gedDoOpen(GedJob* j){
  j->pct = 0;
  uint32_t sz = flexFsSize(j->path);
  if(!sz){ gedJobFail(j, "La foto ya no est\xC3\xA1 en el almacenamiento"); return; }
  if(sz > FML_LIMIT_PHOTO){
    char m[96];
    snprintf(m, sizeof(m), "Foto de %u MB: el editor abre hasta %u MB",
             (unsigned)((sz + 1048575u) / 1048576u), (unsigned)(FML_LIMIT_PHOTO / 1048576u));
    gedJobFail(j, m); return;
  }
  if(memFreePsram() < sz + FLEXMEM_RESERVE_BYTES + GED_PSRAM_MARGIN){ gedJobFail(j, "No hay memoria libre para abrir esta foto"); return; }
  uint8_t* file = (uint8_t*)mediaAlloc(sz);
  if(!file){ gedJobFail(j, "No hay memoria libre para abrir esta foto"); return; }
  FlexFsStream* f = flexFsOpenRead(j->path);
  uint32_t got = 0, last = millis();
  while(f && got < sz && !j->cancel){
    uint32_t want = sz - got > 16384u ? 16384u : sz - got;
    int k = flexFsStreamRead(f, file + got, want);
    if(k <= 0) break;
    got += (uint32_t)k;
    j->pct = (int)(15ULL * got / sz);
    gedYield(&last);
  }
  flexFsStreamClose(f);
  if(j->cancel){ mediaFree(file); j->rc = 1; return; }
  if(got != sz){ mediaFree(file); gedJobFail(j, "No se pudo leer la foto"); return; }
  FlexJpegInfo inf; memset(&inf, 0, sizeof(inf));
  if(flexJpegProbe(file, sz, &inf) != FLEXJPG_OK){
    mediaFree(file);
    gedJobFail(j, inf.progressive ? "JPEG progresivo: el P4 no lo puede editar" : "No es un JPEG que se pueda editar");
    return;
  }
  j->srcW = inf.width; j->srcH = inf.height;
  // La base: lo libre menos la reserva y la copia reducida, y en UN bloque.
  uint32_t fr = memFreePsram();
  uint32_t keep = FLEXMEM_RESERVE_BYTES + GED_PSRAM_MARGIN + (uint32_t)GED_PROXY_LONG * GED_PROXY_LONG * 3u;
  uint32_t budget = fr > keep ? fr - keep : 0;
  uint32_t big = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if(big < budget) budget = big;
  int s = flexIeDecodeScale(inf.width, inf.height, budget);
  if(!s){ mediaFree(file); gedJobFail(j, "No hay memoria para editar una foto tan grande"); return; }
  int mw = (inf.width + s - 1) / s, mh = (inf.height + s - 1) / s;
  uint8_t* base = (uint8_t*)mediaAlloc((size_t)mw * mh * 3);
  if(!base){ mediaFree(file); gedJobFail(j, "No hay memoria para editar esta foto"); return; }
  GedRd rd = { j, &inf, base, (uint32_t)millis() };
  int rc = flexJpegDecode888(file, sz, mw, mh, 0, &inf, gedRowCb, &rd, mediaAlloc, mediaFree);
  mediaFree(file);
  if(rc != FLEXJPG_OK){
    mediaFree(base);
    if(j->cancel){ j->rc = 1; return; }
    gedJobFail(j, flexJpegErrStr(rc)); return;
  }
  j->base = base; j->W = inf.outWidth; j->H = inf.outHeight;
  // Copia reducida para la vista previa (sin ella se pinta desde la base).
  int L = j->W > j->H ? j->W : j->H;
  if(L > GED_PROXY_LONG){
    int pw = (int)((int64_t)j->W * GED_PROXY_LONG / L), ph = (int)((int64_t)j->H * GED_PROXY_LONG / L);
    if(pw < 1) pw = 1;
    if(ph < 1) ph = 1;
    uint8_t* px = (uint8_t*)mediaAlloc((size_t)pw * ph * 3);
    if(px){ flexIeMakeProxy(base, j->W, j->H, px, pw, ph); j->proxy = px; j->PW = pw; j->PH = ph; }
  }
  j->pct = 100; j->rc = 0;
}

struct GedWr { GedJob* j; FlexFsStream* f; uint32_t last; };
static bool gedOutCb(void* u, const uint8_t* d, size_t n){
  GedWr* w = (GedWr*)u;
  if(!flexFsStreamWrite(w->f, d, n)) return false;
  w->j->bytes += (uint32_t)n;
  return true;
}
static bool gedStopCb(void* u){ GedWr* w = (GedWr*)u; gedYield(&w->last); return w->j->cancel != 0; }
static void gedProgCb(void* u, int done, int total){ GedWr* w = (GedWr*)u; w->j->pct = total > 0 ? (int)(95LL * done / total) : 0; }

// Lo escrito se vuelve a leer: tamano, cabecera con las medidas esperadas
// y marca de fin. Un temporal que no pasa esto no se publica nunca.
static bool gedVerify(GedJob* j){
  if(j->bytes < 64 || flexFsSize(j->path) != j->bytes) return false;
  uint8_t* head = (uint8_t*)mediaAlloc(4096);
  if(!head) return false;
  bool ok = false;
  FlexFsStream* f = flexFsOpenRead(j->path);
  if(f){
    int n = flexFsStreamRead(f, head, j->bytes < 4096u ? j->bytes : 4096u);
    FlexJpegInfo inf; memset(&inf, 0, sizeof(inf));
    uint8_t tail[2] = { 0, 0 };
    if(n > 0 && flexJpegProbe(head, (size_t)n, &inf) == FLEXJPG_OK && inf.width == j->outW && inf.height == j->outH &&
       flexFsStreamSeek(f, j->bytes - 2) && flexFsStreamRead(f, tail, 2) == 2 && tail[0] == 0xFF && tail[1] == 0xD9) ok = true;
    flexFsStreamClose(f);
  }
  mediaFree(head);
  return ok;
}

static void gedDoSave(GedJob* j){
  flexFsDelete(j->path);
  FlexFsStream* f = flexFsOpenWrite(j->path);
  if(!f){ gedJobFail(j, "No se pudo crear el archivo temporal"); return; }
  GedWr w = { j, f, (uint32_t)millis() };
  int rc = flexIeSave(gedE, GED_QUALITY, gedOutCb, &w, gedStopCb, gedProgCb, &w, mediaAlloc, mediaFree);
  flexFsStreamClose(f);
  if(rc != FLEXJE_OK){
    flexFsDelete(j->path);
    if(j->cancel){ j->rc = 1; return; }
    gedJobFail(j, rc == FLEXJE_ERR_WRITE ? "No se pudo escribir: \xC2\xBF" "almacenamiento lleno?"
                : rc == FLEXJE_ERR_MEMORY ? "No hay memoria para guardar" : "No se pudo guardar la foto");
    return;
  }
  if(!gedVerify(j)){
    flexFsDelete(j->path);
    gedJobFail(j, "La foto guardada no pasa la comprobaci\xC3\xB3n: no se ha publicado");
    return;
  }
  j->pct = 100; j->rc = 0;
}

static void gedWorker(void*){
  GedJob* j = &gedJob;
  if(j->kind == GJ_OPEN) gedDoOpen(j);
  else if(j->kind == GJ_SAVE) gedDoSave(j);
  __atomic_store_n(&j->done, 1, __ATOMIC_RELEASE);
  vTaskDelete(NULL);
}

// Lanza el trabajo ya preparado en gedJob (path, outW/outH).
static void gedJobStart(uint8_t kind){
  gedJob.kind = kind; gedJob.cancel = 0; gedJob.pct = 0; gedJob.rc = 0; gedJob.why[0] = 0;
  gedJob.base = NULL; gedJob.proxy = NULL; gedJob.W = gedJob.H = gedJob.PW = gedJob.PH = 0; gedJob.bytes = 0;
  __atomic_store_n(&gedJob.done, 0, __ATOMIC_RELEASE);
  gedJobOn = true;
  gedShownPct = -1;
  if(xTaskCreatePinnedToCore(gedWorker, "flexEdit", GED_TASK_STACK, NULL, 1, NULL, 1) != pdPASS){
    snprintf(gedJob.why, sizeof(gedJob.why), "No se pudo iniciar el trabajo en segundo plano");
    gedJob.rc = -1;
    __atomic_store_n(&gedJob.done, 1, __ATOMIC_RELEASE);
  }
}

// Cancela y ESPERA (acotado) al trabajador. false = no termino: lo que usa
// no se puede soltar.
static bool gedJobStopWait(){
  if(!gedJobOn) return true;
  gedJob.cancel = 1;
  for(int i = 0; i < GED_WAIT_TRIES && !gedJobDone(); i++) vTaskDelay(pdMS_TO_TICKS(5));
  if(!gedJobDone()){
    Serial.println(F("[editor] el trabajador no termino a tiempo: su memoria se queda reservada"));
    gedLeak = true;
    return false;
  }
  // Lo que dejo a medias (una base recien abierta, un temporal) se recoge.
  if(gedJob.kind == GJ_OPEN && gedJob.rc == 0){ mediaFree(gedJob.base); mediaFree(gedJob.proxy); }
  if(gedJob.kind == GJ_SAVE && gedJob.rc == 0) flexFsDelete(gedJob.path);
  gedJobOn = false;
  return true;
}

// #############################################################
// ##  TEXTO CON LA FUENTE DEL SISTEMA
// ##  Cobertura por filas del texto a `hpx` de alto de linea, leyendo
// ##  las MISMAS tablas que drawText (Outfit 4bpp). Solo lee constantes:
// ##  la usa tambien el trabajador al guardar.
// #############################################################
static int gedTextFn(void*, const char* s, int hpx, int row, int col0, uint8_t* cov, int covW){
  if(!s || hpx <= 0) return 0;
  float sc = (float)hpx / (float)FONT_LINEH;
  if(row < 0){
    float w = 0; const char* p = s;
    while(*p){ uint32_t cp = nextCP(&p); w += FG[fontIdx(cp)].adv * sc; }
    return (int)(w + 0.5f);
  }
  memset(cov, 0, (size_t)covW);
  float pen = 0, fy = row / sc;
  const char* p = s;
  while(*p){
    uint32_t cp = nextCP(&p);
    const FGlyph* g = &FG[fontIdx(cp)];
    float my = fy - g->topoff;
    if(g->w && my > -1.0f && my < g->h){
      float gx0 = pen + g->bx * sc;
      int cA = (int)floorf(gx0) - col0, cB = (int)ceilf(gx0 + g->w * sc) - col0;
      if(cA < 0) cA = 0;
      if(cB > covW - 1) cB = covW - 1;
      int y0 = (int)floorf(my); float dy = my - y0;
      for(int c = cA; c <= cB; c++){
        float mx = (col0 + c + 0.5f - gx0) / sc - 0.5f;
        int x0 = (int)floorf(mx); float dx = mx - x0;
        float a00 = fgPix(g, x0, y0), a10 = fgPix(g, x0 + 1, y0), a01 = fgPix(g, x0, y0 + 1), a11 = fgPix(g, x0 + 1, y0 + 1);
        float v = ((a00 * (1 - dx) + a10 * dx) * (1 - dy) + (a01 * (1 - dx) + a11 * dx) * dy) * 17.0f;
        int k = (int)(v + 0.5f);
        if(k > 255) k = 255;
        if(k > cov[c]) cov[c] = (uint8_t)k;
      }
    }
    pen += g->adv * sc;
  }
  return (int)(pen + 0.5f);
}

// #############################################################
// ##  GEOMETRIA DE LA PANTALLA
// ##  Una sola fuente para dibujo y toque (como el visor): no pueden
// ##  descolocarse entre si.
// #############################################################
struct GedGeom { int bx, by, bw, bh, pad, topY, wellX, wellY, wellW, wellH, panelX, panelY, panelW, tabsY; };
static GedGeom gedGeom(){
  GedGeom g;
  uiBox(g.bx, g.by, g.bw, g.bh);
  g.pad = uiPad();
  g.topY = g.by;
  g.tabsY = g.by + g.bh - GED_TABS_H;
  g.panelX = g.bx + g.pad / 2; g.panelW = g.bw - g.pad;
  g.panelY = g.tabsY - GED_PANEL_H;
  g.wellX = g.bx; g.wellW = g.bw;
  g.wellY = g.topY + GED_TOP_H;
  g.wellH = g.panelY - 4 - g.wellY;
  if(g.wellH < 60) g.wellH = 60;
  return g;
}
// Lo que se ve: la salida, o en Recortar la foto girada ENTERA (el recorte
// se dibuja encima).
static bool gedCropView(){ return gedTool == GT_CROP && gedPhase == GED_EDIT; }
static void gedViewDims(int* w, int* h){
  if(!gedE || !gedE->W){ *w = 4; *h = 3; return; }
  if(gedCropView()){
    if(gedE->st.rot & 1){ *w = gedE->H; *h = gedE->W; } else { *w = gedE->W; *h = gedE->H; }
  } else flexIeOutSize(gedE, w, h);
}
static void gedImgRect(const GedGeom& g, int& x, int& y, int& w, int& h){
  int vw, vh; gedViewDims(&vw, &vh);
  int mw = g.wellW - 2 * g.pad, mh = g.wellH - 16;
  if(mw < 16) mw = 16;
  if(mh < 16) mh = 16;
  if(mw > GED_BAND_W) mw = GED_BAND_W;
  float k = (float)mw / vw;
  if(vh * k > mh) k = (float)mh / vh;
  w = (int)(vw * k + 0.5f); h = (int)(vh * k + 0.5f);
  if(w < 1) w = 1;
  if(h < 1) h = 1;
  x = g.wellX + (g.wellW - w) / 2; y = g.wellY + (g.wellH - h) / 2;
}

// #############################################################
// ##  VISTA PREVIA
// ##  Se CALCULA una vez por cambio (desde la copia reducida, por bandas)
// ##  y se guarda en RGB565: arrastrar un recorte, dibujar o colocar un
// ##  texto solo recompone encima, sin volver a calcular la foto.
// #############################################################
static void gedBlit565(const uint16_t* src, int x, int y, int w, int h){
  for(int r = 0; r < h; r++){
    int dy = y + r;
    if(dy < gClipY0 || dy > gClipY1) continue;
    const uint16_t* row = src + (size_t)r * w;
    if(gLand){ for(int i = 0; i < w; i++) px(x + i, dy, row[i]); continue; }
    if(dy < 0 || dy >= SCR_H) continue;
    int dx = x, n = w;
    if(dx < gClipX0){ row += gClipX0 - dx; n -= gClipX0 - dx; dx = gClipX0; }
    if(dx + n > gClipX1 + 1) n = gClipX1 + 1 - dx;
    if(dx < 0){ row -= dx; n += dx; dx = 0; }
    if(dx + n > SCR_W) n = SCR_W - dx;
    if(n > 0) memcpy(gBuf + (size_t)dy * SCR_W + dx, row, (size_t)n * 2);
  }
}

static void gedViewBuild(){
  gedViewOk = false;
  if(!gedE || !gedE->base || !gedBand || !gedCov) return;
  GedGeom g = gedGeom();
  int x, y, w, h; gedImgRect(g, x, y, w, h);
  size_t need = (size_t)w * h * 2;
  if(need > gedViewCap){
    mediaFree(gedView); gedView = (uint16_t*)mediaAlloc(need);
    gedViewCap = gedView ? need : 0;
  }
  if(!gedView) return;
  FlexIeState keep = gedE->st;
  if(gedCropView()){                              // la foto entera, sin recorte ni redimension
    gedE->st.c0x = gedE->st.c0y = 0.0f; gedE->st.c1x = gedE->st.c1y = 1.0f; gedE->st.longSide = 0;
  }
  for(int y0 = 0; y0 < h; y0 += GED_BAND){
    int rows = h - y0 < GED_BAND ? h - y0 : GED_BAND;
    flexIeRenderRows(gedE, w, h, y0, rows, gedBand, gedCov);
    for(int r = 0; r < rows; r++){
      const uint8_t* p = gedBand + (size_t)r * w * 3;
      uint16_t* d = gedView + (size_t)(y0 + r) * w;
      for(int i = 0; i < w; i++, p += 3) d[i] = rgb565(p[0], p[1], p[2]);
    }
  }
  gedE->st = keep;
  gedVX = x; gedVY = y; gedVW = w; gedVH = h;
  gedViewOk = true;
}
static void gedChanged(){ gedViewOk = false; gedFtOk = false; }

// ---- Iconos (vectoriales, como el resto del sistema) ----
static void gedIcoArc(int cx, int cy, int r, float a0, float a1, uint16_t col, bool headAtEnd){
  float px0 = cx + r * cosf(a0), py0 = cy - r * sinf(a0);
  for(int k = 1; k <= 10; k++){
    float a = a0 + (a1 - a0) * k / 10.0f;
    float x1 = cx + r * cosf(a), y1 = cy - r * sinf(a);
    strokeSegAA(px0, py0, x1, y1, 1.3f, col);
    px0 = x1; py0 = y1;
  }
  if(headAtEnd){
    // Punta tangente al arco en su extremo final.
    float tx = -sinf(a1) * (a1 > a0 ? 1 : -1), ty = -cosf(a1) * (a1 > a0 ? 1 : -1);
    float ex = cx + r * cosf(a1), ey = cy - r * sinf(a1);
    strokeSegAA(ex, ey, ex - 6 * tx + 4 * ty, ey - 6 * ty - 4 * tx, 1.3f, col);
    strokeSegAA(ex, ey, ex - 6 * tx - 4 * ty, ey - 6 * ty + 4 * tx, 1.3f, col);
  }
}
static void gedIcoUndo(int cx, int cy, uint16_t col, bool redo){
  if(!redo) gedIcoArc(cx, cy + 2, 8, 0.2f, 3.14159f, col, true);
  else      gedIcoArc(cx, cy + 2, 8, 2.94f, 0.0f, col, true);
}
static void gedIcoRotate(int cx, int cy, uint16_t col, bool right){
  if(!right) gedIcoArc(cx, cy, 9, -0.9f, 3.6f, col, true);
  else       gedIcoArc(cx, cy, 9, 4.04f, -0.46f, col, true);
}
static void gedIcoFlip(int cx, int cy, uint16_t col, bool horizontal){
  if(horizontal){
    fillTriangle(cx - 3, cy - 8, cx - 3, cy + 8, cx - 12, cy + 8, col);
    strokeSegAA(cx + 3, cy - 8, cx + 3, cy + 8, 1.1f, col); strokeSegAA(cx + 3, cy + 8, cx + 12, cy + 8, 1.1f, col);
    strokeSegAA(cx + 3, cy - 8, cx + 12, cy + 8, 1.1f, col);
    for(int y = cy - 10; y < cy + 10; y += 4) vLine(cx, y, 2, col);
  } else {
    fillTriangle(cx - 8, cy - 3, cx + 8, cy - 3, cx + 8, cy - 12, col);
    strokeSegAA(cx - 8, cy + 3, cx + 8, cy + 3, 1.1f, col); strokeSegAA(cx + 8, cy + 3, cx + 8, cy + 12, 1.1f, col);
    strokeSegAA(cx - 8, cy + 3, cx + 8, cy + 12, 1.1f, col);
    for(int x = cx - 10; x < cx + 10; x += 4) hLine(x, cy, 2, col);
  }
}
static void gedIcoTool(int t, int cx, int cy, uint16_t col){
  switch(t){
    case GT_CROP:
      strokeSegAA(cx - 6, cy - 11, cx - 6, cy + 6, 1.5f, col); strokeSegAA(cx - 6, cy + 6, cx + 11, cy + 6, 1.5f, col);
      strokeSegAA(cx + 6, cy + 11, cx + 6, cy - 6, 1.5f, col); strokeSegAA(cx + 6, cy - 6, cx - 11, cy - 6, 1.5f, col);
      break;
    case GT_ADJ: mmGlyph(MA_EDIT, cx, cy, col); break;
    case GT_FILTER:
      drawCircle(cx - 4, cy - 3, 7, col); drawCircle(cx + 4, cy - 3, 7, col); drawCircle(cx, cy + 4, 7, col);
      break;
    case GT_DRAW:
      strokeSegAA(cx - 7, cy + 7, cx + 6, cy - 6, 2.6f, col);
      strokeSegAA(cx - 9, cy + 9, cx - 7, cy + 7, 1.0f, col);
      break;
    case GT_TEXT:
      strokeSegAA(cx - 8, cy - 8, cx + 8, cy - 8, 1.6f, col); strokeSegAA(cx, cy - 8, cx, cy + 9, 1.6f, col);
      break;
    default:                                              // tamano: flecha doble en diagonal
      drawRoundRect(cx - 10, cy - 10, 20, 20, 3, col);
      strokeSegAA(cx - 5, cy + 5, cx + 5, cy - 5, 1.3f, col);
      strokeSegAA(cx + 5, cy - 5, cx + 5, cy - 1, 1.3f, col); strokeSegAA(cx + 5, cy - 5, cx + 1, cy - 5, 1.3f, col);
      strokeSegAA(cx - 5, cy + 5, cx - 5, cy + 1, 1.3f, col); strokeSegAA(cx - 5, cy + 5, cx - 1, cy + 5, 1.3f, col);
      break;
  }
}

// ---- Piezas de interfaz ----
static void gedChip(int x, int y, int w, int h, const char* t, bool on, bool enabled){
  uint16_t bg = on ? TH_PRIM : GED_CHIP, fg = on ? TH_ONACC : (enabled ? GED_TXT : GED_DIS);
  fillRoundRect(x, y, w, h, h / 2, bg);
  int fs = uiFontFit(t, w - 12, 2);
  drawTextC(x + w / 2, y + (h - uiLineH(fs)) / 2, t, fs, fg);
}
static void gedPanelBg(int x, int y, int w, int h, int rad){
  if(uiGlass) drawGlassCardFlat(x, y, w, h, rad, GED_GLASS, GED_BG);
  else        fillRoundRect(x, y, w, h, rad, GED_PANEL);
}

// Deslizador: geometria unica para dibujo y toque.
static void gedSliderGeom(const GedGeom& g, int y, int& x0, int& x1){ x0 = g.panelX + 24; x1 = g.panelX + g.panelW - 86; (void)y; }
static void gedSlider(const GedGeom& g, int y, int v, int vmin, int vmax, bool enabled, const char* label){
  int x0, x1; gedSliderGeom(g, y, x0, x1);
  fillRoundRect(x0, y - 2, x1 - x0, 4, 2, GED_CHIP);
  int zx = vmin < 0 ? x0 + (x1 - x0) / 2 : x0;
  int kx = x0 + (int)((int64_t)(v - vmin) * (x1 - x0) / (vmax - vmin));
  uint16_t acc = enabled ? TH_PRIM : GED_DIS;
  if(kx > zx) fillRect(zx, y - 2, kx - zx, 4, acc); else fillRect(kx, y - 2, zx - kx, 4, acc);
  if(vmin < 0) fillRect(zx - 1, y - 7, 2, 14, GED_TXT2);
  fillCircleAA((float)kx, (float)y, 11.0f, enabled ? GED_TXT : GED_DIS);
  char b[24];
  if(label) snprintf(b, sizeof(b), "%s", label);
  else snprintf(b, sizeof(b), vmin < 0 && v > 0 ? "+%d" : "%d", v);
  drawTextC(g.panelX + g.panelW - 44, y - uiLineH(2) / 2, b, 2, enabled ? GED_TXT : GED_DIS);
}
static int gedSliderValue(const GedGeom& g, int y, int tx, int vmin, int vmax){
  int x0, x1; gedSliderGeom(g, y, x0, x1);
  if(tx < x0) tx = x0;
  if(tx > x1) tx = x1;
  return vmin + (int)((int64_t)(tx - x0) * (vmax - vmin) / (x1 - x0));
}

// Filas de botones de igual ancho dentro del panel.
static void gedRowCell(const GedGeom& g, int n, int i, int& x, int& w){
  const int gap = 8, inner = g.panelW - 24;
  w = (inner - (n - 1) * gap) / n;
  x = g.panelX + 12 + i * (w + gap);
}
static int gedRowHit(const GedGeom& g, int n, int tx){
  for(int i = 0; i < n; i++){ int x, w; gedRowCell(g, n, i, x, w); if(tx >= x - 4 && tx <= x + w + 4) return i; }
  return -1;
}

// Fila de colores + grosores (Dibujo) o colores + tamanos (Texto).
static void gedColorGeom(const GedGeom& g, int i, int& cx, int& cy, int& r){
  r = 12; cy = g.panelY + 70;
  cx = g.panelX + 26 + i * 30;
}
static void gedDrawColors(const GedGeom& g){
  for(int i = 0; i < GED_COLORS; i++){
    int cx, cy, r; gedColorGeom(g, i, cx, cy, r);
    uint16_t c = rgb565((GED_RGB[i] >> 16) & 255, (GED_RGB[i] >> 8) & 255, GED_RGB[i] & 255);
    if(i == gedColor) fillCircleAA((float)cx, (float)cy, (float)r + 3.0f, GED_TXT);
    fillCircleAA((float)cx, (float)cy, (float)r, c);
    if(i == 1) drawCircle(cx, cy, r, GED_TXT2);           // el negro se ve sobre el panel oscuro
  }
}
static int gedColorHit(const GedGeom& g, int tx, int ty){
  for(int i = 0; i < GED_COLORS; i++){
    int cx, cy, r; gedColorGeom(g, i, cx, cy, r);
    if(abs(tx - cx) <= 15 && abs(ty - cy) <= 18) return i;
  }
  return -1;
}
// Tres botones a la derecha de los colores (grosor / tamano del texto).
static void gedTrioGeom(const GedGeom& g, int i, int& x, int& y, int& w, int& h){
  int x0 = g.panelX + 26 + GED_COLORS * 30;
  w = (g.panelX + g.panelW - 12 - x0 - 8) / 3; h = 34;
  x = x0 + i * (w + 4); y = g.panelY + 53;
}
static int gedTrioHit(const GedGeom& g, int tx, int ty){
  for(int i = 0; i < 3; i++){
    int x, y, w, h; gedTrioGeom(g, i, x, y, w, h);
    if(tx >= x && tx <= x + w && ty >= y - 4 && ty <= y + h + 4) return i;
  }
  return -1;
}

// ---- Barra de arriba: deshacer, rehacer, tamano de salida, Guardar ----
static void gedSaveBtnGeom(const GedGeom& g, int& x, int& y, int& w, int& h){ w = 116; h = 38; x = g.bx + g.bw - g.pad / 2 - w - 4; y = g.topY + 8; }
static void gedDrawTop(const GedGeom& g){
  fillRect(g.bx, g.topY, g.bw, GED_TOP_H, GED_BG);
  bool edit = gedPhase == GED_EDIT && gedE && gedE->base;
  bool cu = edit && flexIeCanUndo(gedE), cr = edit && flexIeCanRedo(gedE);
  int cy = g.topY + GED_TOP_H / 2;
  fillCircleAA((float)(g.bx + g.pad + 18), (float)cy, 20.0f, GED_CHIP);
  gedIcoUndo(g.bx + g.pad + 18, cy, cu ? GED_TXT : GED_DIS, false);
  fillCircleAA((float)(g.bx + g.pad + 66), (float)cy, 20.0f, GED_CHIP);
  gedIcoUndo(g.bx + g.pad + 66, cy, cr ? GED_TXT : GED_DIS, true);
  if(edit){
    int w, h; flexIeOutSize(gedE, &w, &h);
    char b[40]; snprintf(b, sizeof(b), "%d x %d", w, h);
    drawTextC(g.bx + g.bw / 2 + 10, cy - 4, b, 1, GED_TXT2);
  }
  int x, y, w, h; gedSaveBtnGeom(g, x, y, w, h);
  bool can = edit && !flexIeIsIdentity(gedE);
  fillRoundRect(x, y, w, h, h / 2, can ? TH_PRIM : GED_CHIP);
  drawTextC(x + w / 2, y + (h - uiLineH(2)) / 2, "Guardar", 2, can ? TH_ONACC : GED_DIS);
}

// ---- Pestanas de herramientas ----
static void gedDrawTabs(const GedGeom& g){
  fillRect(g.bx, g.tabsY, g.bw, GED_TABS_H, GED_BG);
  int tw = g.bw / GT_N;
  for(int t = 0; t < GT_N; t++){
    int cx = g.bx + t * tw + tw / 2;
    bool on = t == gedTool;
    uint16_t c = on ? TH_PRIM : GED_TXT2;
    gedIcoTool(t, cx, g.tabsY + 20, c);
    int fs = uiFontFit(GED_TOOL[t], tw - 4, 1);
    drawTextC(cx, g.tabsY + 38, GED_TOOL[t], fs, c);
    if(on) fillRoundRect(cx - 12, g.tabsY + 52, 24, 4, 2, TH_PRIM);
  }
}

// ---- Miniaturas de los filtros (desde la copia reducida) ----
static void gedFilterThumbs(int side){
  if(gedFtOk && gedFtSide == side) return;
  size_t need = (size_t)FLEXIE_FILTER_N * side * side * 2;
  if(!gedFilterThumb || gedFtSide != side){
    mediaFree(gedFilterThumb);
    gedFilterThumb = (uint16_t*)mediaAlloc(need);
    gedFtSide = side;
  }
  if(!gedFilterThumb || !gedE || !gedE->base || !gedBand) return;
  FlexIeState keep = gedE->st;
  // Cuadrada y centrada: el mismo encuadre que la vista, sin trazos.
  int ow, oh; flexIeOutSize(gedE, &ow, &oh);
  float cw = keep.c1x - keep.c0x, ch = keep.c1y - keep.c0y;
  if(ow > oh){ float k = (float)oh / ow * cw; gedE->st.c0x = keep.c0x + (cw - k) / 2; gedE->st.c1x = gedE->st.c0x + k; }
  else if(oh > ow){ float k = (float)ow / oh * ch; gedE->st.c0y = keep.c0y + (ch - k) / 2; gedE->st.c1y = gedE->st.c0y + k; }
  for(int f = 0; f < FLEXIE_FILTER_N; f++){
    gedE->st.filter = (uint8_t)f; gedE->st.filterAmt = 100;
    uint16_t* dst = gedFilterThumb + (size_t)f * side * side;
    for(int y0 = 0; y0 < side; y0 += GED_BAND){
      int rows = side - y0 < GED_BAND ? side - y0 : GED_BAND;
      flexIeRenderRows(gedE, side, side, y0, rows, gedBand, NULL);
      for(int r = 0; r < rows; r++){
        const uint8_t* p = gedBand + (size_t)r * side * 3;
        for(int i = 0; i < side; i++, p += 3) dst[(size_t)(y0 + r) * side + i] = rgb565(p[0], p[1], p[2]);
      }
    }
  }
  gedE->st = keep;
  gedFtOk = true;
}
static void gedFilterGeom(const GedGeom& g, int f, int& x, int& y, int& side){
  const int gap = 6;
  side = (g.panelW - 24 - (FLEXIE_FILTER_N - 1) * gap) / FLEXIE_FILTER_N;
  if(side > 56) side = 56;
  int total = FLEXIE_FILTER_N * side + (FLEXIE_FILTER_N - 1) * gap;
  x = g.panelX + (g.panelW - total) / 2 + f * (side + gap);
  y = g.panelY + 8;
}

// ---- Panel de la herramienta ----
#define GED_SLIDER_Y(g) ((g).panelY + 100)
static void gedDrawPanel(const GedGeom& g){
  fillRect(g.bx, g.panelY - 4, g.bw, GED_PANEL_H + 4, GED_BG);
  gedPanelBg(g.panelX, g.panelY, g.panelW, GED_PANEL_H - 6, 20);
  if(!gedE || !gedE->base) return;
  const FlexIeState* st = &gedE->st;
  switch(gedTool){
    case GT_CROP: {
      static const char* const L[4] = { "Izquierda", "Derecha", "Horizontal", "Vertical" };
      for(int i = 0; i < 4; i++){
        int x, w; gedRowCell(g, 4, i, x, w);
        int y = g.panelY + 10;
        fillRoundRect(x, y, w, 40, 14, GED_CHIP);
        int cx = x + 22, cy = y + 20;
        if(i < 2) gedIcoRotate(cx, cy, GED_TXT, i == 1); else gedIcoFlip(cx, cy, GED_TXT, i == 2);
        drawText(x + 40, y + (40 - uiLineH(1)) / 2 + 1, L[i], 1, GED_TXT);
      }
      for(int i = 0; i < GED_ASPECTS; i++){
        int x, w; gedRowCell(g, GED_ASPECTS, i, x, w);
        gedChip(x, g.panelY + 60, w, 30, GED_ASPECT[i], i == gedAspect, true);
      }
      drawTextC(g.panelX + g.panelW / 2, g.panelY + 101, "Arrastra las esquinas o el centro del marco", 1, GED_TXT2);
      break;
    }
    case GT_ADJ: {
      for(int i = 0; i < FLEXIE_ADJ_N; i++){
        int x, w; gedRowCell(g, 4, i % 4, x, w);
        int y = g.panelY + 8 + (i / 4) * 36;
        gedChip(x, y, w, 30, GED_ADJ[i], i == gedAdj, true);
        if(st->adj[i] && i != gedAdj) fillCircle(x + w - 8, y + 8, 3, TH_PRIM);   // este ajuste esta puesto
      }
      gedSlider(g, GED_SLIDER_Y(g), st->adj[gedAdj], -100, 100, true, NULL);
      break;
    }
    case GT_FILTER: {
      int x, y, side; gedFilterGeom(g, 0, x, y, side);
      gedFilterThumbs(side);
      for(int f = 0; f < FLEXIE_FILTER_N; f++){
        gedFilterGeom(g, f, x, y, side);
        bool on = st->filter == f;
        if(on) fillRoundRect(x - 3, y - 3, side + 6, side + 6, 12, TH_PRIM);
        if(gedFilterThumb && gedFtOk) gedBlit565(gedFilterThumb + (size_t)f * side * side, x, y, side, side);
        else fillRoundRect(x, y, side, side, 10, GED_CHIP);
        int fs = uiFontFit(GED_FILTER[f], side + 4, 1);
        drawTextC(x + side / 2, y + side + 6, GED_FILTER[f], fs, on ? TH_PRIM : GED_TXT2);
      }
      bool en = st->filter != FLEXIE_FILTER_NONE;
      char lb[16]; snprintf(lb, sizeof(lb), "%d %%", en ? st->filterAmt : 0);
      gedSlider(g, GED_SLIDER_Y(g) + 4, en ? st->filterAmt : 0, 0, 100, en, lb);
      break;
    }
    case GT_DRAW: {
      for(int i = 0; i < GED_KINDS; i++){
        int x, w; gedRowCell(g, GED_KINDS, i, x, w);
        gedChip(x, g.panelY + 8, w, 30, GED_KIND[i], i == gedKind, true);
      }
      gedDrawColors(g);
      for(int i = 0; i < 3; i++){
        int x, y, w, h; gedTrioGeom(g, i, x, y, w, h);
        fillRoundRect(x, y, w, h, 12, i == gedWidth ? TH_PRIM : GED_CHIP);
        fillCircleAA((float)(x + w / 2), (float)(y + h / 2), 2.0f + i * 3.0f, i == gedWidth ? TH_ONACC : GED_TXT);
      }
      bool shape = GED_KIND_OV[gedKind] == FLEXIE_OV_RECT || GED_KIND_OV[gedKind] == FLEXIE_OV_ELLIPSE;
      if(shape){
        int x = g.panelX + 12, y = g.panelY + 92;
        fillRoundRect(x, y, 104, 24, 12, gedFill ? TH_PRIM : GED_CHIP);
        drawTextC(x + 52, y + 8, gedFill ? "Relleno: s\xC3\xAD" : "Relleno: no", 1, gedFill ? TH_ONACC : GED_TXT);
        drawText(x + 118, y + 8, "Arrastra sobre la foto", 1, GED_TXT2);
      } else drawTextC(g.panelX + g.panelW / 2, g.panelY + 100, "Dibuja sobre la foto con el dedo", 1, GED_TXT2);
      break;
    }
    case GT_TEXT: {
      if(!gedTextOn){
        int x, w; gedRowCell(g, 1, 0, x, w);
        fillRoundRect(x, g.panelY + 8, w, 34, 17, TH_PRIM);
        drawTextC(x + w / 2, g.panelY + 8 + (34 - uiLineH(2)) / 2, "+ A\xC3\xB1" "adir texto", 2, TH_ONACC);
      } else {
        int x, w; gedRowCell(g, 2, 0, x, w);
        gedChip(x, g.panelY + 8, w, 34, "Cancelar", false, true);
        gedRowCell(g, 2, 1, x, w);
        gedChip(x, g.panelY + 8, w, 34, "Listo", true, true);
      }
      gedDrawColors(g);
      for(int i = 0; i < 3; i++){
        int x, y, w, h; gedTrioGeom(g, i, x, y, w, h);
        fillRoundRect(x, y, w, h, 12, i == gedTextSz ? TH_PRIM : GED_CHIP);
        drawTextC(x + w / 2, y + (h - uiLineH(1 + i)) / 2 + (i == 2 ? 2 : 0), "A", 1 + i, i == gedTextSz ? TH_ONACC : GED_TXT);
      }
      drawTextC(g.panelX + g.panelW / 2, g.panelY + 100,
                gedTextOn ? "Arrastra el texto para colocarlo y pulsa Listo" : "Escribe un texto y col\xC3\xB3" "calo sobre la foto", 1, GED_TXT2);
      break;
    }
    default: {                                            // Tamano
      int w, h; flexIeOutSize(gedE, &w, &h);
      char b[64];
      snprintf(b, sizeof(b), "Se guardar\xC3\xA1 a %d x %d px", w, h);
      drawTextC(g.panelX + g.panelW / 2, g.panelY + 10, b, 2, GED_TXT);
      if(gedSrcW != gedW || gedSrcH != gedH){
        snprintf(b, sizeof(b), "La original mide %d x %d (el P4 edita a %d x %d)", gedSrcW, gedSrcH, gedW, gedH);
        drawTextC(g.panelX + g.panelW / 2, g.panelY + 34, b, 1, GED_TXT2);
      }
      int cur = 0;                                        // -1 = a medida (ningun boton)
      if(st->longSide){
        cur = -1;
        float cw, ch; int ow, oh;
        gedE->st.longSide = 0; flexIeOutSize(gedE, &ow, &oh); gedE->st.longSide = st->longSide;
        cw = (float)(ow > oh ? ow : oh); ch = st->longSide * 100.0f / cw;
        for(int i = 1; i < GED_SIZES; i++) if(fabsf(ch - GED_SIZE_PCT[i]) < 2.0f) cur = i;
      }
      for(int i = 0; i < GED_SIZES; i++){
        int x, cw; gedRowCell(g, GED_SIZES, i, x, cw);
        gedChip(x, g.panelY + 50, cw, 30, GED_SIZE[i], i == cur, true);
      }
      bool id = flexIeIsIdentity(gedE);
      int x, cw; gedRowCell(g, 1, 0, x, cw);
      fillRoundRect(x, g.panelY + 88, cw, 28, 14, GED_CHIP);
      drawTextC(x + cw / 2, g.panelY + 88 + (28 - uiLineH(1)) / 2, "Restablecer todo", 1, id ? GED_DIS : rgb565(255, 120, 120));
      break;
    }
  }
}

// ---- Lo que va ENCIMA de la vista: recorte, forma o texto en curso ----
static void gedCropRectPx(float c0x, float c0y, float c1x, float c1y, int& x0, int& y0, int& x1, int& y1){
  x0 = gedVX + (int)(c0x * gedVW + 0.5f); x1 = gedVX + (int)(c1x * gedVW + 0.5f);
  y0 = gedVY + (int)(c0y * gedVH + 0.5f); y1 = gedVY + (int)(c1y * gedVH + 0.5f);
}
static void gedDrawCropUi(){
  float a0 = gedDrag == GD_CROP ? gedC0x : gedE->st.c0x, b0 = gedDrag == GD_CROP ? gedC0y : gedE->st.c0y;
  float a1 = gedDrag == GD_CROP ? gedC1x : gedE->st.c1x, b1 = gedDrag == GD_CROP ? gedC1y : gedE->st.c1y;
  int x0, y0, x1, y1; gedCropRectPx(a0, b0, a1, b1, x0, y0, x1, y1);
  const uint16_t dark = rgb565(0, 0, 0);
  fillRectA(gedVX, gedVY, gedVW, y0 - gedVY, dark, 150);
  fillRectA(gedVX, y1, gedVW, gedVY + gedVH - y1, dark, 150);
  fillRectA(gedVX, y0, x0 - gedVX, y1 - y0, dark, 150);
  fillRectA(x1, y0, gedVX + gedVW - x1, y1 - y0, dark, 150);
  const uint16_t W = rgb565(255, 255, 255);
  for(int k = 1; k < 3; k++){                          // tercios
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
static uint16_t gedColor565(){
  uint32_t c = GED_RGB[gedColor];
  return rgb565((c >> 16) & 255, (c >> 8) & 255, c & 255);
}
static float gedWidthPx(){ int L = gedVW > gedVH ? gedVW : gedVH; float r = 0.5f * GED_WIDTH[gedWidth] * L; return r < 1.0f ? 1.0f : r; }
static void gedDrawShapeLive(){
  float x0 = gedVX + gedC0x * gedVW, y0 = gedVY + gedC0y * gedVH, x1 = gedVX + gedC1x * gedVW, y1 = gedVY + gedC1y * gedVH;
  uint16_t c = gedColor565();
  float r = gedWidthPx();
  int ov = GED_KIND_OV[gedKind];
  if(ov == FLEXIE_OV_LINE || ov == FLEXIE_OV_ARROW){
    strokeSegAA(x0, y0, x1, y1, r, c);
    if(ov == FLEXIE_OV_ARROW){
      float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
      if(len > 1.0f){
        float hl = len * 0.35f, cap = 6.0f * r + 10.0f; if(hl > cap) hl = cap;
        float ux = dx / len, uy = dy / len, cs = cosf(0.4887f), sn = sinf(0.4887f);
        strokeSegAA(x1, y1, x1 - hl * (ux * cs - uy * sn), y1 - hl * (uy * cs + ux * sn), r, c);
        strokeSegAA(x1, y1, x1 - hl * (ux * cs + uy * sn), y1 - hl * (uy * cs - ux * sn), r, c);
      }
    }
    return;
  }
  float ax = x0 < x1 ? x0 : x1, bx = x0 < x1 ? x1 : x0, ay = y0 < y1 ? y0 : y1, by = y0 < y1 ? y1 : y0;
  if(ov == FLEXIE_OV_RECT){
    if(gedFill) fillRect((int)ax, (int)ay, (int)(bx - ax) + 1, (int)(by - ay) + 1, c);
    else {
      strokeSegAA(ax, ay, bx, ay, r, c); strokeSegAA(bx, ay, bx, by, r, c);
      strokeSegAA(bx, by, ax, by, r, c); strokeSegAA(ax, by, ax, ay, r, c);
    }
    return;
  }
  float cx = (ax + bx) / 2, cy = (ay + by) / 2, rx = (bx - ax) / 2, ry = (by - ay) / 2;
  if(gedFill){
    for(int yy = (int)ay; yy <= (int)by; yy++){
      float t = ry > 0 ? (yy + 0.5f - cy) / ry : 0; if(t * t > 1) continue;
      float hw = rx * sqrtf(1 - t * t);
      hLine((int)(cx - hw), yy, (int)(2 * hw) + 1, c);
    }
    return;
  }
  float px0 = cx + rx, py0 = cy;
  for(int k = 1; k <= 40; k++){
    float a = 6.28318f * k / 40.0f, x = cx + rx * cosf(a), y = cy + ry * sinf(a);
    strokeSegAA(px0, py0, x, y, r, c);
    px0 = x; py0 = y;
  }
}
// El texto a medio colocar, con la MISMA cobertura que saldra al guardar.
static void gedDrawTextLive(){
  if(!gedText[0]) return;
  int L = gedVW > gedVH ? gedVW : gedVH;
  int hpx = (int)(GED_TEXT_H[gedTextSz] * L + 0.5f);
  if(hpx < 4) hpx = 4;
  int x = gedVX + (int)(gedTextU * gedVW), y = gedVY + (int)(gedTextV * gedVH);
  int tw = gedTextFn(NULL, gedText, hpx, -1, 0, NULL, 0);
  uint16_t c = gedColor565();
  uint8_t cov[GED_BAND_W];
  int n = tw < GED_BAND_W ? tw : GED_BAND_W;
  for(int r = 0; r < hpx; r++){
    int yy = y + r;
    if(yy < gedVY || yy >= gedVY + gedVH) continue;
    gedTextFn(NULL, gedText, hpx, r, 0, cov, n);
    for(int i = 0; i < n; i++){
      int xx = x + i;
      if(cov[i] && xx >= gedVX && xx < gedVX + gedVW) pxA(xx, yy, c, cov[i]);
    }
  }
  // Marco de "se puede mover".
  for(int i = 0; i < tw; i += 6) if(x + i < gedVX + gedVW){ px(x + i, y - 3, GED_TXT); px(x + i, y + hpx + 2, GED_TXT); }
}

// ---- El pozo de la foto ----
static void gedDrawWell(bool flush){
  GedGeom g = gedGeom();
  setBuf(fb);
  fillRect(g.wellX, g.wellY, g.wellW, g.wellH, GED_BG);
  if(gedPhase == GED_EDIT && gedE && gedE->base){
    if(!gedViewOk) gedViewBuild();
    if(gedViewOk){
      gedBlit565(gedView, gedVX, gedVY, gedVW, gedVH);
      int ox0 = gClipY0, ox1 = gClipY1;
      gClipY0 = g.wellY; gClipY1 = g.wellY + g.wellH - 1;
      if(gedCropView()) gedDrawCropUi();
      else if(gedDrag == GD_SHAPE) gedDrawShapeLive();
      if(gedTool == GT_TEXT && gedTextOn) gedDrawTextLive();
      gClipY0 = ox0; gClipY1 = ox1;
    } else drawTextC(g.wellX + g.wellW / 2, g.wellY + g.wellH / 2, "Sin memoria para la vista previa", 2, GED_TXT2);
  }
  if(flush) flxFlush(g.wellY, g.wellY + g.wellH - 1);
}

// ---- Progreso (abrir / guardar) y hoja de guardar ----
static void gedCardGeom(const GedGeom& g, int& x, int& y, int& w, int& h){
  w = g.bw - 2 * g.pad - 24; if(w > 380) w = 380;
  h = 170; x = g.bx + (g.bw - w) / 2; y = g.wellY + (g.wellH - h) / 2;
}
static void gedDrawProgress(const GedGeom& g){
  int x, y, w, h; gedCardGeom(g, x, y, w, h);
  gedPanelBg(x, y, w, h, 24);
  bool saving = gedPhase == GED_SAVING;
  int pct = gedJob.pct; if(pct < 0) pct = 0; if(pct > 100) pct = 100;
  drawTextC(x + w / 2, y + 22, gedJob.cancel ? "Cancelando..." : saving ? "Guardando..." : "Abriendo foto...", 3, GED_TXT);
  int bx = x + 24, bw = w - 48, by = y + 76;
  fillRoundRect(bx, by, bw, 8, 4, GED_CHIP);
  if(pct > 0) fillRoundRect(bx, by, bw * pct / 100 < 8 ? 8 : bw * pct / 100, 8, 4, TH_PRIM);
  char b[16]; snprintf(b, sizeof(b), "%d %%", pct);
  drawTextC(x + w / 2, by + 16, b, 1, GED_TXT2);
  fillRoundRect(x + w / 2 - 70, y + h - 50, 140, 36, 18, GED_CHIP);
  drawTextC(x + w / 2, y + h - 50 + (36 - uiLineH(2)) / 2, "Cancelar", 2, gedJob.cancel ? GED_DIS : GED_TXT);
  gedShownPct = pct; gedShownMs = millis();
}
static bool gedProgressCancelHit(const GedGeom& g, int tx, int ty){
  int x, y, w, h; gedCardGeom(g, x, y, w, h);
  return tx >= x + w / 2 - 80 && tx <= x + w / 2 + 80 && ty >= y + h - 56 && ty <= y + h - 8;
}

static void gedSheetGeom(const GedGeom& g, int& x, int& y, int& w, int& h){
  w = g.bw - 2 * g.pad; x = g.bx + g.pad;
  h = (gedSrcW != gedW || gedSrcH != gedH) ? 284 : 256;
  y = g.by + g.bh - h - 8;
}
static void gedSheetBtn(const GedGeom& g, int i, int& x, int& y, int& w, int& h){
  int sx, sy, sw, sh; gedSheetGeom(g, sx, sy, sw, sh);
  x = sx + 20; w = sw - 40; h = 46;
  y = sy + sh - 20 - (3 - i) * 54;
}
static void gedDrawSheet(const GedGeom& g){
  int x, y, w, h; gedSheetGeom(g, x, y, w, h);
  fillRectA(g.bx, g.by, g.bw, g.bh, rgb565(0, 0, 0), 120);
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 26, GED_GLASS); else fillRoundRect(x, y, w, h, 26, GED_PANEL);
  int ow, oh; flexIeOutSize(gedE, &ow, &oh);
  drawTextC(x + w / 2, y + 18, "Guardar foto", 3, GED_TXT);
  char b[96];
  snprintf(b, sizeof(b), "Se guardar\xC3\xA1 en JPEG a %d x %d px", ow, oh);
  drawTextC(x + w / 2, y + 52, b, 1, GED_TXT2);
  if(gedSrcW != gedW || gedSrcH != gedH){
    snprintf(b, sizeof(b), "La original mide %d x %d: el P4 la edita reducida", gedSrcW, gedSrcH);
    drawTextC(x + w / 2, y + 70, b, 1, rgb565(255, 190, 110));
  }
  static const char* const L[3] = { "Guardar como copia", "Reemplazar original", "Cancelar" };
  for(int i = 0; i < 3; i++){
    int bx, by, bw, bh; gedSheetBtn(g, i, bx, by, bw, bh);
    if(i < 2) fillRoundRect(bx, by, bw, bh, bh / 2, i == 0 ? TH_PRIM : GED_CHIP);
    drawTextC(bx + bw / 2, by + (bh - uiLineH(2)) / 2, L[i], 2, i == 0 ? TH_ONACC : i == 1 ? GED_TXT : GED_TXT2);
  }
}

// ---- Pantalla entera ----
static void gedRender(){
  if(!gedActive()) return;
  GedGeom g = gedGeom();
  setBuf(fb);
  fillRect(g.bx, g.by, g.bw, g.bh, GED_BG);
  gedDrawTop(g);
  if(gedPhase == GED_EDIT){
    gedDrawWell(false);
    gedDrawPanel(g);
  } else {
    // Abriendo o guardando: la miniatura (si la hay) y el progreso real.
    if(gedPhase == GED_SAVING && gedViewOk) gedBlit565(gedView, gedVX, gedVY, gedVW, gedVH);
    else if(gMlOk){
      mlLock();
      int i = flexMlFindId(&gMs.lib, gedId), budget = 1;
      const uint16_t* th = i >= 0 ? mlThumbGetLocked(&gMs.lib.recs[i], &budget, NULL) : NULL;
      if(th) mlBlitThumb(th, g.bx + (g.bw - ML_SIDE) / 2, g.wellY + 20, ML_SIDE, ML_SIDE, 16, GED_BG);
      mlUnlock();
    }
    gedDrawProgress(g);
  }
  gedDrawTabs(g);
  if(gedSheet) gedDrawSheet(g);
  flxFlush(WIN_TOP, WIN_BOT);
}

// #############################################################
// ##  ABRIR, CERRAR, SOLTAR MEMORIA
// #############################################################
static bool gedIsJpegPath(const char* p){
  const char* d = p ? strrchr(p, '.') : NULL;
  return d && (!strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg"));
}
// Lo que el menu ofrece editar: una foto JPEG abierta (no protegida), que el
// P4 sabe decodificar y que no pasa del tope de las fotos.
static bool gedEditable(const FlexMlRec* r){
  if(!r || r->kind != FML_K_PHOTO || (r->flags & FML_R_LOCKED) || r->state == FML_S_ERROR) return false;
  if(!gedIsJpegPath(r->path) || r->size > FML_LIMIT_PHOTO) return false;
  return (r->flags & FML_R_PLAYABLE) || (r->flags & FML_R_NEED_THUMB);   // sin comprobar aun: se intenta
}

static void gedFreeImage(){
  if(gedE) flexIeSetBase(gedE, NULL, 0, 0);
  mediaFree(gedBase); gedBase = NULL; gedW = gedH = 0;
  mediaFree(gedProxy); gedProxy = NULL; gedPW = gedPH = 0;
  mediaFree(gedView); gedView = NULL; gedViewCap = 0; gedViewOk = false;
  mediaFree(gedFilterThumb); gedFilterThumb = NULL; gedFtOk = false; gedFtSide = 0;
}
// Suelta TODO y deja el editor cerrado. No publica nada.
static void gedCloseNow(){
  if(!gedJobStopWait()){ gedPhase = GED_OFF; return; }   // el trabajador sigue con lo suyo: no se toca
  gedFreeImage();
  mediaFree(gedBand); gedBand = NULL;
  mediaFree(gedCov); gedCov = NULL;
  mediaFree(gedE); gedE = NULL;
  gedPhase = GED_OFF; gedId = 0; gedPath[0] = 0; gedName[0] = 0;
  gedSheet = false; gedAsk = GA_NONE; gedDrag = GD_NONE; gedTextOn = false; gedText[0] = 0;
  gedReopen = false; gedLivePending = false; gedTextAsk = false;
}

// ¿Se esta viendo? A pantalla completa lo dice el estado de la app; dentro
// de una ventana de DeX, que su tick haya corrido hace nada.
static uint32_t gedTickMs = 0;
static bool gedTicking(){ return gedTickMs && millis() - gedTickMs < 300; }
static bool gedForeground(){
  return (gState == ST_APP && gAppId == IC_GALERIA && gAppState[IC_GALERIA] == ALIFE_RUNNING) || gedTicking();
}
static void galRender();

// Si el trabajador tardo mas que la espera al cerrar, lo que usaba se recoge
// cuando por fin acaba (nunca antes): el editor no se queda inutil hasta
// reiniciar ni se suelta memoria en uso.
static void gedReclaim(){
  if(!gedLeak || !gedJobDone()) return;
  if(gedJob.kind == GJ_OPEN && gedJob.rc == 0){ mediaFree(gedJob.base); mediaFree(gedJob.proxy); gedJob.base = gedJob.proxy = NULL; }
  if(gedJob.kind == GJ_SAVE && gedJob.rc == 0) flexFsDelete(gedJob.path);
  gedJobOn = false; gedLeak = false;
  if(gedPhase == GED_OFF){
    gedFreeImage();
    mediaFree(gedBand); gedBand = NULL;
    mediaFree(gedCov); gedCov = NULL;
    mediaFree(gedE); gedE = NULL;
  }
  Serial.println(F("[editor] el trabajador termino: memoria recogida"));
}

// Se cierra por algo de fuera (la foto se protegio o se borro): fuera los
// pixeles YA, y se dice por que.
static void gedAbort(const char* why){
  Serial.printf("[editor] cerrado: %s\n", why);
  gedCloseNow();
  if(gedForeground()){ galRender(); mmDlgOpen("Editor cerrado", why, "Aceptar", "", false); gedAsk = GA_INFO; }
  else sysNotify("Galer\xC3\xAD" "a", why);
}

static void gedStartOpen(){
  gedPhase = GED_OPENING;
  snprintf(gedJob.path, sizeof(gedJob.path), "%s", gedPath);
  gedJobStart(GJ_OPEN);
}

// No se abre: la Galeria se repinta (el menu de donde se vino desaparece) y
// se dice por que.
static void gedOpenFail(const char* title, const char* why){
  galRender();
  mmDlgOpen(title, why, "Aceptar", "", false);
  gedAsk = GA_INFO;
}
static bool gedOpen(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r)) return false;
  if(!gedEditable(&r)){
    gedOpenFail("No se puede editar", "El editor abre fotos JPEG que el P4 sabe leer (no protegidas).");
    return false;
  }
  gedReclaim();
  if(gedLeak){
    gedOpenFail("Editor ocupado", "El \xC3\xBAltimo trabajo del editor no ha terminado. Int\xC3\xA9ntalo en un momento.");
    return false;
  }
  if(gedActive()) gedCloseNow();
  gedE = (FlexImgEdit*)mediaAlloc(sizeof(FlexImgEdit));
  gedBand = (uint8_t*)mediaAlloc((size_t)GED_BAND_W * GED_BAND * 3);
  gedCov = (uint8_t*)mediaAlloc((size_t)GED_BAND_W * GED_BAND);
  if(!gedE || !gedBand || !gedCov){
    mediaFree(gedE); gedE = NULL; mediaFree(gedBand); gedBand = NULL; mediaFree(gedCov); gedCov = NULL;
    gedOpenFail("Sin memoria", "No hay memoria libre para abrir el editor.");
    return false;
  }
  flexIeInit(gedE, NULL, 0, 0);
  gedId = id;
  snprintf(gedPath, sizeof(gedPath), "%s", r.path);
  snprintf(gedName, sizeof(gedName), "%s", r.name);
  gedTool = GT_CROP; gedAdj = FLEXIE_ADJ_BRIGHT; gedAspect = 0; gedKind = 0;
  gedSheet = false; gedAsk = GA_NONE; gedDrag = GD_NONE; gedTextOn = false; gedText[0] = 0;
  gedReopen = false; gedSeenRev = mlRev();
  Serial.printf("[editor] abriendo id=%lu (%lu KB)\n", (unsigned long)id, (unsigned long)(r.size / 1024u));
  gedStartOpen();
  gedRender();
  return true;
}

// #############################################################
// ##  PUBLICAR (loopTask)
// #############################################################
static bool gedNameTaken(const char* nm){
  bool t = false;
  mlLock();
  for(int i = 0; i < gMs.lib.n && !t; i++) if(!strcasecmp(gMs.lib.recs[i].name, nm)) t = true;
  mlUnlock();
  return t;
}
// "foto (editada).jpg", "foto (editada 2).jpg"... un nombre que no ensena
// ya otro elemento de la biblioteca.
static void gedCopyName(char* out, size_t cap){
  char stem[FML_NAME_MAX];
  snprintf(stem, sizeof(stem), "%s", gedName);
  char* dot = strrchr(stem, '.');
  if(dot && dot != stem) *dot = 0;
  char* ed = strstr(stem, " (editada");
  if(ed && ed != stem) *ed = 0;
  for(int n = 1; n < 1000; n++){
    if(n == 1) snprintf(out, cap, "%s (editada).jpg", stem);
    else snprintf(out, cap, "%s (editada %d).jpg", stem, n);
    if(!gedNameTaken(out)) return;
  }
}

// El trabajador dejo el temporal comprobado: se publica o se descarta.
static bool gedCommit(char* msg, size_t cap){
  GedJob* j = &gedJob;
  FlexMlRec r;
  bool have = mlGet(gedId, &r);
  char why[96] = "";
  // La foto se protegio o se borro mientras se guardaba: no sale nada (una
  // copia de algo protegido no puede quedar sin proteger).
  if(!have || (r.flags & FML_R_LOCKED)){
    flexFsDelete(j->path);
    snprintf(msg, cap, "%s", !have ? "La foto original ya no existe: no se ha guardado nada"
                                   : "La foto se ha protegido: no se ha guardado nada");
    return false;
  }
  if(gedSaveMode == GS_REPLACE){
    if(!mlReplaceFile(gedId, j->path, why, sizeof(why))){
      flexFsDelete(j->path);
      snprintf(msg, cap, "No se pudo reemplazar: %s", why);
      return false;
    }
    snprintf(msg, cap, "Foto reemplazada (%d x %d)", j->outW, j->outH);
  } else {
    char nm[FML_NAME_MAX]; gedCopyName(nm, sizeof(nm));
    uint32_t nid = mlAddFile(j->path, FML_K_PHOTO, nm, FML_O_EDIT, gedId, why, sizeof(why));
    if(!nid){
      flexFsDelete(j->path);
      snprintf(msg, cap, "No se pudo guardar la copia: %s", why);
      return false;
    }
    snprintf(msg, cap, "Copia guardada: %s", nm);
  }
  Serial.printf("[editor] %s id=%lu %dx%d %lu KB\n", gedSaveMode == GS_REPLACE ? "reemplazada" : "copia de",
                (unsigned long)gedId, j->outW, j->outH, (unsigned long)(j->bytes / 1024u));
  return true;
}

// Recoge el resultado del trabajador. true = algo cambio (repintar).
static bool gedCollect(){
  if(!gedJobOn || !gedJobDone()) return false;
  gedJobOn = false;
  GedJob* j = &gedJob;
  if(j->kind == GJ_OPEN){
    if(j->rc == 0){
      gedBase = j->base; gedW = j->W; gedH = j->H;
      gedProxy = j->proxy; gedPW = j->PW; gedPH = j->PH;
      gedSrcW = j->srcW; gedSrcH = j->srcH;
      j->base = NULL; j->proxy = NULL;
      flexIeSetBase(gedE, gedBase, gedW, gedH);    // con gedReopen el estado y el historial siguen
      flexIeSetProxy(gedE, gedProxy, gedPW, gedPH);
      flexIeSetTextFn(gedE, gedTextFn, NULL);
      gedReopen = false;
      gedPhase = GED_EDIT;
      gedChanged();
      Serial.printf("[editor] lista: %dx%d (archivo %dx%d)%s\n", gedW, gedH, gedSrcW, gedSrcH, gedProxy ? ", con copia reducida" : "");
    } else if(j->rc == 1){
      gedCloseNow();
      if(gedForeground()) galRender();
    } else {
      char why[128]; snprintf(why, sizeof(why), "%s", j->why);
      gedCloseNow();
      if(gedForeground()){ galRender(); mmDlgOpen("No se puede editar", why, "Aceptar", "", false); gedAsk = GA_INFO; }
      else sysNotify("Galer\xC3\xAD" "a", why);
    }
    return true;
  }
  if(j->kind == GJ_SAVE){
    gedPhase = GED_EDIT;
    char msg[160] = "";
    if(j->rc == 0){
      if(gedCommit(msg, sizeof(msg))){
        gedCloseNow();                              // hecho: se vuelve a la Galeria
        if(gedForeground()) galRender();
        sysNotify("Galer\xC3\xAD" "a", msg);
        return true;
      }
      // No se publico porque la foto se protegio o desaparecio: el editor no
      // se queda con sus pixeles.
      FlexMlRec r;
      if(!mlGet(gedId, &r) || (r.flags & FML_R_LOCKED)){ gedSeenRev = mlRev(); gedAbort(msg); return true; }
    }
    else if(j->rc == 1) snprintf(msg, sizeof(msg), "Guardado cancelado: la foto no ha cambiado");
    else if(j->rc < 0) snprintf(msg, sizeof(msg), "%s", j->why);
    Serial.printf("[editor] no guardada: %s\n", msg);
    if(gedForeground()){ gedRender(); mmDlgOpen("No se ha guardado", msg, "Aceptar", "", false); gedAsk = GA_INFO; }
    else sysNotify("Galer\xC3\xAD" "a", msg);
    return true;
  }
  return false;
}

// Si la foto que se edita se protege, se borra o se mueve, el editor lo
// nota en cuanto cambia el catalogo.
static void gedValidate(){
  if(!gedActive() || !gMlOk) return;
  uint32_t rv = mlRev();
  if(rv == gedSeenRev) return;
  gedSeenRev = rv;
  FlexMlRec r;
  bool have = mlGet(gedId, &r);
  if(have && !(r.flags & FML_R_LOCKED)){
    if(strcmp(r.path, gedPath)){                   // renombrada: se sigue editando
      snprintf(gedPath, sizeof(gedPath), "%s", r.path);
      snprintf(gedName, sizeof(gedName), "%s", r.name);
    }
    return;
  }
  gedAbort(have ? "La foto se ha protegido: el editor se ha cerrado sin guardar."
                : "La foto ya no existe: el editor se ha cerrado.");
}

// Desde loop(): publica un guardado que termino con la Galeria en segundo
// plano y vigila el catalogo. Con el editor cerrado no cuesta nada.
static void gedBgTick(){
  gedReclaim();
  if(!gedActive()) return;
  if(gedTicking()) return;                         // su tick corre: lo lleva gedTick
  gedCollect();
  gedValidate();
}

// ¿Hay trabajo real en curso? (APP_BG_KEEP: no se desaloja la Galeria
// mientras se guarda.)
static bool gedBusy(){ return gedJobOn; }
static bool gedDirty(){ return gedPhase == GED_EDIT && gedE && flexIeCanUndo(gedE) && !flexIeIsIdentity(gedE); }

// SOLTAR (Galeria suspendida): la foto se suelta y se relee al volver; el
// estado y el historial se quedan (todo esta normalizado).
static size_t gedShed(){
  if(gedPhase != GED_EDIT || gedJobOn || !gedBase) return 0;
  size_t n = (size_t)gedW * gedH * 3 + (size_t)gedPW * gedPH * 3 + gedViewCap;
  gedFreeImage();
  gedReopen = true;
  return n;
}
static void gedSuspend(){
  gedDrag = GD_NONE; gedStrokeOv = -1; gedLivePending = false;
}
static void gedResume(){
  if(!gedActive()) return;
  gedValidate();
  if(!gedActive()) return;
  if(gedPhase == GED_EDIT && gedReopen) gedStartOpen();
  gedChanged();
  gedRender();
}

// #############################################################
// ##  ACCIONES
// #############################################################
static void gedAfterChange(){
  gedChanged();
  GedGeom g = gedGeom();
  setBuf(fb);
  gedDrawTop(g);
  gedDrawWell(false);
  gedDrawPanel(g);
  flxFlush(WIN_TOP, WIN_BOT);
}

static void gedSetTool(int t){
  if(t == gedTool) return;
  if(gedTextOn){ gedTextOn = false; gedText[0] = 0; }
  bool viewChanges = (t == GT_CROP) != (gedTool == GT_CROP);
  gedTool = (uint8_t)t;
  if(viewChanges) gedViewOk = false;
  gedRender();
}

// Recorte con proporcion fija, centrado y lo mas grande posible.
static void gedApplyAspect(int a){
  gedAspect = (uint8_t)a;
  float r = GED_ASPECT_R[a];
  if(r == 0.0f){ gedRender(); return; }               // Libre: solo quita el bloqueo
  int ow, oh; if(gedE->st.rot & 1){ ow = gedE->H; oh = gedE->W; } else { ow = gedE->W; oh = gedE->H; }
  if(r < 0.0f){ flexIeSetCrop(gedE, 0, 0, 1, 1); gedAfterChange(); return; }
  float w = 1.0f, h = (float)ow / (r * oh);
  if(h > 1.0f){ h = 1.0f; w = r * oh / ow; }
  flexIeSetCrop(gedE, (1 - w) / 2, (1 - h) / 2, (1 + w) / 2, (1 + h) / 2);
  gedAfterChange();
}

static void gedStartSave(uint8_t mode){
  int w, h; flexIeOutSize(gedE, &w, &h);
  // Espacio: ~0,5 bytes por pixel a calidad 92 (de sobra), mas margen y la
  // reserva que la particion no cede nunca.
  uint32_t need = (uint32_t)((uint64_t)w * h / 2) + 128u * 1024u + FML_RESERVE_BYTES;
  uint32_t tot = flexFsTotalBytes(), used = flexFsUsedBytes(), fr = used < tot ? tot - used : 0;
  if(fr < need){
    char m[160];
    snprintf(m, sizeof(m), "Hacen falta unos %u KB libres y quedan %u KB. Libera espacio en Almacenamiento.",
             (unsigned)(need / 1024u), (unsigned)(fr / 1024u));
    gedSheet = false; gedRender();
    mmDlgOpen("No hay espacio", m, "Aceptar", "", false); gedAsk = GA_INFO;
    return;
  }
  if(memFreePsram() < 1024u * 1024u + (uint32_t)w * 16u){
    gedSheet = false; gedRender();
    mmDlgOpen("Sin memoria", "No hay memoria libre para codificar la foto ahora.", "Aceptar", "", false); gedAsk = GA_INFO;
    return;
  }
  snprintf(gedJob.path, sizeof(gedJob.path), FML_DIR_TMP "/ed-%lu.jpg", (unsigned long)gedId);
  gedJob.outW = w; gedJob.outH = h;
  gedSaveMode = mode; gedSheet = false;
  gedPhase = GED_SAVING;
  Serial.printf("[editor] guardando %s %dx%d\n", mode == GS_REPLACE ? "(reemplazar)" : "(copia)", w, h);
  gedJobStart(GJ_SAVE);
  gedRender();
}

// ATRAS: primero las capas propias; luego, salir (preguntando si hay cambios).
static bool gedBack(){
  if(!gedActive()) return false;
  if(gedPhase == GED_OPENING || gedPhase == GED_SAVING){ gedJob.cancel = 1; gedRender(); return true; }
  if(gedSheet){ gedSheet = false; gedRender(); return true; }
  if(gedTextOn){ gedTextOn = false; gedText[0] = 0; gedRender(); return true; }
  if(gedDirty()){
    mmDlgOpen("\xC2\xBF" "Descartar los cambios?", "Se perder\xC3\xA1n los cambios que no has guardado.", "Descartar", "Seguir editando", true);
    gedAsk = GA_DISCARD;
    return true;
  }
  gedCloseNow();
  galRender();
  return true;
}

static void gedDlgResult(int r){
  uint8_t a = gedAsk; gedAsk = GA_NONE;
  if(a == GA_DISCARD && r == 1){ gedCloseNow(); galRender(); return; }
  if(a == GA_REPLACE && r == 1){ gedStartSave(GS_REPLACE); return; }
  if(a == GA_REPLACE){ gedSheet = true; }
  if(gedActive()) gedRender(); else galRender();
}

// ---- Toques en la foto ----
static void gedWellPress(const GedGeom& g){
  if(!gedViewOk || T.y < g.wellY || T.y >= g.wellY + g.wellH) return;
  float u = (float)(T.x - gedVX) / gedVW, v = (float)(T.y - gedVY) / gedVH;
  bool inside = u >= 0 && u <= 1 && v >= 0 && v <= 1;
  if(gedTool == GT_CROP){
    int x0, y0, x1, y1; gedCropRectPx(gedE->st.c0x, gedE->st.c0y, gedE->st.c1x, gedE->st.c1y, x0, y0, x1, y1);
    const int cx[4] = { x0, x1, x0, x1 }, cy[4] = { y0, y0, y1, y1 };
    gedCropH = -1;
    for(int k = 0; k < 4 && gedCropH < 0; k++) if(abs(T.x - cx[k]) <= 30 && abs(T.y - cy[k]) <= 30) gedCropH = k;
    if(gedCropH < 0 && GED_ASPECT_R[gedAspect] == 0.0f){   // lados: solo sin proporcion fija
      if(abs(T.y - y0) <= 18 && T.x > x0 && T.x < x1) gedCropH = 4;
      else if(abs(T.y - y1) <= 18 && T.x > x0 && T.x < x1) gedCropH = 5;
      else if(abs(T.x - x0) <= 18 && T.y > y0 && T.y < y1) gedCropH = 6;
      else if(abs(T.x - x1) <= 18 && T.y > y0 && T.y < y1) gedCropH = 7;
    }
    if(gedCropH < 0 && T.x > x0 && T.x < x1 && T.y > y0 && T.y < y1) gedCropH = 8;
    if(gedCropH < 0) return;
    gedDrag = GD_CROP; gedDX0 = T.x; gedDY0 = T.y;
    gedK0x = gedC0x = gedE->st.c0x; gedK0y = gedC0y = gedE->st.c0y;
    gedK1x = gedC1x = gedE->st.c1x; gedK1y = gedC1y = gedE->st.c1y;
    return;
  }
  if(!inside) return;
  if(gedTool == GT_DRAW){
    int ov = GED_KIND_OV[gedKind];
    if(ov == FLEXIE_OV_STROKE){
      gedStrokeOv = flexIeStrokeBegin(gedE, GED_RGB[gedColor], GED_WIDTH[gedWidth], u, v);
      if(gedStrokeOv < 0){ mmDlgOpen("Sin sitio", "Esta foto ya tiene todos los trazos y figuras que caben. Guarda y sigue editando la copia.", "Aceptar", "", false); gedAsk = GA_INFO; return; }
      gedDrag = GD_STROKE; gedPrevX = T.x; gedPrevY = T.y;
      setBuf(fb);
      int ox0 = gClipY0, ox1 = gClipY1; gClipY0 = gedVY; gClipY1 = gedVY + gedVH - 1;
      fillCircleAA((float)T.x, (float)T.y, gedWidthPx(), gedColor565());
      gClipY0 = ox0; gClipY1 = ox1;
      flxFlush(T.y - 30, T.y + 30);
    } else {
      gedDrag = GD_SHAPE; gedC0x = gedC1x = u; gedC0y = gedC1y = v;
    }
    return;
  }
  if(gedTool == GT_TEXT && gedTextOn){
    gedDrag = GD_TEXT; gedDX0 = T.x; gedDY0 = T.y; gedK0x = gedTextU; gedK0y = gedTextV;
  }
}

static void gedCropMove(){
  float dx = (float)(T.x - gedDX0) / gedVW, dy = (float)(T.y - gedDY0) / gedVH;
  float a0 = gedK0x, b0 = gedK0y, a1 = gedK1x, b1 = gedK1y;
  const float mnx = (float)GED_MIN_CROP_PX / gedVW, mny = (float)GED_MIN_CROP_PX / gedVH;
  switch(gedCropH){
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
  if(gedCropH < 8){
    if(a0 < 0) a0 = 0;
    if(b0 < 0) b0 = 0;
    if(a1 > 1) a1 = 1;
    if(b1 > 1) b1 = 1;
    if(a1 - a0 < mnx){ if(gedCropH == 0 || gedCropH == 2 || gedCropH == 6) a0 = a1 - mnx; else a1 = a0 + mnx; }
    if(b1 - b0 < mny){ if(gedCropH <= 1 || gedCropH == 4) b0 = b1 - mny; else b1 = b0 + mny; }
    // Proporcion fija: manda el ancho; el alto se ajusta desde la esquina opuesta.
    float r = GED_ASPECT_R[gedAspect];
    if(r != 0.0f && gedCropH < 4){
      int ow, oh; if(gedE->st.rot & 1){ ow = gedE->H; oh = gedE->W; } else { ow = gedE->W; oh = gedE->H; }
      if(r < 0.0f) r = (float)ow / oh;
      float h = (a1 - a0) * ow / (r * oh);
      bool top = gedCropH <= 1;
      if(top){ b0 = b1 - h; if(b0 < 0){ b0 = 0; h = b1; float w = h * r * oh / ow; if(gedCropH == 0) a0 = a1 - w; else a1 = a0 + w; } }
      else   { b1 = b0 + h; if(b1 > 1){ b1 = 1; h = 1 - b0; float w = h * r * oh / ow; if(gedCropH == 2) a0 = a1 - w; else a1 = a0 + w; } }
    }
  }
  gedC0x = a0; gedC0y = b0; gedC1x = a1; gedC1y = b1;
}

static void gedWellDrag(){
  if(gedDrag == GD_STROKE){
    int ddx = T.x - gedPrevX, ddy = T.y - gedPrevY;
    if(ddx * ddx + ddy * ddy < 4) return;
    float u = (float)(T.x - gedVX) / gedVW, v = (float)(T.y - gedVY) / gedVH;
    if(!flexIeStrokeAdd(gedE, gedStrokeOv, u, v)) return;         // tabla llena: el trazo se queda como esta
    setBuf(fb);
    int ox0 = gClipY0, ox1 = gClipY1; gClipY0 = gedVY; gClipY1 = gedVY + gedVH - 1;
    int sx0 = gClipX0, sx1 = gClipX1; gClipX0 = gedVX; gClipX1 = gedVX + gedVW - 1;
    float r = gedWidthPx();
    strokeSegAA((float)gedPrevX, (float)gedPrevY, (float)T.x, (float)T.y, r, gedColor565());
    gClipY0 = ox0; gClipY1 = ox1; gClipX0 = sx0; gClipX1 = sx1;
    int y0 = gedPrevY < T.y ? gedPrevY : T.y, y1 = gedPrevY < T.y ? T.y : gedPrevY;
    flxFlush(y0 - (int)r - 2, y1 + (int)r + 2);
    gedPrevX = T.x; gedPrevY = T.y;
    return;
  }
  if(gedDrag == GD_SHAPE){
    float u = (float)(T.x - gedVX) / gedVW, v = (float)(T.y - gedVY) / gedVH;
    gedC1x = u < 0 ? 0 : (u > 1 ? 1 : u); gedC1y = v < 0 ? 0 : (v > 1 ? 1 : v);
  } else if(gedDrag == GD_CROP){
    gedCropMove();
  } else if(gedDrag == GD_TEXT){
    gedTextU = gedK0x + (float)(T.x - gedDX0) / gedVW; gedTextV = gedK0y + (float)(T.y - gedDY0) / gedVH;
    if(gedTextU < 0.0f) gedTextU = 0.0f;               // lo que se ve es lo que se guarda
    if(gedTextU > 0.98f) gedTextU = 0.98f;
    if(gedTextV < 0.0f) gedTextV = 0.0f;
    if(gedTextV > 0.95f) gedTextV = 0.95f;
  } else return;
  if(millis() - gedLiveMs < GED_LIVE_MS){ gedLivePending = true; return; }
  gedLiveMs = millis(); gedLivePending = false;
  gedDrawWell(true);
}

static void gedWellRelease(){
  uint8_t d = gedDrag; gedDrag = GD_NONE;
  if(d == GD_STROKE){ flexIeStrokeEnd(gedE, gedStrokeOv); gedStrokeOv = -1; gedAfterChange(); return; }
  if(d == GD_SHAPE){
    float du = (gedC1x - gedC0x) * gedVW, dv = (gedC1y - gedC0y) * gedVH;
    if(du * du + dv * dv >= 36.0f)                   // un toque sin arrastre no crea nada
      flexIeAddShape(gedE, GED_KIND_OV[gedKind], GED_RGB[gedColor], GED_WIDTH[gedWidth], gedFill, gedC0x, gedC0y, gedC1x, gedC1y);
    gedAfterChange();
    return;
  }
  if(d == GD_CROP){
    if(gedC0x != gedE->st.c0x || gedC0y != gedE->st.c0y || gedC1x != gedE->st.c1x || gedC1y != gedE->st.c1y)
      flexIeSetCrop(gedE, gedC0x, gedC0y, gedC1x, gedC1y);
    gedAfterChange();
    return;
  }
  if(d == GD_TEXT) gedDrawWell(true);
}

// ---- Toques en el panel ----
static bool gedSliderPress(const GedGeom& g){
  int sy = gedTool == GT_ADJ ? GED_SLIDER_Y(g) : gedTool == GT_FILTER ? GED_SLIDER_Y(g) + 4 : -1000;
  if(sy < 0 || abs(T.y - sy) > 20) return false;
  int x0, x1; gedSliderGeom(g, sy, x0, x1);
  if(T.x < x0 - 16 || T.x > x1 + 16) return false;
  if(gedTool == GT_FILTER && gedE->st.filter == FLEXIE_FILTER_NONE) return false;
  gedDrag = GD_SLIDER;
  return true;
}
static void gedSliderDrag(const GedGeom& g){
  if(gedTool == GT_ADJ){
    int v = gedSliderValue(g, GED_SLIDER_Y(g), T.x, -100, 100);
    if(abs(v) < 4) v = 0;                                      // el cero "engancha"
    if(v == gedE->st.adj[gedAdj]) return;
    flexIeAdjustLive(gedE, gedAdj, v);
  } else {
    int v = gedSliderValue(g, GED_SLIDER_Y(g) + 4, T.x, 0, 100);
    if(v == gedE->st.filterAmt) return;
    flexIeFilterLive(gedE, gedE->st.filter, v);
  }
  gedViewOk = false;
  setBuf(fb);
  gedDrawPanel(g);
  flxFlush(g.panelY - 4, g.tabsY - 1);
  if(millis() - gedLiveMs < GED_LIVE_MS){ gedLivePending = true; return; }
  gedLiveMs = millis(); gedLivePending = false;
  gedDrawWell(true);
}
static void gedSliderRelease(){
  gedDrag = GD_NONE;
  const FlexIeState* h = &gedE->hist[gedE->cur];
  if(memcmp(h->adj, gedE->st.adj, sizeof(h->adj)) || h->filter != gedE->st.filter || h->filterAmt != gedE->st.filterAmt)
    flexIeCommit(gedE);
  gedLivePending = false;
  gedAfterChange();
}

static void gedPanelTap(const GedGeom& g){
  int tx = T.x, ty = T.y;
  switch(gedTool){
    case GT_CROP:
      if(ty >= g.panelY + 6 && ty <= g.panelY + 54){
        int i = gedRowHit(g, 4, tx);
        if(i == 0) flexIeRotate(gedE, -1); else if(i == 1) flexIeRotate(gedE, +1);
        else if(i == 2) flexIeFlip(gedE, true); else if(i == 3) flexIeFlip(gedE, false);
        if(i >= 0) gedAfterChange();
      } else if(ty >= g.panelY + 56 && ty <= g.panelY + 94){
        int i = gedRowHit(g, GED_ASPECTS, tx);
        if(i >= 0) gedApplyAspect(i);
      }
      break;
    case GT_ADJ:
      if(ty >= g.panelY + 4 && ty <= g.panelY + 78){
        int i = gedRowHit(g, 4, tx), row = (ty - g.panelY - 4) / 36;
        if(i >= 0 && row >= 0 && row < 2){ gedAdj = (uint8_t)(row * 4 + i); setBuf(fb); gedDrawPanel(g); flxFlush(g.panelY - 4, g.tabsY - 1); }
      } else if(abs(ty - GED_SLIDER_Y(g)) <= 20 && tx > g.panelX + g.panelW - 86 && gedE->st.adj[gedAdj]){
        flexIeAdjustLive(gedE, gedAdj, 0); flexIeCommit(gedE); gedAfterChange();   // tocar el valor: a cero
      }
      break;
    case GT_FILTER:
      for(int f = 0; f < FLEXIE_FILTER_N; f++){
        int x, y, side; gedFilterGeom(g, f, x, y, side);
        if(tx >= x - 3 && tx <= x + side + 3 && ty >= y - 3 && ty <= y + side + 18){
          if(f != gedE->st.filter){
            flexIeFilterLive(gedE, f, gedE->st.filter == FLEXIE_FILTER_NONE ? 100 : gedE->st.filterAmt);
            if(f == FLEXIE_FILTER_NONE) gedE->st.filterAmt = 100;
            flexIeCommit(gedE); gedAfterChange();
          }
          return;
        }
      }
      break;
    case GT_DRAW: {
      if(ty >= g.panelY + 4 && ty <= g.panelY + 42){
        int i = gedRowHit(g, GED_KINDS, tx);
        if(i >= 0){ gedKind = (uint8_t)i; setBuf(fb); gedDrawPanel(g); flxFlush(g.panelY - 4, g.tabsY - 1); }
        return;
      }
      int c = gedColorHit(g, tx, ty);
      if(c >= 0){ gedColor = (uint8_t)c; setBuf(fb); gedDrawPanel(g); flxFlush(g.panelY - 4, g.tabsY - 1); return; }
      int w = gedTrioHit(g, tx, ty);
      if(w >= 0){ gedWidth = (uint8_t)w; setBuf(fb); gedDrawPanel(g); flxFlush(g.panelY - 4, g.tabsY - 1); return; }
      bool shape = GED_KIND_OV[gedKind] == FLEXIE_OV_RECT || GED_KIND_OV[gedKind] == FLEXIE_OV_ELLIPSE;
      if(shape && ty >= g.panelY + 88 && ty <= g.panelY + 120 && tx <= g.panelX + 124){
        gedFill = !gedFill; setBuf(fb); gedDrawPanel(g); flxFlush(g.panelY - 4, g.tabsY - 1);
      }
      break;
    }
    case GT_TEXT: {
      if(ty >= g.panelY + 4 && ty <= g.panelY + 46){
        if(!gedTextOn){ gedTextAsk = true; fkNameOpenHint("Texto", "", "Escribe el texto y pulsa Guardar"); return; }
        int i = gedRowHit(g, 2, tx);
        if(i == 0){ gedTextOn = false; gedText[0] = 0; gedRender(); }
        else if(i == 1){
          float hOut = GED_TEXT_H[gedTextSz];
          if(flexIeAddText(gedE, gedText, GED_RGB[gedColor], hOut, gedTextU, gedTextV) < 0){
            mmDlgOpen("Sin sitio", "Esta foto ya tiene todos los trazos, figuras y textos que caben.", "Aceptar", "", false); gedAsk = GA_INFO;
          }
          gedTextOn = false; gedText[0] = 0;
          gedAfterChange();
        }
        return;
      }
      int c = gedColorHit(g, tx, ty);
      if(c >= 0){ gedColor = (uint8_t)c; gedRender(); return; }
      int s = gedTrioHit(g, tx, ty);
      if(s >= 0){ gedTextSz = (uint8_t)s; gedRender(); return; }
      break;
    }
    default:                                                  // Tamano
      if(ty >= g.panelY + 46 && ty <= g.panelY + 84){
        int i = gedRowHit(g, GED_SIZES, tx);
        if(i < 0) return;
        int ow, oh; uint16_t keep = gedE->st.longSide;
        gedE->st.longSide = 0; flexIeOutSize(gedE, &ow, &oh); gedE->st.longSide = keep;
        int L = ow > oh ? ow : oh;
        flexIeSetLongSide(gedE, i == 0 ? 0 : L * GED_SIZE_PCT[i] / 100);
        gedAfterChange();
      } else if(ty >= g.panelY + 86 && ty <= g.panelY + 118 && !flexIeIsIdentity(gedE)){
        flexIeReset(gedE); gedAspect = 0; gedAfterChange();   // se puede deshacer
      }
      break;
  }
}

// #############################################################
// ##  TICK (Galeria en primer plano)
// #############################################################
static void gedTick(){
  gedTickMs = millis() | 1u;
  if(gedCollect()){ if(gedActive() && !mmDlgOn) gedRender(); return; }
  gedValidate();
  if(!gedActive()) return;
  // Teclado del texto.
  if(fkNameOn && gedTextAsk){
    int r = fkNameTick();
    if(r == 1){
      // Se corta en un limite de caracter UTF-8, nunca a mitad.
      size_t n = strlen(fkNameBuf);
      if(n >= sizeof(gedText)){ n = sizeof(gedText) - 1; while(n > 0 && (fkNameBuf[n] & 0xC0) == 0x80) n--; }
      memcpy(gedText, fkNameBuf, n); gedText[n] = 0;
      gedTextOn = gedText[0] != 0; gedTextU = 0.08f; gedTextV = 0.42f;
    }
    if(r != 0){ gedTextAsk = false; mkRedrawAll(); }
    return;
  }
  if(mmDlgOn){ int r = mmDlgTick(); if(r) gedDlgResult(r); return; }
  GedGeom g = gedGeom();
  if(gedPhase == GED_OPENING || gedPhase == GED_SAVING){
    if(T.tap && !gedJob.cancel && gedProgressCancelHit(g, T.x, T.y)){ gedJob.cancel = 1; gedShownPct = -1; }
    int pct = gedJob.pct;
    if(pct != gedShownPct && millis() - gedShownMs >= 100){
      setBuf(fb);
      int x, y, w, h; gedCardGeom(g, x, y, w, h);
      gedDrawProgress(g);
      flxFlush(y - 2, y + h + 2);
    }
    return;
  }
  if(gedSheet){
    if(!T.tap) return;
    for(int i = 0; i < 3; i++){
      int x, y, w, h; gedSheetBtn(g, i, x, y, w, h);
      if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + h){
        if(i == 0){ gedStartSave(GS_COPY); return; }
        if(i == 1){
          gedSheet = false;
          char m[240];
          if(gedSrcW != gedW || gedSrcH != gedH)
            snprintf(m, sizeof(m), "La original (%d x %d) se sustituir\xC3\xA1 por la versi\xC3\xB3n editada, que el P4 guarda a menor resoluci\xC3\xB3n. No se puede deshacer.", gedSrcW, gedSrcH);
          else snprintf(m, sizeof(m), "La foto original se sustituir\xC3\xA1 por la versi\xC3\xB3n editada. No se puede deshacer.");
          gedRender();
          mmDlgOpen("\xC2\xBF" "Reemplazar la original?", m, "Reemplazar", "Cancelar", true);
          gedAsk = GA_REPLACE;
          return;
        }
        gedSheet = false; gedRender(); return;
      }
    }
    int x, y, w, h; gedSheetGeom(g, x, y, w, h);
    if(T.y < y){ gedSheet = false; gedRender(); }
    return;
  }
  // Vista en vivo pendiente (el ultimo movimiento cayo dentro del limite).
  if(gedLivePending && millis() - gedLiveMs >= GED_LIVE_MS){ gedLiveMs = millis(); gedLivePending = false; gedDrawWell(true); }

  if(T.pressed && gedDrag == GD_NONE){
    if(T.y >= g.panelY && T.y < g.tabsY){ gedSliderPress(g); }
    else if(T.y >= g.wellY && T.y < g.wellY + g.wellH){ gedWellPress(g); }
  }
  if(gedDrag != GD_NONE){
    if(T.down){
      if(gedDrag == GD_SLIDER) gedSliderDrag(g); else gedWellDrag();
      return;
    }
    if(gedDrag == GD_SLIDER) gedSliderRelease(); else gedWellRelease();
    return;
  }
  if(!T.tap) return;
  // Barra de arriba.
  if(T.y >= g.topY && T.y < g.topY + GED_TOP_H){
    int cy = g.topY + GED_TOP_H / 2;
    if(abs(T.y - cy) <= 24 && abs(T.x - (g.bx + g.pad + 18)) <= 24){ if(flexIeUndo(gedE)){ gedTextOn = false; gedAfterChange(); } return; }
    if(abs(T.y - cy) <= 24 && abs(T.x - (g.bx + g.pad + 66)) <= 24){ if(flexIeRedo(gedE)){ gedTextOn = false; gedAfterChange(); } return; }
    int x, y, w, h; gedSaveBtnGeom(g, x, y, w, h);
    if(T.x >= x - 6 && T.x <= x + w + 6 && !flexIeIsIdentity(gedE)){
      if(gedTextOn){ gedTextOn = false; gedText[0] = 0; }
      gedSheet = true; gedRender();
    }
    return;
  }
  // Pestanas.
  if(T.y >= g.tabsY){
    int t = (T.x - g.bx) / (g.bw / GT_N);
    if(t >= 0 && t < GT_N) gedSetTool(t);
    return;
  }
  if(T.y >= g.panelY) gedPanelTap(g);
}
