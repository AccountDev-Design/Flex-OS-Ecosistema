// #############################################################
// ##  FLEX OS ULTRA  ·  APP CLIMA  ·  Flex Weather
// ##  ----------------------------------------------------------
// ##  Maquetacion de la pagina, panel horario, pronostico diario, mis
// ##  ubicaciones, buscador de ciudades, fisica del desplazamiento, toques
// ##  y los dos widgets. Los datos los sirve FlexOS_Weather: aqui no se
// ##  habla con la red ni una sola vez.
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
#include "FlexOS_Ultra_WeatherKit.h"   // eslabon anterior de la cadena

// #############################################################
// ##  MAQUETACION DE LA PAGINA
// #############################################################
// Alturas dependientes de los DATOS (numero de dias, tarjetas de
// detalle disponibles), nunca de constantes a ojo.
static int wxDetailCount(){ return 7; }     // humedad, viento, UV, visibilidad, precip, sensacion, presion

static void wxLayout(){
  const FlexWeather* w = flexWeatherData();
  int y = WX_HERO_H + 10;
  wxYHour  = y;  y += WX_HOUR_H + 14;
  int nd   = w ? w->dayCount : 0;
  wxHDaily = 54 + nd * WX_DAY_ROW + 10;
  wxYDaily = y;  y += wxHDaily + 14;
  wxYSun   = y;  y += WX_SUN_H + 14;
  wxYDet   = y;
  int rows = (wxDetailCount() + 1) / 2;
  wxHDet   = rows * (WX_DET_H + 12) - 12;
  y += wxHDet + 16;
  wxYFoot  = y;  y += 70;
  wxContentH = w ? y : WX_VIEW_BOT;      // el estado vacio no se desplaza
  const FlexWeather* ww = w;
  int nh = ww ? ww->hourCount : 0;
  int innerW = WX_CARD_W - 2 * WX_HOUR_PAD;
  wxHourMax = nh * WX_COL_W - innerW;
  if(wxHourMax < 0) wxHourMax = 0;
}
static inline int wxScrollMax(){
  int m = wxContentH - WX_VIEW_BOT;
  return m > 0 ? m : 0;
}

// Progreso de la animacion de entrada por seccion (fundido + subida).
// Escalonado: la cabecera entra primero y las tarjetas van detras, como
// en One UI. Cuando termina, devuelve 255 y desplazamiento 0 -- a partir
// de ahi no hay nada que animar y el bucle deja de repintar.
static uint8_t wxAnimA(int idx, int* offY){
  if(wxEnterMs == 0){ *offY = 0; return 255; }
  int el = (int)(millis() - wxEnterMs);
  int st = 20 + idx * 38, dur = 190;
  if(el >= st + dur){ *offY = 0; return 255; }
  if(el <= st){ *offY = 26; return 0; }
  float p = (float)(el - st) / (float)dur;
  p = 1.0f - (1.0f - p) * (1.0f - p);          // ease-out cuadratico
  *offY = (int)(26 * (1.0f - p));
  return (uint8_t)(255.0f * p);
}
static bool wxAnimRunning(){
  return wxEnterMs && (int)(millis() - wxEnterMs) < (20 + 7 * 38 + 190);
}

// Tarjeta One UI: rectangulo redondeado translucido. NO se usa el panel
// Liquid Glass del sistema a proposito: desenfoca el fondo real y eso
// cuesta un box-blur por tarjeta y por cuadro -- inviable mientras la
// escena se anima o el dedo arrastra (requisitos 9 y 32). El aspecto
// (vidrio azul marino sobre el cielo) se consigue igual con alpha.
static void wxCard(int x, int y, int w, int h, uint8_t a){
  uint8_t base = WX_CARD_A;
  fillRoundRectA(x, y, w, h, 26, WX_CARD_BG, (uint8_t)((int)base * a / 255));
  fillRoundRectA(x, y, w, 2, 26, TH_ONWALL, (uint8_t)(22 * a / 255));   // filo superior
}

// Pin de ubicacion (mismo glifo que usa la cabecera y el widget).
static void wxPin(int cx, int cy, int r, uint16_t col, uint8_t a){
  fillCircleA(cx, cy - r / 3, r, col, a);
  fillTriangle(cx - r * 2 / 3, cy + r / 4, cx + r * 2 / 3, cy + r / 4, cx, cy + r + r / 2, col);
  fillCircleA(cx, cy - r / 3, r / 3, WX_CARD_BG, a);
}
// Gotita de precipitacion.
static void wxDroplet(int cx, int cy, int r, uint16_t col, uint8_t a){
  fillCircleA(cx, cy + r / 3, r, col, a);
  fillTriangle(cx - r, cy + r / 3, cx + r, cy + r / 3, cx, cy - r - r / 2, col);
}
// Flechas de maxima / minima.
static void wxArrowUp(int cx, int cy, int r, uint16_t col){
  fillTriangle(cx - r, cy + r / 2, cx + r, cy + r / 2, cx, cy - r, col);
}
static void wxArrowDown(int cx, int cy, int r, uint16_t col){
  fillTriangle(cx - r, cy - r / 2, cx + r, cy - r / 2, cx, cy + r, col);
}

// #############################################################
// ##  CABECERA (escena + datos grandes)
// #############################################################
static void wxDrawHero(int y0, int y1){
  const FlexWeather* w = flexWeatherData();
  if(!w) return;
  int off; uint8_t a = wxAnimA(0, &off);
  int base = -(int)wxScroll + off;
  if(base + WX_HERO_H < y0 || base > y1) return;
  uint16_t W = rgb565(255,255,255);
  uint16_t sub = TH_ONWALL2;

  // Ubicacion (tocable: abre "Mis ubicaciones")
  wxPin(34, base + 88, 9, W, a);
  char nm[FLEXWX_NAME + 4];
  wxCopy(nm, sizeof(nm), w->loc.name);
  int maxw = SCR_W - 62 - 20;
  int fs = 3;
  if(textW(nm, fs) > maxw){                       // recorta con puntos suspensivos
    int len = (int)strlen(nm);
    while(len > 3 && textW(nm, fs) > maxw - 12){ nm[--len] = 0; }
    snprintf(nm + len, sizeof(nm) - len, "...");
  }
  drawTextA(52, base + 74, nm, fs, W, a);

  // Temperatura gigante
  wxDrawTemp(24, base + 122, w->temp, 11, W, a);

  // Condicion (nombre corto del weather_code REAL)
  drawTextA(26, base + 246, flexWeatherCondName(w->code, cfgLang), 4, W, a);

  // Maxima / minima + sensacion termica
  int ty = base + 322;
  if(w->have & WXF_MINMAX){
    wxArrowUp(32, ty + 8, 6, W);
    int ex = wxDrawTemp(42, ty, w->tmax, 3, W, a);
    drawTextA(ex + 4, ty, "/", 3, sub, a);
    wxArrowDown(ex + 26, ty + 8, 6, W);
    wxDrawTemp(ex + 36, ty, w->tmin, 3, W, a);
  }
  if(w->have & WXF_FEELS){
    char fl[48];
    snprintf(fl, sizeof(fl), "%s", wt(WT_FEELS));
    int ex = drawTextA(26, ty + 34, fl, 2, sub, a);
    wxDrawTemp(ex + 8, ty + 34, w->feels, 2, sub, a);
  }

  // Frase generada con datos reales (weather_code + viento + minima...)
  char desc[160];
  flexWeatherDescribe(desc, sizeof(desc), cfgLang);
  if(desc[0]) wxWrapText(28, base + 402, SCR_W - 56, desc, 2, sub, a, 22, 2, true);
}

// #############################################################
// ##  PANEL HORARIO 48 H  (desplazamiento horizontal propio)
// #############################################################
static void wxDrawHourly(int y0, int y1){
  const FlexWeather* w = flexWeatherData();
  if(!w || w->hourCount == 0) return;
  int off; uint8_t a = wxAnimA(1, &off);
  int sy = wxYHour - (int)wxScroll + off;
  if(sy > y1 || sy + WX_HOUR_H < y0) return;
  wxCard(WX_CARD_X, sy, WX_CARD_W, WX_HOUR_H, a);

  int ix = WX_CARD_X + WX_HOUR_PAD, iw = WX_CARD_W - 2 * WX_HOUR_PAD;
  int n  = w->hourCount;

  // Rango de la grafica: minimo y maximo REALES de las 48 horas. La forma
  // de la curva sale de los datos, no de una tabla dibujada a mano.
  int tmin = 32767, tmax = -32768;
  for(int i = 0; i < n; i++){
    int16_t t = w->hours[i].temp10;
    if(t == WX_NOVAL_I16) continue;
    if(t < tmin) tmin = t;
    if(t > tmax) tmax = t;
  }
  bool haveGraph = (tmax > -32768 && tmin < 32767);
  int range = haveGraph ? (tmax - tmin) : 1;
  if(range < 10) range = 10;                       // menos de 1 grado: linea casi plana

  int gy0 = sy + 118, gy1 = sy + 168;

  // Recorte al interior de la tarjeta: las columnas que se salen por los
  // lados se cortan limpias, sin pintar fuera.
  int ox0 = gClipX0, ox1 = gClipX1, oy0 = gClipY0, oy1 = gClipY1;
  if(ix > gClipX0) gClipX0 = ix;
  if(ix + iw - 1 < gClipX1) gClipX1 = ix + iw - 1;
  int cy0 = sy + 6, cy1 = sy + WX_HOUR_H - 52;
  if(cy0 > gClipY0) gClipY0 = cy0;
  if(cy1 < gClipY1) gClipY1 = cy1;

  int hs = (int)wxHourScroll;
  int prevX = 0, prevY = 0; bool havePrev = false;
  for(int i = 0; i < n; i++){
    int cx = ix + i * WX_COL_W - hs + WX_COL_W / 2;
    if(cx < ix - WX_COL_W || cx > ix + iw + WX_COL_W){ havePrev = false; continue; }
    const FlexWxHour* h = &w->hours[i];
    int32_t loc = h->t + w->utcOffset;

    char hl[16];
    if(i == 0){ snprintf(hl, sizeof(hl), "%s", wt(WT_NOW)); }
    else wxFmtHour(loc, hl, sizeof(hl));
    drawTextCA(cx, sy + 16, hl, 2, WX_TXT_LO, a);

    // Icono: mismo conversor que la escena y que los widgets. El dia/noche
    // de CADA hora sale del amanecer/atardecer reales, no del is_day actual.
    bool dayH = true;
    if(w->have & WXF_SUN){
      int32_t dsec = ((h->t + w->utcOffset) % 86400 + 86400) % 86400;
      int32_t rs = ((w->sunrise + w->utcOffset) % 86400 + 86400) % 86400;
      int32_t ss = ((w->sunset  + w->utcOffset) % 86400 + 86400) % 86400;
      dayH = (dsec >= rs && dsec < ss);
    } else dayH = w->isDay ? true : false;
    wxDrawIcon(cx, sy + 56, 13, flexWeatherVisual(h->code), dayH, a);

    if(h->temp10 != WX_NOVAL_I16)
      wxDrawTempC(cx, sy + 84, h->temp10 / 10.0f, 3, WX_TXT_HI, a);

    // Punto de la grafica
    if(haveGraph && h->temp10 != WX_NOVAL_I16){
      int py = gy1 - (int)((int32_t)(h->temp10 - tmin) * (gy1 - gy0) / range);
      if(havePrev) wxLineA((float)prevX, (float)prevY, (float)cx, (float)py, 1, WX_ACCENT, a);
      fillCircleA(cx, py, 3, rgb565(255,255,255), a);
      prevX = cx; prevY = py; havePrev = true;
    } else havePrev = false;

    // Probabilidad de precipitacion (solo si la API la dio)
    if(h->pop != WX_NOVAL_U8){
      char pp[12]; snprintf(pp, sizeof(pp), "%d %%", (int)h->pop);
      int tw = textW(pp, 1);
      wxDroplet(cx - tw / 2 - 7, sy + 190, 4, WX_ACCENT, a);
      drawTextA(cx - tw / 2 + 3, sy + 186, pp, 1, WX_TXT_LO, a);
    }
  }
  gClipX0 = ox0; gClipX1 = ox1; gClipY0 = oy0; gClipY1 = oy1;

  // Pie de la tarjeta: separador + "Pronostico de 48 horas >"
  int fy = sy + WX_HOUR_H - 44;
  hLineA(WX_CARD_X + 18, fy, WX_CARD_W - 36, WX_LINE, (uint8_t)(180 * a / 255));
  const char* lbl = wt(WT_HOURLY48);
  int lw = textW(lbl, 2);
  drawTextA(WX_CARD_X + WX_CARD_W - 34 - lw, fy + 14, lbl, 2, WX_TXT_LO, a);
  int chx = WX_CARD_X + WX_CARD_W - 26;
  wxLineA(chx - 4, fy + 15, chx + 1, fy + 21, 1, WX_TXT_LO, a);
  wxLineA(chx + 1, fy + 21, chx - 4, fy + 27, 1, WX_TXT_LO, a);
}

// #############################################################
// ##  PRONOSTICO DIARIO
// #############################################################
static void wxDrawDaily(int y0, int y1){
  const FlexWeather* w = flexWeatherData();
  if(!w || w->dayCount == 0) return;
  int off; uint8_t a = wxAnimA(2, &off);
  int sy = wxYDaily - (int)wxScroll + off;
  if(sy > y1 || sy + wxHDaily < y0) return;
  wxCard(WX_CARD_X, sy, WX_CARD_W, wxHDaily, a);
  drawTextA(WX_CARD_X + 22, sy + 20, wt(WT_FORECAST), 3, WX_TXT_HI, a);

  // Escala comun de la barra: minimo y maximo de TODA la semana.
  int wmin = 32767, wmax = -32768;
  for(int i = 0; i < w->dayCount; i++){
    if(w->days[i].min10 != WX_NOVAL_I16 && w->days[i].min10 < wmin) wmin = w->days[i].min10;
    if(w->days[i].max10 != WX_NOVAL_I16 && w->days[i].max10 > wmax) wmax = w->days[i].max10;
  }
  int wr = (wmax > wmin) ? (wmax - wmin) : 1;

  for(int i = 0; i < w->dayCount; i++){
    int ry = sy + 54 + i * WX_DAY_ROW;
    if(ry > y1 || ry + WX_DAY_ROW < y0) continue;
    const FlexWxDay* d = &w->days[i];
    // Nombre del dia a partir de la FECHA real que trajo la API.
    char dn[20];
    if(i == 0) snprintf(dn, sizeof(dn), "%s", wt(WT_TODAY));
    else       snprintf(dn, sizeof(dn), "%s", WD_FULL[LI()][wxWeekday(d->date + w->utcOffset)]);
    drawTextA(WX_CARD_X + 22, ry + 12, dn, 2, WX_TXT_HI, a);

    if(d->pop != WX_NOVAL_U8 && d->pop > 0){
      char pp[10]; snprintf(pp, sizeof(pp), "%d%%", (int)d->pop);
      wxDroplet(WX_CARD_X + 138, ry + 16, 4, WX_ACCENT, a);
      drawTextA(WX_CARD_X + 146, ry + 12, pp, 1, WX_ACCENT, a);
    }
    wxDrawIcon(WX_CARD_X + 196, ry + 18, 12, flexWeatherVisual(d->code), true, a);

    if(d->min10 != WX_NOVAL_I16 && d->max10 != WX_NOVAL_I16){
      wxDrawTemp(WX_CARD_X + 222, ry + 12, d->min10 / 10.0f, 2, WX_TXT_LO, a);
      int bx = WX_CARD_X + 272, bw = 112, by = ry + 18;
      fillRoundRectA(bx, by, bw, 6, 3, WX_LINE, (uint8_t)(200 * a / 255));
      int x0 = bx + (int)((int32_t)(d->min10 - wmin) * bw / wr);
      int x1 = bx + (int)((int32_t)(d->max10 - wmin) * bw / wr);
      if(x1 - x0 < 8) x1 = x0 + 8;
      if(x1 > bx + bw) x1 = bx + bw;
      for(int x = x0; x < x1; x++){                       // degradado frio -> calido
        uint8_t t = (uint8_t)((x - bx) * 255 / (bw ? bw : 1));
        vLine(x, by, 6, mix565(rgb565(90,170,235), rgb565(245,160,70), t));
      }
      wxDrawTemp(WX_CARD_X + 396, ry + 12, d->max10 / 10.0f, 2, WX_TXT_HI, a);
    }
  }
}

// #############################################################
// ##  AMANECER / ATARDECER  (arco con la posicion REAL del sol)
// #############################################################
static void wxDrawSun(int y0, int y1){
  const FlexWeather* w = flexWeatherData();
  if(!w) return;
  int off; uint8_t a = wxAnimA(3, &off);
  int sy = wxYSun - (int)wxScroll + off;
  if(sy > y1 || sy + WX_SUN_H < y0) return;
  wxCard(WX_CARD_X, sy, WX_CARD_W, WX_SUN_H, a);

  if(!(w->have & WXF_SUN)){
    drawTextA(WX_CARD_X + 22, sy + 20, wt(WT_SUNRISE), 3, WX_TXT_HI, a);
    drawTextA(WX_CARD_X + 22, sy + 60, wt(WT_NA), 2, WX_TXT_LO, a);
    return;
  }
  drawTextA(WX_CARD_X + 22, sy + 16, wt(WT_SUNSET_CARD), 2, WX_TXT_LO, a);

  // El arco arranca POR DEBAJO del titulo (antes lo cruzaba) y deja sitio a
  // las dos horas bajo la linea del horizonte.
  int cx = SCR_W / 2, cy = sy + 152, r = 112;
  // Arco de fondo + arco recorrido: la fraccion sale de la hora REAL.
  arcStroke((float)cx, (float)cy, (float)r, 180, 360, 3, WX_LINE);
  int32_t nowUtc = flexWeatherNowLocal() - w->utcOffset;
  float prog = -1.0f;
  if(nowUtc > 0 && w->sunset > w->sunrise)
    prog = (float)(nowUtc - w->sunrise) / (float)(w->sunset - w->sunrise);
  if(prog >= 0.0f && prog <= 1.0f){
    arcStroke((float)cx, (float)cy, (float)r, 180, 180 + 180 * prog, 3, rgb565(255,206,96));
    float ang = (180.0f + 180.0f * prog) * 0.0174532925f;
    int px_ = cx + (int)(r * cosf(ang)), py_ = cy + (int)(r * sinf(ang));
    fillCircleA(px_, py_, 12, rgb565(255,214,110), (uint8_t)(a / 4));
    fillCircleA(px_, py_, 7, rgb565(255,224,140), a);
  }
  hLineA(WX_CARD_X + 22, cy, WX_CARD_W - 44, WX_LINE, (uint8_t)(150 * a / 255));

  char t1[16], t2[16];
  wxFmtClock(w->sunrise + w->utcOffset, t1, sizeof(t1));
  wxFmtClock(w->sunset  + w->utcOffset, t2, sizeof(t2));
  drawTextA(WX_CARD_X + 22, cy + 10, wt(WT_SUNRISE), 1, WX_TXT_MUTE, a);
  drawTextA(WX_CARD_X + 22, cy + 24, t1, 2, WX_TXT_HI, a);
  int rw1 = textW(wt(WT_SUNSET), 1), rw2 = textW(t2, 2);
  drawTextA(WX_CARD_X + WX_CARD_W - 22 - rw1, cy + 10, wt(WT_SUNSET), 1, WX_TXT_MUTE, a);
  drawTextA(WX_CARD_X + WX_CARD_W - 22 - rw2, cy + 24, t2, 2, WX_TXT_HI, a);
}

// #############################################################
// ##  TARJETAS DE DETALLE  (solo variables REALMENTE recibidas)
// #############################################################
static const char* wxUvLabel(float uv){
  int u = (int)lroundf(uv);
  static const char* s0[5] = { "Bajo", "Low", "Faible", "Baixo", "Basso" };
  static const char* s1[5] = { "Moderado", "Moderate", "Mod\xC3\xA9r\xC3\xA9", "Moderado", "Moderato" };
  static const char* s2[5] = { "Alto", "High", "\xC3\x89lev\xC3\xA9", "Alto", "Alto" };
  static const char* s3[5] = { "Muy alto", "Very high", "Tr\xC3\xA8s \xC3\xA9lev\xC3\xA9", "Muito alto", "Molto alto" };
  static const char* s4[5] = { "Extremo", "Extreme", "Extr\xC3\xAAme", "Extremo", "Estremo" };
  int L = LI();
  if(u <= 2) return s0[L];
  if(u <= 5) return s1[L];
  if(u <= 7) return s2[L];
  if(u <= 10) return s3[L];
  return s4[L];
}
static const char* wxWindDirLabel(int deg){
  static const char* P[8] = { "N", "NE", "E", "SE", "S", "SO", "O", "NO" };
  static const char* E[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  int idx = ((deg + 22) / 45) % 8;
  if(idx < 0) idx = 0;
  return (cfgLang == 1) ? E[idx] : P[idx];
}

static void wxDetailAt(int idx, char* title, size_t tn, char* val, size_t vn, char* hint, size_t hn){
  const FlexWeather* w = flexWeatherData();
  title[0] = val[0] = hint[0] = 0;
  if(!w) return;
  switch(idx){
    case 0:
      snprintf(title, tn, "%s", wt(WT_HUMIDITY));
      if(w->have & WXF_HUMIDITY) snprintf(val, vn, "%d %%", (int)lroundf(w->humidity));
      else snprintf(val, vn, "%s", wt(WT_NA));
      if(w->have & WXF_FEELS){ snprintf(hint, hn, "%s %d C", wt(WT_FEELS), (int)lroundf(w->feels)); }
      break;
    case 1:
      snprintf(title, tn, "%s", wt(WT_WIND));
      if(w->have & WXF_WIND) snprintf(val, vn, "%d km/h", (int)lroundf(w->windSpeed));
      else snprintf(val, vn, "%s", wt(WT_NA));
      if(w->have & WXF_GUST) snprintf(hint, hn, "%s %d km/h", wt(WT_GUST), (int)lroundf(w->windGust));
      else if(w->have & WXF_WINDDIR) snprintf(hint, hn, "%s", wxWindDirLabel(w->windDir));
      break;
    case 2:
      snprintf(title, tn, "%s", wt(WT_UV));
      if(w->have & WXF_UV){ snprintf(val, vn, "%d", (int)lroundf(w->uv)); snprintf(hint, hn, "%s", wxUvLabel(w->uv)); }
      else snprintf(val, vn, "%s", wt(WT_NA));
      break;
    case 3:
      snprintf(title, tn, "%s", wt(WT_VISIBILITY));
      if(w->have & WXF_VIS){
        float km = w->visibility / 1000.0f;
        if(km >= 10.0f) snprintf(val, vn, "%d km", (int)lroundf(km));
        else            snprintf(val, vn, "%.1f km", (double)km);
      } else snprintf(val, vn, "%s", wt(WT_NA));
      break;
    case 4:
      snprintf(title, tn, "%s", wt(WT_PRECIP));
      if(w->have & WXF_PRECIP) snprintf(val, vn, "%.1f mm", (double)w->precip);
      else snprintf(val, vn, "%s", wt(WT_NA));
      if(w->have & WXF_POP) snprintf(hint, hn, "%d %%", (int)w->pop);
      break;
    case 5:
      snprintf(title, tn, "%s", wt(WT_FEELS));
      if(w->have & WXF_FEELS) snprintf(val, vn, "%d", (int)lroundf(w->feels));   // el grado lo dibuja la tarjeta
      else snprintf(val, vn, "%s", wt(WT_NA));
      if(w->have & WXF_CLOUD) snprintf(hint, hn, "%d %% nub.", (int)lroundf(w->cloud));
      break;
    default:
      snprintf(title, tn, "%s", wt(WT_PRESSURE));
      if(w->have & WXF_PRESSURE) snprintf(val, vn, "%d hPa", (int)lroundf(w->pressure));
      else snprintf(val, vn, "%s", wt(WT_NA));
      break;
  }
}

static void wxDrawDetails(int y0, int y1){
  const FlexWeather* w = flexWeatherData();
  if(!w) return;
  int off; uint8_t a = wxAnimA(4, &off);
  int top = wxYDet - (int)wxScroll + off;
  if(top > y1 || top + wxHDet < y0) return;
  int cw = (WX_CARD_W - 12) / 2;
  for(int i = 0; i < wxDetailCount(); i++){
    int r = i / 2, c = i % 2;
    int x = WX_CARD_X + c * (cw + 12);
    int y = top + r * (WX_DET_H + 12);
    if(y > y1 || y + WX_DET_H < y0) continue;
    wxCard(x, y, cw, WX_DET_H, a);
    char ti[40], va[40], hi[40];
    wxDetailAt(i, ti, sizeof(ti), va, sizeof(va), hi, sizeof(hi));
    drawTextA(x + 18, y + 18, ti, 2, WX_TXT_LO, a);
    int vs = (textW(va, 5) > cw - 34) ? 4 : 5;
    if(textW(va, vs) > cw - 34) vs = 3;
    int vex = drawTextA(x + 18, y + 46, va, vs, WX_TXT_HI, a);
    // Sensacion termica: es una temperatura, asi que lleva su grado dibujado
    // (la fuente no tiene el simbolo) en vez de una "C" suelta.
    if(i == 5 && (w->have & WXF_FEELS)) wxRingA(vex + vs + 2, y + 46 + vs, vs, 2, WX_TXT_HI, a);
    if(hi[0]) drawTextA(x + 18, y + WX_DET_H - 24, hi, 1, WX_TXT_MUTE, a);
  }
}

// #############################################################
// ##  PIE: atribucion + estado de la ultima actualizacion
// #############################################################
static void wxStatusLine(char* out, size_t n){
  out[0] = 0;
  uint8_t st = flexWeatherStatus();
  if(st == WXS_UPDATING || st == WXS_LOADING){ snprintf(out, n, "%s", wt(WT_UPDATING)); return; }
  int32_t age = flexWeatherAgeSec();
  if(age >= 0){
    int mins = age / 60;
    if(mins <= 0){
      static const char* s[5] = { "Actualizado ahora", "Updated just now", "Mis \xC3\xA0 jour \xC3\xA0 l'instant",
                                  "Atualizado agora", "Aggiornato ora" };
      snprintf(out, n, "%s", s[LI()]);
    } else snprintf(out, n, wt(WT_UPDATED_AGO), mins);
  } else {
    const FlexWeather* w = flexWeatherData();
    if(w && w->obsTime){
      char hh[16]; wxFmtClock(w->obsTime + w->utcOffset, hh, sizeof(hh));
      snprintf(out, n, wt(WT_UPDATED_AT), hh);
    }
  }
  if(st == WXS_STALE){
    size_t l = strlen(out);
    snprintf(out + l, (n > l) ? (n - l) : 0, "%s%s", l ? "  ·  " : "", wt(WT_OFFLINE));
  }
}

static void wxDrawFooter(int y0, int y1){
  if(!flexWeatherData()) return;
  int off; uint8_t a = wxAnimA(5, &off);
  int sy = wxYFoot - (int)wxScroll + off;
  if(sy > y1 || sy + 70 < y0) return;
  // El pie cae sobre el fondo (no sobre una tarjeta): color propio, mas claro,
  // para que se lea igual de bien en tema claro y oscuro.
  uint16_t fc = TH_ONWALL2;
  char st[80]; wxStatusLine(st, sizeof(st));
  if(st[0]) drawTextCA(SCR_W / 2, sy + 6, st, 1, fc, a);
  drawTextCA(SCR_W / 2, sy + 26, wt(WT_ATTRIB), 1, fc, (uint8_t)(a * 3 / 4));
}

// #############################################################
// ##  ESTADO VACIO  ·  jamas una temperatura inventada
// #############################################################
// Geometria compartida por el dibujo y por los toques: una sola
// definicion, asi que las zonas tactiles coinciden EXACTAMENTE con
// lo que se ve (requisito 29).
#define WX_EMPTY_B1_Y  470
#define WX_EMPTY_B2_Y  536
#define WX_EMPTY_B_H    54
#define WX_EMPTY_B_X    90
#define WX_EMPTY_B_W   (SCR_W - 180)

static void wxDrawSpinner(int cx, int cy, int r, uint16_t col, uint8_t a){
  uint32_t ms = millis();
  float a0 = (ms % 1000) * 0.36f;
  for(int i = 0; i < 8; i++){
    float ang = (a0 + i * 45.0f) * 0.0174532925f;
    int x = cx + (int)(r * cosf(ang)), y = cy + (int)(r * sinf(ang));
    fillCircleA(x, y, 3, col, (uint8_t)((uint32_t)a * (uint32_t)(40 + i * 27) / 255u));
  }
}

static void wxComposeEmpty(int y0, int y1){
  uint8_t st = flexWeatherStatus();
  bool loading = (st == WXS_LOADING);
  bool noLoc   = (flexWeatherLocCount() == 0);
  uint16_t W = TH_ONWALL;
  int off; uint8_t a = wxAnimA(0, &off);

  wxDrawIcon(SCR_W / 2, 250 + off, 46, noLoc ? WXV_PARTLY : WXV_CLOUDY, true, (uint8_t)(a * 3 / 4));

  if(loading){
    wxDrawSpinner(SCR_W / 2, 380, 16, W, a);
    drawTextCA(SCR_W / 2, 420 + off, wt(WT_LOADING), 2, TH_ONWALL2, a);
    return;
  }
  drawTextCA(SCR_W / 2, 356 + off, wt(WT_NODATA), 4, W, a);
  const char* sub = noLoc ? wt(WT_NOLOCS) : wt(WT_NODATA_SUB);
  wxWrapText(40, 404 + off, SCR_W - 80, sub, 2, TH_ONWALL2, a, 24, 2, true);

  uint8_t err = flexWeatherError();
  if(!noLoc && err != WXE_NONE)
    drawTextCA(SCR_W / 2, 444 + off, flexWeatherErrorText(err, cfgLang), 1, rgb565(190,204,230), a);

  // Boton primario
  const char* b1 = noLoc ? wt(WT_ADDLOC) : wt(WT_RETRY);
  fillRoundRectA(WX_EMPTY_B_X, WX_EMPTY_B1_Y + off, WX_EMPTY_B_W, WX_EMPTY_B_H, 27, W, (uint8_t)(230 * a / 255));
  drawTextCA(SCR_W / 2, WX_EMPTY_B1_Y + off + 18, b1, 3, rgb565(18,26,48), a);
  // Boton secundario
  const char* b2 = noLoc ? wt(WT_RETRY) : wt(WT_MYLOCS);
  fillRoundRectA(WX_EMPTY_B_X, WX_EMPTY_B2_Y + off, WX_EMPTY_B_W, WX_EMPTY_B_H, 27, W, (uint8_t)(60 * a / 255));
  drawTextCA(SCR_W / 2, WX_EMPTY_B2_Y + off + 18, b2, 3, W, a);
}

// #############################################################
// ##  MIS UBICACIONES
// #############################################################
#define WX_LOC_TOP   130
#define WX_LOC_ROW    78
#define WX_LOC_ADD_Y (SCR_H - 148)

static void wxComposeLocs(int y0, int y1){
  uint16_t W = TH_ONWALL;
  int off; uint8_t a = wxAnimA(0, &off);
  // cabecera con chevron de vuelta
  wxLineA(34, 56, 24, 66, 2, W, a);
  wxLineA(24, 66, 34, 76, 2, W, a);
  drawTextA(56, 56 + off, wt(WT_MYLOCS), 4, W, a);

  uint8_t n = flexWeatherLocCount();
  int8_t sel = flexWeatherLocSel();
  for(uint8_t i = 0; i < n; i++){
    int y = WX_LOC_TOP + i * WX_LOC_ROW + off;
    if(y > y1 || y + WX_LOC_ROW - 10 < y0) continue;
    const FlexWxLoc* l = flexWeatherLocAt(i);
    if(!l) continue;
    bool cur = (sel == (int8_t)i);
    wxCard(WX_CARD_X, y, WX_CARD_W, WX_LOC_ROW - 12, (uint8_t)(cur ? 255 : a));
    if(cur){
      drawRoundRect(WX_CARD_X, y, WX_CARD_W, WX_LOC_ROW - 12, 26, WX_ACCENT);
      drawRoundRect(WX_CARD_X + 1, y + 1, WX_CARD_W - 2, WX_LOC_ROW - 14, 25, WX_ACCENT);
      fillRoundRectA(WX_CARD_X, y, WX_CARD_W, WX_LOC_ROW - 12, 26, WX_ACCENT, 26);
    }
    wxPin(44, y + 30, 8, cur ? WX_ACCENT : WX_TXT_LO, a);
    drawTextA(66, y + 14, l->name, 3, WX_TXT_HI, a);
    if(l->region[0]) drawTextA(66, y + 42, l->region, 1, WX_TXT_MUTE, a);
    // papelera (zona tactil exacta: 60x60 a la derecha)
    int tx = WX_CARD_X + WX_CARD_W - 44;
    int ty = y + 22;
    fillRoundRectA(tx - 9, ty + 2, 18, 16, 3, WX_TXT_MUTE, a);
    fillRectA(tx - 12, ty - 2, 24, 3, WX_TXT_MUTE, a);
    fillRectA(tx - 4, ty - 7, 8, 5, WX_TXT_MUTE, a);
  }
  if(n == 0) drawTextCA(SCR_W / 2, WX_LOC_TOP + 40, wt(WT_NOLOCS), 2, TH_ONWALL2, a);

  // Boton de anadir
  fillRoundRectA(WX_CARD_X, WX_LOC_ADD_Y, WX_CARD_W, 56, 28, W, (uint8_t)(230 * a / 255));
  drawTextCA(SCR_W / 2, WX_LOC_ADD_Y + 18, wt(WT_ADDLOC), 3, rgb565(18,26,48), a);
}

// #############################################################
// ##  BUSCADOR DE CIUDADES  (Open-Meteo Geocoding)
// #############################################################
// El buscador usa EL TECLADO DEL SISTEMA (mapaActivo + kbPaintKey + kbCellAt),
// el mismo que la pantalla de Wi-Fi: con acentos, capas y cambio de idioma. Un
// nombre como "Bogot\xC3\xA1" o "Cusco" se escribe igual que en cualquier otro
// campo del equipo, y no hay un segundo teclado que mantener.
#define WX_SR_TOP   174
#define WX_SR_ROW    60

// Filas de resultados visibles: las que caben ENTRE la cabecera y el panel del
// teclado. Sale de kbPanelTop(), asi que si el teclado cambia de tamano
// (Ajustes -> Teclado) esto se ajusta solo.
static int wxSearchRows(){
  int room = kbPanelTop() - 8 - WX_SR_TOP;
  int n = room / WX_SR_ROW;
  if(n < 1) n = 1;
  return n > FLEXWX_RESULTS ? FLEXWX_RESULTS : n;
}

static void wxComposeSearch(int y0, int y1){
  uint16_t W = TH_ONWALL;
  wxLineA(34, 46, 24, 56, 2, W, 255);
  wxLineA(24, 56, 34, 66, 2, W, 255);
  drawTextA(56, 46, wt(WT_SEARCH), 4, W, 255);

  // Campo de texto: superficie del tema apoyada en la escena.
  fillRoundRectA(WX_CARD_X, 96, WX_CARD_W, 56, 28, TH_SURF, 240);
  uint16_t fieldTxt = onColor(TH_SURF);
  if(wxQuery[0] == 0) drawTextA(WX_CARD_X + 24, 114, wt(WT_SEARCH_HINT), 2, mix565(fieldTxt, TH_SURF, 120), 255);
  else {
    int ex = drawTextClip(WX_CARD_X + 24, 112, wxQuery, 3, fieldTxt, WX_CARD_X + WX_CARD_W - 22);
    if((millis() / 500) % 2 && ex < WX_CARD_X + WX_CARD_W - 26) fillRect(ex + 3, 110, 3, 28, wallAccent());
  }

  uint8_t st = flexWeatherSearchState();
  if(st == WXQ_BUSY){
    wxDrawSpinner(SCR_W / 2, WX_SR_TOP + 40, 14, W, 255);
    drawTextCA(SCR_W / 2, WX_SR_TOP + 76, wt(WT_SEARCHING), 2, TH_ONWALL2, 255);
  } else if(st == WXQ_EMPTY){
    drawTextCA(SCR_W / 2, WX_SR_TOP + 40, wt(WT_NORESULT), 2, TH_ONWALL2, 255);
  } else if(st == WXQ_ERROR){
    drawTextCA(SCR_W / 2, WX_SR_TOP + 30, wt(WT_NORESULT), 2, TH_ONWALL2, 255);
    drawTextCA(SCR_W / 2, WX_SR_TOP + 58, flexWeatherErrorText(flexWeatherError(), cfgLang), 1, TH_ONWALL2, 255);
  } else {
    uint8_t n = flexWeatherSearchCount();
    uint8_t vis = (uint8_t)wxSearchRows();
    if(n > vis) n = vis;
    for(uint8_t i = 0; i < n; i++){
      const FlexWxLoc* l = flexWeatherSearchAt(i);
      if(!l) continue;
      int y = WX_SR_TOP + i * WX_SR_ROW;
      wxCard(WX_CARD_X, y, WX_CARD_W, WX_SR_ROW - 8, 255);
      drawTextClip(WX_CARD_X + 20, y + 8, l->name, 3, WX_TXT_HI, WX_CARD_X + WX_CARD_W - 16);
      if(l->region[0]) drawTextClip(WX_CARD_X + 20, y + 34, l->region, 1, WX_TXT_MUTE, WX_CARD_X + WX_CARD_W - 16);
    }
  }

  // ---- Teclado del sistema, dibujado con SUS propias funciones ----
  int ky = KB_Y;
  if(uiGlass) drawLiquidGlassPanel(0, kbPanelTop(), SCR_W, SCR_H - kbPanelTop(), 0, kbColPanel());
  else        fillRect(0, kbPanelTop(), SCR_W, SCR_H - kbPanelTop(), kbColPanel());
  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    char u[6];
    const char* k = kbResolveKey(mapaActivo[r][c], u, false);
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, kbColKey(), kbColKeyTxt(), false);
  }
  int fy = kbFuncY();
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", wt(WT_SEARCH) };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
}

// #############################################################
// ##  CROMO FIJO: barra de estado, navegacion, toast, pull
// #############################################################
static void wxDrawChrome(int y0, int y1){
  uint16_t W = TH_ONWALL;
  if(y0 <= 48){
    // Misma llamada que el Inicio y que el marco de app: hora + capsula del
    // cronometro con UNA sola geometria en todo el sistema.
    cronoBarClock(16, W);
    drawWifi(SCR_W - 66, 28, 11, W);
    drawBattery(SCR_W - 46, 20, 30, 15, 82, W);
  }
  // En el buscador la barra de navegacion NO se dibuja: el teclado del
  // sistema ocupa esa misma franja y pintar encima seria una zona tactil
  // enganosa (se ve un boton "atras" que en realidad es la barra espaciadora).
  // Se sale con el chevron de la cabecera.
  if(wxView == WXVIEW_SEARCH) return;
  if(y1 >= SCR_H - WX_NAV_H){
    // velo inferior: el contenido que pasa por debajo no se come la barra
    for(int y = SCR_H - WX_NAV_H; y < SCR_H; y++){
      if(y < y0 || y > y1) continue;
      hLineA(0, y, SCR_W, rgb565(8,12,26), (uint8_t)(40 + (y - (SCR_H - WX_NAV_H)) * 3));
    }
    if(gNavMode == 0){
      int ny = SCR_H - 52;
      fillTriangle(SCR_W / 6 - 10, ny + 8, SCR_W / 6 + 8, ny - 2, SCR_W / 6 + 8, ny + 18, W);
      drawCircle(SCR_W / 2, ny + 8, 12, W); drawCircle(SCR_W / 2, ny + 8, 11, W);
      drawRoundRect(SCR_W * 5 / 6 - 11, ny - 3, 22, 22, 4, W);
    } else {
      drawHomeIndicator(SCR_H, 180);
    }
  }
}

static void wxDrawPull(int y0, int y1){
  if(wxView != WXVIEW_MAIN) return;
  if(y0 > 120) return;
  uint16_t W = TH_ONWALL;

  // Remate de exito: una marca de verificacion breve cuando la actualizacion
  // ha ido bien. Dura menos de un segundo y se apaga sola.
  if(wxOkFlashMs){
    uint32_t el = millis() - wxOkFlashMs;
    if(el < 1000){
      uint8_t a = (el < 700) ? 255 : (uint8_t)((1000 - el) * 255 / 300);
      int cx = SCR_W / 2, cy = 44;
      fillCircleA(cx, cy, 16, rgb565(90,200,140), (uint8_t)(a / 3));
      wxLineA(cx - 7, cy + 1, cx - 2, cy + 6, 1, W, a);
      wxLineA(cx - 2, cy + 6, cx + 7, cy - 5, 1, W, a);
      return;
    }
    wxOkFlashMs = 0;
  }
  int over = (wxScroll < 0) ? (int)(-wxScroll) : 0;
  if(!over && !wxPullBusy) return;
  int cy = 42 + (over > 90 ? 90 : over) / 2;
  if(wxPullBusy || flexWeatherBusy()){
    wxDrawSpinner(SCR_W / 2, cy, 13, W, 255);
  } else {
    uint8_t a = (uint8_t)(over > 70 ? 255 : (over * 255 / 70));
    // arco que se completa conforme se estira: es el propio progreso
    float sweep = (over > 70 ? 70 : over) * 360.0f / 70.0f;
    arcStroke(SCR_W / 2, cy, 13, -90, -90 + sweep, 3, W);
    drawTextCA(SCR_W / 2, cy + 22, over > 70 ? wt(WT_RELEASE) : wt(WT_PULL), 1, W, a);
  }
}

static void wxToast(const char* s){
  snprintf(wxToastTxt, sizeof(wxToastTxt), "%s", s);
  wxToastMs = millis();
}
static void wxDrawToast(int y0, int y1){
  if(!wxToastMs) return;
  uint32_t el = millis() - wxToastMs;
  if(el > 2600){ wxToastMs = 0; return; }
  uint8_t a = 255;
  if(el < 160) a = (uint8_t)(el * 255 / 160);
  else if(el > 2200) a = (uint8_t)((2600 - el) * 255 / 400);
  int tw = textW(wxToastTxt, 2) + 40, tx = (SCR_W - tw) / 2, ty = SCR_H - 132;
  if(ty > y1 || ty + 44 < y0) return;
  fillRoundRectA(tx, ty, tw, 44, 22, rgb565(12,16,30), (uint8_t)(a * 220 / 255));
  drawTextCA(SCR_W / 2, ty + 14, wxToastTxt, 2, rgb565(238,242,250), a);
}

// #############################################################
// ##  COMPOSICION  ·  siempre en bbuf, siempre por bandas
// #############################################################
static void wxCompose(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y0 > y1) return;
  setBuf(bbuf);
  int ox0 = gClipX0, ox1 = gClipX1, oy0 = gClipY0, oy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y1;

  wxDrawScene(y0, y1);                       // fondo + escena animada

  if(wxView != WXVIEW_MAIN){
    // Subpantallas: un velo sobre la escena. El paisaje sigue detras (es la
    // identidad de la app) pero las tarjetas y el teclado quedan nitidos.
    for(int y = y0; y <= y1; y++) hLineA(0, y, SCR_W, rgb565(6,10,26), 120);
  }
  if(wxView == WXVIEW_LOCS)        wxComposeLocs(y0, y1);
  else if(wxView == WXVIEW_SEARCH) wxComposeSearch(y0, y1);
  else if(!flexWeatherData())      wxComposeEmpty(y0, y1);
  else {
    wxDrawHero(y0, y1);
    wxDrawHourly(y0, y1);
    wxDrawDaily(y0, y1);
    wxDrawSun(y0, y1);
    wxDrawDetails(y0, y1);
    wxDrawFooter(y0, y1);
    wxDrawPull(y0, y1);
  }
  wxDrawChrome(y0, y1);
  wxDrawToast(y0, y1);

  gClipX0 = ox0; gClipX1 = ox1; gClipY0 = oy0; gClipY1 = oy1;
  setBuf(fb);
}
static void wxPresent(int y0, int y1){
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y0 > y1) return;
  wxCompose(y0, y1);
  present(y0, y1);
}
static inline void wxFull(){ wxPresent(0, SCR_H - 1); }

// Banda que ocupa la escena animada ahora mismo (la unica parte que se
// mueve sola). Si esta fuera de pantalla, no hay nada que repintar.
static bool wxSceneBand(int* y0, int* y1){
  if(wxView != WXVIEW_MAIN) return false;
  int bot = WX_HERO_H - (int)(wxScroll * 0.45f);
  if(bot <= 0) return false;
  *y0 = 0;
  *y1 = bot > SCR_H - 1 ? SCR_H - 1 : bot;
  return true;
}

// #############################################################
// ##  FISICA DEL DESPLAZAMIENTO
// ##  Seguimiento del dedo + inercia + rebote elastico, todo por
// ##  tiempo real (dt de millis()). Sin bucles bloqueantes.
// #############################################################
static uint32_t wxPhysMs = 0;
static bool wxPhysics(){
  uint32_t now = millis();
  float dt = (wxPhysMs == 0) ? 0.033f : (now - wxPhysMs) / 1000.0f;
  wxPhysMs = now;
  if(dt <= 0.0f || dt > 0.25f) dt = 0.033f;
  bool ch = false;
  int maxS = wxScrollMax();

  if(!T.down){
    if(fabsf(wxScrollVel) > 6.0f){
      wxScroll += wxScrollVel * dt;
      wxScrollVel *= powf(0.05f, dt);            // desaceleracion suave
      ch = true;
    } else wxScrollVel = 0;
    // rebote elastico a los limites
    if(wxScroll < 0){
      wxScroll += (0.0f - wxScroll) * (1.0f - powf(0.002f, dt));
      if(wxScroll > -0.6f) wxScroll = 0;
      wxScrollVel = 0; ch = true;
    } else if(wxScroll > maxS){
      wxScroll += ((float)maxS - wxScroll) * (1.0f - powf(0.002f, dt));
      if(wxScroll < maxS + 0.6f) wxScroll = (float)maxS;
      wxScrollVel = 0; ch = true;
    }
    if(fabsf(wxHourVel) > 6.0f){
      wxHourScroll += wxHourVel * dt;
      wxHourVel *= powf(0.05f, dt);
      ch = true;
    } else wxHourVel = 0;
    if(wxHourScroll < 0){ wxHourScroll += (0.0f - wxHourScroll) * (1.0f - powf(0.002f, dt)); if(wxHourScroll > -0.6f) wxHourScroll = 0; wxHourVel = 0; ch = true; }
    else if(wxHourScroll > wxHourMax){ wxHourScroll += ((float)wxHourMax - wxHourScroll) * (1.0f - powf(0.002f, dt)); if(wxHourScroll < wxHourMax + 0.6f) wxHourScroll = (float)wxHourMax; wxHourVel = 0; ch = true; }
  }
  return ch;
}

// #############################################################
// ##  TOQUES
// ##  Regla del requisito 30: TODO estado de gesto se reinicia al
// ##  soltar y al cambiar de vista. Ningun flag puede quedarse
// ##  atrapado y bloquear el sistema.
// #############################################################
// Cancela el GESTO (no el movimiento): al soltar hay que conservar la
// velocidad, que es justo lo que da la inercia del desplazamiento.
static void wxResetGesture(){
  wxDragging = false; wxAxis = 0; wxTapValid = false;
  wxPullArmed = false;
}
// Cancela ademas cualquier movimiento en curso. Se usa al cambiar de vista y
// cuando el episodio tactil se pierde por otra via.
static void wxStopMotion(){
  wxResetGesture();
  wxScrollVel = 0; wxHourVel = 0;
}
static void wxGoView(uint8_t v){
  wxStopMotion();
  wxView = v;
  wxEnterMs = millis();
  wxAnimMs = 0;
  if(v == WXVIEW_SEARCH){ wxQuery[0] = 0; flexWeatherSearchClear(); }
  wxFull();
}

// Zona del panel horario en coordenadas de PANTALLA (misma cuenta que
// el dibujo: si cambia una, cambia la otra).
static bool wxInHourCard(int y){
  int sy = wxYHour - (int)wxScroll;
  return (y >= sy && y <= sy + WX_HOUR_H - 46);
}

static void wxTapMain(int x, int y){
  const FlexWeather* w = flexWeatherData();
  if(!w){
    int b1 = WX_EMPTY_B1_Y, b2 = WX_EMPTY_B2_Y;
    bool noLoc = (flexWeatherLocCount() == 0);
    if(x >= WX_EMPTY_B_X && x <= WX_EMPTY_B_X + WX_EMPTY_B_W){
      if(y >= b1 && y <= b1 + WX_EMPTY_B_H){
        if(noLoc) wxGoView(WXVIEW_SEARCH);
        else { flexWeatherRefresh(true); wxFull(); }
        return;
      }
      if(y >= b2 && y <= b2 + WX_EMPTY_B_H){
        if(noLoc){ flexWeatherRefresh(true); wxFull(); }
        else wxGoView(WXVIEW_LOCS);
        return;
      }
    }
    return;
  }
  int base = -(int)wxScroll;
  // Cabecera de ubicacion
  if(y >= base + 62 && y <= base + 112 && x <= 340){ wxGoView(WXVIEW_LOCS); return; }
  // Pie del panel horario: avanza una pagina (y vuelve al principio al final)
  int hy = wxYHour - (int)wxScroll;
  if(y >= hy + WX_HOUR_H - 44 && y <= hy + WX_HOUR_H && x >= SCR_W / 2){
    float target = wxHourScroll + 6 * WX_COL_W;
    if(target > wxHourMax) target = 0;
    wxHourVel = 0;
    wxHourScroll = target;
    wxFull();
    return;
  }
}

static void wxTouchMain(){
  if(T.pressed){
    wxDragging = false; wxAxis = 0; wxTapValid = true;
    wxDragX0 = T.x; wxDragY0 = T.y;
    wxDragScroll0 = wxScroll; wxDragHour0 = wxHourScroll;
    wxLastX = T.x; wxLastY = T.y; wxLastMoveMs = millis();
    wxScrollVel = 0; wxHourVel = 0;
  }
  if(T.down){
    int dx = T.x - wxDragX0, dy = T.y - wxDragY0;
    if(wxAxis == 0 && (abs(dx) > 9 || abs(dy) > 9)){
      // Eje dominante: el panel horario solo se lleva el gesto si el dedo
      // empezo DENTRO de su tarjeta y se mueve claramente en horizontal.
      if(abs(dx) > abs(dy) + 4 && wxInHourCard(wxDragY0)) wxAxis = 2;
      else wxAxis = 1;
      wxTapValid = false;                 // superado el umbral: ya no es un tap
      wxDragging = true;
    }
    if(wxAxis == 1){
      float ns = wxDragScroll0 - dy;
      int maxS = wxScrollMax();
      if(ns < 0)        ns *= 0.42f;                          // resistencia al estirar
      else if(ns > maxS) ns = maxS + (ns - maxS) * 0.42f;
      uint32_t now = millis();
      if(now > wxLastMoveMs){
        float v = (float)(wxLastY - T.y) * 1000.0f / (float)(now - wxLastMoveMs);
        wxScrollVel = wxScrollVel * 0.6f + v * 0.4f;
      }
      wxLastY = T.y; wxLastMoveMs = now;
      if((int)ns != (int)wxScroll){ wxScroll = ns; wxFull(); }
      else wxScroll = ns;
      wxPullArmed = (wxScroll <= -70.0f);
    } else if(wxAxis == 2){
      float ns = wxDragHour0 - dx;
      if(ns < 0) ns *= 0.42f;
      else if(ns > wxHourMax) ns = wxHourMax + (ns - wxHourMax) * 0.42f;
      uint32_t now = millis();
      if(now > wxLastMoveMs){
        float v = (float)(wxLastX - T.x) * 1000.0f / (float)(now - wxLastMoveMs);
        wxHourVel = wxHourVel * 0.6f + v * 0.4f;
      }
      wxLastX = T.x; wxLastMoveMs = now;
      if((int)ns != (int)wxHourScroll){
        wxHourScroll = ns;
        int hy = wxYHour - (int)wxScroll;
        wxPresent(hy - 2, hy + WX_HOUR_H + 2);        // solo la tarjeta horaria
      } else wxHourScroll = ns;
    }
  }
  if(T.released){
    bool doTap = wxTapValid && T.tap;
    bool pull  = wxPullArmed && wxAxis == 1;
    wxResetGesture();                                 // el estado NUNCA se queda colgado
    if(pull){
      wxPullBusy = true; wxPullMs = millis();
      flexWeatherRefresh(true);
      wxFull();
    }
    if(doTap) wxTapMain(T.x, T.y);
  }
}

static void wxTouchLocs(){
  if(!T.tap) return;
  int x = T.x, y = T.y;
  if(y < 96 && x < 96){ wxGoView(WXVIEW_MAIN); return; }               // chevron atras
  if(y >= WX_LOC_ADD_Y && y <= WX_LOC_ADD_Y + 56 && x >= WX_CARD_X && x <= WX_CARD_X + WX_CARD_W){
    wxGoView(WXVIEW_SEARCH); return;
  }
  uint8_t n = flexWeatherLocCount();
  for(uint8_t i = 0; i < n; i++){
    int ry = WX_LOC_TOP + i * WX_LOC_ROW;
    if(y < ry || y > ry + WX_LOC_ROW - 12) continue;
    if(x < WX_CARD_X || x > WX_CARD_X + WX_CARD_W) return;
    if(x >= WX_CARD_X + WX_CARD_W - 74){                                // papelera
      flexWeatherLocRemove(i);
      wxLayout();
      wxFull();
    } else {
      flexWeatherLocSelect(i);
      wxScroll = 0; wxHourScroll = 0;
      wxLayout();
      wxGoView(WXVIEW_MAIN);
    }
    return;
  }
}

// Pasa la consulta a "Tipo Titulo": las ciudades se llaman asi y el
// teclado del sistema escribe en mayusculas.
// Anade texto a la consulta respetando UTF-8: las teclas del teclado del
// sistema pueden ser de varios bytes ("\xC3\xB1", "\xC3\xA1"...).
static void wxQueryAppend(const char* k){
  if(!k || !k[0]) return;
  size_t L = strlen(wxQuery), n = strlen(k);
  if(L + n >= sizeof(wxQuery)) return;
  memcpy(wxQuery + L, k, n);
  wxQuery[L + n] = 0;
}
static void wxQueryBackspace(){
  int L = (int)strlen(wxQuery);
  if(L <= 0) return;
  wxQuery[utf8Prev(wxQuery, L)] = 0;      // borra el CARACTER, no el byte
}
// Lanza la busqueda. El texto se manda tal cual lo escribio el usuario: el
// geocoding de Open-Meteo no distingue mayusculas y acepta acentos.
static void wxQuerySubmit(){
  if(wxQuery[0] == 0) return;
  flexWeatherSearch(wxQuery, cfgLang);
}

static void wxTouchSearch(){
  if(!T.tap) return;
  int x = T.x, y = T.y;
  if(y < 90 && x < 96){ wxGoView(WXVIEW_LOCS); return; }
  // resultado tocado -> se guarda y se selecciona (persistente en NVS)
  if(flexWeatherSearchState() == WXQ_OK){
    uint8_t n = flexWeatherSearchCount();
    uint8_t vis = (uint8_t)wxSearchRows();
    if(n > vis) n = vis;
    for(uint8_t i = 0; i < n; i++){
      int ry = WX_SR_TOP + i * WX_SR_ROW;
      if(y >= ry && y <= ry + WX_SR_ROW - 8 && x >= WX_CARD_X && x <= WX_CARD_X + WX_CARD_W){
        const FlexWxLoc* l = flexWeatherSearchAt(i);
        if(l && flexWeatherLocAdd(l)){
          wxScroll = 0; wxHourScroll = 0;
          wxLayout();
          wxGoView(WXVIEW_MAIN);
        } else {
          wxToast(wt(WT_FAILREFRESH));
          wxFull();
        }
        return;
      }
    }
  }
  // ---- Teclado del sistema: MISMO despacho que la pantalla de Wi-Fi ----
  int fi = kbFRowHit(x, y);
  if(fi >= 0){
    if(fi == 0)      kbShift = !kbShift;
    else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI
                                : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
    else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
    else if(fi == 3) wxQueryAppend(" ");
    else if(fi == 4) wxQueryBackspace();
    else             wxQuerySubmit();
    wxFull();
    return;
  }
  int cell = kbCellAt(x, y);
  if(cell < 0) return;
  char u[6];
  const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
  wxQueryAppend(k);
  wxFull();
}

// Vuelta atras del boton del sistema: primero cierra la subpantalla.
// Mismo patron que Ajustes (settingsHandleBack).
static bool wxHandleBack(){
  if(wxView == WXVIEW_SEARCH){ wxGoView(WXVIEW_LOCS); return true; }
  if(wxView == WXVIEW_LOCS){   wxGoView(WXVIEW_MAIN); return true; }
  return false;
}

// Suspension real: cancela exclusivamente el gesto/inercia en curso (el dedo
// que vuelve sera otro), pero conserva vista, consulta y posiciones de scroll.
static void wxSuspend(){
  if(mapaActivo == LAYOUT_EN)         wxKbLayout = 1;
  else if(mapaActivo == LAYOUT_NUM)   wxKbLayout = 2;
  else if(mapaActivo == LAYOUT_EMOJI) wxKbLayout = 3;
  else                                wxKbLayout = 0;
  wxKbFlags = (kbLangEs ? 1u : 0u) | (kbShift ? 2u : 0u);
  wxStopMotion();
  wxPhysMs = 0;
}

static void wxResume(){
  switch(wxKbLayout){
    case 1: mapaActivo = LAYOUT_EN; break;
    case 2: mapaActivo = LAYOUT_NUM; break;
    case 3: mapaActivo = LAYOUT_EMOJI; break;
    default: mapaActivo = LAYOUT_ES; break;
  }
  kbLangEs = (wxKbFlags & 1u) != 0;
  kbShift  = (wxKbFlags & 2u) != 0;
  wxStopMotion();
  wxLayout();
  int maxS = wxScrollMax();
  if(wxScroll < 0) wxScroll = 0;
  if(wxScroll > maxS) wxScroll = (float)maxS;
  if(wxHourScroll < 0) wxHourScroll = 0;
  if(wxHourScroll > wxHourMax) wxHourScroll = (float)wxHourMax;
  wxSeenGen = flexWeatherGen();
  wxSeenStatus = flexWeatherStatus();
  wxEnterMs = 0;                    // sin repetir la animacion de apertura
  wxAnimMs = 0; wxPhysMs = 0;
  wxFull();
}

// Boton "atras" de la barra de navegacion. Clima es APP_OWN_TOUCH (el teclado
// del buscador ocupa esa misma franja y, si lo gestionara el framework, la
// barra espaciadora cerraria la app), asi que la atiende la propia app y con
// la MISMA geometria que usa appTick para el resto: nada de zonas invisibles.
static bool wxNavBack(){
  if(!T.tap) return false;
  int ny = SCR_H - 52;
  if(T.y < ny - 10 || T.y > ny + 22 || T.x >= SCR_W / 3) return false;
  if(wxView == WXVIEW_SEARCH && (kbFRowHit(T.x, T.y) >= 0 || kbCellAt(T.x, T.y) >= 0)) return false;   // manda la tecla
  if(!wxHandleBack()) appClose();
  return true;
}

// #############################################################
// ##  ENTRADA / TICK DE LA APP
// #############################################################
static void wxAppEnter(){
  wxView = WXVIEW_MAIN;
  wxScroll = 0; wxScrollVel = 0;
  wxHourScroll = 0; wxHourVel = 0;
  wxResetGesture();
  wxPullBusy = false;
  wxToastMs = 0; wxToastTxt[0] = 0;
  wxQuery[0] = 0; wxSelLoc = -1;
  wxPhysMs = 0;
  flexWeatherSearchClear();
  wxSeenGen    = flexWeatherGen();
  wxSeenStatus = flexWeatherStatus();
  wxLayout();
  wxEnterMs = millis();
  wxAnimMs  = 0;
  // Refresco al abrir, respetando la cadencia minima del motor: NO se
  // descarga nada si lo que hay es reciente (requisito 18).
  flexWeatherRefresh(false);
  wxFull();
}

static void wxAppTick(){
  uint32_t now = millis();

  // SEGURIDAD DE GESTOS (requisito 30). Si el episodio tactil se fue por otra
  // via -- gesto de la barra iOS, suspension, kiosco, cambio de pantalla -- a
  // esta app no le llega el "released". Sin esto, wxDragging se quedaria en
  // true para siempre. En cuanto no hay dedo en pantalla, el gesto se cancela.
  // (T.released se deja pasar: ese frame lo procesa wxTouchMain, que es quien
  //  convierte el gesto en inercia. Cancelar aqui mataria el impulso.)
  if(!T.down && !T.released && (wxDragging || wxAxis)) wxStopMotion();
  if(wxNavBack()) return;

  // ---- 1) Datos nuevos publicados por el motor ----
  uint32_t gen = flexWeatherGen();
  uint8_t  st  = flexWeatherStatus();
  if(gen != wxSeenGen || st != wxSeenStatus){
    bool wasBusy = (wxSeenStatus == WXS_LOADING || wxSeenStatus == WXS_UPDATING);
    bool nowBusy = (st == WXS_LOADING || st == WXS_UPDATING);
    wxSeenGen = gen; wxSeenStatus = st;
    wxLayout();
    int maxS = wxScrollMax();
    if(wxScroll > maxS) wxScroll = (float)maxS;
    if(wxPullBusy && wasBusy && !nowBusy){
      wxPullBusy = false;
      if(flexWeatherError() != WXE_NONE) wxToast(wt(WT_FAILREFRESH));
      else wxOkFlashMs = now;
    }
    wxFull();
    return;
  }
  if(wxPullBusy && !flexWeatherBusy() && (now - wxPullMs) > 2500 && st != WXS_LOADING && st != WXS_UPDATING){
    // El motor ni siquiera llego a arrancar la descarga (sin Wi-Fi, sin
    // ubicacion...): no se deja el indicador girando para siempre.
    wxPullBusy = false;
    if(flexWeatherError() != WXE_NONE) wxToast(wt(WT_FAILREFRESH));
    wxFull();
    return;
  }

  // ---- 2) Toques ----
  if(wxView == WXVIEW_MAIN)        wxTouchMain();
  else if(wxView == WXVIEW_LOCS)   wxTouchLocs();
  else                             wxTouchSearch();

  // ---- 3) Inercia / rebote ----
  if(wxView == WXVIEW_MAIN && wxPhysics()){ wxFull(); return; }

  // ---- 4) Animaciones (entrada, escena, spinner, toast) ----
  uint32_t interval = 40;                       // ~25 fps: suave y barato
  if(now - wxAnimMs < interval) return;
  wxAnimMs = now;

  if(wxAnimRunning()){ wxFull(); return; }      // entrada escalonada de tarjetas

  if(wxToastMs && now - wxToastMs <= 2700){ wxPresent(SCR_H - 140, SCR_H - 76); }
  if(wxOkFlashMs && wxView == WXVIEW_MAIN){    // remate de "actualizado"
    wxPresent(0, 110);
    if(now - wxOkFlashMs >= 1000) wxOkFlashMs = 0;
    return;
  }

  if(wxView == WXVIEW_SEARCH){
    if(flexWeatherSearchState() == WXQ_BUSY) wxPresent(WX_SR_TOP - 10, WX_SR_TOP + 110);
    else if(wxQuery[0]) wxPresent(100, 160);    // parpadeo del cursor
    return;
  }
  if(wxView == WXVIEW_LOCS) return;             // nada que animar

  // Escena: solo su banda, y solo si de verdad se ve y tiene movimiento.
  WxScene sc; wxSceneBlend(&sc);
  int by0, by1;
  if(wxSceneAnimated(&sc) && wxSceneBand(&by0, &by1)) wxPresent(by0, by1);
  else if(flexWeatherBusy() || wxPullBusy)          wxPresent(0, 120);
}

// #############################################################
// ##  WIDGET DEL ESCRITORIO  ·  MISMO WeatherState que la app
// ##  ------------------------------------------------------
// ##  No consulta la red ni tiene logica propia de clima: lee el
// ##  estado publicado. Por construccion no puede ensenar un
// ##  numero distinto al de la app o al del bloqueo.
// #############################################################
static void wxHomeWidget(int x, int y, int w, int h, bool wide){
  uint16_t W = TH_ONWALL;
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 20, rgb565(30,72,150));
  else        fillRoundRect(x, y, w, h, 20, rgb565(28,58,120));

  const FlexWeather* d = flexWeatherData();
  if(!d){
    // Sin descarga valida: se dice, no se rellena con un numero falso.
    drawText(x + 16, y + 16, t(S_WEATHER), 2, W);
    drawText(x + 16, y + 48, wt(WT_NODATA), 1, TH_ONWALL2);
    const char* hint = (flexWeatherLocCount() == 0) ? wt(WT_ADDLOC) : wt(WT_RETRY);
    drawText(x + 16, y + h - 26, hint, 1, mix565(TH_ONWALL2, TH_WALLSURF, 90));
    return;
  }
  bool day = d->isDay ? true : false;
  uint8_t vis = flexWeatherVisual(d->code);

  if(!wide){
    // ---- Vista compacta (208 x 120) ----
    char nm[26]; wxCopy(nm, sizeof(nm), d->loc.name);
    while(textW(nm, 1) > w - 70 && strlen(nm) > 4) nm[strlen(nm) - 1] = 0;
    drawText(x + 16, y + 14, nm, 1, TH_ONWALL2);
    wxDrawTemp(x + 14, y + 32, d->temp, 6, W, 255);
    wxDrawIcon(x + w - 44, y + 44, 15, vis, day, 255);
    char cn[30]; wxCopy(cn, sizeof(cn), flexWeatherCondName(d->code, cfgLang));
    while(textW(cn, 1) > w - 26 && strlen(cn) > 4) cn[strlen(cn) - 1] = 0;
    drawText(x + 16, y + 88, cn, 1, TH_ONWALL2);
    if(d->have & WXF_MINMAX){
      wxArrowUp(x + 20, y + 108, 4, W);
      int ex = wxDrawTemp(x + 26, y + 102, d->tmax, 2, W, 255);
      wxArrowDown(ex + 8, y + 108, 4, W);
      wxDrawTemp(ex + 14, y + 102, d->tmin, 2, TH_ONWALL2, 255);
    }
    return;
  }

  // ---- Vista ancha (432 x 120): bloque de "ahora" + 4 horas REALES ----
  int lw = 200;                                   // ancho del bloque izquierdo
  char nm[30]; wxCopy(nm, sizeof(nm), d->loc.name);
  while(textW(nm, 2) > lw - 10 && strlen(nm) > 4) nm[strlen(nm) - 1] = 0;
  drawText(x + 16, y + 12, nm, 2, W);
  int tex = wxDrawTemp(x + 14, y + 36, d->temp, 6, W, 255);
  wxDrawIcon(tex + 22, y + 62, 15, vis, day, 255);
  char cn[34]; wxCopy(cn, sizeof(cn), flexWeatherCondName(d->code, cfgLang));
  while(textW(cn, 1) > lw - 10 && strlen(cn) > 4) cn[strlen(cn) - 1] = 0;
  drawText(x + 16, y + 94, cn, 1, TH_ONWALL2);
  if(d->have & WXF_MINMAX){
    wxArrowUp(x + 20, y + 112, 4, W);
    int ex = wxDrawTemp(x + 26, y + 106, d->tmax, 2, W, 255);
    wxArrowDown(ex + 8, y + 112, 4, W);
    wxDrawTemp(ex + 14, y + 106, d->tmin, 2, TH_ONWALL2, 255);
  }
  int n = d->hourCount < 4 ? d->hourCount : 4;
  int col = 46, x0 = x + w - 16 - n * col;
  for(int i = 0; i < n; i++){
    const FlexWxHour* hh = &d->hours[i];
    int cx = x0 + i * col + col / 2;
    char hl[8]; wxFmtHourShort(hh->t + d->utcOffset, hl, sizeof(hl));
    drawTextC(cx, y + 16, hl, 1, TH_ONWALL2);
    // Dia/noche de CADA hora, como en la app (no el is_day de ahora).
    bool dayH = day;
    if(d->have & WXF_SUN){
      int32_t ds = ((hh->t + d->utcOffset) % 86400 + 86400) % 86400;
      int32_t rs = ((d->sunrise + d->utcOffset) % 86400 + 86400) % 86400;
      int32_t ss = ((d->sunset  + d->utcOffset) % 86400 + 86400) % 86400;
      dayH = (ds >= rs && ds < ss);
    }
    wxDrawIcon(cx, y + 44, 10, flexWeatherVisual(hh->code), dayH, 255);
    if(hh->temp10 != WX_NOVAL_I16) wxDrawTempC(cx, y + 72, hh->temp10 / 10.0f, 2, W, 255);
  }
}

// #############################################################
// ##  WIDGET DE LA PANTALLA DE BLOQUEO
// ##  Mismo estado, cero red mientras se compone el bloqueo.
// #############################################################
static void wxLockCard(int y){
  int x = 28, w = SCR_W - 56, h = 50;
  if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 16, rgb565(40,50,90));
  else        fillRoundRectA(x, y, w, h, 16, TH_ONWALL, 45);
  uint16_t W = TH_ONWALL;
  const FlexWeather* d = flexWeatherData();
  if(!d){
    wxDrawIcon(x + 30, y + h / 2, 12, WXV_CLOUDY, true, 255);
    drawText(x + 58, y + 9, t(S_WEATHER), 2, W);
    drawText(x + 58, y + 30, wt(WT_NODATA), 1, TH_ONWALL2);
    return;
  }
  bool day = d->isDay ? true : false;
  wxDrawIcon(x + 30, y + h / 2, 12, flexWeatherVisual(d->code), day, 255);
  int ex = wxDrawTemp(x + 56, y + 12, d->temp, 3, W, 255);
  char nm[30]; wxCopy(nm, sizeof(nm), d->loc.name);
  while(textW(nm, 2) > w - (ex - x) - 24 && strlen(nm) > 4) nm[strlen(nm) - 1] = 0;
  drawText(ex + 8, y + 8, nm, 2, W);
  char cn[40]; wxCopy(cn, sizeof(cn), flexWeatherCondName(d->code, cfgLang));
  drawText(ex + 8, y + 30, cn, 1, TH_ONWALL2);
}
