// #############################################################
// ##  FLEX OS ULTRA  ·  FONDOS DE PANTALLA Y PALETA
// ##  ----------------------------------------------------------
// ##  Catalogo de fondos procedurales, fondo a partir de una imagen real
// ##  del almacenamiento (decodificada con FlexOS_JPEG) y extraccion de la
// ##  paleta que despues alimenta al tema.
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
#include "FlexOS_Ultra_Gfx.h"   // eslabon anterior de la cadena

// #############################################################
// ##  CATALOGO DE FONDOS
// ##  ------------------------------------------------------
// ##  Ocho fondos INTEGRADOS (procedurales: se dibujan con las
// ##  primitivas que ya existen, no son imagenes) mas una IMAGEN
// ##  REAL del almacenamiento, decodificada con el mismo
// ##  FlexOS_JPEG que usan Galeria y el visor de archivos.
// ##
// ##  El id 0 es el degradado de siempre y su salida es BIT A BIT
// ##  la de antes: una placa que actualice no ve cambiar el fondo.
// #############################################################
#define WALL_N   8
#define WALL_IMG 200                   // valor especial: imagen del almacenamiento
static const char* WALL_NAME[WALL_N] = {
  "Flex Original", "Aurora", "Nocturno", "Halo", "Onyx", "Oceano", "Violeta", "Naturaleza"
};
static uint8_t  gWallHome = 0;         // fondo del escritorio  (NVS "wallh")
static uint8_t  gWallLock = 0;         // fondo del bloqueo     (NVS "walll")
static char     gWallPath[80] = "";    // ruta de la imagen elegida (NVS "wallp")
static uint8_t  gWallFit  = 0;         // 0 rellenar · 1 ajustar · 2 centrar (NVS "wallfit")
static uint16_t* wallImg  = NULL;      // imagen ya decodificada a 480x800 (PSRAM, solo si se usa)
static bool      wallImgOk = false;    // wallImg contiene la imagen de gWallPath

// ---- Motor radial por LUT de distancia AL CUADRADO --------------------------
// Evita un sqrt() por pixel: la intensidad de un halo o un anillo solo depende
// de la distancia al centro, y d2 = dx^2+dy^2 es monotona con d. Se tabula la
// intensidad EN FUNCION DE d2 (>> WLUT_SHIFT) y el bucle por pixel queda en un
// incremento, un desplazamiento y una consulta.
// d2 maximo en 480x800 = 870400; 870400 >> 11 = 425 < WLUT_N.
#define WLUT_N     512
#define WLUT_SHIFT 11
static uint8_t wlut[WLUT_N];
static void wlutDisc(int rIn, int rOut, uint8_t aIn){
  if(rOut < 1) rOut = 1;
  if(rIn > rOut) rIn = rOut;
  for(int i = 0; i < WLUT_N; i++){
    int d = isqrt32(i << WLUT_SHIFT);
    if(d <= rIn)       wlut[i] = aIn;
    else if(d >= rOut) wlut[i] = 0;
    else               wlut[i] = (uint8_t)((int)aIn * (rOut - d) / (rOut - rIn));
  }
}
static void wlutRing(int r, int hw, uint8_t aPk){
  if(hw < 1) hw = 1;
  for(int i = 0; i < WLUT_N; i++){
    int d = isqrt32(i << WLUT_SHIFT), k = d > r ? d - r : r - d;
    wlut[i] = (k >= hw) ? 0 : (uint8_t)((int)aPk * (hw - k) / hw);
  }
}
// Aplica la LUT activa mezclando 'c' sobre lo que ya hay en gBuf, dentro del
// recorte vigente (que drawWallpaperRows deja fijado a la banda pedida).
static void wallRadial(int cx, int cy, uint16_t c){
  int y0 = gClipY0, y1 = gClipY1, x0 = gClipX0, x1 = gClipX1;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(x0 < 0) x0 = 0;
  if(x1 > SCR_W - 1) x1 = SCR_W - 1;
  for(int y = y0; y <= y1; y++){
    int32_t dy = y - cy, dy2 = dy * dy;
    uint16_t* row = gBuf + (size_t)y * SCR_W;
    int32_t dx = x0 - cx, dx2 = dx * dx;
    for(int x = x0; x <= x1; x++){
      uint32_t idx = (uint32_t)((dx2 + dy2) >> WLUT_SHIFT);
      if(idx >= WLUT_N) idx = WLUT_N - 1;
      uint8_t a = wlut[idx];
      if(a) row[x] = (a >= 255) ? c : mix565(row[x], c, a);
      dx2 += 2 * dx + 1; dx++;
    }
  }
}
// Disco OPACO con degradado lineal interno. El parametro es lineal en x, asi
// que se lleva en un acumulador de punto fijo (dos divisiones por FILA en vez
// de una por pixel). El acumulador es de 64 bits a proposito: ((+-500)*127)<<16
// pasa de 4.100 millones y en 32 bits desbordaria, lo que se ve como bandas
// horizontales dentro del disco.
static void wallDisc(int cx, int cy, int r, uint16_t c0, uint16_t c1, int dirx, int diry){
  if(r <= 0) return;
  int r2 = r * r;
  int y0 = cy - r, y1 = cy + r;
  if(y0 < gClipY0) y0 = gClipY0;
  if(y1 > gClipY1) y1 = gClipY1;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  int denom = r * (abs(dirx) + abs(diry));
  if(denom < 1) denom = 1;
  for(int y = y0; y <= y1; y++){
    int dy = y - cy, hw = isqrt32(r2 - dy * dy);
    if(hw <= 0) continue;
    int xa = cx - hw, xb = cx + hw;
    if(xa < gClipX0) xa = gClipX0;
    if(xb > gClipX1) xb = gClipX1;
    if(xa < 0) xa = 0;
    if(xb > SCR_W - 1) xb = SCR_W - 1;
    if(xa > xb) continue;
    int32_t base = (int32_t)dy * diry;
    int32_t acc  = (int32_t)(((int64_t)((int32_t)(xa - cx) * dirx + base) * 127 * 65536) / denom) + (128 << 16);
    int32_t step = (int32_t)(((int64_t)dirx * 127 * 65536) / denom);
    uint16_t* row = gBuf + (size_t)y * SCR_W;
    for(int x = xa; x <= xb; x++){
      int32_t tt = acc >> 16;
      if(tt < 0) tt = 0;
      if(tt > 255) tt = 255;
      row[x] = mix565(c0, c1, (uint8_t)tt);
      acc += step;
    }
  }
}
// Degradado diagonal de 3 paradas (el patron del fondo original), tabulado.
static void wallDiag3(uint16_t lo, uint16_t mid, uint16_t hi){
  uint16_t lut[256];
  for(int t = 0; t < 256; t++)
    lut[t] = (t < 128) ? mix565(lo, mid, (uint8_t)(t * 2)) : mix565(mid, hi, (uint8_t)((t - 128) * 2));
  int y0 = gClipY0, y1 = gClipY1;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  for(int y = y0; y <= y1; y++){
    int ty = ((SCR_H - 1 - y) * 255) / (SCR_H - 1);
    uint16_t* row = gBuf + (size_t)y * SCR_W;
    for(int x = 0; x < SCR_W; x++) row[x] = lut[(wallTxLut[x] + ty) >> 1];
  }
}
// Degradado bilineal de 4 esquinas (base del fondo "Aurora").
static void wallCorners4(uint16_t tl, uint16_t tr, uint16_t bl, uint16_t br){
  int y0 = gClipY0, y1 = gClipY1;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  for(int y = y0; y <= y1; y++){
    uint8_t ty = (uint8_t)((y * 255) / (SCR_H - 1));
    uint16_t L = mix565(tl, bl, ty), R = mix565(tr, br, ty);
    uint16_t* row = gBuf + (size_t)y * SCR_W;
    for(int x = 0; x < SCR_W; x++) row[x] = mix565(L, R, wallTxLut[x]);
  }
}

// ---- 0. FLEX ORIGINAL (el de siempre) --------------------------------------
static void wallFlexOriginal(bool blobs){
  int y0 = gClipY0, y1 = gClipY1;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  for(int y = y0; y <= y1; y++){
    // t=1 arriba-derecha, t=0 abajo-izquierda. 'ty' es invariante en x.
    int ty = ((SCR_H - 1 - y) * 255) / (SCR_H - 1);
    uint16_t* row = gBuf + (size_t)y * SCR_W;
    for(int x = 0; x < SCR_W; x++) row[x] = wallGradLut[(wallTxLut[x] + ty) >> 1];
  }
  if(blobs){
    // dos manchas suaves mas claras (como el escritorio de tus imagenes)
    fillCircleA(360, 150, 220, rgb565(150, 235, 180), 60);
    fillCircleA(90,  560, 260, rgb565(150, 160, 240), 55);
  }
}
// ---- 1. AURORA -- discos de cristal sobre fondo claro ----------------------
static void wallAurora(){
  wallCorners4(TC(205,238,236), TC(48,132,240), TC(72,208,206), TC(24,86,220));
  wlutDisc(236, 258, 90);  wallRadial(330, 205, TC(255,255,255));   // halo del disco azul
  wallDisc(330, 205, 246, TC(26,68,205), TC(104,176,252), -1, -1);
  wlutDisc(244, 264, 105); wallRadial(96, 486, TC(255,255,255));    // halo del disco menta
  wallDisc(96, 486, 250, TC(86,220,176), TC(186,248,220), 1, -1);
  wlutDisc(150, 330, 60);  wallRadial(430, 760, TC(140,205,255));   // brillo inferior derecho
}
// ---- 2. NOCTURNO -- esferas indigo sobre casi-negro ------------------------
static void wallNocturno(){
  wallDiag3(TC(4,5,14), TC(8,9,26), TC(5,6,18));
  wallDisc(340, 110, 300, TC(10,12,38), TC(62,70,152), -1, -1);
  wlutRing(300, 6, 225); wallRadial(340, 110, TC(182,192,255));
  wallDisc(92, 620, 330, TC(9,10,34), TC(70,78,160), 1, -1);
  wlutRing(330, 7, 240); wallRadial(92, 620, TC(196,204,255));
  wallDisc(456, 468, 186, TC(8,9,30), TC(48,54,124), -1, 1);
  wlutRing(186, 5, 205); wallRadial(456, 468, TC(164,174,250));
  wlutDisc(70, 260, 34); wallRadial(286, 300, TC(120,134,235));
}
// ---- 3. HALO -- anillos concentricos sobre negro ---------------------------
static void wallHalo(){
  wallDiag3(TC(2,2,7), TC(5,5,16), TC(2,2,8));
  wlutDisc(230, 430, 70);  wallRadial(72, 430, TC(46,74,205));      // brillo lateral (asimetria)
  wlutDisc(112, 132, 255); wallRadial(206, 402, TC(17,22,58));      // nucleo lleno
  wlutRing(118, 26, 190);  wallRadial(206, 402, TC(74,112,236));
  wlutRing(122, 5, 235);   wallRadial(206, 402, TC(176,198,255));
  wlutRing(252, 34, 150);  wallRadial(206, 402, TC(58,92,224));
  wlutRing(256, 5, 200);   wallRadial(206, 402, TC(158,182,255));
  wlutRing(392, 44, 120);  wallRadial(206, 402, TC(44,74,206));
  wlutRing(396, 5, 165);   wallRadial(206, 402, TC(140,168,250));
}
// ---- 4..7. Fondos de acompanamiento de los temas ---------------------------
static void wallOnyx(){
  int y0 = gClipY0 < 0 ? 0 : gClipY0, y1 = gClipY1 > SCR_H - 1 ? SCR_H - 1 : gClipY1;
  for(int y = y0; y <= y1; y++) hLine(0, y, SCR_W, TC(0,0,0));
  wlutDisc(60, 380, 46); wallRadial(240, 690, TC(30,64,180));
}
static void wallOceano(){
  wallDiag3(TC(3,32,72), TC(10,116,168), TC(38,206,192));
  wlutDisc(70, 300, 50); wallRadial(400, 180, TC(180,244,236));
}
static void wallVioleta(){
  wallDiag3(TC(26,8,58), TC(122,40,180), TC(232,124,204));
  wlutDisc(80, 320, 46); wallRadial(90, 250, TC(255,210,240));
}
static void wallNaturaleza(){
  wallDiag3(TC(8,44,22), TC(58,138,58), TC(186,222,122));
  wlutDisc(80, 300, 44); wallRadial(380, 640, TC(236,250,190));
}

// Pinta [y0,y1] inclusive del fondo `id`. Tambien fuerza su propio viewport: un
// scroll o una app que haya dejado gClip estrecho no puede recortar el fondo y
// crear una franja negra permanente en el siguiente escritorio.
static void drawWallpaperRowsId(uint16_t* buf, int id, bool blobs, int y0, int y1){
  if(!buf) return;
  if(y0 < 0) y0 = 0;
  if(y1 >= SCR_H) y1 = SCR_H - 1;
  if(y0 > y1) return;
  wallpaperEnsureLut();
  uint16_t* old = gBuf; setBuf(buf);
  bool wasLand = gLand; gLand = false;
  int cx0 = gClipX0, cx1 = gClipX1, cy0 = gClipY0, cy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y1;
  // IMAGEN DEL USUARIO. Se decodifico UNA vez a 480x800 (wallImgLoad): aqui solo
  // se copian filas. Si por lo que sea no esta disponible, se cae al fondo por
  // defecto en vez de dejar la banda en negro.
  if(id == WALL_IMG && wallImg && wallImgOk){
    for(int y = y0; y <= y1; y++)
      memcpy(buf + (size_t)y * SCR_W, wallImg + (size_t)y * SCR_W, (size_t)SCR_W * 2);
  } else {
    if(id == WALL_IMG || id < 0 || id >= WALL_N) id = 0;   // RUTA SEGURA
    switch(id){
      case 1: wallAurora();     break;
      case 2: wallNocturno();   break;
      case 3: wallHalo();       break;
      case 4: wallOnyx();       break;
      case 5: wallOceano();     break;
      case 6: wallVioleta();    break;
      case 7: wallNaturaleza(); break;
      default: wallFlexOriginal(blobs); break;
    }
  }
  gClipX0 = cx0; gClipX1 = cx1; gClipY0 = cy0; gClipY1 = cy1;
  gLand = wasLand;
  setBuf(old);
}
// Compatibilidad: todos los llamantes de siempre siguen pidiendo "el fondo del
// escritorio" con estas dos firmas.
static void drawWallpaperRows(uint16_t* buf, bool blobs, int y0, int y1){
  drawWallpaperRowsId(buf, gWallHome, blobs, y0, y1);
}
static void drawWallpaper(uint16_t* buf, bool blobs){ drawWallpaperRows(buf, blobs, 0, SCR_H - 1); }

// #############################################################
// ##  FONDO A PARTIR DE UNA IMAGEN REAL DEL ALMACENAMIENTO
// ##  ------------------------------------------------------
// ##  Se reutiliza el decodificador que ya existe (FlexOS_JPEG,
// ##  el mismo de Galeria y del visor de archivos) y LittleFS
// ##  (FlexOS_FS). No se enlaza un segundo decodificador ni se
// ##  duplica una sola linea de lectura de archivos.
// ##
// ##  POR QUE SE DECODIFICA UNA SOLA VEZ A 480x800: el fondo se
// ##  pide POR BANDAS (drawWallpaperRows) muchas veces -- cada
// ##  cambio de minuto, cada gesto de pagina, cada repintado de
// ##  un widget. Decodificar el JPEG en cada banda costaria
// ##  decimas de segundo y haria imposible seguir al dedo. Se
// ##  decodifica al elegir la imagen (y al arrancar) y despues
// ##  cada banda es un memcpy.
// ##
// ##  MEMORIA: los 768 KB de wallImg SOLO existen mientras haya
// ##  una imagen elegida. Al volver a un fondo integrado se
// ##  liberan (wallImgDrop).
// ##
// ##  RUTA SEGURA: si el archivo se borro, ya no es un JPEG, es
// ##  progresivo, no cabe o no hay PSRAM, se vuelve al fondo por
// ##  defecto SIN reiniciar y se avisa con un texto real.
// #############################################################
#define WALL_IMG_MAX_BYTES (512 * 1024)     // tope de lectura: un JPEG de fondo no pasa de ahi
static char gWallErr[40] = "";               // ultimo error de carga (para la interfaz)

static int      wiDstX, wiDstY, wiSkipX, wiCopyW, wiRows;
static bool     wallImgRow(void* user, int y, int w, const uint16_t* rgb){
  (void)user;
  if(!wallImg) return false;
  int dy = wiDstY + y;
  if(dy < 0) return true;
  if(dy >= SCR_H) return false;                  // ya se lleno la pantalla: se aborta y se libera solo
  int n = wiCopyW;
  if(wiSkipX + n > w) n = w - wiSkipX;
  if(n > 0) memcpy(wallImg + (size_t)dy * SCR_W + wiDstX, rgb + wiSkipX, (size_t)n * 2);
  wiRows = y + 1;
  return true;
}
static void* wallImgAlloc(size_t n){
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
static void  wallImgFree(void* p){ heap_caps_free(p); }
static void  wallImgDrop(){
  if(wallImg){ heap_caps_free(wallImg); wallImg = NULL; }
  wallImgOk = false;
}
// Decodifica gWallPath a wallImg con el encuadre gWallFit. Devuelve false y
// deja gWallErr con un motivo legible si no se puede.
static bool wallImgLoad(){
  wallImgOk = false;
  gWallErr[0] = 0;
  if(!gWallPath[0]){ snprintf(gWallErr, sizeof(gWallErr), "Sin imagen elegida"); return false; }
  if(!flexFsReady() || !flexFsExists(gWallPath)){
    snprintf(gWallErr, sizeof(gWallErr), "La imagen ya no esta");
    return false;
  }
  uint32_t sz = flexFsSize(gWallPath);
  if(sz == 0 || sz > WALL_IMG_MAX_BYTES){
    snprintf(gWallErr, sizeof(gWallErr), "Imagen demasiado grande");
    return false;
  }
  uint8_t* blob = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!blob) blob = (uint8_t*)malloc(sz);
  if(!blob){ snprintf(gWallErr, sizeof(gWallErr), "Sin memoria"); return false; }
  int rd = flexFsReadBin(gWallPath, blob, sz);
  if(rd <= 0){ free(blob); snprintf(gWallErr, sizeof(gWallErr), "No se pudo leer"); return false; }
  FlexJpegInfo inf;
  if(flexJpegProbe(blob, (size_t)rd, &inf) != FLEXJPG_OK){
    free(blob);
    snprintf(gWallErr, sizeof(gWallErr), "No es un JPEG valido");
    return false;
  }
  if(inf.progressive){
    free(blob);
    snprintf(gWallErr, sizeof(gWallErr), "JPEG progresivo no admitido");
    return false;
  }
  if(!wallImg){
    wallImg = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_H * 2,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if(!wallImg){ free(blob); snprintf(gWallErr, sizeof(gWallErr), "Sin PSRAM para el fondo"); return false; }
  // ENCUADRE. El decodificador solo divide por 1, 2, 4 u 8, asi que el
  // encuadre se elige con ese divisor y con el recorte/centrado de las filas:
  //   rellenar -> el divisor mas grande que AUN cubre la pantalla (se recorta)
  //   ajustar  -> el divisor mas pequeno que CABE entera (se centra, con fondo)
  //   centrar  -> 1:1 y se recorta por el centro
  int den = 1;
  if(gWallFit == 1){                                   // ajustar
    while(den < 8 && (inf.width / den > SCR_W || inf.height / den > SCR_H)) den *= 2;
  } else if(gWallFit == 0){                            // rellenar
    while(den < 8 && inf.width / (den * 2) >= SCR_W && inf.height / (den * 2) >= SCR_H) den *= 2;
  }                                                    // centrar: den = 1
  int ow = inf.width / den, oh = inf.height / den;
  if(ow < 1) ow = 1;
  if(oh < 1) oh = 1;
  // Fondo por debajo: el color medio de la propia imagen no se conoce todavia,
  // asi que se usa el fondo integrado por defecto. Solo se ve con "ajustar".
  drawWallpaperRowsId(wallImg, 0, true, 0, SCR_H - 1);
  wiDstX  = (ow >= SCR_W) ? 0 : (SCR_W - ow) / 2;
  wiSkipX = (ow >= SCR_W) ? (ow - SCR_W) / 2 : 0;
  wiCopyW = (ow >= SCR_W) ? SCR_W : ow;
  wiDstY  = (oh >= SCR_H) ? -((oh - SCR_H) / 2) : (SCR_H - oh) / 2;
  wiRows  = 0;
  int r = flexJpegDecode(blob, (size_t)rd, ow, oh, (uint32_t)SCR_W * SCR_H * 4, NULL,
                         wallImgRow, NULL, wallImgAlloc, wallImgFree);
  free(blob);
  // Un aborto por "ya se lleno la pantalla" no es un fallo: con "rellenar" y
  // "centrar" es el caso NORMAL, porque la imagen es mas alta que el panel.
  if(wiRows <= 0){
    snprintf(gWallErr, sizeof(gWallErr), "%s", flexJpegErrStr(r));
    wallImgDrop();
    return false;
  }
  wallImgOk = true;
  return true;
}
// Se llama al arrancar y cada vez que cambia la eleccion. Si la imagen ya no se
// puede cargar, el escritorio vuelve al fondo integrado por defecto en vez de
// quedarse en negro o reiniciar.
static void wallEnsureImage(){
  bool needed = (gWallHome == WALL_IMG || gWallLock == WALL_IMG);
  if(!needed){ wallImgDrop(); return; }
  if(wallImgOk) return;
  if(wallImgLoad()) return;
  if(gWallHome == WALL_IMG) gWallHome = 0;
  if(gWallLock == WALL_IMG) gWallLock = 0;
}

// #############################################################
// ##  PALETA EXTRAIDA DEL FONDO
// ##  ------------------------------------------------------
// ##  Muestreo REDUCIDO -- una rejilla de 20x20 = 400 muestras de
// ##  384.000 pixeles -- y solo cuando cambia el fondo. Jamas por
// ##  frame. El acento resultante pasa por un control de
// ##  luminancia para que SIEMPRE tenga contraste util contra el
// ##  texto que se le ponga encima.
// #############################################################
static bool     gWallPalOn  = false;       // "Aplicar paleta al sistema" (NVS "wallpal")
static bool     gWallPalOk  = false;       // ya hay una paleta calculada
static uint16_t gWallAcc    = 0;           // acento
static uint16_t gWallAcc2   = 0;           // acento claro
static uint8_t  lum565(uint16_t c){
  int r = ((c >> 11) & 0x1F) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
  return (uint8_t)((r * 77 + g * 151 + b * 28) >> 8);
}
// Blanco o casi-negro segun la luminancia REAL del fondo (Rec.601): nunca se
// dibuja texto de un color parecido al de su superficie.
static uint16_t onColor(uint16_t bg){ return lum565(bg) > 140 ? TC(16,18,26) : TC(255,255,255); }
static void wallPaletteBuild(const uint16_t* src){
  if(!src) return;
  int bestSat = -1, br = 90, bg = 150, bb = 245;
  for(int y = 8; y < SCR_H; y += 40)
    for(int x = 8; x < SCR_W; x += 24){
      uint16_t c = src[(size_t)y * SCR_W + x];
      int r = ((c >> 11) & 0x1F) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
      int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
      int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
      int sat = (mx - mn) * (mx > 40 ? 1 : 0);          // ignora las zonas casi negras
      if(sat > bestSat){ bestSat = sat; br = r; bg = g; bb = b; }
    }
  int lr = br, lg = bg, lb = bb;
  int lum = (lr * 77 + lg * 151 + lb * 28) >> 8;
  if(lum < 70){ lr = lr * 2 + 40; lg = lg * 2 + 40; lb = lb * 2 + 40; }
  if(lr > 255) lr = 255;
  if(lg > 255) lg = 255;
  if(lb > 255) lb = 255;
  gWallAcc  = rgb565((uint8_t)lr, (uint8_t)lg, (uint8_t)lb);
  gWallAcc2 = rgb565((uint8_t)((lr + 255) / 2), (uint8_t)((lg + 255) / 2), (uint8_t)((lb + 255) / 2));
  gWallPalOk = true;
}
// ---- Temas integrados -------------------------------------------------------
// Un tema NO es una paleta nueva: es una combinacion coherente de los ajustes
// que ya existen (apariencia, material, estilo de icono, fondo y acento). Asi
// no hay dos fuentes de verdad de color -- el tema semantico sigue mandando --
// y aplicar uno es cambiar cinco preferencias de golpe, no reescribir la UI.
// Los colores van como bytes RGB para que la tabla se quede en .rodata (flash).
struct HomeLook {
  const char* name;
  uint8_t dark, glass, iconStyle, wall, palette;
  uint8_t ar, ag, ab;      // acento
  uint8_t sr, sg, sb;      // acento claro
};
#define LOOK_N 8
static const HomeLook LOOKS[LOOK_N] = {
  { "Flex Original",  1, 0, 0, 0, 0,  60,110,235,  140,180,250 },
  { "Claro",          0, 1, 0, 1, 0,  45, 95,225,  130,180,250 },
  { "Oscuro",         1, 1, 1, 2, 0,  96,124,235,  160,180,255 },
  { "AMOLED",         1, 0, 0, 4, 0, 120,140,255,  180,196,255 },
  { "Oceano",         1, 1, 1, 5, 1,  38,190,196,  150,240,236 },
  { "Violeta",        1, 1, 1, 6, 1, 186, 96,232,  224,168,248 },
  { "Naturaleza",     0, 1, 0, 7, 1,  72,170, 80,  168,224,150 },
  { "Alto contraste", 1, 0, 0, 4, 0, 255,214, 10,  255,236,120 },
};
static uint8_t gHomeLook = 0;            // tema integrado activo (NVS "hlook")
static uint16_t lookAcc(int i){  return rgb565(LOOKS[i].ar, LOOKS[i].ag, LOOKS[i].ab); }
static uint16_t lookAcc2(int i){ return rgb565(LOOKS[i].sr, LOOKS[i].sg, LOOKS[i].sb); }

// (wallAccent()/wallAccent2() se definen justo detras del tema semantico: su
// respaldo cuando la paleta esta apagada es el acento del tema.)

