// #############################################################
// ##  FLEX OS ULTRA  ·  MODO PC / DeX  ·  dibujo
// ##  ----------------------------------------------------------
// ##  Composicion del escritorio horizontal, marcos de ventana, barra de
// ##  tareas y el hospedaje de una app vertical dentro de una ventana.
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
#include "FlexOS_Ultra_DeX.h"   // eslabon anterior de la cadena

// #############################################################
// ##  DIBUJO
// #############################################################

// Panel glass para LANDSCAPE (Modo PC): drawLiquidGlassPanel escribe en coords
// portrait y no rota, asi que aqui uso primitivas rotacion-aware
// (fillRoundRectA). Translucido + borde, sin blur.
static void pcGlassPanel(int x, int y, int w, int h, int rad, uint16_t tint){
  // Mismo resguardo que ya tienen fillRoundRect/fillRoundRectA/
  // drawLiquidGlassPanelEx: con una ventana lo bastante chica (o un rad grande a
  // proposito) un rad sin recortar dejaria manchas en las esquinas en vez de la
  // curva limpia.
  if(rad < 0) rad = 0;
  if(2 * rad > w) rad = w / 2;
  if(2 * rad > h) rad = h / 2;
  fillRoundRectA(x, y, w, h, rad, tint, 205);
  drawRoundRect(x, y, w, h, rad, TH_BORDER);
}
// Superficie estandar de DeX: sigue el MISMO flag uiGlass del resto del sistema.
static void dexSurface(int x, int y, int w, int h, int rad, uint16_t tint){
  if(uiGlass) pcGlassPanel(x, y, w, h, rad, tint);
  else { fillRoundRect(x, y, w, h, rad, tint); drawRoundRect(x, y, w, h, rad, DEX_BORDER); }
}

// ---- Fondo propio del modo DeX (distinto al wallpaper del Home) ----
// dexWallpaperDraw pinta el fondo en el buffer activo. Es CARO (degradado a lo
// alto + dos arcos), asi que solo se ejecuta al construir la cache: una vez al
// entrar y cada vez que cambia la variante o el modo claro/oscuro.
static void dexWallpaperDraw(int bx, int bw){
  uint16_t a, b;
  // WALLPAPER de Modo PC: es CONTENIDO (el fondo de escritorio), no chrome. Aun
  // asi tiene variante clara y oscura propias, como cualquier wallpaper dinamico,
  // y las elige el mismo gDark del sistema.
  if(dexWall == 0){      a = gDark ? rgb565(10,16,38)  : rgb565(150,186,236);
                         b = gDark ? rgb565(28,64,124) : rgb565(226,236,250); }
  else if(dexWall == 1){ a = gDark ? rgb565(28,12,44)  : rgb565(214,196,238);
                         b = gDark ? rgb565(86,36,96)  : rgb565(246,236,252); }
  else {                 a = gDark ? rgb565(6,30,32)   : rgb565(176,222,214);
                         b = gDark ? rgb565(16,78,84)  : rgb565(232,246,242); }
  for(int ly = 0; ly < LH; ly++)
    hLine(bx, ly, bw, mix565(a, b, (uint8_t)(ly * 255 / (LH - 1))));
  uint16_t g = rgb565(255,255,255);                 // dos arcos suaves: profundidad sin blur
  if(!dexCull(LW - 270, 300)) fillCircle(LW - 120, 70, 150, mix565(b, g, 18));
  if(!dexCull(-100, 380))     fillCircle(90, LH - 90, 190, mix565(a, g, 12));
}
// Construye (o reconstruye) la cache del fondo. Si no hay PSRAM para el buffer
// se sigue funcionando: dexWallpaper cae al camino procedural de siempre.
static void dexBgBuild(){
  if(!dexBg){
    dexBg = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_H * 2,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!dexBg){ dexBgWall = 0xFF; return; }
  }
  uint16_t* old = gBuf;
  int c0 = gClipY0, c1 = gClipY1;
  bool land = gLand;
  setBuf(dexBg); gLand = true; gClipY0 = 0; gClipY1 = SCR_H - 1;
  dexWallpaperDraw(0, LW);
  setBuf(old); gLand = land; gClipY0 = c0; gClipY1 = c1;
  dexBgWall = dexWall; dexBgDark = gDark;
}
static void dexBgFree(){                            // no retener 768 KB fuera de DeX
  if(dexBg){ heap_caps_free(dexBg); dexBg = NULL; }
  dexBgWall = 0xFF;
}
// Fondo de un frame: con cache es una copia de filas fisicas (secuencial y sin
// mezcla); la banda sucia son justo esas filas, asi que solo se copia lo que se
// va a volcar.
static void dexWallpaper(){
  int bx = 0, bw = LW;
  if(!dexBand(bx, bw)) return;
  if(dexBg && (dexBgWall != dexWall || dexBgDark != gDark)) dexBgBuild();   // variante o tema cambiados
  if(dexBg && dexBgWall == dexWall && dexBgDark == gDark){
    int b0 = bx, b1 = bx + bw - 1;
    if(b1 > SCR_H - 1) b1 = SCR_H - 1;
    for(int y = b0; y <= b1; y++)
      memcpy(gBuf + (size_t)y * SCR_W, dexBg + (size_t)y * SCR_W, (size_t)SCR_W * 2);
    return;
  }
  dexWallpaperDraw(bx, bw);                         // respaldo sin cache
}

// ---- Iconos vectoriales de la barra ----
static void dexIcoChip(int x, int y, int s, bool on, uint16_t &fgOut){
  uint16_t bg = on ? DEX_ACCENT : TH_SURF2;
  fillRoundRect(x, y, s, s, s / 4, bg);
  fgOut = on ? TH_ONACC : DEX_TXT_HI;
}
static void dexIcoGrid(int x, int y, int s, bool on){            // cajon de apps
  uint16_t c; dexIcoChip(x, y, s, on, c);
  int d = s / 6, g = (s - 3 * d) / 4;
  for(int r = 0; r < 3; r++) for(int q = 0; q < 3; q++)
    fillRoundRect(x + g + q * (d + g), y + g + r * (d + g), d, d, 1, c);
}
static void dexIcoSearch(int x, int y, int s, bool on){          // buscador tipo Finder
  uint16_t c; dexIcoChip(x, y, s, on, c);
  int cx = x + s * 44 / 100, cy = y + s * 44 / 100, r = s / 4;
  drawCircle(cx, cy, r, c); drawCircle(cx, cy, r - 1, c);
  strokeSegAA((float)(cx + r * 7 / 10), (float)(cy + r * 7 / 10),
              (float)(x + s * 80 / 100), (float)(y + s * 80 / 100), 1.7f, c);
}
static void dexIcoBell(int x, int y, int s, bool badge){         // notificaciones
  uint16_t c; dexIcoChip(x, y, s, false, c);
  int cx = x + s / 2, top = y + s / 4;
  fillRoundRect(cx - s / 5, top, 2 * (s / 5), s / 2, s / 6, c);
  fillRoundRect(cx - s / 3, top + s / 2 - 2, 2 * (s / 3), 3, 1, c);
  fillRect(cx - 1, top - 3, 3, 3, c);
  fillCircle(cx, top + s / 2 + 3, 2, c);
  if(badge) fillCircle(x + s - 5, y + 5, 4, TH_ERR);          // punto de aviso: color de ESTADO
}
static void dexIcoGear(int x, int y, int s, bool on){            // ajustes rapidos
  uint16_t c; dexIcoChip(x, y, s, on, c);
  uint16_t bg = on ? DEX_ACCENT : TH_SURF2;
  int cx = x + s / 2, cy = y + s / 2, r = s / 4;
  fillCircle(cx, cy, r, c);
  fillCircle(cx, cy, r / 2, bg);
  for(int k = 0; k < 4; k++){
    int dx = (k == 2) ? -r - 2 : (k == 3) ? r + 2 : 0;
    int dy = (k == 0) ? -r - 2 : (k == 1) ? r + 2 : 0;
    fillRoundRect(cx + dx - 2, cy + dy - 2, 4, 4, 1, c);
  }
}
static void dexIcoPad(int x, int y, int s, bool on){             // touchpad virtual
  uint16_t c; dexIcoChip(x, y, s, on, c);
  int m = s / 5;
  drawRoundRect(x + m, y + m + 1, s - 2 * m, s - 2 * m - 2, 3, c);
  vLine(x + s / 2, y + s - m - 8, 6, c);
}

// ---- Ventana ----
static void dexWinBtnRect(int x, int y, int w, int k, int &bx, int &by, int &bw, int &bh){
  bw = DEX_BTN_W; bh = DEX_TTL_H - 8; by = y + 4;
  bx = x + w - (3 - k) * DEX_BTN_W - 4;
}
// Zona de TOQUE de los controles, mas grande que la dibujada: ocupa toda la
// altura de la barra de titulo y se ensancha hasta la mitad del hueco entre
// botones (asi crecen sin solaparse entre si). El boton dibujado era de 34x26 y
// se fallaba constantemente; con esto el blanco real es de 40x34.
static void dexWinBtnHit(int x, int y, int w, int k, int &bx, int &by, int &bw, int &bh){
  dexWinBtnRect(x, y, w, k, bx, by, bw, bh);
  by = y; bh = DEX_TTL_H;                         // toda la altura de la barra
  if(k == 2) bw += 4;                             // cerrar llega hasta el borde
}
static void dexWinButtons(int i, int x, int y, int w, bool active){
  uint16_t fg = active ? DEX_TXT_HI : DEX_TXT_LO;
  for(int k = 0; k < 3; k++){
    int bx, by, bw, bh; dexWinBtnRect(x, y, w, k, bx, by, bw, bh);
    if(k == 2) fillRoundRectA(bx, by, bw, bh, 5, TH_DANGER, active ? 200 : 90);   // cerrar: accion destructiva
    int cx = bx + bw / 2, cy = by + bh / 2;
    uint16_t c = (k == 2) ? TH_ONACC : fg;
    if(k == 0){                                                  // minimizar
      fillRect(cx - 5, cy + 3, 11, 2, c);
    } else if(k == 1){                                           // maximizar / restaurar
      if(pwins[i].snap != SNAP_FREE){
        drawRoundRect(cx - 5, cy - 2, 9, 8, 1, c);
        drawRoundRect(cx - 2, cy - 5, 9, 8, 1, c);
      } else drawRoundRect(cx - 5, cy - 4, 11, 9, 1, c);
    } else {                                                     // cerrar
      strokeSegAA((float)(cx - 4), (float)(cy - 4), (float)(cx + 4), (float)(cy + 4), 1.5f, c);
      strokeSegAA((float)(cx - 4), (float)(cy + 4), (float)(cx + 4), (float)(cy - 4), 1.5f, c);
    }
  }
}

// -------------------------------------------------------------
//  HOSTING DE APPS REALES DENTRO DE UNA VENTANA
//  ---------------------------------------------------------
//  Cada ventana tiene su propio lienzo 480x800 (el mismo tamano que la
//  pantalla). La app corre EXACTAMENTE igual que a pantalla completa -- su
//  enter() y su tick() de APP_REG, sin tocar una linea de las 16 apps -- pero
//  con gRtTarget apuntando a ese lienzo, asi que su setBuf(fb) y sus flxFlush
//  no llegan al panel. Despues DeX escala el lienzo dentro del area de cliente
//  de la ventana.
//  Por que un lienzo por ventana y no uno compartido: las apps dibujan de forma
//  INCREMENTAL sobre un framebuffer persistente (un tick que solo repinta una
//  fila cuenta con que el resto sigue ahi). Con un lienzo compartido habria que
//  llamar a enter() en cada refresco, y para varias apps enter() ademas
//  reinicia estado (settingsEnter pone setSel y setScroll a 0), o sea que el
//  scroll y la seleccion se perderian en cada frame.
// -------------------------------------------------------------
struct DexHost {
  uint16_t* surf;      // lienzo 480x800 donde dibuja la app (incremental)
  uint16_t* cache;     // resultado YA escalado, en orden de volcado
  size_t    cap;       // capacidad de cache, en pixeles
  int       fw, fh;    // tamano del contenido con el que se construyo la cache
  int       rw, rh;    // lienzo logico con el que la app dibujo por ultima vez
  uint32_t  reMs;      // ultimo re-render por cambio de tamano (limita la cadencia)
  bool      scaled;    // la cache refleja el lienzo actual
  bool      live;
};
static DexHost dexHost[4];
static bool dexHostBusy = false;              // reentrada: una app no puede hospedar a otra
// Depuracion del mapeo tactil: con DEX_TOUCH_DEBUG a 1 se pinta un punto en la
// coordenada TRADUCIDA, dentro del propio lienzo de la app. Si el punto cae bajo
// el dedo, la traduccion es correcta; si se desvia, el fallo esta en el mapeo.
#define DEX_TOUCH_DEBUG 0
static int dexHostTouchX = -1, dexHostTouchY = -1, dexHostTouchWin = -1;

static void dexClientRect(int i, int &cx, int &cy, int &cw, int &ch){
  cx = pwins[i].x + 1;  cy = pwins[i].y + DEX_TTL_H;
  cw = pwins[i].w - 2;  ch = pwins[i].h - DEX_TTL_H - 1;
  if(cw < 1) cw = 1;
  if(ch < 1) ch = 1;
}
// Modo PC no puede hospedarse a si mismo (seria recursion infinita: pcEnter
// dentro de pcTick). Su ventana se queda con el panel informativo.
static inline bool dexHostable(int app){ return app != IC_MODOPC; }

// Encaje de la app dentro del area de cliente, CONSERVANDO LA PROPORCION
// (letterbox). Es la unica fuente de verdad de la geometria: la usan el
// escalado y el mapeo de toque con los MISMOS pasos en coma fija, asi que no
// puede haber deriva de 1 px entre lo que se ve y lo que se toca.
//
// Antes se estiraba el lienzo a cualquier w/h del marco. Una app de 480x800
// metida en un cliente de 446x249 se comprimia 3.2x en vertical y solo 1.08x en
// horizontal: de ahi la deformacion, y de ahi que un boton de 44x44 acabara
// midiendo 40x13 px en pantalla. Trece pixeles de alto no se aciertan con el
// dedo -- por eso "solo Paint respondia": Paint es un lienzo libre, cualquier
// posicion vale, no tiene que acertar un boton.
static void dexHostFit(int i, int cx, int cy, int cw, int ch, DexFit &f){
  uint8_t fl = APP_REG[pwins[i].app].flags;
  if(cw < 1) cw = 1;
  if(ch < 1) ch = 1;
  f.flex = (fl & APP_FLEX) != 0;
  if(f.flex){
    // App adaptativa: se le da un lienzo del TAMANO REAL del area de cliente y
    // se dibuja 1:1. Ni escalado (nitidez perfecta) ni barras de letterbox (el
    // "espacio vacio"): la app se remaqueta sola contra gAppW/gAppH.
    // La orientacion del lienzo sigue a la forma de la ventana, porque el buffer
    // es de 480x800: en horizontal se usa rotado (hasta 800x480) y en vertical
    // sin rotar (hasta 480x800). Asi cualquier ventana del area util cabe.
    f.land = (cw > ch);
    int maxW = f.land ? LW : SCR_W, maxH = f.land ? LH : SCR_H;
    f.ow = cw < maxW ? cw : maxW;
    f.oh = ch < maxH ? ch : maxH;
    f.aw = f.ow; f.ah = f.oh;                  // lienzo == contenido -> escala 1:1
    f.ox = cx + (cw - f.ow) / 2;
    f.oy = cy + (ch - f.oh) / 2;
    f.stepX = f.stepY = 1u << 16;              // paso exacto: dexStep(idx) == idx
    return;
  }
  f.land = (fl & APP_LAND) != 0;
  f.aw = f.land ? LW : SCR_W;
  f.ah = f.land ? LH : SCR_H;
  f.ow = cw;
  f.oh = (int)(((int64_t)cw * f.ah) / f.aw);
  if(f.oh > ch){ f.oh = ch; f.ow = (int)(((int64_t)ch * f.aw) / f.ah); }
  if(f.ow < 1) f.ow = 1;
  if(f.oh < 1) f.oh = 1;
  f.ox = cx + (cw - f.ow) / 2;                  // centrado: barras iguales a los lados
  f.oy = cy + (ch - f.oh) / 2;
  f.stepX = ((uint32_t)f.aw << 16) / (uint32_t)f.ow;
  f.stepY = ((uint32_t)f.ah << 16) / (uint32_t)f.oh;
}
// Indice de origen para un indice de destino. Coma fija 16.16 y recorte al
// ultimo pixel valido: cierra el off-by-one de la ultima fila y columna, que
// con la division entera podia dar exactamente aw/ah y salirse por uno.
static inline int dexStep(int idx, uint32_t step, int lim){
  int v = (int)(((uint32_t)idx * step) >> 16);
  if(v < 0) v = 0;
  if(v > lim - 1) v = lim - 1;
  return v;
}
// Tamano minimo de una ventana que hospeda una app. Por debajo de ~0.4 de
// escala el texto deja de leerse y los botones bajan de 13 px: la ventana
// existe pero la app es inservible. El suelo se expresa en tamano de CLIENTE y
// se convierte a tamano de ventana con la proporcion de la propia app.
static void dexHostMinSize(int app, int &mw, int &mh){
  mw = DEX_MIN_W; mh = DEX_MIN_H;
  if(!dexHostable(app)) return;
  bool land = (APP_REG[app].flags & APP_LAND) != 0;
  int aw = land ? LW : SCR_W, ah = land ? LH : SCR_H;
  int H = dexWorkBottom();
  int cch = land ? 240 : 330;
  int ccw = (int)(((int64_t)cch * aw) / ah);
  if(cch + DEX_TTL_H + 1 > H){ cch = H - DEX_TTL_H - 1; ccw = (int)(((int64_t)cch * aw) / ah); }
  if(ccw + 2 > LW){ ccw = LW - 2; cch = (int)(((int64_t)ccw * ah) / aw); }
  mw = ccw + 2; mh = cch + DEX_TTL_H + 1;
  if(mw < DEX_MIN_W) mw = DEX_MIN_W;
  if(mh < DEX_MIN_H) mh = DEX_MIN_H;
}
// Tamano inicial: la ventana nace con la PROPORCION de la app, no con un
// 448x284 fijo. Es lo que hace que la escala util (~0.5) no dependa de que el
// usuario acierte a redimensionar bien.
static void dexHostDefaultSize(int app, int &w, int &h){
  bool land = (APP_REG[app].flags & APP_LAND) != 0;
  int aw = land ? LW : SCR_W, ah = land ? LH : SCR_H;
  int H = dexWorkBottom();
  int cch = H - DEX_TTL_H - 21;
  int ccw = (int)(((int64_t)cch * aw) / ah);
  if(ccw > LW - 80){ ccw = LW - 80; cch = (int)(((int64_t)ccw * ah) / aw); }
  w = ccw + 2; h = cch + DEX_TTL_H + 1;
  if(!dexHostable(app)){ w = 448; h = 284; }
}

// Ejecuta enter() y/o tick() de la app con TODO el estado global desviado, y lo
// restaura pase lo que pase. Restaurar gState es lo que impide que una app que
// navega a una subpantalla completa (Ajustes -> Wi-Fi, -> bloqueo) se lleve por
// delante el escritorio: el cambio se descarta y DeX sigue en pie.
static void dexHostRun(int i, bool doEnter, bool doTick, const Touch* inject){
  if(i < 0 || i > 3 || !dexHost[i].surf || dexHostBusy) return;
  int app = pwins[i].app;
  if(!dexHostable(app)) return;

  uint16_t* oBuf = gBuf; bool oLand = gLand;
  int oC0 = gClipY0, oC1 = gClipY1, oX0 = gClipX0, oX1 = gClipX1;
  int oApp = gAppId, oState = gState;
  int oAW = gAppW, oAH = gAppH;
  Touch oT = T;                                      // el T REAL del sistema

  dexHostBusy = true;
  gHosted = true; gHostReq = 0; gHostReqApp = -1;
  gRtTarget = dexHost[i].surf; gRtDirty = false;
  {                                                  // lienzo logico de esta ventana
    int cx, cy, cw, ch; dexClientRect(i, cx, cy, cw, ch);
    DexFit f; dexHostFit(i, cx, cy, cw, ch, f);
    gLand = f.land;                                  // Juegos rota; FLEX rota si la ventana es apaisada
    gAppW = f.aw; gAppH = f.ah;                      // lo que la app usa para maquetar
  }
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  gAppId = app; gState = ST_APP;
  if(inject) T = *inject;                            // toque ya traducido a la app
  setBuf(fb);                                        // -> redirigido al lienzo

  if(doEnter){
    if(!(APP_REG[app].flags & APP_CUSTOM_HEADER)){   // mismo marco que enterApp()
      appDrawChrome(app);
      appDrawHeader(app);
    }
    if(APP_REG[app].enter) APP_REG[app].enter();
    gRtDirty = true;
  }
  if(doTick && APP_REG[app].tick) APP_REG[app].tick();
#if DEX_TOUCH_DEBUG
  if(inject && dexHostTouchWin == i){               // punto en la coordenada TRADUCIDA
    fillCircle(dexHostTouchX, dexHostTouchY, 5, rgb565(255,0,0));
    fillCircle(dexHostTouchX, dexHostTouchY, 2, rgb565(255,255,255));
    gRtDirty = true;
  }
#endif

  // El lienzo con el que ACABA de dibujar la app se anota ANTES de restaurar
  // gAppW/gAppH. Anotarlo despues guardaba 480x800 (el valor restaurado) en vez
  // del tamano real del cliente, asi que dexHostRelayout veia siempre "el
  // tamano ha cambiado" y relanzaba enter() en CADA tick. Para la Calculadora
  // eso significaba volver a poner el display a "0" inmediatamente despues de
  // cada tecla: el toque SI llegaba, pero el resultado se borraba antes de
  // verse. De ahi el "el touch dejo de responder".
  dexHost[i].rw = gAppW; dexHost[i].rh = gAppH;

  gRtTarget = NULL;
  gHosted = false;
  gState = oState; gAppId = oApp;
  gAppW = oAW; gAppH = oAH;
  T = oT;                                            // restaura el T REAL, no el inyectado
  gLand = oLand; gClipY0 = oC0; gClipY1 = oC1; gClipX0 = oX0; gClipX1 = oX1;
  gBuf = oBuf;
  dexHostBusy = false;

  if(gRtDirty){ dexHost[i].scaled = false; dexMarkWin(pwins[i].x, pwins[i].w); dexDirty = true; }
}

static void dexHostClose(int i){
  if(i < 0 || i > 3) return;
  if(dexHost[i].surf){ heap_caps_free(dexHost[i].surf); dexHost[i].surf = NULL; }
  if(dexHost[i].cache){ heap_caps_free(dexHost[i].cache); dexHost[i].cache = NULL; }
  dexHost[i].cap = 0; dexHost[i].fw = dexHost[i].fh = 0;
  dexHost[i].rw = dexHost[i].rh = 0; dexHost[i].reMs = 0;
  dexHost[i].scaled = false; dexHost[i].live = false;
}
// Arranca la app de la ventana i. Si no hay PSRAM para el lienzo se sigue
// adelante sin hosting: la ventana cae al panel decorativo, no se rompe nada.
static void dexHostOpen(int i){
  if(i < 0 || i > 3) return;
  dexHostClose(i);
  if(!dexHostable(pwins[i].app)) return;
  dexHost[i].surf = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_H * 2,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!dexHost[i].surf) return;
  memset(dexHost[i].surf, 0, (size_t)SCR_W * SCR_H * 2);
  dexHost[i].live = true;
  dexHostRun(i, true, false, NULL);
}

// Construye la version escalada del lienzo. Se ejecuta SOLO cuando la app ha
// dibujado algo o cuando cambia el tamano del encaje -- no por frame. Eso es lo
// que permite pagar un filtro de caja: al reducir a menos de dos tercios, el
// vecino mas cercano se salta filas enteras y el texto de las apps se rompe;
// promediando el bloque de origen se mantiene legible.
// La cache se guarda YA EN ORDEN DE VOLCADO (columna logica, fila descendente),
// de modo que componer un frame es un memcpy por columna: es la ruta que corre
// durante el arrastre y las animaciones, y asi es la mas barata posible.
static void dexHostScale(int i, const DexFit &f){
  DexHost &H = dexHost[i];
  size_t need = (size_t)f.ow * (size_t)f.oh;
  if(!H.cache || H.cap < need){
    if(H.cache){ heap_caps_free(H.cache); H.cache = NULL; H.cap = 0; }
    H.cache = (uint16_t*)heap_caps_aligned_alloc(64, need * 2,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    H.cap = H.cache ? need : 0;
  }
  if(!H.cache) return;
  const uint16_t* src = H.surf;
  bool land = f.land;
  bool box  = (f.stepX > (3u << 15)) || (f.stepY > (3u << 15));   // reduccion > 1.5x
  for(int k = 0; k < f.ow; k++){
    int sx  = dexStep(k, f.stepX, f.aw);
    int sx2 = dexStep(k + 1, f.stepX, f.aw);
    if(sx2 < sx) sx2 = sx;
    uint16_t* d = H.cache + (size_t)k * (size_t)f.oh;
    for(int j = 0; j < f.oh; j++){
      int sy  = dexStep(j, f.stepY, f.ah);
      int sy2 = dexStep(j + 1, f.stepY, f.ah);
      if(sy2 < sy) sy2 = sy;
      uint16_t v;
      if(box){
        uint16_t a = land ? src[(size_t)sx  * SCR_W + (SCR_W - 1 - sy)]  : src[(size_t)sy  * SCR_W + sx];
        uint16_t b = land ? src[(size_t)sx2 * SCR_W + (SCR_W - 1 - sy)]  : src[(size_t)sy  * SCR_W + sx2];
        uint16_t c = land ? src[(size_t)sx  * SCR_W + (SCR_W - 1 - sy2)] : src[(size_t)sy2 * SCR_W + sx];
        uint16_t e = land ? src[(size_t)sx2 * SCR_W + (SCR_W - 1 - sy2)] : src[(size_t)sy2 * SCR_W + sx2];
        v = mix565(mix565(a, b, 128), mix565(c, e, 128), 128);
      } else {
        v = land ? src[(size_t)sx * SCR_W + (SCR_W - 1 - sy)] : src[(size_t)sy * SCR_W + sx];
      }
      d[f.oh - 1 - j] = v;                       // orden de volcado (fila fisica ascendente)
    }
  }
  H.fw = f.ow; H.fh = f.oh; H.scaled = true;
}

static void dexHostBlit(int i, int cx, int cy, int cw, int ch){
  DexHost &H = dexHost[i];
  if(!H.surf || cw < 1 || ch < 1) return;
  DexFit f; dexHostFit(i, cx, cy, cw, ch, f);

  // Barras del letterbox. Se pintan SIEMPRE: al reducir la ventana el contenido
  // encoge, y sin esto quedaria asomando el escalado anterior por los lados.
  uint16_t bar = DEX_WIN_BODY;
  if(f.ox > cx)                  fillRect(cx, cy, f.ox - cx, ch, bar);
  if(f.ox + f.ow < cx + cw)      fillRect(f.ox + f.ow, cy, (cx + cw) - (f.ox + f.ow), ch, bar);
  if(f.oy > cy)                  fillRect(f.ox, cy, f.ow, f.oy - cy, bar);
  if(f.oy + f.oh < cy + ch)      fillRect(f.ox, f.oy + f.oh, f.ow, (cy + ch) - (f.oy + f.oh), bar);

  if(H.fw != f.ow || H.fh != f.oh) H.scaled = false;      // se redimensiono -> reescalar
  if(!H.scaled) dexHostScale(i, f);

  int b0 = dexBandLo(), b1 = dexBandHi();
  if(!H.cache){                                          // sin PSRAM: vecino mas cercano al vuelo
    bool land = f.land;
    for(int k = 0; k < f.ow; k++){
      int lx = f.ox + k;
      if(lx < b0 || lx > b1 || (unsigned)lx >= SCR_H) continue;
      int sx = dexStep(k, f.stepX, f.aw);
      uint16_t* d = gBuf + (size_t)lx * SCR_W;
      for(int j = 0; j < f.oh; j++){
        int ly = f.oy + j;
        if((unsigned)ly >= SCR_W) continue;
        int sy = dexStep(j, f.stepY, f.ah);
        d[(SCR_W - 1) - ly] = land ? H.surf[(size_t)sx * SCR_W + (SCR_W - 1 - sy)]
                                   : H.surf[(size_t)sy * SCR_W + sx];
      }
    }
    return;
  }
  for(int k = 0; k < f.ow; k++){                          // ruta normal: memcpy por columna
    int lx = f.ox + k;
    if(lx < b0 || lx > b1 || (unsigned)lx >= SCR_H) continue;
    int y0 = f.oy, n = f.oh;
    if(y0 < 0){ n += y0; y0 = 0; }                        // recorte contra el lienzo fisico
    if(y0 + n > SCR_W) n = SCR_W - y0;
    if(n <= 0) continue;
    int skip = y0 - f.oy;
    memcpy(gBuf + (size_t)lx * SCR_W + (size_t)(SCR_W - (y0 + n)),
           H.cache + (size_t)k * (size_t)f.oh + (size_t)(f.oh - skip - n),
           (size_t)n * 2);
  }
}

// Inversa EXACTA del encaje: mismo dexHostFit, mismos pasos, misma funcion
// dexStep. No puede desviarse de lo que se ve porque es literalmente el mismo
// calculo. Devuelve false si el punto cae en las barras del letterbox: ahi no
// hay app, y entregar ese toque haria que la app reaccionara a algo que el
// usuario no ha tocado.
static bool dexHostMapT(int i, int cx, int cy, int cw, int ch, int px, int py, int &tx, int &ty){
  DexFit f; dexHostFit(i, cx, cy, cw, ch, f);
  if(px < f.ox || px >= f.ox + f.ow || py < f.oy || py >= f.oy + f.oh) return false;
  int ax = dexStep(px - f.ox, f.stepX, f.aw);
  int ay = dexStep(py - f.oy, f.stepY, f.ah);
  if(f.flex){
    // La app adaptativa dibuja en coordenadas de lienzo, asi que el toque va en
    // esas mismas: sin intercambio de ejes. La rotacion, si el lienzo es
    // horizontal, ya la resuelven las primitivas via gLand.
    tx = ax; ty = ay;
  } else if(f.land){
    tx = (SCR_W - 1) - ay;                      // inversa de lx = T.y, ly = SCR_W-1-T.x
    ty = ax;
  } else { tx = ax; ty = ay; }
  int limX = f.flex ? f.aw : SCR_W, limY = f.flex ? f.ah : SCR_H;
  if(!f.flex && f.land){ limX = SCR_W; limY = SCR_H; }
  if(tx < 0) tx = 0;
  if(tx > limX - 1) tx = limX - 1;
  if(ty < 0) ty = 0;
  if(ty > limY - 1) ty = limY - 1;
  return true;
}
static void dexHostTouch(int i, int cx, int cy, int cw, int ch){
  if(!dexHost[i].surf) return;
  int tx, ty;
  if(!dexHostMapT(i, cx, cy, cw, ch, pX, pY, tx, ty)) return;   // barra: no es la app
  int sx = tx, sy = ty;
  dexHostMapT(i, cx, cy, cw, ch, dexPressX, dexPressY, sx, sy);
  // ---------------------------------------------------------------------
  // ESTE evento se construye en una variable LOCAL y se inyecta. Antes se
  // escribia directamente sobre la T global antes de llamar a dexHostRun, y
  // dexHostRun guardaba "la T de entrada" para restaurarla... que ya era la
  // modificada. Resultado: T.startX/startY se quedaban en coordenadas de APP
  // despues de cada toque. En la vuelta siguiente flexPollTouch compara la
  // posicion FISICA del dedo contra ese startX de otro espacio de coordenadas:
  //     else if(abs(gx - T.startX) > 12 ...) T.moved = true;
  // la diferencia siempre pasa de 12 px, asi que T.moved se quedaba en true, y
  // con moved=true pTap NUNCA se activa. De ahi que ninguna app respondiera a
  // toques y que solo Paint diera senales de vida: Paint usa T.down y posicion,
  // no T.tap. Manteniendo el evento fuera de la T global el ciclo se rompe.
  // ---------------------------------------------------------------------
  Touch e;
  e.x = tx; e.y = ty;
  e.startX = sx; e.startY = sy;
  e.dx = tx - sx; e.dy = ty - sy;
  e.down = pDown; e.pressed = pPressed; e.released = pReleased;
  e.tap = pTap; e.moved = T.moved;
  e.downMs = T.downMs; e.lastMs = T.lastMs;
  e.swipeUp = e.swipeDown = e.swipeLeft = e.swipeRight = false;
  dexHostTouchX = tx; dexHostTouchY = ty; dexHostTouchWin = i;   // depuracion
  dexHostRun(i, false, true, &e);
}
// Una app APP_FLEX maqueta contra el tamano de SU ventana, asi que al
// redimensionar hay que volver a ejecutar su enter() con el lienzo nuevo -- si
// no, se veria el layout viejo estirado. Se limita la cadencia para que
// arrastrar el borde no dispare un render completo por frame, y siempre se hace
// uno final cuando el tamano se estabiliza.
// Ademas mantiene vivos los fundidos de las secciones opcionales: mientras
// uiFading este activo se sigue re-renderizando, que es lo que hace que la
// aparicion/desaparicion se vea como un fundido y no como un salto.
#define DEX_RELAYOUT_MS 45
static void dexHostRelayout(int i){
  if(i < 0 || i > 3 || !pwins[i].open || pwins[i].mini || !dexHost[i].surf) return;
  int cx, cy, cw, ch; dexClientRect(i, cx, cy, cw, ch);
  DexFit f; dexHostFit(i, cx, cy, cw, ch, f);
  bool sizeChanged = (f.aw != dexHost[i].rw || f.ah != dexHost[i].rh);
  if(!sizeChanged && !uiFading) return;
  uint32_t now = millis();
  if(sizeChanged && dexHost[i].reMs && now - dexHost[i].reMs < DEX_RELAYOUT_MS) return;
  dexHost[i].reMs = now;
  gRelayout = true;                               // enter() debe re-dibujar, no re-inicializar
  dexHostRun(i, true, false, NULL);
  gRelayout = false;
}
// Atiende lo que la app pidio (cerrarse, abrir otra, Recientes). Se hace FUERA
// de dexHostRun para no reentrar en la app desde su propia pila.
static void dexHostServe(int i){
  int req = gHostReq, reqApp = gHostReqApp;
  gHostReq = 0; gHostReqApp = -1;
  if(req == 1) dexCloseWin(i);
  else if(req == 2 && reqApp >= 0 && reqApp < APP_N){
    int ix, iy, is; dexTbAppRect(reqApp, ix, iy, is);
    dexOpenFrom(reqApp, ix, iy, is);
  }
  else if(req == 3) dexOvOpen(DXO_RECENTS);
}

static void dexWinContent(int i, int app, int x, int y, int w, int h, bool active){
  // Con la app hospedada viva, el contenido ES la app real escalada al marco.
  if(dexHost[i].surf){ dexHostBlit(i, x, y, w, h); return; }
  if(w < 30 || h < 20) return;
  fillRect(x, y, w, h, DEX_WIN_BODY);
  uint16_t lo = active ? DEX_TXT_LO : mix565(DEX_TXT_LO, DEX_WIN_BODY, 80);
  int S = h * 3 / 5; if(S > 96) S = 96;
  if(S >= 30 && w > S + 150 && h > S + 20) drawAppIcon(app, x + w - S - 22, y + h - S - 16, S);
  int tw = w - 40;
  if(h > 50)  dexTextFit(x + 20, y + 16, appName(app), 4, active ? DEX_TXT_HI : DEX_TXT_LO, tw);
  if(h > 84)  dexTextFit(x + 20, y + 56, "Ventana de Samsung DeX", 2, lo, tw);
  if(h > 112) dexTextFit(x + 20, y + 86, "Arrastra la barra de titulo a un borde", 1, lo, tw);
  if(h > 128) dexTextFit(x + 20, y + 100, "para anclarla a media pantalla.", 1, lo, tw);
}
static void dexDrawWindow(int i, int x, int y, int w, int h, bool active){
  if(w < 20 || h < 20 || dexCull(x, w)) return;
  int rad = 14; if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;
  fillRoundRectA(x + 3, y + 6, w, h, rad, TH_SHADOW, effShadow(active ? 100 : 62));         // sombra
  dexSurface(x, y, w, h, rad, DEX_WIN_BG);                                       // cuerpo
  uint16_t tc = active ? DEX_TTL_ACT : DEX_TTL_INA;                              // barra de titulo
  int th = DEX_TTL_H; if(th > h) th = h;
  fillRoundRect(x, y, w, th, rad, tc);
  if(th > rad) fillRect(x, y + th - rad, w, rad, tc);
  hLine(x, y + th - 1, w, DEX_BORDER);
  if(w > 60){
    drawAppIcon(pwins[i].app, x + 10, y + (th - 20) / 2, 20);
    dexTextFit(x + 38, y + (th - 15) / 2, appName(pwins[i].app), 2,
               active ? DEX_TXT_HI : DEX_TXT_LO, w - 52 - 3 * DEX_BTN_W);
    dexWinButtons(i, x, y, w, active);
  }
  dexWinContent(i, pwins[i].app, x + 1, y + th, w - 2, h - th - 1, active);
  // Ventana inactiva: velo sutil (la "transicion de opacidad" activa/inactiva).
  if(!active) fillRoundRectA(x, y, w, h, rad, TH_SCRIM, 34);
}
// Version simplificada para abrir/cerrar/minimizar: crece o encoge con fundido.
// A proposito NO pinta el contenido -- seria tirar trabajo a la basura en cada
// uno de los ~6 frames de la animacion.
static void dexDrawWinAnim(int x, int y, int w, int h, int app, uint8_t a){
  if(w < 6 || h < 6 || dexCull(x, w)) return;
  int rad = 14; if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;
  fillRoundRectA(x + 2, y + 4, w, h, rad, TH_SHADOW, effShadow(a * 70 / 255));
  fillRoundRectA(x, y, w, h, rad, DEX_WIN_BG, a);
  int th = DEX_TTL_H * h / 260; if(th < 6) th = 6;
  if(th > DEX_TTL_H) th = DEX_TTL_H;
  if(th > h) th = h;
  fillRoundRectA(x, y, w, th, rad, DEX_TTL_ACT, a);
  if(th > rad) fillRectA(x, y + th - rad, w, rad, DEX_TTL_ACT, a);
  int S = h / 3; if(S > 56) S = 56;
  if(a >= 210 && S >= 18 && w > S + 20 && h > th + S + 8)
    drawAppIcon(app, x + (w - S) / 2, y + th + (h - th - S) / 2, S);
}
static void dexDrawGhost(){                          // contorno fantasma del snap
  if(dexSnapGhost == SNAP_FREE) return;
  int x, y, w, h; dexSnapRect(dexSnapGhost, x, y, w, h);
  if(dexCull(x, w)) return;
  fillRoundRectA(x + 6, y + 6, w - 12, h - 12, 16, mix565(TH_PRIM, TH_SURF, 150), 70);
  drawRoundRect(x + 6, y + 6, w - 12, h - 12, 16, mix565(TH_PRIM, TH_SURF, 110));
  drawRoundRect(x + 7, y + 7, w - 14, h - 14, 15, TH_PRIM);
}
// Se conserva la firma original (PWin*): es la razon por la que el tipo PWin vive
// arriba del todo del archivo (ver el comentario de los tipos).
static void pcDrawWindow(PWin* wn){
  int i = (int)(wn - pwins);
  if(i < 0 || i > 3) return;
  dexDrawWindow(i, wn->x, wn->y, wn->w, wn->h, i == dexFocus);
}

// ---- Barra de tareas ----
static void dexTaskbar(){
  int ty = dexTbY(), h = DEX_TB_H;
  if(ty >= LH) return;                                          // oculta del todo
  // La barra se dibuja SIEMPRE con su geometria completa (0..LW) y es el recorte
  // de banda quien decide que columnas se tocan. Antes se le pasaba el rect ya
  // recortado a la banda, y pcGlassPanel -> drawRoundRect remata el rect con un
  // vLine en CADA extremo: es decir, pintaba dos lineas verticales claras justo
  // en los bordes de la banda. Como la banda se mueve con el puntero, cada
  // repintado dejaba dos rayas nuevas -> las columnas claras de la captura.
  // Con la geometria completa esos vLine caen en lx=0 y lx=LW-1, donde deben.
  if(uiGlass) pcGlassPanel(0, ty, LW, h, 0, DEX_TB_GLASS);
  else fillRect(0, ty, LW, h, DEX_TB_BG);
  hLine(0, ty, LW, DEX_BORDER);
  if(!dexCull(dexRDrw[0], dexRDrw[2]))
    dexIcoGrid(dexRDrw[0], dexRDrw[1], dexRDrw[2], dexOv == DXO_DRAWER && !dexOvClosing);
  if(!dexCull(dexRFnd[0], dexRFnd[2]))
    dexIcoSearch(dexRFnd[0], dexRFnd[1], dexRFnd[2], dexOv == DXO_FINDER && !dexOvClosing);
  // Grupo CENTRADO: fijadas + abiertas, con indicador de "abierta".
  int app[10]; bool op[10];
  int n = dexTbItems(app, op);
  for(int i = 0; i < n; i++){
    int x, y, s; dexTbItemRect(i, n, x, y, s);
    if(dexCull(x - 6, s + 12)) continue;
    bool foc = false;
    for(int k = 0; k < 4; k++)
      if(pwins[k].open && pwins[k].app == app[i] && k == dexFocus && !pwins[k].mini) foc = true;
    if(op[i]) fillRoundRectA(x - 6, y - 4, s + 12, s + 8, 8, DEX_ACCENT, foc ? 76 : 34);
    drawAppIcon(app[i], x, y, s);
    if(op[i]){
      int iw = foc ? 16 : 8;
      fillRoundRect(x + s / 2 - iw / 2, y + s + 6, iw, 3, 1, DEX_ACCENT);
    }
  }
  // Extremo derecho: touchpad · campana · ajustes · wifi · bateria · reloj/fecha
  if(!dexCull(dexRPad[0], dexRPad[2]))   dexIcoPad(dexRPad[0], dexRPad[1], dexRPad[2], dexPadOn);
  if(!dexCull(dexRBell[0], dexRBell[2])) dexIcoBell(dexRBell[0], dexRBell[1], dexRBell[2], gNotifCount > 0);
  if(!dexCull(dexRGear[0], dexRGear[2])) dexIcoGear(dexRGear[0], dexRGear[1], dexRGear[2],
                                                    dexOv == DXO_NOTIF && !dexOvClosing);
  if(!dexCull(dexRWifiX - 11, 22)) drawWifi(dexRWifiX, ty + h / 2 + 6, 11, DEX_TXT_HI);
  if(!dexCull(dexRBat[0], dexRBat[2] + 2))
    drawBattery(dexRBat[0], dexRBat[1], dexRBat[2], dexRBat[3], 82, DEX_TXT_HI);
  if(!dexCull(dexRClkX - 110, 112)){
    char cs[12]; clkStrBar(cs, sizeof(cs));
    char sd[40]; buildShortDate(sd, sizeof(sd));
    drawTextR(dexRClkX, ty + 12, cs, 2, DEX_TXT_HI);
    drawTextR(dexRClkX, ty + 34, sd, 1, DEX_TXT_LO);
  }
}

// ---- Teclado compacto del buscador ----
#define DEX_KB_RH  32
#define DEX_KB_GAP 5
static void dexKbRect(int kx, int ky, int kw, int row, int col, int &x, int &y, int &w, int &h){
  int n = (row == 0) ? 10 : 9;
  int kwid = (kw - (n - 1) * DEX_KB_GAP) / n;
  int rowW = n * kwid + (n - 1) * DEX_KB_GAP;
  x = kx + (kw - rowW) / 2 + col * (kwid + DEX_KB_GAP);
  y = ky + row * (DEX_KB_RH + DEX_KB_GAP);
  w = kwid; h = DEX_KB_RH;
}
static void dexKbDraw(int kx, int ky, int kw){
  for(int r = 0; r < 3; r++){
    int n = (r == 0) ? 10 : 9;
    for(int c = 0; c < n; c++){
      int x, y, w, h; dexKbRect(kx, ky, kw, r, c, x, y, w, h);
      fillRoundRect(x, y, w, h, 6, DEX_PANEL2);
      const char* row = DEX_KB[r];
      int rl = (int)strlen(row);
      if(c < rl){ char b[2] = { row[c], 0 }; drawTextC(x + w / 2, y + h / 2 - 8, b, 2, DEX_TXT_HI); }
      else if(r == 2 && c == 7) fillRect(x + w / 2 - 6, y + h / 2 + 5, 13, 2, DEX_TXT_LO);   // espacio
      else if(r == 2 && c == 8){                                                            // borrar
        int mx = x + w / 2, my = y + h / 2;
        strokeSegAA((float)(mx - 6), (float)my, (float)(mx + 6), (float)my, 1.6f, DEX_TXT_HI);
        strokeSegAA((float)(mx - 6), (float)my, (float)(mx - 1), (float)(my - 5), 1.6f, DEX_TXT_HI);
        strokeSegAA((float)(mx - 6), (float)my, (float)(mx - 1), (float)(my + 5), 1.6f, DEX_TXT_HI);
      }
    }
  }
}
static bool dexKbTap(int kx, int ky, int kw, int px, int py){
  for(int r = 0; r < 3; r++){
    int n = (r == 0) ? 10 : 9;
    for(int c = 0; c < n; c++){
      int x, y, w, h; dexKbRect(kx, ky, kw, r, c, x, y, w, h);
      if(!dexInBox(px, py, x, y, w, h)) continue;
      const char* row = DEX_KB[r];
      int rl = (int)strlen(row);
      if(c < rl){ if(dexQLen < (int)sizeof(dexQuery) - 1){ dexQuery[dexQLen++] = row[c]; dexQuery[dexQLen] = 0; } }
      else if(r == 2 && c == 7){ if(dexQLen < (int)sizeof(dexQuery) - 1){ dexQuery[dexQLen++] = ' '; dexQuery[dexQLen] = 0; } }
      else if(r == 2 && c == 8){ if(dexQLen > 0) dexQuery[--dexQLen] = 0; }
      return true;
    }
  }
  return false;
}
static void dexSearchField(int x, int y, int w, int h, const char* ph){
  fillRoundRect(x, y, w, h, h / 2, DEX_PANEL2);
  drawRoundRect(x, y, w, h, h / 2, DEX_BORDER);
  int cx = x + 20, cy = y + h / 2, r = 6;
  drawCircle(cx, cy - 1, r, DEX_TXT_LO);
  strokeSegAA((float)(cx + 4), (float)(cy + 3), (float)(cx + 9), (float)(cy + 8), 1.6f, DEX_TXT_LO);
  if(dexQLen > 0){
    dexTextFit(x + 38, y + h / 2 - 8, dexQuery, 2, DEX_TXT_HI, w - 76);
    int bx = x + w - 26, by = y + h / 2;                       // limpiar
    fillCircle(bx, by, 9, DEX_BORDER);
    strokeSegAA((float)(bx - 4), (float)(by - 4), (float)(bx + 4), (float)(by + 4), 1.5f, DEX_PANEL);
    strokeSegAA((float)(bx - 4), (float)(by + 4), (float)(bx + 4), (float)(by - 4), 1.5f, DEX_PANEL);
  } else dexTextFit(x + 38, y + h / 2 - 8, ph, 2, DEX_TXT_LO, w - 56);
}
// Marco animado de un pop-up: mientras crece/encoge solo se pinta la caja con
// fundido; el contenido aparece cuando ya esta abierto del todo. Devuelve true
// cuando toca dibujar el contenido.
static bool dexPopupFrame(int &x, int &y, int w, int h, int rad){
  float p = dexOvProg();
  if(p >= 0.999f){
    x = (LW - w) / 2; y = (dexWorkBottom() - h) / 2;
    if(dexCull(x, w)) return false;
    fillRoundRectA(x + 4, y + 8, w, h, rad, TH_SHADOW, effShadow(110));
    dexSurface(x, y, w, h, rad, DEX_PANEL);
    return true;
  }
  float sc = 0.86f + 0.14f * p;
  int aw = (int)(w * sc), ah = (int)(h * sc);
  int ax = (LW - aw) / 2, ay = (dexWorkBottom() - ah) / 2;
  x = ax; y = ay;
  if(dexCull(ax, aw)) return false;
  uint8_t a = (uint8_t)(235 * p);
  fillRoundRectA(ax + 4, ay + 8, aw, ah, rad, TH_SHADOW, effShadow((int)(90 * p)));
  fillRoundRectA(ax, ay, aw, ah, rad, DEX_PANEL, a);
  return false;
}

// ---- Cajon de apps: pop-up centrado (NO pantalla completa) ----
#define DEX_DRW_KBY 292                      // offset del teclado dentro del pop-up
static void dexDrawerGrid(int fx, int fy, int idx, int &x, int &y, int &s){
  int cols = 6, cw = (DEX_DRW_W - 28) / cols, rh = 76;
  s = 52;
  x = fx + 14 + (idx % cols) * cw + (cw - s) / 2;
  y = fy + 62 + (idx / cols) * rh;
}
static void dexDrawerDraw(){
  int fx, fy;
  if(!dexPopupFrame(fx, fy, DEX_DRW_W, DEX_DRW_H, 20)) return;
  dexSearchField(fx + 18, fy + 14, DEX_DRW_W - 36, 36, "Buscar aplicaciones");
  int list[18]; int n = dexFilterApps(list, 18);
  int cw = (DEX_DRW_W - 28) / 6;
  for(int i = 0; i < n; i++){
    int x, y, s; dexDrawerGrid(fx, fy, i, x, y, s);
    drawAppIcon(list[i], x, y, s);
    dexTextFitC(x + s / 2, y + s + 6, appName(list[i]), 1, DEX_TXT_HI, cw - 6);
  }
  if(n == 0) drawTextC(fx + DEX_DRW_W / 2, fy + 130, "Sin resultados", 2, DEX_TXT_LO);
  dexKbDraw(fx + 14, fy + DEX_DRW_KBY, DEX_DRW_W - 28);
}

// ---- Buscador tipo Finder: apps + ajustes a la vez ----
#define DEX_FND_ROW  27
#define DEX_FND_KBY  292
#define DEX_FND_APPS 4
#define DEX_FND_SETS 3
static void dexFinderRows(int &nApps, int &nSet, int* apps, int* sets){
  nApps = dexFilterApps(apps, DEX_FND_APPS);
  nSet = 0;
  for(int i = 0; i < 8 && nSet < DEX_FND_SETS; i++)
    if(dexMatch(DEX_SET[i], dexQuery, dexQLen)) sets[nSet++] = i;
}
static void dexFinderDraw(){
  int fx, fy;
  if(!dexPopupFrame(fx, fy, DEX_FND_W, DEX_FND_H, 20)) return;
  dexSearchField(fx + 18, fy + 14, DEX_FND_W - 36, 36, "Buscar apps y ajustes");
  int apps[DEX_FND_APPS], sets[DEX_FND_SETS], na, ns;
  dexFinderRows(na, ns, apps, sets);
  int y = fy + 62;
  if(na > 0){
    drawText(fx + 22, y, "Aplicaciones", 1, DEX_TXT_LO); y += 15;
    for(int i = 0; i < na; i++){
      drawAppIcon(apps[i], fx + 20, y + 2, 20);
      dexTextFit(fx + 48, y + 5, appName(apps[i]), 2, DEX_TXT_HI, DEX_FND_W - 78);
      y += DEX_FND_ROW;
    }
    y += 6;
  }
  if(ns > 0){
    drawText(fx + 22, y, "Ajustes", 1, DEX_TXT_LO); y += 15;
    for(int i = 0; i < ns; i++){
      fillRoundRect(fx + 20, y + 2, 20, 20, 6, DEX_PANEL2);
      fillCircle(fx + 30, y + 12, 5, DEX_ACCENT);
      dexTextFit(fx + 48, y + 5, DEX_SET[sets[i]], 2, DEX_TXT_HI, DEX_FND_W - 78);
      y += DEX_FND_ROW;
    }
  }
  if(na == 0 && ns == 0) drawTextC(fx + DEX_FND_W / 2, fy + 120, "Sin resultados", 2, DEX_TXT_LO);
  dexKbDraw(fx + 14, fy + DEX_FND_KBY, DEX_FND_W - 28);
}

// ---- Panel de notificaciones / ajustes rapidos (cortina) ----
#define DEX_NP_TH   58                       // alto de cada tile
#define DEX_NP_STEP 68                       // paso vertical entre filas de tiles
static void dexNpRect(int &x, int &y, int &w, int &h){
  w = DEX_NP_W; x = LW - w - 12; y = 10; h = dexWorkBottom() - 22;
}
static void dexNpTile(int idx, int nx, int ny, int &x, int &y, int &w, int &h){
  w = (DEX_NP_W - 36) / 2; h = DEX_NP_TH;
  x = nx + 12 + (idx % 2) * (w + 12);
  y = ny + 42 + (idx / 2) * DEX_NP_STEP;
}
static inline int dexNpBrightY(int ny){ return ny + 42 + 2 * DEX_NP_STEP + 6; }
// Boton de salida SIEMPRE visible en el panel. La barra de tareas de DeX ya no
// lleva el boton "Salir" que tenia el Modo PC viejo (ahora ese hueco es del
// grupo centrado de apps), asi que la salida vive aqui y ademas en el menu
// contextual de la barra: sin una de las dos, se podria quedar uno encerrado.
#define DEX_NP_EXIT_H 34
static inline int dexNpExitY(int ny, int nh){ return ny + nh - DEX_NP_EXIT_H - 12; }
static bool dexNpState(int idx){
  switch(idx){
    case 0:  return uiGlass;
    case 1:  return gDark;
    case 2:  return dexTbAuto;
    default: return dexPadOn;
  }
}
static const char* dexNpLabel(int idx){
  switch(idx){
    case 0:  return "Liquid Glass";
    case 1:  return "Modo oscuro";
    case 2:  return "Barra auto";
    default: return "Touchpad";
  }
}
static void dexNotifDraw(){
  float p = dexOvProg();
  if(p <= 0.01f) return;
  int nx, ny, nw, nh; dexNpRect(nx, ny, nw, nh);
  if(dexCull(nx, nw)) return;
  int visH = (int)(nh * p);                       // cortina: se despliega hacia abajo
  if(visH < 8) return;
  fillRoundRectA(nx + 4, ny + 6, nw, visH, 18, TH_SHADOW, effShadow((int)(110 * p)));
  if(p < 0.999f){                                 // aun desplegandose: solo la caja
    fillRoundRectA(nx, ny, nw, visH, 18, DEX_PANEL, (uint8_t)(255 * p));
    return;
  }
  dexSurface(nx, ny, nw, visH, 18, DEX_PANEL);
  drawText(nx + 16, ny + 14, "Ajustes rapidos", 2, DEX_TXT_HI);
  for(int i = 0; i < 4; i++){
    int x, y, w, h; dexNpTile(i, nx, ny, x, y, w, h);
    bool on = dexNpState(i);
    fillRoundRect(x, y, w, h, 12, on ? DEX_ACCENT : DEX_PANEL2);
    uint16_t tc = on ? TH_ONACC : DEX_TXT_HI;
    fillCircle(x + 20, y + 20, 8, tc);
    fillCircle(x + 20, y + 20, on ? 3 : 5, on ? DEX_ACCENT : DEX_PANEL2);
    dexTextFit(x + 10, y + 36, dexNpLabel(i), 1, tc, w - 20);
  }
  // Brillo: PWM REAL del backlight (setBacklight), igual que el panel rapido.
  int by = dexNpBrightY(ny), bx = nx + 12, bw = nw - 24, bh = 26;
  drawText(nx + 16, by, "Brillo", 1, DEX_TXT_LO);
  fillRoundRect(bx, by + 14, bw, bh, bh / 2, DEX_PANEL2);
  int fw = bw * gBright / 100; if(fw < bh) fw = bh;
  fillRoundRect(bx, by + 14, fw, bh, bh / 2, DEX_ACCENT);
  char pb[8]; snprintf(pb, sizeof(pb), "%d%%", gBright);
  drawTextR(bx + bw - 10, by + 14 + bh / 2 - 8, pb, 2, DEX_TXT_HI);
  // Notificaciones REALES (gNotifs[]); no se inventan tarjetas de relleno.
  int ly = by + 14 + bh + 16, listBot = dexNpExitY(ny, nh) - 10;
  drawText(nx + 16, ly, "Notificaciones", 1, DEX_TXT_LO); ly += 16;
  if(gNotifCount == 0) drawText(nx + 16, ly + 6, "Sin notificaciones", 2, DEX_TXT_LO);
  else for(int i = 0; i < gNotifCount && i < NOTIF_MAX; i++){
    if(ly + 46 > listBot) break;
    fillRoundRect(nx + 12, ly, nw - 24, 42, 12, DEX_PANEL2);
    fillCircle(nx + 34, ly + 21, 10, DEX_ACCENT);
    dexTextFit(nx + 54, ly + 8, gNotifs[i].mod.name, 2, DEX_TXT_HI, nw - 74);
    dexTextFit(nx + 54, ly + 26, "Modulo detectado", 1, DEX_TXT_LO, nw - 74);
    ly += 48;
  }
  int ey = dexNpExitY(ny, nh);
  fillRoundRect(nx + 12, ey, nw - 24, DEX_NP_EXIT_H, 12, TH_DANGER);        // salir de Modo PC: accion destructiva
  drawTextC(nx + nw / 2, ey + DEX_NP_EXIT_H / 2 - 8, "Salir de Modo PC", 2, TH_ONACC);
}

// ---- Recientes (selector de tareas propio de este modo) ----
// Mismo PATRON de tarjetas que el App Switcher del resto del OS (swRenderCards:
// miniatura + nombre, arrastrar hacia arriba para cerrar), pero con geometria
// landscape y sobre las VENTANAS de DeX. No se reutiliza swTasks[] a proposito:
// ese array se declara mas abajo en el archivo (no seria visible aqui) y guarda
// apps a pantalla completa, que no es lo mismo que una ventana de DeX.
#define DEX_REC_W 190
#define DEX_REC_H 148
static int dexRecList(int* out){
  int n = 0;
  for(int k = 3; k >= 0; k--){ int i = dexOrder[k]; if(pwins[i].open) out[n++] = i; }
  return n;
}
static void dexRecCard(int idx, int n, int &x, int &y){
  int gap = 18, tot = n * DEX_REC_W + (n - 1) * gap;
  x = (LW - tot) / 2 + idx * (DEX_REC_W + gap);
  y = (dexWorkBottom() - DEX_REC_H) / 2;
}
static void dexRecentsDraw(){
  float p = dexOvProg();
  if(p <= 0.01f) return;
  int dx = 0, dw = LW;
  if(dexBand(dx, dw)) fillRectA(dx, 0, dw, dexWorkBottom(), TH_SCRIM, (uint8_t)(170 * p));
  int list[4]; int n = dexRecList(list);
  if(n == 0){
    drawTextC(LW / 2, dexWorkBottom() / 2 - 10, "Sin ventanas abiertas", 3, TH_TXT);
    return;
  }
  for(int i = 0; i < n; i++){
    int x, y; dexRecCard(i, n, x, y);
    if(i == dexRecDrag) y += dexRecDY;
    int w = DEX_REC_W, h = DEX_REC_H;
    if(dexCull(x, w)) continue;
    fillRoundRectA(x + 3, y + 6, w, h, 14, TH_SHADOW, effShadow((int)(120 * p)));
    fillRoundRect(x, y, w, h, 14, DEX_WIN_BG);
    drawRoundRect(x, y, w, h, 14, DEX_BORDER);
    fillRoundRect(x, y, w, 24, 14, DEX_TTL_ACT);
    fillRect(x, y + 10, w, 14, DEX_TTL_ACT);
    drawAppIcon(pwins[list[i]].app, x + 6, y + 4, 16);
    dexTextFit(x + 26, y + 6, appName(pwins[list[i]].app), 1, DEX_TXT_HI, w - 56);
    int S = 54;
    drawAppIcon(pwins[list[i]].app, x + (w - S) / 2, y + 24 + (h - 24 - S - 22) / 2, S);
    dexTextFitC(x + w / 2, y + h - 20, pwins[list[i]].mini ? "Minimizada" : "En ejecucion",
                1, DEX_TXT_LO, w - 16);
    int cx = x + w - 14, cy = y + 12;                          // cerrar
    strokeSegAA((float)(cx - 4), (float)(cy - 4), (float)(cx + 4), (float)(cy + 4), 1.5f, DEX_TXT_HI);
    strokeSegAA((float)(cx - 4), (float)(cy + 4), (float)(cx + 4), (float)(cy - 4), 1.5f, DEX_TXT_HI);
  }
  drawTextC(LW / 2, dexWorkBottom() - 40,
            "Arrastra una tarjeta hacia arriba para cerrarla", 1, TH_TXT2);
}

// ---- Menu contextual ----
#define DEX_MENU_W  216
#define DEX_MENU_IH 30
static int dexMenuCount(uint8_t k){ return k == 2 ? 5 : 4; }
static const char* dexMenuLabel(uint8_t k, int i){
  if(k == 0){
    switch(i){ case 0:  return "Cambiar fondo";
               case 1:  return "Organizar en cascada";
               case 2:  return "Recientes";
               default: return "Cajon de apps"; }
  }
  if(k == 1){
    switch(i){ case 0:  return dexTbAuto ? "Barra: auto-ocultar ON" : "Barra: auto-ocultar OFF";
               case 1:  return dexBigIcons ? "Iconos grandes" : "Iconos normales";
               case 2:  return "Recientes";
               default: return "Salir de Modo PC"; }
  }
  switch(i){ case 0:  return "Minimizar";
             case 1:  return (dexMenuWin >= 0 && pwins[dexMenuWin].snap != SNAP_FREE) ? "Restaurar" : "Maximizar";
             case 2:  return "Anclar a la izquierda";
             case 3:  return "Anclar a la derecha";
             default: return "Cerrar"; }
}
static void dexMenuGeom(int &x, int &y, int &w, int &h){
  int n = dexMenuCount(dexMenuKind);
  w = DEX_MENU_W; h = 10 + n * DEX_MENU_IH;
  x = dexMenuX; y = dexMenuY;
  if(x + w > LW - 6) x = LW - 6 - w;
  if(x < 6) x = 6;
  if(y + h > dexWorkBottom() - 6) y = dexWorkBottom() - 6 - h;
  if(y < 6) y = 6;
}
static void dexMenuDraw(){
  int x, y, w, h; dexMenuGeom(x, y, w, h);
  if(dexCull(x, w)) return;
  fillRoundRectA(x + 3, y + 5, w, h, 12, TH_SHADOW, effShadow(120));
  dexSurface(x, y, w, h, 12, DEX_PANEL);
  int n = dexMenuCount(dexMenuKind);
  for(int i = 0; i < n; i++){
    int iy = y + 5 + i * DEX_MENU_IH;
    bool danger = (dexMenuKind == 2 && i == 4) || (dexMenuKind == 1 && i == 3);
    dexTextFit(x + 14, iy + 8, dexMenuLabel(dexMenuKind, i), 2,
               danger ? TH_DANGER : DEX_TXT_HI, w - 28);
  }
}

// ---- Touchpad virtual + cursor ----
static void dexPadGeom(int &x, int &y, int &w, int &h){
  w = DEX_PAD_W; h = DEX_PAD_H;
  x = LW - w - 14; y = dexWorkBottom() - h - 14;
}
static void dexPadDraw(){
  int x, y, w, h; dexPadGeom(x, y, w, h);
  if(dexCull(x, w)) return;
  fillRoundRectA(x, y, w, h, 14, TH_SURF, 190);
  drawRoundRect(x, y, w, h, 14, DEX_BORDER);
  hLine(x + 10, y + h - 30, w - 20, TH_DIV);          // divisor DENTRO de la superficie
  drawTextC(x + w / 2, y + 12, "Touchpad", 1, DEX_TXT_LO);
  drawTextC(x + w / 2, y + h - 22, "Toca = clic · Manten = menu", 1, DEX_TXT_LO);
}
// Forma del cursor segun el contexto: flecha / manita / cursor de texto.
static uint8_t dexCursorShape(){
  int x = dexCurX, y = dexCurY;
  if(dexOv == DXO_DRAWER || dexOv == DXO_FINDER){
    int w = (dexOv == DXO_DRAWER) ? DEX_DRW_W : DEX_FND_W;
    int h = (dexOv == DXO_DRAWER) ? DEX_DRW_H : DEX_FND_H;
    int fx = (LW - w) / 2, fy = (dexWorkBottom() - h) / 2;
    if(dexInBox(x, y, fx + 16, fy + 12, w - 32, 34)) return 2;      // campo de busqueda
    if(dexInBox(x, y, fx, fy, w, h)) return 1;
  }
  if(y >= dexTbY()) return 1;
  if(dexMenuOn) return 1;
  for(int k = 3; k >= 0; k--){
    int i = dexOrder[k];
    if(!pwins[i].open || pwins[i].mini) continue;
    if(dexInBox(x, y, pwins[i].x, pwins[i].y, pwins[i].w, DEX_TTL_H)) return 1;
  }
  return 0;
}
static void dexCursorDraw(){
  int x = dexCurX, y = dexCurY;
  if(dexCull(x - 8, 26)) return;
  // CURSOR del touchpad: blanco con contorno negro, en las dos apariencias. Es la
  // unica combinacion que se ve sobre cualquier cosa que haya debajo (wallpaper,
  // ventana clara, ventana oscura, una app hospedada). Requisito grafico.
  uint16_t W = rgb565(255,255,255), K = rgb565(24,28,38);
  uint8_t sh = dexCursorShape();
  if(sh == 2){                                    // cursor de texto
    fillRect(x - 1, y - 9, 3, 19, W);
    fillRect(x - 4, y - 10, 9, 2, W);
    fillRect(x - 4, y + 9, 9, 2, W);
    return;
  }
  if(sh == 1){                                    // manita
    fillRoundRect(x - 6, y - 1, 13, 15, 5, W);
    drawRoundRect(x - 6, y - 1, 13, 15, 5, K);
    fillRoundRect(x - 2, y - 11, 5, 12, 2, W);
    drawRoundRect(x - 2, y - 11, 5, 12, 2, K);
    return;
  }
  fillTriangle(x, y, x, y + 16, x + 11, y + 11, W);              // flecha
  fillTriangle(x, y, x + 11, y + 11, x + 12, y + 12, W);
  strokeSegAA((float)x, (float)y, (float)x, (float)(y + 16), 1.1f, K);
  strokeSegAA((float)x, (float)y, (float)(x + 12), (float)(y + 12), 1.1f, K);
  strokeSegAA((float)x, (float)(y + 16), (float)(x + 7), (float)(y + 11), 1.1f, K);
  strokeSegAA((float)(x + 7), (float)(y + 11), (float)(x + 12), (float)(y + 12), 1.1f, K);
}

// -------------------------------------------------------------
//  Composicion + presentacion con banda sucia
// -------------------------------------------------------------
static void dexCompose(){
  dexWallpaper();                                  // escritorio limpio: SIN iconos sueltos
  for(int k = 0; k < 4; k++){                      // ventanas, de atras hacia delante
    int i = dexOrder[k];
    if(!pwins[i].open) continue;
    if(dexAnimIs(i)){
      int ax, ay, aw, ah; uint8_t al; dexAnimCur(ax, ay, aw, ah, al);
      if(dexAK == DXA_GEOM) dexDrawWindow(i, ax, ay, aw, ah, i == dexFocus);
      else dexDrawWinAnim(ax, ay, aw, ah, pwins[i].app, al);
    } else if(!pwins[i].mini){
      dexDrawWindow(i, pwins[i].x, pwins[i].y, pwins[i].w, pwins[i].h, i == dexFocus);
    }
  }
  if(dexGrab == DXG_MOVE) dexDrawGhost();
  if(dexOv == DXO_RECENTS) dexRecentsDraw();       // Recientes va BAJO la barra
  dexTaskbar();
  if(dexOv == DXO_DRAWER)      dexDrawerDraw();
  else if(dexOv == DXO_FINDER) dexFinderDraw();
  else if(dexOv == DXO_NOTIF)  dexNotifDraw();
  if(dexMenuOn) dexMenuDraw();
  if(dexPadOn){ dexPadDraw(); dexCursorDraw(); }
}
// Compone en bbuf y sube SOLO la banda sucia. La banda es un rango de lx que,
// tras la rotacion, es un rango de filas fisicas: gClipY0/gClipY1 recortan justo
// ese eje, y dexCull/dexBand evitan ademas recorrer lo que cae fuera.
static void dexPaint(bool full){
  if(full) dexMarkAll();
  dexDirty = false;
  if(dexBX1 < dexBX0) return;
  int b0 = dexBX0, b1 = dexBX1;
  dexBX0 = 0x7FFF; dexBX1 = -1;
  if(b0 < 0) b0 = 0;
  if(b1 > LW - 1) b1 = LW - 1;
  gLand = true; setBuf(bbuf);
  gClipY0 = b0; gClipY1 = b1;
  dexCompose();
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  setBuf(fb);
  present(b0, b1);
}
static void pcRender(){ dexPaint(true); }
