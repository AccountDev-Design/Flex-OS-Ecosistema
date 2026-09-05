// #############################################################
// ##  FLEX OS ULTRA  ·  APPS BASICAS: Calculadora, Calendario, Bienestar y Galeria
// ##  ----------------------------------------------------------
// ##  Las apps que usan el marco estandar sin logica pesada propia. El
// ##  cuerpo real de Galeria vive en FlexOS_Ultra_AppGallery.h.
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
#include "FlexOS_Ultra_AppSettings.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP CALCULADORA  (Milestone 2)  ·  app normal (marco estandar)
// #############################################################
static char   calcDisp[24] = "0";
static double calcAcc = 0;
static char   calcOp = 0;          // 0,'+','-','x','/'
static bool   calcFresh = true;    // el proximo digito empieza entrada nueva

static const char* CALC_LBL[5][4] = {
  {"C","+/-","%","/"}, {"7","8","9","x"}, {"4","5","6","-"},
  {"1","2","3","+"},   {"0",".","=","DEL"} };

static bool calcErr = false;       // el display muestra "Error" (division por cero / desbordamiento)

// Finito sin depender de macros de math.h (a prueba de .ino):
// NaN falla (v == v); los infinitos fallan el rango.
static inline bool calcFinite(double v){ return (v == v) && (v > -1.0e308) && (v < 1.0e308); }

static void calcFmt(double v){
  if(!calcFinite(v)){ snprintf(calcDisp, sizeof(calcDisp), "Error"); calcErr = true; return; }
  calcErr = false;
  if(v == 0) v = 0;                 // evita "-0"
  snprintf(calcDisp, sizeof(calcDisp), "%g", v);
}
static double calcCompute(double a, double b, char op){
  // Antes 5/0 devolvia 0 en silencio: una respuesta falsa presentada como buena.
  // Ahora se propaga un NaN y calcFmt lo convierte en "Error".
  switch(op){ case '+': return a + b; case '-': return a - b;
              case 'x': return a * b; case '/': return (b != 0) ? (a / b) : (double)NAN; }
  return b;
}
static void calcKey(char k){
  // Con "Error" en pantalla, el estado numerico no vale: cualquier entrada de
  // numero limpia primero. Los operadores se ignoran (no hay operando valido).
  if(calcErr){
    if(k == 'c' || (k >= '0' && k <= '9') || k == '.'){
      strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; calcErr = false;
      if(k == 'c') return;
    } else return;
  }
  int L = strlen(calcDisp);
  if(k >= '0' && k <= '9'){
    if(calcFresh || (L == 1 && calcDisp[0] == '0')){ calcDisp[0] = k; calcDisp[1] = 0; }
    else if(L < 16){ calcDisp[L] = k; calcDisp[L + 1] = 0; }
    calcFresh = false;
  } else if(k == '.'){
    if(calcFresh){ strcpy(calcDisp, "0."); calcFresh = false; }
    else if(!strchr(calcDisp, '.') && L < 15){ calcDisp[L] = '.'; calcDisp[L + 1] = 0; }
  } else if(k == 'c'){ strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; }
  else if(k == '\b'){ if(!calcFresh && L > 0){ calcDisp[L - 1] = 0; if(calcDisp[0] == 0) strcpy(calcDisp, "0"); } }
  else if(k == 'n'){
    if(strcmp(calcDisp, "0") != 0){
      if(calcDisp[0] == '-') memmove(calcDisp, calcDisp + 1, strlen(calcDisp));
      else { memmove(calcDisp + 1, calcDisp, strlen(calcDisp) + 1); calcDisp[0] = '-'; }
    }
  } else if(k == '%'){ calcFmt(atof(calcDisp) / 100.0); calcFresh = true; }
  else if(k == '+' || k == '-' || k == 'x' || k == '/'){
    double cur = atof(calcDisp);
    calcAcc = (calcOp && !calcFresh) ? calcCompute(calcAcc, cur, calcOp) : cur;
    calcOp = k; calcFresh = true; calcFmt(calcAcc);
  } else if(k == '='){
    if(calcOp){ calcAcc = calcCompute(calcAcc, atof(calcDisp), calcOp); calcFmt(calcAcc); calcOp = 0; }
    calcFresh = true;
  }
}
static void calcKeyFromLabel(const char* t){
  char k;
  if(!strcmp(t, "C")) k = 'c';
  else if(!strcmp(t, "+/-")) k = 'n';
  else if(!strcmp(t, "DEL")) k = '\b';
  else k = t[0];   // digitos, '.', '%', '/', 'x', '-', '+', '='
  calcKey(k);
}
// ---- Layout ADAPTATIVO (app de referencia del modo embebido) ----
// Nada de constantes de pantalla completa: todo sale del lienzo logico
// (gAppW x [WIN_TOP..WIN_BOT]), que a pantalla completa es la pantalla menos su
// chrome y dentro de una ventana de DeX es el area de cliente. La misma
// funcion sirve para las dos situaciones y para cualquier tamano intermedio.
// CALCULADORA · adaptativa (app de referencia).
//   Esencial   : rejilla 5x4 de teclas -- tiene PRIORIDAD sobre el display.
//   Opcional 1 : display -- cede alto a la rejilla y llega a omitirse si el
//                lienzo no da para las 5 filas.
//   Opcional 2 : panel lateral de memoria e historial -- aparece cuando el
//                ancho da para la rejilla con teclas de >= 44 px MAS 150 px de
//                panel. Por debajo de ese umbral desaparece entero, nunca
//                encogido a medias.
#define CALC_SIDE_MIN 150
#define CALC_KEY_MIN   44
// Ancho que reserva el panel lateral (0 si no toca mostrarlo).
static int calcSideW(){
  int w = gAppW, pad = w / 30; if(pad < 4) pad = 4; if(pad > 16) pad = 16;
  int gap = w / 40; if(gap < 3) gap = 3; if(gap > 12) gap = 12;
  int need = 4 * CALC_KEY_MIN + 3 * gap + 2 * pad + CALC_SIDE_MIN + gap;
  if(w < need) return 0;
  int sw = w / 4; if(sw < CALC_SIDE_MIN) sw = CALC_SIDE_MIN; if(sw > 260) sw = 260;
  return sw;
}
static void calcBox(int &bx, int &by, int &bw, int &bh){
  bx = 0; by = WIN_TOP; bw = gAppW; bh = WIN_BOT - WIN_TOP;
  int sw = calcSideW();
  if(sw > 0) bw -= sw;                          // la calculadora cede sitio al panel
  if(bw < 40) bw = 40;
  if(bh < 60) bh = 60;
}
// Reparto vertical. La rejilla tiene PRIORIDAD: primero se asegura de que las 5
// filas caben, y el display se queda con lo que sobre (hasta desaparecer en
// lienzos absurdamente bajos). Al reves -- display con alto minimo fijo -- la
// rejilla se salia por debajo del marco en ventanas achatadas.
static void calcLayout(int &m, int &gap, int &dh, int &bwv, int &bhv){
  int bx, by, bw, bh; calcBox(bx, by, bw, bh);
  (void)bx; (void)by;
  m = gAppW / 30; if(m < 4) m = 4; if(m > 16) m = 16;
  if(4 * m > bh){ m = bh / 8; if(m < 2) m = 2; }
  gap = gAppW / 40; if(gap < 3) gap = 3; if(gap > 12) gap = 12;
  int avail = bh - 2 * m; if(avail < 20) avail = 20;
  int need = 5 * 6 + 4 * gap;                   // minimo vital de la rejilla
  while(gap > 2 && need > avail * 3 / 4){ gap--; need = 5 * 6 + 4 * gap; }
  dh = avail / 5;                               // el display aspira a ~1/5
  if(dh > 120) dh = 120;
  int maxDh = avail - m - need;                 // ...pero nunca a costa de la rejilla
  if(dh > maxDh) dh = maxDh;
  if(dh < 0) dh = 0;
  int gridH = avail - dh - (dh > 0 ? m : 0);
  bhv = (gridH - 4 * gap) / 5; if(bhv < 6) bhv = 6;
  bwv = (bw - 2 * m - 3 * gap) / 4; if(bwv < 6) bwv = 6;
}
static void calcDispRect(int &x, int &y, int &w, int &h){
  int bx, by, bw, bh; calcBox(bx, by, bw, bh);
  int m, gap, dh, bwv, bhv; calcLayout(m, gap, dh, bwv, bhv);
  (void)bh;
  x = bx + m; y = by + m; w = bw - 2 * m; h = dh;
}
static void calcGrid(int &gx, int &gy, int &bw, int &bh, int &gap){
  int bx, by, bwx, bhx; calcBox(bx, by, bwx, bhx);
  (void)bwx; (void)bhx;
  int m, dh, bwv, bhv; calcLayout(m, gap, dh, bwv, bhv);
  gx = bx + m;
  gy = by + m + dh + (dh > 0 ? m : 0);
  bw = bwv; bh = bhv;
}
// Tamano de fuente segun el boton, para que la etiqueta nunca se salga.
static int calcFontFor(int bw, int bh, const char* t){
  int lim = (bw < bh) ? bw : bh;
  int fs = lim >= 56 ? 4 : lim >= 38 ? 3 : lim >= 24 ? 2 : 1;
  if(strlen(t) > 1 && fs > 1) fs--;
  while(fs > 1 && textW(t, fs) > bw - 6) fs--;
  return fs;
}
static int calcKeyY0 = 0, calcKeyY1 = 0;
static void calcRender(){
  // A pantalla completa se sigue componiendo en lockBuf y volcando de una
  // pasada (anti-parpadeo, igual que antes). Embebida NO: el lienzo de la
  // ventana ya se compone entero fuera de pantalla y se vuelca de golpe, y
  // ademas fbCopyBand copia FILAS FISICAS, que con el lienzo rotado de una
  // ventana apaisada no corresponden a las filas logicas.
  bool host = gHosted;
  setBuf(host ? fb : lockBuf);                 // hospedada, fb ya apunta al lienzo
  // Se limpia el LIENZO ENTERO, no solo la caja de la calculadora. calcBox le
  // resta el ancho del panel lateral, asi que limpiar solo esa caja dejaba sin
  // tocar la franja del panel: al cruzar el breakpoint quedaban ahi las
  // tarjetas del frame anterior, y el fundido se mezclaba contra esa basura en
  // vez de contra el fondo. Eso era el ghosting.
  int fx, fy, fw, fh; uiBox(fx, fy, fw, fh);
  fillRect(fx, fy, fw, fh, WIN_BG);
  int bx, by, bw0, bh0; calcBox(bx, by, bw0, bh0);
  int dx, dy, dw, dh; calcDispRect(dx, dy, dw, dh);
  if(dh > 0){
    int drad = dh / 5; if(drad > 14) drad = 14; if(drad < 2) drad = 2;
    uiSurface(dx, dy, dw, dh, drad, UIS_CARD);   // display: material del sistema
    int dfs = dh >= 90 ? 5 : dh >= 64 ? 4 : dh >= 40 ? 3 : 2;
    while(dfs > 1 && textW(calcDisp, dfs) > dw - 20) dfs--;
    drawTextR(dx + dw - 10, dy + dh / 2 - dfs * 4, calcDisp, dfs, TH_TXT);
  }
  int gx, gy, bw, bh, gap; calcGrid(gx, gy, bw, bh, gap);
  calcKeyY0 = gy - 4; calcKeyY1 = gy + 5 * (bh + gap) + 4;
  if(calcKeyY1 > gAppH) calcKeyY1 = gAppH;
  int rad = bw / 6; if(rad > 14) rad = 14; if(rad < 3) rad = 3;
  for(int r = 0; r < 5; r++) for(int c = 0; c < 4; c++){
    int x = gx + c * (bw + gap), y = gy + r * (bh + gap);
    const char* tl = CALC_LBL[r][c];
    // La columna de operadores conserva su NARANJA de marca (identidad de la
    // app, igual en las dos apariencias). El resto de teclas son superficies.
    uint16_t bg;
    if(c == 3 || (r == 4 && c == 2)) bg = rgb565(245,150,40);
    else if(r == 0)                  bg = TH_SURF2;
    else                             bg = thCard2();
    // drawLiquidGlassPanel solo es correcto en portrait sin rotar; con lienzo
    // apaisado (ventana ancha) se usa el relleno plano.
    if(uiGlass && !gLand) drawLiquidGlassPanel(x, y, bw, bh, rad, bg);
    else fillRoundRect(x, y, bw, bh, rad, bg);
    int fs = calcFontFor(bw, bh, tl);
    bool acc = (c == 3 || (r == 4 && c == 2));
    drawTextC(x + bw / 2, y + bh / 2 - fs * 4 + 1, tl, fs, acc ? TH_ONACC : TH_TXT);
  }
  // Panel lateral opcional: memoria e historial. Aparece/desaparece entero.
  int sw = calcSideW();
  uint8_t aSide = uiSection(0, sw > 0);
  if(aSide && sw > 0){
    int pad, gapL, dhL, bwL, bhL; calcLayout(pad, gapL, dhL, bwL, bhL);
    (void)gapL; (void)dhL; (void)bwL; (void)bhL;
    int sx = bx + bw0, sy = by, shh = bh0;
    uiRectA(sx + pad / 2, sy + pad, sw - pad, shh - 2 * pad, pad, thCard(), aSide);
    int ix = sx + pad, iw = sw - 2 * pad, iy = sy + pad * 2;
    uiTextC(sx + sw / 2, iy, "Memoria", uiFontFit("Memoria", iw, 3), TH_TXT2, aSide);
    iy += uiLineH(3) + pad;
    const char* mk[3] = { "MC", "MR", "M+" };
    int mh = (shh / 8) < 30 ? 30 : (shh / 8);
    for(int i = 0; i < 3; i++){
      uiRectA(ix, iy, iw, mh, mh / 4, TH_SURF2, aSide);
      uiTextC(sx + sw / 2, iy + mh / 2 - uiLineH(2), mk[i], uiFontFit(mk[i], iw - 8, 3), TH_TXT, aSide);
      iy += mh + pad / 2;
    }
    iy += pad;
    uiTextC(sx + sw / 2, iy, "Resultado", uiFontFit("Resultado", iw, 2), TH_TXT2, aSide);
    iy += uiLineH(2) + 4;
    uiTextC(sx + sw / 2, iy, calcDisp, uiFontFit(calcDisp, iw, 3), TH_ACCS, aSide);
  }
  if(!host){ setBuf(fb); fbCopyBand(lockBuf, WIN_TOP, WIN_BOT - 1); }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void calcRenderDisplay(){                       // solo el display (al teclear) -> responsivo
  // Dos dibujos SEPARADOS directo en
  // fb (fillRoundRect + drawTextR) antes de un solo flxFlush. Como esta
  // funcion se llama en CADA tecla tocada, era el candidato mas probable
  // para el "parpadeo al hacer algo en una app" -- se nota mucho mas que
  // el tirador porque pasa constantemente, no cada 120ms. Se compone en
  // lockBuf (igual que calcRender() ya hace) y se vuelca de una pasada.
  bool host = gHosted;
  int dx, dy, dw, dh; calcDispRect(dx, dy, dw, dh);
  if(dh <= 0) return;                            // lienzo sin sitio para el display
  int y0 = dy - 2, y1 = dy + dh + 2;
  setBuf(host ? fb : lockBuf);
  fillRect(dx - 2, y0, dw + 4, y1 - y0, WIN_BG);  // borra el valor anterior
  int drad = dh / 5; if(drad > 14) drad = 14; if(drad < 2) drad = 2;
  uiSurface(dx, dy, dw, dh, drad, UIS_CARD);     // display: material del sistema
  int dfs = dh >= 90 ? 5 : dh >= 64 ? 4 : dh >= 40 ? 3 : 2;
  while(dfs > 1 && textW(calcDisp, dfs) > dw - 20) dfs--;
  drawTextR(dx + dw - 10, dy + dh / 2 - dfs * 4, calcDisp, dfs, TH_TXT);
  if(!host){ fbCopyBand(lockBuf, y0, y1); setBuf(fb); }
  flxFlush(y0, y1);
}
static void calcEnter(){
  // Primera apertura tras el arranque: se recupera la operacion visible. Si no
  // hay sesion valida, la calculadora arranca en "0" como siempre.
  bool first = !gSessLoaded[IC_CALC];
  if(!gRelayout && !first){ strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; calcErr = false; }
  appLoadSessionOnce(IC_CALC);
  calcRender();                                   // re-maquetado: conserva el display
}
static void calcTick(){
  if(!T.tap) return;
  int gx, gy, bw, bh, gap; calcGrid(gx, gy, bw, bh, gap);
  for(int r = 0; r < 5; r++) for(int c = 0; c < 4; c++){
    int x = gx + c * (bw + gap), y = gy + r * (bh + gap);
    if(T.x >= x && T.x <= x + bw && T.y >= y && T.y <= y + bh){
      calcKeyFromLabel(CALC_LBL[r][c]); calcRenderDisplay();
      sessMarkDirty(IC_CALC);                 // la operacion visible es estado de sesion
      return;
    }
  }
}

// #############################################################
// ##  CALCULADORA · CICLO DE VIDA Y SESION
// ##  Se conserva la OPERACION VISIBLE: display, acumulador, operador
// ##  pendiente y si el proximo digito empieza una entrada nueva. Sin
// ##  eso, volver desde Recientes borraba la cuenta a medias.
// #############################################################
#define CALC_SESS_VER   1
#define CALC_SESS_PATH  FS_DIR_SESS "/calc.bin"
struct CalcSessV1 { double acc; char disp[24]; char op; uint8_t fresh; uint8_t err; uint8_t rsv; };

static void calcResume(){ calcRender(); }     // NO reinicia el display (a diferencia de enter)
static bool calcSaveSess(){
  if(!flexFsReady()) return true;
  CalcSessV1 v;
  memset(&v, 0, sizeof(v));
  v.acc = calcAcc; v.op = calcOp; v.fresh = calcFresh ? 1 : 0; v.err = calcErr ? 1 : 0;
  snprintf(v.disp, sizeof(v.disp), "%s", calcDisp);
  return sessWrite(CALC_SESS_PATH, CALC_SESS_VER, IC_CALC, &v, sizeof(v));
}
static void calcLoadSess(){
  if(!flexFsReady()) return;
  CalcSessV1 v;
  if(sessRead(CALC_SESS_PATH, CALC_SESS_VER, IC_CALC, &v, sizeof(v)) != sizeof(v)) return;
  v.disp[sizeof(v.disp) - 1] = 0;
  if(!v.disp[0]) return;                                  // sesion incoherente: se ignora
  if(v.op && !strchr("+-x/", v.op)) return;               // operador imposible: se ignora
  snprintf(calcDisp, sizeof(calcDisp), "%s", v.disp);
  calcAcc = v.acc; calcOp = v.op; calcFresh = v.fresh != 0; calcErr = v.err != 0;
}

// #############################################################
// ##  APPS M2: Calendario, Bienestar, Galeria (marco estandar)
// #############################################################

// ---- Calendario: vista de mes con el dia de hoy resaltado ----
// CALENDARIO · adaptativo.
//   Esencial   : rejilla del mes; celda y fuente escalan con el lienzo y la
//                rejilla siempre cabe entera (6 filas posibles).
//   Opcional 1 : panel lateral "Hoy" con el dia grande y la fecha larga --
//                aparece cuando el lienzo pasa de 430 px de ancho, que es lo
//                que necesita la rejilla (>= 28 px por celda) mas el panel
//                (>= 150 px) sin apretar ninguno de los dos.
static void calRender(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  int pad = uiPad(), gap = uiGap();
  char hdr[40]; snprintf(hdr, sizeof(hdr), "%s %d", MO_FULL[LI()][rtcMo - 1], rtcY);

  // El panel lateral solo si quedan >= 28 px por celda para la rejilla.
  int sideW = bw / 3; if(sideW > 210) sideW = 210;
  uint8_t aSide = uiSection(0, bw >= 430 && (bw - sideW - gap - 2 * pad) / 7 >= 28);
  int gridW = aSide ? (bw - sideW - gap - 2 * pad) : (bw - 2 * pad);

  int y = by + pad;
  int fsH = uiFontFit(hdr, gridW, uiFontH(bh / 12));
  drawTextC(bx + pad + gridW / 2, y, hdr, fsH, TH_TXT);
  y += uiLineH(fsH) + gap / 2;

  const char* wd[7] = { "D", "L", "M", "M", "J", "V", "S" };
  int cw = gridW / 7;
  int fsW = uiFontFit("W", cw - 2, 2);
  for(int i = 0; i < 7; i++) drawTextC(bx + pad + i * cw + cw / 2, y, wd[i], fsW, TH_TXT2);
  y += uiLineH(fsW) + gap / 2;

  int fw = ((rtcWd - (rtcD - 1)) % 7 + 7) % 7;
  int dim = daysInMonth(rtcY, rtcMo);
  int rows = (fw + dim + 6) / 7; if(rows < 1) rows = 1;
  int availH = (by + bh) - y - pad;
  int ch = availH / (rows > 0 ? rows : 1);
  if(ch < 12) ch = 12;
  int fsD = uiFontH(ch * 2 / 3);
  int rad = (cw < ch ? cw : ch) / 2 - 2; if(rad < 6) rad = 6;
  for(int d = 1; d <= dim; d++){
    int cell = fw + d - 1, r = cell / 7, c = cell % 7;
    int cx = bx + pad + c * cw + cw / 2, cy = y + r * ch;
    if(cy + ch > by + bh) break;                       // nunca fuera del marco
    if(d == rtcD) fillCircle(cx, cy + ch / 2, rad, TH_PRIM);      // dia de hoy: seleccion
    char ds[4]; snprintf(ds, sizeof(ds), "%d", d);
    drawTextC(cx, cy + ch / 2 - uiLineH(fsD) / 2, ds, fsD, d == rtcD ? TH_ONACC : TH_TXT);
  }
  if(aSide){
    int sx = bx + pad + gridW + gap, sy = by + pad;
    int shh = bh - 2 * pad;
    uiRectA(sx, sy, sideW, shh, pad, thCard(), aSide);
    uiTextC(sx + sideW / 2, sy + pad, "Hoy", uiFontFit("Hoy", sideW - 16, 3), TH_TXT2, aSide);
    char dd[8]; snprintf(dd, sizeof(dd), "%d", rtcD);
    int fsBig = uiFontFit(dd, sideW - 24, uiFontH(shh / 3));
    uiTextC(sx + sideW / 2, sy + shh / 2 - uiLineH(fsBig), dd, fsBig, TH_ACCS, aSide);
    char ld[64]; buildLongDate(ld, sizeof(ld));
    uiTextC(sx + sideW / 2, sy + shh / 2 + uiLineH(2), ld,
            uiFontFit(ld, sideW - 16, 2), TH_TXT, aSide);
  }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void calEnter(){ calRender(); }
// Reanudar es repintar: el calendario no guarda ningun estado navegable propio
// (ver H_CALEND). Existe para que el contrato sea explicito y no dependa de que
// enter() y resume() coincidan por casualidad.
static void calResume(){ calRender(); }
static void calTick(){ if(gMinChanged) calRender(); }

// ---- Bienestar: tiempo encendido + uso de memoria ----
// BIENESTAR · adaptativo.
//   Esencial   : tiempo encendido + barra de PSRAM.
//   Opcional 1 : columna derecha con RAM interna y frecuencia -- aparece
//                cuando el lienzo pasa de 400 px de ancho (dos columnas de
//                >= 190 px, que es lo minimo para que la etiqueta y el valor
//                no se pisen).
//   Opcional 2 : pie de consejo -- aparece si sobran >= 20 px al fondo.
static void bienRender(){
  setBuf(fb);
  int bx0, by, bw0, bh; uiBox(bx0, by, bw0, bh);
  fillRect(bx0, by, bw0, bh, WIN_BG);
  int pad = uiPad(), gap = uiGap();
  int y = by + pad;
  y = uiTitle(bx0, y, bw0, "Bienestar del equipo", TH_TXT, uiFontH(bh / 12));
  char up[40]; buildUptime(up, sizeof(up));
  int fsUp = uiFontFit(up, bw0 - 2 * pad, uiFontH(bh / 6));
  drawTextC(bx0 + bw0 / 2, y, up, fsUp, TH_ACCS);
  y += uiLineH(fsUp) + 2;
  drawTextC(bx0 + bw0 / 2, y, "tiempo encendido", uiFontFit("tiempo encendido", bw0 - 2 * pad, 2), TH_TXT2);
  y += uiLineH(2) + gap;

  size_t pf = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), pt = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  int usedp = pt > 0 ? (int)(100 - (uint64_t)pf * 100 / pt) : 0;
  char v[40];
  uint8_t aCol = uiSection(0, bw0 >= 400);
  int colW = aCol ? (bw0 - 2 * pad - gap) / 2 : (bw0 - 2 * pad);
  int cx1 = bx0 + pad;
  int barH = bh / 16; if(barH < 10) barH = 10; if(barH > 20) barH = 20;
  int fsL = uiFontFit("RAM interna libre", colW / 2, 2);
  drawText(cx1, y, "PSRAM", fsL, TH_TXT);
  snprintf(v, sizeof(v), "%d%% en uso", usedp);
  drawTextR(cx1 + colW, y, v, fsL, TH_TXT2);
  int barY = y + uiLineH(fsL) + 6;
  fillRoundRect(cx1, barY, colW, barH, barH / 2, TH_TRACK);                        // track apagado
  fillRoundRect(cx1, barY, colW * usedp / 100, barH, barH / 2, TH_OK);             // relleno: estado
  if(aCol){
    int cx2 = cx1 + colW + gap;
    uiText(cx2, y, "RAM interna", fsL, TH_TXT, aCol);
    snprintf(v, sizeof(v), "%u KB", (unsigned)(esp_get_free_heap_size() / 1024));
    uiTextR(cx2 + colW, y, v, fsL, TH_TXT2, aCol);
    int fr = (int)getCpuFrequencyMhz();
    uiRectA(cx2, barY, colW, barH, barH / 2, TH_TRACK, aCol);
    int pctF = fr > 360 ? 100 : fr * 100 / 360;
    uiRectA(cx2, barY, colW * pctF / 100, barH, barH / 2, TH_PRIM, aCol);
    snprintf(v, sizeof(v), "CPU %d MHz", fr);
    uiText(cx2, barY + barH + 6, v, uiFontFit(v, colW, 2), TH_TXT2, aCol);
  }
  y = barY + barH + uiLineH(2) + gap;
  uint8_t aFoot = uiSection(1, (by + bh) - y - pad >= 20);
  if(aFoot){
    const char* tip = "Recuerda descansar la vista";
    uiTextC(bx0 + bw0 / 2, by + bh - pad - uiLineH(2), tip,
            uiFontFit(tip, bw0 - 2 * pad, 2), TH_MUTE, aFoot);
  }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void bienEnter(){ bienRender(); }
static void bienTick(){ if(gMinChanged) bienRender(); }

// ---- Galeria: cuadricula de miniaturas (mini-paisajes generados) ----
// GALERIA · adaptativa.
//   Esencial   : rejilla de miniaturas. El NUMERO DE COLUMNAS se calcula con el
//                ancho real (miniatura minima legible de 92 px), asi que al
//                ensanchar la ventana no queda hueco: entran mas columnas.
//   Opcional 1 : pie con el recuento de elementos -- aparece si sobran >= 18 px.
// La GALERIA vive mas abajo, junto al resto de las pantallas que usan el kit de
// ficheros (menu de pulsacion larga, renombrar, papelera): necesita fkMenu*,
// fkAsk*, fkName* y fkTrash*, que se definen despues de este punto. Aqui solo
// quedaba su version antigua de miniaturas generadas, que ya no existe.
