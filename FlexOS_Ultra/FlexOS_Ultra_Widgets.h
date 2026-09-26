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
// ##  almacenamiento (LittleFS de verdad), cronometro, acceso a
// ##  Camara, clima (Open-Meteo) y calendario. No hay widget de
// ##  bateria: el porcentaje todavia es un valor fijo en la barra
// ##  de estado -- inventarlo seria relleno.
// ##
// ##  CADA WIDGET ES DE UNA PAGINA. Vive en gHomeWg[pagina] con su
// ##  celda y su tamano en la rejilla de esa pagina (fila 0 =
// ##  cabecera), viaja con ella al deslizar y se puede redimensionar
// ##  y llevar a otra pagina en Modo Edicion. Toda colocacion pasa
// ##  por homeWgPlaceOk(): limites de su tipo, alto real suficiente
// ##  y sin pisar iconos ni widgets.
// ##
// ##  REGLA DE RENDIMIENTO: el widget NO calcula nada al
// ##  dibujarse. wgDataTick() refresca las cadenas cada 2 s FUERA
// ##  del render y marca que han cambiado; wgDrawCell() solo pinta
// ##  lo que ya esta en el cache. Y cuando cambian, se repinta
// ##  SOLO el rectangulo de ese widget (wgRepaint), no la pantalla.
// #############################################################
static void cronoFmt(char* out, size_t n, uint32_t ms, bool cent);   // definido con el cronometro

// Limites de tamano ELEGIDOS POR CONTENIDO, no al azar: un texto de una linea
// (fecha, memoria) no gana nada con dos filas; el reloj analogico es un circulo
// y no admite tiras largas; el calendario necesita sus seis semanas en vertical
// (110 px) y el clima compacto sus cinco lineas (116 px). Ningun widget pasa de
// 4 columnas: es el ancho de la rejilla mas estrecha.
static const WgDesc WG_REG[WG_COUNT] = {
  //  nombre              categoria                w  h   minW maxW minH maxH minPxH
  { "",                 "",                        1, 1,   1, 1, 1, 1,   0 },   // WG_NONE (nunca se ofrece)
  { "Reloj digital",    "Reloj",                   2, 1,   2, 4, 1, 2,   0 },
  { "Reloj anal\xC3\xB3gico", "Reloj",             2, 2,   1, 2, 1, 2,  70 },
  { "Fecha",            "Reloj",                   2, 1,   2, 4, 1, 1,   0 },
  { "Wi-Fi",            "Sistema",                 1, 1,   1, 2, 1, 1,   0 },
  { "Memoria",          "Sistema",                 2, 1,   2, 4, 1, 1,   0 },
  { "Almacenamiento",   "Sistema",                 2, 1,   2, 4, 1, 1,   0 },
  { "",                 "",                        1, 1,   1, 1, 1, 1,   0 },   // WG_RETIRED_7: id reservado para migrar NVS antiguo
  { "Cron\xC3\xB3metro","Reloj",                   2, 1,   2, 4, 1, 1,   0 },
  { "C\xC3\xA1mara",    "Accesos",                 1, 1,   1, 2, 1, 1,   0 },
  { "Clima",            "Informaci\xC3\xB3n",      2, 2,   2, 4, 1, 2, 116 },   // WG_CLIMA: datos reales de Open-Meteo
  { "Calendario",       "Informaci\xC3\xB3n",      2, 1,   2, 4, 1, 3, 110 },   // WG_CALEND: rtcY/rtcMo/rtcD reales
};

// ---- Cache de datos (se rellena en wgDataTick, jamas dentro del dibujo) ----
static char     wgTime[16] = "", wgDate[48] = "", wgWifi[28] = "";
static char     wgMem[24]  = "", wgSto[28] = "", wgCro[16] = "";
static int      wgStoPct   = 0;
static bool     wgNetUp    = false;
static uint32_t wgDataMs   = 0;
static bool     wgDirty    = false;

static uint32_t wgWxGen = 0xFFFFFFFFu;   // generacion del clima ya reflejada en pantalla

// ---- ALMACENAMIENTO: el unico dato caro del widget ----------------------
// flexFsUsedBytes() recorre la particion LittleFS entera (ver FlexOS_FS.cpp).
// Antes se pedia AQUI, cada 2 s, en CUALQUIER pantalla -- dentro de una app,
// en mitad de una transicion o con el dedo arrastrando -- haya o no un widget
// de almacenamiento en el escritorio: un tiron periodico del hilo de la
// interfaz que crecia con la biblioteca de medios. Ahora solo se mide si hay
// un widget de almacenamiento colocado, con el escritorio a la vista y quieto,
// y como mucho cada WG_STO_MS (la capa de archivos ademas guarda la cifra
// hasta que algo cambia el disco).
#define WG_STO_MS 10000u
static uint32_t wgStoMs = 0;
static bool appTrVisible();              // FlexOS_Ultra_AppFramework.h (mas abajo)
static bool wgStoragePlaced(){
  for(int p = 0; p < gHomePageN && p < HOME_PAGES_MAX; p++)
    for(int k = 0; k < gHomeWgN[p] && k < HOME_WG_MAX; k++)
      if(gHomeWg[p][k].type == WG_STORAGE) return true;
  return false;
}
static void wgStorageTick(uint32_t now, char* s1, size_t n1){
  bool due = wgStoragePlaced() && gState == ST_HOME && !editMode && !T.down &&
             !hpDragging && !hpSettling && !appTrVisible() &&
             (!wgStoMs || now - wgStoMs >= WG_STO_MS);
  if(!due){ snprintf(s1, n1, "%s", wgSto); return; }       // se conserva lo ultimo medido
  wgStoMs = now;
  uint32_t tot = flexFsTotalBytes(), usd = flexFsUsedBytes();
  wgStoPct = (tot > 0) ? (int)((uint64_t)usd * 100 / tot) : 0;
  char a[16], b[16];
  flexFsFmtSize(usd, a, sizeof(a));
  flexFsFmtSize(tot, b, sizeof(b));
  snprintf(s1, n1, "%s de %s", a, b);
}

static void wgDataTick(){
  uint32_t now = millis();
  // CLIMA. Se comprueba SIEMPRE (es una comparacion de un entero, no cuesta
  // nada) y no cada 2 s: cuando el motor publica una descarga nueva, el
  // widget colocado se repinta ya. Clima ya no es un widget fijo: es uno mas
  // de su pagina, asi que no hace falta rehacer el escritorio entero.
  uint32_t wxg = flexWeatherGen();
  if(wxg != wgWxGen){
    wgWxGen = wxg;
    wgDirty = true;                       // widgets colocados (Clima incluido): solo sus filas
  }
  if(wgDataMs && now - wgDataMs < 2000) return;
  wgDataMs = now;
  char t1[16], d1[48], w1[28], m1[24], s1[28], c1[16];
  clkStrBar(t1, sizeof(t1));
  buildShortDate(d1, sizeof(d1));
  // No consultar WiFi.status() desde este tick periodico. En ESP32-P4 esa
  // llamada alcanza esp-hosted/SDIO incluso sin una conexion activa.
  // gNetOnline solo cambia tras una conexion/desconexion real.
  bool up = gNetOnline;
  // WiFi.SSID() devuelve String: se copia AQUI, en el tick de datos, y nunca en
  // el camino de dibujo ni en un bucle de interfaz.
  // SIN String: wifiActiveSSID() devuelve el char[33] que ya mantiene el bloque
  // Wi-Fi. Este tick corre cada 2 s y no puede pedir memoria dinamica.
  if(up){ const char* ss = wifiActiveSSID(); snprintf(w1, sizeof(w1), "%s", ss[0] ? ss : "Conectado"); }
  else   snprintf(w1, sizeof(w1), "Sin conexi\xC3\xB3n");
  snprintf(m1, sizeof(m1), "%u KB libres", (unsigned)(esp_get_free_heap_size() / 1024));
  wgStorageTick(now, s1, sizeof(s1));
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
// Filas: la 0 es la CABECERA (y=72, 120 px, donde antes vivian los widgets
// fijos); de la 1 en adelante, las filas de iconos con la geometria de siempre
// (una fila de iconos da exactamente el rectangulo de antes). Un widget puede
// ocupar la cabecera y seguir hacia abajo: el rectangulo es continuo.
static void wgRect(const HomeWidget* w, int &x, int &y, int &ww, int &hh){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  x  = w->col * cs + 8;         ww = w->w * cs - 16;
  int rTop = w->row, rBot = w->row + (w->h > 0 ? w->h : 1) - 1;
  y = (rTop == 0) ? HOME_HDR_Y : gy0 + (rTop - 1) * rs - 6;
  int yEnd = (rBot == 0) ? HOME_HDR_Y + HOME_HDR_H : gy0 + rBot * rs - 18;
  hh = yEnd - y;
  if(ww < 24) ww = 24;
  if(hh < 24) hh = 24;
}
// Dibuja un widget. 'mini' = dentro de una miniatura de pagina: se simplifica,
// porque a esa escala un texto de 10 px seria ilegible.
static void wgDrawCell(const HomeWidget* wg, int x, int y, int w, int h, bool mini){
  if(!wg || wg->type <= WG_NONE || wg->type >= WG_COUNT) return;
  // CLIMA DE UNA FILA: es el widget que antes estaba fijo arriba, con su
  // composicion compacta (o la ancha, a partir de 4 columnas) y su propio
  // material. Con dos filas sigue la composicion de tarjeta de siempre.
  if(!mini && wg->type == WG_CLIMA && h < 180){ wxHomeWidget(x, y, w, h, w >= 400); return; }
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
    case WG_CALEND:
      // El calendario que antes estaba fijo arriba, ahora de una pagina.
      calWidgetBody(x, y, w, h, fg, mix565(fg, base, 104));
      break;
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
// Mascara de 20 bits por pagina: 1 = celda de ICONOS ocupada por un icono o por
// un widget. La fila de cabecera no tiene celdas de icono: su ocupacion la da
// homeHdrMask().
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
        if(r < 1 || r > rows || c < 0 || c >= cols) continue;   // fila 0 = cabecera
        int i = (r - 1) * cols + c;
        if(i < n) m |= (1u << i);
      }
  }
  return m;
}
// Columnas de la CABECERA ocupadas por widgets (bit c = columna c).
static uint32_t homeHdrMask(int page, int skipWg){
  if(page < 0 || page >= HOME_PAGES_MAX) return 0xFFFFFFFFu;
  uint32_t m = 0;
  for(int k = 0; k < gHomeWgN[page] && k < HOME_WG_MAX; k++){
    if(k == skipWg) continue;
    const HomeWidget* w = &gHomeWg[page][k];
    if(w->type == WG_NONE || w->row != 0) continue;
    for(int c = w->col; c < w->col + w->w && c < 32; c++) m |= (1u << c);
  }
  return m;
}
// ¿Cabe (c,r,w,h) en la pagina sin salirse de la rejilla ni pisar iconos u
// otros widgets? Filas en coordenadas de widgets (0 = cabecera).
static bool homeWgFits(int page, int c, int r, int w, int h, int skipWg){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  if(w < 1 || h < 1 || c < 0 || r < 0 || c + w > cols || r + h > rows + 1) return false;
  uint32_t m = homeCellMask(page, skipWg);
  uint32_t hm = (r == 0) ? homeHdrMask(page, skipWg) : 0u;
  for(int rr = r; rr < r + h; rr++)
    for(int cc = c; cc < c + w; cc++){
      if(rr == 0){ if(hm & (1u << cc)) return false; }
      else if(m & (1u << ((rr - 1) * cols + cc))) return false;
    }
  return true;
}
// Lo mismo, contando SOLO widgets (los iconos se recolocan despues: el widget
// manda sobre la celda). Lo usa la normalizacion.
static bool homeWgFreeOfWidgets(int page, int c, int r, int w, int h, int upTo){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  if(w < 1 || h < 1 || c < 0 || r < 0 || c + w > cols || r + h > rows + 1) return false;
  for(int k = 0; k < upTo && k < HOME_WG_MAX; k++){
    const HomeWidget* o = &gHomeWg[page][k];
    if(o->type == WG_NONE) continue;
    if(c < o->col + o->w && o->col < c + w && r < o->row + o->h && o->row < r + h) return false;
  }
  return true;
}
// VALIDACION ESPACIAL COMPLETA de un widget de tipo `type` en (c,r,w,h): tamano
// dentro de los limites de su tipo, alto real suficiente para su contenido y
// hueco libre. Es la UNICA puerta por la que pasa colocar, mover, redimensionar
// o llevar un widget a otra pagina: ningun camino puede saltarsela.
static bool homeWgSizeOk(int type, int r, int w, int h){
  if(type <= WG_NONE || type >= WG_COUNT || type == WG_RETIRED_7) return false;
  const WgDesc* d = &WG_REG[type];
  if(w < d->minW || w > d->maxW || h < d->minH || h > d->maxH) return false;
  if(d->minPxH){
    HomeWidget t; t.type = (uint8_t)type; t.col = 0; t.row = (uint8_t)r; t.w = (uint8_t)w; t.h = (uint8_t)h;
    int x, y, ww, hh; wgRect(&t, x, y, ww, hh);
    if(hh < d->minPxH) return false;
  }
  return true;
}
static bool homeWgPlaceOk(int page, int type, int c, int r, int w, int h, int skipWg){
  return homeWgSizeOk(type, r, w, h) && homeWgFits(page, c, r, w, h, skipWg);
}
// Primer hueco para (w,h), recorriendo por filas de arriba abajo y de izquierda
// a derecha: DETERMINISTA, la misma pagina da siempre el mismo sitio.
static bool homeWgSpotFor(int page, int type, int w, int h, int skipWg, int &oc, int &orow){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  for(int r = 0; r + h <= rows + 1; r++)
    for(int c = 0; c + w <= cols; c++)
      if(homeWgPlaceOk(page, type, c, r, w, h, skipWg)){ oc = c; orow = r; return true; }
  return false;
}
// Limites de tamano de un tipo, en celdas (1x1 para un tipo desconocido).
static void wgSizeLimits(int type, int &minW, int &maxW, int &minH, int &maxH){
  if(type <= WG_NONE || type >= WG_COUNT){ minW = maxW = minH = maxH = 1; return; }
  const WgDesc* d = &WG_REG[type];
  minW = d->minW; maxW = d->maxW; minH = d->minH; maxH = d->maxH;
}
// ¿Admite este tipo mas de un tamano? (decide si se ofrece el asa de redimensionar)
static bool wgCanResize(int type){
  if(type <= WG_NONE || type >= WG_COUNT) return false;
  const WgDesc* d = &WG_REG[type];
  return d->minW != d->maxW || d->minH != d->maxH;
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
  if(!homeWgSpotFor(page, type, w, h, -1, c, r)) return 2;
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
// Cambia el tamano de un widget. Solo si el tamano nuevo es valido para su tipo
// y cabe en su sitio sin pisar nada; si no, no cambia nada y devuelve false.
static bool homeWgResize(int page, int idx, int w, int h){
  if(page < 0 || page >= HOME_PAGES_MAX || idx < 0 || idx >= gHomeWgN[page]) return false;
  HomeWidget* o = &gHomeWg[page][idx];
  if(!homeWgPlaceOk(page, o->type, o->col, o->row, w, h, idx)) return false;
  o->w = (uint8_t)w; o->h = (uint8_t)h;
  return true;
}
// LLEVAR UN WIDGET A OTRA PAGINA. Regla DETERMINISTA, en este orden:
//   1. mismo sitio y mismo tamano, si ahi cabe;
//   2. primer hueco (por filas, de arriba abajo) con su mismo tamano;
//   3. primer hueco con el tamano MINIMO de su tipo;
//   4. si nada de eso cabe, o la pagina ya tiene el maximo de widgets, NO se
//      mueve: se queda donde estaba y la funcion devuelve -1.
// Nunca pisa ni desplaza lo que ya hay en la pagina destino. Conserva el tipo
// (y con el su configuracion: los widgets leen su dato del sistema, no
// guardan estado propio). Devuelve el indice nuevo en la pagina destino.
static int homeWgToPage(int src, int idx, int dst){
  if(src < 0 || src >= gHomePageN || dst < 0 || dst >= gHomePageN || src == dst) return -1;
  if(idx < 0 || idx >= gHomeWgN[src] || gHomeWgN[dst] >= HOME_WG_MAX) return -1;
  HomeWidget w = gHomeWg[src][idx];
  int c = w.col, r = w.row, ww = w.w, hh = w.h;
  if(!homeWgPlaceOk(dst, w.type, c, r, ww, hh, -1)){
    if(!homeWgSpotFor(dst, w.type, ww, hh, -1, c, r)){
      ww = WG_REG[w.type].minW; hh = WG_REG[w.type].minH;
      // el tamano minimo de celdas puede no llegar al alto minimo en pixeles
      // fuera de la cabecera: se prueba con una fila mas antes de rendirse.
      if(!homeWgSpotFor(dst, w.type, ww, hh, -1, c, r)){
        hh++;
        if(hh > WG_REG[w.type].maxH || !homeWgSpotFor(dst, w.type, ww, hh, -1, c, r)) return -1;
      }
    }
  }
  HomeWidget* d = &gHomeWg[dst][gHomeWgN[dst]];
  d->type = w.type; d->col = (uint8_t)c; d->row = (uint8_t)r; d->w = (uint8_t)ww; d->h = (uint8_t)hh;
  gHomeWgN[dst]++;
  homeWgRemove(src, idx);
  return gHomeWgN[dst] - 1;
}
// Deja los widgets en un estado COHERENTE con la rejilla y el numero de paginas
// actuales. Un widget que ya no cabe donde estaba (cambio de rejilla, datos
// corruptos, solape con otro widget) se RECOLOCA en su misma pagina con la
// misma regla que al moverlo (mismo tamano y, si no, el minimo); solo si de
// verdad no hay sitio se retira. Nunca se dibuja a medias ni tapa celdas que
// no existen, y dos widgets nunca comparten celda.
static void homeWgNormalize(){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  for(int p = 0; p < HOME_PAGES_MAX; p++){
    if(p >= gHomePageN){ gHomeWgN[p] = 0; }
    if(gHomeWgN[p] > HOME_WG_MAX) gHomeWgN[p] = HOME_WG_MAX;
    uint8_t n = 0, total = gHomeWgN[p];
    for(int k = 0; k < total; k++){
      HomeWidget w = gHomeWg[p][k];
      if(w.type <= WG_NONE || w.type >= WG_COUNT || w.type == WG_RETIRED_7) continue;
      const WgDesc* d = &WG_REG[w.type];
      if(w.w < d->minW) w.w = d->minW;
      if(w.w > d->maxW) w.w = d->maxW;
      if(w.h < d->minH) w.h = d->minH;
      if(w.h > d->maxH) w.h = d->maxH;
      bool ok = homeWgSizeOk(w.type, w.row, w.w, w.h) && homeWgFreeOfWidgets(p, w.col, w.row, w.w, w.h, n);
      if(!ok){
        // recolocar: primero su tamano, luego el minimo (con una fila mas si el
        // minimo no llega al alto que necesita su contenido)
        uint8_t tw[3] = { w.w, d->minW, d->minW }, th[3] = { w.h, d->minH, (uint8_t)(d->minH + 1) };
        for(int t = 0; t < 3 && !ok; t++){
          if(th[t] > d->maxH) continue;
          for(int r = 0; r + th[t] <= rows + 1 && !ok; r++)
            for(int c = 0; c + tw[t] <= cols && !ok; c++)
              if(homeWgSizeOk(w.type, r, tw[t], th[t]) && homeWgFreeOfWidgets(p, c, r, tw[t], th[t], n)){
                w.col = (uint8_t)c; w.row = (uint8_t)r; w.w = tw[t]; w.h = th[t]; ok = true;
              }
        }
      }
      if(!ok) continue;
      gHomeWg[p][n++] = w;
    }
    gHomeWgN[p] = n;
    for(int k = n; k < HOME_WG_MAX; k++) gHomeWg[p][k].type = WG_NONE;
  }
}
// Los dos widgets que antes eran FIJOS en la franja de arriba de TODAS las
// paginas, ahora en la cabecera de la pagina principal: el aspecto de fabrica
// (y el de una placa que actualiza) es el de siempre, pero ya son de una pagina.
static void homeWgFactory(){
  int p = (gHomeMain < gHomePageN) ? gHomeMain : 0;
  static const uint8_t T[2] = { WG_CLIMA, WG_CALEND };
  for(int i = 0; i < 2; i++){
    if(gHomeWgN[p] >= HOME_WG_MAX) break;
    int c = i * 2;
    if(!homeWgPlaceOk(p, T[i], c, 0, 2, 1, -1)) continue;   // hueco ocupado: no se fuerza
    HomeWidget* d = &gHomeWg[p][gHomeWgN[p]++];
    d->type = T[i]; d->col = (uint8_t)c; d->row = 0; d->w = 2; d->h = 1;
  }
}
// SERIALIZACION. Tamano FIJO y validacion completa al cargar: un blob que no
// cuadre se descarta entero (escritorio sin widgets, que es un estado valido)
// en vez de dejar medio widget colocado en una celda que no existe.
static void homeWgSerialize(uint8_t* b){
  memset(b, 0, HOME_WG_BLOB);
  b[0] = 'W'; b[1] = 2;
  int o = 2;
  for(int p = 0; p < HOME_PAGES_MAX; p++){
    b[o++] = gHomeWgN[p];
    for(int k = 0; k < HOME_WG_MAX; k++){
      b[o++] = gHomeWg[p][k].type; b[o++] = gHomeWg[p][k].col; b[o++] = gHomeWg[p][k].row;
      b[o++] = gHomeWg[p][k].w;    b[o++] = gHomeWg[p][k].h;
    }
  }
}
// Lector comun de los dos formatos. perPage = widgets por pagina del formato;
// rowAdd = cuanto baja cada fila (1 en v1, que no tenia cabecera).
static bool homeWgParse(const uint8_t* b, uint8_t ver, int perPage, int rowAdd){
  if(b[0] != 'W' || b[1] != ver) return false;
  HomeWidget tmp[HOME_PAGES_MAX][HOME_WG_MAX];
  uint8_t cnt[HOME_PAGES_MAX];
  memset(tmp, 0, sizeof(tmp));
  int o = 2;
  for(int p = 0; p < HOME_PAGES_MAX; p++){
    cnt[p] = b[o++];
    if(cnt[p] > perPage) return false;
    for(int k = 0; k < perPage; k++){
      uint8_t ty = b[o++], c = b[o++], r = b[o++], w = b[o++], h = b[o++];
      if(ty >= WG_COUNT) return false;
      if(w > HOME_COLS_MAX || h > HOME_ROWS_MAX + 1) return false;
      if(ty != WG_NONE){
        r = (uint8_t)(r + rowAdd);
        if(c + w > HOME_COLS_MAX || r + h > HOME_ROWS_MAX + 1) return false;
      }
      tmp[p][k].type = ty; tmp[p][k].col = c; tmp[p][k].row = r; tmp[p][k].w = w; tmp[p][k].h = h;
    }
  }
  memcpy(gHomeWg, tmp, sizeof(gHomeWg));
  memcpy(gHomeWgN, cnt, sizeof(gHomeWgN));
  return true;
}
static bool homeWgDeserialize(const uint8_t* b){ return homeWgParse(b, 2, HOME_WG_MAX, 0); }
static bool homeWgDeserializeV1(const uint8_t* b){ return homeWgParse(b, 1, HOME_WG_MAX_V1, 1); }

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
// proposito: el fondo se repone por filas, y regenerar la banda entera y volver
// a poner encima widgets e iconos de esas filas es correcto por construccion y
// sigue costando una fraccion de la pantalla. El fondo limpio sale del cache
// del escritorio (hpBg) si esta al dia -- una copia -- y el vidrio, del
// backdrop, igual que al componer la pagina entera.
static void wgRepaint(){
  if(!homeBuf) return;
  int page = gHomePage;
  hpBufPage = -1;                         // la pagina vecina cacheada tambien lleva widgets vivos
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
  if(hpBgOk && hpBg && y0 >= HOME_PAGE_TOP && y1 < HOME_BAND_BOT_MAX)
    memcpy(homeBuf + (size_t)y0 * SCR_W, hpBg + (size_t)(y0 - HOME_PAGE_TOP) * SCR_W,
           (size_t)(y1 - y0 + 1) * SCR_W * 2);
  else drawWallpaperRowsId(homeBuf, gWallHome, true, y0, y1);
  setBuf(homeBuf);
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y1;
  homeGlassBegin(-1);                     // misma geometria: los paneles anotados siguen valiendo
  homeDrawPage(page, 0, false);
  homeGlassEnd();
  homeDrawSafePill();
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
  bbufSys(); setBuf(bbuf);
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
