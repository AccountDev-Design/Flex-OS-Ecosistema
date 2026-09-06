// #############################################################
// ##  FLEX OS ULTRA  ·  PANEL RAPIDO  ·  material Liquid Glass
// ##  ----------------------------------------------------------
// ##  La capa de vidrio del panel: composicion, cache a escala reducida y
// ##  el arrastre de la cortina sin recalcular el desenfoque por cuadro.
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
#include "FlexOS_Ultra_QuickPanel.h"   // eslabon anterior de la cadena

// #############################################################
// ##  MATERIAL LIQUID GLASS DEL PANEL
// ##  ------------------------------------------------------
// ##  TODO el panel -- cabecera, capsulas, tarjeta de controles,
// ##  sliders, modulos, editor y catalogo -- se apoya en esta
// ##  funcion. No quedan rectangulos opacos.
// ##
// ##  Reutiliza EXACTAMENTE la matematica de composicion de
// ##  drawLiquidGlassPanelEx (tinte adaptativo por luminancia,
// ##  especular blanco arriba / sombreado abajo, y highlight
// ##  direccional en los bordes con GLASS_CORNER_STRONG/WEAK),
// ##  pero SIN su copia + desenfoque por llamada: el fondo sobre
// ##  el que escribe YA esta desenfocado, porque la capa de vidrio
// ##  del panel se calcula una sola vez y a escala reducida
// ##  (qpGlassBuild/qpGlassRows, mas abajo).
// ##
// ##  Esa es toda la diferencia de coste: drawLiquidGlassPanelEx
// ##  hace 4 pasadas sobre la region (memcpy + 2 de box-blur, la
// ##  vertical con acceso en columna sobre PSRAM, + composicion) y
// ##  seis divisiones enteras por pixel; esto hace UNA pasada de
// ##  mezclas sin division. Visualmente es el mismo material.
// #############################################################
static void qpGlassSurface(int x, int y, int w, int h, int rad, uint16_t tint, int mixBase){
  if(w <= 0 || h <= 0) return;
  if(2 * rad > w) rad = w / 2;
  if(2 * rad > h) rad = h / 2;
  // TINTE ADAPTATIVO, igual que el resto del sistema: cuanto mas se parece la
  // luminancia del fondo a la del tinte, menos tinte se aplica (si no, el
  // panel se aplana en un bloque liso). Se muestrea 1 de cada 4 filas por 1 de
  // cada 8 columnas -- 1/32 de los pixeles -- y UNA vez por superficie.
  uint32_t lumaSum = 0; int lumaN = 0;
  for(int j = 0; j < h; j += 4){
    int yy = y + j;
    if(yy < 0 || yy >= SCR_H) continue;
    const uint16_t* row = gBuf + (size_t)yy * SCR_W;
    for(int i = 0; i < w; i += 8){
      int xx = x + i;
      if(xx < 0 || xx >= SCR_W) continue;
      lumaSum += (uint32_t)glassLuma(row[xx]); lumaN++;
    }
  }
  int lo = mixBase - 12, hi = mixBase + 12;
  if(lo < 0) lo = 0;
  if(hi > 255) hi = 255;
  uint8_t tintMix = (uint8_t)mixBase;
  if(lumaN > 0){
    int dif = (int)(lumaSum / (uint32_t)lumaN) - glassLuma(tint);
    if(dif < 0) dif = -dif;
    if(dif > GLASS_TINT_DIFF_MAX) dif = GLASS_TINT_DIFF_MAX;
    tintMix = (uint8_t)(lo + (dif * (hi - lo)) / GLASS_TINT_DIFF_MAX);
  }
  const uint8_t GLASS_CORNER_STRONG = 156, GLASS_CORNER_WEAK = 104;
  // El especular y el sombreado se acotan EN PIXELES (70 y 90). En un panel
  // pequeno esto es exactamente el 45%/55% de siempre; en una tarjeta de 330
  // px evita que el material se convierta en un degradado de arriba abajo que
  // se come el contraste del texto de la mitad inferior.
  int hTop = h * 45 / 100; if(hTop > 70) hTop = 70;
  int hBot = h - (h * 45 / 100); if(hBot > 90) hBot = 90;
  const int yBot = h - hBot;
  for(int j = 0; j < h; j++){
    int yy = y + j;
    if(yy < 0 || yy >= SCR_H || yy < gClipY0 || yy > gClipY1) continue;
    int ins = glInset(j, h, rad);
    uint16_t* dst = gBuf + (size_t)yy * SCR_W;
    int lx = x + ins, rx = x + w - 1 - ins;
    // Especular y sombreado del MATERIAL: constantes por FILA, asi que el
    // bucle interior solo hace mezclas. Es luz de cristal (blanco/negro), no
    // un color de tema: se aplica sobre el tinte, que si viene de la paleta.
    uint8_t sa = 0, da = 0;
    if(hTop > 0 && j < hTop)          sa = (uint8_t)(26 - (26 * j) / hTop);
    else if(hBot > 0 && j >= yBot)    da = (uint8_t)((30 * (j - yBot)) / hBot);
    int i0 = lx < gClipX0 ? gClipX0 : lx;
    int i1 = rx > gClipX1 ? gClipX1 : rx;
    if(i0 < 0) i0 = 0;
    if(i1 > SCR_W - 1) i1 = SCR_W - 1;
    for(int i = i0; i <= i1; i++){
      uint16_t out = mix565(dst[i], tint, tintMix);
      if(sa)      out = mix565(out, rgb565(255,255,255), sa);
      else if(da) out = mix565(out, rgb565(0,0,0), da);
      dst[i] = out;
    }
    // Borde del cristal: reflejo claro arriba, sombra abajo, y peso distinto
    // por lado para que las cuatro esquinas no queden iguales.
    uint16_t bcol = (j < 3) ? rgb565(255,255,255)
                            : (j < h / 2 ? rgb565(205,214,228) : rgb565(22,28,40));
    bool topZone = (j < h / 2);
    if(lx >= gClipX0 && lx <= gClipX1 && lx >= 0 && lx < SCR_W)
      dst[lx] = mix565(dst[lx], bcol, topZone ? GLASS_CORNER_STRONG : GLASS_CORNER_WEAK);
    if(rx >= gClipX0 && rx <= gClipX1 && rx >= 0 && rx < SCR_W)
      dst[rx] = mix565(dst[rx], bcol, topZone ? GLASS_CORNER_WEAK : GLASS_CORNER_STRONG);
  }
}

// Mezclas de tinte por tipo de superficie. Con Liquid Glass activo el vidrio
// deja pasar mas fondo; con el estilo Plano el mismo material se "esmerila"
// (mas tinte), pero NUNCA se vuelve un rectangulo opaco: sigue habiendo
// translucidez, especular y borde.
// Mezcla de tinte por tipo de superficie. Se eligieron mirando el contraste
// del texto sobre un wallpaper CLARO, que es el caso peor: el efecto no puede
// costar legibilidad. Con Liquid Glass activo el vidrio deja pasar mas fondo;
// con el estilo Plano el mismo material se "esmerila" mas.
// PLANO ES PLANO. Estos cuatro valores eran 176/158/205/205 con el estilo Plano:
// seguian dejando pasar fondo, o sea seguian siendo vidrio "esmerilado". El
// estilo Plano pide superficies SOLIDAS -- sin transparencia y sin brillo de
// material --, asi que ahi la mezcla es total (255) y el color es exactamente el
// de la paleta activa. Con Liquid Glass se conservan tal cual los valores
// afinados de siempre.
static inline int qpMixCard(){ return uiGlass ? 128 : 255; }   // tarjetas y modulos
static inline int qpMixTile(){ return uiGlass ? 112 : 255; }   // circulo apagado
static inline int qpMixAcc (){ return uiGlass ? 178 : 255; }   // acento (control activo)
static inline int qpMixHdr (){ return uiGlass ? 168 : 255; }   // cabeceras fijas (editor/catalogo)

// Superficie de una tarjeta o modulo: sombra muy leve (tres lineas alfa bajo
// el borde, no un rectangulo alfa del tamano de la tarjeta) + el material.
static void qpSurface(int x, int y, int w, int h, int rad, uint16_t tint){
  for(int k = 1; k <= 3; k++){
    int yy = y + h + k - 1;
    if(yy < SCR_H) hLineA(x + rad, yy, w - 2 * rad, TH_SHADOW, effShadow(46 - k * 12));
  }
  qpGlassSurface(x, y, w, h, rad, tint, qpMixCard());
}

// Alpha del destello de toque (0 = ya no destella).
static uint8_t qpFlashA(int kind, int idx){
  if(qpFlashKind != kind || qpFlashIdx != idx) return 0;
  uint32_t e = millis() - qpFlashMs;
  if(e >= QP_FLASH_MS) return 0;
  return (uint8_t)(110 * (1.0f - (float)e / QP_FLASH_MS));
}

// ---- CABECERA FIJA ---------------------------------------------------
// Hora grande, fecha compacta, estado REAL de red y los tres botones
// alineados arriba a la derecha (lapiz, apagado, engranaje), como el video.
// No hay porcentaje de bateria: este firmware no lee ningun sensor de
// bateria, y estamparlo seria inventarse un dato.
#define QP_HBTN_R   22
#define QP_HBTN_CY  50
#define QP_HBTN_X2  (SCR_W - QP_MX - QP_HBTN_R)          // engranaje
#define QP_HBTN_X1  (QP_HBTN_X2 - 52)                    // apagado
#define QP_HBTN_X0  (QP_HBTN_X1 - 52)                    // lapiz
static const int QP_HBTN_CX[3] = { QP_HBTN_X0, QP_HBTN_X1, QP_HBTN_X2 };

static void qpHeaderBtn(int i, bool danger){
  int cx = QP_HBTN_CX[i], cy = QP_HBTN_CY, r = QP_HBTN_R;
  // Los tres botones comparten la misma cara, como en el video. El apagado se
  // distingue por el GLIFO en color de peligro, no por un circulo rojo entero:
  // asi la cabecera no grita y el significado destructivo sigue ahi.
  uint16_t face = qpCard();
  qpGlassSurface(cx - r, cy - r, 2 * r, 2 * r, r, face, qpMixCard());
  if(danger) drawRoundRect(cx - r, cy - r, 2 * r, 2 * r, r, mix565(TH_BORDER, TH_DANGER, 140));
  qpIcoBgAt(cx, cy);
  uint16_t fg = danger ? TH_DANGER : TH_TXT;
  if(i == 0) qpIcoPencil(cx, cy, 26, fg);
  else if(i == 1) qpIcoPower(cx, cy, 26, fg);
  else qpIcoGear(cx, cy, 26, fg);
}

static void qpDrawHeader(){
  const int right = QP_HBTN_X0 - QP_HBTN_R - 12;      // hasta donde puede llegar el texto
  char buf[40];
  clkStrBar(buf, sizeof(buf));
  drawText(QP_MX, 22, buf, 6, TH_TXT);                // hora GRANDE (altura de caja 48 px)
  int tw = textW(buf, 6);
  // Fecha compacta a la derecha de la hora y con la misma linea base, igual
  // que en el panel de referencia ("8:23  mie, 19 ago.").
  char d[48]; buildShortDate(d, sizeof(d));
  drawTextClip(QP_MX + tw + 14, 54, d, 2, TH_TXT2, right);
  // Segunda linea: estado REAL de red. Sale de connWifiSub(), la MISMA fuente
  // que la pantalla de Conectividad -- no hay ningun dato inventado aqui, y
  // por eso tampoco hay porcentaje de bateria: este firmware no lo mide.
  char ns[64];
  if(gAirplane) snprintf(ns, sizeof(ns), "Modo avi\xC3\xB3n activo");
  else connWifiSub(ns, sizeof(ns));
  if(ns[0]) drawTextClip(QP_MX, 84, ns, 1, TH_TXT2, right);
  qpHeaderBtn(0, false);
#if POWEROFF_ON
  qpHeaderBtn(1, true);
#endif
  qpHeaderBtn(2, false);
}

// ---- MODULO EXTERIOR (capsula 2x1 / modulo ancho / slider 4x1) -------
static void qpDrawSliderBody(int x, int y, int w, int h, int id){
  const QsCtl* c = qpCtl(id);
  int th = h - 20; if(th < 40) th = 40;
  int ty = y + (h - th) / 2;
  int pct = (id == QSID_BRIGHT) ? gBright
          : (id == QSID_VOLUME) ? (int)flexAudioVolume() : 0;
  int fw = th + (w - th) * pct / 100;                     // nunca menor que el diametro
  if(fw > w) fw = w;
  qpGlassSurface(x, ty, w, th, th / 2, TH_TRACK, qpMixCard());          // pista: vidrio
  if(fw > 0) qpGlassSurface(x, ty, fw, th, th / 2, wallAccent(), qpMixAcc()); // relleno: acento DEL USUARIO
  int icx = x + th / 2, icy = ty + th / 2;
  bool onFill = (fw >= th);
  qpIcoBgAt(icx, icy);
  if(c && c->icon) c->icon(icx, icy, 30, onFill ? TH_ONACC : TH_TXT2);
  char v[16]; if(c && c->sub) c->sub(v, sizeof(v)); else v[0] = 0;
  if(v[0]) drawTextR(x + w - 20, ty + th / 2 - 8, v, 2, (fw > w - 70) ? TH_ONACC : TH_TXT2);
}

// Capsula / modulo. ori = QOR_H (icono a la izquierda) o QOR_V (icono arriba).
static void qpDrawModule(int x, int y, int w, int h, int id, int ori){
  const QsCtl* c = qpCtl(id);
  if(!c) return;
  if(c->type == QT_SLIDER){ qpDrawSliderBody(x, y, w, h, id); return; }
  bool on = (c->type == QT_TOGGLE && c->state) ? c->state() : false;
  uint16_t face = on ? qpCapOn() : qpCard();
  qpSurface(x, y, w, h, QP_RAD_S, face);
  uint16_t fg  = TH_TXT;
  uint16_t fg2 = TH_TXT2;      // sobre vidrio, TH_MUTE se pierde
  int ir = 22;
  uint16_t icoFace = on ? TH_PRIM : qpTileOff();
  uint16_t icoCol  = on ? TH_ONACC : TH_TXT2;
  char sub[64]; sub[0] = 0;
  if(c->sub) c->sub(sub, sizeof(sub));
  if(ori == QOR_V){
    int icx = x + w / 2, icy = y + 22 + ir - 8;
    qpGlassSurface(icx - ir, icy - ir, 2 * ir, 2 * ir, ir, icoFace, on ? qpMixAcc() : qpMixTile());
    qpIcoBgAt(icx, icy); if(c->icon) c->icon(icx, icy, 30, icoCol);
    drawTextC(icx, icy + ir + 8, c->title, 1, fg);
    if(sub[0]) drawTextC(icx, icy + ir + 22, sub, 1, fg2);
  } else {
    int icx = x + 14 + ir, icy = y + h / 2;
    qpGlassSurface(icx - ir, icy - ir, 2 * ir, 2 * ir, ir, icoFace, on ? qpMixAcc() : qpMixTile());
    qpIcoBgAt(icx, icy); if(c->icon) c->icon(icx, icy, 30, icoCol);
    int tx = icx + ir + 12, right = x + w - 12;
    if(sub[0]){
      drawTextClip(tx, icy - 17, c->title, 2, fg, right);
      drawTextClip(tx, icy + 5, sub, 1, fg2, right);
    } else {
      drawTextClip(tx, icy - 8, c->title, 2, fg, right);
    }
  }
  uint8_t fa = qpFlashA(1, id);
  if(fa) fillRoundRectA(x, y, w, h, QP_RAD_S, TH_TXT, fa);
}

// ---- TARJETA EXPANDIBLE DE CIRCULOS ----------------------------------
// Contenido recortado de verdad al area interior (gClip*): nada se sale por
// las esquinas redondeadas ni pisa el asa.
static void qpDrawGroup(int x, int y, int w, int h){
  qpSurface(x, y, w, h, QP_RAD, qpCard());
  int innerTop = y + QP_GPAD;
  int innerBot = y + h - QP_HANDLE_H;
  if(innerBot <= innerTop) return;
  const int oy0 = gClipY0, oy1 = gClipY1, ox0 = gClipX0, ox1 = gClipX1;
  if(gClipY0 < innerTop) gClipY0 = innerTop;
  if(gClipY1 > innerBot - 1) gClipY1 = innerBot - 1;
  if(gClipX0 < x + 2) gClipX0 = x + 2;
  if(gClipX1 > x + w - 3) gClipX1 = x + w - 3;
  if(gClipY1 >= gClipY0){
    int gyTop = innerTop - (int)(qpGScrollF + 0.5f);
    for(int k = 0; k < qpTileN; k++){
      int cx, cy; qpTileCenter(k, gyTop, cx, cy);
      if(cy + QP_TROW < gClipY0) continue;             // fila por encima de la banda
      if(cy - QP_TCIRC / 2 > gClipY1) break;           // filas siguientes: ya fuera
      const QpItem* it = &qpLaySrc[qpTiles[k]];
      const QsCtl* c = qpCtl(it->id);
      if(!c) continue;
      int r = QP_TCIRC / 2;
      // HUECO DE INSERCION: el circulo que se esta moviendo deja su sitio
      // marcado y el reordenamiento se ve en tiempo real mientras el dedo
      // recorre la cuadricula.
      if(qpMode == QPM_EDIT && (int)qpTiles[k] == qpEdDrag){
        fillRoundRectA(cx - r, cy - r, 2 * r, 2 * r, r, TH_PRIM, 60);
        drawRoundRect(cx - r, cy - r, 2 * r, 2 * r, r, TH_PRIM);
        continue;
      }
      bool on = (c->type == QT_TOGGLE && c->state) ? c->state() : false;
      uint16_t face = on ? TH_PRIM : qpTileOff();
      qpGlassSurface(cx - r, cy - r, 2 * r, 2 * r, r, face, on ? qpMixAcc() : qpMixTile());
      qpIcoBgAt(cx, cy);
      if(c->icon) c->icon(cx, cy, 32, on ? TH_ONACC : TH_TXT2);
      uint8_t fa = qpFlashA(0, it->id);
      if(fa) fillRoundRectA(cx - r, cy - r, 2 * r, 2 * r, r, TH_TXT, fa);
      // Etiqueta: hasta dos lineas cortadas al ancho de la columna.
      int lw = QP_TCOLW - 6;
      const char* nm = c->name;
      if(textW(nm, 1) <= lw) drawTextC(cx, cy + r + 8, nm, 1, on ? TH_TXT : TH_TXT2);
      else drawTextClip(cx - lw / 2, cy + r + 8, nm, 1, on ? TH_TXT : TH_TXT2, cx + lw / 2);
      // Subtitulo real (solo cuando cabe y aporta: estado del cronometro, SSID...)
      if(c->sub){
        char sb[48]; c->sub(sb, sizeof(sb));
        if(sb[0]){
          if(textW(sb, 1) <= lw) drawTextC(cx, cy + r + 22, sb, 1, TH_TXT2);
          else drawTextClip(cx - lw / 2, cy + r + 22, sb, 1, TH_TXT2, cx + lw / 2);
        }
      }
      // En edicion, cada circulo lleva su "-" para quitarlo y -- si el control
      // admite otro tamano -- su asa en el borde DERECHO de la celda, igual
      // que los modulos grandes: arrastrarla convierte el circulo en capsula.
      if(qpMode == QPM_EDIT){
        fillCircleAA(cx - r + 4, cy - r + 4, 12, TH_DANGER);
        qpIcoMinus(cx - r + 4, cy - r + 4, 20, TH_ONACC);
        uint8_t nw, nh;
        if(qpNextSize(it->id, it->w, it->h, +1, nw, nh) ||
           qpNextSize(it->id, it->w, it->h, -1, nw, nh)){
          int hx = cx + QP_TCOLW / 2 - 5;
          fillRoundRect(hx - 3, cy - 12, 6, 24, 3, TH_PRIM);
        }
      }
    }
  }
  gClipY0 = oy0; gClipY1 = oy1; gClipX0 = ox0; gClipX1 = ox1;
  // Asa inferior centrada, como en el video.
  fillRoundRect(x + w / 2 - 28, y + h - QP_HANDLE_H / 2 - 3, 56, 6, 3, TH_MUTE);
  // Indicador de scroll interno: solo si de verdad hay mas filas que ver.
  int inner = qpGroupInnerH(h);
  int total = qpTotalRows() * QP_TROW;
  if(total > inner + 2){
    int barH = inner * inner / total;
    if(barH < 24) barH = 24;
    if(barH > inner) barH = inner;
    int maxS = total - inner;
    int sc = (int)qpGScrollF;                       // puede ser negativo con el rebote
    if(sc < 0) sc = 0; if(sc > maxS) sc = maxS;
    int off = (maxS > 0) ? sc * (inner - barH) / maxS : 0;
    if(off < 0) off = 0; if(off > inner - barH) off = inner - barH;
    fillRoundRect(x + w - 8, innerTop + off, 3, barH, 2, TH_MUTE);
  }
}

// ---- BLOQUE "ANADIR UN CONTROL" --------------------------------------
static void qpDrawAddBlock(int x, int y, int w, int h){
  uint16_t tint = mix565(qpCard(), TH_PRIM, 70);
  qpGlassSurface(x, y, w, h, QP_RAD_S, tint, qpMixCard());
  drawRoundRect(x, y, w, h, QP_RAD_S, mix565(TH_BORDER, TH_PRIM, 150));
  int icx = x + w / 2 - 92, icy = y + h / 2;
  qpIcoBgAt(icx, icy);
  qpIcoPlus(icx, icy, 28, TH_TXT);
  drawText(icx + 22, icy - 8, "A\xC3\xB1" "adir un control", 2, TH_TXT);
}

// ---- ADORNOS DEL MODO EDICION ----------------------------------------
// "-" para quitar, asa de redimension en el borde DERECHO (One UI 8.5) y
// boton de orientacion solo en los controles que la admiten.
static void qpDrawEditChrome(int x, int y, int w, int h, int id, bool canResize, bool canRot){
  fillCircleAA(x + 4, y + 4, 14, TH_DANGER);
  qpIcoMinus(x + 4, y + 4, 22, TH_ONACC);
  if(canResize){
    int hx = x + w - 7, hy = y + h / 2;
    fillRoundRect(hx - 3, hy - 16, 6, 32, 3, TH_PRIM);
    fillRoundRect(hx - 9, hy - 8, 4, 16, 2, mix565(TH_PRIM, TH_TXT, 90));
  }
  if(canRot){
    // Esquina inferior IZQUIERDA: no se pisa con el "-" (arriba a la
    // izquierda), ni con el asa de redimension (borde derecho), ni con el "-"
    // del modulo de al lado.
    int rx = x + 6, ry = y + h - 6;
    fillCircleAA(rx, ry, 13, mix565(qpCard(), TH_TXT, 70));
    arcStroke(rx, ry, 7, 30, 300, 2, TH_TXT);
    fillTriangle(rx + 4, ry - 10, rx + 11, ry - 5, rx + 3, ry - 1, TH_TXT);
  }
  if(qpEdRejectF == id && millis() - qpEdRejectMs < 350){
    uint8_t a = (uint8_t)(120 * (1.0f - (float)(millis() - qpEdRejectMs) / 350.0f));
    fillRoundRectA(x, y, w, h, QP_RAD_S, TH_DANGER, a);
  }
}

// ---- DIBUJO COMPLETO DEL CUERPO (panel y editor comparten motor) -----
// Se llama con gClip* ya acotado a la BANDA SUCIA que se va a publicar: los
// bloques que no la cortan ni se dibujan.
static void qpDrawBody(){
  int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
  for(int b = 0; b < qpBlkN; b++){
    int bx = qpBlk[b].x, by = top + qpBlk[b].y, bw = qpBlk[b].w, bh = qpBlk[b].h;
    if(by + bh < gClipY0 || by > gClipY1) continue;               // fuera de la banda
    if(qpBlk[b].kind == QB_GROUP){
      qpDrawGroup(bx, by, bw, bh);
      continue;
    }
    if(qpBlk[b].kind == QB_ADD){ qpDrawAddBlock(bx, by, bw, bh); continue; }
    int i = qpBlk[b].item;
    if(i < 0 || i >= qpLayN) continue;
    if(qpMode == QPM_EDIT && i == qpEdDrag){                       // hueco de insercion
      fillRoundRectA(bx, by, bw, bh, QP_RAD_S, TH_PRIM, 60);
      drawRoundRect(bx, by, bw, bh, QP_RAD_S, TH_PRIM);
      continue;                                                   // el elemento lo dibuja el fantasma
    }
    const QpItem* it = &qpLaySrc[i];
    qpDrawModule(bx, by, bw, bh, it->id, it->ori);
    if(qpMode == QPM_EDIT){
      const QsCtl* c = qpCtl(it->id);
      uint8_t nw, nh;
      bool canR = c && (qpNextSize(it->id, it->w, it->h, +1, nw, nh) ||
                        qpNextSize(it->id, it->w, it->h, -1, nw, nh));
      bool canO = c && (c->oris == (QOR_H | QOR_V));
      qpDrawEditChrome(bx, by, bw, bh, it->id, canR, canO);
    }
  }
}

// Fantasma del elemento que se esta moviendo: va SIEMPRE encima y sigue al
// dedo, sin recortarse al contenido.
static void qpDrawGhost(){
  if(qpMode != QPM_EDIT || qpEdDrag < 0 || qpEdDrag >= qpEdN) return;
  const QpItem* it = &qpEdIt[qpEdDrag];
  int w = (it->w == 1) ? QP_CW : qpSpanW(it->w);
  int h = (it->h >= 2) ? QP_RH2 : QP_RH1;
  if(it->w == 1){ w = QP_TCIRC + 20; h = QP_TROW - 12; }
  int x = qpEdDragX - w / 2, y = qpEdDragY - h / 2;
  if(x < 4) x = 4; if(x + w > SCR_W - 4) x = SCR_W - 4 - w;
  for(int k = 1; k <= 4; k++){
    int yy = y + h + k - 1;
    if(yy < SCR_H) hLineA(x + 8, yy, w - 16, TH_SHADOW, effShadow(96 - k * 20));
  }
  if(it->w == 1){
    const QsCtl* c = qpCtl(it->id);
    int r = QP_TCIRC / 2, cx = x + w / 2, cy = y + r + 4;
    fillRoundRect(cx - r, cy - r, 2 * r, 2 * r, r, mix565(qpCard(), TH_PRIM, 90));
    qpIcoBg = mix565(qpCard(), TH_PRIM, 90);
    if(c && c->icon) c->icon(cx, cy, 32, TH_TXT);
    if(c) drawTextC(cx, cy + r + 6, c->name, 1, TH_TXT);
  } else {
    qpDrawModule(x, y, w, h, it->id, it->ori);
    drawRoundRect(x, y, w, h, QP_RAD_S, TH_PRIM);
  }
}

// ---- CABECERA DEL EDITOR ---------------------------------------------
#define QP_EDH_H 96
static void qpDrawEditHeader(){
  // Banda de vidrio esmerilado: el contenido que pasa por debajo al desplazar
  // se ve difuminado, como en One UI, en vez de cortarse contra un bloque liso.
  qpGlassSurface(0, 0, SCR_W, QP_EDH_H, 0, TH_GLASS2, qpMixHdr());
  drawText(QP_MX, 20, "Cancelar", 2, TH_TXT2);
  drawTextC(SCR_W / 2, 20, "Editar panel", 2, TH_TXT);
  drawTextR(SCR_W - QP_MX, 20, "Listo", 2, TH_PRIM);
  // Restablecer diseno: siempre visible, nunca destructivo hasta "Listo".
  drawTextC(SCR_W / 2, 56, "Restablecer dise\xC3\xB1o", 1, TH_TXT2);
  fillRect(0, QP_EDH_H - 1, SCR_W, 1, TH_DIV);
}
// Zonas tactiles de la cabecera del editor (>= 44 px de alto).
#define QP_EDH_BTN_Y0 6
#define QP_EDH_BTN_Y1 50
#define QP_EDH_RST_Y0 50
#define QP_EDH_RST_Y1 QP_EDH_H

// ---- CATALOGO "ANADIR UN CONTROL" ------------------------------------
#define QP_CAT_HDR   96
#define QP_CAT_ROW   112
#define QP_CAT_VIEWH (SCR_H - QP_CAT_HDR)

// Rehace la lista de controles ofrecidos: SOLO los disponibles de verdad en
// esta placa y que no esten ya en el diseno en edicion.
static void qpCatBuild(){
  qpCatN = 0;
  for(int id = 0; id < QSID_COUNT; id++){
    if(!qpCtlAvail(id)) continue;
    bool present = false;
    for(int i = 0; i < qpEdN; i++) if(qpEdIt[i].id == id){ present = true; break; }
    if(present) continue;
    qpCatIds[qpCatN++] = (uint8_t)id;
  }
}
static inline int qpCatRows(){ return (qpCatN + 3) / 4; }
static inline int qpCatContentH(){ return qpCatRows() * QP_CAT_ROW + 24; }

static void qpDrawCatalog(){
  int top = QP_CAT_HDR - (int)(qpCatScrollF + 0.5f) + 12;
  const int oy0 = gClipY0, oy1 = gClipY1;
  if(gClipY0 < QP_CAT_HDR) gClipY0 = QP_CAT_HDR;
  // Hoja de vidrio del catalogo: el mismo material que el resto del panel, para
  // que la pantalla no quede como wallpaper pelado con iconos encima.
  if(gClipY1 >= gClipY0)
    qpGlassSurface(0, QP_CAT_HDR, SCR_W, SCR_H - QP_CAT_HDR, 0, TH_GLASS2, qpMixCard());
  if(gClipY1 >= gClipY0){
    for(int k = 0; k < qpCatN; k++){
      int r = k / 4, c = k % 4;
      int cx = qpColX(c) + QP_CW / 2, cy = top + r * QP_CAT_ROW + QP_TCIRC / 2;
      if(cy + QP_CAT_ROW < gClipY0) continue;
      if(cy - QP_TCIRC / 2 > gClipY1) break;
      const QsCtl* ct = qpCtl(qpCatIds[k]);
      if(!ct) continue;
      bool sel = (qpCatSel == k);
      int rad = QP_TCIRC / 2;
      uint16_t face = sel ? TH_PRIM : qpTileOff();
      qpGlassSurface(cx - rad, cy - rad, 2 * rad, 2 * rad, rad, face, sel ? qpMixAcc() : qpMixTile());
      qpIcoBgAt(cx, cy);
      if(ct->icon) ct->icon(cx, cy, 32, sel ? TH_ONACC : TH_TXT);
      int lw = QP_CW - 2;
      if(textW(ct->name, 1) <= lw) drawTextC(cx, cy + rad + 8, ct->name, 1, TH_TXT);
      else drawTextClip(cx - lw / 2, cy + rad + 8, ct->name, 1, TH_TXT, cx + lw / 2);
      const char* cat = QP_CAT_NAME[ct->cat];
      if(textW(cat, 1) <= lw) drawTextC(cx, cy + rad + 22, cat, 1, TH_TXT2);
      else drawTextClip(cx - lw / 2, cy + rad + 22, cat, 1, TH_TXT2, cx + lw / 2);
    }
    if(qpCatN == 0)
      drawTextC(SCR_W / 2, QP_CAT_HDR + 60, "No queda ning\xC3\xBAn control disponible", 2, TH_MUTE);
  }
  gClipY0 = oy0; gClipY1 = oy1;
  // Cabecera FIJA del catalogo
  if(gClipY0 < QP_CAT_HDR){
    qpGlassSurface(0, 0, SCR_W, QP_CAT_HDR, 0, TH_GLASS2, qpMixHdr());
    drawText(QP_MX, 22, "Atr\xC3\xA1s", 2, TH_TXT2);
    drawTextC(SCR_W / 2, 22, "A\xC3\xB1" "adir un control", 2, TH_TXT);
    drawTextC(SCR_W / 2, 56, "Solo se listan controles con funci\xC3\xB3n real", 1, TH_TXT2);
    fillRect(0, QP_CAT_HDR - 1, SCR_W, 1, TH_DIV);
  }
}

// ---- ASA DE CIERRE DE LA CORTINA (franja inferior fija) --------------
static void qpDrawFooter(){
  fillRoundRect(SCR_W / 2 - 44, SCR_H - QP_FOOT_H / 2 - 3, 88, 6, 3, TH_MUTE);
}

// #############################################################
// ##  LA CORTINA ES UNA CAPA GLOBAL, NO UNA PANTALLA DEL HOME
// ##  ------------------------------------------------------
// ##  El FONDO es qsBgSrc(): homeBuf en el escritorio, y una captura
// ##  del ultimo cuadro de la app cuando hay una app delante. La
// ##  captura se hace UNA vez, al empezar el gesto, y se libera en
// ##  cuanto la cortina se cierra del todo.
// ##
// ##  POR QUE EL ARRASTRE IBA A TIRONES (y como se arregla)
// ##  ------------------------------------------------------
// ##  1. La capa de vidrio se calculaba con drawLiquidGlassPanelEx a
// ##     PANTALLA COMPLETA en el primer cuadro del gesto. Esa funcion
// ##     hace memcpy + dos pasadas de box-blur + composicion sobre
// ##     384.000 pixeles, con SEIS divisiones enteras por pixel y una
// ##     pasada vertical que recorre columnas (una linea de cache de
// ##     PSRAM por pixel). Son cientos de milisegundos JUSTO cuando el
// ##     dedo empieza a moverse.
// ##       -> ahora el vidrio se calcula a 1/4 de escala (120x200 =
// ##          24.000 pixeles) en qpGlassBuild(), y se expande por
// ##          filas con qpGlassRows(). Mismo material, ~1/16 del
// ##          trabajo, y ademas la reduccion 4x4 YA es un desenfoque.
// ##  2. El contenido del panel se redibujaba en cada cuadro del
// ##     arrastre.
// ##       -> ahora qsBuf guarda el panel COMPUESTO (vidrio +
// ##          contenido) y se compone PEREZOSAMENTE: solo las filas
// ##          que el gesto acaba de revelar (qsEnsureComposed). Un
// ##          cuadro de arrastre es un memcpy de la banda, cero
// ##          dibujo.
// ##  3. El manejador tactil llamaba a qsRender() ademas de uiTick(),
// ##     asi que se publicaban dos bandas por vuelta de loop() y cada
// ##     flxFlush espera al DMA2D.
// ##       -> el tacto ya SOLO actualiza estado y marca banda sucia;
// ##          el unico punto de render es qsTick(), una vez por cuadro.
// ##  4. La posicion se suavizaba con una constante de tiempo, o sea
// ##     el panel iba SIEMPRE por detras del dedo.
// ##       -> con el dedo abajo la posicion es 1:1. El easing existe
// ##          solo despues de soltar.
// #############################################################
static uint16_t* qsBuf     = NULL;   // panel COMPUESTO (vidrio + contenido)
static uint16_t* qsAppSnap = NULL;   // ultimo cuadro de la app de debajo
static bool      qsOverApp = false;
static int       qsLastY   = 0;
static int       qpLastMin = -1;     // minuto con el que se pinto la cabecera

static inline uint16_t* qsBgSrc(){
  if(qsOverApp && qsAppSnap) return qsAppSnap;
  return homeBuf;
}
static bool qsCaptureApp(){
  if(!qsAppSnap)
    qsAppSnap = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!qsAppSnap) return false;                 // sin memoria: la cortina no se abre (mejor que abrirla rota)
  fbLock();
  memcpy(qsAppSnap, fb, (size_t)SCR_W * SCR_H * 2);
  fbUnlock();
  qsOverApp = true;
  qsDirty   = true;
  return true;
}
static void qsFreeApp(){
  if(qsAppSnap){ heap_caps_free(qsAppSnap); qsAppSnap = NULL; }
  qsOverApp = false;
}

// #############################################################
// ##  CAPA DE VIDRIO DEL PANEL  ·  CACHEADA Y A ESCALA REDUCIDA
// ##  ------------------------------------------------------
// ##  Sobre un fondo desenfocado, reducir a 1/4 y volver a ampliar
// ##  es indistinguible de desenfocar a resolucion completa -- el
// ##  desenfoque ya se comio el detalle fino. Asi que:
// ##    · se reduce el snapshot 4x4 con media de caja (16 muestras
// ##      por pixel de salida: eso solo YA es un desenfoque),
// ##    · se pasa un box-blur corto sobre 24.000 pixeles en vez de
// ##      384.000, con la pasada vertical sobre filas de 240 bytes
// ##      (cabe en cache) en vez de columnas de 960 bytes de paso,
// ##    · se aplica el velo y el tinte del tema AHI, en pequeno,
// ##    · y se expande por filas, solo cuando hacen falta.
// ##
// ##  Se recalcula unicamente cuando cambia el fondo (abrir sobre
// ##  otra app, volver al escritorio) o el tema. NUNCA por cuadro.
// #############################################################
#define QP_GS_SH 2                                  // reduccion 1/4 (desplazamiento)
#define QP_GS_W  (SCR_W >> QP_GS_SH)                // 120
#define QP_GS_H  (SCR_H >> QP_GS_SH)                // 200
#define QP_GS_R  2                                  // radio del blur en pequeno (~8 px reales)
static uint16_t* qsGlassSm   = NULL;                // 120 x 200 = 48 KB
// Version ya EXPANDIDA del vidrio, a resolucion completa. Es opcional: si no
// hay PSRAM se sigue expandiendo por bandas (mas lento, mismo resultado). La
// paga el scroll y el editor, que si recomponen filas ya compuestas: sin esta
// cache tendrian que rehacer la interpolacion bilineal en cada cuadro.
static uint16_t* qsGlassFull = NULL;                // 480 x 800 = 768 KB
static int       qsGlassUpTo = -1;                  // ultima fila expandida en qsGlassFull
static bool      qsGlassOk   = false;

// Box-blur de suma corrediza sobre la capa pequena. Los indices se sujetan a
// los bordes en vez de encoger la ventana: asi el divisor es CONSTANTE
// (2R+1 = 5) y el compilador lo convierte en una multiplicacion, sin ninguna
// division entera por pixel -- que es justo lo que hace cara a glassBlur().
static void qpGlassBlurSm(){
  static uint16_t line[(QP_GS_W > QP_GS_H ? QP_GS_W : QP_GS_H)];
  const int W = 2 * QP_GS_R + 1;
  int r, g, b;
  for(int j = 0; j < QP_GS_H; j++){                 // horizontal
    uint16_t* row = qsGlassSm + (size_t)j * QP_GS_W;
    memcpy(line, row, QP_GS_W * 2);
    for(int i = 0; i < QP_GS_W; i++){
      int sr = 0, sg = 0, sb = 0;
      for(int k = -QP_GS_R; k <= QP_GS_R; k++){
        int q = i + k; if(q < 0) q = 0; if(q >= QP_GS_W) q = QP_GS_W - 1;
        un565(line[q], r, g, b); sr += r; sg += g; sb += b;
      }
      row[i] = pk565(sr / W, sg / W, sb / W);
    }
  }
  for(int i = 0; i < QP_GS_W; i++){                 // vertical
    for(int j = 0; j < QP_GS_H; j++) line[j] = qsGlassSm[(size_t)j * QP_GS_W + i];
    for(int j = 0; j < QP_GS_H; j++){
      int sr = 0, sg = 0, sb = 0;
      for(int k = -QP_GS_R; k <= QP_GS_R; k++){
        int q = j + k; if(q < 0) q = 0; if(q >= QP_GS_H) q = QP_GS_H - 1;
        un565(line[q], r, g, b); sr += r; sg += g; sb += b;
      }
      qsGlassSm[(size_t)j * QP_GS_W + i] = pk565(sr / W, sg / W, sb / W);
    }
  }
}

// Velo del panel. Con Liquid Glass activo deja pasar mas fondo; con el estilo
// Plano el mismo material queda mas "esmerilado". En los dos casos es una capa
// TRANSLUCIDA sobre el fondo desenfocado, nunca un relleno opaco.
// Velo del fondo de la cortina. En PLANO es TOTAL: la cortina es una superficie
// solida de la paleta (TH_PAGE), sin fondo desenfocado detras ni tinte de
// vidrio -- que es lo que pide el estilo Plano. Antes se quedaba en 212 y el
// wallpaper seguia asomando desenfocado por debajo: vidrio con otro nombre.
// Con Liquid Glass se conserva el 152 afinado de siempre.
static inline uint8_t qpVeilAlpha(){ return uiGlass ? 152 : 255; }

static bool qpGlassBuild(){
  uint16_t* bg = qsBgSrc();
  if(!bg) return false;
  if(!qsGlassSm)
    qsGlassSm = (uint16_t*)heap_caps_malloc((size_t)QP_GS_W * QP_GS_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!qsGlassSm) return false;
  // 1) reduccion 4x4 con media de caja
  for(int j = 0; j < QP_GS_H; j++){
    const uint16_t* s0 = bg + (size_t)(j << QP_GS_SH) * SCR_W;
    uint16_t* d = qsGlassSm + (size_t)j * QP_GS_W;
    for(int i = 0; i < QP_GS_W; i++){
      int sr = 0, sg = 0, sb = 0, r, g, b;
      int x0 = i << QP_GS_SH;
      for(int dy = 0; dy < (1 << QP_GS_SH); dy++){
        const uint16_t* row = s0 + (size_t)dy * SCR_W + x0;
        for(int dx = 0; dx < (1 << QP_GS_SH); dx++){ un565(row[dx], r, g, b); sr += r; sg += g; sb += b; }
      }
      d[i] = pk565(sr >> (2 * QP_GS_SH), sg >> (2 * QP_GS_SH), sb >> (2 * QP_GS_SH));
    }
  }
  // 2) desenfoque corto y 3) velo + tinte del tema, todo en pequeno
  qpGlassBlurSm();
  uint8_t a = qpVeilAlpha();
  // El TINTE de vidrio solo existe con Liquid Glass. En Plano se salta (mezcla
  // 0) y el velo total de arriba deja la superficie exactamente en TH_PAGE.
  uint16_t veil = TH_PAGE, tint = TH_GLASS2;
  uint8_t tintMix = uiGlass ? 40 : 0;
  for(int j = 0; j < QP_GS_H; j++){
    uint16_t* d = qsGlassSm + (size_t)j * QP_GS_W;
    // Degradado muy suave hacia arriba: da profundidad a la cabecera sin
    // separarla con una linea dura, igual que en el panel de referencia.
    uint8_t extra = (j < QP_GS_H / 5) ? (uint8_t)(18 - (18 * j) / (QP_GS_H / 5)) : 0;
    for(int i = 0; i < QP_GS_W; i++){
      uint16_t c = tintMix ? mix565(d[i], tint, tintMix) : d[i];
      c = mix565(c, veil, a);
      if(extra) c = mix565(c, TH_SURF2, extra);
      d[i] = c;
    }
  }
  qsGlassOk = true;
  return true;
}

// Expande la capa pequena a las filas [y0, y1] de dst (resolucion completa),
// con interpolacion bilineal. Las dos filas de origen se interpolan en
// HORIZONTAL una sola vez y se reutilizan para las cuatro filas de salida que
// las comparten: 1,5 mezclas por pixel en vez de 3.
static uint16_t qpGlRowA[SCR_W], qpGlRowB[SCR_W];
static int      qpGlRowSy = -1;
static void qpGlassRows(uint16_t* dst, int y0, int y1){
  if(!qsGlassOk || !qsGlassSm) return;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  for(int y = y0; y <= y1; y++){
    int sy = y >> QP_GS_SH;
    if(sy > QP_GS_H - 1) sy = QP_GS_H - 1;
    if(sy != qpGlRowSy){
      int sy1 = sy + 1 < QP_GS_H ? sy + 1 : sy;
      const uint16_t* ra = qsGlassSm + (size_t)sy  * QP_GS_W;
      const uint16_t* rb = qsGlassSm + (size_t)sy1 * QP_GS_W;
      for(int x = 0; x < SCR_W; x++){
        int sx = x >> QP_GS_SH;
        int sx1 = sx + 1 < QP_GS_W ? sx + 1 : sx;
        uint8_t fx = (uint8_t)((x & ((1 << QP_GS_SH) - 1)) << (8 - QP_GS_SH));
        qpGlRowA[x] = mix565(ra[sx], ra[sx1], fx);
        qpGlRowB[x] = mix565(rb[sx], rb[sx1], fx);
      }
      qpGlRowSy = sy;
    }
    uint8_t fy = (uint8_t)((y & ((1 << QP_GS_SH) - 1)) << (8 - QP_GS_SH));
    uint16_t* d = dst + (size_t)y * SCR_W;
    if(fy == 0) memcpy(d, qpGlRowA, SCR_W * 2);
    else for(int x = 0; x < SCR_W; x++) d[x] = mix565(qpGlRowA[x], qpGlRowB[x], fy);
  }
}

// Libera TODO lo temporal del panel. Lo llama el cierre de la cortina, la
// cancelacion del editor y cualquier cambio de estado del sistema: ni el OTA
// ni una suspension pueden dejar PSRAM reservada de balde.
static void qpFreeBuffers(){
  if(qsBuf){ heap_caps_free(qsBuf); qsBuf = NULL; }
  if(qsGlassSm){ heap_caps_free(qsGlassSm); qsGlassSm = NULL; }
  if(qsGlassFull){ heap_caps_free(qsGlassFull); qsGlassFull = NULL; }
  qsGlassOk = false; qpGlRowSy = -1; qsGlassUpTo = -1;
  qsFreeApp();
}
// Deja listas en qsGlassFull las filas [0, y1] del vidrio expandido y las
// copia sobre dst. Sin la cache, expande directamente sobre dst.
static void qpGlassInto(uint16_t* dst, int y0, int y1){
  if(!qsGlassFull){ qpGlassRows(dst, y0, y1); return; }
  if(y1 > qsGlassUpTo){
    qpGlassRows(qsGlassFull, qsGlassUpTo + 1, y1);
    qsGlassUpTo = y1;
  }
  memcpy(dst + (size_t)y0 * SCR_W, qsGlassFull + (size_t)y0 * SCR_W,
         (size_t)(y1 - y0 + 1) * SCR_W * 2);
}

// ---- BANDAS SUCIAS ---------------------------------------------------
// Dos acumuladores distintos y a proposito:
//   qpDy0/qpDy1  -> lo que hay que PUBLICAR en pantalla este cuadro.
//   qpCy0/qpCy1  -> lo que hay que volver a COMPONER en qsBuf (mas caro).
// Arrastrar solo mueve el primero; tocar un control, desplazar o editar
// mueven los dos.
static int  qpDy0 = 0x7FFF, qpDy1 = -1;
static int  qpCy0 = 0x7FFF, qpCy1 = -1;
static inline void qpMark(int y0, int y1){
  if(y0 < qpDy0) qpDy0 = y0;
  if(y1 > qpDy1) qpDy1 = y1;
}
// Recomponer implica publicar: nunca se compone algo que no se vaya a ver.
static inline void qpRecompose(int y0, int y1){
  if(y0 < qpCy0) qpCy0 = y0;
  if(y1 > qpCy1) qpCy1 = y1;
  qpMark(y0, y1);
}
// qpMarkAll RECOMPONE: lo usan las operaciones que cambian el contenido
// (editor, catalogo, cambio de estado de un control). El ARRASTRE no lo usa
// nunca -- marca su propia banda con qpMark y no recompone ni una fila.
static inline void qpMarkAll(){ qpRecompose(0, SCR_H - 1); }
static inline void qpMarkView(){ qpRecompose(QP_VIEW_Y0, SCR_H - 1); }
// El scroll INTERNO de la tarjeta solo cambia la tarjeta: recomponer todo el
// viewport por eso seria tirar la mitad del presupuesto del cuadro.
static void qpMarkGroup(){
  if(qpGroupBlk < 0 || qpGroupBlk >= qpBlkN){ qpMarkView(); return; }
  int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
  int y0 = top + qpBlk[qpGroupBlk].y, y1 = y0 + qpBlk[qpGroupBlk].h + 4;
  if(y0 < QP_VIEW_Y0) y0 = QP_VIEW_Y0;
  if(y1 > QP_VIEW_Y1) y1 = QP_VIEW_Y1;
  if(y1 >= y0) qpRecompose(y0, y1);
}
static inline void qpMarkHeader(){ qpRecompose(0, QP_HDR_H - 1); }
static int qpFlashY0 = 0, qpFlashY1 = -1;   // rect del destello, en pantalla

// Invalida la cortina entera (cambios de tema y de apariencia): el vidrio
// cacheado y el panel compuesto ya no valen.
static void qpInvalidateAll(){
  qsDirty = true;
  qpMarkAll();
}

// ---- MAQUETACION VIVA ------------------------------------------------
// Reconstruye la lista de bloques con la configuracion y el alto de tarjeta
// actuales. Es barata (un recorrido de <= 24 elementos) y por eso puede
// llamarse por cuadro mientras se estira la tarjeta.
static void qpRelayout(){
  if(qpMode == QPM_EDIT){
    qpLaySrc = qpEdIt; qpLayN = qpEdN; qpLayEdit = true;
    qpLayGroupPx = qpGroupH(qpEdGrows);
  } else {
    qpLaySrc = qpIt; qpLayN = qpN; qpLayEdit = false;
    if(qpGH <= 0) qpGH = (float)qpGroupH(qpGrows);
    qpLayGroupPx = (int)(qpGH + 0.5f);
  }
  qpLayout();
  // ALTO DE LA TARJETA ACOTADO A LAS FILAS QUE DE VERDAD HAY. qpGrows viene de
  // NVS y puede ser mayor que las filas actuales -- por ejemplo si el usuario
  // guardo 4 filas y luego quito controles. Sin esto la tarjeta se dibujaba con
  // filas vacias y empujaba el resto del panel fuera de la pantalla.
  // qpGroupMaxPx() necesita qpTileN, que lo acaba de calcular qpLayout(): por
  // eso el ajuste va DESPUES y, si corrige algo, se rehace la maquetacion.
  {
    int lo = qpGroupMinPx(), hi = qpGroupMaxPx();
    int want = qpLayGroupPx;
    if(want < lo) want = lo;
    if(want > hi) want = hi;
    if(want != qpLayGroupPx){
      qpLayGroupPx = want;
      if(qpMode != QPM_EDIT) qpGH = (float)want;   // en edicion manda qpEdGrows
      qpLayout();
    }
  }
  // El scroll interno de la tarjeta nunca puede quedar fuera de rango tras
  // cambiar el alto o el numero de controles.
  int inner = qpGroupInnerH(qpLayGroupPx);
  int maxG  = qpTotalRows() * QP_TROW - inner;
  if(maxG < 0) maxG = 0;
  if(qpGScrollF > maxG) qpGScrollF = (float)maxG;
  if(qpGScrollF < 0) qpGScrollF = 0;
}
static inline int qpScrollMax(){
  int m = qpContentH - QP_VIEW_H;
  return m > 0 ? m : 0;
}
static inline int qpCatScrollMax(){
  int m = qpCatContentH() - QP_CAT_VIEWH;
  return m > 0 ? m : 0;
}

// ---- INSTRUMENTACION (medible, y desactivable de una linea) ----------
// Cuenta lo que de verdad importa para el tacto: cuanto tarda un cuadro,
// cuantos se pasan del presupuesto, cuantas filas se COMPONEN (lo caro) y
// cuantas solo se PUBLICAN (lo barato). Un arrastre sano tiene miles de
// filas publicadas y casi ninguna compuesta.
#define QP_PROF 1
#define QP_BUDGET_US 16000                       // presupuesto de un cuadro a ~60 fps
#if QP_PROF
static uint32_t qpPfFrames = 0, qpPfUs = 0, qpPfWorst = 0, qpPfSlow = 0;
static uint32_t qpPfRowsComp = 0, qpPfRowsPub = 0, qpPfGlass = 0, qpPfDragFrames = 0;
static uint32_t qpPfDragUs = 0, qpPfDragWorst = 0;
static void qpProfReset(){
  qpPfFrames = qpPfUs = qpPfWorst = qpPfSlow = 0;
  qpPfRowsComp = qpPfRowsPub = qpPfGlass = 0;
  qpPfDragFrames = qpPfDragUs = qpPfDragWorst = 0;
}
static void qpProfReport(const char* que){
  if(!qpPfFrames) return;
  Serial.printf("[QP] %s: %lu cuadros, medio %lu us, peor %lu us, lentos(>%d ms) %lu\n",
                que, (unsigned long)qpPfFrames, (unsigned long)(qpPfUs / qpPfFrames),
                (unsigned long)qpPfWorst, QP_BUDGET_US / 1000, (unsigned long)qpPfSlow);
  if(qpPfDragFrames)
    Serial.printf("[QP]   arrastre: %lu cuadros, medio %lu us, peor %lu us (~%lu fps)\n",
                  (unsigned long)qpPfDragFrames, (unsigned long)(qpPfDragUs / qpPfDragFrames),
                  (unsigned long)qpPfDragWorst,
                  (unsigned long)(qpPfDragUs ? 1000000UL / (qpPfDragUs / qpPfDragFrames) : 0));
  Serial.printf("[QP]   filas compuestas %lu, publicadas %lu, capas de vidrio %lu\n",
                (unsigned long)qpPfRowsComp, (unsigned long)qpPfRowsPub, (unsigned long)qpPfGlass);
}
#else
static inline void qpProfReset(){}
static inline void qpProfReport(const char*){}
#endif

// ---- COMPOSICION DEL PANEL EN qsBuf ----------------------------------
// qsComposedTo = ultima fila de qsBuf que es valida. La composicion es
// PEREZOSA: al arrastrar solo se componen las filas que el gesto acaba de
// revelar, asi que el coste se reparte por el recorrido en vez de caer entero
// en el primer cuadro.
static int qsComposedTo = -1;

// Compone las filas [y0, y1] de qsBuf: primero el vidrio (expandido de la capa
// pequena) y encima el contenido, recortado a esa banda. Cada fila se compone
// EXACTAMENTE una vez por pasada, y la pasada siempre reescribe el vidrio
// antes de dibujar, asi que repetirla sobre las mismas filas es idempotente.
static void qsComposeRows(int y0, int y1){
  if(!qsBuf || !qsGlassOk) return;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y1 < y0) return;
#if QP_PROF
  qpPfRowsComp += (uint32_t)(y1 - y0 + 1);
#endif
  qpGlassInto(qsBuf, y0, y1);

  uint16_t* oBuf = gBuf;
  const int oy0 = gClipY0, oy1 = gClipY1, ox0 = gClipX0, ox1 = gClipX1;
  setBuf(qsBuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1;
  if(qpMode == QPM_CAT){
    gClipY0 = y0; gClipY1 = y1;
    qpDrawCatalog();
  } else {
    qpRelayout();
    // EL CONTENIDO NO PUEDE INVADIR LAS BANDAS FIJAS. Al desplazar, los
    // bloques viajan por encima de QP_VIEW_Y0 y por debajo de QP_VIEW_Y1: sin
    // este recorte se colarian bajo la cabecera y bajo el asa de cierre.
    int bodyTop = (qpMode == QPM_EDIT) ? QP_EDH_H : QP_VIEW_Y0;
    int bodyBot = (qpMode == QPM_EDIT) ? (SCR_H - 1) : QP_VIEW_Y1;
    gClipY0 = y0 > bodyTop ? y0 : bodyTop;
    gClipY1 = y1 < bodyBot ? y1 : bodyBot;
    if(gClipY1 >= gClipY0) qpDrawBody();
    gClipY0 = y0; gClipY1 = y1;
    if(qpMode == QPM_EDIT){ qpDrawEditHeader(); qpDrawGhost(); }
    else                  { qpDrawHeader(); qpDrawFooter(); }
  }
  gClipY0 = oy0; gClipY1 = oy1; gClipX0 = ox0; gClipX1 = ox1;
  setBuf(oBuf);
}
// Garantiza que qsBuf es valido hasta la fila y1 incluida.
static void qsEnsureComposed(int y1){
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y1 <= qsComposedTo) return;
  qsComposeRows(qsComposedTo + 1, y1);
  qsComposedTo = y1;
}
// Construye (o reconstruye) el vidrio y descarta el panel compuesto.
static void qsCompose(){
  if(!qsBuf) qsBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!qsBuf) return;
  // La cache del vidrio expandido es un LUJO opcional: si no hay PSRAM para
  // ella el panel funciona igual, solo recompone mas despacio.
  if(!qsGlassFull)
    qsGlassFull = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  qsGlassUpTo = -1;
  qpGlRowSy = -1;
  if(!qpGlassBuild()) return;
#if QP_PROF
  qpPfGlass++;
#endif
  qsComposedTo = -1;
  qsDirty = false;
  qpMarkAll();
}

// ---- REPINTADO POR BANDAS --------------------------------------------
// Esta es la UNICA funcion que dibuja en pantalla mientras la cortina esta a
// la vista, y la llama un solo sitio (qsTick), una vez por cuadro.
static void qsRender(bool full){
  uint16_t* bg = qsBgSrc();
  if(!bg) return;
  if(qsPanelY <= 0){
    if(qsLastY > 0 || full){ blitToFb(bg); flxFlushAll(); qsLastY = 0; }
    return;
  }
  if(!qpLoaded) qpLoad();
  if(qsDirty || !qsBuf || !qsGlassOk){ qsCompose(); full = true; }
  if(!qsBuf || !qsGlassOk){ blitToFb(bg); flxFlushAll(); return; }

#if QP_PROF
  uint32_t t0 = micros();
#endif
  int py = qsPanelY < SCR_H ? qsPanelY : SCR_H;

  // 1) recorrido del borde (con su asa por arriba y su sombra por abajo)
  int mlo = py < qsLastY ? py : qsLastY;
  int mhi = py > qsLastY ? py : qsLastY;
  if(py != qsLastY) qpMark(mlo - QS_HANDLE_MARGIN, mhi + QS_SHADOW_H);
  // 2) el reloj de la cabecera cambio de minuto
  if(qpLastMin != rtcMin){ qpLastMin = rtcMin; qpMarkHeader(); }
  // 3) destello de un control (se desvanece: hay que recomponerlo hasta el final)
  if(qpFlashIdx >= 0){
    qpRecompose(qpFlashY0, qpFlashY1);
    if(millis() - qpFlashMs >= QP_FLASH_MS){ qpFlashIdx = -1; qpFlashKind = -1; }
  }
  // 4) rechazo de tamano en el editor
  if(qpEdRejectF >= 0 && millis() - qpEdRejectMs >= 350){ qpEdRejectF = -1; qpMarkView(); }
  // 5) repintado completo pedido
  if(full) qpMark(0, mhi + QS_SHADOW_H);

  // Recomposicion pendiente (contenido que cambio). Va ANTES de publicar, y
  // solo sobre las filas ya compuestas: las que aun no lo estan las hara
  // qsEnsureComposed con el valor nuevo.
  if(qpCy1 >= qpCy0){
    int c1 = qpCy1 < qsComposedTo ? qpCy1 : qsComposedTo;
    if(c1 >= qpCy0) qsComposeRows(qpCy0, c1);
    qpCy0 = 0x7FFF; qpCy1 = -1;
  }
  // Solo se compone lo que el borde de la cortina deja ver.
  qsEnsureComposed(py - 1);

  if(qpDy1 < qpDy0){ qsLastY = py; return; }
  int cutTop = qpDy0 < 0 ? 0 : qpDy0;
  int cutBot = qpDy1 > SCR_H - 1 ? SCR_H - 1 : qpDy1;
  qpDy0 = 0x7FFF; qpDy1 = -1;
  if(cutBot < cutTop){ qsLastY = py; return; }

  // Publicacion: por encima del borde manda el panel ya compuesto; por debajo,
  // el escritorio o la captura de la app -- que es lo que "restaura" las filas
  // que la cortina acaba de dejar libres. Cero dibujo, solo copia.
  setBuf(bbuf);
  for(int j = cutTop; j <= cutBot; j++){
    const uint16_t* src = (j < py) ? qsBuf : bg;
    memcpy(bbuf + (size_t)j * SCR_W, src + (size_t)j * SCR_W, SCR_W * 2);
  }
#if QP_PROF
  qpPfRowsPub += (uint32_t)(cutBot - cutTop + 1);
#endif

  // Sombra y tirador del borde movil: 18 filas, y solo mientras la cortina no
  // esta abierta del todo.
  if(py < SCR_H){
    const int oy0 = gClipY0, oy1 = gClipY1, ox0 = gClipX0, ox1 = gClipX1;
    gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = cutTop; gClipY1 = cutBot;
    for(int yy = py; yy < py + QS_SHADOW_H; yy++){
      uint8_t a = (uint8_t)(70 * (1.0f - (float)(yy - py) / (float)QS_SHADOW_H));
      if(a > 0) hLineA(0, yy, SCR_W, TH_SHADOW, effShadow(a));
    }
    fillRoundRect(SCR_W / 2 - 28, py - 14, 56, 5, 2, TH_MUTE);
    gClipY0 = oy0; gClipY1 = oy1; gClipX0 = ox0; gClipX1 = ox1;
  }
  setBuf(fb);

  present(cutTop, cutBot);
  qsLastY = py;
#if QP_PROF
  uint32_t dt = micros() - t0;
  qpPfFrames++; qpPfUs += dt;
  if(dt > qpPfWorst) qpPfWorst = dt;
  if(dt > QP_BUDGET_US) qpPfSlow++;
  if(qsDragging){
    qpPfDragFrames++; qpPfDragUs += dt;
    if(dt > qpPfDragWorst) qpPfDragWorst = dt;
  }
#endif
}

// ---- ANIMACION DE APERTURA / CIERRE (no bloqueante) -----------------
// REPARTO DE PAPELES, que es lo que hace que el gesto se sienta pegado al
// dedo:
//   · con el dedo ABAJO no hay ni easing ni resorte ni suavizado: la
//     posicion es qsDragBase + (T.y - qsDragY0), 1:1 y sin filtro. El
//     suavizado exponencial que habia antes ponia el panel SIEMPRE por
//     detras del dedo, y eso es exactamente lo que se percibe como lag.
//   · al SOLTAR entra la animacion: solo PROGRAMA el movimiento y
//     qsAnimStep() -- una vez por cuadro desde qsTick() -- lo avanza con el
//     reloj. Cero delay(), cero bucles, cero pasos fijos por cuadro.
//   · si el dedo vuelve a tocar durante esa animacion, se cancela en el acto
//     y el control vuelve al dedo desde donde estuviera el panel.
// La velocidad se sigue filtrando (solo para decidir el lanzamiento al
// soltar), pero NO afecta a la posicion mientras se arrastra.
#define QS_VEL_TAU    45.0f
#define QS_FLICK      0.45f
static float    qsVel = 0;
static int      qsPrevY = 0;
static uint32_t qsPrevMs = 0;
static int      qsDragBase = 0, qsDragY0 = 0;
static bool     qsDragMoved = false;
static float    qsPosF = 0;
static bool     qsAnimOn   = false;
static int      qsAnimFrom = 0, qsAnimDest = 0;
static uint32_t qsAnimT0   = 0, qsAnimDur = 1;

static void qsRestoreBg(){
  uint16_t* bg = qsBgSrc();
  if(!bg) return;
  blitToFb(bg); flxFlushAll();
  qsLastY = 0;
}
static void qpEditCancel();     // definida con el editor, mas abajo

static void qsSettleClosed(){
  qsPanelY = 0; qsPosF = 0; qsVel = 0;
  qsRestoreBg();
  qpProfReport("sesion");             // medicion real del gesto que acaba de terminar
  qpFreeBuffers();                    // suelta la captura, el vidrio y el panel compuesto
  qsComposedTo = -1;
  qsDirty = true;
}
static void qsAnimTo(int target){
  if(target < 0) target = 0; if(target > SCR_H) target = SCR_H;
  int from = qsPanelY;
  int dist = target > from ? target - from : from - target;
  if(dist == 0){
    qsAnimOn = false;
    qsPanelY = target; qsPosF = (float)target;
    if(target <= 0) qsSettleClosed(); else qpMark(0, SCR_H - 1);
    return;
  }
  qsAnimFrom = from; qsAnimDest = target;
  qsAnimT0   = millis();
  qsAnimDur  = 150 + (uint32_t)((uint32_t)dist * 150u / (uint32_t)SCR_H);
  qsAnimOn   = true;
}

// ---- MAQUINA DE GESTOS DEL PANEL -------------------------------------
// PROPIEDAD EXPLICITA DEL GESTO: al presionar se decide QUE superficie manda
// y ya no cambia hasta soltar. Es lo que evita los scrolls anidados
// imposibles y que estirar el asa acabe cerrando la cortina.
enum { QG_NONE = 0, QG_PENDING, QG_CURTAIN, QG_SCROLL, QG_GSCROLL,
       QG_RESIZE, QG_SLIDER, QG_EDDRAG, QG_CATSCROLL };
static uint8_t  qpG = QG_NONE;
static int      qpGx0 = 0, qpGy0 = 0;         // punto de agarre
static float    qpGBase = 0;                  // valor de partida (scroll o alto)
static int      qpGTargetBlk = -1;            // bloque bajo el dedo (para el toque)
static int      qpGTargetTile = -1;           // circulo bajo el dedo (indice en qpTiles)
static int      qpGTargetHdr = -1;            // boton de cabecera bajo el dedo
static bool     qpGLong = false;              // ya se disparo la accion secundaria
// GUARDADO DIFERIDO. Escribir en NVS son decenas de milisegundos de flash, y
// el sitio donde caia -- el cuadro en que se levanta el dedo -- es justo donde
// arranca la animacion de asentamiento: se veia como un tiron al soltar. El
// manejador tactil solo LEVANTA la bandera; qsTick() escribe DESPUES de haber
// publicado el cuadro.
static bool     qpSavePanel = false;          // configuracion del panel (alto de la tarjeta)
static bool     qpSavePrefs = false;          // preferencias del sistema (brillo)
static uint32_t qpGPrevMs = 0;
static int      qpGPrevY = 0;
#define QP_DRAG_TH   8                        // px para dejar de ser toque
#define QP_LONG_MS   480                      // pulsacion larga -> detalles
#define QP_RUBBER    0.42f                    // resistencia en los extremos

static inline float qpRubber(float over){ return over * QP_RUBBER; }

// Acota el scroll con rebote elastico en los extremos.
static void qpClampScroll(bool elastic){
  int mx = qpScrollMax();
  if(qpScrollF < 0)  qpScrollF = elastic ? qpRubber(qpScrollF) : 0;
  if(qpScrollF > mx) qpScrollF = elastic ? (mx + qpRubber(qpScrollF - mx)) : (float)mx;
}
static void qpClampGScroll(bool elastic){
  int inner = qpGroupInnerH(qpLayGroupPx);
  int mx = qpTotalRows() * QP_TROW - inner;
  if(mx < 0) mx = 0;
  if(qpGScrollF < 0)  qpGScrollF = elastic ? qpRubber(qpGScrollF) : 0;
  if(qpGScrollF > mx) qpGScrollF = elastic ? (mx + qpRubber(qpGScrollF - mx)) : (float)mx;
}

// "Snap" del alto de la tarjeta al tamano valido mas cercano (filas
// COMPLETAS), con un rebote final muy pequeno y SIN sobrepasar los limites.
static void qpGroupSnap(){
  int lo = qpGroupMinPx(), hi = qpGroupMaxPx();
  int best = lo, bd = 0x7FFFFFFF;
  int rmax = qpTotalRows(); if(rmax > QP_GROWS_MAX) rmax = QP_GROWS_MAX;
  if(rmax < QP_GROWS_MIN) rmax = QP_GROWS_MIN;
  for(int r = QP_GROWS_MIN; r <= rmax; r++){
    int hpx = qpGroupH(r);
    int d = (int)fabsf(qpGH - hpx);
    if(d < bd){ bd = d; best = hpx; qpGrows = (uint8_t)r; }
  }
  if(best < lo) best = lo; if(best > hi) best = hi;
  qpGFrom = qpGH; qpGTo = (float)best;
  qpGT0 = millis();
  int dist = (int)fabsf(qpGTo - qpGFrom);
  qpGDur = 140 + (uint32_t)(dist * 2);
  if(qpGDur > 420) qpGDur = 420;
  qpGAnim = true;
}
static void qpGroupAnimStep(){
  if(!qpGAnim) return;
  uint32_t e = millis() - qpGT0; if(e > qpGDur) e = qpGDur;
  float p = (float)e / (float)qpGDur;
  // ease-out con un rebote MUY pequeno (c1 = 0.55) y acotado a los limites,
  // asi que el asa nunca se sale del rango valido ni un cuadro.
  float u = p - 1.0f;
  const float c1 = 0.55f, c3 = c1 + 1.0f;
  p = 1.0f + c3 * u * u * u + c1 * u * u;
  qpGH = qpGFrom + (qpGTo - qpGFrom) * p;
  float lo = (float)qpGroupMinPx(), hi = (float)qpGroupMaxPx();
  if(qpGH < lo) qpGH = lo; if(qpGH > hi) qpGH = hi;
  qpMarkView();
  if(e >= qpGDur){ qpGAnim = false; qpGH = qpGTo; }
}

// Inercia y asentamiento del scroll. Todo por tiempo transcurrido: la
// velocidad no depende de cuantos cuadros diera el sistema.
static void qpScrollAnimStep(uint32_t dt){
  if(dt == 0) return;
  // Se distingue QUE se movio: si solo se asento el scroll interno de la
  // tarjeta, se recompone la tarjeta y no las 684 filas del viewport.
  bool moved = false, movedGroup = false;
  if(qpG != QG_SCROLL && fabsf(qpScrollVel) > 0.01f){
    qpScrollF += qpScrollVel * (float)dt;
    qpScrollVel *= expf(-(float)dt / 190.0f);
    if(fabsf(qpScrollVel) < 0.01f) qpScrollVel = 0;
    moved = true;
  }
  if(qpG != QG_SCROLL){
    int mx = qpScrollMax();
    if(qpScrollF < 0 || qpScrollF > mx){
      float tgt = (qpScrollF < 0) ? 0.0f : (float)mx;
      float a = 1.0f - expf(-(float)dt / 90.0f);
      qpScrollF += (tgt - qpScrollF) * a;
      if(fabsf(tgt - qpScrollF) < 0.5f){ qpScrollF = tgt; qpScrollVel = 0; }
      moved = true;
    }
  }
  if(qpG != QG_GSCROLL && fabsf(qpGScrollVel) > 0.01f){
    qpGScrollF += qpGScrollVel * (float)dt;
    qpGScrollVel *= expf(-(float)dt / 190.0f);
    if(fabsf(qpGScrollVel) < 0.01f) qpGScrollVel = 0;
    movedGroup = true;
  }
  if(qpG != QG_GSCROLL){
    int inner = qpGroupInnerH(qpLayGroupPx);
    int mx = qpTotalRows() * QP_TROW - inner; if(mx < 0) mx = 0;
    if(qpGScrollF < 0 || qpGScrollF > mx){
      float tgt = (qpGScrollF < 0) ? 0.0f : (float)mx;
      float a = 1.0f - expf(-(float)dt / 90.0f);
      qpGScrollF += (tgt - qpGScrollF) * a;
      if(fabsf(tgt - qpGScrollF) < 0.5f){ qpGScrollF = tgt; qpGScrollVel = 0; }
      movedGroup = true;
    }
  }
  if(moved)           qpMarkView();
  else if(movedGroup) qpMarkGroup();
}

// ---- HIT-TEST --------------------------------------------------------
// Misma geometria que el dibujo (qpBlk/qpTileCenter): no puede desalinearse
// lo pintado de lo pulsable. Las zonas tactiles son mas grandes que el
// dibujo; ningun boton importante baja de QP_TOUCH_MIN.
static int qpHdrBtnAt(int px, int py){
  if(py < 0 || py > QP_HDR_H) return -1;
  for(int i = 0; i < 3; i++){
#if !POWEROFF_ON
    if(i == 1) continue;
#endif
    int cx = QP_HBTN_CX[i], cy = QP_HBTN_CY, r = QP_TOUCH_MIN / 2 + 2;
    if(px >= cx - r && px <= cx + r && py >= cy - r && py <= cy + r) return i;
  }
  return -1;
}
// Hit-test puro sobre la maquetacion. NO filtra por visibilidad a proposito: lo
// usan tambien el editor y el catalogo, que ocupan la pantalla entera. Quien
// decide que un panel a medio desplegar no tiene controles que tocar es
// qpGlobalHandle, en su primera rama ("if(qsPanelY < SCR_H) qpG = QG_CURTAIN"):
// con la cortina a medias el gesto entero es de la cortina, y ni el slider ni
// ningun otro control lo ven.
static int qpBlockAt(int px, int py){
  int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
  for(int b = 0; b < qpBlkN; b++){
    int bx = qpBlk[b].x, by = top + qpBlk[b].y, bw = qpBlk[b].w, bh = qpBlk[b].h;
    if(px >= bx && px < bx + bw && py >= by && py < by + bh) return b;
  }
  return -1;
}
// ¿El punto cae en el ASA de la tarjeta? Se le da un margen generoso hacia
// arriba y hacia abajo: es el gesto mas fino del panel.
static bool qpOnGroupHandle(int px, int py){
  if(qpGroupBlk < 0) return false;
  int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
  int gy = top + qpBlk[qpGroupBlk].y, gh = qpBlk[qpGroupBlk].h;
  int hy = gy + gh - QP_HANDLE_H;
  return (px >= QP_MX && px <= QP_MX + QP_CONT_W &&
          py >= hy - 10 && py <= gy + gh + 14);
}
// Indice (en qpTiles) del circulo bajo el dedo, o -1.
static int qpTileAt(int px, int py){
  if(qpGroupBlk < 0) return -1;
  int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
  int gy = top + qpBlk[qpGroupBlk].y, gh = qpBlk[qpGroupBlk].h;
  int innerTop = gy + QP_GPAD, innerBot = gy + gh - QP_HANDLE_H;
  if(py < innerTop || py >= innerBot) return -1;
  int gyTop = innerTop - (int)(qpGScrollF + 0.5f);
  for(int k = 0; k < qpTileN; k++){
    int cx, cy; qpTileCenter(k, gyTop, cx, cy);
    int hw = QP_TCOLW / 2, hh = QP_TROW / 2;
    if(hw < QP_TOUCH_MIN / 2) hw = QP_TOUCH_MIN / 2;
    if(px >= cx - hw && px <= cx + hw && py >= cy - QP_TCIRC / 2 - 6 && py <= cy + hh)
      return k;
  }
  return -1;
}
static bool qpGroupCanScroll(){
  int inner = qpGroupInnerH(qpLayGroupPx);
  return qpTotalRows() * QP_TROW > inner + 2;
}

// ---- EJECUCION DEL TOQUE ---------------------------------------------
static void qpFlashTileK(int k){
  if(k < 0 || k >= qpTileN || qpGroupBlk < 0) return;
  int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
  int gy = top + qpBlk[qpGroupBlk].y;
  int gyTop = gy + QP_GPAD - (int)(qpGScrollF + 0.5f);
  int cx, cy; qpTileCenter(k, gyTop, cx, cy);
  qpFlashKind = 0; qpFlashIdx = qpIt[qpTiles[k]].id; qpFlashMs = millis();
  qpFlashY0 = cy - QP_TCIRC / 2 - 2; qpFlashY1 = cy + QP_TCIRC / 2 + 2;
  qpMark(qpFlashY0, qpFlashY1);
}
static void qpFlashBlock(int b){
  if(b < 0 || b >= qpBlkN) return;
  int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
  int i = qpBlk[b].item;
  if(i < 0 || i >= qpN) return;
  qpFlashKind = 1; qpFlashIdx = qpIt[i].id; qpFlashMs = millis();
  qpFlashY0 = top + qpBlk[b].y - 2; qpFlashY1 = top + qpBlk[b].y + qpBlk[b].h + 2;
  qpMark(qpFlashY0, qpFlashY1);
}
// Ejecuta un control. detail = pulsacion larga (accion secundaria).
// Devuelve true si la accion ABANDONA la cortina (cambia de pantalla): en ese
// caso quien llama no puede seguir dibujando el panel.
static bool qpExecCtl(int id, bool detail){
  const QsCtl* c = qpCtl(id);
  if(!c || !c->avail || !c->avail()) return false;
  if(detail && c->detail){ c->detail(); return true; }
  if(c->type == QT_ACTION){ if(c->tap) c->tap(); return true; }
  if(c->tap) c->tap();
  // Cambiar de tema recompone el fondo de la cortina; el resto solo cambia el
  // estado de su control y basta con repintar su banda.
  if(id == QSID_THEME || id == QSID_GLASS) qpInvalidateAll();
  else qpMarkView();
  return false;
}

// ---- ATENCION DEL TOQUE CON EL PANEL ABIERTO -------------------------
static void qpEditEnter();      // definidas con el editor
static bool qpEditTouch();
static bool qpCatTouch();

static bool qpPanelTouch(){
  uint32_t now = millis();
  uint32_t dt = now - qpGPrevMs; if(dt < 1) dt = 1; if(dt > 100) dt = 100;

  if(T.pressed && qpG == QG_NONE){
    qpGx0 = T.x; qpGy0 = T.y; qpGPrevY = T.y; qpGPrevMs = now;
    qpGLong = false; qpGTargetBlk = -1; qpGTargetTile = -1; qpGTargetHdr = -1;
    qpScrollVel = 0; qpGScrollVel = 0; qpGAnim = false;
    // 0) a medio abrir no hay controles que tocar: manda la cortina entera
    if(qsPanelY < SCR_H) qpG = QG_CURTAIN;
    // 1) cabecera: botones o agarre para cerrar
    else if(T.y < QP_HDR_H){
      qpGTargetHdr = qpHdrBtnAt(T.x, T.y);
      qpG = (qpGTargetHdr >= 0) ? QG_PENDING : QG_CURTAIN;
    }
    // 2) franja inferior: agarre de cierre
    else if(T.y > SCR_H - QP_FOOT_H) qpG = QG_CURTAIN;
    // 3) asa de la tarjeta: manda el RESIZE
    else if(qpOnGroupHandle(T.x, T.y)){ qpG = QG_RESIZE; qpGBase = qpGH; }
    else {
      int b = qpBlockAt(T.x, T.y);
      qpGTargetBlk = b;
      if(b >= 0 && qpBlk[b].kind == QB_ITEM){
        int i = qpBlk[b].item;
        if(i >= 0 && i < qpN && qpCtl(qpIt[i].id) && qpCtl(qpIt[i].id)->type == QT_SLIDER){
          qpG = QG_SLIDER;                                  // el slider actua ya
        } else qpG = QG_PENDING;
      } else if(b >= 0 && qpBlk[b].kind == QB_GROUP){
        qpGTargetTile = qpTileAt(T.x, T.y);
        qpG = QG_PENDING;
      } else qpG = QG_PENDING;
      qpGBase = qpScrollF;
    }
    if(qpG == QG_CURTAIN){
      qsAnimOn = false; qsDragging = true; qsDragMoved = false;
      qsDragBase = qsPanelY; qsDragY0 = T.y; qsPosF = (float)qsPanelY;
      qsPrevY = T.y; qsPrevMs = now; qsVel = 0;
    }
  }

  // ---- SLIDER (brillo real, actualizacion continua) ----
  if(qpG == QG_SLIDER){
    if(T.down){
      int b = qpGTargetBlk;
      if(b >= 0 && b < qpBlkN && qpBlk[b].kind == QB_ITEM){
        int i = qpBlk[b].item;
        int id = (i >= 0 && i < qpN) ? qpIt[i].id : -1;
        int x = qpBlk[b].x, w = qpBlk[b].w;
        int th = qpBlk[b].h - 20; if(th < 40) th = 40;
        int run = w - th; if(run < 1) run = 1;               // nunca se divide por cero
        int v = (T.x - x - th / 2) * 100 / run;
        if(v < 0) v = 0; if(v > 100) v = 100;
        // VOLUMEN: escribe el registro del codec en el acto, igual
        // que el brillo escribe el PWM. Y se recompone la banda del
        // slider por el mismo motivo que alli (ver el comentario de
        // abajo): el panel vive compuesto en qsBuf.
        if(id == QSID_VOLUME && v != (int)flexAudioVolume()){
          flexAudioSetVolume((uint8_t)v);
          int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
          int sy0 = top + qpBlk[b].y, sy1 = sy0 + qpBlk[b].h;
          if(sy0 < QP_VIEW_Y0) sy0 = QP_VIEW_Y0;
          if(sy1 > QP_VIEW_Y1) sy1 = QP_VIEW_Y1;
          if(sy1 > sy0) qpRecompose(sy0, sy1 - 1);
          return true;                                      // el gesto es del slider
        }
        if(id == QSID_BRIGHT && v != gBright){
          setBacklight(v);                                  // PWM real, sin present() completo
          // RECOMPONER, NO SOLO PUBLICAR. Aqui estaba el fallo del slider: el
          // contenido del panel vive COMPUESTO en qsBuf (ver qsComposeRows), y
          // qpMark solo apunta filas para VOLCARLAS a pantalla desde ese cache.
          // El brillo fisico cambiaba -- setBacklight es PWM real -- pero el
          // relleno y el pulgar se volvian a copiar del qsBuf VIEJO, dibujado
          // con el porcentaje anterior: el indicador se quedaba clavado mientras
          // la pantalla si cambiaba de brillo. qpRecompose vuelve a dibujar esas
          // filas desde gBright (la unica fuente de verdad, la misma que lee
          // qpDrawSliderBody y qpSubBright) y luego las publica.
          // Solo la banda del slider: el resto del panel no se recompone.
          int top = QP_VIEW_Y0 - (int)(qpScrollF + 0.5f);
          int sy0 = top + qpBlk[b].y, sy1 = sy0 + qpBlk[b].h;
          if(sy0 < QP_VIEW_Y0) sy0 = QP_VIEW_Y0;            // nunca bajo la cabecera
          if(sy1 > QP_VIEW_Y1) sy1 = QP_VIEW_Y1;            // ni bajo el asa de cierre
          if(sy1 >= sy0) qpRecompose(sy0, sy1);
        }
      }
      return true;
    }
    qpSavePrefs = true;                                     // NVS DIFERIDA, fuera del gesto
    qpG = QG_NONE;
    return true;
  }

  // ---- ARRASTRE DE LA CORTINA ----
  if(qpG == QG_CURTAIN){
    if(T.down){
      if(!qsDragMoved){
        if(abs(T.y - qsDragY0) <= 6){ qsPrevY = T.y; qsPrevMs = now; return true; }
        qsDragMoved = true;
      }
      // dt acotado: si un cuadro se retrasa (una escritura de flash, la radio),
      // la velocidad no se dispara ni el filtro pega un salto.
      float d = (float)(now - qsPrevMs); if(d < 1) d = 1; if(d > 100) d = 100;
      // POSICION 1:1 CON EL DEDO. Sin suavizado, sin resorte, sin easing: el
      // borde de la cortina esta exactamente donde esta el dedo.
      int target = qsDragBase + (T.y - qsDragY0);
      if(target < 0) target = 0; if(target > SCR_H) target = SCR_H;
      // La velocidad se filtra SOLO para decidir el lanzamiento al soltar.
      float inst = (float)(T.y - qsPrevY) / d;
      qsVel += (inst - qsVel) * (d / (d + QS_VEL_TAU));
      qsPrevY = T.y; qsPrevMs = now;
      qsPosF = (float)target;
      // El tacto NO dibuja: solo mueve el estado y marca la banda. El unico
      // punto de render es qsTick(), una vez por cuadro.
      if(target != qsPanelY){
        int lo = target < qsPanelY ? target : qsPanelY;
        int hi = target > qsPanelY ? target : qsPanelY;
        qsPanelY = target;
        qpMark(lo - QS_HANDLE_MARGIN, hi + QS_SHADOW_H);
      }
      return true;
    }
    qsDragging = false; qpG = QG_NONE;
    if(!qsDragMoved){
      qsPanelY = qsDragBase; qsPosF = (float)qsDragBase;
      // Un TOQUE cierra solo desde el asa inferior -- la superficie declarada
      // para eso. Tocar la cabecera (junto al reloj, entre los botones) no
      // cierra nada: ahi el gesto valido es ARRASTRAR.
      if(qsPanelY >= SCR_H && T.tap && qpGy0 > SCR_H - QP_FOOT_H) qsAnimTo(0);
      else if(qsPanelY > 0){
        if(qsVel > QS_FLICK) qsAnimTo(SCR_H); else if(qsVel < -QS_FLICK) qsAnimTo(0);
        else qsAnimTo(qsPanelY >= (SCR_H * QS_OPEN_PCT) / 100 ? SCR_H : 0);
      } else qsSettleClosed();
      return true;
    }
    if(qsVel > QS_FLICK)       qsAnimTo(SCR_H);
    else if(qsVel < -QS_FLICK) qsAnimTo(0);
    else qsAnimTo(qsPanelY >= (SCR_H * QS_OPEN_PCT) / 100 ? SCR_H : 0);
    return true;
  }

  // ---- ESTIRAMIENTO DE LA TARJETA (asa 1:1 con el dedo) ----
  if(qpG == QG_RESIZE){
    if(T.down){
      float lo = (float)qpGroupMinPx(), hi = (float)qpGroupMaxPx();
      float h = qpGBase + (float)(T.y - qpGy0);
      // Resistencia fuera de rango: el asa acompana al dedo pero no deja que
      // la tarjeta pase de sus limites validos.
      if(h < lo) h = lo + qpRubber(h - lo);
      if(h > hi) h = hi + qpRubber(h - hi);
      if(h < lo - 40) h = lo - 40; if(h > hi + 40) h = hi + 40;
      if((int)h != (int)qpGH){ qpGH = h; qpMarkView(); }
      return true;
    }
    qpG = QG_NONE;
    qpGroupSnap();
    qpSavePanel = true;                          // el alto elegido es configuracion (NVS diferida)
    return true;
  }

  // ---- SCROLL DEL PANEL / DE LA TARJETA ----
  if(qpG == QG_SCROLL || qpG == QG_GSCROLL){
    if(T.down){
      int d = qpGPrevY - T.y;
      float inst = (float)d / (float)dt;
      if(qpG == QG_SCROLL){
        qpScrollF += d; qpClampScroll(true);
        qpScrollVel += (inst - qpScrollVel) * ((float)dt / ((float)dt + 45.0f));
      } else {
        qpGScrollF += d; qpClampGScroll(true);
        qpGScrollVel += (inst - qpGScrollVel) * ((float)dt / ((float)dt + 45.0f));
      }
      qpGPrevY = T.y; qpGPrevMs = now;
      // El scroll del PANEL mueve todo el contenido; el de la TARJETA solo la
      // tarjeta. Recomponer lo segundo como si fuera lo primero costaba el
      // doble por cuadro sin que cambiara un pixel de mas.
      if(qpG == QG_SCROLL) qpMarkView(); else qpMarkGroup();
      return true;
    }
    qpG = QG_NONE;
    return true;
  }

  // ---- PENDIENTE: aun puede ser toque, scroll o pulsacion larga ----
  if(qpG == QG_PENDING){
    if(T.down){
      if(abs(T.y - qpGy0) > QP_DRAG_TH || abs(T.x - qpGx0) > QP_DRAG_TH * 2){
        // Nacido en la CABECERA (aunque fuera sobre un boton): el gesto es de
        // la cortina entera, no del contenido. Asi bajar el dedo desde el
        // lapiz no acaba haciendo scroll de la lista.
        if(qpGTargetHdr >= 0 || qpGy0 < QP_HDR_H){
          qpGTargetHdr = -1;
          qpG = QG_CURTAIN;
          qsAnimOn = false; qsDragging = true; qsDragMoved = false;
          qsDragBase = qsPanelY; qsDragY0 = qpGy0; qsPosF = (float)qsPanelY;
          qsPrevY = T.y; qsPrevMs = now; qsVel = 0;
          return true;
        }
        // Si el gesto nacio DENTRO de la tarjeta y la tarjeta tiene mas filas
        // de las que ensena, manda el scroll interno. Si no, manda el panel.
        bool inGroup = (qpGTargetBlk >= 0 && qpGTargetBlk < qpBlkN &&
                        qpBlk[qpGTargetBlk].kind == QB_GROUP);
        qpG = (inGroup && qpGroupCanScroll()) ? QG_GSCROLL : QG_SCROLL;
        qpGPrevY = T.y; qpGPrevMs = now;
        qpScrollVel = 0; qpGScrollVel = 0;
        return true;
      }
      // Pulsacion larga -> accion secundaria (detalles / Ajustes del control)
      if(!qpGLong && (now - T.downMs) > QP_LONG_MS){
        qpGLong = true;
        int id = -1;
        if(qpGTargetTile >= 0 && qpGTargetTile < qpTileN) id = qpIt[qpTiles[qpGTargetTile]].id;
        else if(qpGTargetBlk >= 0 && qpBlk[qpGTargetBlk].kind == QB_ITEM){
          int i = qpBlk[qpGTargetBlk].item;
          if(i >= 0 && i < qpN) id = qpIt[i].id;
        }
        const QsCtl* c = qpCtl(id);
        if(c && c->detail){ if(qpExecCtl(id, true)) return true; }
      }
      return true;
    }
    // Soltado sin arrastrar: es un TOQUE.
    uint8_t was = qpG; qpG = QG_NONE; (void)was;
    if(qpGLong) return true;                     // la larga ya se atendio
    if(qpGTargetHdr >= 0){
      int h = qpGTargetHdr; qpGTargetHdr = -1;
      if(h == 0){ qpEditEnter(); return true; }
#if POWEROFF_ON
      if(h == 1){ qsRestoreBg(); qsForceClose(); poffEnter(); return true; }
#endif
      if(h == 2){ qpTapSettings(); return true; }
      return true;
    }
    // Toque de control: lo resuelve qsTapTile (circulos primero, modulos
    // despues). Un toque en el vacio NO cierra el panel: cerrar es del asa
    // inferior y de la cabecera, que son las superficies declaradas para eso.
    qsTapTile(T.x, T.y);
    return true;
  }

  if(!T.down && qpG != QG_NONE) qpG = QG_NONE;
  return true;
}
