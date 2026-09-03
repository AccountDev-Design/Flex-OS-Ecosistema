// #############################################################
// ##  FLEX OS ULTRA  ·  APP PAINT  ·  galeria + lienzo sobre ficheros reales
// ##  ----------------------------------------------------------
// ##  Cada dibujo es un .fxp en /Paint. El lienzo nunca se repinta entero
// ##  mientras se dibuja y se guarda al levantar el dedo.
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
#include "FlexOS_Ultra_AppCodeIDE.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP PAINT  ·  galeria + lienzo, sobre ficheros REALES
// ##  ------------------------------------------------------
// ##  FORMATO: cada dibujo es un .fxp en /Paint, o sea la lista
// ##  de trazos serializada (ver el bloque de justificacion en
// ##  FlexOS_FS.h). Resumen del porque: un bitmap RGB565 del
// ##  lienzo son ~542 KB por dibujo -- no cabe ni uno en la
// ##  particion de datos del Pro y llena la del Ultra con
// ##  cuatro. Un dibujo por trazos ronda los 3-8 KB, se
// ##  redibuja a cualquier escala (que es exactamente lo que
// ##  necesita la MINIATURA de la galeria: los mismos trazos
// ##  reproducidos mas pequenos, no una imagen aparte) y se
// ##  puede ampliar trazo a trazo sin reescribir el fichero.
// ##
// ##  GUARDADO: al levantar el dedo. No hay boton "guardar" que
// ##  se pueda olvidar, y un apagon solo se lleva el trazo que
// ##  estaba a medias.
// ##
// ##  RENDER: el lienzo NUNCA se repinta entero mientras se
// ##  dibuja. Cada segmento vuelca solo la franja de filas que
// ##  ha tocado (flxFlush(y0, y1)), igual que hacia la version
// ##  anterior de esta app y que el resto del sistema.
// #############################################################
#define P_TOP           96
#define P_BOT           (SCR_H - navBarH() - 66)
#define PAINT_CX        8
#define PAINT_CW        (SCR_W - 16)
#define PAINT_CH        (P_BOT - P_TOP)
#define PAINT_MAX_PTS   512            // puntos por trazo (buffer de trabajo)
#define PAINT_MAX_LIST  16
#define PAINT_COLS       2
#define PAINT_CARD_TOP  118

static const uint16_t P_PAL[6] = { rgb565(30,30,40), rgb565(230,60,60), rgb565(240,150,40),
                                   rgb565(240,210,50), rgb565(80,180,120), rgb565(60,120,235) };
static const uint8_t  P_SIZES[3] = { 3, 6, 12 };

static int       paintView    = 0;                 // 0 = galeria, 1 = lienzo
static FlexFsEntry paintList[PAINT_MAX_LIST];
static int       paintListN   = 0;
static int       paintSelIdx  = -1;
static char      paintPath[FLEXFS_PATH_MAX] = "";
static int       paintScroll  = 0;
static int       paintDragY0 = 0, paintDragS0 = 0;
static bool      paintDragging = false, paintLongFired = false;
static bool      paintMulti   = false;
static uint32_t  paintMask    = 0;

static uint16_t  pColor  = 0;
static int       pSizeIx = 1;
static int16_t*  pStroke = NULL;                   // trazo en curso (PSRAM)
static int       pStrokeN = 0;
static int       pPrevX = -1, pPrevY = -1;
// Transformacion documento -> pantalla. Los .fxp ya guardan su ancho/alto en
// FlexPaintHdr; se respetan para que un dibujo creado antes de reservar la
// barra de navegacion no pierda sus 64 px inferiores. Solo se recalcula al
// abrir/repintar el lienzo, nunca durante un trazo.
static int       pDocW = PAINT_CW, pDocH = PAINT_CH;
static int       pDocX = PAINT_CX, pDocY = P_TOP;
static int       pDocDispW = PAINT_CW, pDocDispH = PAINT_CH;
static float     pDocScale = 1.0f;

static void paintRenderGallery();
static void paintRenderCanvas();
static int  paintMaxScroll();

static void paintDocLayout(){
  FlexPaintHdr hd;
  pDocW = PAINT_CW; pDocH = PAINT_CH;
  if(paintPath[0] && flexPaintHeader(paintPath, &hd) && hd.w > 0 && hd.h > 0){
    pDocW = hd.w; pDocH = hd.h;
  }
  float sx = (float)PAINT_CW / (float)pDocW;
  float sy = (float)PAINT_CH / (float)pDocH;
  pDocScale = sx < sy ? sx : sy;
  if(pDocScale <= 0.0f) pDocScale = 1.0f;
  pDocDispW = (int)(pDocW * pDocScale + 0.5f);
  pDocDispH = (int)(pDocH * pDocScale + 0.5f);
  if(pDocDispW < 1) pDocDispW = 1; if(pDocDispW > PAINT_CW) pDocDispW = PAINT_CW;
  if(pDocDispH < 1) pDocDispH = 1; if(pDocDispH > PAINT_CH) pDocDispH = PAINT_CH;
  pDocX = PAINT_CX + (PAINT_CW - pDocDispW) / 2;
  pDocY = P_TOP + (PAINT_CH - pDocDispH) / 2;
}
static bool paintDocHit(int x, int y){
  return x >= pDocX && x < pDocX + pDocDispW && y >= pDocY && y < pDocY + pDocDispH;
}
static int16_t paintDocCoord(int v, int org, int lim){
  int q = (int)(((float)(v - org) / pDocScale) + 0.5f);
  if(q < 0) q = 0; if(q >= lim) q = lim - 1;
  return (int16_t)q;
}
static uint8_t paintDocRadius(){
  int r = (int)((float)P_SIZES[pSizeIx] / pDocScale + 0.5f);
  if(r < 1) r = 1; if(r > 255) r = 255;
  return (uint8_t)r;
}

// Buffer de TRABAJO del trazo en curso. Va a PSRAM cuando la placa la
// tiene (Ultra y Ultra S3): son 2 KB que no hacen falta en la RAM
// interna, que es el recurso escaso. En el Pro, sin PSRAM, cae al heap
// normal -- y si tampoco hubiera, la app lo dice en vez de dibujar sin
// poder guardar.
static void paintBufInit(){
  if(pStroke) return;
  size_t bytes = (size_t)PAINT_MAX_PTS * 2 * sizeof(int16_t);
  if(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0)
    pStroke = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if(!pStroke) pStroke = (int16_t*)malloc(bytes);
}

// ---- Reproduccion de trazos -------------------------------------------
// El MISMO callback sirve para el lienzo a tamano real y para las
// miniaturas: solo cambia la escala con la que flexPaintReplay entrega
// los segmentos. Por eso una miniatura no puede "no parecerse" al
// dibujo: es el dibujo.
static void paintSegCb(int x0, int y0, int x1, int y1, uint16_t color, int radius, void* user){
  (void)user;
  if(x0 == x1 && y0 == y1) fillCircleAA((float)x0, (float)y0, (float)radius, color);
  else                     strokeSegAA((float)x0, (float)y0, (float)x1, (float)y1, (float)radius, color);
}

static void paintPathOf(int i, char* out, size_t n){
  snprintf(out, n, "%s/%s", FLEXFS_DIR_PAINT, paintList[i].name);
}

static void paintReload(){
  paintListN = flexFsList(FLEXFS_DIR_PAINT, paintList, PAINT_MAX_LIST);
  int maxS = paintMaxScroll();
  if(paintScroll < 0) paintScroll = 0;
  if(paintScroll > maxS) paintScroll = maxS;
}

// ---- Galeria ----------------------------------------------------------
static void paintCardRect(int i, int &x, int &y, int &w, int &h){
  int col = i % PAINT_COLS, row = i / PAINT_COLS;
  w = (SCR_W - 3 * 16) / PAINT_COLS;
  h = (int)(w * (float)PAINT_CH / (float)PAINT_CW);      // misma proporcion que el lienzo
  if(h > 340) h = 340;
  x = 16 + col * (w + 16);
  y = PAINT_CARD_TOP + row * (h + 46) - paintScroll;
}

static int paintMaxScroll(){
  int x, y, w, h; paintCardRect(0, x, y, w, h);
  int rows = (paintListN + PAINT_COLS - 1) / PAINT_COLS;
  int need = PAINT_CARD_TOP + rows * (h + 46) + 40;
  int m = need - (SCR_H - 60);
  return m > 0 ? m : 0;
}

static void paintFabRect(int &x, int &y, int &r){ r = 52; x = SCR_W - 76; y = SCR_H - 146; }

static void paintRenderGallery(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  uiHdrDraw("Paint", 5, TH_TXT, TH_NAV, true);

  // Mismo criterio que en Notas: el nombre va encima de la tarjeta, asi
  // que sin viewport se colaba en la cabecera al subir la galeria.
  uiClipViewport(UIHDR_ZONE + 4, SCR_H - 1);
  if(paintListN == 0){
    drawTextC(SCR_W / 2, 320, "No hay dibujos todav\xC3\xAD" "a", 3, TH_TXT2);
    drawTextC(SCR_W / 2, 364, "Pulsa + para crear uno", 1, TH_MUTE);
  }
  for(int i = 0; i < paintListN; i++){
    int x, y, w, h; paintCardRect(i, x, y, w, h);
    if(y + h < 60 || y > SCR_H) continue;
    char title[FLEXFS_NAME_MAX];
    flexFsStem(paintList[i].name, title, sizeof(title));
    drawTextC(x + w / 2, y - 34, title, 2, TH_TXT);
    fillRoundRect(x, y, w, h, 12, rgb565(255,255,255));

    // MINIATURA REAL: se reproducen los trazos del fichero a escala.
    char p[FLEXFS_PATH_MAX]; paintPathOf(i, p, sizeof(p));
    FlexPaintHdr ph;
    int dw = PAINT_CW, dh = PAINT_CH;
    if(flexPaintHeader(p, &ph) && ph.w > 0 && ph.h > 0){ dw = ph.w; dh = ph.h; }
    float scx = (float)(w - 8) / (float)dw;
    float scy = (float)(h - 8) / (float)dh;
    float sc = scx < scy ? scx : scy;
    int rw = (int)(dw * sc + 0.5f), rh = (int)(dh * sc + 0.5f);
    int rox = x + (w - rw) / 2, roy = y + (h - rh) / 2;
    int savedX0 = gClipX0, savedX1 = gClipX1, savedY0 = gClipY0, savedY1 = gClipY1;
    // INTERSECCION, no sustitucion: si la tarjeta esta a medio salir por
    // arriba, su miniatura no puede escaparse del viewport de la galeria.
    gClipX0 = max(savedX0, x + 4);     gClipX1 = min(savedX1, x + w - 4);
    gClipY0 = max(savedY0, y + 4);     gClipY1 = min(savedY1, y + h - 4);
    flexPaintReplay(p, sc, rox, roy, paintSegCb, NULL);
    gClipX0 = savedX0; gClipX1 = savedX1; gClipY0 = savedY0; gClipY1 = savedY1;

    if(paintMulti && (paintMask & (1UL << i))){
      drawRoundRect(x, y, w, h, 12, TH_PRIM);
      drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, TH_PRIM);
      fillCircle(x + w - 18, y + 18, 10, TH_PRIM);
      strokeSegAA(x + w - 23, y + 18, x + w - 20, y + 22, 2.2f, TH_ONACC);
      strokeSegAA(x + w - 20, y + 22, x + w - 13, y + 13, 2.2f, TH_ONACC);
    }
  }
  uiClipFull();                     // barra de seleccion, boton flotante y menu, encima
  if(paintMulti){
    int by = SCR_H - 128;
    if(uiGlass) drawLiquidGlassPanel(12, by, SCR_W - 24, 60, 16, TH_GLASS2);
    else fillRoundRect(12, by, SCR_W - 24, 60, 16, TH_SURF2);
    drawText(28, by + 20, "Selecci\xC3\xB3n", 2, TH_TXT);
    drawTextR(SCR_W - 140, by + 20, "Papelera", 2, TH_WARN);
    drawTextR(SCR_W - 28,  by + 20, "Salir", 2, TH_TXT2);
  } else {
    int fx, fy, fr; paintFabRect(fx, fy, fr);
    drawTextR(fx - fr - 6, fy - 34, "Nuevo dibujo", 2, TH_TXT);
    fillCircle(fx, fy, fr, TH_BORDER);
    fillCircle(fx, fy, fr - 5, TH_SURF);
    fillRoundRect(fx - 22, fy - 5, 44, 10, 4, TH_TXT);
    fillRoundRect(fx - 5, fy - 22, 10, 44, 4, TH_TXT);
  }
  if(fkMenuOn) fkMenuDraw();
  flxFlushAll();
}

// ---- Lienzo -----------------------------------------------------------
static void paintTools(){
  setBuf(fb);
  int y = P_BOT + 8, sw = 34, gap = 6, x0 = 10;
  fillRect(0, P_BOT, SCR_W, SCR_H - navBarH() - P_BOT, thCard());
  for(int i = 0; i < 6; i++){
    int cx = x0 + i * (sw + gap) + sw / 2;
    fillCircle(cx, y + sw / 2, sw / 2 - 2, P_PAL[i]);
    if(P_PAL[i] == pColor){
      drawCircle(cx, y + sw / 2, sw / 2, TH_TXT);
      drawCircle(cx, y + sw / 2, sw / 2 - 1, TH_TXT);
    }
  }
  // Grosor: tres puntos de tamano real (lo que se ve es lo que se pinta).
  int gx = x0 + 6 * (sw + gap) + 8;
  fillRoundRect(gx, y, 44, sw, 8, TH_SURF2);
  fillCircle(gx + 22, y + sw / 2, P_SIZES[pSizeIx], TH_TXT);
  fillRoundRect(gx + 52, y, 76, sw, 8, TH_SURF2);
  drawTextC(gx + 90, y + 9, "Deshacer", 1, TH_TXT);
  fillRoundRect(gx + 134, y, 66, sw, 8, TH_DANGER);
  drawTextC(gx + 167, y + 9, "Limpiar", 1, TH_ONACC);
  flxFlush(P_BOT, SCR_H - navBarH() - 1);
}

static void paintRenderCanvas(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, P_TOP, TH_PAGE);
  strokeSegAA(30, 26, 18, 18, 2.4f, TH_NAV);
  strokeSegAA(18, 18, 30, 10, 2.4f, TH_NAV);
  char title[FLEXFS_NAME_MAX];
  flexFsStem(paintPath, title, sizeof(title));
  drawTextC(SCR_W / 2, 30, title, 3, TH_TXT);
  FlexPaintHdr hd;
  if(flexPaintHeader(paintPath, &hd)){
    char sub[48];
    snprintf(sub, sizeof(sub), "%u trazos guardados", (unsigned)hd.strokes);
    drawTextC(SCR_W / 2, 66, sub, 1, TH_TXT2);
  }
  paintDocLayout();
  fillRect(PAINT_CX, P_TOP, PAINT_CW, PAINT_CH, rgb565(218,222,230));
  fillRect(pDocX, pDocY, pDocDispW, pDocDispH, rgb565(250,250,252));
  // El lienzo se reconstruye desde el FICHERO, no desde un buffer en RAM:
  // lo que se ve al abrir un dibujo es exactamente lo que hay guardado.
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gClipX0 = pDocX; gClipX1 = pDocX + pDocDispW - 1;
  gClipY0 = pDocY; gClipY1 = pDocY + pDocDispH - 1;
  flexPaintReplay(paintPath, pDocScale, pDocX, pDocY, paintSegCb, NULL);
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
  paintTools();
  flxFlushAll();
}

static void paintOpen(int i){
  if(i < 0 || i >= paintListN) return;
  paintPathOf(i, paintPath, sizeof(paintPath));
  paintBufInit();
  pStrokeN = 0; pPrevX = pPrevY = -1;
  paintView = 1;
  paintRenderCanvas();
  sessMarkDirty(IC_PAINT);
}

// Nuevo dibujo: crea el FICHERO ya (cabecera .fxp con 0 trazos) con el
// primer numero libre de /Paint, y entra a el. Aparece en la lista
// porque existe en el disco, no porque se haya anadido a un array.
static void paintNew(){
  char full[FLEXFS_PATH_MAX];
  if(!flexFsNewName(FLEXFS_DIR_PAINT, "Dibujo", FLEXFS_EXT_PAINT, full, sizeof(full))) return;
  if(!flexPaintCreate(full, PAINT_CW, PAINT_CH)) return;
  // Hay un archivo nuevo en /Paint: el indice de medios que comparten
  // Galeria y Multimedia queda caducado y se reconstruye la proxima
  // vez que se abra una de las dos. Es lo que hace que un dibujo
  // recien creado aparezca en la Galeria sin refrescar a mano.
  mediaIndexInvalidate();
  paintReload();
  for(int i = 0; i < paintListN; i++){
    char p[FLEXFS_PATH_MAX]; paintPathOf(i, p, sizeof(p));
    if(!strcmp(p, full)){ paintOpen(i); return; }
  }
  paintRenderGallery();
}

// Cierra el trazo en curso escribiendolo en el fichero.
static void paintFlushStroke(){
  if(pStrokeN > 0 && pStroke && paintPath[0])
    flexPaintAppend(paintPath, pColor, paintDocRadius(), pStroke, (uint16_t)pStrokeN);
  pStrokeN = 0; pPrevX = pPrevY = -1;
}

static void paintMenuAction(int act){
  char p[FLEXFS_PATH_MAX];
  if(paintSelIdx >= 0 && paintSelIdx < paintListN) paintPathOf(paintSelIdx, p, sizeof(p));
  else p[0] = 0;
  if(act == FK_ACT_SEL){
    paintMulti = true; paintMask = 0;
    if(paintSelIdx >= 0) paintMask |= (1UL << paintSelIdx);
  } else if(act == FK_ACT_DEL){
    if(p[0]){
      char stem[FLEXFS_NAME_MAX]; flexFsStem(paintList[paintSelIdx].name, stem, sizeof(stem));
      fkAskOpen("\xC2\xBF" "Borrar definitivamente?", stem);
      return;
    }
  } else if(act == FK_ACT_REN){
    if(p[0]){
      char stem[FLEXFS_NAME_MAX]; flexFsStem(paintList[paintSelIdx].name, stem, sizeof(stem));
      fkNameOpen("Renombrar dibujo", stem);
      return;
    }
  } else if(act == FK_ACT_TRASH){
    if(p[0]){ flexFsTrash(p); paintSelIdx = -1; paintReload(); }
    else { fkTrashOpen(); return; }               // sin seleccion: abre la papelera
  } else if(act == FK_ACT_VAULT){
    // FLEX VAULT: el dibujo se cifra y sale de /Paint. Dentro de la boveda se
    // sigue VIENDO: la Galeria privada lo descifra a RAM y reproduce sus trazos
    // desde ahi (flexPaintReplayMem), sin escribir ningun .fxp en claro.
    if(p[0]){
      paintSelIdx = -1;
      if(vaultMoveRequest(p, FXV_KIND_PHOTO)){
        if(gState == ST_VAULT) return;
        paintReload();
      }
    }
  }
  paintRenderGallery();
}

static void paintGalleryTick(){
  if(fkTrashOn){ if(!fkTrashTick()){ paintReload(); paintRenderGallery(); } return; }
  if(fkAskOn){
    int r = fkAskTick();
    if(r == 1 && paintSelIdx >= 0 && paintSelIdx < paintListN){
      char p[FLEXFS_PATH_MAX]; paintPathOf(paintSelIdx, p, sizeof(p));
      flexFsDelete(p);
      paintSelIdx = -1; paintReload();
    }
    if(r != 0) paintRenderGallery();
    return;
  }
  if(fkNameOn){
    int r = fkNameTick();
    if(r == 1 && paintSelIdx >= 0 && paintSelIdx < paintListN){
      char p[FLEXFS_PATH_MAX]; paintPathOf(paintSelIdx, p, sizeof(p));
      flexFsRename(p, fkNameBuf);
      paintSelIdx = -1; paintReload();
    }
    if(r != 0) paintRenderGallery();
    return;
  }
  if(fkMenuOn){
    if(T.tap){
      int a = fkMenuHit(T.x, T.y);
      fkMenuOn = false;
      if(a >= 0) paintMenuAction(a);
      else       paintRenderGallery();
    }
    return;
  }

  int maxS = paintMaxScroll();
  if(T.pressed){ paintDragY0 = T.y; paintDragS0 = paintScroll; paintDragging = false; }
  if(T.down && maxS > 0){
    int dy = paintDragY0 - T.y;
    if(!paintDragging && abs(dy) > 8) paintDragging = true;
    if(paintDragging){
      int ns = paintDragS0 + dy;
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != paintScroll){ paintScroll = ns; paintRenderGallery(); }
      paintLongFired = true;
      return;
    }
  }
  if(T.released && paintDragging){ paintDragging = false; sessMarkDirty(IC_PAINT); return; }

  if(T.down && !paintLongFired && (millis() - T.downMs) > 550
     && abs(T.x - T.startX) < 14 && abs(T.y - T.startY) < 14){
    for(int i = 0; i < paintListN; i++){
      int x, y, w, h; paintCardRect(i, x, y, w, h);
      if(T.startX >= x && T.startX <= x + w && T.startY >= y && T.startY <= y + h){
        paintLongFired = true; paintSelIdx = i;
        fkMenuOpenV(T.x, T.y - 40, true);   // con "Mover a Carpeta segura"
        return;
      }
    }
    paintLongFired = true;
  }
  if(!T.down) paintLongFired = false;
  if(!T.tap) return;

  if(uiHdrMenuHit(T.x, T.y)){ paintSelIdx = -1; fkMenuOpen(SCR_W - 40, UIHDR_ZONE); return; }
  if(uiHdrBackHit(T.x, T.y)){ appClose(); return; }

  if(paintMulti){
    int by = SCR_H - 128;
    if(T.y >= by && T.y <= by + 60){
      if(T.x > SCR_W - 100){ paintMulti = false; paintMask = 0; paintRenderGallery(); return; }
      if(T.x > SCR_W - 230){
        for(int i = 0; i < paintListN; i++) if(paintMask & (1UL << i)){
          char p[FLEXFS_PATH_MAX]; paintPathOf(i, p, sizeof(p));
          flexFsTrash(p);
        }
        paintMulti = false; paintMask = 0; paintReload(); paintRenderGallery(); return;
      }
    }
  } else {
    int fx, fy, fr; paintFabRect(fx, fy, fr);
    long ddx = T.x - fx, ddy = T.y - fy;
    if(ddx * ddx + ddy * ddy <= (long)fr * fr){ paintNew(); return; }
  }
  for(int i = 0; i < paintListN; i++){
    int x, y, w, h; paintCardRect(i, x, y, w, h);
    if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + h){
      if(paintMulti){ paintMask ^= (1UL << i); paintRenderGallery(); }
      else paintOpen(i);
      return;
    }
  }
}

static void paintCanvasTick(){
  // ---- Trazo: interpolacion suave y volcado SOLO de la franja tocada ----
  if(T.down && paintDocHit(T.x, T.y)){
    int rad = P_SIZES[pSizeIx];
    setBuf(fb);
    int y0, y1;
    if(pPrevX >= 0){
      // Se descartan los puntos casi pegados: el GT911 entrega muestras muy
      // juntas y guardarlas todas engorda el fichero sin cambiar el trazo.
      int ddx = T.x - pPrevX, ddy = T.y - pPrevY;
      if(ddx * ddx + ddy * ddy < 4) return;
      strokeSegAA((float)pPrevX, (float)pPrevY, (float)T.x, (float)T.y, (float)rad, pColor);
      y0 = min(pPrevY, T.y); y1 = max(pPrevY, T.y);
    } else {
      fillCircleAA((float)T.x, (float)T.y, (float)rad, pColor);
      y0 = y1 = T.y;
    }
    if(pStroke && pStrokeN < PAINT_MAX_PTS){
      pStroke[pStrokeN * 2]     = paintDocCoord(T.x, pDocX, pDocW);
      pStroke[pStrokeN * 2 + 1] = paintDocCoord(T.y, pDocY, pDocH);
      pStrokeN++;
    } else if(pStroke && pStrokeN >= PAINT_MAX_PTS){
      // Trazo larguisimo: se cierra y se empieza otro, sin perder nada.
      int lx = pPrevX, ly = pPrevY;
      paintFlushStroke();
      pStroke[0] = paintDocCoord(lx, pDocX, pDocW);
      pStroke[1] = paintDocCoord(ly, pDocY, pDocH);
      pStrokeN = 1;
    }
    pPrevX = T.x; pPrevY = T.y;
    flxFlush(y0 - rad - 1, y1 + rad + 1);
    return;
  }
  // ---- Dedo levantado: el trazo se ESCRIBE en el fichero ----
  if(!T.down && pStrokeN > 0) paintFlushStroke();
  if(!T.down){ pPrevX = pPrevY = -1; }

  if(!T.tap) return;
  if(T.x < 48 && T.y < 48){                       // volver a la galeria
    paintFlushStroke();
    paintView = 0; paintReload(); paintRenderGallery();
    sessMarkDirty(IC_PAINT);
    return;
  }
  int y = P_BOT + 8, sw = 34, gap = 6, x0 = 10;
  if(T.y >= y - 4 && T.y <= y + sw + 4){
    for(int i = 0; i < 6; i++){
      int cx = x0 + i * (sw + gap) + sw / 2;
      if(T.x >= cx - sw / 2 && T.x <= cx + sw / 2){ pColor = P_PAL[i]; paintTools(); sessMarkDirty(IC_PAINT); return; }
    }
    int gx = x0 + 6 * (sw + gap) + 8;
    if(T.x >= gx && T.x < gx + 44){ pSizeIx = (pSizeIx + 1) % 3; paintTools(); sessMarkDirty(IC_PAINT); return; }
    if(T.x >= gx + 52 && T.x < gx + 128){         // DESHACER real: recorta el fichero
      if(flexPaintUndo(paintPath)) paintRenderCanvas();
      return;
    }
    if(T.x >= gx + 134){                          // LIMPIAR real: deja el .fxp a 0 trazos
      if(flexPaintClear(paintPath)) paintRenderCanvas();
      return;
    }
  }
}

static void paintEnter(){
  paintBufInit();
  if(!flexFsReady()){ fkNoFsScreen("Paint"); return; }
  if(!pStroke){                                   // sin memoria no se puede guardar: se dice
    setBuf(fb);
    fillRect(0, 0, SCR_W, SCR_H, rgb565(14,16,24));
    drawTextC(SCR_W / 2, 320, "Sin memoria para el lienzo", 2, rgb565(240,140,140));
    flxFlushAll();
    return;
  }
  bool restoreCanvas = (paintView == 1 && paintPath[0] && flexFsExists(paintPath));
  if(!restoreCanvas) pColor = P_PAL[0];
  paintMulti = false; paintMask = 0; paintSelIdx = -1;
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  if(restoreCanvas){ paintRenderCanvas(); return; }
  paintView = 0; paintPath[0] = 0;
  paintReload();
  paintRenderGallery();
}

static void paintTick(){
  if(!flexFsReady() || !pStroke){ if(T.tap && T.x < 60 && T.y < 60) appClose(); return; }
  if(paintView == 0) paintGalleryTick();
  else               paintCanvasTick();
}

// Estado de interfaz; los trazos siguen viviendo solamente en su .fxp.
#define PAINT_SESS_VER  1
#define PAINT_SESS_PATH FS_DIR_SESS "/paint.bin"
struct PaintSessV1 { uint8_t view, sizeIx; uint16_t color; int32_t scroll; char path[FLEXFS_PATH_MAX]; };

static bool paintSaveSess(){
  if(!flexFsReady()) return true;
  paintFlushStroke();
  PaintSessV1 v; memset(&v, 0, sizeof(v));
  v.view = (paintView == 1 && paintPath[0]) ? 1 : 0;
  v.sizeIx = (uint8_t)pSizeIx; v.color = pColor; v.scroll = paintScroll;
  snprintf(v.path, sizeof(v.path), "%s", paintPath);
  return sessWrite(PAINT_SESS_PATH, PAINT_SESS_VER, IC_PAINT, &v, sizeof(v));
}
static void paintLoadSess(){
  if(!flexFsReady()) return;
  PaintSessV1 v;
  if(sessRead(PAINT_SESS_PATH, PAINT_SESS_VER, IC_PAINT, &v, sizeof(v)) != sizeof(v)) return;
  v.path[sizeof(v.path) - 1] = 0;
  paintView = (v.view == 1 && v.path[0] && flexFsExists(v.path)) ? 1 : 0;
  pSizeIx = v.sizeIx < 3 ? v.sizeIx : 1;
  pColor = v.color; paintScroll = v.scroll > 0 ? v.scroll : 0;
  snprintf(paintPath, sizeof(paintPath), "%s", paintView == 1 ? v.path : "");
}
static bool paintBackScreen(){
  if(paintView != 1) return false;
  paintFlushStroke(); paintView = 0; paintPath[0] = 0;
  paintReload(); paintRenderGallery(); sessMarkDirty(IC_PAINT);
  return true;
}
static void paintSuspend(){
  paintFlushStroke(); paintDragging = false; pPrevX = pPrevY = -1;
  // Superficie de dialogos compartida: se cierra al suspender para que otra
  // app no herede una confirmacion o un renombrado de Paint. El lienzo, ruta,
  // herramientas y scroll siguen exactamente donde estaban.
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
}
static void paintResume(){
  if(paintView == 1 && paintPath[0] && flexFsExists(paintPath)) paintRenderCanvas();
  else { paintView = 0; paintPath[0] = 0; paintReload(); paintRenderGallery(); }
}
// CAMBIOS SIN GUARDAR: hay un trazo en curso que todavia no se ha volcado al
// fichero (paintFlushStroke lo escribe y pone pStrokeN a 0).
static bool paintDirty(){ return pStrokeN > 0; }

static void paintCloseApp(){
  paintSuspend();
  fkMenuOn = false; fkNameOn = false; fkAskOn = false; fkTrashOn = false;
  if(pStroke){ free(pStroke); pStroke = NULL; }
  pStrokeN = 0; paintView = 0; paintPath[0] = 0; paintScroll = 0;
  gSessLoaded[IC_PAINT] = false;
}

