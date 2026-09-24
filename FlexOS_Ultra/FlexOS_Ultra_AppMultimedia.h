// #############################################################
// ##  FLEX OS ULTRA  ·  APP MULTIMEDIA  ·  reproductor real
// ##  ----------------------------------------------------------
// ##  Reproduccion de ficheros locales (JPEG, AVI/MJPEG, WAV), volumen,
// ##  orientacion, pantalla completa y liberacion al suspender.
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
// ##  APP MULTIMEDIA  ·  REPRODUCTOR REAL DE ARCHIVOS LOCALES
// ##  ------------------------------------------------------
// ##  QUE REPRODUCE DE VERDAD
// ##    · JPEG baseline desde la memoria interna.
// ##    · Video AVI con pista MJPEG: cada fotograma es un JPEG
// ##      completo y pasa por FlexOS_JPEG, que ya esta probado
// ##      contra libjpeg-turbo. No hay prediccion entre cuadros,
// ##      asi que se puede empezar a ver en cualquier punto y
// ##      SALTAR fotogramas sin que la imagen se rompa -- que es
// ##      justo lo que permite ir tarde sin congelar el sistema.
// ##    · El AUDIO no: se escucha en Musica, que sigue sonando
// ##      aunque se salga de ella. Un archivo de audio que llegue
// ##      aqui (desde el Explorador) se pasa a Musica.
// ##
// ##  DE DONDE SALE LA LISTA. Del catalogo de la biblioteca (el
// ##  mismo de la Galeria y de Flex Web Server): fotos, videos y
// ##  dibujos. Lo protegido sale con un candado y pide la clave del
// ##  sistema; anterior/siguiente en el visor no pasa por ello.
// ##
// ##  QUE NO, Y POR QUE. MP4/H.264 no se anuncia ni se intenta:
// ##  esta placa no tiene decodificador de video por hardware y en
// ##  software no da el ritmo. Un archivo asi se abre y se explica,
// ##  no se reproduce a medias.
// ##
// ##  MEMORIA. No existe el fichero entero en RAM en ningun
// ##  momento. Se mantiene:
// ##    · lectura incremental del archivo,
// ##    · UN buffer para el fotograma comprimido (VID_FRAME_CAP),
// ##    · y nada mas: el JPEG se decodifica DIRECTAMENTE sobre el
// ##      framebuffer, fila a fila. Por eso no hay doble buffer de
// ##      video (el esqueleto anterior gastaba 2 x 220 KB de PSRAM
// ##      en un patron sintetico).
// ##
// ##  RITMO. Ni un delay(). El siguiente fotograma se decide
// ##  comparando micros() contra el instante teorico; si se va
// ##  tarde se SALTAN fotogramas leyendo solo sus cabeceras (8
// ##  bytes), con un tope por vuelta para que la interfaz no se
// ##  quede sin turno.
// #############################################################

// ---- Presupuestos, todos acotados y con motivo ----
// Un fotograma MJPEG de 640x480 con calidad normal ronda los 40 KB;
// 192 KB deja sitio de sobra para 720p y para picos de calidad, y
// es lo unico grande que reserva el reproductor de VIDEO.
#define VID_FRAME_CAP     (192 * 1024)
// Tope de una FOTO. El decodificador necesita los bytes comprimidos
// completos (decodifica por filas, pero lee de un buffer), asi que
// una foto si se carga entera -- es la unica excepcion, y por eso
// lleva tope y comprobacion de memoria libre. 6 MB cubre de sobra un
// JPEG de 12 megapixeles; por encima se dice el motivo en vez de
// intentar una reserva que dejaria al sistema sin PSRAM.
#define VID_PHOTO_CAP     (6 * 1024 * 1024)
// Margen de PSRAM que NUNCA se toca, para que abrir una foto grande
// no deje sin memoria al resto del sistema.
#define VID_PSRAM_MARGIN  (512 * 1024)
// Repintados por segundo como mucho al desplazar una foto ampliada.
// Cada uno es una decodificacion: sin este freno, arrastrar el dedo
// pediria una por cada movimiento del tactil.
#define VID_PAN_MS        90
// Fotogramas que se pueden saltar SEGUIDOS para recuperar ritmo.
// Con un tope, ir tarde se nota como un video que salta; sin el, un
// archivo mas pesado que la placa dejaria la interfaz sin turno.
#define VID_MAX_CATCHUP   4
// Filas de imagen que se acumulan antes de volcarlas giradas. Ver
// vidLandFlush: convierte 800 escrituras sueltas por fila en tiras
// contiguas de 8 pixeles.
#define VIDT_ROWS         8
#define VID_CTRL_HIDE_MS  3500       // los controles se ocultan solos
#define VID_SEEK_STEP_MS  10000      // adelantar/retroceder 10 s

// Pantallas de la app.
#define VS_LIST   0
#define VS_VIEW   1

// Clase de lo que hay abierto en el visor.
#define VK_NONE   0
#define VK_PHOTO  1
#define VK_VIDEO  2
#define VK_ERROR  4

// ---- Estado de la lista ----
static int   vidScreen   = VS_LIST;
static int   vidFilter   = 0;          // 0 todo, 1 videos, 2 fotos (el audio va a Musica)
static int   vidListScroll = 0;
static int   vidListDragY0 = 0, vidListDragS0 = 0;
static bool  vidListDragging = false;

// ---- Estado del visor ----
static char       vidPath[FLEXMED_PATH_MAX] = "";
static char       vidName[FLEXFS_NAME_MAX]  = "";
static uint8_t    vidKind      = VK_NONE;
static char       vidErrMsg[72] = "";
static bool       vidLand      = false;     // orientacion EFECTIVA en curso
static MediaStream vidStream;
static FlexAviCtx  vidAvi;
static uint8_t*   vidFrameBuf  = NULL;
static uint8_t*   vidPhotoBuf  = NULL;      // JPEG comprimido de la foto abierta
static uint32_t   vidPhotoLen  = 0;
static uint16_t*  vidTile      = NULL;      // tira para el volcado girado
static bool       vidPlaying   = false;
static bool       vidEnded     = false;
static uint32_t   vidCurFrame  = 0;
static unsigned long vidNextUs = 0;
static unsigned long vidCtrlMs = 0;         // instante del ultimo toque
static bool       vidCtrlOn    = true;
static uint32_t   vidCurId     = 0;         // id del catalogo de lo abierto (0 = abierto por ruta, sin registro)
static bool       vidPanning   = false;     // arrastrando una foto ampliada

// GEOMETRIA DEL CONTENIDO, en coordenadas del lienzo LOGICO.
//   vidSrcW/H : tamano real del medio (para decidir orientacion y proporcion)
//   vidBox*   : el hueco en pantalla donde entra AJUSTADO sin deformar
//   vidDW/DH  : tamano al que se pide la decodificacion = caja * zoom
//   vidPan*   : que trozo de esa imagen ampliada se esta viendo
// Con zoom 1 y sin desplazamiento, vidD* == vidBox* y el camino es el
// mismo de siempre: el zoom no anade ningun coste cuando no se usa.
static int vidSrcW = 0, vidSrcH = 0;
static int vidBoxX = 0, vidBoxY = 0, vidBoxW = 0, vidBoxH = 0;
static int vidDW = 0, vidDH = 0;
static int vidPanX = 0, vidPanY = 0;
static int vidZoom = 1;                    // 1..VID_ZOOM_MAX
#define VID_ZOOM_MAX 4

// ---- Medicion REAL de rendimiento ----
// No se anuncia ningun numero que no salga de aqui. Se acumula por
// ventanas de un segundo y solo se imprime cuando hay algo que
// contar, para no llenar Serial ni pagar el coste de imprimir en
// cada cuadro.
static uint32_t vidStatFrames = 0, vidStatSkipped = 0, vidStatErrors = 0;
static unsigned long vidStatMs = 0;
static uint16_t vidFpsReal = 0;             // ultimo valor medido (x10)
static uint32_t vidDecodeUs = 0;            // coste del ultimo fotograma

// Declaraciones adelantadas. El reproductor es un grafo de estados
// con ciclos naturales (buscar repinta, repintar puede fallar y
// volver a la lista), asi que unas cuantas funciones se llaman entre
// si en los dos sentidos.
static void vidRenderAll();
static void vidListRender();
static void vidRenderViewer();
static void vidDrawControls(bool publish);
static void vidDrawCurrentFrame(bool publish);
static void vidCycleOrientation();
static void vidOpenNeighbour(int delta);
static void vidSeekToMs(uint32_t ms);
static bool vidOpenPath(const char* path);
static void vidReleaseMedia(bool keepPosition);
static void vidFail(const char* why);
static void vidLayout(int srcW, int srcH);
static void vidClampPan();

// -------------------------------------------------------------
//  Reanudacion por archivo
//  ------------------------------------------------------------
//  Se guarda el fotograma por el que iba cada archivo, identificado
//  por una huella de su ruta. Una tabla corta y fija: recordar los
//  ocho ultimos es lo util, y no crece sin control.
// -------------------------------------------------------------
#define VID_RESUME_N 8
// (el nombre lleva 'Tab' porque vidResume() ya es el gancho de
//  ciclo de vida de la app: dos cosas distintas, dos nombres)
struct VidResumeSlot { uint32_t key; uint32_t frame; uint32_t whenMs; };
static VidResumeSlot vidResumeTab[VID_RESUME_N];

static uint32_t vidKeyOf(const char* path){
  uint32_t h = 2166136261u;
  for(const char* p = path; p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return h ? h : 1u;
}
static uint32_t vidResumeGet(const char* path){
  uint32_t k = vidKeyOf(path);
  for(int i = 0; i < VID_RESUME_N; i++) if(vidResumeTab[i].key == k) return vidResumeTab[i].frame;
  return 0;
}
static void vidResumeSet(const char* path, uint32_t frame){
  uint32_t k = vidKeyOf(path);
  int slot = -1;
  for(int i = 0; i < VID_RESUME_N; i++) if(vidResumeTab[i].key == k){ slot = i; break; }
  if(slot < 0){                                  // el hueco mas viejo cede su sitio
    slot = 0;
    for(int i = 1; i < VID_RESUME_N; i++)
      if(vidResumeTab[i].key == 0 || vidResumeTab[i].whenMs < vidResumeTab[slot].whenMs) slot = i;
  }
  vidResumeTab[slot].key    = k;
  vidResumeTab[slot].frame  = frame;
  vidResumeTab[slot].whenMs = millis();
}

// -------------------------------------------------------------
//  LIBERACION
//  ------------------------------------------------------------
//  UN solo sitio suelta TODO lo del visor. Lo llaman: cerrar el
//  archivo, volver a la lista, cambiar de video, suspender y cerrar
//  la app. Tener un unico camino evita dejar recursos reservados.
// -------------------------------------------------------------
static void vidReleaseMedia(bool keepPosition){
  if(vidKind == VK_VIDEO && keepPosition && vidPath[0] && !vidEnded)
    vidResumeSet(vidPath, vidCurFrame);
  vidPlaying = false;
  mediaStreamClose(&vidStream);
  memset(&vidAvi, 0, sizeof(vidAvi));
  if(vidFrameBuf){ mediaFree(vidFrameBuf); vidFrameBuf = NULL; }
  if(vidPhotoBuf){ mediaFree(vidPhotoBuf); vidPhotoBuf = NULL; vidPhotoLen = 0; }
  if(vidTile){ heap_caps_free(vidTile); vidTile = NULL; }
  vidKind    = VK_NONE;
  vidEnded   = false;
  vidCurFrame = 0;
}

// -------------------------------------------------------------
//  MAQUETACION DEL LIENZO
// -------------------------------------------------------------
static void vidClampPan(){
  int mx = vidDW - vidBoxW, my = vidDH - vidBoxH;
  if(mx < 0) mx = 0;
  if(my < 0) my = 0;
  if(vidPanX < 0) vidPanX = 0;
  if(vidPanY < 0) vidPanY = 0;
  if(vidPanX > mx) vidPanX = mx;
  if(vidPanY > my) vidPanY = my;
}

static void vidLayout(int srcW, int srcH){
  vidSrcW = srcW; vidSrcH = srcH;
  int cw = mediaCanvasW(vidLand), ch = mediaCanvasH(vidLand);
  mediaFitBox(srcW, srcH, cw, ch, vidBoxW, vidBoxH);
  vidBoxX = (cw - vidBoxW) / 2;
  vidBoxY = (ch - vidBoxH) / 2;
  if(vidZoom < 1) vidZoom = 1;
  if(vidZoom > VID_ZOOM_MAX) vidZoom = VID_ZOOM_MAX;
  vidDW = vidBoxW * vidZoom;
  vidDH = vidBoxH * vidZoom;
  vidClampPan();
}

// -------------------------------------------------------------
//  VOLCADO DEL FOTOGRAMA
//  ------------------------------------------------------------
//  Dos caminos, y ninguno pasa por un buffer intermedio del tamano
//  de la imagen:
//
//  VERTICAL. La fila de la imagen es una fila del framebuffer: un
//  memcpy y ya esta.
//
//  HORIZONTAL. El panel es fisicamente vertical, asi que una fila
//  logica cae en una COLUMNA de memoria. Escribir pixel a pixel
//  serian ~384.000 escrituras sueltas por cuadro, cada una en una
//  linea de cache distinta (el mismo problema que ya documenta el
//  compositor de Modo PC). En vez de eso se acumulan VIDT_ROWS
//  filas en una tira y se vuelcan juntas: cada escritura pasa a ser
//  una tira CONTIGUA de 8 pixeles, y el numero de accesos
//  dispersos baja en la misma proporcion.
//  Si no hay RAM interna para la tira se usa el camino directo
//  pixel a pixel: mas lento, pero correcto. No se desactiva la
//  funcion por no tener la optimizacion.
// -------------------------------------------------------------
static int vidTileBase = 0, vidTileRows = 0, vidTileW = 0;

// La tira guarda VIDT_ROWS filas ya RECORTADAS a la caja visible:
// vidTileLX0 es su primera columna logica y vidTileW su anchura.
static int vidTileLX0 = 0;

static void vidLandFlushTile(){
  if(!vidTile || vidTileRows <= 0) return;
  for(int i = 0; i < vidTileW; i++){
    const int lx = vidTileLX0 + i;                     // columna logica
    const int phyY = lx;                               // en horizontal, lx ES la fila fisica
    if((unsigned)phyY >= (unsigned)SCR_H) continue;
    // ly crece -> x fisica decrece: la tira se escribe al reves.
    const int lyTop = vidTileBase + vidTileRows - 1;
    const int xStart = (SCR_W - 1) - lyTop;
    if(xStart < 0 || xStart >= SCR_W) continue;
    uint16_t* dst = fb + (size_t)phyY * SCR_W + xStart;
    int k = 0, kmax = vidTileRows;
    if(xStart + kmax > SCR_W) kmax = SCR_W - xStart;
    for(; k < kmax; k++)
      dst[k] = vidTile[(size_t)(vidTileRows - 1 - k) * (size_t)LW + i];
  }
  vidTileRows = 0;
}

// Escribe UNA fila ya decodificada en el lienzo, aplicando el
// desplazamiento del zoom y recortando a la caja visible. Es el
// unico sitio donde se decide donde cae cada pixel, en vertical y en
// horizontal: por eso la imagen y el tacto no pueden discrepar.
static bool vidJpegRow(void* user, int y, int w, const uint16_t* rgb){
  (void)user;
  // Fila logica de destino dentro del lienzo.
  const int ly = vidBoxY + y - vidPanY;
  if(ly >= vidBoxY + vidBoxH) return false;     // por debajo de la caja: se acabo
  if(ly < vidBoxY) return true;                 // por encima: aun no entra

  // Recorte horizontal a la caja.
  int src0 = 0, lx0 = vidBoxX - vidPanX, n = w;
  if(lx0 < vidBoxX){ src0 = vidBoxX - lx0; n -= src0; lx0 = vidBoxX; }
  if(n > vidBoxW - (lx0 - vidBoxX)) n = vidBoxW - (lx0 - vidBoxX);
  if(n <= 0) return true;
  const uint16_t* src = rgb + src0;

  if(!vidLand){
    if((unsigned)ly >= (unsigned)SCR_H) return true;
    int dx = lx0;
    if(dx < 0){ src -= dx; n += dx; dx = 0; }
    if(dx + n > SCR_W) n = SCR_W - dx;
    if(n > 0) memcpy(fb + (size_t)ly * SCR_W + dx, src, (size_t)n * 2);
    return true;
  }

  if(vidTile){
    // Si cambia el tramo horizontal (no deberia dentro de una imagen)
    // se vuelca lo acumulado antes de empezar otro.
    if(vidTileRows > 0 && (lx0 != vidTileLX0 || n != vidTileW)) vidLandFlushTile();
    if(n > LW) n = LW;                          // la tira mide LW de ancho
    if(vidTileRows == 0){ vidTileBase = ly; vidTileLX0 = lx0; vidTileW = n; }
    memcpy(vidTile + (size_t)vidTileRows * (size_t)LW, src, (size_t)n * 2);
    vidTileRows++;
    if(vidTileRows >= VIDT_ROWS) vidLandFlushTile();
    return true;
  }
  // Camino de respaldo sin tira: correcto, solo mas lento.
  for(int i = 0; i < n; i++){
    int phyY = lx0 + i, phyX = (SCR_W - 1) - ly;
    if((unsigned)phyY < (unsigned)SCR_H && (unsigned)phyX < (unsigned)SCR_W)
      fb[(size_t)phyY * SCR_W + phyX] = src[i];
  }
  return true;
}

// Banda FISICA que ocupa el contenido, para volcar solo eso.
// En horizontal la "banda" es el tramo de X logica, que es
// justamente el rango de filas fisicas (misma regla que Modo PC).
static void vidFrameBand(int &b0, int &b1){
  if(vidLand){ b0 = vidBoxX; b1 = vidBoxX + vidBoxW - 1; }
  else       { b0 = vidBoxY; b1 = vidBoxY + vidBoxH - 1; }
  if(b0 < 0) b0 = 0;
  if(b1 > SCR_H - 1) b1 = SCR_H - 1;
}

// Decodifica `len` bytes de JPEG sobre el lienzo. Devuelve el codigo
// de FlexOS_JPEG. NO vuelca: quien llama decide cuando publicar.
static int vidDrawJpeg(const uint8_t* data, uint32_t len){
  vidTileRows = 0;
  unsigned long t0 = micros();
  int r = flexJpegDecode(data, len, vidDW, vidDH, 0, NULL,
                         vidJpegRow, NULL, mediaAlloc, mediaFree);
  if(vidLand) vidLandFlushTile();               // lo que quedo en la tira
  vidDecodeUs = (uint32_t)(micros() - t0);
  return r;
}

// -------------------------------------------------------------
//  APERTURA DE UN ARCHIVO
// -------------------------------------------------------------
static void vidFail(const char* why){
  vidKind = VK_ERROR;
  snprintf(vidErrMsg, sizeof(vidErrMsg), "%s", why ? why : "No se pudo abrir");
  vidReleaseMedia(false);
  vidKind = VK_ERROR;                            // vidReleaseMedia lo puso a NONE
}

static bool vidOpenPath(const char* path){
  vidReleaseMedia(true);
  vidErrMsg[0] = 0;
  vidEnded = false;
  snprintf(vidPath, sizeof(vidPath), "%s", path ? path : "");
  const char* nm = strrchr(vidPath, '/');
  snprintf(vidName, sizeof(vidName), "%s", nm ? nm + 1 : vidPath);
  // Cada archivo empieza en Auto y sin zoom: lo que se eligio para el
  // anterior no debe arrastrarse al siguiente.
  gMediaOriMode = MORI_AUTO;
  vidZoom = 1; vidPanX = vidPanY = 0; vidPanning = false;

  if(!mediaVolReady(vidPath)){
    vidFail("Sin almacenamiento");
    return false;
  }
  const int kind = flexMediaClassify(vidName);
  if(kind == FLEXMED_UNSUP || kind == FLEXMED_NONE){
    const char* why = flexMediaUnsupportedReason(vidName);
    vidFail(why ? why : "Formato no compatible");
    mediaNotify(MOD_MEDIA, "No se puede reproducir", vidErrMsg);
    return false;
  }

  // ---------- FOTO ----------
  // Los bytes comprimidos se leen UNA vez y se quedan mientras la
  // foto este abierta. Antes de esto, cada desplazamiento del zoom
  // volvia a leer el archivo entero: girar o arrastrar
  // costaba una lectura completa, no solo una decodificacion.
  if(kind == FLEXMED_PHOTO || kind == FLEXMED_DRAW){
    vidKind = VK_PHOTO;
    vidLand = false;
    if(kind == FLEXMED_DRAW){ vidLayout(SCR_W, SCR_H); return true; }

    uint32_t sz = mediaFileSize(vidPath);
    if(sz == 0){ vidFail("El archivo est\xC3\xA1 vac\xC3\xADo"); return false; }
    if(sz > VID_PHOTO_CAP){
      char m[72];
      snprintf(m, sizeof(m), "Imagen de %u MB: supera el l\xC3\xADmite de %u MB",
               (unsigned)(sz / 1048576u), (unsigned)(VID_PHOTO_CAP / 1048576u));
      vidFail(m);
      return false;
    }
    // Se comprueba que quede PSRAM de sobra ANTES de pedirla: una
    // reserva que agota la memoria deja al sistema entero tocado, no
    // solo a esta foto.
    size_t freePs = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if(freePs > 0 && (size_t)sz + VID_PSRAM_MARGIN > freePs){
      vidFail("No hay memoria libre suficiente para esta imagen");
      return false;
    }
    vidPhotoBuf = (uint8_t*)mediaAlloc(sz);
    if(!vidPhotoBuf){ vidFail("La imagen no cabe en memoria"); return false; }
    int rd = mediaReadWhole(vidPath, vidPhotoBuf, sz);
    if(rd <= 0){ vidFail("No se pudo leer la imagen"); return false; }
    vidPhotoLen = (uint32_t)rd;

    FlexJpegInfo inf;
    if(flexJpegProbe(vidPhotoBuf, vidPhotoLen, &inf) != FLEXJPG_OK){
      vidFail(inf.progressive ? "JPEG progresivo: no se puede decodificar"
                              : "No es un JPEG que se pueda abrir");
      return false;
    }
    vidLand = mediaOriLandscape(inf.width, inf.height);
    if(inf.width == inf.height) gMediaSquareLand = vidLand;
    vidLayout(inf.width, inf.height);
    return true;
  }

  // ---------- AUDIO ----------
  // Se escucha en Musica (mediaOpenInPlayer ya lo manda alli); si llega
  // aqui por otro camino, se dice donde, sin abrir un segundo reproductor.
  if(kind == FLEXMED_AUDIO){ vidFail("El audio se escucha en M\xC3\xBAsica"); return false; }

  // ---------- VIDEO ----------
  if(!mediaStreamOpen(&vidStream, vidPath)){ vidFail("No se pudo abrir el archivo"); return false; }
  FlexMediaIO io; mediaBindIO(&io, &vidStream);
  int r = flexAviOpen(&vidAvi, &io);
  if(r != FLEXAVI_OK){
    vidFail(flexAviErrStr(r));
    mediaNotify(MOD_MEDIA, "No se puede reproducir", vidErrMsg);
    return false;
  }
  vidFrameBuf = (uint8_t*)mediaAlloc(VID_FRAME_CAP);
  if(!vidFrameBuf){ vidFail("Sin memoria para el video"); return false; }
  // La tira del volcado girado va en RAM INTERNA a proposito: se
  // lee por columnas y en PSRAM ese patron es el mas lento posible.
  // Si no hay, se sigue sin ella (camino de respaldo).
  vidTile = (uint16_t*)heap_caps_malloc((size_t)LW * VIDT_ROWS * 2,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  vidKind = VK_VIDEO;
  vidLand = mediaOriLandscape(vidAvi.width, vidAvi.height);
  vidLayout(vidAvi.width, vidAvi.height);

  // Reanudar donde se dejo, si ese punto sigue teniendo sentido.
  uint32_t res = vidResumeGet(vidPath);
  vidCurFrame = 0;
  if(res > 0 && vidAvi.frames > 0 && res < vidAvi.frames - 1){
    int landed = flexAviSeekFrame(&vidAvi, res);
    if(landed >= 0) vidCurFrame = (uint32_t)landed;
  }
  vidPlaying = false;                    // se abre en pausa, con el cuadro pintado
  vidNextUs  = micros();
  vidStatFrames = vidStatSkipped = vidStatErrors = 0;
  vidStatMs = millis();
  vidFpsReal = 0;
  return true;
}

// -------------------------------------------------------------
//  DIBUJO DEL VISOR
// -------------------------------------------------------------
static void vidDrawCurrentFrame(bool publish){
  if(vidKind != VK_VIDEO || !vidFrameBuf) return;
  uint32_t fn = 0;
  int n = flexAviReadFrame(&vidAvi, vidFrameBuf, VID_FRAME_CAP, &fn);
  if(n == FLEXAVI_ERR_EOF){ vidEnded = true; vidPlaying = false; return; }
  if(n < 0){ vidStatErrors++; return; }
  vidCurFrame = fn;
  setBuf(fb);
  int r = vidDrawJpeg(vidFrameBuf, (uint32_t)n);
  if(r != FLEXJPG_OK) vidStatErrors++;
  else                vidStatFrames++;
  if(publish){ int b0, b1; vidFrameBand(b0, b1); flxFlush(b0, b1); }
}

// Pinta una foto ajustada a la caja, SIN deformar (mediaFitBox
// conserva la proporcion: sobra fondo por un lado, no se estira la
// imagen). Se decodifica cuando cambia lo que se ve -- abrir, girar,
// hacer zoom o desplazar --, no en cada vuelta del tick: mientras la
// foto solo se esta mirando, aqui no entra nadie.
//
// El zoom no gasta memoria extra: se pide la decodificacion al
// tamano ampliado y vidJpegRow recorta a la caja, asi que las filas
// que no se ven no llegan a copiarse.
static void vidDrawPhoto(){
  if(!vidPhotoBuf || !vidPhotoLen) return;      // ya se dijo por que al abrir
  setBuf(fb);
  int r = vidDrawJpeg(vidPhotoBuf, vidPhotoLen);
  if(r != FLEXJPG_OK && r != FLEXJPG_ERR_ABORTED)
    vidFail(flexJpegErrStr(r));
}

// ---- Controles: se dibujan ENCIMA del contenido ----
// Su geometria esta en coordenadas del lienzo logico, asi que gira
// con la imagen: los botones, la barra y el tacto usan las MISMAS
// coordenadas y no pueden quedar descolocados entre si.
static void vidCtrlGeom(int &bx, int &by, int &bw, int &bh){
  int cw = mediaCanvasW(vidLand), chh = mediaCanvasH(vidLand);
  bw = cw - 32; bh = 118;
  bx = 16;
  // En vertical se respeta la franja de la barra de gestos del
  // sistema; en horizontal esa barra no se dibuja (gLand la
  // desactiva), asi que el panel puede bajar mas.
  by = chh - bh - (vidLand ? 16 : navBarH() + 12);
}

// UNA sola fuente para donde estan los botones. Dibujo y hit-test
// llaman a esta funcion, asi que no pueden descolocarse entre si --
// que es exactamente el fallo que produce "se ve horizontal pero el
// tacto responde en vertical".
static void vidBtnGeom(VidBtns &b){
  int bx, by, bw, bh; vidCtrlGeom(bx, by, bw, bh);
  b.cx      = bx + bw / 2;
  b.cy      = by + 78;
  b.back10X = b.cx - 62;
  b.fwd10X  = b.cx + 62;
  b.prevX   = b.cx - 122;
  b.nextX   = b.cx + 122;
  b.oriW    = 74; b.oriH = 30;
  b.oriX    = bx + bw - b.oriW - 12;
  b.oriY    = by + 56;
}
static inline bool vidHit(int px, int py, int cx, int cy, int r){
  return px >= cx - r && px <= cx + r && py >= cy - r && py <= cy + r;
}
static uint32_t vidPosMs(){
  if(vidKind != VK_VIDEO || !vidAvi.usPerFrame) return 0;
  return (uint32_t)(((uint64_t)vidCurFrame * vidAvi.usPerFrame) / 1000ull);
}
static void vidFmtTime(uint32_t ms, char* out, size_t n){
  uint32_t s = ms / 1000u;
  snprintf(out, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}

static void vidDrawControls(bool publish){
  if(!vidCtrlOn) return;
  int bx, by, bw, bh; vidCtrlGeom(bx, by, bw, bh);
  setBuf(fb);
  // Panel coherente con la personalizacion global: cristal en Liquid
  // Glass, superficie solida en Plano. No se mezcla: dentro del modo
  // Plano no aparece ni una tarjeta de cristal.
  if(uiGlass) drawLiquidGlassPanel(bx, by, bw, bh, 18, TH_GLASS2);
  else        fillRoundRect(bx, by, bw, bh, 18, TH_SURF2);

  // ---- barra de progreso REAL ----
  const int sx = bx + 20, sw = bw - 40, sy = by + 22;
  uint32_t dur = (vidKind == VK_VIDEO) ? flexAviDurationMs(&vidAvi) : 0;
  uint32_t pos = vidPosMs();
  fillRoundRect(sx, sy, sw, 6, 3, TH_TRACK);
  if(dur > 0){
    int fw = (int)((uint64_t)sw * pos / dur);
    if(fw < 0) fw = 0;
    if(fw > sw) fw = sw;
    fillRoundRect(sx, sy, fw, 6, 3, TH_PRIM);
    fillCircle(sx + fw, sy + 3, 9, TH_TXT);
  }
  char t1[16], t2[16];
  vidFmtTime(pos, t1, sizeof(t1));
  if(dur > 0){ vidFmtTime(dur, t2, sizeof(t2)); }
  else snprintf(t2, sizeof(t2), "--:--");
  drawText (sx,      sy + 14, t1, 1, TH_TXT2);
  drawTextR(sx + sw, sy + 14, t2, 1, TH_TXT2);

  // ---- transporte ----
  VidBtns b; vidBtnGeom(b);
  fillCircle(b.cx, b.cy, 26, TH_PRIM);
  if(vidPlaying){
    fillRect(b.cx - 8, b.cy - 11, 5, 22, TH_ONACC);
    fillRect(b.cx + 3, b.cy - 11, 5, 22, TH_ONACC);
  } else {
    fillTriangle(b.cx - 7, b.cy - 11, b.cx - 7, b.cy + 11, b.cx + 11, b.cy, TH_ONACC);
  }
  const bool seekable = (vidKind == VK_VIDEO);
  // Adelantar/retroceder solo se pintan si de verdad se puede: no hay
  // botones que no hagan nada.
  if(seekable){
    drawTextC(b.back10X, b.cy - 7, "<<10", 2, TH_TXT);
    drawTextC(b.fwd10X,  b.cy - 7, "10>>", 2, TH_TXT);
  }
  drawTextC(b.prevX, b.cy - 7, "|<", 2, TH_TXT2);
  drawTextC(b.nextX, b.cy - 7, ">|", 2, TH_TXT2);

  // ---- orientacion (Auto / Vertical / Horizontal) ----
  const char* om = (gMediaOriMode == MORI_AUTO) ? "Auto"
                 : (gMediaOriMode == MORI_PORT) ? "Vertical" : "Horizontal";
  if(uiGlass) drawLiquidGlassPanel(b.oriX, b.oriY, b.oriW, b.oriH, 15, TH_GLASS);
  else        fillRoundRect(b.oriX, b.oriY, b.oriW, b.oriH, 15, TH_SURF);
  drawTextC(b.oriX + b.oriW / 2, b.oriY + 9, om, 1, TH_ACCS);

  // ---- rendimiento MEDIDO (solo video, y solo si ya hay medida) ----
  if(vidKind == VK_VIDEO && vidFpsReal > 0){
    char st[48];
    snprintf(st, sizeof(st), "%u.%u fps  ·  %u saltados",
             (unsigned)(vidFpsReal / 10), (unsigned)(vidFpsReal % 10),
             (unsigned)vidStatSkipped);
    drawText(bx + 20, by + 62, st, 1, TH_MUTE);
  }
  if(vidKind == VK_PHOTO && vidZoom > 1){
    char zt[16]; snprintf(zt, sizeof(zt), "x%d", vidZoom);
    drawText(bx + 20, by + 62, zt, 2, TH_ACCS);
  }
  if(publish) flxFlush(vidLand ? bx : by, vidLand ? bx + bw - 1 : by + bh - 1);
}

static void vidRenderViewer(){
  setBuf(fb);
  // Fondo negro: es el unico color honesto detras de una foto o un
  // video, y ademas evita que se vea el escritorio por las bandas.
  bool oldLand = gLand;
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  fillRect(0, 0, SCR_W, SCR_H, rgb565(0, 0, 0));
  gLand = oldLand;

  gLand = vidLand;
  if(vidLand){ gClipY0 = 0; gClipY1 = LW - 1; gClipX0 = 0; gClipX1 = LH - 1; }
  else       { gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1; }

  if(vidKind == VK_PHOTO)      vidDrawPhoto();
  else if(vidKind == VK_VIDEO) vidDrawCurrentFrame(false);

  if(vidKind == VK_ERROR){
    int cw = mediaCanvasW(vidLand), chh = mediaCanvasH(vidLand);
    drawTextC(cw / 2, chh / 2 - 40, "No se puede abrir", 3, TH_ERR);
    drawTextC(cw / 2, chh / 2, vidName, 2, TH_TXT2);
    drawTextC(cw / 2, chh / 2 + 30, vidErrMsg, 1, TH_MUTE);
  }
  // Titulo discreto arriba (no tapa la imagen: va sobre el fondo).
  drawTextC(mediaCanvasW(vidLand) / 2, 10, vidName, 1, TH_MUTE);
  // Flecha de volver, en el lienzo logico -> gira con todo lo demas.
  strokeSegAA(30, 26, 18, 18, 2.4f, TH_TXT2);
  strokeSegAA(18, 18, 30, 10, 2.4f, TH_TXT2);

  vidCtrlOn = true; vidCtrlMs = millis();
  vidDrawControls(false);

  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  flxFlushAll();
}

// -------------------------------------------------------------
//  PANTALLA DE LISTA
//  ------------------------------------------------------------
//  Sale del catalogo de la biblioteca, el MISMO de la Galeria y de
//  Flex Web Server: si la Galeria ve una foto, Multimedia la ve, y al
//  reves. Seleccion, menus y acciones son los del kit comun (mk*).
// -------------------------------------------------------------
#define VID_ROW_H   64
#define VID_HEAD_H  104
#define VID_TABS_N  3
#define VID_THUMB_BUDGET 6            // miniaturas nuevas por repintado
static const char* VID_TABS[VID_TABS_N] = { "Todo", "V\xC3\xAD" "deos", "Fotos" };
static uint32_t vidTabMask(){
  switch(vidFilter){
    case 1: return FML_MASK(FML_K_VIDEO);
    case 2: return FML_MASK(FML_K_PHOTO) | FML_MASK(FML_K_DRAW);
  }
  return FML_MASK_VISUAL;
}

// Vista del catalogo (indices dentro de gMs.lib.recs): solo vale con el
// cerrojo tomado, como en la Galeria.
static uint16_t   vidViewStore[FML_CAP];
static FlexMlView vidView;
static uint32_t   vidViewMask = 0;
static bool       vidViewReady = false;
static int        vidCountCache = 0;
static bool       vidMorePending = false;
static uint32_t   vidSeenRev = 0, vidSeenMs = 0;

static void vidSyncLocked(){
  uint32_t m = vidTabMask();
  if(!vidViewReady || vidViewMask != m){
    flexMlViewInit(&vidView, vidViewStore, FML_CAP, m, FML_SORT_NEWEST);
    vidViewMask = m; vidViewReady = true;
    flexMlViewSync(&vidView, &gMs.lib, true);
  } else flexMlViewSync(&vidView, &gMs.lib, false);
}

static void vidOpenId(uint32_t id);
static const MediaListApp VID_APP = { "Multimedia", vidRenderAll, vidOpenId, NULL };

static int vidRowY(int i){ int bx, by, bw, bh; uiBox(bx, by, bw, bh); return by + VID_HEAD_H + i * VID_ROW_H - vidListScroll; }

static void vidDrawRow(const FlexMlRec* r, int x, int y, int w, int &budget){
  int h = VID_ROW_H - 8;
  bool sel = mkMulti && mkIsSel(r->id);
  if(sel) fillRoundRect(x, y, w, h, 12, TH_SEL);
  else if(uiGlass) drawGlassCardFlat(x, y, w, h, 12, TH_GLASS, WIN_BG);
  else        fillRoundRect(x, y, w, h, 12, TH_SURF);
  int tx = x + 8, ty = y + (h - ML_SMALL) / 2;
  if(r->flags & FML_R_LOCKED){
    // Solo el candado: ni miniatura, ni nombre, ni tamano.
    fillRoundRect(tx, ty, ML_SMALL, ML_SMALL, 8, TH_SURF2);
    mlPadlock(tx + ML_SMALL / 2, ty + ML_SMALL / 2 - 2, 8, TH_TXT2, false);
    drawText(x + 64, y + (h - uiLineH(2)) / 2, "Protegido", 2, TH_TXT2);
  } else {
    bool more = false;
    const uint16_t* th = mlThumbGetLocked(r, &budget, &more);
    if(more) vidMorePending = true;
    if(th) mlBlitThumbSmall(th, tx, ty, 8);
    else {
      fillRoundRect(tx, ty, ML_SMALL, ML_SMALL, 8, TH_SURF2);
      if(r->kind == FML_K_VIDEO) fillTriangle(tx + 16, ty + 12, tx + 16, ty + 32, tx + 32, ty + 22, TH_TXT2);
      else { fillTriangle(tx + 7, ty + 34, tx + 18, ty + 18, tx + 29, ty + 34, TH_TXT2); fillCircle(tx + 32, ty + 13, 4, TH_TXT2); }
    }
    if(r->kind == FML_K_VIDEO && th){
      fillCircle(tx + 11, ty + 11, 8, rgb565(0, 0, 0));
      fillTriangle(tx + 8, ty + 6, tx + 8, ty + 16, tx + 16, ty + 11, rgb565(255, 255, 255));
    }
    char nm[FML_NAME_MAX]; flexMlDisplayName(r, nm, sizeof(nm));
    drawTextClip(x + 64, y + 8, nm, 2, TH_TXT, x + w - (mkMulti ? 44 : 12));
    // Lo que es de verdad: clase, duracion o medidas, tamano, y si esta
    // placa no lo abre, por que.
    char sub[72], sz[16], extra[24] = "";
    flexFsFmtSize(r->size, sz, sizeof(sz));
    if(r->kind == FML_K_VIDEO && r->durMs){ mlFmtDur(r->durMs, extra, sizeof(extra)); }
    else if(r->w && r->h) snprintf(extra, sizeof(extra), "%ux%u", (unsigned)r->w, (unsigned)r->h);
    const char* cls = r->kind == FML_K_VIDEO ? "V\xC3\xAD" "deo" : r->kind == FML_K_DRAW ? "Dibujo" : "Foto";
    if(r->state == FML_S_ERROR) snprintf(sub, sizeof(sub), "%s  \xC2\xB7  Archivo da\xC3\xB1" "ado", cls);
    else if(!(r->flags & FML_R_PLAYABLE) && !(r->flags & FML_R_NEED_THUMB))
      snprintf(sub, sizeof(sub), "%s  \xC2\xB7  %s  \xC2\xB7  Solo guardar", flexMlFmtName(r->fmt), sz);
    else snprintf(sub, sizeof(sub), "%s%s%s  \xC2\xB7  %s", cls, extra[0] ? "  \xC2\xB7  " : "", extra, sz);
    drawTextClip(x + 64, y + 34, sub, 1, r->state == FML_S_ERROR ? TH_WARN : TH_TXT2, x + w - (mkMulti ? 44 : 12));
  }
  if(mkMulti){
    int cx = x + w - 22, cy = y + h / 2;
    fillCircle(cx, cy, 11, sel ? TH_PRIM : rgb565(0, 0, 0));
    drawCircle(cx, cy, 11, rgb565(255, 255, 255));
    if(sel){
      strokeSegAA(cx - 5, cy, cx - 1, cy + 5, 2.0f, TH_ONACC);
      strokeSegAA(cx - 1, cy + 5, cx + 6, cy - 5, 2.0f, TH_ONACC);
    }
  }
}

static void vidListRender(){
  if(webSheetIsOpen()){ webSheetRender(); return; }  // la hoja del servidor manda mientras esta abierta
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  vidMorePending = false;
  mlThumbNewPass();
  int budget = VID_THUMB_BUDGET;
  drawText(bx + pad, by + 14, "Multimedia", 4, TH_TXT);
  for(int i = 0; i < 3; i++) fillCircle(bx + bw - pad - 4, by + 22 + i * 12, 4, TH_NAV);

  if(!gMlOk){
    const char* t = !flexFsReady() ? "Sin almacenamiento" : gSafeMode ? "Modo seguro" : "Biblioteca no disponible";
    drawTextC(bx + bw / 2, by + bh / 2 - 20, t, 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 16, !flexFsReady() ? flexFsError() : "La biblioteca no se abre ahora", 1, TH_MUTE);
    vidCountCache = 0;
    flxFlush(WIN_TOP, WIN_BOT);
    return;
  }

  mlLock();
  vidSyncLocked();
  const int n = vidView.n;
  vidCountCache = n;
  vidSeenRev = gMs.lib.rev; vidSeenMs = millis();
  mkPruneLocked(&vidView);
  { char cnt[48];
    if(mkMulti) snprintf(cnt, sizeof(cnt), "%u seleccionado%s", (unsigned)mkSelN, mkSelN == 1 ? "" : "s");
    else snprintf(cnt, sizeof(cnt), "%d elemento%s", n, n == 1 ? "" : "s");
    drawText(bx + pad, by + 52, cnt, 1, mkMulti ? TH_PRIM : TH_TXT2); }
  const char* st = NULL; char stb[64];
  if(gMs.scanning) st = "Buscando archivos\xE2\x80\xA6";
  else if(gMs.pending){ snprintf(stb, sizeof(stb), "Preparando miniaturas (%u)\xE2\x80\xA6", (unsigned)gMs.pending); st = stb; }
  if(st) drawTextR(bx + bw - pad - 18, by + 52, st, 1, TH_TXT2);

  // Pestanas
  const int tabY = by + 68, tw = (bw - 2 * pad) / VID_TABS_N;
  for(int i = 0; i < VID_TABS_N; i++){
    int tx = bx + pad + i * tw;
    if(i == vidFilter) fillRoundRect(tx + 2, tabY, tw - 4, 28, 14, TH_PRIM);
    drawTextC(tx + tw / 2, tabY + 8, VID_TABS[i], 1, i == vidFilter ? TH_ONACC : TH_TXT2);
  }

  if(n == 0){
    drawTextC(bx + bw / 2, by + bh / 2 - 30, "No hay fotos ni v\xC3\xAD" "deos", 3, TH_TXT2);
    drawTextC(bx + bw / 2, by + bh / 2 + 6, "S\xC3\xBA" "belos desde el m\xC3\xB3vil:", 1, TH_MUTE);
    drawTextC(bx + bw / 2, by + bh / 2 + 24, "men\xC3\xBA \xE2\x8B\xAE \xE2\x80\xBA Conectar con el m\xC3\xB3vil", 1, TH_MUTE);
  }

  uiClipViewport(by + VID_HEAD_H - 6, by + bh - 1);
  for(int i = 0; i < n; i++){
    int y = vidRowY(i);
    if(y + VID_ROW_H < by + VID_HEAD_H - 6 || y > by + bh) continue;   // solo las visibles
    vidDrawRow(&gMs.lib.recs[vidView.idx[i]], bx + pad, y, bw - 2 * pad, budget);
  }
  uiClipFull();
  int selLocked = 0, selOpen = 0, selectable = 0;
  if(mkMulti) mkCountLocked(&vidView, &selLocked, &selOpen, &selectable);
  mlUnlock();

  if(mkMulti) mkDrawBar(selLocked, selOpen, selectable);
  mkDrawOverlays();
  flxFlush(WIN_TOP, WIN_BOT);
}

static int vidListMaxScroll(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int need = VID_HEAD_H + vidCountCache * VID_ROW_H + (mkMulti ? MKB_H + 26 : 30);
  int m = need - bh;
  return m > 0 ? m : 0;
}

// Toque sobre la lista -> elemento (o 0).
static uint32_t vidHitId(int tx, int ty){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  if(ty < by + VID_HEAD_H - 6) return 0;
  uint32_t id = 0;
  mlLock();
  vidSyncLocked();
  for(int i = 0; i < vidView.n && !id; i++){
    int y = vidRowY(i);
    if(ty >= y && ty <= y + VID_ROW_H - 8 && tx >= bx && tx <= bx + bw) id = gMs.lib.recs[vidView.idx[i]].id;
  }
  mlUnlock();
  return id;
}

static void vidSelectAll(){
  mlLock();
  vidSyncLocked();
  mkSelectAllLocked(&vidView);
  mlUnlock();
  vidListRender();
}

// -------------------------------------------------------------
//  APERTURA DESDE OTRAS APPS
//  ------------------------------------------------------------
//  Galeria y el Explorador llaman aqui. La ruta se guarda y la app
//  se abre por el camino normal (enterApp), asi que la animacion,
//  el historial de recientes y el ciclo de vida son los de siempre.
// -------------------------------------------------------------
static char    gMediaPending[FLEXMED_PATH_MAX] = "";
// De que app se salio para abrir el visor. La Galeria la pone antes
// de abrir un elemento, para que "volver" desde el visor regrese a
// la rejilla y no a la lista de Multimedia. 0xFF = se entro a
// Multimedia por su cuenta.
static uint8_t gMediaReturnApp = 0xFF;
static void musOpenPath(const char* path);        // Musica, mas abajo en la cadena

// Id del catalogo de una ruta (0 si no esta registrada).
static uint32_t vidIdOfPath(const char* path){
  mlLock();
  int i = flexMlFindPath(&gMs.lib, path);
  uint32_t id = i >= 0 ? gMs.lib.recs[i].id : 0;
  mlUnlock();
  return id;
}

static void mediaOpenInPlayer(const char* path){
  if(!path || !path[0]) return;
  // El audio se escucha en Musica: un solo reproductor, que ademas sigue
  // sonando en segundo plano.
  const char* nm = strrchr(path, '/');
  if(flexMediaClassify(nm ? nm + 1 : path) == FLEXMED_AUDIO){ musOpenPath(path); return; }
  snprintf(gMediaPending, sizeof(gMediaPending), "%s", path);
  if(gState == ST_APP && gAppId == IC_MULTIMEDIA){
    // Ya estamos dentro: se abre en el acto.
    vidScreen = VS_VIEW;
    vidCurId = vidIdOfPath(gMediaPending);
    vidOpenPath(gMediaPending);
    gMediaPending[0] = 0;
    vidRenderViewer();
    return;
  }
  if(gState == ST_APP) appClose();
  enterApp(IC_MULTIMEDIA);
}

// Abrir desde la lista (ya autorizado si estaba protegido).
static void vidOpenId(uint32_t id){
  FlexMlRec r;
  if(!mlGet(id, &r)) return;
  vidCurId = id;
  vidScreen = VS_VIEW;
  vidOpenPath(r.path);
  vidRenderViewer();
}

// Anterior/siguiente recorre la lista que el usuario ve, SALTANDO lo
// protegido: pasar de una foto a la siguiente no puede ensenar algo que
// pide clave.
static void vidOpenNeighbour(int delta){
  char path[FML_PATH_MAX] = "";
  uint32_t id = 0;
  mlLock();
  vidSyncLocked();
  int n = vidView.n;
  int cur = vidCurId ? flexMlViewFindId(&vidView, &gMs.lib, vidCurId) : -1;
  int base = cur >= 0 ? cur : (delta > 0 ? -1 : n);
  for(int step = 1; step <= n && !id; step++){
    int k = ((base + delta * step) % n + n) % n;
    const FlexMlRec* r = &gMs.lib.recs[vidView.idx[k]];
    if(r->flags & FML_R_LOCKED) continue;
    if(r->id == vidCurId) break;                  // dio la vuelta: no hay otro
    id = r->id;
    snprintf(path, sizeof(path), "%s", r->path);
  }
  mlUnlock();
  if(!id) return;
  vidCurId = id;
  vidOpenPath(path);
  vidRenderViewer();
}

// -------------------------------------------------------------
//  BUSQUEDA (barra de progreso y botones de +-10 s)
// -------------------------------------------------------------
static void vidSeekToMs(uint32_t ms){
  if(vidKind != VK_VIDEO || !vidAvi.usPerFrame) return;
  uint32_t target = (uint32_t)(((uint64_t)ms * 1000ull) / vidAvi.usPerFrame);
  if(vidAvi.frames && target >= vidAvi.frames) target = vidAvi.frames - 1;
  int landed = flexAviSeekFrame(&vidAvi, target);
  if(landed < 0){
    vidFail("Se perdio el acceso al archivo");
    vidRenderViewer();
    return;
  }
  vidCurFrame = (uint32_t)landed;
  vidEnded = false;
  vidNextUs = micros();
  vidDrawCurrentFrame(true);
  vidDrawControls(true);
}

// -------------------------------------------------------------
//  TICK DEL REPRODUCTOR
//  ------------------------------------------------------------
//  Un fotograma como mucho por vuelta de loop(). Si se va tarde se
//  saltan fotogramas (leyendo solo sus cabeceras), con tope: la
//  interfaz nunca se queda sin turno por muy pesado que sea el
//  archivo. Ni un delay() en todo el camino.
// -------------------------------------------------------------
static void vidPlaybackTick(){
  if(!vidPlaying || vidKind != VK_VIDEO) return;
  unsigned long now = micros();
  if((long)(now - vidNextUs) < 0) return;

  // Cuanto se ha ido tarde, en fotogramas.
  const uint32_t spf = vidAvi.usPerFrame ? vidAvi.usPerFrame : 40000;
  long late = (long)(now - vidNextUs);
  int drop = (int)(late / (long)spf);
  if(drop > VID_MAX_CATCHUP) drop = VID_MAX_CATCHUP;

  for(int i = 0; i < drop; i++){
    int r = flexAviSkipFrame(&vidAvi);
    if(r == FLEXAVI_ERR_EOF){ vidEnded = true; break; }
    if(r < 0){ vidStatErrors++; break; }
    vidCurFrame++;
    vidStatSkipped++;
  }
  if(!vidEnded) vidDrawCurrentFrame(true);

  // El instante teorico avanza por los fotogramas consumidos, no por
  // "ahora + spf": asi el video no se va acumulando retraso.
  vidNextUs += (unsigned long)spf * (unsigned long)(drop + 1);
  // Si el retraso es tan grande que ya no se puede recuperar, se
  // reengancha al presente en vez de intentar reproducir el pasado.
  if((long)(micros() - vidNextUs) > (long)(spf * 8)) vidNextUs = micros() + spf;

  if(vidEnded){
    vidPlaying = false;
    vidResumeSet(vidPath, 0);                  // terminado: la proxima vez, desde el principio
    mediaNotify(MOD_MEDIA, "Reproducci\xC3\xB3n terminada", vidName);
    vidCtrlOn = true; vidCtrlMs = millis();
    vidDrawControls(true);
  }

  // ---- medicion real, por ventanas de 1 s ----
  unsigned long ms = millis();
  if(ms - vidStatMs >= 1000){
    vidFpsReal = (uint16_t)(vidStatFrames * 10u);      // cuadros en 1 s, con un decimal
    vidStatFrames = 0;
    vidStatMs = ms;
    if(vidCtrlOn) vidDrawControls(true);
  }
}

// -------------------------------------------------------------
//  ENTRADA
// -------------------------------------------------------------
// -------------------------------------------------------------
//  SALIR DEL VISOR
//  ------------------------------------------------------------
//  UN solo camino, y lo comparten las tres formas de salir: la
//  flecha del lienzo, el boton ATRAS de la barra del sistema y el
//  gesto. Tener uno solo es lo que garantiza que por cualquiera de
//  los tres se suelten los mismos recursos y se vuelva al mismo
//  sitio; si cada uno hiciera lo suyo, bastaria olvidarse en uno
//  para mantener una lectura incremental eficiente.
// -------------------------------------------------------------
static void vidLeaveViewer(){
  vidReleaseMedia(true);                 // guarda la posicion y suelta TODO
  gLand = false;                         // el motor vuelve a vertical
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  vidScreen = VS_LIST;
  vidZoom = 1; vidPanX = vidPanY = 0;
  vidPanning = false;
  // Si el archivo se abrio DESDE otra app (la Galeria), se vuelve
  // alli: salir por donde se entro es lo que espera cualquiera.
  if(gMediaReturnApp != 0xFF){
    uint8_t back = gMediaReturnApp;
    gMediaReturnApp = 0xFF;
    appClose();
    enterApp(back);
    return;
  }
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, WIN_BG);
  appDrawChrome(IC_MULTIMEDIA);
  appDrawHeader(IC_MULTIMEDIA);
  vidListRender();
  flxFlushAll();
}

// El desplazamiento de una foto ampliada: se arrastra con el dedo y
// se repinta solo cuando el desplazamiento cambia de verdad.
static int  vidPanGrabX = 0, vidPanGrabY = 0, vidPanBaseX = 0, vidPanBaseY = 0;

static void vidViewerTouch(){
  int tx, ty; mediaTouchXY(vidLand, tx, ty);

  // ---- desplazamiento de la foto ampliada ----
  // Va ANTES que todo lo demas: mientras se arrastra, el toque es
  // del desplazamiento y no llega a los botones.
  if(vidKind == VK_PHOTO && vidZoom > 1){
    if(T.pressed){
      vidPanning = false;
      vidPanGrabX = tx; vidPanGrabY = ty;
      vidPanBaseX = vidPanX; vidPanBaseY = vidPanY;
    }
    if(T.down && !vidPanning
       && (abs(tx - vidPanGrabX) > 8 || abs(ty - vidPanGrabY) > 8)) vidPanning = true;
    if(T.down && vidPanning){
      int px = vidPanBaseX - (tx - vidPanGrabX);
      int py = vidPanBaseY - (ty - vidPanGrabY);
      int ox = vidPanX, oy = vidPanY;
      vidPanX = px; vidPanY = py;
      vidClampPan();
      // Cada repintado es UNA decodificacion. Se limita la cadencia
      // para que arrastrar el dedo no pida una por cada movimiento
      // del tactil; el ultimo repintado se hace igual al soltar, asi
      // que la posicion final siempre acaba siendo la correcta.
      static unsigned long vidPanMs = 0;
      if((vidPanX != ox || vidPanY != oy) && millis() - vidPanMs >= VID_PAN_MS){
        vidPanMs = millis();
        vidRenderViewer();
      }
      return;
    }
    if(!T.down && vidPanning){ vidPanning = false; vidRenderViewer(); return; }
  }

  // Volver: esquina superior izquierda del lienzo LOGICO, asi que
  // esta donde se ve la flecha tambien en horizontal. En horizontal
  // la barra del sistema no se dibuja (la desactiva gLand), asi que
  // esta flecha es la UNICA salida y por eso vive en el lienzo
  // logico, girando con todo lo demas.
  if(T.tap && tx < 56 && ty < 56){ vidLeaveViewer(); return; }

  if(!T.tap) return;

  // Con los controles ocultos, el primer toque solo los muestra: asi
  // tocar la imagen no pausa el video sin querer.
  if(!vidCtrlOn){
    vidCtrlOn = true; vidCtrlMs = millis();
    vidDrawControls(true);
    return;
  }
  vidCtrlMs = millis();

  int bx, by, bw, bh; vidCtrlGeom(bx, by, bw, bh);
  VidBtns b; vidBtnGeom(b);

  // Doble uso del area de contenido: en una foto, tocar fuera del
  // panel cambia el nivel de zoom (1 -> 2 -> 3 -> 4 -> 1). Es el
  // "zoom sencillo": sin gestos de dos dedos, que aqui compiten con
  // el gesto de suspension del sistema.
  if(tx < bx || tx > bx + bw || ty < by || ty > by + bh){
    if(vidKind == VK_PHOTO){
      vidZoom = (vidZoom % VID_ZOOM_MAX) + 1;
      // Al ampliar se centra sobre el punto tocado; al volver a x1 se
      // suelta el desplazamiento.
      if(vidZoom == 1){ vidPanX = vidPanY = 0; }
      else {
        vidPanX = (tx - vidBoxX) * vidZoom - vidBoxW / 2;
        vidPanY = (ty - vidBoxY) * vidZoom - vidBoxH / 2;
      }
      vidRenderViewer();
    }
    return;
  }

  // ---- orientacion ----
  if(tx >= b.oriX && tx <= b.oriX + b.oriW && ty >= b.oriY && ty <= b.oriY + b.oriH){
    vidCycleOrientation();
    return;
  }

  // ---- barra de progreso ----
  const int sx = bx + 20, sw = bw - 40, sy = by + 22;
  if(ty >= sy - 16 && ty <= sy + 18 && tx >= sx - 14 && tx <= sx + sw + 14){
    uint32_t dur = (vidKind == VK_VIDEO) ? flexAviDurationMs(&vidAvi) : 0;
    if(dur > 0){
      int rel = tx - sx;
      if(rel < 0) rel = 0;
      if(rel > sw) rel = sw;
      vidSeekToMs((uint32_t)((uint64_t)dur * (uint32_t)rel / sw));
    }
    return;
  }

  // ---- transporte ----
  if(ty < b.cy - 30 || ty > b.cy + 30) return;

  if(vidHit(tx, ty, b.cx, b.cy, 30)){                  // play / pausa
    if(vidKind == VK_VIDEO){
      if(vidEnded){ vidSeekToMs(0); vidEnded = false; }
      vidPlaying = !vidPlaying;
      vidNextUs = micros();
    }
    vidDrawControls(true);
    return;
  }
  if(vidKind == VK_VIDEO && vidHit(tx, ty, b.back10X, b.cy, 28)){
    uint32_t p = vidPosMs();
    vidSeekToMs(p > VID_SEEK_STEP_MS ? p - VID_SEEK_STEP_MS : 0);
    return;
  }
  if(vidKind == VK_VIDEO && vidHit(tx, ty, b.fwd10X, b.cy, 28)){
    vidSeekToMs(vidPosMs() + VID_SEEK_STEP_MS);
    return;
  }
  if(vidHit(tx, ty, b.prevX, b.cy, 28)){ vidOpenNeighbour(-1); return; }
  if(vidHit(tx, ty, b.nextX, b.cy, 28)){ vidOpenNeighbour(+1); return; }
}

// -------------------------------------------------------------
//  ORIENTACION MANUAL: Auto -> Vertical -> Horizontal -> Auto
//  ------------------------------------------------------------
//  Se aplica al archivo ACTUAL. Al abrir otro se vuelve a Auto (lo
//  hace vidOpenPath): girar un video concreto no debe cambiar como
//  se abren los siguientes.
//
//  Aqui NO hay un segundo motor de rotacion. Se cambia gLand -- el
//  mismo que usan Modo PC y Juegos -- y se vuelve a maquetar; el
//  lienzo logico, los controles, la barra de progreso y el tacto
//  salen todos de las mismas coordenadas (mediaCanvasW/H,
//  vidBtnGeom y mediaTouchXY), asi que giran juntos por
//  construccion. No puede pasar que se vea horizontal y el dedo
//  responda en vertical.
// -------------------------------------------------------------
static void vidCycleOrientation(){
  gMediaOriMode = (uint8_t)((gMediaOriMode + 1) % 3);
  const bool want = mediaOriLandscape(vidSrcW, vidSrcH);
  // Un archivo CUADRADO no tiene orientacion propia: se recuerda la
  // ultima que se uso en esta sesion, que es lo que se pidio.
  if(vidSrcW == vidSrcH) gMediaSquareLand = want;
  vidLand = want;
  // El zoom se reinicia al girar: el hueco visible ya no es el mismo
  // y conservar el desplazamiento dejaria la foto mirando a un trozo
  // que ya no existe.
  vidZoom = 1; vidPanX = vidPanY = 0;
  vidLayout(vidSrcW, vidSrcH);
  if(vidKind == VK_VIDEO){
    // Se vuelve al fotograma ACTUAL para repintarlo girado: girar no
    // debe costar un fotograma perdido.
    flexAviSeekFrame(&vidAvi, vidCurFrame);
  }
  vidRenderViewer();
}

static void vidListTouch(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  const int tabY = by + 68, tw = (bw - 2 * pad) / VID_TABS_N;

  // Arrastre vertical de la lista.
  if(T.pressed){ vidListDragging = false; vidListDragY0 = T.y; vidListDragS0 = vidListScroll; }
  if(T.down && !vidListDragging && abs(T.y - vidListDragY0) > 8) vidListDragging = true;
  if(T.down && vidListDragging){
    int ns = vidListDragS0 - (T.y - vidListDragY0);
    int mx = vidListMaxScroll();
    if(ns < 0) ns = 0;
    if(ns > mx) ns = mx;
    if(ns != vidListScroll){ vidListScroll = ns; vidListRender(); }
    return;
  }
  // Pulsacion larga: el menu del elemento (no activa la seleccion sola).
  static bool longFired = false;
  if(!gHosted && T.down && !longFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    longFired = true;
    uint32_t id = vidHitId(T.startX, T.startY);
    if(id && !mkMulti){ mkOpenItemMenu(id, T.x, T.y + 10, NULL, 0); return; }
    if(id && mkMulti){ mkToggle(id); vidListRender(); return; }
  }
  if(!T.down) longFired = false;
  if(!T.tap) return;
  if(vidListDragging){ vidListDragging = false; return; }

  if(mkMulti && mkBarTouch(vidSelectAll)) return;
  // Menu de la app (los tres puntos).
  if(T.x > bx + bw - pad - 30 && T.y < by + 60){
    static const uint8_t acts[3] = { MA_CONNECT, MA_SELECT, MA_TRASHBIN };
    mkOpenAppMenu(bx + bw - MM_W / 2 - 16, by + 50, acts, 3);
    return;
  }
  if(T.y >= tabY && T.y <= tabY + 28){
    int k = (T.x - bx - pad) / (tw > 0 ? tw : 1);
    if(k >= 0 && k < VID_TABS_N && k != vidFilter){
      vidFilter = k; vidListScroll = 0; vidListRender();
    }
    return;
  }
  uint32_t id = vidHitId(T.x, T.y);
  if(!id) return;
  if(mkMulti){ mkToggle(id); vidListRender(); return; }
  mkRequestOpen(id);                              // lo protegido pide antes la clave
}

// -------------------------------------------------------------
//  CICLO DE VIDA
// -------------------------------------------------------------
static void vidRenderAll(){
  if(vidScreen == VS_VIEW) vidRenderViewer();
  else                     vidListRender();
}

static void vidEnter(){
  mkBind(&VID_APP);
  memset(&vidStream, 0, sizeof(vidStream));
  vidCtrlOn = true; vidCtrlMs = millis();
  appLoadSessionOnce(IC_MULTIMEDIA);
  if(!gRelayout) mkReset();
  if(gMediaPending[0]){
    vidScreen = VS_VIEW;
    vidCurId = vidIdOfPath(gMediaPending);
    vidOpenPath(gMediaPending);
    gMediaPending[0] = 0;
    vidRenderViewer();
    return;
  }
  if(gMlOk && !gRelayout) mlRequestScan();      // lo copiado por otras vias aparece al entrar
  vidScreen = VS_LIST;
  vidListRender();
}

static void vidTick(){
  if(vidScreen == VS_VIEW){
    vidPlaybackTick();
    // Los controles se ocultan solos mientras se reproduce, y tambien
    // sobre una foto (para poder verla entera). En PAUSA se quedan:
    // ahi lo util es tenerlos a mano.
    // Ocultarlos sobre una foto obliga a repintarla, y eso es UNA
    // decodificacion, no una por cuadro: pasa una sola vez, cuando
    // vence la cuenta atras.
    if(vidCtrlOn && (vidPlaying || vidKind == VK_PHOTO)
       && millis() - vidCtrlMs > VID_CTRL_HIDE_MS){
      vidCtrlOn = false;
      if(vidKind == VK_VIDEO) vidDrawCurrentFrame(true);
      else                    vidRenderViewer();
    }
    vidViewerTouch();
    return;
  }
  if(mkTick()) return;                            // menu, dialogos, papelera, hoja del servidor
  // El catalogo cambio (subida del movil, miniatura lista, reconciliacion):
  // como mucho un repintado cada 300 ms, y nunca a mitad de un arrastre.
  if(gMlOk && !vidListDragging && millis() - vidSeenMs >= 300 && mlRev() != vidSeenRev){ vidListRender(); return; }
  if(vidMorePending && !T.down){ vidListRender(); return; }
  vidListTouch();
}

// #############################################################
// ##  MULTIMEDIA · SESION Y LIBERACION
// ##  ------------------------------------------------------
// ##  Suspender PARA la reproduccion de verdad (una app en segundo
// ##  plano no decodifica ni suena) y cerrar suelta TODO: el
// ##  descriptor del archivo, el buffer del fotograma y la tira del
// ##  volcado girado. Lo que se conserva es solo la POSICION, que
// ##  es lo unico que hace falta para retomar.
// #############################################################
#define VID_SESS_VER   2
#define VID_SESS_PATH  FS_DIR_SESS "/media.bin"
struct VidSessV2 {
  int32_t  filter;
  uint32_t resumeKey[VID_RESUME_N];
  uint32_t resumeFrame[VID_RESUME_N];
};

// ATRAS (barra del sistema o gesto) desde el visor vuelve a la lista,
// no expulsa la app: es la misma regla que ya sigue la Galeria con su
// menu, y evita que ver una foto y pulsar atras te saque al
// escritorio perdiendo la lista donde estabas.
static bool vidBackScreen(){
  if(vidScreen != VS_VIEW) return false;
  vidLeaveViewer();
  return true;
}
// En la lista, ATRAS cierra primero el menu, los dialogos y la seleccion.
static bool vidBackLayer(){ return vidScreen == VS_LIST && mkBackLayer(); }

static void vidSuspend(){
  // Una app en segundo plano no decodifica. Se guarda la posicion y se
  // suelta todo; al volver, vidResume reabre por donde iba.
  mkSuspend();
  vidReleaseMedia(true);
  gLand = false;                       // el framework tambien lo hace; aqui por si acaso
}
// SOLTAR SIN CERRAR. vidSuspend ya suelta el fotograma y el descriptor al pasar
// a segundo plano, asi que aqui casi siempre no queda nada -- y entonces se
// devuelve 0, que es lo que hace que el optimizador no se apunte bytes que no
// libero. Existe para el caso en que la app quedara suspendida por otra via.
static size_t vidShed(){
  // Igual que en la Galeria: se comprueba que haya algo pesado ANTES de decir
  // que se solto. vidSuspend ya libera al pasar a segundo plano, asi que lo
  // normal es que aqui no quede nada -- y entonces la respuesta honesta es 0.
  if(vidKind == VK_NONE && !vidPhotoBuf) return 0;
  vidReleaseMedia(true);             // conserva el segundo de reproduccion
  return 1;
}
static void vidResume(){
  mkBind(&VID_APP);
  vidCtrlOn = true; vidCtrlMs = millis();
  if(vidScreen == VS_VIEW){
    if(vidKind == VK_NONE && vidPath[0]) vidOpenPath(vidPath);
    vidRenderViewer();
    return;
  }
  vidListRender();
}
static void vidCloseApp(){
  vidReleaseMedia(true);
  if(mkApp == &VID_APP) mkReset();
  gLand = false;
  vidScreen = VS_LIST;
  gSessLoaded[IC_MULTIMEDIA] = false;
}
static bool vidSaveSess(){
  if(!flexFsReady()) return true;
  VidSessV2 v;
  memset(&v, 0, sizeof(v));
  v.filter = vidFilter;
  for(int i = 0; i < VID_RESUME_N; i++){
    v.resumeKey[i]   = vidResumeTab[i].key;
    v.resumeFrame[i] = vidResumeTab[i].frame;
  }
  return sessWrite(VID_SESS_PATH, VID_SESS_VER, IC_MULTIMEDIA, &v, sizeof(v));
}
static void vidLoadSess(){
  if(!flexFsReady()) return;
  VidSessV2 v;
  if(sessRead(VID_SESS_PATH, VID_SESS_VER, IC_MULTIMEDIA, &v, sizeof(v)) != sizeof(v)) return;
  // (La pestana 3 era "Audio", que ahora vive en Musica: se vuelve a "Todo".)
  vidFilter = (v.filter >= 0 && v.filter < VID_TABS_N) ? (int)v.filter : 0;
  for(int i = 0; i < VID_RESUME_N; i++){
    vidResumeTab[i].key    = v.resumeKey[i];
    vidResumeTab[i].frame  = v.resumeFrame[i];
    vidResumeTab[i].whenMs = 0;
  }
}
