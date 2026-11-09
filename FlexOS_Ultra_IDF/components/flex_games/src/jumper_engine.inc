// GENERADO por tools/gen_jumper_engine.py desde FlexOS_Ultra/ (no editar a mano; run_tests.sh lo comprueba).
// Motor de dibujo de Arduino que usa Jumper: primitivas, fuente Outfit antialiasada y fuente 5x7.
// ---- FlexOS_Ultra_Gfx.h: gClipY0,gClipX0,gClipLY0,gLand,rgb565,DIV255,mix565,isqrt32,putPhys,putPhysA,px,pxA,hLine,hLineA,vLine,fillSpanLand,fillSpanLandA,rowsInClip,fillRect,fillRectA,rrInset,fillRoundRect,fillRoundRectA,drawRoundRect,fillCircle,fillCircleA,drawCircle,AA_EPS,aaClipBox,fillCircleAA,strokeSegAA
// GENERADO por extract_arduino.py desde FlexOS_Ultra/ (no editar)
// ---- FlexOS_Ultra_Gfx.h: gClipY0
static int gClipY0 = 0, gClipY1 = SCR_H - 1;
// ---- FlexOS_Ultra_Gfx.h: gClipX0
static int gClipX0 = 0, gClipX1 = SCR_W - 1;   // recorte horizontal
// ---- FlexOS_Ultra_Gfx.h: gClipLY0
static int gClipLY0 = 0, gClipLY1 = SCR_W - 1;
// ---- FlexOS_Ultra_Gfx.h: gLand
static bool gLand = false;
// ---- FlexOS_Ultra_Gfx.h: rgb565
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// ---- FlexOS_Ultra_Gfx.h: DIV255
#define DIV255(v)  (uint16_t)(((v) + 1u + ((v) >> 8)) >> 8)
// ---- FlexOS_Ultra_Gfx.h: mix565
static inline uint16_t mix565(uint16_t a, uint16_t b, uint8_t t){
  uint32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint32_t it = 255u - t;
  uint32_t rr = ar * it + br * t;
  uint32_t rg = ag * it + bg * t;
  uint32_t rb = ab * it + bb * t;
  return (uint16_t)((DIV255(rr) << 11) | (DIV255(rg) << 5) | DIV255(rb));
}
// ---- FlexOS_Ultra_Gfx.h: isqrt32
static inline int isqrt32(int v){
  if(v <= 0) return 0;
  uint32_t op = (uint32_t)v, res = 0, one = 1u << 30;
  while(one > op) one >>= 2;
  while(one){
    if(op >= res + one){ op -= res + one; res += one << 1; }
    res >>= 1; one >>= 2;
  }
  return (int)res;
}
// ---- FlexOS_Ultra_Gfx.h: putPhys
static inline void putPhys(int lx, int ly, uint16_t c){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  if(ly < gClipLY0 || ly > gClipLY1) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
// ---- FlexOS_Ultra_Gfx.h: putPhysA
static inline void putPhysA(int lx, int ly, uint16_t c, uint8_t a){
  if((unsigned)lx >= SCR_H || (unsigned)ly >= SCR_W) return;
  if(ly < gClipLY0 || ly > gClipLY1) return;
  int x = (SCR_W - 1) - ly, y = lx;
  if(y < gClipY0 || y > gClipY1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x; gBuf[i] = mix565(gBuf[i], c, a);
}
// ---- FlexOS_Ultra_Gfx.h: px
static inline void px(int x, int y, uint16_t c){
  if(gLand){ putPhys(x, y, c); return; }
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  gBuf[(size_t)y * SCR_W + x] = c;
}
// ---- FlexOS_Ultra_Gfx.h: pxA
static inline void pxA(int x, int y, uint16_t c, uint8_t a){
  if(gLand){ putPhysA(x, y, c, a); return; }
  if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1 || x < gClipX0 || x > gClipX1) return;
  if(a >= 255){ gBuf[(size_t)y * SCR_W + x] = c; return; }
  if(a == 0) return;
  size_t i = (size_t)y * SCR_W + x;
  gBuf[i] = mix565(gBuf[i], c, a);
}
// ---- FlexOS_Ultra_Gfx.h: hLine
static void hLine(int x, int y, int w, uint16_t c){
  if(w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) putPhys(x + i, y, c); return; }
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }     // recorte horizontal
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = c;
}
// ---- FlexOS_Ultra_Gfx.h: hLineA
static void hLineA(int x, int y, int w, uint16_t c, uint8_t a){
  if(a >= 255){ hLine(x, y, w, c); return; }
  if(a == 0 || w <= 0) return;
  if(gLand){ for(int i = 0; i < w; i++) pxA(x + i, y, c, a); return; }   // landscape: ruta original
  if((unsigned)y >= SCR_H) return;
  if(y < gClipY0 || y > gClipY1) return;
  if(x < 0){ w += x; x = 0; }
  if(x + w > SCR_W) w = SCR_W - x;
  if(x < gClipX0){ w -= (gClipX0 - x); x = gClipX0; }
  if(x + w > gClipX1 + 1) w = gClipX1 + 1 - x;
  if(w <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < w; i++) p[i] = mix565(p[i], c, a);
}
// ---- FlexOS_Ultra_Gfx.h: vLine
static void vLine(int x, int y, int h, uint16_t c){
  if(h <= 0) return;
  if(gLand){ for(int i = 0; i < h; i++) putPhys(x, y + i, c); return; }
  if((unsigned)x >= SCR_W || x < gClipX0 || x > gClipX1) return;   // recorte horizontal
  if(y < 0){ h += y; y = 0; }
  if(y < gClipY0){ h -= (gClipY0 - y); y = gClipY0; }
  if(y + h > SCR_H) h = SCR_H - y;
  if(y + h > gClipY1 + 1) h = gClipY1 + 1 - y;
  if(h <= 0) return;
  uint16_t* p = gBuf + (size_t)y * SCR_W + x;
  for(int i = 0; i < h; i++){ *p = c; p += SCR_W; }
}
// ---- FlexOS_Ultra_Gfx.h: fillSpanLand
static void fillSpanLand(int lx, int ly, int n, uint16_t c){
  if(n <= 0) return;
  if((unsigned)lx >= SCR_H) return;
  if(lx < gClipY0 || lx > gClipY1) return;
  if(ly < gClipLY0){ n -= gClipLY0 - ly; ly = gClipLY0; }
  if(ly + n > gClipLY1 + 1) n = gClipLY1 + 1 - ly;
  if(ly < 0){ n += ly; ly = 0; }
  if(ly + n > SCR_W) n = SCR_W - ly;
  if(n <= 0) return;
  uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - (ly + n));
  for(int i = 0; i < n; i++) p[i] = c;
}
// ---- FlexOS_Ultra_Gfx.h: fillSpanLandA
static void fillSpanLandA(int lx, int ly, int n, uint16_t c, uint8_t a){
  if(a >= 255){ fillSpanLand(lx, ly, n, c); return; }
  if(a == 0 || n <= 0) return;
  if((unsigned)lx >= SCR_H) return;
  if(lx < gClipY0 || lx > gClipY1) return;
  if(ly < gClipLY0){ n -= gClipLY0 - ly; ly = gClipLY0; }
  if(ly + n > gClipLY1 + 1) n = gClipLY1 + 1 - ly;
  if(ly < 0){ n += ly; ly = 0; }
  if(ly + n > SCR_W) n = SCR_W - ly;
  if(n <= 0) return;
  uint16_t* p = gBuf + (size_t)lx * SCR_W + (SCR_W - (ly + n));
  for(int i = 0; i < n; i++) p[i] = mix565(p[i], c, a);
}
// ---- FlexOS_Ultra_Gfx.h: rowsInClip
static inline bool rowsInClip(int y, int h, int* j0, int* j1){
  int a = 0, b = h;                                // intervalo [a, b)
  if(y + a < gClipY0)     a = gClipY0 - y;
  if(y + a < 0)           a = -y;
  if(y + b > gClipY1 + 1) b = gClipY1 + 1 - y;
  if(y + b > SCR_H)       b = SCR_H - y;
  if(a >= b) return false;
  *j0 = a; *j1 = b;
  return true;
}
// ---- FlexOS_Ultra_Gfx.h: fillRect
static void fillRect(int x, int y, int w, int h, uint16_t c){
  if(gLand){ for(int i = 0; i < w; i++) fillSpanLand(x + i, y, h, c); return; }
  int j0, j1;
  if(!rowsInClip(y, h, &j0, &j1)) return;
  for(int j = j0; j < j1; j++) hLine(x, y + j, w, c);
}
// ---- FlexOS_Ultra_Gfx.h: fillRectA
static void fillRectA(int x, int y, int w, int h, uint16_t c, uint8_t a){
  if(gLand){ for(int i = 0; i < w; i++) fillSpanLandA(x + i, y, h, c, a); return; }
  for(int j = 0; j < h; j++) hLineA(x, y + j, w, c, a);
}
// ---- FlexOS_Ultra_Gfx.h: rrInset
static int rrInset(int k, int len, int r){
  if(k < r){ int d = r - 1 - k; return r - isqrt32(r * r - d * d); }
  if(k >= len - r){ int d = k - (len - r); return r - isqrt32(r * r - d * d); }
  return 0;
}
// ---- FlexOS_Ultra_Gfx.h: fillRoundRect
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  if(gLand){
    for(int i = 0; i < w; i++){
      int in = rrInset(i, w, r);
      fillSpanLand(x + i, y + in, h - 2 * in, c);
    }
    return;
  }
  int j0, j1;
  if(!rowsInClip(y, h, &j0, &j1)) return;
  for(int j = j0; j < j1; j++){
    int inset = rrInset(j, h, r);
    hLine(x + inset, y + j, w - 2 * inset, c);
  }
}
// ---- FlexOS_Ultra_Gfx.h: fillRoundRectA
static void fillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a){
  if(w <= 0 || h <= 0) return;
  if(r < 0) r = 0;
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  if(gLand){
    for(int i = 0; i < w; i++){
      int in = rrInset(i, w, r);
      fillSpanLandA(x + i, y + in, h - 2 * in, c, a);
    }
    return;
  }
  int j0, j1;
  if(!rowsInClip(y, h, &j0, &j1)) return;
  for(int j = j0; j < j1; j++){
    int inset = rrInset(j, h, r);
    hLineA(x + inset, y + j, w - 2 * inset, c, a);
  }
}
// ---- FlexOS_Ultra_Gfx.h: drawRoundRect
static void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c){
  if(2 * r > w) r = w / 2;
  if(2 * r > h) r = h / 2;
  hLine(x + r, y, w - 2 * r, c);
  hLine(x + r, y + h - 1, w - 2 * r, c);
  vLine(x, y + r, h - 2 * r, c);
  vLine(x + w - 1, y + r, h - 2 * r, c);
  // esquinas
  int f = 1 - r, ddx = 1, ddy = -2 * r, xx = 0, yy = r;
  while(xx < yy){
    if(f >= 0){ yy--; ddy += 2; f += ddy; }
    xx++; ddx += 2; f += ddx;
    px(x + r - xx, y + r - yy, c); px(x + w - r - 1 + xx, y + r - yy, c);
    px(x + r - yy, y + r - xx, c); px(x + w - r - 1 + yy, y + r - xx, c);
    px(x + r - xx, y + h - r - 1 + yy, c); px(x + w - r - 1 + xx, y + h - r - 1 + yy, c);
    px(x + r - yy, y + h - r - 1 + xx, c); px(x + w - r - 1 + yy, y + h - r - 1 + xx, c);
  }
}
// ---- FlexOS_Ultra_Gfx.h: fillCircle
static void fillCircle(int cx, int cy, int r, uint16_t c){
  if(r <= 0){ px(cx, cy, c); return; }
  int j0, j1;
  if(gLand){ j0 = 0; j1 = 2 * r + 1; }
  else if(!rowsInClip(cy - r, 2 * r + 1, &j0, &j1)) return;
  for(int j = j0; j < j1; j++){
    int dy = j - r;
    int dx = isqrt32(r * r - dy * dy);
    hLine(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}
// ---- FlexOS_Ultra_Gfx.h: fillCircleA
static void fillCircleA(int cx, int cy, int r, uint16_t c, uint8_t a){
  if(r <= 0){ pxA(cx, cy, c, a); return; }
  int j0, j1;
  if(gLand){ j0 = 0; j1 = 2 * r + 1; }
  else if(!rowsInClip(cy - r, 2 * r + 1, &j0, &j1)) return;
  for(int j = j0; j < j1; j++){
    int dy = j - r;
    int dx = isqrt32(r * r - dy * dy);
    hLineA(cx - dx, cy + dy, 2 * dx + 1, c, a);
  }
}
// ---- FlexOS_Ultra_Gfx.h: drawCircle
static void drawCircle(int cx, int cy, int r, uint16_t c){
  int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  px(cx, cy + r, c); px(cx, cy - r, c); px(cx + r, cy, c); px(cx - r, cy, c);
  while(x < y){
    if(f >= 0){ y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    px(cx + x, cy + y, c); px(cx - x, cy + y, c);
    px(cx + x, cy - y, c); px(cx - x, cy - y, c);
    px(cx + y, cy + x, c); px(cx - y, cy + x, c);
    px(cx + y, cy - x, c); px(cx - y, cy - x, c);
  }
}
// ---- FlexOS_Ultra_Gfx.h: AA_EPS
#define AA_EPS 0.02f      // holgura de los atajos (ver la nota 2 de arriba)
// ---- FlexOS_Ultra_Gfx.h: aaClipBox
static inline bool aaClipBox(int* x0, int* x1, int* y0, int* y1){
  if(gLand){
    if(*x0 < gClipY0)    *x0 = gClipY0;          // la X logica es la fila fisica
    if(*x1 > gClipY1)    *x1 = gClipY1;
    if(*y0 < gClipLY0)   *y0 = gClipLY0;
    if(*y1 > gClipLY1)   *y1 = gClipLY1;
    if(*x0 < 0)          *x0 = 0;
    if(*x1 > SCR_H - 1)  *x1 = SCR_H - 1;
    if(*y0 < 0)          *y0 = 0;
    if(*y1 > SCR_W - 1)  *y1 = SCR_W - 1;
    return (*x0 <= *x1 && *y0 <= *y1);
  }
  if(*x0 < gClipX0)    *x0 = gClipX0;
  if(*x1 > gClipX1)    *x1 = gClipX1;
  if(*y0 < gClipY0)    *y0 = gClipY0;
  if(*y1 > gClipY1)    *y1 = gClipY1;
  if(*x0 < 0)          *x0 = 0;
  if(*x1 > SCR_W - 1)  *x1 = SCR_W - 1;
  if(*y0 < 0)          *y0 = 0;
  if(*y1 > SCR_H - 1)  *y1 = SCR_H - 1;
  return (*x0 <= *x1 && *y0 <= *y1);
}
// ---- FlexOS_Ultra_Gfx.h: fillCircleAA
static void fillCircleAA(float cx, float cy, float r, uint16_t col){
  int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  if(!aaClipBox(&x0, &x1, &y0, &y1)) return;
  const float rOut = r + 0.5f;
  const float rOutS = rOut + AA_EPS, rInS = r - 0.5f - AA_EPS;
  const float rOut2 = rOutS * rOutS, rIn2 = (rInS > 0.0f) ? rInS * rInS : -1.0f;
  for(int y = y0; y <= y1; y++){
    float dy = (float)y - cy, dy2 = dy * dy;
    if(dy2 >= rOut2) continue;                       // fila entera fuera del disco
    // Tramo OPACO de esta fila, con un pixel de margen a cada lado para que
    // ningun pixel de cobertura casi-plena se cuele en el relleno solido.
    int sx0 = 1, sx1 = 0;
    if(rIn2 > 0.0f && dy2 < rIn2){
      float hw = sqrtf(rIn2 - dy2);
      sx0 = (int)floorf(cx - hw) + 1;
      sx1 = (int)ceilf (cx + hw) - 1;
      if(sx0 < x0) sx0 = x0;
      if(sx1 > x1) sx1 = x1;
      if(sx0 <= sx1) hLine(sx0, y, sx1 - sx0 + 1, col);
      else { sx0 = 1; sx1 = 0; }
    }
    for(int x = x0; x <= x1; x++){
      if(x >= sx0 && x <= sx1){ x = sx1; continue; }  // ya relleno de una pasada
      float dx = (float)x - cx, d2 = dx * dx + dy2;
      if(d2 >= rOut2) continue;                      // fuera: ni una raiz
      float cov = rOut - sqrtf(d2);
      if(cov <= 0) continue; if(cov > 1) cov = 1;
      pxA(x, y, col, (uint8_t)(cov * 255));
    }
  }
}
// ---- FlexOS_Ultra_Gfx.h: strokeSegAA
static void strokeSegAA(float x0, float y0, float x1, float y1, float rad, uint16_t col){
  int minx = (int)floorf(fminf(x0, x1) - rad - 1), maxx = (int)ceilf(fmaxf(x0, x1) + rad + 1);
  int miny = (int)floorf(fminf(y0, y1) - rad - 1), maxy = (int)ceilf(fmaxf(y0, y1) + rad + 1);
  if(!aaClipBox(&minx, &maxx, &miny, &maxy)) return;
  // Constantes del segmento: estaban DENTRO de distToSeg, o sea se rehacian
  // una vez por pixel de la caja para obtener siempre lo mismo.
  //
  // LA ARITMETICA ES LA MISMA, EXPRESION POR EXPRESION. Se conserva la
  // DIVISION por l2 (no el producto por su reciproco) y el mismo orden de
  // operaciones que tenia distToSeg. Escribirlo "mejor" -- reciproco
  // premultiplicado, restas reagrupadas -- cambia el ultimo bit de t, y ese
  // bit se propaga a un nivel de alpha de mas o de menos en los pixeles del
  // borde: 59 de 24.000 geometrias al azar salian distintas. Aqui no se
  // toca el calculo; lo que se quita es el trabajo que NO hacia falta.
  const float sdx = x1 - x0, sdy = y1 - y0;
  const float l2  = sdx * sdx + sdy * sdy;
  const bool  deg = (l2 <= 0.0f);                    // segmento degenerado: t = 0
  const float rOut = rad + 0.5f;
  const float rOutS = rOut + AA_EPS, rInS = rad - 0.5f - AA_EPS;
  const float rOut2 = rOutS * rOutS, rIn2 = (rInS > 0.0f) ? rInS * rInS : -1.0f;
  // DESCARTE SIN DIVIDIR. La distancia de un punto al SEGMENTO nunca es menor
  // que su distancia a la RECTA que lo contiene, y esa sale del producto
  // vectorial: dist_recta^2 = cross^2 / l2. Asi que cross^2 >= rOut^2 * l2
  // basta para saber que el pixel esta fuera -- un producto y una comparacion,
  // sin division y sin raiz. En una marca diagonal de la rosa eso son dos
  // tercios de la caja descartados antes de tocar la aritmetica cara, y es
  // EXACTO: solo descarta pixeles que la version original tambien descartaba.
  const float lineRej = rOut2 * l2;
  for(int y = miny; y <= maxy; y++){
    float fy = (float)y;
    float ty = (fy - y0) * sdy;                      // termino de fila del producto escalar
    float cy_ = (fy - y0) * sdx;                     // termino de fila del producto vectorial
    for(int x = minx; x <= maxx; x++){
      float fx = (float)x;
      if(!deg){
        float cross = (fx - x0) * sdy - cy_;
        if(cross * cross >= lineRej) continue;       // fuera de la banda de la recta
      }
      float t = deg ? 0.0f : (((fx - x0) * sdx + ty) / l2);
      if(t < 0) t = 0; else if(t > 1) t = 1;
      float qx = x0 + t * sdx - fx, qy = y0 + t * sdy - fy;
      float d2 = qx * qx + qy * qy;
      if(d2 >= rOut2) continue;                      // fuera del trazo: sin raiz
      if(d2 <= rIn2){ px(x, y, col); continue; }     // interior opaco: sin raiz ni mezcla
      float cov = rOut - sqrtf(d2);
      if(cov <= 0) continue; if(cov > 1) cov = 1;
      pxA(x, y, col, (uint8_t)(cov * 255));
    }
  }
}

// ---- FlexOS_Ultra_Text.h: FONT5x7,GLYPH_INVQ,GLYPH_INVEXCL,GLYPH_MIDDOT,ACC_NONE,mapCP,drawGlyphRaw,drawAccent,drawGlyphSmooth
// GENERADO por extract_arduino.py desde FlexOS_Ultra/ (no editar)
// ---- FlexOS_Ultra_Text.h: FONT5x7
static const uint8_t FONT5x7[95][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
  {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
  {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08}
};
// ---- FlexOS_Ultra_Text.h: GLYPH_INVQ
static const uint8_t GLYPH_INVQ[5]    = {0x30,0x48,0x45,0x40,0x20};
// ---- FlexOS_Ultra_Text.h: GLYPH_INVEXCL
static const uint8_t GLYPH_INVEXCL[5] = {0x00,0x00,0x7D,0x00,0x00};
// ---- FlexOS_Ultra_Text.h: GLYPH_MIDDOT
static const uint8_t GLYPH_MIDDOT[5] = {0x00,0x08,0x08,0x08,0x00};
// ---- FlexOS_Ultra_Text.h: ACC_NONE
enum { ACC_NONE=0, ACC_ACUTE, ACC_GRAVE, ACC_TILDE, ACC_DIAER, ACC_CIRC, ACC_CED };
// ---- FlexOS_Ultra_Text.h: mapCP
static void mapCP(uint32_t cp, uint8_t &base, uint8_t &acc){
  acc = ACC_NONE;
  if(cp < 0x80){ base = (cp >= 0x20 && cp <= 0x7E) ? (uint8_t)cp : '?'; return; }
  switch(cp){
    case 0xE1: base='a'; acc=ACC_ACUTE; return;   case 0xE9: base='e'; acc=ACC_ACUTE; return;
    case 0xED: base='i'; acc=ACC_ACUTE; return;   case 0xF3: base='o'; acc=ACC_ACUTE; return;
    case 0xFA: base='u'; acc=ACC_ACUTE; return;   case 0xFC: base='u'; acc=ACC_DIAER; return;
    case 0xF1: base='n'; acc=ACC_TILDE; return;
    case 0xC1: base='A'; acc=ACC_ACUTE; return;   case 0xC9: base='E'; acc=ACC_ACUTE; return;
    case 0xCD: base='I'; acc=ACC_ACUTE; return;   case 0xD3: base='O'; acc=ACC_ACUTE; return;
    case 0xDA: base='U'; acc=ACC_ACUTE; return;   case 0xD1: base='N'; acc=ACC_TILDE; return;
    case 0xDC: base='U'; acc=ACC_DIAER; return;
    case 0xE0: base='a'; acc=ACC_GRAVE; return;   case 0xE8: base='e'; acc=ACC_GRAVE; return;
    case 0xEC: base='i'; acc=ACC_GRAVE; return;   case 0xF2: base='o'; acc=ACC_GRAVE; return;
    case 0xF9: base='u'; acc=ACC_GRAVE; return;
    case 0xE2: base='a'; acc=ACC_CIRC; return;    case 0xEA: base='e'; acc=ACC_CIRC; return;
    case 0xEE: base='i'; acc=ACC_CIRC; return;    case 0xF4: base='o'; acc=ACC_CIRC; return;
    case 0xFB: base='u'; acc=ACC_CIRC; return;
    case 0xE3: base='a'; acc=ACC_TILDE; return;   case 0xF5: base='o'; acc=ACC_TILDE; return;
    case 0xE7: base='c'; acc=ACC_CED; return;     case 0xC7: base='C'; acc=ACC_CED; return;
    case 0xBF: base=1; return;                    case 0xA1: base=2; return;
    case 0xB7: base=3; return;                    // punto medio: separador de etiquetas
    default:   base='?'; return;
  }
}
// ---- FlexOS_Ultra_Text.h: drawGlyphRaw
static void drawGlyphRaw(int x, int y, uint8_t base, int s, uint16_t col){
  const uint8_t* g;
  if(base == 1) g = GLYPH_INVQ;
  else if(base == 2) g = GLYPH_INVEXCL;
  else if(base == 3) g = GLYPH_MIDDOT;
  else { int idx = (int)base - 0x20; if(idx < 0 || idx > 94) return; g = FONT5x7[idx]; }
  for(int c = 0; c < 5; c++){
    uint8_t bits = g[c];
    for(int r = 0; r < 7; r++){
      if(bits & (1 << r)){
        if(s == 1) px(x + c, y + r, col);
        else fillRect(x + c * s, y + r * s, s, s, col);
      }
    }
  }
}
// ---- FlexOS_Ultra_Text.h: drawAccent
static void drawAccent(int x, int y, int s, uint8_t acc, uint16_t col){
  float cx = x + 2.5f * s;
  float r = s * 0.55f; if(r < 1.0f) r = 1.0f;
  switch(acc){
    case ACC_ACUTE: strokeSegAA(cx - s, y - s, cx + s, y - 3 * s, r, col); break;
    case ACC_GRAVE: strokeSegAA(cx - s, y - 3 * s, cx + s, y - s, r, col); break;
    case ACC_CIRC:  strokeSegAA(cx - 1.6f * s, y - s, cx, y - 3 * s, r, col);
                    strokeSegAA(cx, y - 3 * s, cx + 1.6f * s, y - s, r, col); break;
    case ACC_TILDE: strokeSegAA(cx - 2 * s, y - 2 * s, cx - 0.4f * s, y - 2.9f * s, r, col);
                    strokeSegAA(cx - 0.4f * s, y - 2.9f * s, cx + 0.6f * s, y - 1.9f * s, r, col);
                    strokeSegAA(cx + 0.6f * s, y - 1.9f * s, cx + 2 * s, y - 2.7f * s, r, col); break;
    case ACC_DIAER: fillCircleAA(cx - s, y - 2 * s, r, col);
                    fillCircleAA(cx + s, y - 2 * s, r, col); break;
    case ACC_CED:   strokeSegAA(cx, y + 7 * s - s, cx - 1.2f * s, y + 7 * s + 0.6f * s, r, col); break;
  }
}
// ---- FlexOS_Ultra_Text.h: drawGlyphSmooth
static void drawGlyphSmooth(int x, int y, uint8_t base, int s, uint16_t col, uint8_t alpha){
  const uint8_t* g;
  if(base == 1) g = GLYPH_INVQ;
  else if(base == 2) g = GLYPH_INVEXCL;
  else if(base == 3) g = GLYPH_MIDDOT;
  else { int idx = (int)base - 0x20; if(idx < 0 || idx > 94) return; g = FONT5x7[idx]; }
  if(s <= 1){
    for(int c = 0; c < 5; c++){ uint8_t bits = g[c];
      for(int r = 0; r < 7; r++) if(bits & (1 << r)) pxA(x + c, y + r, col, alpha); }
    return;
  }
  const int SS = 3;
  int W = 5 * s, H = 7 * s;
  for(int ty = 0; ty < H; ty++){
    for(int tx = 0; tx < W; tx++){
      int cov = 0;
      for(int sy = 0; sy < SS; sy++) for(int sx = 0; sx < SS; sx++){
        int ix = (int)((tx + (sx + 0.5f) / SS) / s);
        int iy = (int)((ty + (sy + 0.5f) / SS) / s);
        if(ix >= 0 && ix < 5 && iy >= 0 && iy < 7 && (g[ix] & (1 << iy))) cov++;
      }
      if(cov) pxA(x + tx, y + ty, col, (uint8_t)((cov * alpha) / (SS * SS)));
    }
  }
}

// ---- FlexOS_Ultra_Font.h: FONT_LINEH,FONT_ASC,FG,FBM,fontIdx,FONT_HPS,FONT_CAPOFF,fgPix,fontSc,utf8Decode,cpZeroWidth,cpFold,nextCP,GLYPH_COLS_MAX,gGlyphCx,gGlyphFx,drawGlyphScaled,textW,drawTextA,drawText,drawTextC,drawTextCA,drawTextR
// GENERADO por extract_arduino.py desde FlexOS_Ultra/ (no editar)
// ---- FlexOS_Ultra_Font.h: FONT_LINEH
#define FONT_LINEH 51
// ---- FlexOS_Ultra_Font.h: FONT_ASC
#define FONT_ASC 40
// ---- FlexOS_Ultra_Font.h: FG
static const FGlyph FG[127] = {
  {0,0,0,0,8,0},{5,30,3,10,10,0},{12,10,2,11,16,90},{21,29,2,11,26,150},{19,37,2,7,24,469},{22,29,2,11,26,839},
  {24,29,2,11,26,1158},{5,10,2,11,9,1506},{10,36,2,9,12,1536},{10,36,1,9,12,1716},{16,17,2,9,20,1896},{17,19,2,17,22,2032},
  {6,11,2,35,11,2203},{13,4,3,27,18,2236},{5,5,3,35,12,2264},{16,32,0,10,16,2279},{23,29,2,11,26,2535},{10,29,1,11,14,2883},
  {19,29,1,11,22,3028},{19,29,1,11,22,3318},{22,29,1,11,24,3608},{20,29,0,11,22,3927},{19,29,2,11,23,4217},{18,29,1,11,21,4507},
  {19,29,2,11,22,4768},{20,29,2,11,23,5058},{5,18,3,22,11,5348},{6,24,2,22,11,5402},{17,19,2,17,22,5474},{17,13,2,20,22,5645},
  {17,19,2,17,22,5762},{18,30,1,10,20,5933},{26,27,2,17,30,6203},{27,29,1,11,28,6554},{20,29,3,11,25,6960},{25,29,2,11,28,7250},
  {24,29,3,11,30,7627},{19,29,3,11,24,7975},{18,29,3,11,23,8265},{27,29,2,11,31,8526},{22,29,3,11,28,8932},{4,29,3,11,10,9251},
  {16,29,1,11,20,9309},{23,29,3,11,27,9541},{18,29,3,11,22,9889},{27,29,3,11,34,10150},{22,29,3,11,28,10556},{28,29,2,11,32,10875},
  {19,29,3,11,24,11281},{30,31,2,11,33,11571},{21,29,3,11,25,12036},{19,29,1,11,22,12355},{23,29,1,11,25,12645},{22,29,3,11,27,12993},
  {26,29,1,11,28,13312},{37,29,1,11,39,13689},{26,29,1,11,28,14240},{25,29,0,11,27,14617},{20,29,2,11,23,14994},{9,33,3,11,14,15284},
  {16,32,0,10,16,15449},{9,33,1,11,14,15705},{14,11,2,10,18,15870},{19,4,1,41,20,15947},{9,9,1,9,11,15987},{19,20,1,20,23,16032},
  {19,29,3,11,23,16232},{18,20,1,20,20,16522},{19,29,1,11,23,16702},{19,20,1,20,21,16992},{18,30,0,10,16,17192},{19,28,1,20,23,17462},
  {17,29,3,11,22,17742},{5,29,2,11,9,18003},{13,37,-5,11,9,18090},{17,29,3,11,20,18349},{4,29,3,11,9,18610},{29,20,3,20,34,18668},
  {17,20,3,20,22,18968},{20,20,1,20,23,19148},{19,28,3,20,23,19348},{20,28,1,20,23,19628},{14,20,3,20,17,19908},{16,20,0,20,17,20048},
  {14,28,0,12,15,20208},{16,20,2,20,21,20404},{20,20,0,20,20,20564},{30,20,0,20,30,20764},{20,20,0,20,20,21064},{20,28,0,20,21,21264},
  {16,20,1,20,18,21544},{11,33,1,11,13,21704},{4,37,4,8,11,21902},{11,33,1,11,13,21976},{18,6,2,23,22,22174},{19,31,1,9,23,22228},
  {19,31,1,9,21,22538},{9,32,0,8,9,22848},{20,31,1,9,23,23008},{16,32,2,8,21,23318},{16,28,2,12,21,23574},{17,29,3,11,22,23798},
  {27,40,1,0,28,24059},{19,40,3,0,24,24619},{9,40,0,0,10,25019},{28,40,2,0,32,25219},{22,40,3,0,27,25779},{22,37,3,3,27,26219},
  {22,38,3,2,28,26626},{19,31,1,9,23,27044},{19,31,1,9,21,27354},{9,31,0,9,9,27664},{20,31,1,9,23,27819},{16,31,2,9,21,28129},
  {19,31,1,9,23,28377},{19,31,1,9,21,28687},{15,31,-3,9,9,28997},{20,31,1,9,23,29245},{16,31,2,9,21,29555},{19,29,1,11,23,29803},
  {20,29,1,11,23,30093},{18,29,1,20,20,30383},{25,38,2,11,28,30644},{16,28,2,20,20,31138},{5,28,3,20,11,31362},{12,13,2,11,16,31446},
  {5,5,0,23,5,31524},
};
// ---- FlexOS_Ultra_Font.h: FBM
static const uint8_t FBM[31539] = {
  15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,
  176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,
  255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,44,252,32,207,255,
  192,255,255,240,207,255,192,44,252,32,63,255,160,10,255,244,47,255,144,9,
  255,242,15,255,128,8,255,241,14,255,96,6,255,240,13,255,80,5,255,208,
  12,255,64,4,255,192,11,255,48,3,255,176,9,255,16,1,255,144,8,255,
  0,0,255,128,7,254,0,0,239,112,0,0,0,95,250,0,0,13,255,32,
  0,0,0,0,127,248,0,0,31,255,0,0,0,0,0,159,246,0,0,63,
  253,0,0,0,0,0,207,244,0,0,95,251,0,0,0,0,0,239,242,0,
  0,127,249,0,0,0,0,1,255,240,0,0,159,246,0,0,0,0,3,255,
  192,0,0,191,244,0,0,0,0,5,255,160,0,0,223,242,0,0,9,153,
  156,255,217,153,153,255,249,153,144,15,255,255,255,255,255,255,255,255,255,240,
  15,255,255,255,255,255,255,255,255,255,240,15,255,255,255,255,255,255,255,255,
  255,240,0,0,31,254,0,0,9,255,96,0,0,0,0,63,252,0,0,11,
  255,64,0,0,0,0,95,250,0,0,14,255,32,0,0,0,0,143,248,0,
  0,31,255,0,0,0,0,0,175,246,0,0,63,253,0,0,0,255,255,255,
  255,255,255,255,255,255,255,0,255,255,255,255,255,255,255,255,255,255,0,255,
  255,255,255,255,255,255,255,255,255,0,153,155,255,233,153,153,239,250,153,153,
  0,0,6,255,160,0,0,239,242,0,0,0,0,8,255,128,0,1,255,240,
  0,0,0,0,10,255,96,0,3,255,192,0,0,0,0,12,255,48,0,5,
  255,160,0,0,0,0,14,255,16,0,7,255,128,0,0,0,0,31,254,0,
  0,10,255,96,0,0,0,0,79,252,0,0,12,255,64,0,0,0,0,111,
  250,0,0,14,255,32,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,57,207,255,200,48,0,0,0,
  0,43,255,255,255,255,250,16,0,0,2,239,255,255,255,255,255,211,0,0,
  13,255,255,222,255,239,255,254,32,0,111,255,228,13,255,6,239,254,48,0,
  191,255,64,13,255,0,61,227,0,0,239,253,0,13,255,0,2,48,0,0,
  255,251,0,13,255,0,0,0,0,0,239,253,0,13,255,0,0,0,0,0,
  207,255,64,13,255,0,0,0,0,0,143,255,228,13,255,0,0,0,0,0,
  30,255,255,174,255,0,0,0,0,0,5,255,255,255,255,16,0,0,0,0,
  0,77,255,255,255,233,48,0,0,0,0,1,125,255,255,255,249,16,0,0,
  0,0,0,94,255,255,255,193,0,0,0,0,0,13,255,239,255,251,0,0,
  0,0,0,13,255,24,255,255,64,0,0,0,0,13,255,0,143,255,160,0,
  0,0,0,13,255,0,30,255,208,0,0,0,0,13,255,0,12,255,240,0,
  18,0,0,13,255,0,11,255,240,1,204,16,0,13,255,0,14,255,208,29,
  255,193,0,13,255,0,127,255,160,62,255,254,80,13,255,24,255,255,80,4,
  255,255,254,190,255,255,255,251,0,0,78,255,255,255,255,255,255,193,0,0,
  2,191,255,255,255,255,249,16,0,0,0,3,140,239,255,183,32,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,13,255,0,0,0,0,0,
  0,0,0,13,255,0,0,0,0,0,0,0,0,6,136,0,0,0,0,0,
  41,223,217,32,0,0,0,6,255,243,3,239,255,255,227,0,0,0,30,255,
  144,30,255,254,255,254,16,0,0,159,254,16,143,253,32,61,255,128,0,3,
  255,246,0,223,244,0,4,255,208,0,12,255,192,0,255,240,0,0,255,240,
  0,111,255,48,0,255,240,0,0,255,224,1,239,249,0,0,207,244,0,5,
  255,192,9,255,225,0,0,127,253,48,78,255,112,63,255,96,0,0,29,255,
  255,255,253,16,207,252,0,0,0,2,223,255,255,210,6,255,243,0,0,0,
  0,24,206,200,16,30,255,144,0,0,0,0,0,0,0,0,159,254,16,0,
  0,0,0,0,0,0,3,255,246,0,0,0,0,0,0,0,0,12,255,176,
  0,0,0,0,0,0,0,0,111,255,48,0,0,0,0,0,0,0,1,239,
  249,0,0,0,0,0,0,0,0,9,255,225,1,124,236,129,0,0,0,0,
  63,255,80,45,255,255,253,32,0,0,0,207,251,0,223,255,255,255,209,0,
  0,6,255,243,7,255,228,4,223,247,0,0,30,255,128,12,255,80,0,79,
  252,0,0,159,254,16,14,255,16,0,15,255,0,3,255,245,0,14,255,16,
  0,15,255,0,12,255,176,0,12,255,80,0,79,252,0,111,255,48,0,7,
  255,211,2,223,248,1,239,248,0,0,1,223,255,239,255,225,9,255,209,0,
  0,0,62,255,255,254,48,63,255,80,0,0,0,1,157,253,146,0,0,0,
  0,5,173,255,217,48,0,0,0,0,0,0,2,207,255,255,255,250,16,0,
  0,0,0,0,46,255,255,255,255,255,193,0,0,0,0,0,207,255,252,154,
  223,255,252,0,0,0,0,6,255,254,64,0,5,239,251,0,0,0,0,11,
  255,244,0,0,0,62,144,0,0,0,0,14,255,208,0,0,0,3,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,14,255,192,0,0,
  0,0,0,0,0,0,0,12,255,241,0,0,0,0,0,0,0,0,0,7,
  255,249,0,0,0,0,0,0,0,0,0,1,239,255,80,0,0,0,0,0,
  0,0,0,0,111,255,227,0,0,0,0,0,0,0,0,4,223,255,254,32,
  0,0,0,0,0,0,0,111,255,255,255,209,0,0,0,0,0,0,5,255,
  254,108,255,252,16,0,0,0,0,0,30,255,227,1,223,255,176,0,0,0,
  0,0,127,255,96,0,45,255,249,0,0,0,0,0,191,254,16,0,3,239,
  255,128,0,0,0,0,239,252,0,0,0,63,255,246,0,0,0,0,255,251,
  0,0,0,4,255,255,80,0,0,0,239,253,0,0,0,0,111,255,227,0,
  0,0,207,255,48,0,0,0,7,255,254,32,0,0,127,255,193,0,0,0,
  0,191,255,209,0,0,30,255,252,64,0,0,42,255,255,252,16,0,6,255,
  255,253,169,172,255,255,255,255,176,0,0,143,255,255,255,255,255,255,124,255,
  250,0,0,5,223,255,255,255,255,179,1,207,255,128,0,0,5,173,255,236,
  131,0,0,45,255,246,63,255,160,47,255,144,15,255,128,14,255,96,13,255,
  80,12,255,64,11,255,48,9,255,16,8,255,0,7,254,0,0,0,0,3,
  0,0,0,0,127,128,0,0,7,255,246,0,0,79,255,160,0,1,239,252,
  0,0,10,255,226,0,0,63,255,112,0,0,175,254,16,0,2,255,248,0,
  0,8,255,242,0,0,13,255,192,0,0,47,255,128,0,0,111,255,64,0,
  0,159,255,16,0,0,191,254,0,0,0,223,253,0,0,0,239,252,0,0,
  0,255,251,0,0,0,255,251,0,0,0,239,251,0,0,0,223,252,0,0,
  0,207,254,0,0,0,175,255,16,0,0,127,255,48,0,0,63,255,96,0,
  0,14,255,160,0,0,10,255,225,0,0,4,255,246,0,0,0,223,252,0,
  0,0,127,255,80,0,0,13,255,208,0,0,5,255,248,0,0,0,175,255,
  80,0,0,28,255,244,0,0,1,223,193,0,0,0,24,16,0,48,0,0,
  0,27,228,0,0,0,159,255,64,0,0,28,255,226,0,0,2,239,251,0,
  0,0,95,255,96,0,0,11,255,208,0,0,3,255,246,0,0,0,207,253,
  0,0,0,111,255,48,0,0,31,255,128,0,0,12,255,208,0,0,8,255,
  241,0,0,6,255,244,0,0,3,255,247,0,0,2,255,249,0,0,1,255,
  250,0,0,0,255,250,0,0,0,255,251,0,0,0,255,250,0,0,1,255,
  249,0,0,3,255,248,0,0,5,255,245,0,0,7,255,243,0,0,11,255,
  224,0,0,14,255,160,0,0,79,255,96,0,0,175,254,16,0,1,255,249,
  0,0,8,255,242,0,0,47,255,144,0,0,207,254,32,0,9,255,246,0,
  0,143,255,144,0,0,62,250,0,0,0,2,128,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,10,179,0,0,0,60,16,0,30,255,96,0,
  3,239,160,0,111,254,16,0,10,255,247,0,191,245,0,0,0,143,255,66,
  255,176,0,0,0,5,255,233,255,32,0,0,0,0,61,255,247,0,0,0,
  0,0,41,255,255,255,255,247,0,91,255,255,255,255,255,245,142,255,255,143,
  241,89,223,243,159,255,195,15,244,0,2,97,47,231,0,15,248,0,0,0,
  6,32,0,15,252,0,0,0,0,0,0,15,255,16,0,0,0,0,0,15,
  255,80,0,0,0,0,0,10,134,32,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,
  0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,
  15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,153,153,153,159,255,
  217,153,153,144,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,
  255,240,255,255,255,255,255,255,255,255,240,0,0,0,15,255,176,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,
  255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,8,136,80,
  0,0,0,2,207,178,11,255,251,15,255,255,12,255,253,3,223,249,0,143,
  242,1,239,144,8,255,32,30,249,0,43,242,0,0,32,0,153,153,153,153,
  153,153,144,255,255,255,255,255,255,240,255,255,255,255,255,255,240,255,255,255,
  255,255,255,240,44,252,32,207,255,192,255,255,240,191,255,176,44,252,32,0,
  0,0,0,0,7,255,208,0,0,0,0,0,13,255,128,0,0,0,0,0,
  63,255,48,0,0,0,0,0,143,253,0,0,0,0,0,0,223,247,0,0,
  0,0,0,3,255,242,0,0,0,0,0,9,255,192,0,0,0,0,0,14,
  255,112,0,0,0,0,0,79,255,32,0,0,0,0,0,159,251,0,0,0,
  0,0,0,239,246,0,0,0,0,0,5,255,241,0,0,0,0,0,10,255,
  176,0,0,0,0,0,30,255,96,0,0,0,0,0,95,255,16,0,0,0,
  0,0,191,250,0,0,0,0,0,1,255,245,0,0,0,0,0,6,255,225,
  0,0,0,0,0,11,255,160,0,0,0,0,0,47,255,64,0,0,0,0,
  0,127,254,0,0,0,0,0,0,207,249,0,0,0,0,0,2,255,244,0,
  0,0,0,0,7,255,208,0,0,0,0,0,12,255,128,0,0,0,0,0,
  63,255,48,0,0,0,0,0,143,253,0,0,0,0,0,0,223,248,0,0,
  0,0,0,3,255,242,0,0,0,0,0,9,255,192,0,0,0,0,0,14,
  255,112,0,0,0,0,0,79,255,32,0,0,0,0,0,0,0,0,4,157,
  239,236,130,0,0,0,0,0,0,3,207,255,255,255,255,145,0,0,0,0,
  0,95,255,255,255,255,255,253,32,0,0,0,5,255,255,253,169,190,255,255,
  226,0,0,0,63,255,251,48,0,0,94,255,253,0,0,0,207,255,144,0,
  0,0,1,223,255,128,0,5,255,251,0,0,0,0,0,46,255,241,0,12,
  255,242,0,0,0,0,0,7,255,248,0,47,255,160,0,0,0,0,0,1,
  239,253,0,111,255,80,0,0,0,0,0,0,175,255,32,175,255,16,0,0,
  0,0,0,0,111,255,96,207,254,0,0,0,0,0,0,0,63,255,128,239,
  252,0,0,0,0,0,0,0,31,255,144,255,251,0,0,0,0,0,0,0,
  15,255,160,255,251,0,0,0,0,0,0,0,15,255,176,255,251,0,0,0,
  0,0,0,0,15,255,160,239,252,0,0,0,0,0,0,0,31,255,144,207,
  254,0,0,0,0,0,0,0,63,255,128,159,255,32,0,0,0,0,0,0,
  111,255,80,111,255,80,0,0,0,0,0,0,175,255,32,47,255,160,0,0,
  0,0,0,1,239,253,0,11,255,242,0,0,0,0,0,7,255,247,0,4,
  255,251,0,0,0,0,0,46,255,225,0,0,191,255,144,0,0,0,1,223,
  255,128,0,0,46,255,251,48,0,0,93,255,253,0,0,0,4,255,255,253,
  169,174,255,255,226,0,0,0,0,78,255,255,255,255,255,253,48,0,0,0,
  0,2,191,255,255,255,255,145,0,0,0,0,0,0,3,156,239,236,130,0,
  0,0,0,143,255,255,255,251,143,255,255,255,251,143,255,255,255,251,73,153,
  153,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,0,255,251,0,0,
  0,255,251,0,0,0,255,251,0,0,4,156,239,237,147,0,0,0,0,2,
  191,255,255,255,255,161,0,0,0,78,255,255,255,255,255,253,16,0,4,255,
  255,253,169,174,255,255,160,0,46,255,252,48,0,1,143,255,244,0,143,255,
  160,0,0,0,8,255,249,0,26,251,0,0,0,0,1,239,253,0,0,130,
  0,0,0,0,0,207,255,0,0,0,0,0,0,0,0,191,255,0,0,0,
  0,0,0,0,0,223,254,0,0,0,0,0,0,0,3,255,252,0,0,0,
  0,0,0,0,10,255,248,0,0,0,0,0,0,0,95,255,242,0,0,0,
  0,0,0,2,239,255,144,0,0,0,0,0,0,29,255,253,16,0,0,0,
  0,0,1,207,255,227,0,0,0,0,0,0,11,255,255,64,0,0,0,0,
  0,0,175,255,246,0,0,0,0,0,0,9,255,255,112,0,0,0,0,0,
  0,143,255,248,0,0,0,0,0,0,7,255,255,128,0,0,0,0,0,0,
  111,255,249,0,0,0,0,0,0,5,255,255,160,0,0,0,0,0,0,79,
  255,250,0,0,0,0,0,0,3,239,255,176,0,0,0,0,0,0,46,255,
  255,169,153,153,153,153,153,144,223,255,255,255,255,255,255,255,255,240,255,255,
  255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,208,0,153,153,153,153,153,158,255,254,32,0,0,
  0,0,0,0,143,255,243,0,0,0,0,0,0,6,255,255,80,0,0,0,
  0,0,0,79,255,247,0,0,0,0,0,0,3,239,255,144,0,0,0,0,
  0,0,29,255,250,0,0,0,0,0,0,1,207,255,193,0,0,0,0,0,
  0,11,255,253,16,0,0,0,0,0,0,143,255,255,218,64,0,0,0,0,
  0,207,255,255,255,250,16,0,0,0,0,207,221,239,255,255,192,0,0,0,
  0,32,0,3,175,255,248,0,0,0,0,0,0,0,5,255,255,32,0,0,
  0,0,0,0,0,143,255,128,0,0,0,0,0,0,0,31,255,192,0,0,
  0,0,0,0,0,12,255,224,0,0,0,0,0,0,0,11,255,240,0,0,
  0,0,0,0,0,12,255,224,0,0,0,0,0,0,0,31,255,208,0,37,
  0,0,0,0,0,111,255,144,1,223,64,0,0,0,3,239,255,64,29,255,
  249,32,0,0,110,255,251,0,12,255,255,252,169,190,255,255,226,0,1,191,
  255,255,255,255,255,254,48,0,0,7,239,255,255,255,255,178,0,0,0,0,
  22,173,255,236,131,0,0,0,0,0,0,0,0,175,255,128,0,0,0,0,
  0,0,0,3,255,254,16,0,0,0,0,0,0,0,11,255,247,0,0,0,
  0,0,0,0,0,79,255,208,0,0,0,0,0,0,0,0,207,255,96,0,
  0,0,0,0,0,0,5,255,252,0,0,0,0,0,0,0,0,13,255,244,
  0,0,0,0,0,0,0,0,111,255,176,0,0,0,0,0,0,0,0,223,
  255,48,0,0,0,0,0,0,0,6,255,250,0,0,34,34,0,0,0,0,
  30,255,242,0,0,255,251,0,0,0,0,127,255,144,0,0,255,251,0,0,
  0,1,239,255,32,0,0,255,251,0,0,0,8,255,248,0,0,0,255,251,
  0,0,0,47,255,225,0,0,0,255,251,0,0,0,159,255,112,0,0,0,
  255,251,0,0,2,255,253,16,0,0,0,255,251,0,0,10,255,246,0,0,
  0,0,255,251,0,0,63,255,255,255,255,255,255,255,255,255,248,143,255,255,
  255,255,255,255,255,255,255,248,143,255,255,255,255,255,255,255,255,255,248,73,
  153,153,153,153,153,153,255,253,153,148,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,0,0,255,251,0,0,0,5,255,255,255,255,255,255,255,80,0,7,255,
  255,255,255,255,255,255,80,0,8,255,255,255,255,255,255,255,80,0,9,255,
  233,153,153,153,153,153,48,0,11,255,160,0,0,0,0,0,0,0,12,255,
  144,0,0,0,0,0,0,0,14,255,112,0,0,0,0,0,0,0,15,255,
  96,0,0,0,0,0,0,0,31,255,64,0,0,0,0,0,0,0,63,255,
  48,0,0,0,0,0,0,0,79,255,39,206,255,218,64,0,0,0,111,255,
  239,255,255,255,252,32,0,0,127,255,255,255,255,255,255,227,0,0,79,255,
  253,185,155,255,255,253,16,0,5,251,48,0,0,24,255,255,128,0,0,48,
  0,0,0,0,111,255,225,0,0,0,0,0,0,0,10,255,245,0,0,0,
  0,0,0,0,4,255,248,0,0,0,0,0,0,0,1,255,250,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,1,255,250,0,0,0,
  0,0,0,0,5,255,248,0,22,0,0,0,0,0,12,255,244,1,191,112,
  0,0,0,0,143,255,208,11,255,250,48,0,0,42,255,255,80,10,255,255,
  253,169,172,255,255,249,0,0,175,255,255,255,255,255,255,144,0,0,5,223,
  255,255,255,255,229,0,0,0,0,5,173,239,253,165,16,0,0,0,0,0,
  0,0,207,255,96,0,0,0,0,0,0,8,255,251,0,0,0,0,0,0,
  0,63,255,226,0,0,0,0,0,0,1,223,255,96,0,0,0,0,0,0,
  9,255,250,0,0,0,0,0,0,0,79,255,225,0,0,0,0,0,0,1,
  223,255,80,0,0,0,0,0,0,9,255,249,0,0,0,0,0,0,0,95,
  255,209,0,0,0,0,0,0,1,239,255,152,152,81,0,0,0,0,10,255,
  255,255,255,254,112,0,0,0,95,255,255,255,255,255,251,16,0,1,239,255,
  255,255,255,255,255,192,0,8,255,255,213,16,38,223,255,248,0,30,255,250,
  0,0,0,10,255,255,32,111,255,192,0,0,0,0,207,255,112,175,255,64,
  0,0,0,0,79,255,176,223,254,0,0,0,0,0,14,255,224,255,251,0,
  0,0,0,0,11,255,240,255,251,0,0,0,0,0,11,255,240,239,253,0,
  0,0,0,0,13,255,208,191,255,32,0,0,0,0,47,255,160,127,255,144,
  0,0,0,0,159,255,96,47,255,245,0,0,0,5,255,254,16,8,255,255,
  112,0,1,127,255,246,0,0,207,255,254,169,174,255,255,160,0,0,28,255,
  255,255,255,255,251,16,0,0,1,159,255,255,255,255,112,0,0,0,0,2,
  140,239,236,113,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,255,255,255,255,255,254,153,153,153,153,153,153,
  154,255,249,0,0,0,0,0,0,6,255,243,0,0,0,0,0,0,12,255,
  208,0,0,0,0,0,0,63,255,112,0,0,0,0,0,0,143,255,32,0,
  0,0,0,0,0,239,251,0,0,0,0,0,0,5,255,245,0,0,0,0,
  0,0,11,255,225,0,0,0,0,0,0,31,255,144,0,0,0,0,0,0,
  127,255,64,0,0,0,0,0,0,223,253,0,0,0,0,0,0,3,255,248,
  0,0,0,0,0,0,9,255,242,0,0,0,0,0,0,30,255,192,0,0,
  0,0,0,0,111,255,96,0,0,0,0,0,0,191,254,16,0,0,0,0,
  0,2,255,250,0,0,0,0,0,0,8,255,244,0,0,0,0,0,0,13,
  255,208,0,0,0,0,0,0,79,255,128,0,0,0,0,0,0,175,255,32,
  0,0,0,0,0,1,255,252,0,0,0,0,0,0,6,255,246,0,0,0,
  0,0,0,12,255,241,0,0,0,0,0,0,63,255,160,0,0,0,0,0,
  0,143,255,64,0,0,0,0,0,0,3,140,239,236,131,0,0,0,0,1,
  159,255,255,255,255,145,0,0,0,28,255,255,255,255,255,252,16,0,0,175,
  255,254,169,174,255,255,160,0,3,255,255,129,0,1,143,255,243,0,8,255,
  249,0,0,0,9,255,248,0,10,255,242,0,0,0,2,255,250,0,11,255,
  240,0,0,0,0,255,251,0,9,255,242,0,0,0,2,255,249,0,5,255,
  249,0,0,0,9,255,245,0,0,223,255,129,0,1,143,255,208,0,0,63,
  255,254,169,174,255,255,48,0,0,4,223,255,255,255,255,228,0,0,0,4,
  223,255,255,255,255,195,0,0,0,127,255,255,255,255,255,255,80,0,6,255,
  255,164,16,37,207,255,243,0,30,255,245,0,0,0,9,255,252,0,127,255,
  112,0,0,0,0,191,255,48,207,254,16,0,0,0,0,79,255,112,239,252,
  0,0,0,0,0,31,255,160,255,251,0,0,0,0,0,15,255,176,239,253,
  0,0,0,0,0,63,255,160,207,255,64,0,0,0,0,143,255,112,127,255,
  209,0,0,0,3,255,255,48,30,255,253,64,0,0,110,255,251,0,6,255,
  255,253,169,174,255,255,226,0,0,143,255,255,255,255,255,254,64,0,0,5,
  223,255,255,255,255,178,0,0,0,0,5,173,255,236,148,0,0,0,0,0,
  1,107,239,253,165,0,0,0,0,0,94,255,255,255,255,212,0,0,0,9,
  255,255,255,255,255,255,112,0,0,159,255,255,185,172,255,255,247,0,5,255,
  255,145,0,0,59,255,255,48,13,255,246,0,0,0,0,175,255,176,95,255,
  160,0,0,0,0,13,255,243,175,255,32,0,0,0,0,6,255,247,223,253,
  0,0,0,0,0,2,255,250,255,251,0,0,0,0,0,0,255,251,255,252,
  0,0,0,0,0,1,255,250,239,254,0,0,0,0,0,4,255,249,191,255,
  80,0,0,0,0,9,255,246,127,255,209,0,0,0,0,63,255,241,30,255,
  252,16,0,0,3,239,255,176,7,255,255,231,48,19,159,255,255,48,0,175,
  255,255,255,255,255,255,250,0,0,10,255,255,255,255,255,255,225,0,0,0,
  93,255,255,255,255,255,64,0,0,0,0,71,153,126,255,249,0,0,0,0,
  0,0,0,143,255,193,0,0,0,0,0,0,4,255,255,48,0,0,0,0,
  0,0,30,255,247,0,0,0,0,0,0,0,191,255,176,0,0,0,0,0,
  0,6,255,254,16,0,0,0,0,0,0,63,255,245,0,0,0,0,0,0,
  1,223,255,144,0,0,0,0,0,0,9,255,253,16,0,0,0,0,0,0,
  95,255,243,0,0,0,0,0,44,252,32,207,255,192,255,255,240,207,255,192,
  44,252,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,44,252,32,207,255,192,255,255,240,207,255,192,44,
  252,32,2,207,194,12,255,252,15,255,255,12,255,252,2,207,194,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,2,191,178,11,255,251,15,255,255,12,255,253,3,223,249,0,111,242,0,
  223,144,7,255,32,30,250,0,26,242,0,0,32,0,0,0,0,0,0,0,
  0,5,192,0,0,0,0,0,0,6,223,240,0,0,0,0,0,23,239,255,
  240,0,0,0,0,40,239,255,255,224,0,0,0,57,255,255,255,214,16,0,
  0,75,255,255,255,180,0,0,0,92,255,255,255,147,0,0,0,109,255,255,
  253,113,0,0,0,0,255,255,252,80,0,0,0,0,0,255,255,230,16,0,
  0,0,0,0,191,255,255,232,32,0,0,0,0,3,175,255,255,250,48,0,
  0,0,0,2,159,255,255,252,80,0,0,0,0,1,142,255,255,253,113,0,
  0,0,0,1,125,255,255,254,144,0,0,0,0,0,92,255,255,240,0,0,
  0,0,0,0,75,255,240,0,0,0,0,0,0,0,58,240,0,0,0,0,
  0,0,0,0,32,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,
  255,255,240,255,255,255,255,255,255,255,255,240,153,153,153,153,153,153,153,153,
  144,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,153,153,153,153,153,153,153,153,144,255,255,255,255,255,
  255,255,255,240,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,
  255,240,197,0,0,0,0,0,0,0,0,255,214,0,0,0,0,0,0,0,
  255,255,231,16,0,0,0,0,0,239,255,255,232,32,0,0,0,0,22,223,
  255,255,249,48,0,0,0,0,4,191,255,255,251,64,0,0,0,0,3,159,
  255,255,252,80,0,0,0,0,1,125,255,255,253,96,0,0,0,0,0,92,
  255,255,240,0,0,0,0,0,22,239,255,240,0,0,0,0,40,239,255,255,
  176,0,0,0,58,255,255,255,163,0,0,0,92,255,255,255,146,0,0,1,
  125,255,255,254,129,0,0,0,158,255,255,253,113,0,0,0,0,255,255,252,
  80,0,0,0,0,0,255,251,64,0,0,0,0,0,0,250,48,0,0,0,
  0,0,0,0,32,0,0,0,0,0,0,0,0,0,0,4,157,239,236,130,
  0,0,0,2,191,255,255,255,255,112,0,0,78,255,255,255,255,255,250,0,
  2,239,255,253,169,191,255,255,112,11,255,252,48,0,2,191,255,225,10,255,
  193,0,0,0,12,255,246,0,142,32,0,0,0,4,255,249,0,2,0,0,
  0,0,1,255,251,0,0,0,0,0,0,0,255,250,0,0,0,0,0,0,
  3,255,249,0,0,0,0,0,0,8,255,245,0,0,0,0,0,0,79,255,
  225,0,0,0,0,0,56,255,255,112,0,0,0,0,255,255,255,250,0,0,
  0,0,0,255,255,255,144,0,0,0,0,0,255,255,180,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,2,207,194,0,0,0,0,0,0,12,255,252,0,0,0,0,0,0,15,
  255,255,0,0,0,0,0,0,12,255,252,0,0,0,0,0,0,2,207,194,
  0,0,0,0,0,0,0,22,156,222,219,132,0,0,0,0,0,0,0,41,
  255,255,255,255,255,214,0,0,0,0,0,6,239,255,255,255,255,255,255,194,
  0,0,0,0,159,255,252,115,16,18,90,255,253,32,0,0,9,255,253,64,
  0,0,0,0,44,255,209,0,0,111,255,193,0,0,0,0,0,0,175,251,
  0,2,239,253,16,0,0,0,0,0,0,12,255,80,9,255,244,0,3,156,
  203,114,153,96,4,255,192,31,255,176,0,143,255,255,254,255,160,0,207,243,
  111,255,80,7,255,255,255,255,255,160,0,127,247,175,255,16,47,255,146,2,
  159,255,160,0,79,251,223,253,0,143,249,0,0,10,255,160,0,31,253,239,
  251,0,207,242,0,0,4,255,160,0,15,254,255,251,0,207,240,0,0,2,
  255,160,0,15,254,239,251,0,191,242,0,0,4,255,160,0,31,254,223,253,
  0,127,249,0,0,10,255,160,0,47,252,191,255,0,30,255,146,2,175,255,
  160,0,95,250,127,255,64,5,255,255,255,255,255,255,255,255,245,47,255,144,
  0,78,255,255,251,255,255,255,255,224,11,255,242,0,1,88,151,48,68,68,
  68,68,32,3,255,251,0,0,0,0,0,0,0,0,0,0,0,159,255,144,
  0,0,0,0,0,1,0,0,0,0,12,255,251,32,0,0,0,0,61,144,
  0,0,0,1,207,255,233,65,0,1,90,255,248,0,0,0,0,25,255,255,
  255,237,239,255,255,211,0,0,0,0,0,76,255,255,255,255,255,231,0,0,
  0,0,0,0,0,56,189,255,236,149,0,0,0,0,0,0,0,0,0,1,
  255,242,0,0,0,0,0,0,0,0,0,0,0,7,255,248,0,0,0,0,
  0,0,0,0,0,0,0,13,255,253,0,0,0,0,0,0,0,0,0,0,
  0,79,255,255,80,0,0,0,0,0,0,0,0,0,0,175,255,255,176,0,
  0,0,0,0,0,0,0,0,1,255,255,255,242,0,0,0,0,0,0,0,
  0,0,7,255,252,255,247,0,0,0,0,0,0,0,0,0,13,255,226,255,
  253,0,0,0,0,0,0,0,0,0,79,255,160,175,255,64,0,0,0,0,
  0,0,0,0,175,255,64,79,255,160,0,0,0,0,0,0,0,1,255,253,
  0,13,255,241,0,0,0,0,0,0,0,7,255,247,0,7,255,247,0,0,
  0,0,0,0,0,13,255,241,0,2,255,253,0,0,0,0,0,0,0,79,
  255,160,0,0,191,255,64,0,0,0,0,0,0,175,255,64,0,0,95,255,
  160,0,0,0,0,0,1,255,253,0,0,0,14,255,241,0,0,0,0,0,
  7,255,247,0,0,0,8,255,247,0,0,0,0,0,13,255,242,0,0,0,
  2,255,253,0,0,0,0,0,63,255,176,0,0,0,0,191,255,64,0,0,
  0,0,159,255,255,255,255,255,255,255,255,160,0,0,0,1,239,255,255,255,
  255,255,255,255,255,241,0,0,0,6,255,255,255,255,255,255,255,255,255,247,
  0,0,0,12,255,249,153,153,153,153,153,154,255,252,0,0,0,63,255,176,
  0,0,0,0,0,0,207,255,48,0,0,159,255,80,0,0,0,0,0,0,
  111,255,144,0,1,239,254,0,0,0,0,0,0,0,31,255,225,0,6,255,
  249,0,0,0,0,0,0,0,10,255,246,0,12,255,243,0,0,0,0,0,
  0,0,4,255,252,0,63,255,192,0,0,0,0,0,0,0,0,223,255,48,
  255,255,255,255,255,254,201,48,0,0,255,255,255,255,255,255,255,250,16,0,
  255,255,255,255,255,255,255,255,193,0,255,253,153,153,153,154,239,255,249,0,
  255,251,0,0,0,0,8,255,255,32,255,251,0,0,0,0,0,159,255,112,
  255,251,0,0,0,0,0,47,255,160,255,251,0,0,0,0,0,15,255,176,
  255,251,0,0,0,0,0,31,255,160,255,251,0,0,0,0,0,79,255,112,
  255,251,0,0,0,0,1,207,255,32,255,251,0,0,0,1,92,255,248,0,
  255,255,255,255,255,255,255,255,176,0,255,255,255,255,255,255,255,252,0,0,
  255,255,255,255,255,255,255,255,177,0,255,253,153,153,153,153,207,255,252,16,
  255,251,0,0,0,0,3,207,255,144,255,251,0,0,0,0,0,29,255,242,
  255,251,0,0,0,0,0,6,255,246,255,251,0,0,0,0,0,1,255,249,
  255,251,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,2,255,250,
  255,251,0,0,0,0,0,6,255,248,255,251,0,0,0,0,0,30,255,244,
  255,251,0,0,0,0,3,223,255,208,255,253,153,153,153,154,207,255,255,64,
  255,255,255,255,255,255,255,255,246,0,255,255,255,255,255,255,255,253,64,0,
  255,255,255,255,255,255,218,96,0,0,0,0,0,0,5,156,239,254,201,80,
  0,0,0,0,0,0,7,239,255,255,255,255,253,96,0,0,0,0,3,223,
  255,255,255,255,255,255,252,32,0,0,0,95,255,255,253,169,155,239,255,255,
  228,0,0,5,255,255,232,32,0,0,3,175,255,249,0,0,46,255,251,32,
  0,0,0,0,4,223,160,0,0,207,255,160,0,0,0,0,0,0,40,0,
  0,6,255,252,16,0,0,0,0,0,0,0,0,0,12,255,243,0,0,0,
  0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,0,0,0,0,0,
  143,255,80,0,0,0,0,0,0,0,0,0,0,191,255,16,0,0,0,0,
  0,0,0,0,0,0,223,253,0,0,0,0,0,0,0,0,0,0,0,255,
  252,0,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,0,0,0,0,239,252,0,0,0,0,0,0,0,0,0,0,0,223,253,
  0,0,0,0,0,0,0,0,0,0,0,191,255,16,0,0,0,0,0,0,
  0,0,0,0,143,255,80,0,0,0,0,0,0,0,0,0,0,63,255,176,
  0,0,0,0,0,0,0,0,0,0,12,255,243,0,0,0,0,0,0,0,
  0,0,0,5,255,252,16,0,0,0,0,0,0,0,0,0,0,207,255,160,
  0,0,0,0,0,0,25,16,0,0,46,255,251,32,0,0,0,0,2,207,
  193,0,0,4,255,255,232,32,0,0,3,159,255,252,0,0,0,95,255,255,
  253,169,155,223,255,255,246,0,0,0,3,223,255,255,255,255,255,255,253,64,
  0,0,0,0,7,239,255,255,255,255,254,113,0,0,0,0,0,0,5,156,
  239,254,201,80,0,0,0,255,255,255,255,255,254,201,80,0,0,0,0,255,
  255,255,255,255,255,255,254,113,0,0,0,255,255,255,255,255,255,255,255,253,
  64,0,0,255,253,153,153,153,154,223,255,255,246,0,0,255,251,0,0,0,
  0,2,142,255,255,80,0,255,251,0,0,0,0,0,2,191,255,243,0,255,
  251,0,0,0,0,0,0,10,255,252,0,255,251,0,0,0,0,0,0,1,
  207,255,96,255,251,0,0,0,0,0,0,0,63,255,208,255,251,0,0,0,
  0,0,0,0,11,255,243,255,251,0,0,0,0,0,0,0,5,255,248,255,
  251,0,0,0,0,0,0,0,1,255,251,255,251,0,0,0,0,0,0,0,
  0,223,253,255,251,0,0,0,0,0,0,0,0,191,254,255,251,0,0,0,
  0,0,0,0,0,191,255,255,251,0,0,0,0,0,0,0,0,191,255,255,
  251,0,0,0,0,0,0,0,0,223,253,255,251,0,0,0,0,0,0,0,
  1,255,251,255,251,0,0,0,0,0,0,0,5,255,248,255,251,0,0,0,
  0,0,0,0,11,255,243,255,251,0,0,0,0,0,0,0,63,255,208,255,
  251,0,0,0,0,0,0,1,207,255,96,255,251,0,0,0,0,0,0,10,
  255,252,0,255,251,0,0,0,0,0,2,191,255,243,0,255,251,0,0,0,
  0,2,142,255,255,96,0,255,253,153,153,153,154,223,255,255,246,0,0,255,
  255,255,255,255,255,255,255,253,64,0,0,255,255,255,255,255,255,255,254,129,
  0,0,0,255,255,255,255,255,254,201,81,0,0,0,0,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,255,255,255,255,
  255,255,255,255,128,255,253,153,153,153,153,153,153,153,64,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,253,153,153,153,
  153,153,153,144,0,255,255,255,255,255,255,255,255,240,0,255,255,255,255,255,
  255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,0,255,253,153,153,153,153,153,153,153,64,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,255,255,255,255,
  255,255,255,255,128,255,255,255,255,255,255,255,255,248,255,255,255,255,255,255,
  255,255,248,255,255,255,255,255,255,255,255,248,255,253,153,153,153,153,153,153,
  148,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,240,
  255,255,255,255,255,255,255,255,240,255,253,153,153,153,153,153,153,144,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,0,0,0,0,4,140,239,254,201,81,0,0,0,0,
  0,0,0,6,223,255,255,255,255,254,129,0,0,0,0,0,2,207,255,255,
  255,255,255,255,254,80,0,0,0,0,78,255,255,253,185,154,223,255,255,247,
  0,0,0,4,255,255,248,32,0,0,2,142,255,255,96,0,0,46,255,252,
  32,0,0,0,0,1,191,255,80,0,0,191,255,176,0,0,0,0,0,0,
  9,245,0,0,5,255,252,16,0,0,0,0,0,0,0,64,0,0,12,255,
  243,0,0,0,0,0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,
  0,0,0,0,0,0,127,255,80,0,0,0,0,0,0,0,0,0,0,0,
  191,255,16,0,0,0,0,0,0,0,0,0,0,0,223,253,0,0,0,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,4,153,153,153,153,153,
  153,144,255,251,0,0,0,0,7,255,255,255,255,255,255,240,239,251,0,0,
  0,0,7,255,255,255,255,255,255,224,223,253,0,0,0,0,7,255,255,255,
  255,255,255,208,191,255,16,0,0,0,0,0,0,0,0,13,255,176,127,255,
  80,0,0,0,0,0,0,0,0,31,255,144,63,255,176,0,0,0,0,0,
  0,0,0,95,255,80,12,255,244,0,0,0,0,0,0,0,0,191,255,16,
  4,255,253,16,0,0,0,0,0,0,4,255,250,0,0,191,255,176,0,0,
  0,0,0,0,46,255,243,0,0,46,255,251,32,0,0,0,0,3,223,255,
  144,0,0,4,255,255,232,32,0,0,3,159,255,253,16,0,0,0,78,255,
  255,253,169,155,223,255,255,210,0,0,0,0,2,207,255,255,255,255,255,255,
  252,32,0,0,0,0,0,6,223,255,255,255,255,253,96,0,0,0,0,0,
  0,0,5,156,239,254,200,64,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,253,153,153,153,153,153,153,153,255,251,255,255,255,255,255,
  255,255,255,255,255,251,255,255,255,255,255,255,255,255,255,255,251,255,255,255,
  255,255,255,255,255,255,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,
  251,255,251,255,251,255,251,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,250,0,0,0,
  0,0,2,255,249,0,0,0,0,0,7,255,246,1,163,0,0,0,29,255,
  242,27,254,80,0,2,207,255,160,175,255,253,169,191,255,254,32,45,255,255,
  255,255,255,244,0,1,191,255,255,255,253,64,0,0,4,157,239,235,97,0,
  0,255,251,0,0,0,0,0,0,159,255,245,0,255,251,0,0,0,0,0,
  9,255,255,80,0,255,251,0,0,0,0,0,159,255,245,0,0,255,251,0,
  0,0,0,8,255,255,80,0,0,255,251,0,0,0,0,143,255,245,0,0,
  0,255,251,0,0,0,8,255,255,80,0,0,0,255,251,0,0,0,143,255,
  245,0,0,0,0,255,251,0,0,8,255,255,80,0,0,0,0,255,251,0,
  0,143,255,245,0,0,0,0,0,255,251,0,8,255,255,80,0,0,0,0,
  0,255,251,0,143,255,245,0,0,0,0,0,0,255,251,8,255,255,96,0,
  0,0,0,0,0,255,251,143,255,246,0,0,0,0,0,0,0,255,254,255,
  255,96,0,0,0,0,0,0,0,255,254,255,255,160,0,0,0,0,0,0,
  0,255,251,111,255,249,0,0,0,0,0,0,0,255,251,7,255,255,128,0,
  0,0,0,0,0,255,251,0,143,255,247,0,0,0,0,0,0,255,251,0,
  9,255,255,96,0,0,0,0,0,255,251,0,0,175,255,245,0,0,0,0,
  0,255,251,0,0,10,255,255,64,0,0,0,0,255,251,0,0,0,191,255,
  244,0,0,0,0,255,251,0,0,0,28,255,254,48,0,0,0,255,251,0,
  0,0,1,223,255,226,0,0,0,255,251,0,0,0,0,45,255,253,32,0,
  0,255,251,0,0,0,0,2,239,255,209,0,0,255,251,0,0,0,0,0,
  62,255,252,16,0,255,251,0,0,0,0,0,3,239,255,193,0,255,251,0,
  0,0,0,0,0,79,255,251,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,253,153,153,153,153,
  153,153,148,255,255,255,255,255,255,255,255,248,255,255,255,255,255,255,255,255,
  248,255,255,255,255,255,255,255,255,248,255,209,0,0,0,0,0,0,0,0,
  0,1,223,240,255,247,0,0,0,0,0,0,0,0,0,7,255,240,255,254,
  32,0,0,0,0,0,0,0,0,46,255,240,255,255,160,0,0,0,0,0,
  0,0,0,175,255,240,255,255,244,0,0,0,0,0,0,0,4,255,255,240,
  255,255,252,0,0,0,0,0,0,0,12,255,255,240,255,255,255,96,0,0,
  0,0,0,0,111,255,255,240,255,255,255,225,0,0,0,0,0,1,239,255,
  255,240,255,254,255,249,0,0,0,0,0,9,255,255,255,240,255,251,191,255,
  48,0,0,0,0,63,255,203,255,240,255,251,63,255,176,0,0,0,0,191,
  255,59,255,240,255,251,9,255,245,0,0,0,5,255,249,11,255,240,255,251,
  1,239,253,16,0,0,29,255,225,11,255,240,255,251,0,111,255,128,0,0,
  143,255,112,11,255,240,255,251,0,12,255,242,0,2,255,253,0,11,255,240,
  255,251,0,4,255,250,0,10,255,244,0,11,255,240,255,251,0,0,175,255,
  64,79,255,160,0,11,255,240,255,251,0,0,46,255,192,207,255,32,0,11,
  255,240,255,251,0,0,7,255,252,255,248,0,0,11,255,240,255,251,0,0,
  0,223,255,255,209,0,0,11,255,240,255,251,0,0,0,95,255,255,80,0,
  0,11,255,240,255,251,0,0,0,11,255,251,0,0,0,11,255,240,255,251,
  0,0,0,2,255,243,0,0,0,11,255,240,255,251,0,0,0,0,17,16,
  0,0,0,11,255,240,255,251,0,0,0,0,0,0,0,0,0,11,255,240,
  255,251,0,0,0,0,0,0,0,0,0,11,255,240,255,251,0,0,0,0,
  0,0,0,0,0,11,255,240,255,251,0,0,0,0,0,0,0,0,0,11,
  255,240,255,251,0,0,0,0,0,0,0,0,0,11,255,240,255,225,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,255,
  96,0,0,0,0,0,0,255,251,255,255,243,0,0,0,0,0,0,255,251,
  255,255,253,16,0,0,0,0,0,255,251,255,255,255,144,0,0,0,0,0,
  255,251,255,255,255,245,0,0,0,0,0,255,251,255,253,255,254,32,0,0,
  0,0,255,251,255,251,127,255,192,0,0,0,0,255,251,255,251,11,255,248,
  0,0,0,0,255,251,255,251,1,239,255,64,0,0,0,255,251,255,251,0,
  79,255,225,0,0,0,255,251,255,251,0,8,255,251,0,0,0,255,251,255,
  251,0,0,207,255,112,0,0,255,251,255,251,0,0,46,255,243,0,0,255,
  251,255,251,0,0,5,255,253,16,0,255,251,255,251,0,0,0,175,255,160,
  0,255,251,255,251,0,0,0,29,255,245,0,255,251,255,251,0,0,0,3,
  255,254,32,255,251,255,251,0,0,0,0,127,255,192,255,251,255,251,0,0,
  0,0,11,255,248,255,251,255,251,0,0,0,0,1,239,255,255,251,255,251,
  0,0,0,0,0,79,255,255,251,255,251,0,0,0,0,0,8,255,255,251,
  255,251,0,0,0,0,0,0,207,255,251,255,251,0,0,0,0,0,0,46,
  255,251,255,251,0,0,0,0,0,0,5,255,251,255,251,0,0,0,0,0,
  0,0,175,251,255,251,0,0,0,0,0,0,0,31,251,0,0,0,0,4,
  156,239,254,200,64,0,0,0,0,0,0,0,7,223,255,255,255,255,253,96,
  0,0,0,0,0,3,207,255,255,255,255,255,255,252,32,0,0,0,0,78,
  255,255,253,169,155,223,255,255,228,0,0,0,4,255,255,232,32,0,0,3,
  159,255,255,64,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,0,
  191,255,176,0,0,0,0,0,0,28,255,251,0,5,255,253,16,0,0,0,
  0,0,0,1,223,255,80,12,255,243,0,0,0,0,0,0,0,0,79,255,
  192,63,255,176,0,0,0,0,0,0,0,0,11,255,243,143,255,80,0,0,
  0,0,0,0,0,0,5,255,247,191,255,16,0,0,0,0,0,0,0,0,
  1,255,251,223,253,0,0,0,0,0,0,0,0,0,0,223,253,255,251,0,
  0,0,0,0,0,0,0,0,0,207,254,255,251,0,0,0,0,0,0,0,
  0,0,0,191,255,239,252,0,0,0,0,0,0,0,0,0,0,191,254,223,
  253,0,0,0,0,0,0,0,0,0,0,223,253,191,255,16,0,0,0,0,
  0,0,0,0,1,255,251,127,255,80,0,0,0,0,0,0,0,0,5,255,
  247,47,255,176,0,0,0,0,0,0,0,0,11,255,243,11,255,244,0,0,
  0,0,0,0,0,0,79,255,192,4,255,253,16,0,0,0,0,0,0,1,
  223,255,64,0,191,255,177,0,0,0,0,0,0,28,255,251,0,0,30,255,
  252,32,0,0,0,0,2,207,255,226,0,0,3,239,255,249,32,0,0,2,
  159,255,254,48,0,0,0,62,255,255,253,169,154,223,255,255,228,0,0,0,
  0,2,207,255,255,255,255,255,255,252,32,0,0,0,0,0,6,223,255,255,
  255,255,253,96,0,0,0,0,0,0,0,4,140,239,254,201,64,0,0,0,
  0,255,255,255,255,255,254,182,16,0,0,255,255,255,255,255,255,255,229,0,
  0,255,255,255,255,255,255,255,255,128,0,255,253,153,153,153,172,255,255,245,
  0,255,251,0,0,0,0,60,255,254,16,255,251,0,0,0,0,1,207,255,
  96,255,251,0,0,0,0,0,79,255,176,255,251,0,0,0,0,0,13,255,
  224,255,251,0,0,0,0,0,11,255,240,255,251,0,0,0,0,0,11,255,
  240,255,251,0,0,0,0,0,13,255,208,255,251,0,0,0,0,0,79,255,
  176,255,251,0,0,0,0,1,207,255,96,255,251,0,0,0,0,60,255,254,
  16,255,253,153,153,153,156,255,255,245,0,255,255,255,255,255,255,255,255,112,
  0,255,255,255,255,255,255,255,229,0,0,255,255,255,255,255,254,182,16,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,0,0,0,0,0,4,156,239,254,201,
  64,0,0,0,0,0,0,0,0,6,223,255,255,255,255,253,96,0,0,0,
  0,0,0,2,207,255,255,255,255,255,255,252,32,0,0,0,0,0,78,255,
  255,253,169,155,223,255,255,228,0,0,0,0,4,255,255,232,32,0,0,2,
  159,255,255,64,0,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,
  0,0,191,255,176,0,0,0,0,0,0,27,255,251,0,0,5,255,253,16,
  0,0,0,0,0,0,1,223,255,80,0,12,255,243,0,0,0,0,0,0,
  0,0,79,255,192,0,63,255,176,0,0,0,0,0,0,0,0,11,255,243,
  0,127,255,80,0,0,0,0,0,0,0,0,5,255,247,0,191,255,16,0,
  0,0,0,0,0,0,0,1,255,251,0,223,253,0,0,0,0,0,0,0,
  0,0,0,223,253,0,255,251,0,0,0,0,0,0,0,0,0,0,207,254,
  0,255,251,0,0,0,0,0,0,0,0,0,0,191,255,0,239,252,0,0,
  0,0,0,0,182,0,0,0,191,254,0,223,253,0,0,0,0,0,11,255,
  96,0,0,223,253,0,191,255,16,0,0,0,0,111,255,246,0,1,255,251,
  0,127,255,96,0,0,0,0,9,255,255,80,5,255,247,0,47,255,192,0,
  0,0,0,0,159,255,245,11,255,243,0,11,255,244,0,0,0,0,0,9,
  255,255,143,255,192,0,4,255,253,16,0,0,0,0,0,159,255,255,255,80,
  0,0,175,255,177,0,0,0,0,0,9,255,255,251,0,0,0,29,255,252,
  48,0,0,0,0,2,239,255,246,0,0,0,3,239,255,249,48,0,0,2,
  143,255,255,255,64,0,0,0,62,255,255,253,185,154,223,255,255,255,255,228,
  0,0,0,2,191,255,255,255,255,255,255,252,58,255,254,48,0,0,0,5,
  223,255,255,255,255,253,96,0,191,255,227,0,0,0,0,4,140,239,254,201,
  64,0,0,11,255,246,0,0,0,0,0,0,0,0,0,0,0,0,0,191,
  112,0,0,0,0,0,0,0,0,0,0,0,0,0,21,0,255,255,255,255,
  255,254,200,32,0,0,0,255,255,255,255,255,255,255,248,0,0,0,255,255,
  255,255,255,255,255,255,176,0,0,255,253,153,153,153,155,239,255,249,0,0,
  255,251,0,0,0,0,24,255,255,48,0,255,251,0,0,0,0,0,159,255,
  144,0,255,251,0,0,0,0,0,31,255,208,0,255,251,0,0,0,0,0,
  12,255,224,0,255,251,0,0,0,0,0,11,255,240,0,255,251,0,0,0,
  0,0,13,255,224,0,255,251,0,0,0,0,0,63,255,176,0,255,251,0,
  0,0,0,1,207,255,112,0,255,251,0,0,0,2,109,255,254,16,0,255,
  255,255,255,255,255,255,255,245,0,0,255,255,255,255,255,255,255,255,96,0,
  0,255,255,255,255,255,255,255,179,0,0,0,255,253,153,223,255,249,98,0,
  0,0,0,255,251,0,46,255,251,0,0,0,0,0,255,251,0,4,255,255,
  128,0,0,0,0,255,251,0,0,127,255,245,0,0,0,0,255,251,0,0,
  10,255,254,32,0,0,0,255,251,0,0,1,207,255,209,0,0,0,255,251,
  0,0,0,46,255,251,0,0,0,255,251,0,0,0,4,255,255,128,0,0,
  255,251,0,0,0,0,127,255,245,0,0,255,251,0,0,0,0,10,255,254,
  32,0,255,251,0,0,0,0,1,207,255,209,0,255,251,0,0,0,0,0,
  46,255,250,0,255,251,0,0,0,0,0,4,255,255,112,0,0,0,57,206,
  254,217,64,0,0,0,0,43,255,255,255,255,252,64,0,0,2,239,255,255,
  255,255,255,247,0,0,13,255,255,218,154,223,255,255,96,0,111,255,228,0,
  0,4,207,255,64,0,191,255,48,0,0,0,10,244,0,0,239,252,0,0,
  0,0,0,48,0,0,255,251,0,0,0,0,0,0,0,0,239,253,0,0,
  0,0,0,0,0,0,207,255,80,0,0,0,0,0,0,0,143,255,246,0,
  0,0,0,0,0,0,46,255,255,197,0,0,0,0,0,0,6,255,255,255,
  232,32,0,0,0,0,0,94,255,255,255,251,64,0,0,0,0,1,158,255,
  255,255,250,16,0,0,0,0,1,108,255,255,255,210,0,0,0,0,0,0,
  57,255,255,252,0,0,0,0,0,0,0,42,255,255,80,0,0,0,0,0,
  0,0,175,255,160,0,0,0,0,0,0,0,31,255,208,0,0,0,0,0,
  0,0,12,255,240,0,32,0,0,0,0,0,11,255,240,3,231,0,0,0,
  0,0,14,255,208,62,255,96,0,0,0,0,111,255,160,143,255,251,48,0,
  0,24,255,255,80,10,255,255,253,169,155,239,255,251,0,0,159,255,255,255,
  255,255,255,193,0,0,5,223,255,255,255,255,249,16,0,0,0,5,156,239,
  253,183,32,0,0,143,255,255,255,255,255,255,255,255,255,255,240,143,255,255,
  255,255,255,255,255,255,255,255,240,143,255,255,255,255,255,255,255,255,255,255,
  240,73,153,153,153,153,255,253,153,153,153,153,144,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,
  0,0,255,251,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,239,253,0,0,0,0,0,0,2,255,250,191,255,16,0,0,0,0,
  0,5,255,247,127,255,112,0,0,0,0,0,11,255,243,47,255,226,0,0,
  0,0,0,79,255,208,10,255,252,16,0,0,0,3,239,255,96,2,239,255,
  230,0,0,1,127,255,251,0,0,79,255,255,235,154,191,255,255,210,0,0,
  4,239,255,255,255,255,255,252,32,0,0,0,42,255,255,255,255,255,129,0,
  0,0,0,0,56,206,255,219,113,0,0,0,159,255,128,0,0,0,0,0,
  0,0,14,255,242,63,255,208,0,0,0,0,0,0,0,95,255,176,12,255,
  244,0,0,0,0,0,0,0,191,255,80,6,255,249,0,0,0,0,0,0,
  1,255,254,0,1,255,254,16,0,0,0,0,0,7,255,248,0,0,175,255,
  96,0,0,0,0,0,13,255,242,0,0,79,255,176,0,0,0,0,0,63,
  255,192,0,0,13,255,242,0,0,0,0,0,159,255,96,0,0,8,255,247,
  0,0,0,0,0,239,254,16,0,0,2,255,253,0,0,0,0,5,255,249,
  0,0,0,0,191,255,64,0,0,0,11,255,243,0,0,0,0,95,255,144,
  0,0,0,47,255,208,0,0,0,0,14,255,225,0,0,0,127,255,112,0,
  0,0,0,9,255,245,0,0,0,223,255,16,0,0,0,0,3,255,251,0,
  0,3,255,250,0,0,0,0,0,0,207,255,32,0,9,255,244,0,0,0,
  0,0,0,127,255,112,0,30,255,208,0,0,0,0,0,0,31,255,208,0,
  95,255,128,0,0,0,0,0,0,10,255,244,0,191,255,32,0,0,0,0,
  0,0,4,255,249,2,255,251,0,0,0,0,0,0,0,0,223,254,23,255,
  245,0,0,0,0,0,0,0,0,143,255,93,255,225,0,0,0,0,0,0,
  0,0,47,255,223,255,144,0,0,0,0,0,0,0,0,11,255,255,255,48,
  0,0,0,0,0,0,0,0,6,255,255,252,0,0,0,0,0,0,0,0,
  0,1,239,255,246,0,0,0,0,0,0,0,0,0,0,159,255,241,0,0,
  0,0,0,0,0,0,0,0,63,255,160,0,0,0,0,0,0,0,0,0,
  0,12,255,64,0,0,0,0,0,159,255,48,0,0,0,0,0,14,255,32,
  0,0,0,0,0,14,255,192,79,255,128,0,0,0,0,0,63,255,96,0,
  0,0,0,0,79,255,112,14,255,192,0,0,0,0,0,143,255,176,0,0,
  0,0,0,143,255,32,9,255,242,0,0,0,0,0,207,255,241,0,0,0,
  0,0,223,253,0,5,255,246,0,0,0,0,2,255,255,245,0,0,0,0,
  3,255,248,0,1,239,251,0,0,0,0,6,255,255,249,0,0,0,0,7,
  255,243,0,0,175,255,16,0,0,0,11,255,255,254,0,0,0,0,12,255,
  208,0,0,111,255,80,0,0,0,31,255,207,255,48,0,0,0,31,255,144,
  0,0,31,255,144,0,0,0,95,255,62,255,128,0,0,0,111,255,64,0,
  0,11,255,224,0,0,0,159,253,10,255,192,0,0,0,175,254,0,0,0,
  6,255,244,0,0,0,239,249,6,255,242,0,0,1,239,250,0,0,0,2,
  255,248,0,0,3,255,244,1,255,246,0,0,5,255,245,0,0,0,0,207,
  253,0,0,8,255,225,0,207,251,0,0,9,255,241,0,0,0,0,127,255,
  32,0,12,255,160,0,127,255,16,0,14,255,176,0,0,0,0,63,255,112,
  0,47,255,96,0,63,255,80,0,63,255,96,0,0,0,0,13,255,176,0,
  111,255,16,0,13,255,144,0,143,255,16,0,0,0,0,8,255,241,0,191,
  252,0,0,9,255,224,0,207,252,0,0,0,0,0,4,255,246,1,255,247,
  0,0,4,255,243,2,255,247,0,0,0,0,0,0,239,250,5,255,243,0,
  0,0,239,248,7,255,242,0,0,0,0,0,0,159,254,9,255,208,0,0,
  0,175,252,11,255,208,0,0,0,0,0,0,95,255,78,255,144,0,0,0,
  95,255,63,255,128,0,0,0,0,0,0,30,255,207,255,64,0,0,0,31,
  255,191,255,48,0,0,0,0,0,0,10,255,255,254,16,0,0,0,11,255,
  255,253,0,0,0,0,0,0,0,5,255,255,250,0,0,0,0,7,255,255,
  249,0,0,0,0,0,0,0,1,255,255,246,0,0,0,0,2,255,255,244,
  0,0,0,0,0,0,0,0,191,255,242,0,0,0,0,0,223,255,224,0,
  0,0,0,0,0,0,0,111,255,192,0,0,0,0,0,143,255,160,0,0,
  0,0,0,0,0,0,47,255,112,0,0,0,0,0,79,255,80,0,0,0,
  0,0,0,0,0,12,255,48,0,0,0,0,0,14,255,16,0,0,0,0,
  29,255,248,0,0,0,0,0,0,0,159,255,160,4,255,255,48,0,0,0,
  0,0,4,255,254,16,0,159,255,209,0,0,0,0,0,29,255,244,0,0,
  29,255,249,0,0,0,0,0,159,255,144,0,0,3,255,255,64,0,0,0,
  4,255,253,16,0,0,0,127,255,209,0,0,0,29,255,244,0,0,0,0,
  12,255,250,0,0,0,159,255,128,0,0,0,0,2,239,255,80,0,5,255,
  252,0,0,0,0,0,0,111,255,226,0,30,255,243,0,0,0,0,0,0,
  10,255,251,0,175,255,112,0,0,0,0,0,0,1,239,255,101,255,252,0,
  0,0,0,0,0,0,0,95,255,238,255,226,0,0,0,0,0,0,0,0,
  9,255,255,255,96,0,0,0,0,0,0,0,0,1,223,255,251,0,0,0,
  0,0,0,0,0,0,0,191,255,252,0,0,0,0,0,0,0,0,0,6,
  255,255,255,112,0,0,0,0,0,0,0,0,46,255,255,255,243,0,0,0,
  0,0,0,0,0,191,255,104,255,252,0,0,0,0,0,0,0,7,255,251,
  0,207,255,112,0,0,0,0,0,0,63,255,226,0,63,255,242,0,0,0,
  0,0,0,223,255,80,0,8,255,252,0,0,0,0,0,9,255,250,0,0,
  1,223,255,112,0,0,0,0,79,255,209,0,0,0,79,255,242,0,0,0,
  1,223,255,64,0,0,0,9,255,252,0,0,0,10,255,248,0,0,0,0,
  1,223,255,112,0,0,95,255,208,0,0,0,0,0,79,255,226,0,2,239,
  255,48,0,0,0,0,0,9,255,251,0,11,255,247,0,0,0,0,0,0,
  1,223,255,112,127,255,192,0,0,0,0,0,0,0,79,255,226,30,255,246,
  0,0,0,0,0,0,0,159,255,160,6,255,254,16,0,0,0,0,0,3,
  255,254,32,0,191,255,144,0,0,0,0,0,12,255,247,0,0,47,255,243,
  0,0,0,0,0,111,255,192,0,0,8,255,252,0,0,0,0,1,239,255,
  64,0,0,1,223,255,96,0,0,0,10,255,249,0,0,0,0,95,255,225,
  0,0,0,79,255,225,0,0,0,0,10,255,249,0,0,0,223,255,96,0,
  0,0,0,2,239,255,64,0,7,255,251,0,0,0,0,0,0,127,255,192,
  0,46,255,242,0,0,0,0,0,0,13,255,247,0,175,255,128,0,0,0,
  0,0,0,4,255,254,36,255,253,16,0,0,0,0,0,0,0,175,255,173,
  255,244,0,0,0,0,0,0,0,0,30,255,255,255,160,0,0,0,0,0,
  0,0,0,6,255,255,254,32,0,0,0,0,0,0,0,0,0,207,255,247,
  0,0,0,0,0,0,0,0,0,0,63,255,208,0,0,0,0,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,
  0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,
  0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,
  0,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,0,0,0,0,
  15,255,176,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,0,0,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,15,255,255,255,255,255,
  255,255,255,255,15,255,255,255,255,255,255,255,255,255,15,255,255,255,255,255,
  255,255,255,255,9,153,153,153,153,153,153,158,255,247,0,0,0,0,0,0,
  0,95,255,192,0,0,0,0,0,0,1,239,255,48,0,0,0,0,0,0,
  10,255,247,0,0,0,0,0,0,0,111,255,192,0,0,0,0,0,0,2,
  239,255,48,0,0,0,0,0,0,11,255,248,0,0,0,0,0,0,0,111,
  255,208,0,0,0,0,0,0,2,239,255,64,0,0,0,0,0,0,11,255,
  249,0,0,0,0,0,0,0,111,255,209,0,0,0,0,0,0,2,239,255,
  64,0,0,0,0,0,0,11,255,249,0,0,0,0,0,0,0,111,255,225,
  0,0,0,0,0,0,2,239,255,80,0,0,0,0,0,0,11,255,250,0,
  0,0,0,0,0,0,127,255,225,0,0,0,0,0,0,2,239,255,80,0,
  0,0,0,0,0,11,255,250,0,0,0,0,0,0,0,127,255,226,0,0,
  0,0,0,0,2,255,255,96,0,0,0,0,0,0,12,255,251,0,0,0,
  0,0,0,0,127,255,251,153,153,153,153,153,153,153,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
  255,255,255,255,255,255,255,255,240,255,255,255,255,240,255,254,238,238,224,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,
  240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,255,255,255,240,255,
  255,255,255,240,255,255,255,255,240,79,255,32,0,0,0,0,0,14,255,112,
  0,0,0,0,0,9,255,192,0,0,0,0,0,3,255,242,0,0,0,0,
  0,0,223,248,0,0,0,0,0,0,143,253,0,0,0,0,0,0,63,255,
  48,0,0,0,0,0,12,255,128,0,0,0,0,0,7,255,208,0,0,0,
  0,0,2,255,244,0,0,0,0,0,0,207,249,0,0,0,0,0,0,127,
  254,0,0,0,0,0,0,47,255,64,0,0,0,0,0,11,255,160,0,0,
  0,0,0,6,255,225,0,0,0,0,0,1,255,245,0,0,0,0,0,0,
  191,250,0,0,0,0,0,0,95,255,16,0,0,0,0,0,30,255,96,0,
  0,0,0,0,10,255,176,0,0,0,0,0,5,255,241,0,0,0,0,0,
  0,239,246,0,0,0,0,0,0,159,251,0,0,0,0,0,0,79,255,32,
  0,0,0,0,0,14,255,112,0,0,0,0,0,9,255,192,0,0,0,0,
  0,3,255,242,0,0,0,0,0,0,223,247,0,0,0,0,0,0,143,253,
  0,0,0,0,0,0,63,255,48,0,0,0,0,0,13,255,128,0,0,0,
  0,0,7,255,208,255,255,255,255,240,255,255,255,255,240,238,238,238,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,
  0,0,0,255,240,0,0,0,255,240,0,0,0,255,240,255,255,255,255,240,
  255,255,255,255,240,255,255,255,255,240,0,0,2,136,48,0,0,0,0,11,
  255,192,0,0,0,0,79,255,245,0,0,0,0,207,255,253,0,0,0,5,
  255,184,255,96,0,0,13,255,49,239,225,0,0,111,249,0,127,248,0,1,
  239,226,0,30,255,32,8,255,128,0,6,255,144,46,254,16,0,0,223,243,
  159,246,0,0,0,95,251,153,153,153,153,153,153,153,153,153,144,255,255,255,
  255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,240,255,255,255,
  255,255,255,255,255,255,240,0,3,0,0,0,0,143,64,0,0,8,255,226,
  0,0,46,255,252,0,0,3,223,255,144,0,0,44,255,246,0,0,1,175,
  255,48,0,0,8,255,64,0,0,0,85,0,0,0,3,140,239,235,96,11,
  255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,253,
  255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,42,255,
  255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,
  255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,
  255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,
  255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,
  255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,255,255,
  255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,253,75,
  255,240,0,0,3,156,239,235,97,11,255,240,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,
  0,0,255,240,1,107,239,236,130,0,0,0,255,240,77,255,255,255,255,145,
  0,0,255,245,255,255,255,255,255,252,16,0,255,255,255,252,169,191,255,255,
  192,0,255,255,250,32,0,1,143,255,248,0,255,255,144,0,0,0,6,255,
  255,16,255,253,0,0,0,0,0,175,255,112,255,246,0,0,0,0,0,47,
  255,176,255,242,0,0,0,0,0,13,255,224,255,240,0,0,0,0,0,11,
  255,240,255,240,0,0,0,0,0,11,255,240,255,242,0,0,0,0,0,13,
  255,224,255,246,0,0,0,0,0,63,255,176,255,253,0,0,0,0,0,175,
  255,112,255,255,144,0,0,0,6,255,255,32,255,255,250,32,0,1,159,255,
  248,0,255,255,255,252,169,191,255,255,193,0,255,246,255,255,255,255,255,253,
  32,0,255,240,94,255,255,255,255,145,0,0,255,240,1,107,239,236,147,0,
  0,0,0,0,1,123,239,253,165,0,0,0,0,126,255,255,255,255,212,0,
  0,27,255,255,255,255,255,255,112,0,191,255,255,185,156,255,255,209,7,255,
  255,145,0,0,25,253,32,30,255,246,0,0,0,0,82,0,111,255,160,0,
  0,0,0,0,0,191,255,32,0,0,0,0,0,0,239,253,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,223,253,0,0,0,0,0,0,0,191,255,32,0,0,0,0,0,0,111,
  255,160,0,0,0,0,0,0,30,255,246,0,0,0,0,83,0,6,255,255,
  145,0,0,24,254,32,0,175,255,255,185,155,255,255,209,0,10,255,255,255,
  255,255,255,112,0,0,110,255,255,255,255,212,0,0,0,1,107,239,253,165,
  0,0,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,0,0,0,0,0,11,
  255,240,0,0,0,0,0,0,0,11,255,240,0,0,3,156,239,235,96,11,
  255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,237,
  255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,43,255,
  255,240,30,255,246,0,0,0,0,175,255,240,127,255,160,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,
  255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,
  255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,
  255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,
  255,240,8,255,255,145,0,0,43,255,255,240,1,207,255,255,185,172,255,255,
  255,240,0,45,255,255,255,255,255,253,255,240,0,1,175,255,255,255,253,59,
  255,240,0,0,3,157,255,235,96,11,255,240,0,0,1,107,239,253,165,0,
  0,0,0,0,110,255,255,255,255,195,0,0,0,10,255,255,255,255,255,254,
  64,0,0,175,255,254,185,172,255,255,226,0,6,255,255,113,0,0,60,255,
  251,0,30,255,244,0,0,0,1,223,255,48,111,255,128,0,0,0,0,95,
  255,144,191,255,16,0,0,0,0,14,255,192,239,255,255,255,255,255,255,255,
  255,224,255,255,255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,
  255,224,223,255,153,153,153,153,153,153,153,128,191,255,32,0,0,0,0,0,
  0,0,111,255,144,0,0,0,0,0,0,0,30,255,246,0,0,0,0,38,
  0,0,6,255,255,145,0,0,6,239,144,0,0,175,255,255,201,155,239,255,
  247,0,0,10,255,255,255,255,255,255,177,0,0,0,110,255,255,255,255,232,
  0,0,0,0,1,106,223,254,183,16,0,0,0,0,0,0,40,206,253,146,
  0,0,0,0,6,239,255,255,254,80,0,0,0,95,255,255,255,255,208,0,
  0,1,239,255,234,156,253,32,0,0,8,255,251,16,0,98,0,0,0,12,
  255,242,0,0,0,0,0,0,14,255,192,0,0,0,0,0,0,15,255,176,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,
  0,0,143,255,255,255,255,255,255,128,0,143,255,255,255,255,255,255,128,0,
  143,255,255,255,255,255,255,128,0,73,153,159,255,217,153,153,64,0,0,0,
  15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,
  176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,
  0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,
  0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,
  0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,
  255,176,0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,
  0,0,0,0,0,0,15,255,176,0,0,0,0,0,0,15,255,176,0,0,
  0,0,0,0,4,173,255,218,64,11,255,240,0,1,191,255,255,255,251,27,
  255,240,0,45,255,255,255,255,255,204,255,240,1,223,255,254,185,174,255,255,
  255,240,9,255,255,112,0,0,110,255,255,240,47,255,244,0,0,0,4,255,
  255,240,127,255,144,0,0,0,0,143,255,240,191,255,32,0,0,0,0,47,
  255,240,239,253,0,0,0,0,0,13,255,240,255,251,0,0,0,0,0,11,
  255,240,255,251,0,0,0,0,0,11,255,240,239,253,0,0,0,0,0,13,
  255,240,191,255,32,0,0,0,0,31,255,240,127,255,144,0,0,0,0,143,
  255,240,47,255,244,0,0,0,3,255,255,240,9,255,255,112,0,0,94,255,
  255,240,1,223,255,254,185,173,255,255,255,240,0,45,255,255,255,255,255,204,
  255,240,0,2,191,255,255,255,251,27,255,240,0,0,4,173,255,218,64,11,
  255,240,0,0,0,0,0,0,0,14,255,208,0,0,0,0,0,0,0,79,
  255,144,3,214,0,0,0,0,1,223,255,64,95,255,163,0,0,0,93,255,
  251,0,62,255,255,219,153,190,255,255,226,0,4,239,255,255,255,255,255,254,
  48,0,0,42,255,255,255,255,255,161,0,0,0,0,40,190,255,236,130,0,
  0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,90,239,236,113,0,0,255,251,27,255,255,255,253,64,
  0,255,252,223,255,255,255,255,243,0,255,255,255,235,154,239,255,253,0,255,
  255,250,16,0,25,255,255,96,255,255,176,0,0,0,175,255,176,255,255,32,
  0,0,0,47,255,224,255,253,0,0,0,0,13,255,240,255,251,0,0,0,
  0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,
  255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,0,0,
  0,0,0,0,0,0,0,0,0,0,191,255,0,191,255,0,191,255,0,191,
  255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,
  191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,0,191,255,
  0,191,255,0,191,255,0,191,255,0,0,0,0,0,44,252,32,0,0,0,
  0,207,255,192,0,0,0,0,255,255,240,0,0,0,0,207,255,192,0,0,
  0,0,44,252,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,191,255,0,
  0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,
  0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,
  255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,
  191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,
  0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,
  0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,0,
  0,0,0,191,255,0,0,0,0,0,191,255,0,0,0,0,0,191,255,0,
  0,0,0,0,191,255,0,0,0,0,0,223,254,0,0,163,0,7,255,251,
  0,11,255,185,207,255,246,0,127,255,255,255,255,192,0,10,255,255,255,252,
  16,0,0,91,239,235,96,0,0,255,251,0,0,0,0,0,0,0,255,251,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,
  0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,4,255,254,48,255,
  251,0,0,0,62,255,244,0,255,251,0,0,2,239,255,80,0,255,251,0,
  0,29,255,246,0,0,255,251,0,1,207,255,112,0,0,255,251,0,11,255,
  248,0,0,0,255,251,0,175,255,160,0,0,0,255,251,8,255,251,0,0,
  0,0,255,251,127,255,193,0,0,0,0,255,254,255,255,16,0,0,0,0,
  255,252,223,255,144,0,0,0,0,255,251,62,255,247,0,0,0,0,255,251,
  4,255,255,80,0,0,0,255,251,0,95,255,244,0,0,0,255,251,0,8,
  255,254,32,0,0,255,251,0,0,159,255,210,0,0,255,251,0,0,11,255,
  252,16,0,255,251,0,0,1,207,255,176,0,255,251,0,0,0,46,255,249,
  0,255,251,0,0,0,3,239,255,112,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,0,107,239,235,96,0,0,41,223,236,
  130,0,0,255,251,61,255,255,255,253,48,7,255,255,255,254,96,0,255,253,
  239,255,255,255,255,226,143,255,255,255,255,245,0,255,255,255,234,155,239,255,
  253,255,253,169,223,255,254,16,255,255,248,16,0,26,255,255,255,112,0,6,
  255,255,128,255,255,144,0,0,0,207,255,248,0,0,0,111,255,192,255,255,
  16,0,0,0,79,255,225,0,0,0,14,255,224,255,252,0,0,0,0,31,
  255,192,0,0,0,12,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,
  11,255,240,255,251,0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,
  0,0,0,0,15,255,176,0,0,0,11,255,240,255,251,0,0,0,0,15,
  255,176,0,0,0,11,255,240,255,251,0,90,239,235,96,0,0,255,251,27,
  255,255,255,252,32,0,255,252,223,255,255,255,255,209,0,255,255,255,235,154,
  239,255,251,0,255,255,250,16,0,25,255,255,64,255,255,176,0,0,0,175,
  255,160,255,255,32,0,0,0,47,255,208,255,253,0,0,0,0,13,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,
  240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,
  251,0,0,0,0,11,255,240,0,0,1,123,239,253,165,0,0,0,0,0,
  110,255,255,255,255,212,0,0,0,10,255,255,255,255,255,255,112,0,0,175,
  255,255,185,172,255,255,246,0,6,255,255,129,0,0,59,255,255,48,30,255,
  246,0,0,0,0,175,255,176,111,255,144,0,0,0,0,29,255,242,191,255,
  32,0,0,0,0,7,255,247,239,253,0,0,0,0,0,2,255,249,255,251,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,1,255,251,223,253,
  0,0,0,0,0,3,255,249,175,255,48,0,0,0,0,7,255,246,111,255,
  160,0,0,0,0,29,255,242,30,255,246,0,0,0,0,175,255,160,6,255,
  255,145,0,0,59,255,254,32,0,175,255,255,185,172,255,255,246,0,0,10,
  255,255,255,255,255,255,96,0,0,0,110,255,255,255,255,212,0,0,0,0,
  1,123,239,253,165,0,0,0,255,240,1,107,239,236,130,0,0,0,255,240,
  77,255,255,255,255,145,0,0,255,245,255,255,255,255,255,252,16,0,255,255,
  255,252,169,191,255,255,192,0,255,255,250,32,0,1,143,255,248,0,255,255,
  144,0,0,0,6,255,255,16,255,253,0,0,0,0,0,175,255,112,255,246,
  0,0,0,0,0,47,255,176,255,242,0,0,0,0,0,13,255,224,255,240,
  0,0,0,0,0,11,255,240,255,240,0,0,0,0,0,11,255,240,255,242,
  0,0,0,0,0,13,255,224,255,246,0,0,0,0,0,63,255,176,255,253,
  0,0,0,0,0,175,255,112,255,255,144,0,0,0,6,255,255,32,255,255,
  250,32,0,1,159,255,248,0,255,255,255,252,169,191,255,255,193,0,255,246,
  255,255,255,255,255,253,32,0,255,240,94,255,255,255,255,145,0,0,255,240,
  1,107,239,236,147,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,255,240,0,0,0,0,0,0,0,0,255,240,
  0,0,0,0,0,0,0,0,0,0,2,140,239,235,97,2,255,248,0,1,
  159,255,255,255,253,66,255,248,0,28,255,255,255,255,255,246,255,248,0,207,
  255,255,185,172,255,255,255,248,8,255,255,129,0,0,43,255,255,248,31,255,
  246,0,0,0,0,175,255,248,127,255,160,0,0,0,0,30,255,248,191,255,
  32,0,0,0,0,8,255,248,239,253,0,0,0,0,0,4,255,248,255,251,
  0,0,0,0,0,3,255,248,255,251,0,0,0,0,0,3,255,248,239,253,
  0,0,0,0,0,4,255,248,191,255,32,0,0,0,0,8,255,248,127,255,
  160,0,0,0,0,30,255,248,47,255,246,0,0,0,0,191,255,248,8,255,
  255,145,0,0,43,255,255,248,1,207,255,255,185,172,255,255,255,248,0,45,
  255,255,255,255,255,246,255,248,0,1,159,255,255,255,253,66,255,248,0,0,
  3,156,239,235,97,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,0,0,0,0,0,0,0,2,255,248,0,0,
  0,0,0,0,0,2,255,248,255,251,2,157,255,199,16,255,251,95,255,255,
  255,210,255,253,255,255,255,255,244,255,255,255,234,155,255,112,255,255,247,0,
  0,55,0,255,255,128,0,0,0,0,255,255,16,0,0,0,0,255,252,0,
  0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,
  0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,
  251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,0,
  255,251,0,0,0,0,0,255,251,0,0,0,0,0,255,251,0,0,0,0,
  0,255,251,0,0,0,0,0,0,0,23,190,255,218,64,0,0,3,223,255,
  255,255,251,32,0,46,255,255,255,255,255,210,0,159,255,251,154,223,255,247,
  0,223,255,48,0,4,223,128,0,255,251,0,0,0,22,0,0,239,254,16,
  0,0,0,0,0,175,255,213,0,0,0,0,0,46,255,255,234,81,0,0,
  0,3,223,255,255,254,146,0,0,0,5,191,255,255,254,64,0,0,0,1,
  107,255,255,225,0,0,0,0,0,44,255,247,0,0,0,0,0,2,255,250,
  0,75,16,0,0,1,255,251,5,255,212,0,0,8,255,249,12,255,255,218,
  154,223,255,244,1,207,255,255,255,255,255,176,0,24,255,255,255,255,250,16,
  0,0,39,206,255,217,64,0,0,0,15,255,176,0,0,0,0,15,255,176,
  0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,
  176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,
  255,176,0,0,143,255,255,255,255,255,248,143,255,255,255,255,255,248,143,255,
  255,255,255,255,248,73,153,159,255,217,153,148,0,0,15,255,176,0,0,0,
  0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,
  0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,
  0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,
  0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,255,
  176,0,0,0,0,15,255,176,0,0,0,0,15,255,176,0,0,0,0,15,
  255,176,0,0,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,
  0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,
  0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,
  239,252,0,0,0,0,207,254,207,255,16,0,0,1,255,252,143,255,128,0,
  0,8,255,248,63,255,246,0,0,111,255,243,9,255,255,218,157,255,255,144,
  1,207,255,255,255,255,252,16,0,26,255,255,255,255,161,0,0,0,73,223,
  253,164,0,0,95,255,160,0,0,0,0,1,255,252,13,255,241,0,0,0,
  0,7,255,245,7,255,247,0,0,0,0,13,255,208,1,239,253,0,0,0,
  0,95,255,112,0,159,255,80,0,0,0,191,255,16,0,63,255,176,0,0,
  2,255,249,0,0,11,255,242,0,0,8,255,243,0,0,4,255,248,0,0,
  30,255,176,0,0,0,223,254,0,0,111,255,64,0,0,0,111,255,96,0,
  207,253,0,0,0,0,30,255,192,3,255,246,0,0,0,0,8,255,243,9,
  255,225,0,0,0,0,2,255,249,31,255,128,0,0,0,0,0,175,254,143,
  255,32,0,0,0,0,0,79,255,255,250,0,0,0,0,0,0,12,255,255,
  244,0,0,0,0,0,0,6,255,255,192,0,0,0,0,0,0,0,239,255,
  96,0,0,0,0,0,0,0,127,254,0,0,0,0,0,0,0,0,31,247,
  0,0,0,0,79,255,112,0,0,0,3,255,48,0,0,0,7,255,244,13,
  255,208,0,0,0,9,255,144,0,0,0,13,255,208,8,255,243,0,0,0,
  14,255,224,0,0,0,63,255,128,2,255,248,0,0,0,79,255,244,0,0,
  0,143,255,32,0,191,253,0,0,0,159,255,249,0,0,0,223,251,0,0,
  111,255,64,0,0,239,255,254,0,0,4,255,246,0,0,30,255,144,0,4,
  255,255,255,64,0,9,255,225,0,0,9,255,224,0,10,255,186,255,160,0,
  14,255,144,0,0,4,255,244,0,30,255,85,255,225,0,79,255,64,0,0,
  0,223,250,0,95,254,16,239,245,0,175,253,0,0,0,0,143,254,16,175,
  250,0,159,250,1,239,248,0,0,0,0,47,255,81,255,244,0,79,255,21,
  255,242,0,0,0,0,11,255,166,255,208,0,13,255,106,255,192,0,0,0,
  0,5,255,252,255,128,0,8,255,207,255,96,0,0,0,0,1,239,255,255,
  48,0,2,255,255,254,16,0,0,0,0,0,159,255,252,0,0,0,207,255,
  250,0,0,0,0,0,0,79,255,247,0,0,0,111,255,244,0,0,0,0,
  0,0,13,255,241,0,0,0,31,255,208,0,0,0,0,0,0,7,255,176,
  0,0,0,11,255,128,0,0,0,0,0,0,2,255,80,0,0,0,5,255,
  32,0,0,0,9,255,251,0,0,0,0,111,255,192,1,223,255,112,0,0,
  2,239,254,32,0,63,255,243,0,0,11,255,245,0,0,7,255,252,0,0,
  111,255,160,0,0,0,207,255,128,2,239,253,16,0,0,0,46,255,243,11,
  255,244,0,0,0,0,5,255,253,127,255,128,0,0,0,0,0,175,255,255,
  252,0,0,0,0,0,0,29,255,255,243,0,0,0,0,0,0,4,255,255,
  128,0,0,0,0,0,0,9,255,255,209,0,0,0,0,0,0,95,255,255,
  250,0,0,0,0,0,2,239,254,223,255,80,0,0,0,0,11,255,245,79,
  255,225,0,0,0,0,127,255,160,9,255,251,0,0,0,3,255,253,16,1,
  223,255,96,0,0,12,255,244,0,0,79,255,226,0,0,143,255,144,0,0,
  9,255,251,0,4,255,253,16,0,0,1,223,255,112,29,255,244,0,0,0,
  0,63,255,243,47,255,208,0,0,0,0,4,255,250,11,255,243,0,0,0,
  0,10,255,244,5,255,249,0,0,0,0,47,255,192,0,239,254,16,0,0,
  0,143,255,96,0,143,255,96,0,0,0,239,254,0,0,47,255,192,0,0,
  5,255,247,0,0,10,255,243,0,0,11,255,225,0,0,4,255,249,0,0,
  47,255,144,0,0,0,223,254,0,0,159,255,32,0,0,0,127,255,80,1,
  239,251,0,0,0,0,31,255,176,6,255,244,0,0,0,0,9,255,242,12,
  255,192,0,0,0,0,3,255,248,63,255,96,0,0,0,0,0,207,254,175,
  254,0,0,0,0,0,0,111,255,255,247,0,0,0,0,0,0,30,255,255,
  241,0,0,0,0,0,0,9,255,255,144,0,0,0,0,0,0,2,255,255,
  32,0,0,0,0,0,0,1,255,251,0,0,0,0,0,0,0,8,255,244,
  0,0,0,0,0,0,0,30,255,192,0,0,0,0,0,0,0,127,255,96,
  0,0,0,0,0,0,1,239,253,0,0,0,0,0,0,0,7,255,247,0,
  0,0,0,0,0,0,13,255,225,0,0,0,0,0,0,0,111,255,128,0,
  0,0,0,0,0,0,223,255,32,0,0,0,0,0,0,5,255,250,0,0,
  0,0,0,0,15,255,255,255,255,255,255,255,15,255,255,255,255,255,255,255,
  15,255,255,255,255,255,255,251,9,153,153,153,153,255,255,225,0,0,0,0,
  8,255,255,64,0,0,0,0,63,255,248,0,0,0,0,1,223,255,192,0,
  0,0,0,10,255,254,32,0,0,0,0,95,255,245,0,0,0,0,2,239,
  255,144,0,0,0,0,11,255,252,0,0,0,0,0,127,255,226,0,0,0,
  0,3,255,255,80,0,0,0,0,29,255,249,0,0,0,0,0,159,255,209,
  0,0,0,0,5,255,255,48,0,0,0,0,46,255,253,153,153,153,153,144,
  191,255,255,255,255,255,255,240,255,255,255,255,255,255,255,240,255,255,255,255,
  255,255,255,240,0,0,23,206,255,128,0,2,223,255,255,128,0,11,255,255,
  238,112,0,63,255,113,0,0,0,111,250,0,0,0,0,143,247,0,0,0,
  0,143,247,0,0,0,0,111,248,0,0,0,0,95,249,0,0,0,0,63,
  251,0,0,0,0,47,252,0,0,0,0,31,253,0,0,0,0,15,254,0,
  0,0,0,31,253,0,0,0,1,143,249,0,0,0,255,255,210,0,0,0,
  255,255,64,0,0,0,221,255,227,0,0,0,0,111,250,0,0,0,0,15,
  253,0,0,0,0,15,254,0,0,0,0,31,253,0,0,0,0,47,252,0,
  0,0,0,79,251,0,0,0,0,95,249,0,0,0,0,111,248,0,0,0,
  0,143,247,0,0,0,0,143,247,0,0,0,0,111,251,0,0,0,0,63,
  255,129,0,0,0,11,255,255,255,112,0,1,223,255,255,128,0,0,23,206,
  255,128,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,
  255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,251,255,253,163,0,
  0,0,255,255,255,112,0,0,238,255,255,244,0,0,0,5,239,250,0,0,
  0,0,127,254,0,0,0,0,79,255,0,0,0,0,79,255,0,0,0,0,
  95,254,0,0,0,0,111,252,0,0,0,0,143,251,0,0,0,0,159,250,
  0,0,0,0,175,248,0,0,0,0,191,247,0,0,0,0,159,248,0,0,
  0,0,111,254,48,0,0,0,12,255,255,128,0,0,2,239,255,128,0,0,
  29,255,237,96,0,0,127,252,16,0,0,0,175,247,0,0,0,0,191,247,
  0,0,0,0,175,248,0,0,0,0,159,250,0,0,0,0,127,251,0,0,
  0,0,111,253,0,0,0,0,95,254,0,0,0,0,79,255,0,0,0,0,
  79,255,0,0,0,0,143,254,0,0,0,21,239,250,0,0,255,255,255,244,
  0,0,255,255,255,112,0,0,255,253,163,0,0,0,0,4,137,133,0,0,
  0,1,0,2,207,255,255,214,0,0,110,80,46,255,255,255,255,234,156,255,
  244,175,255,255,255,255,255,255,255,226,29,249,32,22,223,255,255,254,48,2,
  96,0,0,5,190,253,129,0,0,0,0,0,0,23,0,0,0,0,0,0,
  0,0,0,191,128,0,0,0,0,0,0,0,8,255,247,0,0,0,0,0,
  0,0,79,255,248,0,0,0,0,0,0,2,239,255,96,0,0,0,0,0,
  0,12,255,228,0,0,0,0,0,0,0,143,254,48,0,0,0,0,0,0,
  0,62,194,0,0,0,0,0,0,0,0,3,16,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  3,140,239,235,96,11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,
  255,255,255,255,255,253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,
  255,129,0,0,42,255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,
  160,0,0,0,0,13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,
  0,0,0,0,0,4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,
  0,0,0,0,0,3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,
  32,0,0,0,0,8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,
  246,0,0,0,0,159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,
  255,255,185,156,255,255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,
  159,255,255,255,253,75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,
  0,0,0,84,0,0,0,0,0,0,0,0,2,239,64,0,0,0,0,0,
  0,0,12,255,227,0,0,0,0,0,0,0,159,255,229,0,0,0,0,0,
  0,6,255,254,48,0,0,0,0,0,0,63,255,194,0,0,0,0,0,0,
  0,223,251,16,0,0,0,0,0,0,0,127,144,0,0,0,0,0,0,0,
  0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,1,107,239,253,165,0,0,0,0,0,
  110,255,255,255,255,195,0,0,0,10,255,255,255,255,255,254,64,0,0,175,
  255,254,185,172,255,255,226,0,6,255,255,113,0,0,60,255,251,0,30,255,
  244,0,0,0,1,223,255,48,111,255,128,0,0,0,0,95,255,144,191,255,
  16,0,0,0,0,14,255,192,239,255,255,255,255,255,255,255,255,224,255,255,
  255,255,255,255,255,255,255,240,255,255,255,255,255,255,255,255,255,224,223,255,
  153,153,153,153,153,153,153,128,191,255,32,0,0,0,0,0,0,0,111,255,
  144,0,0,0,0,0,0,0,30,255,246,0,0,0,0,38,0,0,6,255,
  255,145,0,0,6,239,144,0,0,175,255,255,201,155,239,255,247,0,0,10,
  255,255,255,255,255,255,177,0,0,0,110,255,255,255,255,232,0,0,0,0,
  1,106,223,254,183,16,0,0,0,0,0,16,0,0,0,8,177,0,0,0,
  95,251,16,0,3,239,255,160,0,29,255,252,32,0,191,255,161,0,8,255,
  248,0,0,30,255,80,0,0,3,195,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,
  255,176,0,0,15,255,176,0,0,0,0,0,0,53,0,0,0,0,0,0,
  0,0,1,223,80,0,0,0,0,0,0,0,11,255,245,0,0,0,0,0,
  0,0,143,255,246,0,0,0,0,0,0,5,255,254,64,0,0,0,0,0,
  0,46,255,210,0,0,0,0,0,0,0,223,252,16,0,0,0,0,0,0,
  0,127,160,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  1,123,239,253,165,0,0,0,0,0,110,255,255,255,255,212,0,0,0,10,
  255,255,255,255,255,255,112,0,0,175,255,255,185,172,255,255,246,0,6,255,
  255,129,0,0,59,255,255,48,30,255,246,0,0,0,0,175,255,176,111,255,
  144,0,0,0,0,29,255,242,191,255,32,0,0,0,0,7,255,247,239,253,
  0,0,0,0,0,2,255,249,255,251,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,1,255,251,223,253,0,0,0,0,0,3,255,249,175,255,
  48,0,0,0,0,7,255,246,111,255,160,0,0,0,0,29,255,242,30,255,
  246,0,0,0,0,175,255,160,6,255,255,145,0,0,59,255,254,32,0,175,
  255,255,185,172,255,255,246,0,0,10,255,255,255,255,255,255,96,0,0,0,
  110,255,255,255,255,212,0,0,0,0,1,123,239,253,165,0,0,0,0,0,
  0,0,1,0,0,0,0,0,0,0,108,16,0,0,0,0,0,3,255,176,
  0,0,0,0,0,29,255,249,0,0,0,0,0,159,255,193,0,0,0,0,
  5,255,251,16,0,0,0,0,46,255,160,0,0,0,0,0,143,248,0,0,
  0,0,0,0,11,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,207,254,207,255,
  16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,246,0,0,111,
  255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,252,16,0,26,
  255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,44,252,32,0,44,
  252,32,0,207,255,192,0,207,255,192,0,255,255,240,0,255,255,240,0,207,
  255,192,0,207,255,192,0,44,252,32,0,44,252,32,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,
  191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,
  0,0,0,0,191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,
  207,254,207,255,16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,
  246,0,0,111,255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,
  252,16,0,26,255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,0,
  1,16,0,0,0,0,0,0,42,255,251,48,0,5,32,0,3,239,255,255,
  250,68,143,210,0,13,255,255,255,255,255,255,248,0,4,237,66,57,255,255,
  255,144,0,0,49,0,0,58,239,198,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,
  251,0,90,239,235,96,0,0,255,251,27,255,255,255,252,32,0,255,252,223,
  255,255,255,255,209,0,255,255,255,235,154,239,255,251,0,255,255,250,16,0,
  25,255,255,64,255,255,176,0,0,0,175,255,160,255,255,32,0,0,0,47,
  255,208,255,253,0,0,0,0,13,255,240,255,251,0,0,0,0,11,255,240,
  255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,
  0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,
  0,0,11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,
  11,255,240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,
  240,255,251,0,0,0,0,11,255,240,255,251,0,0,0,0,11,255,240,0,
  0,0,0,0,0,1,214,0,0,0,0,0,0,0,0,0,0,0,0,11,
  255,96,0,0,0,0,0,0,0,0,0,0,0,143,255,245,0,0,0,0,
  0,0,0,0,0,0,5,255,255,144,0,0,0,0,0,0,0,0,0,0,
  62,255,246,0,0,0,0,0,0,0,0,0,0,1,223,254,64,0,0,0,
  0,0,0,0,0,0,0,7,255,194,0,0,0,0,0,0,0,0,0,0,
  0,0,154,16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,255,
  242,0,0,0,0,0,0,0,0,0,0,0,7,255,248,0,0,0,0,0,
  0,0,0,0,0,0,13,255,253,0,0,0,0,0,0,0,0,0,0,0,
  79,255,255,80,0,0,0,0,0,0,0,0,0,0,175,255,255,176,0,0,
  0,0,0,0,0,0,0,1,255,255,255,242,0,0,0,0,0,0,0,0,
  0,7,255,252,255,247,0,0,0,0,0,0,0,0,0,13,255,226,255,253,
  0,0,0,0,0,0,0,0,0,79,255,160,175,255,64,0,0,0,0,0,
  0,0,0,175,255,64,79,255,160,0,0,0,0,0,0,0,1,255,253,0,
  13,255,241,0,0,0,0,0,0,0,7,255,247,0,7,255,247,0,0,0,
  0,0,0,0,13,255,241,0,2,255,253,0,0,0,0,0,0,0,79,255,
  160,0,0,191,255,64,0,0,0,0,0,0,175,255,64,0,0,95,255,160,
  0,0,0,0,0,1,255,253,0,0,0,14,255,241,0,0,0,0,0,7,
  255,247,0,0,0,8,255,247,0,0,0,0,0,13,255,242,0,0,0,2,
  255,253,0,0,0,0,0,63,255,176,0,0,0,0,191,255,64,0,0,0,
  0,159,255,255,255,255,255,255,255,255,160,0,0,0,1,239,255,255,255,255,
  255,255,255,255,241,0,0,0,6,255,255,255,255,255,255,255,255,255,247,0,
  0,0,12,255,249,153,153,153,153,153,154,255,252,0,0,0,63,255,176,0,
  0,0,0,0,0,207,255,48,0,0,159,255,80,0,0,0,0,0,0,111,
  255,144,0,1,239,254,0,0,0,0,0,0,0,31,255,225,0,6,255,249,
  0,0,0,0,0,0,0,10,255,246,0,12,255,243,0,0,0,0,0,0,
  0,4,255,252,0,63,255,192,0,0,0,0,0,0,0,0,223,255,48,0,
  0,0,0,3,227,0,0,0,0,0,0,0,0,29,254,48,0,0,0,0,
  0,0,0,191,255,225,0,0,0,0,0,0,7,255,254,80,0,0,0,0,
  0,0,79,255,211,0,0,0,0,0,0,2,239,252,32,0,0,0,0,0,
  0,8,255,161,0,0,0,0,0,0,0,0,168,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,128,255,
  255,255,255,255,255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,255,
  253,153,153,153,153,153,153,153,64,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,253,153,153,153,153,153,153,144,0,255,
  255,255,255,255,255,255,255,240,0,255,255,255,255,255,255,255,255,240,0,255,
  255,255,255,255,255,255,255,240,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  251,0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,255,
  253,153,153,153,153,153,153,153,64,255,255,255,255,255,255,255,255,255,128,255,
  255,255,255,255,255,255,255,255,128,255,255,255,255,255,255,255,255,255,128,0,
  0,8,177,0,0,0,95,251,0,0,2,239,255,144,0,28,255,252,32,0,
  175,255,161,0,6,255,248,0,0,13,255,96,0,0,2,212,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  0,0,0,0,0,0,39,0,0,0,0,0,0,0,0,0,0,0,0,1,
  223,128,0,0,0,0,0,0,0,0,0,0,0,10,255,247,0,0,0,0,
  0,0,0,0,0,0,0,127,255,248,0,0,0,0,0,0,0,0,0,0,
  4,255,255,96,0,0,0,0,0,0,0,0,0,0,46,255,228,0,0,0,
  0,0,0,0,0,0,0,0,207,252,32,0,0,0,0,0,0,0,0,0,
  0,0,111,161,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,156,239,
  254,200,64,0,0,0,0,0,0,0,7,223,255,255,255,255,253,96,0,0,
  0,0,0,3,207,255,255,255,255,255,255,252,32,0,0,0,0,78,255,255,
  253,169,155,223,255,255,228,0,0,0,4,255,255,232,32,0,0,3,159,255,
  255,64,0,0,46,255,252,32,0,0,0,0,2,207,255,226,0,0,191,255,
  176,0,0,0,0,0,0,28,255,251,0,5,255,253,16,0,0,0,0,0,
  0,1,223,255,80,12,255,243,0,0,0,0,0,0,0,0,79,255,192,63,
  255,176,0,0,0,0,0,0,0,0,11,255,243,143,255,80,0,0,0,0,
  0,0,0,0,5,255,247,191,255,16,0,0,0,0,0,0,0,0,1,255,
  251,223,253,0,0,0,0,0,0,0,0,0,0,223,253,255,251,0,0,0,
  0,0,0,0,0,0,0,207,254,255,251,0,0,0,0,0,0,0,0,0,
  0,191,255,239,252,0,0,0,0,0,0,0,0,0,0,191,254,223,253,0,
  0,0,0,0,0,0,0,0,0,223,253,191,255,16,0,0,0,0,0,0,
  0,0,1,255,251,127,255,80,0,0,0,0,0,0,0,0,5,255,247,47,
  255,176,0,0,0,0,0,0,0,0,11,255,243,11,255,244,0,0,0,0,
  0,0,0,0,79,255,192,4,255,253,16,0,0,0,0,0,0,1,223,255,
  64,0,191,255,177,0,0,0,0,0,0,28,255,251,0,0,30,255,252,32,
  0,0,0,0,2,207,255,226,0,0,3,239,255,249,32,0,0,2,159,255,
  254,48,0,0,0,62,255,255,253,169,154,223,255,255,228,0,0,0,0,2,
  207,255,255,255,255,255,255,252,32,0,0,0,0,0,6,223,255,255,255,255,
  253,96,0,0,0,0,0,0,0,4,140,239,254,201,64,0,0,0,0,0,
  0,0,0,0,8,193,0,0,0,0,0,0,0,0,0,95,252,16,0,0,
  0,0,0,0,0,3,239,255,176,0,0,0,0,0,0,0,29,255,253,32,
  0,0,0,0,0,0,0,191,255,177,0,0,0,0,0,0,0,8,255,248,
  0,0,0,0,0,0,0,0,30,255,96,0,0,0,0,0,0,0,0,3,
  211,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,239,253,0,0,0,0,0,0,2,255,250,
  191,255,16,0,0,0,0,0,5,255,247,127,255,112,0,0,0,0,0,11,
  255,243,47,255,226,0,0,0,0,0,79,255,208,10,255,252,16,0,0,0,
  3,239,255,96,2,239,255,230,0,0,1,127,255,251,0,0,79,255,255,235,
  154,191,255,255,210,0,0,4,239,255,255,255,255,255,252,32,0,0,0,42,
  255,255,255,255,255,129,0,0,0,0,0,56,206,255,219,113,0,0,0,0,
  0,44,252,32,0,44,252,32,0,0,0,0,207,255,192,0,207,255,192,0,
  0,0,0,255,255,240,0,255,255,240,0,0,0,0,207,255,192,0,207,255,
  192,0,0,0,0,44,252,32,0,44,252,32,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,255,251,
  0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,
  255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,
  255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,
  0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,
  0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,
  251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,
  251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,0,0,255,251,255,251,0,0,0,0,
  0,0,0,255,251,255,251,0,0,0,0,0,0,0,255,251,239,253,0,0,
  0,0,0,0,2,255,250,191,255,16,0,0,0,0,0,5,255,247,127,255,
  112,0,0,0,0,0,11,255,243,47,255,226,0,0,0,0,0,79,255,208,
  10,255,252,16,0,0,0,3,239,255,96,2,239,255,230,0,0,1,127,255,
  251,0,0,79,255,255,235,154,191,255,255,210,0,0,4,239,255,255,255,255,
  255,252,32,0,0,0,42,255,255,255,255,255,129,0,0,0,0,0,56,206,
  255,219,113,0,0,0,0,0,0,1,16,0,0,0,0,0,0,0,0,5,
  223,254,129,0,0,112,0,0,0,0,143,255,255,254,115,75,250,0,0,0,
  5,255,255,255,255,255,255,254,32,0,0,0,175,131,38,223,255,255,228,0,
  0,0,0,4,0,0,7,223,234,32,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,255,225,0,0,0,0,0,0,0,255,251,255,251,0,0,
  0,0,0,0,0,255,251,255,255,96,0,0,0,0,0,0,255,251,255,255,
  243,0,0,0,0,0,0,255,251,255,255,253,16,0,0,0,0,0,255,251,
  255,255,255,144,0,0,0,0,0,255,251,255,255,255,245,0,0,0,0,0,
  255,251,255,253,255,254,32,0,0,0,0,255,251,255,251,127,255,192,0,0,
  0,0,255,251,255,251,11,255,248,0,0,0,0,255,251,255,251,1,239,255,
  64,0,0,0,255,251,255,251,0,79,255,225,0,0,0,255,251,255,251,0,
  8,255,251,0,0,0,255,251,255,251,0,0,207,255,112,0,0,255,251,255,
  251,0,0,46,255,243,0,0,255,251,255,251,0,0,5,255,253,16,0,255,
  251,255,251,0,0,0,175,255,160,0,255,251,255,251,0,0,0,29,255,245,
  0,255,251,255,251,0,0,0,3,255,254,32,255,251,255,251,0,0,0,0,
  127,255,192,255,251,255,251,0,0,0,0,11,255,248,255,251,255,251,0,0,
  0,0,1,239,255,255,251,255,251,0,0,0,0,0,79,255,255,251,255,251,
  0,0,0,0,0,8,255,255,251,255,251,0,0,0,0,0,0,207,255,251,
  255,251,0,0,0,0,0,0,46,255,251,255,251,0,0,0,0,0,0,5,
  255,251,255,251,0,0,0,0,0,0,0,175,251,255,251,0,0,0,0,0,
  0,0,31,251,0,0,0,1,48,0,0,0,0,0,0,0,0,11,209,0,
  0,0,0,0,0,0,0,191,251,0,0,0,0,0,0,0,4,255,255,112,
  0,0,0,0,0,0,0,94,255,244,0,0,0,0,0,0,0,3,239,254,
  32,0,0,0,0,0,0,0,45,255,176,0,0,0,0,0,0,0,1,191,
  193,0,0,0,0,0,0,0,0,8,16,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,140,239,235,
  96,11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,
  255,253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,
  42,255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,
  0,13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,
  0,4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,
  0,3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,
  0,8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,
  0,159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,
  255,255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,
  253,75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,0,2,16,0,
  0,0,0,0,0,0,0,62,160,0,0,0,0,0,0,0,2,239,247,0,
  0,0,0,0,0,0,9,255,255,48,0,0,0,0,0,0,0,159,255,209,
  0,0,0,0,0,0,0,7,255,251,0,0,0,0,0,0,0,0,94,255,
  128,0,0,0,0,0,0,0,3,239,144,0,0,0,0,0,0,0,0,40,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,1,107,239,253,165,0,0,0,0,0,110,255,255,255,
  255,195,0,0,0,10,255,255,255,255,255,254,64,0,0,175,255,254,185,172,
  255,255,226,0,6,255,255,113,0,0,60,255,251,0,30,255,244,0,0,0,
  1,223,255,48,111,255,128,0,0,0,0,95,255,144,191,255,16,0,0,0,
  0,14,255,192,239,255,255,255,255,255,255,255,255,224,255,255,255,255,255,255,
  255,255,255,240,255,255,255,255,255,255,255,255,255,224,223,255,153,153,153,153,
  153,153,153,128,191,255,32,0,0,0,0,0,0,0,111,255,144,0,0,0,
  0,0,0,0,30,255,246,0,0,0,0,38,0,0,6,255,255,145,0,0,
  6,239,144,0,0,175,255,255,201,155,239,255,247,0,0,10,255,255,255,255,
  255,255,177,0,0,0,110,255,255,255,255,232,0,0,0,0,1,106,223,254,
  183,16,0,0,0,131,0,0,0,8,253,16,0,0,143,255,192,0,0,111,
  255,249,0,0,3,239,255,96,0,0,44,255,244,0,0,1,175,254,0,0,
  0,8,246,0,0,0,0,32,0,0,0,0,0,0,0,0,0,0,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,15,255,176,0,0,
  0,0,2,16,0,0,0,0,0,0,0,0,46,160,0,0,0,0,0,0,
  0,2,223,247,0,0,0,0,0,0,0,9,255,255,64,0,0,0,0,0,
  0,0,159,255,226,0,0,0,0,0,0,0,6,255,252,0,0,0,0,0,
  0,0,0,78,255,144,0,0,0,0,0,0,0,3,223,160,0,0,0,0,
  0,0,0,0,40,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,1,123,239,253,165,0,0,0,0,
  0,110,255,255,255,255,212,0,0,0,10,255,255,255,255,255,255,112,0,0,
  175,255,255,185,172,255,255,246,0,6,255,255,129,0,0,59,255,255,48,30,
  255,246,0,0,0,0,175,255,176,111,255,144,0,0,0,0,29,255,242,191,
  255,32,0,0,0,0,7,255,247,239,253,0,0,0,0,0,2,255,249,255,
  251,0,0,0,0,0,0,255,251,255,251,0,0,0,0,0,1,255,251,223,
  253,0,0,0,0,0,3,255,249,175,255,48,0,0,0,0,7,255,246,111,
  255,160,0,0,0,0,29,255,242,30,255,246,0,0,0,0,175,255,160,6,
  255,255,145,0,0,59,255,254,32,0,175,255,255,185,172,255,255,246,0,0,
  10,255,255,255,255,255,255,96,0,0,0,110,255,255,255,255,212,0,0,0,
  0,1,123,239,253,165,0,0,0,0,0,4,112,0,0,0,0,0,0,62,
  244,0,0,0,0,0,3,239,253,16,0,0,0,0,2,207,255,176,0,0,
  0,0,0,27,255,247,0,0,0,0,0,0,175,255,48,0,0,0,0,0,
  8,255,208,0,0,0,0,0,0,127,80,0,0,0,0,0,0,2,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,
  255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,
  255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,
  0,0,0,191,255,255,251,0,0,0,0,191,255,239,252,0,0,0,0,207,
  254,207,255,16,0,0,1,255,252,143,255,128,0,0,8,255,248,63,255,246,
  0,0,111,255,243,9,255,255,218,157,255,255,144,1,207,255,255,255,255,252,
  16,0,26,255,255,255,255,161,0,0,0,73,223,253,164,0,0,0,0,0,
  0,175,253,16,0,0,0,0,0,0,7,255,255,176,0,0,0,0,0,0,
  79,255,255,249,0,0,0,0,0,2,239,254,223,255,112,0,0,0,0,29,
  255,227,28,255,245,0,0,0,0,175,253,32,1,207,255,64,0,0,5,255,
  194,0,0,27,255,208,0,0,0,172,16,0,0,0,174,48,0,0,0,1,
  0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,3,140,239,235,96,11,255,240,0,1,159,
  255,255,255,253,59,255,240,0,28,255,255,255,255,255,253,255,240,0,207,255,
  255,185,172,255,255,255,240,8,255,255,129,0,0,42,255,255,240,30,255,246,
  0,0,0,0,143,255,240,127,255,160,0,0,0,0,13,255,240,191,255,32,
  0,0,0,0,7,255,240,239,253,0,0,0,0,0,4,255,240,255,251,0,
  0,0,0,0,2,255,240,255,251,0,0,0,0,0,3,255,240,239,253,0,
  0,0,0,0,5,255,240,191,255,32,0,0,0,0,8,255,240,127,255,160,
  0,0,0,0,13,255,240,31,255,246,0,0,0,0,159,255,240,8,255,255,
  145,0,0,42,255,255,240,1,207,255,255,185,156,255,255,255,240,0,44,255,
  255,255,255,255,253,255,240,0,1,159,255,255,255,253,75,255,240,0,0,3,
  156,239,235,97,11,255,240,0,0,0,1,223,249,0,0,0,0,0,0,0,
  12,255,255,112,0,0,0,0,0,0,159,255,255,244,0,0,0,0,0,6,
  255,253,255,254,32,0,0,0,0,79,255,177,78,255,209,0,0,0,2,239,
  250,0,3,239,251,0,0,0,11,255,144,0,0,45,255,112,0,0,2,215,
  0,0,0,2,219,16,0,0,0,16,0,0,0,0,17,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
  107,239,253,165,0,0,0,0,0,110,255,255,255,255,195,0,0,0,10,255,
  255,255,255,255,254,64,0,0,175,255,254,185,172,255,255,226,0,6,255,255,
  113,0,0,60,255,251,0,30,255,244,0,0,0,1,223,255,48,111,255,128,
  0,0,0,0,95,255,144,191,255,16,0,0,0,0,14,255,192,239,255,255,
  255,255,255,255,255,255,224,255,255,255,255,255,255,255,255,255,240,255,255,255,
  255,255,255,255,255,255,224,223,255,153,153,153,153,153,153,153,128,191,255,32,
  0,0,0,0,0,0,0,111,255,144,0,0,0,0,0,0,0,30,255,246,
  0,0,0,0,38,0,0,6,255,255,145,0,0,6,239,144,0,0,175,255,
  255,201,155,239,255,247,0,0,10,255,255,255,255,255,255,177,0,0,0,110,
  255,255,255,255,232,0,0,0,0,1,106,223,254,183,16,0,0,0,0,0,
  104,132,0,0,0,0,0,7,255,255,64,0,0,0,0,95,255,255,226,0,
  0,0,4,255,255,255,253,16,0,0,62,255,229,143,255,193,0,2,223,253,
  48,5,239,251,0,12,255,177,0,0,45,255,128,4,233,0,0,0,1,172,
  16,0,32,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,255,251,0,0,0,0,0,0,
  255,251,0,0,0,0,0,0,1,223,250,0,0,0,0,0,0,0,11,255,
  255,112,0,0,0,0,0,0,159,255,255,244,0,0,0,0,0,6,255,253,
  239,254,48,0,0,0,0,79,255,193,62,255,209,0,0,0,2,239,251,16,
  3,223,251,0,0,0,10,255,144,0,0,45,255,96,0,0,2,216,0,0,
  0,1,204,16,0,0,0,16,0,0,0,0,17,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,123,239,
  253,165,0,0,0,0,0,110,255,255,255,255,212,0,0,0,10,255,255,255,
  255,255,255,112,0,0,175,255,255,185,172,255,255,246,0,6,255,255,129,0,
  0,59,255,255,48,30,255,246,0,0,0,0,175,255,176,111,255,144,0,0,
  0,0,29,255,242,191,255,32,0,0,0,0,7,255,247,239,253,0,0,0,
  0,0,2,255,249,255,251,0,0,0,0,0,0,255,251,255,251,0,0,0,
  0,0,1,255,251,223,253,0,0,0,0,0,3,255,249,175,255,48,0,0,
  0,0,7,255,246,111,255,160,0,0,0,0,29,255,242,30,255,246,0,0,
  0,0,175,255,160,6,255,255,145,0,0,59,255,254,32,0,175,255,255,185,
  172,255,255,246,0,0,10,255,255,255,255,255,255,96,0,0,0,110,255,255,
  255,255,212,0,0,0,0,1,123,239,253,165,0,0,0,0,0,0,191,251,
  0,0,0,0,0,8,255,255,144,0,0,0,0,111,255,255,246,0,0,0,
  4,255,253,223,255,64,0,0,46,255,194,28,255,226,0,1,223,251,16,1,
  191,253,16,6,255,160,0,0,10,255,96,0,136,0,0,0,0,153,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,
  251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,
  0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,
  251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,
  0,191,255,255,251,0,0,0,0,191,255,255,251,0,0,0,0,191,255,239,
  252,0,0,0,0,207,254,207,255,16,0,0,1,255,252,143,255,128,0,0,
  8,255,248,63,255,246,0,0,111,255,243,9,255,255,218,157,255,255,144,1,
  207,255,255,255,255,252,16,0,26,255,255,255,255,161,0,0,0,73,223,253,
  164,0,0,0,0,0,1,16,0,0,0,0,0,0,0,42,255,252,64,0,
  3,64,0,0,3,239,255,255,251,84,126,227,0,0,30,255,255,255,255,255,
  255,247,0,0,5,251,66,74,255,255,255,128,0,0,0,64,0,0,75,239,
  197,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,140,239,235,96,
  11,255,240,0,1,159,255,255,255,253,59,255,240,0,28,255,255,255,255,255,
  253,255,240,0,207,255,255,185,172,255,255,255,240,8,255,255,129,0,0,42,
  255,255,240,30,255,246,0,0,0,0,143,255,240,127,255,160,0,0,0,0,
  13,255,240,191,255,32,0,0,0,0,7,255,240,239,253,0,0,0,0,0,
  4,255,240,255,251,0,0,0,0,0,2,255,240,255,251,0,0,0,0,0,
  3,255,240,239,253,0,0,0,0,0,5,255,240,191,255,32,0,0,0,0,
  8,255,240,127,255,160,0,0,0,0,13,255,240,31,255,246,0,0,0,0,
  159,255,240,8,255,255,145,0,0,42,255,255,240,1,207,255,255,185,156,255,
  255,255,240,0,44,255,255,255,255,255,253,255,240,0,1,159,255,255,255,253,
  75,255,240,0,0,3,156,239,235,97,11,255,240,0,0,0,17,0,0,0,
  0,0,0,0,0,93,255,232,16,0,7,0,0,0,8,255,255,255,231,69,
  191,144,0,0,95,255,255,255,255,255,255,226,0,0,10,248,50,109,255,255,
  254,64,0,0,0,80,0,0,125,254,162,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,1,123,239,253,165,0,0,0,0,0,110,255,255,255,255,
  212,0,0,0,10,255,255,255,255,255,255,112,0,0,175,255,255,185,172,255,
  255,246,0,6,255,255,129,0,0,59,255,255,48,30,255,246,0,0,0,0,
  175,255,176,111,255,144,0,0,0,0,29,255,242,191,255,32,0,0,0,0,
  7,255,247,239,253,0,0,0,0,0,2,255,249,255,251,0,0,0,0,0,
  0,255,251,255,251,0,0,0,0,0,1,255,251,223,253,0,0,0,0,0,
  3,255,249,175,255,48,0,0,0,0,7,255,246,111,255,160,0,0,0,0,
  29,255,242,30,255,246,0,0,0,0,175,255,160,6,255,255,145,0,0,59,
  255,254,32,0,175,255,255,185,172,255,255,246,0,0,10,255,255,255,255,255,
  255,96,0,0,0,110,255,255,255,255,212,0,0,0,0,1,123,239,253,165,
  0,0,0,0,0,1,107,223,253,182,16,0,0,0,110,255,255,255,255,230,
  0,0,10,255,255,255,255,255,255,144,0,175,255,255,201,155,239,255,243,6,
  255,255,145,0,0,23,255,64,30,255,246,0,0,0,0,52,0,111,255,160,
  0,0,0,0,0,0,191,255,32,0,0,0,0,0,0,239,253,0,0,0,
  0,0,0,0,255,251,0,0,0,0,0,0,0,255,251,0,0,0,0,0,
  0,0,223,253,0,0,0,0,0,0,0,191,255,48,0,0,0,0,0,0,
  111,255,160,0,0,0,0,0,0,30,255,247,0,0,0,0,52,0,6,255,
  255,146,0,0,23,239,64,0,175,255,255,201,155,239,255,243,0,10,255,255,
  255,255,255,255,160,0,0,110,255,255,255,255,230,0,0,0,1,107,255,253,
  182,16,0,0,0,0,5,255,128,0,0,0,0,0,0,10,255,233,32,0,
  0,0,0,0,14,255,255,227,0,0,0,0,0,3,52,175,252,0,0,0,
  0,0,0,0,31,255,0,0,0,0,28,130,1,143,253,0,0,0,0,191,
  255,255,255,244,0,0,0,0,41,223,255,251,48,0,0,0,0,0,1,34,
  0,0,0,0,0,0,0,0,4,140,239,254,218,97,0,0,0,0,0,0,
  6,223,255,255,255,255,255,146,0,0,0,0,2,207,255,255,255,255,255,255,
  254,96,0,0,0,78,255,255,253,185,154,223,255,255,250,0,0,4,255,255,
  249,48,0,0,1,126,255,254,32,0,46,255,252,32,0,0,0,0,1,175,
  227,0,0,191,255,177,0,0,0,0,0,0,7,48,0,5,255,253,16,0,
  0,0,0,0,0,0,0,0,12,255,244,0,0,0,0,0,0,0,0,0,
  0,63,255,176,0,0,0,0,0,0,0,0,0,0,127,255,80,0,0,0,
  0,0,0,0,0,0,0,191,255,16,0,0,0,0,0,0,0,0,0,0,
  223,253,0,0,0,0,0,0,0,0,0,0,0,255,252,0,0,0,0,0,
  0,0,0,0,0,0,255,251,0,0,0,0,0,0,0,0,0,0,0,239,
  252,0,0,0,0,0,0,0,0,0,0,0,223,253,0,0,0,0,0,0,
  0,0,0,0,0,191,255,16,0,0,0,0,0,0,0,0,0,0,127,255,
  80,0,0,0,0,0,0,0,0,0,0,63,255,176,0,0,0,0,0,0,
  0,0,0,0,12,255,244,0,0,0,0,0,0,0,0,0,0,5,255,253,
  16,0,0,0,0,0,0,0,0,0,0,191,255,177,0,0,0,0,0,0,
  6,96,0,0,46,255,252,32,0,0,0,0,0,143,246,0,0,4,255,255,
  249,48,0,0,1,109,255,255,80,0,0,78,255,255,253,185,154,207,255,255,
  251,16,0,0,2,207,255,255,255,255,255,255,255,128,0,0,0,0,6,223,
  255,255,255,255,255,163,0,0,0,0,0,0,4,140,255,254,218,98,0,0,
  0,0,0,0,0,0,5,255,128,0,0,0,0,0,0,0,0,0,0,10,
  255,233,32,0,0,0,0,0,0,0,0,0,14,255,255,227,0,0,0,0,
  0,0,0,0,0,3,52,175,252,0,0,0,0,0,0,0,0,0,0,0,
  31,255,0,0,0,0,0,0,0,0,28,114,1,143,253,0,0,0,0,0,
  0,0,0,191,255,255,255,244,0,0,0,0,0,0,0,0,41,239,255,250,
  48,0,0,0,0,0,0,0,0,0,1,34,0,0,0,0,0,0,0,0,
  2,207,194,0,0,0,0,0,12,255,252,0,0,0,0,0,15,255,255,0,
  0,0,0,0,12,255,252,0,0,0,0,0,2,207,194,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,1,255,240,0,0,0,0,0,
  2,255,240,0,0,0,0,0,3,255,240,0,0,0,0,0,92,255,240,0,
  0,0,0,44,255,255,240,0,0,0,2,223,255,255,240,0,0,0,11,255,
  252,65,0,0,0,0,79,255,176,0,0,0,0,0,175,255,32,0,0,0,
  0,0,223,253,0,0,0,0,0,0,255,251,0,0,0,0,0,0,255,252,
  0,0,0,0,2,0,223,255,16,0,0,0,63,96,175,255,112,0,0,1,
  223,248,95,255,246,0,0,60,255,249,11,255,255,218,156,255,255,209,2,223,
  255,255,255,255,254,48,0,43,255,255,255,255,178,0,0,0,90,239,253,164,
  0,0,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,0,0,0,
  0,0,0,0,0,0,0,0,0,15,255,176,15,255,176,15,255,176,15,255,
  176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,
  255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,15,255,176,
  15,255,176,15,255,176,0,6,206,236,96,0,1,207,255,255,252,16,12,255,
  255,255,255,176,95,255,113,23,255,245,191,248,0,0,143,251,239,242,0,0,
  47,254,255,240,0,0,15,255,239,242,0,0,47,254,191,248,0,0,143,251,
  111,255,96,6,255,246,12,255,255,255,255,192,2,207,255,255,252,32,0,23,
  207,252,113,0,44,252,32,207,255,192,255,255,240,207,255,192,44,252,32,
};
// ---- FlexOS_Ultra_Font.h: fontIdx
static int fontIdx(uint32_t cp){
  if(cp>=0x20 && cp<=0x7E) return cp-0x20;
  switch(cp){
    case 0xE1: return 95;
    case 0xE9: return 96;
    case 0xED: return 97;
    case 0xF3: return 98;
    case 0xFA: return 99;
    case 0xFC: return 100;
    case 0xF1: return 101;
    case 0xC1: return 102;
    case 0xC9: return 103;
    case 0xCD: return 104;
    case 0xD3: return 105;
    case 0xDA: return 106;
    case 0xDC: return 107;
    case 0xD1: return 108;
    case 0xE0: return 109;
    case 0xE8: return 110;
    case 0xEC: return 111;
    case 0xF2: return 112;
    case 0xF9: return 113;
    case 0xE2: return 114;
    case 0xEA: return 115;
    case 0xEE: return 116;
    case 0xF4: return 117;
    case 0xFB: return 118;
    case 0xE3: return 119;
    case 0xF5: return 120;
    case 0xE7: return 121;
    case 0xC7: return 122;
    case 0xBF: return 123;
    case 0xA1: return 124;
    case 0xB0: return 125;
    case 0xB7: return 126;
    default: return 0x3F-0x20;
  }
}
// ---- FlexOS_Ultra_Font.h: FONT_HPS
#define FONT_HPS 8    // px de alto por unidad de 'size' logico
// ---- FlexOS_Ultra_Font.h: FONT_CAPOFF
#define FONT_CAPOFF 11 // alinea el tope de mayusculas/digitos con y (como el 5x7)
// ---- FlexOS_Ultra_Font.h: fgPix
static inline uint8_t fgPix(const FGlyph* g, int x, int y){
  if(x < 0 || y < 0 || x >= g->w || y >= g->h) return 0;
  int bpr = (g->w + 1) >> 1;
  uint8_t b = FBM[g->off + (uint32_t)y * bpr + (x >> 1)];
  return (x & 1) ? (b & 0x0F) : (b >> 4);
}
// ---- FlexOS_Ultra_Font.h: fontSc
static inline float fontSc(int size){ return (float)(size * FONT_HPS) / (float)FONT_LINEH; }
// ---- FlexOS_Ultra_Font.h: utf8Decode
static uint32_t utf8Decode(const char** ps){
  const uint8_t* s = (const uint8_t*)*ps;
  const uint8_t b = *s++;
  uint32_t cp; int extra;
  if(b < 0x80){ *ps = (const char*)s; return b; }
  if((b & 0xE0) == 0xC0){ cp = b & 0x1F; extra = 1; }
  else if((b & 0xF0) == 0xE0){ cp = b & 0x0F; extra = 2; }
  else if((b & 0xF8) == 0xF0){ cp = b & 0x07; extra = 3; }
  else { *ps = (const char*)s; return 0xFFFD; }            // byte suelto o invalido
  for(int k = 0; k < extra; k++){
    // Secuencia cortada: se para SIN comerse el byte siguiente (puede ser el
    // terminador, o el principio del caracter que viene).
    if((*s & 0xC0) != 0x80){ *ps = (const char*)s; return 0xFFFD; }
    cp = (cp << 6) | (uint32_t)(*s++ & 0x3F);
  }
  *ps = (const char*)s;
  return cp;
}
// ---- FlexOS_Ultra_Font.h: cpZeroWidth
static inline bool cpZeroWidth(uint32_t cp){
  return cp == 0x00AD || (cp >= 0x200B && cp <= 0x200F) || cp == 0x2060 ||
         cp == 0xFEFF || (cp >= 0xFE00 && cp <= 0xFE0F);
}
// ---- FlexOS_Ultra_Font.h: cpFold
static uint32_t cpFold(uint32_t cp){
  if(cp < 0x80) return cp;
  if(cp >= 0x2000 && cp <= 0x200A) return ' ';           // espacios de ancho fijo
  switch(cp){
    case 0x00A0: case 0x1680: case 0x202F: case 0x205F: case 0x3000: return ' ';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return '-';
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x00AB: case 0x00BB: case 0x2033: return '"';
    case 0x2022: case 0x2027: case 0x2219: return 0xB7;    // vinetas -> punto medio
    case 0x2026: return '.';
    default: return cp;
  }
}
// ---- FlexOS_Ultra_Font.h: nextCP
static uint32_t nextCP(const char** ps){
  for(;;){
    const uint32_t cp = utf8Decode(ps);
    if(!cpZeroWidth(cp)) return cpFold(cp);
    if(!**ps) return ' ';
  }
}
// ---- FlexOS_Ultra_Font.h: GLYPH_COLS_MAX
#define GLYPH_COLS_MAX 128
// ---- FlexOS_Ultra_Font.h: gGlyphCx
static int16_t gGlyphCx[GLYPH_COLS_MAX];
// ---- FlexOS_Ultra_Font.h: gGlyphFx
static float   gGlyphFx[GLYPH_COLS_MAX];
// ---- FlexOS_Ultra_Font.h: drawGlyphScaled
static void drawGlyphScaled(int px0, int py0, const FGlyph* g, float sc, uint16_t col, uint8_t alpha){
  if(g->w == 0) return;
  int tw = (int)(g->w * sc + 0.999f), th = (int)(g->h * sc + 0.999f);
  if(tw <= 0 || th <= 0) return;
  // RECORTE POR CAJA, ANTES DE RASTERIZAR.
  //
  // Casi todo el sistema repinta por BANDAS (regiones sucias) y las listas
  // desplazables dibujan sus filas dentro de un viewport recortado. En los dos
  // casos hay texto cuyo glifo cae ENTERO fuera de la banda: la version
  // anterior lo rasterizaba completo -- interpolacion bilineal de ~700 pixeles,
  // con cuatro lecturas del master y seis multiplicaciones cada uno -- para que
  // pxA() los descartase despues uno a uno. Comprobar la caja aqui cuesta cuatro
  // comparaciones y ahorra el glifo entero. No cambia ni un pixel: solo se salta
  // trabajo cuyo resultado ya se estaba tirando.
  //
  // En landscape putPhys()/putPhysA() mapean (lx,ly) a la fila FISICA lx y solo
  // aplican gClipY0/gClipY1 sobre ella (gClipX0/gClipX1 no intervienen), por eso
  // los dos modos se comprueban por separado.
  if(gLand){
    if(px0 + tw <= 0 || px0 >= SCR_H || py0 + th <= 0 || py0 >= SCR_W) return;
    if(px0 + tw <= gClipY0 || px0 > gClipY1) return;
  } else {
    if(px0 + tw <= 0 || px0 >= SCR_W || py0 + th <= 0 || py0 >= SCR_H) return;
    if(py0 + th <= gClipY0 || py0 > gClipY1) return;
    if(px0 + tw <= gClipX0 || px0 > gClipX1) return;
  }
  // Y AHORA EL RECORTE FINO, por filas y columnas del propio glifo.
  //
  // La comprobacion de caja de arriba solo descarta el glifo que cae ENTERO
  // fuera. El que cruza el borde de la banda -- la primera y la ultima fila de
  // cualquier lista con scroll, el texto que asoma por el borde de una tarjeta,
  // cualquier cabecera en un repintado parcial -- se rasterizaba COMPLETO y
  // pxA() iba tirando pixel a pixel lo que sobraba: cuatro lecturas del master,
  // seis multiplicaciones y una interpolacion bilineal en coma flotante por
  // cada pixel descartado. Aqui se acota el recorrido a lo que pxA() va a
  // aceptar, que es exactamente el mismo conjunto de pixeles: no cambia ni uno.
  //
  // Los dos modos se calculan por separado por el mismo motivo que la caja: en
  // landscape la coordenada que decide la fila FISICA (y por tanto el recorte
  // vertical) es px0+tx, no py0+ty.
  int tx0 = 0, tx1 = tw - 1, ty0 = 0, ty1 = th - 1;
  if(gLand){
    int lo = gClipY0 > 0 ? gClipY0 : 0, hi = gClipY1 < SCR_H - 1 ? gClipY1 : SCR_H - 1;
    if(px0 + tx0 < lo) tx0 = lo - px0;
    if(px0 + tx1 > hi) tx1 = hi - px0;
    if(py0 + ty0 < 0) ty0 = -py0;
    if(py0 + ty1 > SCR_W - 1) ty1 = SCR_W - 1 - py0;
  } else {
    int xlo = gClipX0 > 0 ? gClipX0 : 0, xhi = gClipX1 < SCR_W - 1 ? gClipX1 : SCR_W - 1;
    int ylo = gClipY0 > 0 ? gClipY0 : 0, yhi = gClipY1 < SCR_H - 1 ? gClipY1 : SCR_H - 1;
    if(px0 + tx0 < xlo) tx0 = xlo - px0;
    if(px0 + tx1 > xhi) tx1 = xhi - px0;
    if(py0 + ty0 < ylo) ty0 = ylo - py0;
    if(py0 + ty1 > yhi) ty1 = yhi - py0;
  }
  if(tx0 > tx1 || ty0 > ty1) return;

  const bool tbl = (tw <= GLYPH_COLS_MAX);
  if(tbl) for(int tx = tx0; tx <= tx1; tx++){
    float fx = tx / sc; int x0 = (int)fx;
    gGlyphCx[tx] = (int16_t)x0; gGlyphFx[tx] = fx - x0;
  }
  for(int ty = ty0; ty <= ty1; ty++){
    float fy = ty / sc; int y0 = (int)fy; float dyf = fy - y0;
    for(int tx = tx0; tx <= tx1; tx++){
      int x0; float dxf;
      if(tbl){ x0 = gGlyphCx[tx]; dxf = gGlyphFx[tx]; }
      else   { float fx = tx / sc; x0 = (int)fx; dxf = fx - x0; }
      float a00 = fgPix(g, x0, y0),     a10 = fgPix(g, x0 + 1, y0);
      float a01 = fgPix(g, x0, y0 + 1), a11 = fgPix(g, x0 + 1, y0 + 1);
      float top = a00 * (1 - dxf) + a10 * dxf;
      float bot = a01 * (1 - dxf) + a11 * dxf;
      float cov = (top * (1 - dyf) + bot * dyf) * 17.0f;   // 0..255
      if(cov > 4.0f){
        int a = (int)(cov * alpha / 255.0f); if(a > 255) a = 255;
        pxA(px0 + tx, py0 + ty, col, (uint8_t)a);
      }
    }
  }
}
// ---- FlexOS_Ultra_Font.h: textW
static int textW(const char* s, int size){
  if(size <= 1){                 // texto minusculo: bitmap 5x7 nitido (6px monoespaciado)
    int n = 0;
    while(*s){ (void)nextCP(&s); n++; }        // MISMA cuenta que drawTextA: un caracter, 6 px
    return n > 0 ? n * 6 - 1 : 0;
  }
  float sc = fontSc(size), w = 0;
  while(*s){ uint32_t cp = nextCP(&s); w += FG[fontIdx(cp)].adv * sc; }
  return (int)(w + 0.5f);
}
// ---- FlexOS_Ultra_Font.h: drawTextA
static int drawTextA(int x, int y, const char* s, int size, uint16_t col, uint8_t alpha){
  if(size <= 1){
    while(*s){
      const uint32_t cp = nextCP(&s);
      uint8_t base, acc; mapCP(cp, base, acc);
      drawGlyphSmooth(x, y, base, 1, col, alpha);     // 1:1 = nitido
      if(acc) drawAccent(x, y, 1, acc, col);
      x += 6;
    }
    return x;
  }
  float sc = fontSc(size), penx = x;
  while(*s){
    uint32_t cp = nextCP(&s);
    const FGlyph* g = &FG[fontIdx(cp)];
    drawGlyphScaled((int)(penx + g->bx * sc + 0.5f), y + (int)((g->topoff - FONT_CAPOFF) * sc + 0.5f), g, sc, col, alpha);
    penx += g->adv * sc;
  }
  return (int)(penx + 0.5f);
}
// ---- FlexOS_Ultra_Font.h: drawText
static int  drawText(int x, int y, const char* s, int size, uint16_t col){ return drawTextA(x, y, s, size, col, 255); }
// ---- FlexOS_Ultra_Font.h: drawTextC
static void drawTextC(int cx, int y, const char* s, int size, uint16_t col){ drawTextA(cx - textW(s, size) / 2, y, s, size, col, 255); }
// ---- FlexOS_Ultra_Font.h: drawTextCA
static void drawTextCA(int cx, int y, const char* s, int size, uint16_t col, uint8_t a){ drawTextA(cx - textW(s, size) / 2, y, s, size, col, a); }
// ---- FlexOS_Ultra_Font.h: drawTextR
static void drawTextR(int rx, int y, const char* s, int size, uint16_t col){ drawTextA(rx - textW(s, size), y, s, size, col, 255); }
