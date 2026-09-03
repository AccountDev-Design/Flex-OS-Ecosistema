// #############################################################
// ##  FLEX OS ULTRA  ·  CLIMA  ·  utilidades de dibujo, iconografia y escenas
// ##  ----------------------------------------------------------
// ##  La iconografia meteorologica y las escenas 100 % procedurales que
// ##  comparten la app, el widget del escritorio y el del bloqueo.
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
#include "FlexOS_Ultra_AppPaint.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP CLIMA  ·  "Flex Weather"  ·  estilo One UI Weather
// ##  ------------------------------------------------------
// ##  TODO lo que se ve aqui sale de FlexOS_Weather (Open-Meteo).
// ##  Esta seccion NO habla con la red ni una sola vez: pide el
// ##  puntero al WeatherState publicado y dibuja. Si no hay datos
// ##  validos, pinta el estado vacio -- nunca una temperatura
// ##  inventada.
// ##
// ##  COMO SE DIBUJA (y por que no ahoga al sistema)
// ##    · Se compone SIEMPRE en bbuf y se publica con present(),
// ##      igual que el resto de animaciones del sistema.
// ##    · wxCompose(y0,y1) recibe la BANDA que hay que rehacer y
// ##      cada seccion se salta sola si no la toca. La escena
// ##      animada vive arriba: mientras solo se mueven las nubes
// ##      se repinta la franja del cielo, no los 480x800.
// ##    · Cuando la escena queda fuera de pantalla (scroll abajo)
// ##      no se repinta NADA hasta que el dedo o los datos lo
// ##      pidan.
// ##    · Cero delay(), cero while() de espera: toda animacion es
// ##      funcion de millis() y avanza un paso por vuelta de loop.
// #############################################################

// ---- Geometria de la pagina (coordenadas de CONTENIDO) ----
#define WX_HERO_H     442          // alto de la escena + textos grandes
#define WX_CARD_X      16
#define WX_CARD_W     (SCR_W - 32)
#define WX_HOUR_H     272          // tarjeta del pronostico horario
#define WX_DAY_ROW     46
#define WX_SUN_H      196
#define WX_DET_H      112          // alto de una tarjeta de detalle
#define WX_NAV_H       64          // barra de navegacion (no se desplaza)
#define WX_VIEW_BOT   (SCR_H - WX_NAV_H)

// Panel horario: 6 columnas a la vista, como en One UI.
#define WX_COL_W       72
#define WX_HOUR_PAD    14

// ---- Vistas de la app ----
#define WXVIEW_MAIN    0
#define WXVIEW_LOCS    1
#define WXVIEW_SEARCH  2

// ---- Estado de interfaz (se conserva mientras la app esta suspendida) ----
static uint8_t  wxView       = WXVIEW_MAIN;
static float    wxScroll     = 0;      // desplazamiento vertical (px de contenido)
static float    wxScrollVel  = 0;      // inercia vertical
static int      wxContentH   = 0;      // alto total de la pagina
static float    wxHourScroll = 0;      // desplazamiento del panel horario
static float    wxHourVel    = 0;
static int      wxHourMax    = 0;
// Gesto: 0 = sin decidir, 1 = vertical, 2 = horizontal (panel horario)
static uint8_t  wxAxis       = 0;
static bool     wxDragging   = false;
static bool     wxTapValid   = false;  // el toque aun puede ser un tap
static int      wxDragX0 = 0, wxDragY0 = 0;
static float    wxDragScroll0 = 0, wxDragHour0 = 0;
static int      wxLastY = 0, wxLastX = 0;
static uint32_t wxLastMoveMs = 0;
static bool     wxPullArmed  = false;  // pull-to-refresh listo para disparar
static bool     wxPullBusy   = false;  // refresco pedido desde el gesto
static uint32_t wxPullMs     = 0;      // millis() en que se pidio (para no darlo por hecho antes de tiempo)
static uint32_t wxAnimMs     = 0;      // ultimo repintado animado
static uint32_t wxEnterMs    = 0;      // t0 de la animacion de entrada
static uint32_t wxToastMs    = 0;
static char     wxToastTxt[40] = "";
static uint32_t wxSeenGen    = 0;      // generacion de datos ya dibujada
static uint8_t  wxSeenStatus = 255;
static int      wxSelLoc     = -1;     // fila pulsada en "Mis ubicaciones"
static char     wxQuery[FLEXWX_QUERY] = "";
static uint32_t wxSceneMixMs = 0;      // t0 del cruce entre escenas
static uint8_t  wxSceneFrom  = 0xFF;   // escena anterior (0xFF = ninguna)
static uint8_t  wxSceneTo    = 0xFF;
static bool     wxSceneFromDay = true, wxSceneToDay = true;
static uint32_t wxOkFlashMs  = 0;      // destello de "actualizado"
// Clima comparte el teclado global con Wi-Fi, Notas y Seguridad. Al suspender
// se guarda solo la parte que usa su buscador para que otra app pueda usar el
// teclado sin cambiarle el idioma/capa al volver.
static uint8_t  wxKbLayout   = 0;      // 0 ES, 1 EN, 2 numeros, 3 emoji
static uint8_t  wxKbFlags    = 0;      // bit 0 idioma ES, bit 1 mayusculas

// Tops de cada seccion en coordenadas de contenido (los calcula wxLayout).
static int wxYHour = 0, wxYDaily = 0, wxYSun = 0, wxYDet = 0, wxYFoot = 0;
static int wxHDaily = 0, wxHDet = 0;

// (el enum WT_* vive arriba, junto al prototipo de wt(): lo necesita el
//  widget colocable de Clima, que se dibuja mucho antes que esta seccion)
static const char* WXT[WT_NSTR][5] = {
  {"Clima","Weather","M\xC3\xA9t\xC3\xA9o","Clima","Meteo"},
  {"Sin datos meteorol\xC3\xB3gicos","No weather data","Pas de donn\xC3\xA9" "es m\xC3\xA9t\xC3\xA9o","Sem dados meteorol\xC3\xB3gicos","Nessun dato meteo"},
  {"Con\xC3\xA9" "ctate a Wi-Fi para obtener el pron\xC3\xB3stico.","Connect to Wi-Fi to get the forecast.","Connecte-toi au Wi-Fi pour la pr\xC3\xA9vision.","Liga-te ao Wi-Fi para obter a previs\xC3\xA3o.","Collegati al Wi-Fi per la previsione."},
  {"Reintentar","Retry","R\xC3\xA9" "essayer","Tentar de novo","Riprova"},
  {"A\xC3\xB1" "adir ubicaci\xC3\xB3n","Add location","Ajouter un lieu","Adicionar local","Aggiungi posizione"},
  {"Mis ubicaciones","My locations","Mes lieux","Os meus locais","Le mie posizioni"},
  {"Buscar","Search","Rechercher","Pesquisar","Cerca"},
  {"Escribe una ciudad","Type a city","\xC3\x89" "cris une ville","Escreve uma cidade","Scrivi una citt\xC3\xA0"},
  {"Sin resultados","No results","Aucun r\xC3\xA9sultat","Sem resultados","Nessun risultato"},
  {"Buscando...","Searching...","Recherche...","A procurar...","Ricerca..."},
  {"Pron\xC3\xB3stico de 48 horas","48-hour forecast","Pr\xC3\xA9vision 48 heures","Previs\xC3\xA3o de 48 horas","Previsione 48 ore"},
  {"Pron\xC3\xB3stico","Forecast","Pr\xC3\xA9vision","Previs\xC3\xA3o","Previsione"},
  {"No te pierdas el atardecer","Don't miss the sunset","Ne rate pas le coucher","N\xC3\xA3o percas o p\xC3\xB4r do sol","Non perderti il tramonto"},
  {"Amanecer","Sunrise","Lever","Nascer do sol","Alba"},
  {"Atardecer","Sunset","Coucher","P\xC3\xB4r do sol","Tramonto"},
  {"Humedad","Humidity","Humidit\xC3\xA9","Humidade","Umidit\xC3\xA0"},
  {"Viento","Wind","Vent","Vento","Vento"},
  {"\xC3\x8Dndice UV","UV index","Indice UV","\xC3\x8Dndice UV","Indice UV"},
  {"Visibilidad","Visibility","Visibilit\xC3\xA9","Visibilidade","Visibilit\xC3\xA0"},
  {"Precipitaci\xC3\xB3n","Precipitation","Pr\xC3\xA9" "cipitations","Precipita\xC3\xA7\xC3\xA3o","Precipitazioni"},
  {"Sensaci\xC3\xB3n t\xC3\xA9rmica","Feels like","Ressenti","Sensa\xC3\xA7\xC3\xA3o t\xC3\xA9rmica","Percepita"},
  {"Presi\xC3\xB3n","Pressure","Pression","Press\xC3\xA3o","Pressione"},
  {"No disponible","Not available","Non disponible","Indispon\xC3\xADvel","Non disponibile"},
  {"Actualizado hace %d min","Updated %d min ago","Mis \xC3\xA0 jour il y a %d min","Atualizado h\xC3\xA1 %d min","Aggiornato %d min fa"},
  {"Actualizado a las %s","Updated at %s","Mis \xC3\xA0 jour \xC3\xA0 %s","Atualizado \xC3\xA0s %s","Aggiornato alle %s"},
  {"Sin conexi\xC3\xB3n","Offline","Hors ligne","Sem liga\xC3\xA7\xC3\xA3o","Offline"},
  {"Actualizando...","Updating...","Mise \xC3\xA0 jour...","A atualizar...","Aggiornamento..."},
  {"Datos meteorol\xC3\xB3gicos: Open-Meteo","Weather data: Open-Meteo","Donn\xC3\xA9" "es m\xC3\xA9t\xC3\xA9o : Open-Meteo","Dados meteorol\xC3\xB3gicos: Open-Meteo","Dati meteo: Open-Meteo"},
  {"Hoy","Today","Aujourd'hui","Hoje","Oggi"},
  {"Desliza para actualizar","Pull to refresh","Tire pour actualiser","Puxe para atualizar","Trascina per aggiornare"},
  {"Suelta para actualizar","Release to refresh","Rel\xC3\xA2" "che pour actualiser","Solte para atualizar","Rilascia per aggiornare"},
  {"No se pudo actualizar el clima","Couldn't update the weather","Mise \xC3\xA0 jour impossible","N\xC3\xA3o foi poss\xC3\xADvel atualizar","Aggiornamento non riuscito"},
  {"Cargando el pron\xC3\xB3stico...","Loading forecast...","Chargement...","A carregar...","Caricamento..."},
  {"Rachas","Gusts","Rafales","Rajadas","Raffiche"},
  {"Eliminar","Delete","Supprimer","Eliminar","Elimina"},
  {"A\xC3\xB1" "ade una ciudad para ver su clima","Add a city to see its weather","Ajoute une ville","Adiciona uma cidade","Aggiungi una citt\xC3\xA0"},
  {"Ahora","Now","Maintenant","Agora","Ora"},
};
static const char* wt(int id){ return WXT[id][LI()]; }

// ---- Paleta ----
// La ESCENA es CONTENIDO (cielo, montanas, sol), igual que el wallpaper del
// Inicio: no se retine con la apariencia. Y las tarjetas van APOYADAS sobre
// ella, que es exactamente el caso que el tema semantico ya resuelve con
// TH_WALLSURF / TH_ONWALL (los mismos tokens que usan las tarjetas del
// Bloqueo). Asi Clima no inventa una segunda paleta: hereda la del sistema y
// respeta la excepcion deliberada documentada junto a TH_ONWALL.
#define WX_CARD_BG   (uiGlass ? TH_WALLSURF2 : TH_WALLSURF)
#define WX_CARD_A    (uiGlass ? 190 : 170)
#define WX_TXT_HI    TH_ONWALL
#define WX_TXT_LO    TH_ONWALL2
#define WX_TXT_MUTE  mix565(TH_ONWALL2, WX_CARD_BG, 90)
#define WX_ACCENT    rgb565(90,170,235)
#define WX_LINE      mix565(TH_ONWALL2, WX_CARD_BG, 190)

// #############################################################
// ##  UTILIDADES DE DIBUJO PROPIAS DE CLIMA
// #############################################################

// Copia recortada EXPLICITA. En esta app varios textos se acortan a proposito
// para caber en su hueco (nombre de ciudad en el widget, condicion, etc.), asi
// que se dice con todas las letras en vez de dejarlo en manos de snprintf.
static void wxCopy(char* dst, size_t n, const char* src){
  if(!dst || n == 0) return;
  if(!src){ dst[0] = 0; return; }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

// Anillo con alpha (el simbolo de grado). fillRing no admite alpha y el
// grado aparece junto a textos que se funden en la animacion de entrada.
static void wxRingA(int cx, int cy, int r, int th, uint16_t col, uint8_t a){
  if(r <= 0 || a == 0) return;
  int ri = r - th; if(ri < 0) ri = 0;
  int r2 = r * r, ri2 = ri * ri;
  for(int dy = -r; dy <= r; dy++){
    for(int dx = -r; dx <= r; dx++){
      int d2 = dx * dx + dy * dy;
      if(d2 <= r2 && d2 >= ri2) pxA(cx + dx, cy + dy, col, a);
    }
  }
}
// Segmento con alpha (la version AA del sistema no la admite).
static void wxLineA(float x0, float y0, float x1, float y1, int rad, uint16_t col, uint8_t a){
  if(a == 0) return;
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int n = (int)len + 1;
  if(n > 400) n = 400;
  for(int i = 0; i <= n; i++){
    float t = (float)i / n;
    int cx = (int)(x0 + dx * t + 0.5f), cy = (int)(y0 + dy * t + 0.5f);
    if(rad <= 0) pxA(cx, cy, col, a);
    else fillCircleA(cx, cy, rad, col, a);
  }
}
// Temperatura entera + grado, devuelve el x final. size en unidades de fuente.
static int wxDrawTemp(int x, int y, float t, int size, uint16_t col, uint8_t a){
  char s[8]; snprintf(s, sizeof(s), "%d", (int)lroundf(t));
  int ex = drawTextA(x, y, s, size, col, a);
  // El anillo se dibuja a mano (la fuente no tiene el simbolo de grado). En
  // tamanos pequenos el grosor baja a 1 px: con 2 se rellenaba y parecia un
  // asterisco.
  int r  = size >= 8 ? size + 2 : (size >= 4 ? size : 3);
  int th = size >= 8 ? 5 : (size >= 4 ? 2 : 1);
  wxRingA(ex + r + 2, y + r + (size >= 8 ? 4 : 1), r, th, col, a);
  return ex + 2 * r + 6;
}
// Igual, centrado en cx.
static void wxDrawTempC(int cx, int y, float t, int size, uint16_t col, uint8_t a){
  char s[8]; snprintf(s, sizeof(s), "%d", (int)lroundf(t));
  int r = size >= 4 ? size : size + 1;
  int w = textW(s, size) + 2 * r + 4;
  wxDrawTemp(cx - w / 2, y, t, size, col, a);
}

// Texto con salto de linea por palabras. Devuelve el numero de lineas.
static int wxWrapText(int x, int y, int w, const char* s, int size, uint16_t col,
                      uint8_t a, int lineH, int maxLines, bool center){
  char line[96]; int ll = 0, lines = 0;
  const char* p = s;
  while(*p && lines < maxLines){
    // siguiente palabra
    const char* ws = p;
    while(*p && *p != ' ') p++;
    int wl = (int)(p - ws);
    if(wl > 90) wl = 90;
    char word[96];
    memcpy(word, ws, wl); word[wl] = 0;
    char cand[96];
    if(ll){
      wxCopy(cand, sizeof(cand), line);
      size_t cl = strlen(cand);
      if(cl + 1 < sizeof(cand)){ cand[cl] = ' '; wxCopy(cand + cl + 1, sizeof(cand) - cl - 1, word); }
    } else wxCopy(cand, sizeof(cand), word);
    if(textW(cand, size) <= w || ll == 0){
      wxCopy(line, sizeof(line), cand);
      ll = (int)strlen(line);
    } else {
      if(center) drawTextCA(x + w / 2, y + lines * lineH, line, size, col, a);
      else       drawTextA(x, y + lines * lineH, line, size, col, a);
      lines++;
      wxCopy(line, sizeof(line), word);
      ll = (int)strlen(line);
    }
    while(*p == ' ') p++;
  }
  if(ll && lines < maxLines){
    if(center) drawTextCA(x + w / 2, y + lines * lineH, line, size, col, a);
    else       drawTextA(x, y + lines * lineH, line, size, col, a);
    lines++;
  }
  return lines;
}

// Hora local (unix+offset) -> "19:00" o "7 p. m." segun el ajuste del sistema.
static void wxFmtHour(int32_t localUnix, char* out, size_t n){
  int32_t d = localUnix % 86400; if(d < 0) d += 86400;
  int h = (int)(d / 3600), mi = (int)((d % 3600) / 60);
  if(g24h){ snprintf(out, n, "%d:%02d", h, mi); return; }
  int h12 = h % 12; if(h12 == 0) h12 = 12;
  if(cfgLang == 0) snprintf(out, n, "%d %s", h12, h < 12 ? "a. m." : "p. m.");
  else             snprintf(out, n, "%d %s", h12, h < 12 ? "AM" : "PM");
}
static void wxFmtHourShort(int32_t localUnix, char* out, size_t n){
  int32_t d = localUnix % 86400; if(d < 0) d += 86400;
  int h = (int)(d / 3600);
  if(g24h){ snprintf(out, n, "%d", h); return; }
  int h12 = h % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d%c", h12, h < 12 ? 'a' : 'p');
}
static void wxFmtClock(int32_t localUnix, char* out, size_t n){
  int32_t d = localUnix % 86400; if(d < 0) d += 86400;
  int h = (int)(d / 3600), mi = (int)((d % 3600) / 60);
  if(g24h){ snprintf(out, n, "%d:%02d", h, mi); return; }
  int h12 = h % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d %s", h12, mi, h < 12 ? "AM" : "PM");
}
// Dia de la semana (0=domingo) a partir de un unix LOCAL. 1970-01-01 fue jueves.
static int wxWeekday(int32_t localUnix){
  int32_t days = localUnix / 86400;
  if(localUnix < 0) days--;
  int wd = (int)((days % 7 + 7) % 7);      // 0 = jueves
  return (wd + 4) % 7;                     // 0 = domingo, como WD_FULL
}

// #############################################################
// ##  ICONOGRAFIA METEOROLOGICA  ·  UNA sola implementacion
// ##  La usan la app, el widget del Home y el del bloqueo, asi
// ##  que un mismo weather_code produce SIEMPRE el mismo dibujo.
// #############################################################
// Nube de ancho 'w' y alto ~w/2.6: base redondeada + tres bultos. Antes eran
// tres circulos de radio w/4, que a tamano de escena (200 px) salian como
// burbujas gigantes encima del texto.
static void wxCloudA(int cx, int cy, int w, uint16_t col, uint8_t a){
  int r = w / 6; if(r < 2) r = 2;
  fillRoundRectA(cx - w / 2, cy - r / 2, w, r + r / 2 + 1, r, col, a);
  fillCircleA(cx - r, cy - r / 2, r, col, a);
  fillCircleA(cx + (r * 3) / 2, cy - r / 3, (r * 3) / 4, col, a);
  fillCircleA(cx + r / 4, cy - r, (r * 5) / 4, col, a);
}
static void wxSunA(int cx, int cy, int r, uint16_t col, uint16_t glow, uint8_t a, bool rays){
  if(a == 0) return;
  fillCircleA(cx, cy, r + r / 2, glow, (uint8_t)(a / 6));
  fillCircleA(cx, cy, r + r / 4, glow, (uint8_t)(a / 4));
  fillCircleA(cx, cy, r, col, a);
  if(rays){
    for(int k = 0; k < 8; k++){
      float ang = k * 45.0f * 0.0174532925f;
      float x0 = cx + cosf(ang) * (r + r / 3), y0 = cy + sinf(ang) * (r + r / 3);
      float x1 = cx + cosf(ang) * (r + r * 3 / 4), y1 = cy + sinf(ang) * (r + r * 3 / 4);
      wxLineA(x0, y0, x1, y1, r / 6, col, a);
    }
  }
}
static void wxMoonA(int cx, int cy, int r, uint16_t col, uint16_t sky, uint8_t a){
  if(a == 0) return;
  fillCircleA(cx, cy, r + r / 3, col, (uint8_t)(a / 7));
  fillCircleA(cx, cy, r, col, a);
  fillCircleA(cx + r / 2, cy - r / 3, r, sky, a);     // mordisco: fase creciente
}
static void wxDropA(int x, int y, int len, uint16_t col, uint8_t a){
  wxLineA((float)x, (float)y, (float)(x - len / 3), (float)(y + len), 0, col, a);
}
static void wxFlakeA(int x, int y, int r, uint16_t col, uint8_t a){
  wxLineA(x - r, y, x + r, y, 0, col, a);
  wxLineA(x, y - r, x, y + r, 0, col, a);
  wxLineA(x - r * 2 / 3, y - r * 2 / 3, x + r * 2 / 3, y + r * 2 / 3, 0, col, a);
  wxLineA(x - r * 2 / 3, y + r * 2 / 3, x + r * 2 / 3, y - r * 2 / 3, 0, col, a);
}
static void wxBoltA(int x, int y, int h, uint16_t col, uint8_t a){
  int w = h / 2;
  wxLineA(x + w / 2, y, x - w / 4, y + h / 2, 2, col, a);
  wxLineA(x - w / 4, y + h / 2, x + w / 4, y + h / 2, 2, col, a);
  wxLineA(x + w / 4, y + h / 2, x - w / 2, y + h, 2, col, a);
}

// Icono completo. r = radio nominal (el icono ocupa ~2.4r de ancho).
static void wxDrawIcon(int cx, int cy, int r, uint8_t vis, bool day, uint8_t a){
  if(a == 0 || r <= 0) return;
  uint16_t white = rgb565(244,248,255), grey = rgb565(196,208,228);
  uint16_t sun   = rgb565(255,206,72),  moonc = TH_ONWALL2;
  uint16_t rain  = rgb565(120,190,250), snow = rgb565(226,240,255);
  uint16_t dark  = rgb565(150,164,190), bolt = rgb565(255,214,90);
  uint16_t sky   = WX_CARD_BG;    // el "mordisco" de la luna se recorta contra la tarjeta
  switch(vis){
    case WXV_CLEAR:
      if(day) wxSunA(cx, cy, r, sun, sun, a, true);
      else    wxMoonA(cx, cy, r, moonc, sky, a);
      break;
    case WXV_MOSTLY_CLEAR:
      if(day) wxSunA(cx - r / 3, cy - r / 4, (r * 4) / 5, sun, sun, a, true);
      else    wxMoonA(cx - r / 3, cy - r / 4, (r * 4) / 5, moonc, sky, a);
      wxCloudA(cx + r / 2, cy + r / 2, (r * 7) / 5, white, (uint8_t)(a * 3 / 4));
      break;
    case WXV_PARTLY:
      if(day) wxSunA(cx - r / 2, cy - r / 2, (r * 3) / 4, sun, sun, a, false);
      else    wxMoonA(cx - r / 2, cy - r / 2, (r * 3) / 4, moonc, sky, a);
      wxCloudA(cx + r / 4, cy + r / 3, (r * 11) / 5, white, a);
      break;
    case WXV_CLOUDY:
      wxCloudA(cx - r / 3, cy - r / 3, (r * 8) / 5, grey, a);
      wxCloudA(cx + r / 3, cy + r / 3, (r * 11) / 5, white, a);
      break;
    case WXV_FOG:
      wxCloudA(cx, cy - r / 2, (r * 11) / 5, grey, a);
      for(int i = 0; i < 3; i++)
        wxLineA(cx - r, cy + r / 2 + i * (r / 2), cx + r, cy + r / 2 + i * (r / 2), r / 8, dark, a);
      break;
    case WXV_DRIZZLE:
      wxCloudA(cx, cy - r / 2, (r * 11) / 5, white, a);
      for(int i = 0; i < 2; i++) wxDropA(cx - r / 2 + i * r, cy + r / 2, r / 2, rain, a);
      break;
    case WXV_RAIN:
      wxCloudA(cx, cy - r / 2, (r * 11) / 5, white, a);
      for(int i = 0; i < 3; i++) wxDropA(cx - r + i * r, cy + r / 2, (r * 2) / 3, rain, a);
      break;
    case WXV_HEAVY_RAIN:
      wxCloudA(cx, cy - r / 2, (r * 12) / 5, grey, a);
      for(int i = 0; i < 4; i++) wxDropA(cx - r - r / 4 + i * (r * 3) / 4, cy + r / 2, r, rain, a);
      break;
    case WXV_SLEET:
      wxCloudA(cx, cy - r / 2, (r * 11) / 5, white, a);
      wxDropA(cx - r / 2, cy + r / 2, (r * 2) / 3, rain, a);
      wxFlakeA(cx + r / 2, cy + r, r / 3, snow, a);
      break;
    case WXV_SNOW:
      wxCloudA(cx, cy - r / 2, (r * 11) / 5, white, a);
      wxFlakeA(cx - r / 2, cy + r, r / 3, snow, a);
      wxFlakeA(cx + r / 2, cy + r * 3 / 4, r / 3, snow, a);
      break;
    case WXV_THUNDER:
      wxCloudA(cx, cy - r / 2, (r * 11) / 5, grey, a);
      wxBoltA(cx, cy + r / 3, r, bolt, a);
      break;
    case WXV_HAIL:
      wxCloudA(cx, cy - r / 2, (r * 11) / 5, grey, a);
      wxBoltA(cx - r / 3, cy + r / 3, r, bolt, a);
      fillCircleA(cx + r / 2, cy + r, r / 4, snow, a);
      break;
    default:
      wxCloudA(cx, cy, (r * 11) / 5, white, a);
      break;
  }
}

// #############################################################
// ##  ESCENAS METEOROLOGICAS  ·  100 % procedurales
// ##  ------------------------------------------------------
// ##  Ni un solo bitmap: cielo en degradado, montanas, agua,
// ##  astro, nubes y particulas se dibujan con las primitivas del
// ##  motor. Cuesta menos memoria que una imagen 480x442 (424 KB)
// ##  y ademas se puede animar y fundir entre estados.
// ##
// ##  Las particulas NO guardan estado: su posicion es funcion de
// ##  millis() y de un hash del indice. Asi no hay nada que
// ##  reiniciar al entrar/salir, no hay deriva y no hay malloc.
// #############################################################

static inline uint32_t wxHash(uint32_t i){
  i ^= i << 13; i ^= i >> 17; i ^= i << 5;
  return i;
}
static inline int wxHashRange(uint32_t i, int lo, int hi){
  if(hi <= lo) return lo;
  return lo + (int)(wxHash(i * 2654435761u + 12345u) % (uint32_t)(hi - lo));
}

// Paleta y "cantidades" de cada escena. Todo sale del weather_code
// REAL y de is_day/amanecer/atardecer reales: aqui no se elige nada
// a mano.
static void wxSceneOf(uint8_t vis, bool day, WxScene* s){
  memset(s, 0, sizeof(WxScene));
  bool rainy = false;                 // cielo de lluvia: paleta comun a todos los codigos de agua
  if(day){
    s->skyTop = rgb565(64,132,224); s->skyMid = rgb565(126,182,240); s->skyBot = rgb565(196,222,246);
    s->land   = rgb565(58,104,150); s->land2  = rgb565(40,78,120);
    s->glow   = rgb565(255,214,96); s->sun = 1;
  } else {
    s->skyTop = rgb565(20,22,64);   s->skyMid = rgb565(44,44,104);  s->skyBot = rgb565(92,80,150);
    s->land   = rgb565(30,32,74);   s->land2  = rgb565(18,20,50);
    s->glow   = rgb565(232,238,255); s->sun = 2; s->stars = 210;
  }
  switch(vis){
    case WXV_CLEAR:                                          s->clouds = 20;  break;
    case WXV_MOSTLY_CLEAR:                                   s->clouds = 70;  break;
    case WXV_PARTLY:                                         s->clouds = 150; break;
    case WXV_CLOUDY:
      s->clouds = 235; s->stars = (uint8_t)(s->stars / 5);
      if(day){ s->skyTop = rgb565(104,124,152); s->skyMid = rgb565(146,162,184); s->skyBot = rgb565(196,204,216);
               s->land = rgb565(74,88,108); s->land2 = rgb565(54,66,84); }
      else   { s->skyTop = rgb565(24,28,48);  s->skyMid = rgb565(44,50,76);  s->skyBot = rgb565(70,78,106); }
      s->sun = 0;
      break;
    case WXV_FOG:
      s->clouds = 120; s->fog = 220; s->sun = 0; s->stars = 0;
      if(day){ s->skyTop = rgb565(150,158,168); s->skyMid = rgb565(178,184,192); s->skyBot = rgb565(206,210,216);
               s->land = rgb565(120,128,140); s->land2 = rgb565(96,104,118); }
      else   { s->skyTop = rgb565(34,38,52); s->skyMid = rgb565(56,60,76); s->skyBot = rgb565(84,88,104); }
      break;
    case WXV_DRIZZLE: s->clouds = 210; s->rain = 90;  s->sun = 0; s->stars = 0; rainy = true; break;
    case WXV_RAIN:    s->clouds = 235; s->rain = 170; s->sun = 0; s->stars = 0; rainy = true; break;
    case WXV_HEAVY_RAIN: s->clouds = 255; s->rain = 250; s->sun = 0; s->stars = 0; rainy = true; break;
    case WXV_SLEET:   s->clouds = 235; s->rain = 130; s->snow = 90; s->sun = 0; s->stars = 0; rainy = true; break;
    case WXV_THUNDER: s->clouds = 255; s->rain = 200; s->bolt = 200; s->sun = 0; s->stars = 0; rainy = true; break;
    case WXV_HAIL:    s->clouds = 255; s->rain = 210; s->snow = 70; s->bolt = 230; s->sun = 0; s->stars = 0; rainy = true; break;
    case WXV_SNOW:
      s->clouds = 240; s->snow = 220; s->sun = 0; s->stars = 0;
      // (la paleta de nieve es propia: no pasa por el bloque 'rainy')
      if(day){ s->skyTop = rgb565(140,158,186); s->skyMid = rgb565(178,192,212); s->skyBot = rgb565(214,224,236);
               s->land = rgb565(200,212,230); s->land2 = rgb565(168,182,204); }
      else   { s->skyTop = rgb565(28,34,58); s->skyMid = rgb565(52,62,92); s->skyBot = rgb565(92,104,140);
               s->land = rgb565(78,90,120); s->land2 = rgb565(52,62,88); }
      break;
    default: break;
  }
  if(rainy){
    if(day){ s->skyTop = rgb565(72,86,110);  s->skyMid = rgb565(104,120,146); s->skyBot = rgb565(150,166,190);
             s->land = rgb565(52,66,88); s->land2 = rgb565(36,48,66); }
    else   { s->skyTop = rgb565(16,20,38);  s->skyMid = rgb565(32,40,64);  s->skyBot = rgb565(56,66,94);
             s->land = rgb565(20,26,46); s->land2 = rgb565(12,16,32); }
  }
}

// Mezcla de dos escenas (transicion suave dia<->noche o de condicion).
static void wxSceneBlend(WxScene* out){
  const FlexWeather* w = flexWeatherData();
  uint8_t vis = w ? flexWeatherVisual(w->code) : (uint8_t)WXV_CLOUDY;
  bool day = true;
  if(w){
    day = w->isDay ? true : false;
    // Si hay amanecer/atardecer reales, mandan ellos (is_day solo cambia
    // cuando la API refresca; el arco solar corre con el reloj de la API).
    if(w->have & WXF_SUN){
      int32_t now = flexWeatherNowLocal() - w->utcOffset;      // de vuelta a UTC
      if(now > 0 && w->sunrise && w->sunset) day = (now >= w->sunrise && now < w->sunset);
    }
  }
  if(wxSceneTo == 0xFF){ wxSceneTo = vis; wxSceneToDay = day; wxSceneFrom = vis; wxSceneFromDay = day; wxSceneMixMs = 0; }
  if(vis != wxSceneTo || day != wxSceneToDay){
    // Arranca un cruce: lo que hubiera a medias se da por terminado y la
    // escena que se estaba mostrando pasa a ser el origen del nuevo fundido.
    wxSceneFrom = wxSceneTo; wxSceneFromDay = wxSceneToDay;
    wxSceneTo = vis; wxSceneToDay = day;
    wxSceneMixMs = millis();
  }
  WxScene a, b;
  wxSceneOf(wxSceneFrom, wxSceneFromDay, &a);
  wxSceneOf(wxSceneTo,   wxSceneToDay,   &b);
  uint32_t el = wxSceneMixMs ? (millis() - wxSceneMixMs) : 900;
  uint8_t t = (el >= 900) ? 255 : (uint8_t)(el * 255 / 900);
  out->skyTop = mix565(a.skyTop, b.skyTop, t);
  out->skyMid = mix565(a.skyMid, b.skyMid, t);
  out->skyBot = mix565(a.skyBot, b.skyBot, t);
  out->land   = mix565(a.land,   b.land,   t);
  out->land2  = mix565(a.land2,  b.land2,  t);
  out->glow   = mix565(a.glow,   b.glow,   t);
  out->stars  = (uint8_t)((a.stars  * (255 - t) + b.stars  * t) / 255);
  out->clouds = (uint8_t)((a.clouds * (255 - t) + b.clouds * t) / 255);
  out->rain   = (uint8_t)((a.rain   * (255 - t) + b.rain   * t) / 255);
  out->snow   = (uint8_t)((a.snow   * (255 - t) + b.snow   * t) / 255);
  out->fog    = (uint8_t)((a.fog    * (255 - t) + b.fog    * t) / 255);
  out->bolt   = (uint8_t)((a.bolt   * (255 - t) + b.bolt   * t) / 255);
  out->sun    = (t < 128) ? a.sun : b.sun;
  out->sunFade = (a.sun == b.sun) ? 255 : (uint8_t)((t < 128) ? (255 - t * 2) : ((t - 128) * 2));
}

// true si la escena tiene algo que animar en este momento.
static bool wxSceneAnimated(const WxScene* s){
  return s->clouds > 30 || s->rain || s->snow || s->fog || s->stars || s->sun || s->bolt;
}

// ---- Fondo completo: degradado + escena. Solo pinta filas [y0,y1]. ----
static void wxDrawScene(int y0, int y1){
  WxScene s; wxSceneBlend(&s);
  // Parallax: la escena se desplaza algo menos que el contenido.
  int par = (int)(wxScroll * 0.80f);
  int top = -par;                              // y de pantalla del techo de la escena
  int heroBot = WX_HERO_H - par;

  // Degradado de pantalla completa: cielo arriba y, por debajo del horizonte,
  // una caida hacia un azul mas profundo. Es el "papel" sobre el que flotan
  // las tarjetas -- y al desplazarse, el fondo se oscurece solo, que es lo que
  // da el aire de One UI Weather (y de paso sube el contraste del pie).
  int hb = heroBot; if(hb < 140) hb = 140; if(hb > SCR_H) hb = SCR_H;
  int mid = hb / 2;
  // Por debajo del horizonte el cielo cae hacia el fondo de pagina del tema:
  // con apariencia oscura se hunde en azul profundo y con la clara se abre,
  // sin que la escena deje de ser la misma.
  uint16_t pageBot = mix565(s.skyBot, TH_PAGE, gDark ? 170 : 130);
  for(int y = y0; y <= y1; y++){
    uint16_t c;
    if(y <= mid)      c = mix565(s.skyTop, s.skyMid, (uint8_t)(y * 255 / (mid > 0 ? mid : 1)));
    else if(y <= hb)  c = mix565(s.skyMid, s.skyBot, (uint8_t)((y - mid) * 255 / (hb - mid > 0 ? hb - mid : 1)));
    else              c = mix565(s.skyBot, pageBot,  (uint8_t)((y - hb) * 255 / (SCR_H - hb > 0 ? SCR_H - hb : 1)));
    hLine(0, y, SCR_W, c);
  }

  if(heroBot < y0 - 40) return;                // la escena ya no se ve: nada que dibujar
  // Los elementos de la escena NUNCA se salen de la cabecera: sin este
  // recorte, al desplazarse se veian montanas por detras de las tarjetas de
  // media pagina.
  int oy1 = gClipY1;
  if(heroBot < gClipY1) gClipY1 = heroBot;
  uint32_t ms = millis();

  // Estrellas (solo de noche). Parpadeo por funcion de millis: sin estado.
  if(s.stars){
    for(int i = 0; i < 42; i++){
      int sx = wxHashRange(i * 7 + 1, 6, SCR_W - 6);
      int sy = top + wxHashRange(i * 13 + 3, 30, 300);
      if(sy < y0 - 2 || sy > y1 + 2) continue;
      float ph = (ms % 4000) / 4000.0f * 6.2831853f + i;
      int a = (int)(s.stars * (0.45f + 0.55f * (0.5f + 0.5f * sinf(ph))));
      int r = (i % 7 == 0) ? 2 : 1;
      fillCircleA(sx, sy, r, rgb565(255,255,255), (uint8_t)a);
    }
  }

  // Astro: sol con destello lento, o luna en fase.
  if(s.sun && s.sunFade){
    int cx = SCR_W - 132, cy = top + 178;
    int rr = 34;
    float pulse = 0.5f + 0.5f * sinf((ms % 5200) / 5200.0f * 6.2831853f);
    if(cy + rr * 3 >= y0 && cy - rr * 3 <= y1){
      if(s.sun == 1){
        fillCircleA(cx, cy, (int)(rr * 2.6f + pulse * 6), s.glow, (uint8_t)(s.sunFade / 12));
        fillCircleA(cx, cy, (int)(rr * 1.7f + pulse * 4), s.glow, (uint8_t)(s.sunFade / 7));
        wxSunA(cx, cy, rr, rgb565(255,238,178), s.glow, s.sunFade, false);
      } else {
        wxMoonA(cx, cy, rr - 6, s.glow, s.skyTop, s.sunFade);
      }
    }
  }

  // Montanas + lamina de agua: dan el "paisaje" de One UI sin un bitmap.
  int hz = heroBot - 96;                                  // linea de horizonte
  if(hz < y1 && heroBot > y0){
    // cordillera lejana
    for(int k = 0; k < 3; k++){
      int bx = 40 + k * 160, bw = 210, bh = 74 + (k % 2) * 26;
      fillTriangle(bx - bw / 2, hz, bx + bw / 2, hz, bx, hz - bh, s.land2);
    }
    // cordillera cercana
    for(int k = 0; k < 2; k++){
      int bx = 130 + k * 230, bw = 260, bh = 52 + k * 18;
      fillTriangle(bx - bw / 2, hz, bx + bw / 2, hz, bx, hz - bh, s.land);
    }
    // agua: banda mas oscura con dos brillos horizontales
    if(hz + 1 <= y1){
      int wh = heroBot - hz;
      if(wh > 0){
        for(int y = hz; y < heroBot; y++){
          if(y < y0 || y > y1) continue;
          uint8_t t = (uint8_t)((y - hz) * 255 / (wh ? wh : 1));
          hLine(0, y, SCR_W, mix565(s.land2, s.skyBot, (uint8_t)(t / 3)));
        }
        for(int i = 0; i < 5; i++){
          int ly = hz + 12 + i * 14;
          if(ly < y0 || ly > y1) continue;
          int lw = 60 + ((i * 37) % 90);
          int lx = 40 + ((i * 91 + (int)(ms / 240)) % (SCR_W - 120));
          hLineA(lx, ly, lw, rgb565(255,255,255), (uint8_t)(26 - i * 3));
        }
      }
    }
  }

  // Nubes: 5 cuerpos a distinta altura y velocidad. Un barrido completo
  // tarda entre 50 y 90 s -- movimiento perceptible pero nunca inquieto.
  if(s.clouds > 12){
    for(int i = 0; i < 5; i++){
      int cw  = 120 + (i % 3) * 54;
      int spd = 70 + (i % 4) * 26;                       // ms por pixel
      int cx  = (int)(((ms / spd) + i * 149) % (uint32_t)(SCR_W + cw * 2)) - cw;
      int cy  = top + 90 + (i % 4) * 46;
      if(cy + cw / 2 < y0 || cy - cw / 2 > y1) continue;
      // Color de nube segun la escena: blancas con cielo despejado, plomizas
      // con lluvia (mezcladas con el propio cielo, asi encajan siempre).
      uint16_t cc = mix565(rgb565(255,255,255), s.skyMid, (uint8_t)((s.rain ? 120 : 30) + i * 16));
      uint8_t a = (uint8_t)((s.clouds * (i % 2 ? 3 : 4) / 5) * 3 / 5);
      wxCloudA(cx, cy, cw, cc, a);
    }
  }

  // Niebla: bandas horizontales que reptan muy despacio.
  if(s.fog){
    for(int i = 0; i < 4; i++){
      int fy = top + 150 + i * 54;
      if(fy < y0 - 20 || fy > y1 + 20) continue;
      int off = (int)(((ms / 90) + i * 130) % 300) - 150;
      fillRoundRectA(off - 40, fy, SCR_W + 120, 26, 13, rgb565(232,236,244), (uint8_t)(s.fog / 5));
    }
  }

  // Lluvia: gotas inclinadas. Cantidad proporcional a la intensidad REAL
  // del codigo (llovizna != lluvia intensa), con tope fijo de 64.
  if(s.rain){
    int n = 16 + (s.rain * 48) / 255;
    uint16_t col = rgb565(190,220,255);
    for(int i = 0; i < n; i++){
      int span = WX_HERO_H + 60;
      int y = top + (int)(((ms / 3) + (uint32_t)wxHashRange(i + 700, 0, span)) % (uint32_t)span) - 30;
      if(y < y0 - 12 || y > y1) continue;
      int x = wxHashRange(i + 91, 0, SCR_W) + (y % 40) / 6;
      wxDropA(x, y, 12 + (i % 3) * 4, col, (uint8_t)(90 + (s.rain / 3)));
    }
  }

  // Nieve: copos que caen mas lento y ondulan.
  if(s.snow){
    int n = 10 + (s.snow * 26) / 255;
    for(int i = 0; i < n; i++){
      int span = WX_HERO_H + 60;
      int y = top + (int)(((ms / 14) + (uint32_t)wxHashRange(i + 1300, 0, span)) % (uint32_t)span) - 30;
      if(y < y0 - 8 || y > y1) continue;
      int bx = wxHashRange(i + 177, 0, SCR_W);
      int x = bx + (int)(9.0f * sinf((ms % 6000) / 6000.0f * 6.2831853f + i));
      fillCircleA(x, y, 1 + (i % 3 == 0 ? 1 : 0), rgb565(255,255,255), (uint8_t)(120 + s.snow / 3));
    }
  }

  // Relampago: destello corto cada ~6 s. No es aleatorio por frame (eso
  // parpadearia distinto en cada repintado): es funcion del reloj.
  if(s.bolt){
    uint32_t ph = ms % 6000;
    if(ph < 150){
      uint8_t a = (uint8_t)((ph < 60 ? ph * 2 : (150 - ph)) * s.bolt / 255);
      int fy0 = y0 > 0 ? y0 : 0, fy1 = (y1 < heroBot) ? y1 : heroBot;
      if(fy1 > fy0) fillRectA(0, fy0, SCR_W, fy1 - fy0, rgb565(255,255,255), (uint8_t)(a / 3));
    }
  }
  gClipY1 = oy1;

  // Velo superior muy suave: la hora, el nombre de la ubicacion y la
  // temperatura gigante siempre legibles, pase lo que pase por detras
  // (una nube blanca sobre cielo claro se comia el texto).
  for(int y = y0; y <= y1 && y < 210; y++)
    hLineA(0, y, SCR_W, rgb565(6,12,30), (uint8_t)(54 - y * 54 / 210));
}
