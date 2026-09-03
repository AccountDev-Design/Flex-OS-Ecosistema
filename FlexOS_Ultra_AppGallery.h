// #############################################################
// ##  FLEX OS ULTRA  ·  GALERIA  ·  la biblioteca visual real
// ##  ----------------------------------------------------------
// ##  Se alimenta del indice de medios; miniaturas decodificadas por lotes
// ##  y apertura delegada en el visor de Multimedia.
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

// #############################################################
// ##  GALERIA  ·  LA BIBLIOTECA VISUAL REAL DE FLEX OS
// ##  ------------------------------------------------------
// ##  De donde sale lo que se ve: del INDICE DE MEDIOS que
// ##  construye el nucleo de medios recorriendo, por lotes y sin
// ##  bloquear, las carpetas de la particion interna
// ##  (/Documentos, /Paint) y las de la tarjeta (las de Flex OS y
// ##  las de siempre: DCIM, Download, Pictures, Movies, Music).
// ##  Es el MISMO indice que usa Multimedia: si aqui aparece un
// ##  archivo, alli tambien, y al reves.
// ##
// ##  MINIATURAS. Se decodifican a un tamano FIJO
// ##  (GAL_THUMB_W x GAL_THUMB_H) y se guardan en una cache
// ##  pequena de PSRAM. Solo se generan las de las celdas
// ##  VISIBLES, y como mucho GAL_THUMB_PER_PASS por repintado:
// ##  desplazarse por 300 fotos no puede convertirse en 300
// ##  decodificaciones seguidas. Antes cada repintado
// ##  redecodificaba TODOS los JPEG de la rejilla.
// ##
// ##  ABRIR UN ELEMENTO no duplica el visor: se lo pasa al de
// ##  Multimedia, que es el que sabe ajustar sin deformar, girar,
// ##  ampliar y reproducir. Un segundo visor aqui seria un segundo
// ##  motor de orientacion, que es justo lo que no puede haber.
// ##
// ##  Lo que NO hace: inventarse fotos. Si no hay ninguna, lo
// ##  dice; y mientras la camara real no exista, no se simula ni
// ##  un archivo de camara.
// #############################################################
#define GAL_COLS            3
#define GAL_CACHE_N        16      // cubre una pantalla llena de celdas y sobra
#define GAL_THUMB_PER_PASS  2      // decodificaciones nuevas por repintado
#define GAL_SEL_MAX        32      // tope REAL de la seleccion multiple
#define GAL_JPEG_MAX_BYTES (768 * 1024)   // por encima, no se hace miniatura

// Pestanas. Las tres primeras filtran por CLASE y las dos ultimas
// por VOLUMEN: son las cinco agrupaciones que se pidieron.
#define GAL_TABS 5
static const char* GAL_TAB_NAME[GAL_TABS] = {
  "Todas", "Fotos", "V\xC3\xAD" "deos", "Interna", "Tarjeta SD"
};
static int galTab = 0;
static int galTabKind(){
  switch(galTab){ case 1: return FLEXMED_PHOTO; case 2: return FLEXMED_VIDEO; default: return 0; }
}
static int galTabVol(){
  switch(galTab){ case 3: return FLEXMED_VOL_INT; case 4: return FLEXMED_VOL_SD; default: return -1; }
}
static int galCount(){ return flexMediaIndexCount(&gMedIx, galTabKind(), galTabVol()); }
static int galItemAt(int nth){ return flexMediaIndexNth(&gMedIx, galTabKind(), galTabVol(), nth); }

// ---- Estado de la rejilla ----
static int      galScroll = 0;
static int      galSelIdx = -1;              // posicion en la LISTA filtrada
static bool     galLongFired = false;
static bool     galMulti = false;
static uint16_t galSel[GAL_SEL_MAX];
static uint8_t  galSelN = 0;
static int      galDragY0 = 0, galDragS0 = 0;
static bool     galDragging = false;
static bool     galMorePending = false;      // quedaron miniaturas por generar
static char     galResumePath[FLEXMED_PATH_MAX] = ""; // identidad estable, no un indice
static void galRender();

// Ruta del elemento `nth` de la lista filtrada (cadena vacia si no hay).
static const char* galPathOf(int nth){
  int idx = galItemAt(nth);
  return (idx >= 0) ? gMedStore[idx].path : "";
}
static const char* galNameOf(int nth){
  const char* p = galPathOf(nth);
  const char* s = strrchr(p, '/');
  return s ? s + 1 : p;
}
static int galKindOf(int nth){
  int idx = galItemAt(nth);
  return (idx >= 0) ? (int)gMedStore[idx].kind : FLEXMED_NONE;
}
static uint32_t galSizeOf(int nth){
  int idx = galItemAt(nth);
  return (idx >= 0) ? gMedStore[idx].size : 0;
}

// ---- Seleccion multiple ----
// Un array corto en vez de una mascara de bits: con el indice
// llegando a cientos de elementos, un uint32 solo habria podido
// seleccionar los 32 primeros SIN decirlo. Aqui el tope es explicito
// y se puede seleccionar cualquier elemento.
static bool galIsSel(int i){
  for(int k = 0; k < galSelN; k++) if(galSel[k] == (uint16_t)i) return true;
  return false;
}
static void galToggleSel(int i){
  for(int k = 0; k < galSelN; k++)
    if(galSel[k] == (uint16_t)i){
      for(int j = k; j < galSelN - 1; j++) galSel[j] = galSel[j + 1];
      galSelN--;
      return;
    }
  if(galSelN < GAL_SEL_MAX) galSel[galSelN++] = (uint16_t)i;
}
static void galClearSel(){ galSelN = 0; }

// -------------------------------------------------------------
//  CACHE DE MINIATURAS
//  ------------------------------------------------------------
//  Tamano fijo por entrada, numero fijo de entradas y sustitucion
//  por la menos usada recientemente. Las imagenes van a PSRAM y se
//  reservan a demanda: una galeria vacia no gasta ni un byte.
//
//  Una miniatura que FALLA se marca como fallida y no se reintenta
//  en cada repintado: un JPEG progresivo o un fichero corrupto no
//  puede costar una decodificacion por cuadro para siempre.
// -------------------------------------------------------------
static GalThumb galCache[GAL_CACHE_N];
static bool     galCacheInit = false;

static void galCacheReset(){
  for(int i = 0; i < GAL_CACHE_N; i++){
    if(galCache[i].px){ mediaFree(galCache[i].px); galCache[i].px = NULL; }
    galCache[i].path[0] = 0;
    galCache[i].state = GTH_EMPTY;
    galCache[i].w = galCache[i].h = 0;
    galCache[i].useMs = 0;
    galCache[i].pass  = 0;
  }
  galCacheInit = true;
}
static void galCacheFree(){ galCacheReset(); galCacheInit = false; }

static int galCacheFind(const char* path){
  for(int i = 0; i < GAL_CACHE_N; i++)
    if(galCache[i].state != GTH_EMPTY && !strcmp(galCache[i].path, path)) return i;
  return -1;
}
// Repintado en curso. Sirve para que una miniatura nueva no expulse a
// otra que YA se ha dibujado en este mismo pase: si lo hiciera, con la
// pantalla llena de celdas la cache se pisaria a si misma y cada
// repintado volveria a decodificarlo todo -- justo lo que la cache
// existe para evitar.
static uint32_t galPass = 0;

// Ranura libre; si no hay, la que lleva mas tiempo sin usarse de
// entre las que NO se han usado en este repintado. Si todas se han
// usado, devuelve -1 y la celda se dibuja con su marco.
static int galCacheSlot(){
  int best = -1;
  for(int i = 0; i < GAL_CACHE_N; i++){
    if(galCache[i].state == GTH_EMPTY) return i;
    if(galCache[i].pass == galPass) continue;              // en uso AHORA
    if(best < 0 || galCache[i].useMs < galCache[best].useMs) best = i;
  }
  return best;
}

// Destino de la decodificacion de una miniatura.
static GalThumb* galThumbDst = NULL;
static bool galThumbRow(void* user, int y, int w, const uint16_t* rgb){
  (void)user;
  GalThumb* t = galThumbDst;
  if(!t || !t->px) return false;
  if(y >= GAL_THUMB_H) return false;
  int n = w > GAL_THUMB_W ? GAL_THUMB_W : w;
  if(n > 0) memcpy(t->px + (size_t)y * GAL_THUMB_W, rgb, (size_t)n * 2);
  if(y + 1 > t->h) t->h = (uint16_t)(y + 1);
  if(n > t->w)     t->w = (uint16_t)n;
  return true;
}

// Reproduce un dibujo de Paint dentro de la miniatura.
static int galPaintOX = 0, galPaintOY = 0;
static GalThumb* galPaintDst = NULL;
static void galThumbSeg(int x0, int y0, int x1, int y1, uint16_t color, int radius, void* user){
  (void)user; (void)radius;
  GalThumb* t = galPaintDst;
  if(!t || !t->px) return;
  // Trazo de un pixel de grosor sobre el lienzo de la miniatura: es
  // una vista previa, no el dibujo a tamano real.
  int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for(int guard = 0; guard < 4096; guard++){
    int px = galPaintOX + x0, py = galPaintOY + y0;
    if((unsigned)px < GAL_THUMB_W && (unsigned)py < GAL_THUMB_H)
      t->px[(size_t)py * GAL_THUMB_W + px] = color;
    if(x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if(e2 >= dy){ err += dy; x0 += sx; }
    if(e2 <= dx){ err += dx; y0 += sy; }
  }
}

// Genera la miniatura de `path`. Devuelve la ranura, o -1.
static int galThumbBuild(const char* path, int kind, uint32_t size){
  int slot = galCacheSlot();
  if(slot < 0) return -1;
  GalThumb* t = &galCache[slot];
  if(!t->px){
    t->px = (uint16_t*)mediaAlloc((size_t)GAL_THUMB_W * GAL_THUMB_H * 2);
    if(!t->px) return -1;                     // sin PSRAM: se dibuja el marco y ya
  }
  snprintf(t->path, sizeof(t->path), "%s", path);
  t->useMs = millis();
  t->pass  = galPass;
  t->w = t->h = 0;
  t->state = GTH_FAIL;                        // hasta que se demuestre lo contrario
  // Fondo de la miniatura: un gris neutro, para que una imagen que
  // no llena la caja no ensene basura de la entrada anterior.
  for(size_t i = 0; i < (size_t)GAL_THUMB_W * GAL_THUMB_H; i++) t->px[i] = rgb565(24,26,34);

  if(kind == FLEXMED_DRAW){
    FlexPaintHdr hd;
    if(flexPaintHeader(path, &hd) && hd.w && hd.h){
      for(size_t i = 0; i < (size_t)GAL_THUMB_W * GAL_THUMB_H; i++) t->px[i] = rgb565(250,250,252);
      int ow, oh; mediaFitBox(hd.w, hd.h, GAL_THUMB_W, GAL_THUMB_H, ow, oh);
      galPaintOX = (GAL_THUMB_W - ow) / 2;
      galPaintOY = (GAL_THUMB_H - oh) / 2;
      galPaintDst = t;
      flexPaintReplay(path, (float)ow / (float)hd.w, 0, 0, galThumbSeg, NULL);
      galPaintDst = NULL;
      t->w = GAL_THUMB_W; t->h = GAL_THUMB_H;
      t->state = GTH_OK;
    }
    return slot;
  }

  // Video: la miniatura es el PRIMER fotograma REAL del archivo, no
  // un icono generico. Cuesta una lectura y una decodificacion, la
  // misma que una foto, porque en MJPEG un fotograma ES un JPEG.
  if(kind == FLEXMED_VIDEO){
    MediaStream st;
    memset(&st, 0, sizeof(st));
    if(!mediaStreamOpen(&st, path)) return slot;
    FlexMediaIO io; mediaBindIO(&io, &st);
    FlexAviCtx a;
    if(flexAviOpen(&a, &io) == FLEXAVI_OK){
      // Un fotograma de portada cabe de sobra en 96 KB; si no cabe,
      // no se hace miniatura y se ensena el marco con la insignia.
      const uint32_t cap = 96 * 1024;
      uint8_t* fbuf = (uint8_t*)mediaAlloc(cap);
      if(fbuf){
        int n = flexAviReadFrame(&a, fbuf, cap, NULL);
        if(n > 0){
          galThumbDst = t;
          if(flexJpegDecode(fbuf, (size_t)n, GAL_THUMB_W, GAL_THUMB_H, 0, NULL,
                            galThumbRow, NULL, mediaAlloc, mediaFree) == FLEXJPG_OK)
            t->state = GTH_OK;
          galThumbDst = NULL;
        }
        mediaFree(fbuf);
      }
    }
    mediaStreamClose(&st);
    return slot;
  }

  if(kind != FLEXMED_PHOTO) return slot;
  if(size == 0 || size > GAL_JPEG_MAX_BYTES) return slot;
  uint8_t* buf = (uint8_t*)mediaAlloc(size);
  if(!buf) return slot;
  int rd = mediaReadWhole(path, buf, size);
  if(rd > 0){
    galThumbDst = t;
    // maxW/maxH = la caja: el decodificador elige el divisor y NO
    // construye antes la version grande. Es lo que hace que una foto
    // de 8 MP cueste una miniatura y no 8 megapixeles de trabajo.
    if(flexJpegDecode(buf, (size_t)rd, GAL_THUMB_W, GAL_THUMB_H, 0, NULL,
                      galThumbRow, NULL, mediaAlloc, mediaFree) == FLEXJPG_OK)
      t->state = GTH_OK;
    galThumbDst = NULL;
  }
  mediaFree(buf);
  return slot;
}

// Vuelca una miniatura cacheada en la celda, centrada y sin estirar.
static void galBlitThumb(const GalThumb* t, int x, int y, int w, int h){
  if(!t || !t->px || t->w == 0 || t->h == 0) return;
  int ox = x + (w - t->w) / 2, oy = y + (h - t->h) / 2;
  for(int ry = 0; ry < t->h; ry++){
    int dy = oy + ry;
    if(dy < gClipY0 || dy > gClipY1 || dy < 0 || dy >= SCR_H) continue;
    const uint16_t* src = t->px + (size_t)ry * GAL_THUMB_W;
    if(gLand){                                   // hospedada en horizontal
      for(int rx = 0; rx < t->w; rx++) px(ox + rx, dy, src[rx]);
      continue;
    }
    int dx = ox, n = t->w;
    if(dx < gClipX0){ src += (gClipX0 - dx); n -= (gClipX0 - dx); dx = gClipX0; }
    if(dx + n > gClipX1 + 1) n = gClipX1 + 1 - dx;
    if(dx < 0){ src -= dx; n += dx; dx = 0; }
    if(dx + n > SCR_W) n = SCR_W - dx;
    if(n > 0) memcpy(gBuf + (size_t)dy * SCR_W + dx, src, (size_t)n * 2);
  }
}

// Dibuja el contenido de una celda: la miniatura si esta o se puede
// hacer ahora, y si no un marco honesto con el nombre. `budget`
// lleva la cuenta de las decodificaciones que quedan en este pase.
static void galDrawCell(int nth, int x, int y, int w, int h, int &budget){
  const char* path = galPathOf(nth);
  if(!path[0]) return;
  int slot = galCacheFind(path);
  if(slot < 0){
    if(budget <= 0){ galMorePending = true; }
    else {
      budget--;
      slot = galThumbBuild(path, galKindOf(nth), galSizeOf(nth));
    }
  }
  if(slot >= 0){
    galCache[slot].useMs = millis();
    galCache[slot].pass  = galPass;
    if(galCache[slot].state == GTH_OK){ galBlitThumb(&galCache[slot], x, y, w, h); return; }
  }
  // Sin miniatura: marco con la clase y el nombre. Nunca una imagen
  // inventada en el hueco de un archivo que no se pudo leer.
  fillRect(x, y, w, h, rgb565(28,30,38));
  const int k = galKindOf(nth);
  const char* tag = (k == FLEXMED_VIDEO) ? "VIDEO"
                  : (k == FLEXMED_AUDIO) ? "AUDIO"
                  : (k == FLEXMED_DRAW)  ? "DIBUJO" : "JPEG";
  drawTextC(x + w / 2, y + h / 2 - 10, tag, 2, rgb565(150,156,170));
  if(slot >= 0 && galCache[slot].state == GTH_FAIL)
    drawTextC(x + w / 2, y + h / 2 + 12, "sin vista previa", 1, rgb565(120,124,140));
}

// ---- Geometria de la rejilla ----
static int galHeadH(){ return 104; }            // titulo + pestanas
static void galCellRect(int i, int &x, int &y, int &w, int &h){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad(), gap = uiGap();
  w = (bw - 2 * pad - (GAL_COLS - 1) * gap) / GAL_COLS;
  h = w;
  int c = i % GAL_COLS, r = i / GAL_COLS;
  x = bx + pad + c * (w + gap);
  y = by + galHeadH() + r * (h + 26) - galScroll;
}
static int galMaxScroll(){
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int x, y, w, h; galCellRect(0, x, y, w, h);
  int rows = (galCount() + GAL_COLS - 1) / GAL_COLS;
  int need = galHeadH() + rows * (h + 26) + 40;
  int m = need - bh;
  return m > 0 ? m : 0;
}

// Insignia de video sobre la miniatura: un triangulo dentro de un
// circulo, en la esquina. Es informacion (esto se reproduce), no
// decoracion.
static void galVideoBadge(int x, int y, int w, int h){
  int cx = x + w - 20, cy = y + h - 20;
  fillCircle(cx, cy, 13, rgb565(0,0,0));
  fillCircle(cx, cy, 12, rgb565(255,255,255));
  fillTriangle(cx - 4, cy - 6, cx - 4, cy + 6, cx + 6, cy, rgb565(20,22,30));
}
static void galSdBadge(int x, int y){
  fillRoundRect(x + 8, y + 8, 22, 14, 4, rgb565(0,0,0));
  drawTextC(x + 19, y + 11, "SD", 1, rgb565(230,234,244));
}

static void galRenderGrid(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad();
  galMorePending = false;
  galPass++;
  int budget = GAL_THUMB_PER_PASS;

  drawText(bx + pad, by + 14, "Galer\xC3\xAD" "a", 4, TH_TXT);
  { char cnt[48];
    int n = galCount();
    snprintf(cnt, sizeof(cnt), "%d elemento%s", n, n == 1 ? "" : "s");
    drawTextR(bx + bw - pad, by + 26, cnt, 1, TH_TXT2); }
  for(int i = 0; i < 3; i++) fillCircle(bx + bw - pad - 4, by + 52 + i * 12, 4, TH_NAV);

  // ---- Pestanas ----
  const int tabY = by + 62, tw = (bw - 2 * pad) / GAL_TABS;
  for(int i = 0; i < GAL_TABS; i++){
    int tx = bx + pad + i * tw;
    if(i == galTab) fillRoundRect(tx + 2, tabY, tw - 4, 28, 14, TH_PRIM);
    drawTextC(tx + tw / 2, tabY + 8, GAL_TAB_NAME[i], 1, i == galTab ? TH_ONACC : TH_TXT2);
  }

  const int n = galCount();
  // Estado REAL. Cada caso dice algo distinto porque son problemas
  // distintos: no es lo mismo "no hay fotos" que "no hay memoria".
  if(mediaIndexBusy()){
    char pr[56];
    snprintf(pr, sizeof(pr), "Indexando... %u revisados, %d encontrados",
             (unsigned)mediaIndexSeen(), mediaIndexN());
    drawTextC(bx + bw / 2, by + galHeadH() - 16, pr, 1, TH_TXT2);
  } else if(gMedIx.full){
    drawTextC(bx + bw / 2, by + galHeadH() - 16,
              "Hay mas archivos de los que caben en el " "\xC3\xAD" "ndice", 1, TH_WARN);
  }
  if(n == 0 && !mediaIndexBusy()){
    if(!flexFsReady() && !flexSdReady()){
      drawTextC(bx + bw / 2, by + bh / 2 - 20, "Sin almacenamiento", 3, TH_TXT2);
      drawTextC(bx + bw / 2, by + bh / 2 + 16, flexFsError(), 1, TH_MUTE);
    } else if(galTab == 4 && !flexSdReady()){
      drawTextC(bx + bw / 2, by + bh / 2 - 20, "Sin tarjeta SD", 3, TH_TXT2);
      drawTextC(bx + bw / 2, by + bh / 2 + 16, flexSdError(), 1, TH_MUTE);
    } else {
      drawTextC(bx + bw / 2, by + bh / 2 - 30, "No hay nada aqu" "\xC3\xAD", 3, TH_TXT2);
      drawTextC(bx + bw / 2, by + bh / 2 + 6,
                "JPEG y AVI MJPEG de la memoria interna", 1, TH_MUTE);
      drawTextC(bx + bw / 2, by + bh / 2 + 26, "y de la tarjeta SD", 1, TH_MUTE);
    }
  }

  for(int i = 0; i < n; i++){
    int x, y, w, h; galCellRect(i, x, y, w, h);
    // SOLO LAS VISIBLES. Las celdas fuera de pantalla ni se dibujan
    // ni piden miniatura: por eso una galeria de 300 fotos cuesta lo
    // mismo que una de 9.
    if(y + h < by + galHeadH() - 40 || y > by + bh) continue;
    int rad = w / 10; if(rad < 3) rad = 3;
    fillRoundRect(x, y, w, h, rad, TH_SURF2);
    int ox0 = gClipX0, ox1 = gClipX1, oy0 = gClipY0, oy1 = gClipY1;
    gClipX0 = x; gClipX1 = x + w - 1; gClipY0 = y; gClipY1 = y + h - 1;
    galDrawCell(i, x + 2, y + 2, w - 4, h - 4, budget);
    gClipX0 = ox0; gClipX1 = ox1; gClipY0 = oy0; gClipY1 = oy1;

    if(galKindOf(i) == FLEXMED_VIDEO) galVideoBadge(x, y, w, h);
    { int idx = galItemAt(i);
      if(idx >= 0 && gMedStore[idx].vol == FLEXMED_VOL_SD) galSdBadge(x, y); }

    if(galMulti && galIsSel(i)){
      drawRoundRect(x, y, w, h, rad, TH_PRIM);
      drawRoundRect(x + 1, y + 1, w - 2, h - 2, rad, TH_PRIM);
      fillCircle(x + w - 16, y + 16, 10, TH_PRIM);
      strokeSegAA(x + w - 21, y + 16, x + w - 17, y + 21, 2.2f, TH_ONACC);
      strokeSegAA(x + w - 17, y + 21, x + w - 10, y + 11, 2.2f, TH_ONACC);
    }
    drawTextClip(x, y + h + 4, galNameOf(i), 1, TH_TXT2, x + w);
  }

  if(galMulti){
    int aby = by + bh - 68;
    if(uiGlass) drawLiquidGlassPanel(bx + 12, aby, bw - 24, 56, 16, TH_GLASS2);
    else        fillRoundRect(bx + 12, aby, bw - 24, 56, 16, TH_SURF2);
    char sel[40];
    snprintf(sel, sizeof(sel), "%u de %d (m\xC3\xA1x. %d)", (unsigned)galSelN, GAL_SEL_MAX, GAL_SEL_MAX);
    drawText(bx + 28, aby + 10, "Selecci\xC3\xB3n", 2, TH_TXT);
    drawText(bx + 28, aby + 32, sel, 1, TH_TXT2);
    drawTextR(bx + bw - 140, aby + 18, "Papelera", 2, TH_WARN);
    drawTextR(bx + bw - 28,  aby + 18, "Salir", 2, TH_TXT2);
  }
  if(fkMenuOn) fkMenuDraw();
  flxFlush(WIN_TOP, WIN_BOT);
}

static void galRender(){ galRenderGrid(); }

// -------------------------------------------------------------
//  ABRIR UN ELEMENTO
//  ------------------------------------------------------------
//  Se lo lleva el visor de Multimedia, que es el unico de todo el
//  sistema: ajuste sin deformar, orientacion Auto/Vertical/
//  Horizontal, zoom, desplazamiento y reproduccion. Al volver del
//  visor se regresa AQUI, porque de aqui se salio.
// -------------------------------------------------------------
// OJO: aqui NO se decide a donde se vuelve. Lo decide quien abre
// (gMediaReturnApp), porque esta misma funcion la llaman la Galeria y
// el Explorador, y volver siempre a la Galeria dejaria al usuario en
// una app en la que no estaba.
static void galOpenPath(const char* path){
  if(!path || !path[0]) return;
  mediaOpenInPlayer(path);
}

// ---- Acciones del menu contextual (con Flex Vault) ----
// En la tarjeta SOLO SE LEE: borrar, renombrar, mover a la papelera
// y cifrar en la boveda quedan fuera. Son archivos del usuario y la
// papelera y la boveda viven ademas en la particion interna.
static bool galSdReadOnlyGuard(const char* p){
  if(!flexSdIsSdPath(p)) return false;
  mediaNotify(MOD_SDCARD, "Solo lectura en la tarjeta",
              "Flex OS no borra ni mueve tus archivos");
  return true;
}

static void galMenuAction(int act){
  char p[FLEXMED_PATH_MAX];
  snprintf(p, sizeof(p), "%s", (galSelIdx >= 0) ? galPathOf(galSelIdx) : "");

  if(act == FK_ACT_SEL){
    galMulti = true; galClearSel();
    if(galSelIdx >= 0) galToggleSel(galSelIdx);
  } else if(act == FK_ACT_DEL){
    if(p[0]){
      if(galSdReadOnlyGuard(p)){ galRender(); return; }
      fkAskOpen("\xC2\xBF" "Borrar definitivamente?", galNameOf(galSelIdx));
      return;
    }
  } else if(act == FK_ACT_REN){
    if(p[0]){
      if(galSdReadOnlyGuard(p)){ galRender(); return; }
      char stem[FLEXFS_NAME_MAX]; flexFsStem(galNameOf(galSelIdx), stem, sizeof(stem));
      fkNameOpen("Renombrar", stem);
      return;
    }
  } else if(act == FK_ACT_TRASH){
    if(p[0]){
      if(galSdReadOnlyGuard(p)){ galRender(); return; }
      flexFsTrash(p); galSelIdx = -1; mediaIndexRescan();
    } else { fkTrashOpen(); return; }
  } else if(act == FK_ACT_VAULT){
    // FLEX VAULT: la imagen (o el dibujo) se cifra dentro de la
    // boveda y desaparece de la galeria normal. Igual que antes.
    if(p[0]){
      if(galSdReadOnlyGuard(p)){ galRender(); return; }
      galSelIdx = -1;
      if(vaultMoveRequest(p, FXV_KIND_PHOTO)){
        if(gState == ST_VAULT) return;         // se fue a pedir la clave
        mediaIndexRescan();
      }
    }
  }
  galRender();
}

static void galTick(){
  // --- Dialogos modales del kit de ficheros ---
  if(fkTrashOn){ if(!fkTrashTick()){ mediaIndexRescan(); galRender(); } return; }
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && galSelIdx >= 0){
      const char* p = galPathOf(galSelIdx);
      if(p[0] && !flexSdIsSdPath(p)) flexFsDelete(p);   // borrado DEFINITIVO real
      galSelIdx = -1; mediaIndexRescan();
    }
    if(r != 0) galRender();
    return;
  }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && galSelIdx >= 0){
      const char* p = galPathOf(galSelIdx);
      if(p[0] && !flexSdIsSdPath(p)) flexFsRename(p, fkNameBuf);
      galSelIdx = -1; mediaIndexRescan();
    }
    if(r != 0) galRender();
    return;
  }
  if(fkMenuOn){
    if(T.tap){
      int a = fkMenuHit(T.x, T.y);
      fkMenuOn = false;
      if(a >= 0) galMenuAction(a);
      else       galRender();
    }
    return;
  }

  // Mientras el indice crece, o mientras queden miniaturas por
  // generar, se repinta -- pero NO en cada vuelta: solo cuando hay
  // algo nuevo que ensenar. Asi la rejilla se rellena sola sin
  // gastar cuadros en repintar lo mismo.
  static int galLastN = -1;
  if(mediaIndexBusy()){
    int n = galCount();
    if(n != galLastN){ galLastN = n; galRender(); return; }
  }
  if(galMorePending && !T.down){ galRender(); return; }

  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();

  // --- Scroll de la rejilla ---
  int maxS = galMaxScroll();
  if(T.pressed){ galDragY0 = T.y; galDragS0 = galScroll; galDragging = false; }
  if(T.down && maxS > 0){
    if(!galDragging && abs(T.y - galDragY0) > 10) galDragging = true;
    if(galDragging){
      int ns = galDragS0 + (galDragY0 - T.y);
      if(ns < 0) ns = 0;
      if(ns > maxS) ns = maxS;
      if(ns != galScroll){ galScroll = ns; galRender(); }
      return;
    }
  }

  // --- Pulsacion larga: menu del elemento, con Flex Vault ---
  if(!gHosted && !gLand && T.down && !galLongFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    int n = galCount();
    for(int i = 0; i < n; i++){
      int x, y, w, h; galCellRect(i, x, y, w, h);
      if(T.startX >= x && T.startX <= x + w && T.startY >= y && T.startY <= y + h){
        galLongFired = true; galSelIdx = i;
        fkMenuOpenV(T.x, T.y - 40, true);
        return;
      }
    }
    galLongFired = true;
  }
  if(!T.down) galLongFired = false;
  if(!T.tap) return;
  if(galDragging){ galDragging = false; return; }

  // --- Pestanas ---
  const int tabY = by + 62, tw = (bw - 2 * pad) / GAL_TABS;
  if(T.y >= tabY && T.y <= tabY + 28){
    int k = (T.x - bx - pad) / (tw > 0 ? tw : 1);
    if(k >= 0 && k < GAL_TABS && k != galTab){
      galTab = k; galScroll = 0; galSelIdx = -1;
      galMulti = false; galClearSel();
      if(galTab == 4){ flexSdPoke(); }         // al pedir la tarjeta, comprobarla
      galRender();
    }
    return;
  }

  // --- Barra del modo seleccion ---
  if(galMulti){
    int aby = by + bh - 68;
    if(T.y >= aby && T.y <= aby + 56){
      if(T.x > bx + bw - 100){ galMulti = false; galClearSel(); galRender(); return; }
      if(T.x > bx + bw - 230){
        for(int k = 0; k < galSelN; k++){
          const char* p = galPathOf(galSel[k]);
          if(p[0] && !flexSdIsSdPath(p)) flexFsTrash(p);
        }
        galMulti = false; galClearSel(); galSelIdx = -1;
        mediaIndexRescan(); galRender();
        return;
      }
      return;
    }
  }
  // --- Menu de la app (papelera) ---
  if(!galMulti && T.x > bx + bw - pad - 26 && T.y < by + 56){
    galSelIdx = -1;
    fkMenuOpen(bx + bw - 120, by + 70);
    return;
  }
  // --- Toque sobre un elemento: se abre en el visor ---
  { int n = galCount();
    for(int i = 0; i < n; i++){
      int x, y, w, h; galCellRect(i, x, y, w, h);
      if(T.x < x || T.x > x + w || T.y < y || T.y > y + h) continue;
      if(galMulti){ galToggleSel(i); galRender(); return; }
      galSelIdx = i;
      snprintf(galResumePath, sizeof(galResumePath), "%s", galPathOf(i));
      gMediaReturnApp = IC_GALERIA;          // al cerrar el visor, de vuelta aqui
      galOpenPath(galPathOf(i));
      return;
    } }
}

static void galEnter(){
  if(!galCacheInit) galCacheReset();
  // gRelayout = true significa "re-dibuja con la geometria nueva", no
  // "empieza de cero": conserva la vista y el scroll del usuario.
  if(!gRelayout){
    galSelIdx = -1; galScroll = 0;
    galMulti = false; galClearSel();
    fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  }
  // Al abrir la Galeria se comprueba la tarjeta en el acto y se
  // reindexa si hace falta (si no, no se toca nada).
  flexSdPoke();
  flexSdTick();
  mediaIndexEnsure();
  galRender();
}

// ATRAS deshace primero las superficies/selecciones propias de Galeria.
static bool galBackLayer(){
  if(fkMenuOn || fkNameOn || fkAskOn || fkTrashOn){
    fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
    galRender();
    return true;
  }
  if(galMulti){
    galMulti = false; galClearSel(); galSelIdx = -1;
    galRender();
    return true;
  }
  return false;
}
// La vista a pantalla completa ya no vive aqui (la tiene Multimedia),
// asi que no hay una segunda pantalla propia que deshacer.
static bool galBackScreen(){ return false; }

static void galSuspend(){
  galResumePath[0] = 0;
  if(galSelIdx >= 0) snprintf(galResumePath, sizeof(galResumePath), "%s", galPathOf(galSelIdx));
  galDragging = false; galLongFired = false;
  // El kit de archivos es global y tambien lo usan Paint, Notas y
  // Archivos: no puede quedar modal mientras otra app lo reutiliza.
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  // Una seleccion por indices deja de ser segura si otra app cambia
  // la lista en segundo plano. Se descarta solo esa capa transitoria.
  galMulti = false; galClearSel();
}

static void galResume(){
  flexSdPoke();
  flexSdTick();
  mediaIndexEnsure();
  galSelIdx = -1;
  if(galResumePath[0]){
    int n = galCount();
    for(int i = 0; i < n; i++)
      if(!strcmp(galPathOf(i), galResumePath)){ galSelIdx = i; break; }
  }
  int maxS = galMaxScroll();
  if(galScroll < 0) galScroll = 0;
  if(galScroll > maxS) galScroll = maxS;
  galDragging = false; galLongFired = false;
  galRender();
}

// Cerrar la app suelta la cache de miniaturas ENTERA. Son cientos de
// KB de PSRAM que no tienen por que seguir reservados cuando la
// Galeria no esta abierta.
// SOLTAR SIN CERRAR. La cache de miniaturas son cientos de KB de PSRAM que la
// Galeria sabe reconstruir sola: al volver, las celdas visibles se decodifican
// otra vez. El estado logico -- carpeta, foto seleccionada, desplazamiento --
// no se toca, asi que la app vuelve exactamente donde estaba.
static size_t galShed(){
  if(!galCacheInit) return 0;
  // Se cuentan las miniaturas que REALMENTE estaban decodificadas. Con la
  // cache inicializada pero vacia no se ha soltado nada, y devolver 1 marcaria
  // la app como "Estado guardado" sin que fuera verdad.
  size_t live = 0;
  for(int i = 0; i < GAL_CACHE_N; i++) if(galCache[i].px) live++;
  galCacheFree();
  return live;                       // los BYTES los mide quien llama; esto es el hecho
}

static void galCloseApp(){
  galCacheFree();
  galSelIdx = -1; galScroll = 0;
  galMulti = false; galClearSel();
}

