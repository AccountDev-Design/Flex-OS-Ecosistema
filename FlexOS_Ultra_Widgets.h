// #############################################################
// ##  FLEX OS ULTRA  ·  WIDGETS DEL ESCRITORIO
// ##  ----------------------------------------------------------
// ##  Los widgets que muestran dato REAL (reloj, fecha, Wi-Fi, memoria,
// ##  clima...), su rejilla, su repintado por rectangulo y wgDataTick().
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
#include "FlexOS_Ultra_Home.h"   // eslabon anterior de la cadena

// #############################################################
// ##  WIDGETS DEL ESCRITORIO
// ##  ------------------------------------------------------
// ##  SOLO entran widgets cuyo dato es REAL en esta placa: reloj
// ##  y fecha (NTP + reloj del sistema), Wi-Fi, memoria,
// ##  almacenamiento (LittleFS de verdad), cronometro y acceso a
// ##  Camara. No hay widget de
// ##  clima ni de bateria: el clima no tiene fuente configurada y
// ##  el porcentaje de bateria todavia es un valor fijo en la
// ##  barra de estado -- inventarlos seria relleno.
// ##
// ##  REGLA DE RENDIMIENTO: el widget NO calcula nada al
// ##  dibujarse. wgDataTick() refresca las cadenas cada 2 s FUERA
// ##  del render y marca que han cambiado; wgDrawCell() solo pinta
// ##  lo que ya esta en el cache. Y cuando cambian, se repinta
// ##  SOLO el rectangulo de ese widget (wgRepaint), no la pantalla.
// #############################################################
static void cronoFmt(char* out, size_t n, uint32_t ms, bool cent);   // definido con el cronometro

static const WgDesc WG_REG[WG_COUNT] = {
  { "",                 "",              1, 1 },   // WG_NONE (nunca se ofrece)
  { "Reloj digital",    "Reloj",         2, 1 },
  { "Reloj anal\xC3\xB3gico", "Reloj",   2, 2 },
  { "Fecha",            "Reloj",         2, 1 },
  { "Wi-Fi",            "Sistema",       1, 1 },
  { "Memoria",          "Sistema",       2, 1 },
  { "Almacenamiento",   "Sistema",       2, 1 },
  { "",                 "",              1, 1 },   // WG_RETIRED_7: id reservado para migrar NVS antiguo
  { "Cron\xC3\xB3metro","Reloj",         2, 1 },
  { "C\xC3\xA1mara",    "Accesos",       1, 1 },
  { "Clima",            "Informaci\xC3\xB3n", 2, 2 },   // WG_CLIMA: datos reales de Open-Meteo
};

// ---- Cache de datos (se rellena en wgDataTick, jamas dentro del dibujo) ----
static char     wgTime[16] = "", wgDate[48] = "", wgWifi[28] = "";
static char     wgMem[24]  = "", wgSto[28] = "", wgCro[16] = "";
static int      wgStoPct   = 0;
static bool     wgNetUp    = false;
static uint32_t wgDataMs   = 0;
static bool     wgDirty    = false;

static uint32_t wgWxGen = 0xFFFFFFFFu;   // generacion del clima ya reflejada en pantalla
static void wgDataTick(){
  uint32_t now = millis();
  // CLIMA. Se comprueba SIEMPRE (es una comparacion de un entero, no cuesta
  // nada) y no cada 2 s: cuando el motor publica una descarga nueva, el
  // widget colocado se repinta ya, y el widget fijo de la fila de arriba
  // obliga a rehacer el escritorio -- que es donde vive.
  uint32_t wxg = flexWeatherGen();
  if(wxg != wgWxGen){
    wgWxGen = wxg;
    wgDirty = true;                       // widgets colocados: solo su rectangulo
    gHomeDirty = true;                    // widget fijo: entra al rehacer homeBuf
  }
  if(wgDataMs && now - wgDataMs < 2000) return;
  wgDataMs = now;
  char t1[16], d1[48], w1[28], m1[24], s1[28], c1[16];
  clkStrBar(t1, sizeof(t1));
  buildShortDate(d1, sizeof(d1));
  // No consultar WiFi.status() desde este tick periodico. En ESP32-P4 esa
  // llamada alcanza esp-hosted/SDIO incluso sin una conexion activa y
  // colisiona con la microSD montada. gNetOnline solo cambia tras una
  // conexion/desconexion real y es la fuente correcta para la interfaz.
  bool up = gNetOnline;
  // WiFi.SSID() devuelve String: se copia AQUI, en el tick de datos, y nunca en
  // el camino de dibujo ni en un bucle de interfaz.
  // SIN String: wifiActiveSSID() devuelve el char[33] que ya mantiene el bloque
  // Wi-Fi. Este tick corre cada 2 s y no puede pedir memoria dinamica.
  if(up){ const char* ss = wifiActiveSSID(); snprintf(w1, sizeof(w1), "%s", ss[0] ? ss : "Conectado"); }
  else   snprintf(w1, sizeof(w1), "Sin conexi\xC3\xB3n");
  snprintf(m1, sizeof(m1), "%u KB libres", (unsigned)(esp_get_free_heap_size() / 1024));
  uint32_t tot = flexFsTotalBytes(), usd = flexFsUsedBytes();
  wgStoPct = (tot > 0) ? (int)((uint64_t)usd * 100 / tot) : 0;
  { char a[16], b[16];
    flexFsFmtSize(usd, a, sizeof(a));
    flexFsFmtSize(tot, b, sizeof(b));
    snprintf(s1, sizeof(s1), "%s de %s", a, b); }
  cronoFmt(c1, sizeof(c1), cronoElapsed(), false);
  if(strcmp(t1, wgTime) || strcmp(d1, wgDate) || strcmp(w1, wgWifi) ||
     strcmp(m1, wgMem) || strcmp(s1, wgSto) || strcmp(c1, wgCro) || up != wgNetUp) wgDirty = true;
  snprintf(wgTime, sizeof(wgTime), "%s", t1);
  snprintf(wgDate, sizeof(wgDate), "%s", d1);
  snprintf(wgWifi, sizeof(wgWifi), "%s", w1);
  snprintf(wgMem,  sizeof(wgMem),  "%s", m1);
  snprintf(wgSto,  sizeof(wgSto),  "%s", s1);
  snprintf(wgCro,  sizeof(wgCro),  "%s", c1);
  wgNetUp = up;
}
// Antena Wi-Fi en miniatura (arcos por puntos, sin fuentes de iconos)
static void wgWifiGlyph(int cx, int cy, int r, uint16_t col, bool on){
  for(int k = 0; k < 3; k++){
    if(!on && k < 2) continue;                       // sin red: solo el punto
    int rr = r - k * (r / 3);
    for(int a = -50; a <= 50; a += 5){
      float rad = (float)a * 0.0174533f;
      pxA(cx + (int)(sinf(rad) * rr), cy - (int)(cosf(rad) * rr) + r, col, 220);
    }
  }
  fillCircle(cx, cy + r, 2, col);
}
// Rectangulo en pixeles de un widget colocado, a partir de la rejilla ACTIVA.
static void wgRect(const HomeWidget* w, int &x, int &y, int &ww, int &hh){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  x  = w->col * cs + 8;         ww = w->w * cs - 16;
  y  = gy0 + w->row * rs - 6;   hh = w->h * rs - 12;
  if(ww < 24) ww = 24;
  if(hh < 24) hh = 24;
}
// Dibuja un widget. 'mini' = dentro de una miniatura de pagina: se simplifica,
// porque a esa escala un texto de 10 px seria ilegible.
static void wgDrawCell(const HomeWidget* wg, int x, int y, int w, int h, bool mini){
  if(!wg || wg->type <= WG_NONE || wg->type >= WG_COUNT) return;
  uint16_t base;
  if(uiGlass && !mini){ drawLiquidGlassPanel(x, y, w, h, mini ? 8 : 20, TH_GLASS2); base = TH_GLASS2; }
  else { fillRoundRectA(x, y, w, h, mini ? 6 : 20, TH_SURF, mini ? 200 : 225); base = TH_SURF; }
  uint16_t fg = onColor(base), fg2 = mix565(fg, base, 96);
  if(mini){
    fillRoundRect(x + 4, y + 4, (w - 8) > 8 ? (w - 8) / 2 : 4, 3, 1, fg2);
    if(wg->type == WG_CLOCK_A) drawCircle(x + w / 2, y + h / 2, (h < w ? h : w) / 3, fg);
    else                       fillRoundRect(x + 6, y + h / 2 - 2, w - 12, 4, 2, fg2);
    return;
  }
  int pad = 12;
  switch(wg->type){
    case WG_CLOCK:
      drawText(x + pad, y + 10, "Reloj", 1, fg2);
      drawText(x + pad, y + h / 2 - 10, wgTime, 4, fg);
      break;
    case WG_CLOCK_A: {
      int cx = x + w / 2, cy = y + h / 2, r = (w < h ? w : h) / 2 - 14;
      if(r < 8) r = 8;
      drawCircle(cx, cy, r, fg); drawCircle(cx, cy, r - 1, fg2);
      for(int i = 0; i < 12; i++){
        float a = i * 0.5235988f;
        fillCircle(cx + (int)(sinf(a) * (r - 6)), cy - (int)(cosf(a) * (r - 6)), 1, fg2);
      }
      float ah = ((rtcH % 12) + rtcMin / 60.0f) * 0.5235988f, am = rtcMin * 0.1047198f;
      strokeSegAA((float)cx, (float)cy, cx + sinf(ah) * (r * 0.52f), cy - cosf(ah) * (r * 0.52f), 2.4f, fg);
      strokeSegAA((float)cx, (float)cy, cx + sinf(am) * (r * 0.78f), cy - cosf(am) * (r * 0.78f), 1.8f, fg);
      fillCircle(cx, cy, 3, fg);
      break;
    }
    case WG_DATE:
      drawText(x + pad, y + 10, "Fecha", 1, fg2);
      drawTextClip(x + pad, y + h / 2 - 8, wgDate, 2, fg, x + w - pad);
      break;
    case WG_WIFI:
      wgWifiGlyph(x + w / 2, y + h / 2 - 14, 11, wgNetUp ? fg : fg2, wgNetUp);
      drawTextC(x + w / 2, y + h - 22, wgNetUp ? "Wi-Fi" : "Sin red", 1, fg2);
      break;
    case WG_MEM:
      drawText(x + pad, y + 10, "Memoria", 1, fg2);
      drawTextClip(x + pad, y + h / 2 - 4, wgMem, 2, fg, x + w - pad);
      break;
    case WG_STORAGE: {
      drawText(x + pad, y + 10, "Almacenamiento", 1, fg2);
      int bw = w - 2 * pad, bx = x + pad, by = y + h - 24;
      fillRoundRect(bx, by, bw, 8, 4, TH_TRACK);
      int fw = bw * wgStoPct / 100;
      if(fw < 2) fw = 2;
      fillRoundRect(bx, by, fw, 8, 4, wallAccent2());
      drawTextClip(bx, y + h / 2 - 12, wgSto, 1, fg, x + w - pad);
      break;
    }
    case WG_CLIMA: {
      // Widget colocable: MISMO WeatherState que la app, el widget fijo y el
      // bloqueo. Sin red y sin calculos aqui (regla de wgDataTick).
      drawText(x + pad, y + 10, "Clima", 1, fg2);
      const FlexWeather* d = flexWeatherData();
      if(!d){ drawTextC(x + w / 2, y + h / 2 - 8, wt(WT_NODATA), 2, fg2); break; }
      wxDrawIcon(x + w - pad - 22, y + 40, 16, flexWeatherVisual(d->code), d->isDay != 0, 255);
      wxDrawTemp(x + pad, y + 34, d->temp, 5, fg, 255);
      drawTextClip(x + pad, y + h - 40, d->loc.name, 1, fg2, x + w - pad);
      drawTextClip(x + pad, y + h - 24, flexWeatherCondName(d->code, cfgLang), 1, fg2, x + w - pad);
      break;
    }
    case WG_CRONO:
      drawText(x + pad, y + 10, gCronoSt == CRONO_RUN ? "Cron\xC3\xB3metro en marcha" : "Cron\xC3\xB3metro", 1, fg2);
      drawText(x + pad, y + h / 2 - 6, wgCro, 3, fg);
      break;
    case WG_CAM:
      fillRoundRect(x + w / 2 - 15, y + h / 2 - 20, 30, 22, 6, fg);
      fillCircle(x + w / 2, y + h / 2 - 9, 7, base);
      drawTextC(x + w / 2, y + h - 22, "C\xC3\xA1mara", 1, fg2);
      break;
    default: break;
  }
}
// ---- Ocupacion de celdas ---------------------------------------------------
// Mascara de 20 bits por pagina: 1 = celda ocupada por un icono o por un widget.
static uint32_t homeCellMask(int page, int skipWg){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  int n = homeSlotCount();
  uint32_t m = 0;
  if(page < 0 || page >= HOME_PAGES_MAX) return 0xFFFFFFFFu;
  for(int i = 0; i < n; i++) if(homeOrder[homeIdx(page, i)] != HOME_EMPTY) m |= (1u << i);
  for(int k = 0; k < gHomeWgN[page] && k < HOME_WG_MAX; k++){
    if(k == skipWg) continue;
    const HomeWidget* w = &gHomeWg[page][k];
    if(w->type == WG_NONE) continue;
    for(int r = w->row; r < w->row + w->h; r++)
      for(int c = w->col; c < w->col + w->w; c++){
        if(r < 0 || r >= rows || c < 0 || c >= cols) continue;
        int i = r * cols + c;
        if(i < n) m |= (1u << i);
      }
  }
  return m;
}
static bool homeWgFits(int page, int c, int r, int w, int h, int skipWg){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  if(c < 0 || r < 0 || c + w > cols || r + h > rows) return false;
  uint32_t m = homeCellMask(page, skipWg);
  for(int rr = r; rr < r + h; rr++)
    for(int cc = c; cc < c + w; cc++) if(m & (1u << (rr * cols + cc))) return false;
  return true;
}
static bool homeWgSpot(int page, int w, int h, int &oc, int &orow){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  for(int r = 0; r + h <= rows; r++)
    for(int c = 0; c + w <= cols; c++)
      if(homeWgFits(page, c, r, w, h, -1)){ oc = c; orow = r; return true; }
  return false;
}
// Indice del widget de esa pagina bajo el punto, o -1.
static int homeWgAt(int page, int px, int py){
  if(page < 0 || page >= HOME_PAGES_MAX) return -1;
  for(int k = 0; k < gHomeWgN[page] && k < HOME_WG_MAX; k++){
    if(gHomeWg[page][k].type == WG_NONE) continue;
    int x, y, w, h; wgRect(&gHomeWg[page][k], x, y, w, h);
    if(px >= x && px < x + w && py >= y && py < y + h) return k;
  }
  return -1;
}
// Coloca un widget. 0 = ok · 1 = la pagina ya tiene el maximo · 2 = sin hueco.
static int homeWgAdd(int page, int type){
  if(page < 0 || page >= gHomePageN) return 2;
  if(type <= WG_NONE || type >= WG_COUNT || type == WG_RETIRED_7) return 2;
  if(gHomeWgN[page] >= HOME_WG_MAX) return 1;
  int w = WG_REG[type].w, h = WG_REG[type].h, c, r;
  if(!homeWgSpot(page, w, h, c, r)) return 2;
  HomeWidget* d = &gHomeWg[page][gHomeWgN[page]];
  d->type = (uint8_t)type; d->col = (uint8_t)c; d->row = (uint8_t)r;
  d->w = (uint8_t)w; d->h = (uint8_t)h;
  gHomeWgN[page]++;
  return 0;
}
static void homeWgRemove(int page, int idx){
  if(page < 0 || page >= HOME_PAGES_MAX) return;
  if(idx < 0 || idx >= gHomeWgN[page]) return;
  for(int k = idx; k < gHomeWgN[page] - 1; k++) gHomeWg[page][k] = gHomeWg[page][k + 1];
  gHomeWgN[page]--;
  gHomeWg[page][gHomeWgN[page]].type = WG_NONE;
}
// Deja los widgets en un estado COHERENTE con la rejilla y el numero de paginas
// actuales: los que ya no caben se retiran (nunca se dibujan a medias ni tapan
// celdas que no existen).
static void homeWgNormalize(){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  for(int p = 0; p < HOME_PAGES_MAX; p++){
    if(p >= gHomePageN){ gHomeWgN[p] = 0; }
    if(gHomeWgN[p] > HOME_WG_MAX) gHomeWgN[p] = HOME_WG_MAX;
    uint8_t n = 0;
    for(int k = 0; k < gHomeWgN[p]; k++){
      HomeWidget w = gHomeWg[p][k];
      if(w.type <= WG_NONE || w.type >= WG_COUNT || w.type == WG_RETIRED_7) continue;
      if(w.w < 1 || w.h < 1 || w.w > HOME_COLS_MAX || w.h > HOME_ROWS_MAX) continue;
      if(w.col + w.w > cols || w.row + w.h > rows) continue;
      gHomeWg[p][n++] = w;
    }
    gHomeWgN[p] = n;
    for(int k = n; k < HOME_WG_MAX; k++) gHomeWg[p][k].type = WG_NONE;
  }
}
// SERIALIZACION. Tamano FIJO y validacion completa al cargar: un blob que no
// cuadre se descarta entero (escritorio sin widgets, que es un estado valido)
// en vez de dejar medio widget colocado en una celda que no existe.
static void homeWgSerialize(uint8_t* b){
  memset(b, 0, HOME_WG_BLOB);
  b[0] = 'W'; b[1] = 1;
  int o = 2;
  for(int p = 0; p < HOME_PAGES_MAX; p++){
    b[o++] = gHomeWgN[p];
    for(int k = 0; k < HOME_WG_MAX; k++){
      b[o++] = gHomeWg[p][k].type; b[o++] = gHomeWg[p][k].col; b[o++] = gHomeWg[p][k].row;
      b[o++] = gHomeWg[p][k].w;    b[o++] = gHomeWg[p][k].h;
    }
  }
}
static bool homeWgDeserialize(const uint8_t* b){
  if(b[0] != 'W' || b[1] != 1) return false;
  HomeWidget tmp[HOME_PAGES_MAX][HOME_WG_MAX];
  uint8_t cnt[HOME_PAGES_MAX];
  int o = 2;
  for(int p = 0; p < HOME_PAGES_MAX; p++){
    cnt[p] = b[o++];
    if(cnt[p] > HOME_WG_MAX) return false;
    for(int k = 0; k < HOME_WG_MAX; k++){
      uint8_t ty = b[o++], c = b[o++], r = b[o++], w = b[o++], h = b[o++];
      if(ty >= WG_COUNT) return false;
      if(w > HOME_COLS_MAX || h > HOME_ROWS_MAX) return false;
      if(ty != WG_NONE && (c + w > HOME_COLS_MAX || r + h > HOME_ROWS_MAX)) return false;
      tmp[p][k].type = ty; tmp[p][k].col = c; tmp[p][k].row = r; tmp[p][k].w = w; tmp[p][k].h = h;
    }
  }
  memcpy(gHomeWg, tmp, sizeof(gHomeWg));
  memcpy(gHomeWgN, cnt, sizeof(gHomeWgN));
  return true;
}

// Dibuja los widgets de una pagina, desplazados igual que sus iconos.
static void homeDrawWidgets(int page, int xoff){
  if(page < 0 || page >= gHomePageN) return;
  for(int k = 0; k < gHomeWgN[page] && k < HOME_WG_MAX; k++){
    if(gHomeWg[page][k].type == WG_NONE) continue;
    int x, y, w, h; wgRect(&gHomeWg[page][k], x, y, w, h);
    if(x + xoff + w < 0 || x + xoff > SCR_W) continue;
    wgDrawCell(&gHomeWg[page][k], x + xoff, y, w, h, false);
  }
}
// REPINTADO PARCIAL. Solo las FILAS que ocupan los widgets de la pagina
// visible, ni una mas -- no la pantalla entera.
//
// Se repintan filas COMPLETAS y no el rectangulo exacto de cada widget a
// proposito: drawWallpaperRowsId() fuerza su propio viewport horizontal (lo
// necesita para que un recorte olvidado por otra pantalla no deje una franja
// negra permanente en el escritorio), asi que un recorte en X puesto desde
// fuera no lo acota. Regenerar la banda entera y volver a poner encima widgets
// e iconos de esas filas es correcto por construccion y sigue costando una
// fraccion de la pantalla.
static void wgRepaint(){
  if(!homeBuf) return;
  int page = gHomePage;
  if(page < 0 || page >= gHomePageN || gHomeWgN[page] == 0) return;
  int y0 = SCR_H, y1 = -1;
  for(int k = 0; k < gHomeWgN[page] && k < HOME_WG_MAX; k++){
    if(gHomeWg[page][k].type == WG_NONE) continue;
    int x, y, w, h; wgRect(&gHomeWg[page][k], x, y, w, h);
    if(y < y0) y0 = y;
    if(y + h - 1 > y1) y1 = y + h - 1;
  }
  if(y1 < y0) return;
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  uint16_t* old = gBuf;
  int c0 = gClipX0, c1 = gClipX1, r0 = gClipY0, r1 = gClipY1;
  drawWallpaperRowsId(homeBuf, gWallHome, true, y0, y1);
  setBuf(homeBuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y1;
  homeDrawWidgets(page, 0);
  homeDrawGridWork(page, 0, false);
  gClipX0 = c0; gClipX1 = c1; gClipY0 = r0; gClipY1 = r1;
  setBuf(old);
  fbCopyBand(homeBuf, y0, y1);
  flxFlush(y0, y1);
}

// ---- Destello de reflejo al tocar un icono (estilo "Vidrio") ----
// Circulo blanco que crece y se desvanece (~0.5 s) desde el punto exacto
// donde se toco. Se activa en homeTick() (T.pressed sobre un icono) y se
// anima aqui; se llama desde uiTick() solo mientras gState==ST_HOME, asi
// que si se abre otra pantalla (enterApp) el destello deja de dibujarse
// de inmediato aunque el temporizador no haya terminado.
static bool     gRippleActive = false;
static int      gRippleX = 0, gRippleY = 0;
static uint32_t gRippleStart = 0;
static const uint32_t RIPPLE_DUR_MS = 500;
static const int      RIPPLE_MAX_R  = 70;
static void animateIconRipple(){
  if(gIconStyle != 1 || !bbuf || !homeBuf || gSafeMode){ gRippleActive = false; return; }
  setBuf(bbuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;   // recorte completo
  // REPINTADO PARCIAL. El destello es un circulo de radio <= RIPPLE_MAX_R
  // centrado en el punto tocado, que NO se mueve durante la animacion. Antes se
  // recopiaba/volcaba la banda entera 64..726; ahora solo la franja vertical que
  // el circulo puede alcanzar (centro +- radio maximo, acotada a la pantalla).
  // El resto de fb ya es correcto. Salida byte-identica, mucho menos memcpy por
  // frame durante el ~medio segundo del efecto.
  int y0 = gRippleY - RIPPLE_MAX_R; if(y0 < 64)  y0 = 64;
  int y1 = gRippleY + RIPPLE_MAX_R; if(y1 > 726) y1 = 726;
  for(int j = y0; j <= y1; j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);
  uint32_t e = millis() - gRippleStart;
  if(e < RIPPLE_DUR_MS){
    float   p = (float)e / RIPPLE_DUR_MS;                // 0..1
    int     r = (int)(RIPPLE_MAX_R * p);                  // crece
    uint8_t a = (uint8_t)(160 * (1.0f - p));              // se desvanece
    if(r > 0 && a > 0) fillCircleA(gRippleX, gRippleY, r, TH_ONWALL, a);   // ripple sobre el wallpaper
  } else {
    gRippleActive = false;                                // termino: este frame sale limpio (sin circulo)
  }
  present(y0, y1);
}
