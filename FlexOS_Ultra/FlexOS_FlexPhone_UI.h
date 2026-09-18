// #############################################################
// ##  FLEX PHONE · COMPONENTES DE INTERFAZ
// ##  ---------------------------------------------------------
// ##  Un solo sitio para las tarjetas, los botones, las pildoras de
// ##  estado y las filas que usan TODAS las pantallas de Flex Phone
// ##  -- y tambien el Centro de notificaciones.
// ##
// ##  POR QUE EXISTE
// ##  ------------------------------------------------------------
// ##  La version anterior repetia el vidrio pantalla por pantalla y
// ##  resolvia los toques comparando coordenadas a mano
// ##  ("if(T.y > 350)"). Eso tiene dos consecuencias que se pagan
// ##  siempre: mover un elemento 20 px rompe su zona tactil en
// ##  silencio, y cada pantalla acaba con un gris propio.
// ##
// ##  Aqui el DIBUJO y el TOQUE salen del mismo sitio: cada
// ##  componente que se puede pulsar registra su rectangulo real
// ##  mientras se pinta. Si se mueve, su zona tactil se mueve con
// ##  el, porque es literalmente la misma.
// ##
// ##  RENDIMIENTO
// ##  ------------------------------------------------------------
// ##  El vidrio usa drawGlassCardFlat, que cachea la tarjeta y la
// ##  copia por filas: no se recalcula un desenfoque por tarjeta y
// ##  por cuadro. Y ninguna pantalla de Flex Phone se repinta por
// ##  cuadro -- solo cuando algo cambia de verdad.
// ##
// ##  ES UN MODULO DEL SKETCH: usa las primitivas graficas, que son
// ##  `static` dentro del .ino.
// #############################################################
#pragma once

// =============================================================
//  1) SEMANTICA DE ESTADO  (la misma en todo el ecosistema)
// =============================================================
// Un estado se dice con COLOR **y** con FORMA. Quien no distingue
// el verde del rojo tiene que poder leer la pantalla igual, y ahi
// un punto de color no basta (ver §accesibilidad).
enum {
  FG_ST_OK = 0,      // verde   · circulo relleno   · conectado / activo
  FG_ST_BUSY,        // ambar   · anillo            · conectando / reconectando
  FG_ST_BAD,         // rojo    · aspa              · desconectado / error
  FG_ST_OFF,         // gris    · guion             · no disponible / apagado
};

static uint16_t fgStColor(uint8_t st){
  switch(st){
    case FG_ST_OK:   return TH_OK;
    case FG_ST_BUSY: return TH_WARN;
    case FG_ST_BAD:  return TH_ERR;
    default:         return TH_DIS;
  }
}

// El glifo del estado. Se dibuja, no es un caracter de fuente: asi
// escala limpio y no depende de que la tipografia tenga el simbolo.
static void fgStGlyph(int cx, int cy, int r, uint8_t st){
  const uint16_t c = fgStColor(st);
  switch(st){
    case FG_ST_OK:
      fillCircle(cx, cy, r, c);
      break;
    case FG_ST_BUSY:
      fillCircle(cx, cy, r, c);
      fillCircle(cx, cy, r - 3, TH_PAGE);
      break;
    case FG_ST_BAD:
      strokeSeg(cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1, 3, c);
      strokeSeg(cx + r - 1, cy - r + 1, cx - r + 1, cy + r - 1, 3, c);
      break;
    default:
      fillRoundRect(cx - r, cy - 2, r * 2, 4, 2, c);
      break;
  }
}

// Nombre canonico de cada estado. UNA sola forma de decir cada cosa
// en todo Flex Phone: mezclar "sin conexion", "desconectado" y "no
// hay telefono" para el mismo estado confunde mas que ayuda.
static const char* fgStName(uint8_t st){
  const bool en = (LI() == 1);
  switch(st){
    case FG_ST_OK:   return en ? "Connected"    : "Conectado";
    case FG_ST_BUSY: return en ? "Connecting"   : "Conectando";
    case FG_ST_BAD:  return en ? "Disconnected" : "Desconectado";
    default:         return en ? "Unavailable"  : "No disponible";
  }
}

// =============================================================
//  2) ZONAS TACTILES
// =============================================================
// Cada componente pulsable registra su rectangulo REAL al pintarse.
// El manejador de toques pregunta por identificador, nunca por
// coordenadas escritas a mano.
#define FG_HITS_MAX 28
typedef struct { int16_t x, y, w, h; uint16_t id; } FgHit;
static FgHit  fgHits[FG_HITS_MAX];
static int    fgHitN = 0;

static void fgHitsReset(){ fgHitN = 0; }
static void fgHitAdd(int x, int y, int w, int h, uint16_t id){
  if(fgHitN >= FG_HITS_MAX) return;      // se descarta, no se desborda
  fgHits[fgHitN].x = (int16_t)x; fgHits[fgHitN].y = (int16_t)y;
  fgHits[fgHitN].w = (int16_t)w; fgHits[fgHitN].h = (int16_t)h;
  fgHits[fgHitN].id = id;
  fgHitN++;
}
// Devuelve el id pulsado, o 0 (que nunca se usa como id valido).
static uint16_t fgHitAt(int px, int py){
  // Al reves: lo ultimo pintado esta ENCIMA, asi que gana.
  for(int i = fgHitN - 1; i >= 0; i--){
    const FgHit* h = &fgHits[i];
    if(px >= h->x && px < h->x + h->w && py >= h->y && py < h->y + h->h) return h->id;
  }
  return 0;
}

// =============================================================
//  3) DESPLAZAMIENTO
// =============================================================
// Una sola pieza de scroll para todas las pantallas. Guarda el alto
// del contenido MIENTRAS se pinta, asi que el tope siempre
// corresponde a lo que hay de verdad en la pantalla actual.
typedef struct {
  int  off;          // desplazamiento actual (px, >= 0)
  int  content;      // alto del contenido de la ultima pasada
  int  viewTop;      // primera fila visible del area desplazable
  int  viewBot;      // ultima + 1
} FgScroll;

static void fgScrollReset(FgScroll* s, int top, int bot){
  if(!s) return;
  s->off = 0; s->content = 0; s->viewTop = top; s->viewBot = bot;
}
static void fgScrollSetView(FgScroll* s, int top, int bot){
  if(!s) return;
  s->viewTop = top; s->viewBot = bot;
}
// Limita el desplazamiento a lo que hay. Devuelve true si cambio:
// asi el llamador solo repinta cuando el scroll se movio de verdad.
static bool fgScrollClamp(FgScroll* s){
  if(!s) return false;
  const int view = s->viewBot - s->viewTop;
  int max = s->content - view;
  if(max < 0) max = 0;
  const int was = s->off;
  if(s->off > max) s->off = max;
  if(s->off < 0)   s->off = 0;
  return s->off != was;
}
static bool fgScrollBy(FgScroll* s, int dy){
  if(!s) return false;
  const int was = s->off;
  s->off += dy;
  fgScrollClamp(s);
  return s->off != was;
}
static bool fgScrollNeeded(const FgScroll* s){
  return s && s->content > (s->viewBot - s->viewTop);
}
// Barra lateral fina. Solo aparece si de verdad hay mas contenido:
// una barra permanente sobre una lista corta es ruido.
static void fgScrollBar(const FgScroll* s){
  if(!fgScrollNeeded(s)) return;
  const int view = s->viewBot - s->viewTop;
  int h = view * view / s->content;
  if(h < 28) h = 28;
  const int range = view - h;
  const int maxOff = s->content - view;
  const int y = s->viewTop + (maxOff > 0 ? (s->off * range / maxOff) : 0);
  fillRoundRect(SCR_W - 9, y, 4, h, 2, TH_TRACK);
}

// =============================================================
//  4) TARJETAS Y PANELES
// =============================================================
static uint16_t fgTint(){ return uiGlass ? TH_GLASS : TH_SURF; }

// La tarjeta base de todo Flex Phone.
static void fgCard(int x, int y, int w, int h, int rad = 16){
  if(h <= 0 || w <= 0) return;
  drawGlassCardFlat(x, y, w, h, rad, fgTint(), TH_PAGE);
}

// Tarjeta con borde de acento a la izquierda. Se usa para destacar
// sin cambiar el fondo: cambiar el fondo de una tarjeta rompe el
// cacheado del vidrio y obliga a rehornear una tarjeta por color.
static void fgCardAccent(int x, int y, int w, int h, uint16_t accent, int rad = 16){
  fgCard(x, y, w, h, rad);
  fillRoundRect(x + 3, y + 10, 4, h - 20, 2, accent);
}

// =============================================================
//  5) TEXTO QUE NO SE SALE
// =============================================================
// Recorta con puntos suspensivos en frontera de caracter UTF-8. Los
// nombres de aplicacion y los titulos de notificacion vienen del
// telefono y pueden ser larguisimos; sin esto se salen de la
// tarjeta y se pintan encima de lo siguiente.
static void fgTextEllipsis(int x, int y, int maxW, const char* s, int size, uint16_t col){
  if(!s || !*s) return;
  if(textW(s, size) <= maxW){ drawText(x, y, s, size, col); return; }
  char buf[96];
  const int dotsW = textW("...", size);
  size_t n = strlen(s);
  if(n > sizeof(buf) - 4) n = sizeof(buf) - 4;
  memcpy(buf, s, n);
  buf[n] = 0;
  while(n > 0){
    // Retroceder hasta el inicio de un caracter: cortar un UTF-8 por
    // la mitad pinta un glifo roto, no un texto recortado.
    do { n--; } while(n > 0 && ((uint8_t)buf[n] & 0xC0) == 0x80);
    buf[n] = 0;
    if(textW(buf, size) + dotsW <= maxW) break;
  }
  strcat(buf, "...");
  drawText(x, y, buf, size, col);
}

// Parrafo con ajuste por palabras. Devuelve el alto usado, para que
// quien lo llama pueda maquetar lo siguiente sin adivinar.
static int fgParagraph(int x, int y, int w, const char* s, int size,
                       uint16_t col, int maxLines = 6){
  if(!s || !*s) return 0;
  const int lh = (size >= 2) ? 22 : 16;
  int ty = y, lines = 0;
  size_t at = 0;
  const size_t len = strlen(s);
  char line[80];
  while(at < len && lines < maxLines){
    size_t take = 0, lastSpace = 0;
    while(at + take < len && take < sizeof(line) - 1){
      line[take] = s[at + take];
      line[take + 1] = 0;
      if(textW(line, size) > w){ break; }
      if(s[at + take] == ' ') lastSpace = take;
      take++;
    }
    if(at + take < len && lastSpace > 0) take = lastSpace;
    if(take == 0) take = 1;
    memcpy(line, s + at, take);
    line[take] = 0;
    drawText(x, ty, line, size, col);
    ty += lh;
    lines++;
    at += take;
    while(at < len && s[at] == ' ') at++;
  }
  return ty - y;
}

// =============================================================
//  6) PILDORA DE ESTADO
// =============================================================
// Glifo + texto, con un fondo suave. Devuelve el ancho ocupado.
static int fgStatusChip(int x, int y, uint8_t st, const char* label){
  const char* t = label ? label : fgStName(st);
  const int w = 26 + textW(t, 1) + 12;
  fillRoundRect(x, y, w, 24, 12, TH_SURF2);
  fgStGlyph(x + 13, y + 12, 6, st);
  drawText(x + 26, y + 5, t, 1, TH_TXT2);
  return w;
}

// =============================================================
//  7) BOTONES
// =============================================================
enum { FG_BTN_PLAIN = 0, FG_BTN_PRIMARY, FG_BTN_DANGER, FG_BTN_DISABLED };

// Botón de ancho completo. Registra su zona tactil AL PINTARSE.
// Un boton FG_BTN_DISABLED se pinta apagado y NO registra zona: no
// puede haber un boton que parezca pulsable y no haga nada.
static void fgButton(int x, int y, int w, int h, const char* label,
                     uint8_t style, uint16_t id){
  uint16_t bg = TH_SURF2, fg = TH_TXT;
  switch(style){
    case FG_BTN_PRIMARY:  bg = TH_PRIM;   fg = TH_ONACC; break;
    case FG_BTN_DANGER:   bg = TH_SURF2;  fg = TH_DANGER; break;
    case FG_BTN_DISABLED: bg = TH_TRACK;  fg = TH_DIS;   break;
    default: break;
  }
  fillRoundRect(x, y, w, h, h / 2 > 16 ? 16 : h / 2, bg);
  drawTextC(x + w / 2, y + (h - 16) / 2, label, 1, fg);
  if(style != FG_BTN_DISABLED) fgHitAdd(x, y, w, h, id);
}

// =============================================================
//  8) FILAS
// =============================================================
// Fila etiqueta / valor. El valor se recorta por la derecha para que
// una etiqueta larga nunca se coma el dato.
static void fgRow(int x, int y, int w, const char* label, const char* value){
  drawText(x, y, label, 1, TH_MUTE);
  if(!value || !*value) return;
  const int vw = textW(value, 1);
  const int maxV = w / 2;
  if(vw <= maxV) drawTextR(x + w, y, value, 1, TH_TXT);
  else {
    // No cabe: se recorta desde la izquierda del valor.
    char tmp[64];
    flexLinkUtf8Copy(tmp, sizeof(tmp), value);
    fgTextEllipsis(x + w - maxV, y, maxV, tmp, 1, TH_TXT);
  }
}

// Fila con glifo de estado a la izquierda. La usa el Diagnostico.
static void fgRowState(int x, int y, int w, const char* label,
                       uint8_t st, const char* detail){
  fgStGlyph(x + 8, y + 8, 6, st);
  drawText(x + 24, y, label, 1, TH_TXT);
  if(detail && *detail) fgTextEllipsis(x + w / 2, y, w / 2, detail, 1, TH_MUTE);
}

// Fila de navegacion: titulo, subtitulo y chevron. Registra su zona.
static void fgNavRow(int x, int y, int w, int h, const char* title,
                     const char* sub, uint16_t id, bool enabled){
  fgCard(x, y, w, h);
  const int tw = w - 60;
  fgTextEllipsis(x + 18, y + (sub && *sub ? 12 : (h - 18) / 2), tw, title, 2,
                 enabled ? TH_TXT : TH_DIS);
  if(sub && *sub) fgTextEllipsis(x + 18, y + 38, tw, sub, 1, TH_MUTE);
  // Chevron.
  const uint16_t cc = enabled ? TH_MUTE : TH_DIS;
  strokeSeg(x + w - 30, y + h / 2 - 7, x + w - 22, y + h / 2, 3, cc);
  strokeSeg(x + w - 22, y + h / 2, x + w - 30, y + h / 2 + 7, 3, cc);
  if(enabled) fgHitAdd(x, y, w, h, id);
}

// =============================================================
//  9) CABECERA DE SECCION
// =============================================================
static void fgSectionHeader(int x, int y, const char* text){
  drawText(x, y, text, 1, TH_MUTE);
}

// =============================================================
//  10) ESTADO VACIO
// =============================================================
// Toda lista que puede estar vacia tiene que decir POR QUE lo esta.
// Una pantalla en blanco parece un fallo.
static void fgEmpty(int y, const char* title, const char* why){
  fillCircle(SCR_W / 2, y + 30, 26, TH_SURF2);
  fgStGlyph(SCR_W / 2, y + 30, 10, FG_ST_OFF);
  drawTextC(SCR_W / 2, y + 70, title, 2, TH_TXT2);
  if(why && *why){
    const int w = SCR_W - 96;
    // Centrado a ojo: el parrafo se alinea a la izquierda dentro de
    // una caja centrada, que se lee mejor que centrar cada linea.
    fgParagraph(48, y + 100, w, why, 1, TH_MUTE, 3);
  }
}

// =============================================================
//  11) BARRA DE MEDIDA
// =============================================================
// Para bateria, almacenamiento y memoria. `pct` fuera de 0..100 se
// pinta como "sin dato" en vez de recortarse en silencio.
static void fgMeter(int x, int y, int w, int pct, uint16_t col){
  fillRoundRect(x, y, w, 8, 4, TH_TRACK);
  if(pct < 0 || pct > 100) return;
  const int fw = w * pct / 100;
  if(fw > 0) fillRoundRect(x, y, fw < 8 ? 8 : fw, 8, 4, col);
}

// =============================================================
//  12) AVISO
// =============================================================
// Tarjeta de aviso con titulo y explicacion. Devuelve el alto que ha
// ocupado para que la pantalla siga maquetando debajo.
static int fgNotice(int x, int y, int w, const char* title, const char* body,
                    uint16_t accent){
  const int bodyH = body && *body ? 0 : 0;
  (void)bodyH;
  // Se mide primero para no pintar una tarjeta mas alta que su texto.
  int h = 20 + (title && *title ? 24 : 0);
  if(body && *body){
    // Alto estimado por lineas de 16 px; fgParagraph respeta el tope.
    const int lines = (textW(body, 1) / (w - 36)) + 1;
    h += (lines > 5 ? 5 : lines) * 16 + 6;
  }
  if(h < 56) h = 56;
  fgCardAccent(x, y, w, h, accent);
  int ty = y + 12;
  if(title && *title){ drawText(x + 18, ty, title, 2, accent); ty += 26; }
  if(body && *body)  fgParagraph(x + 18, ty, w - 36, body, 1, TH_TXT2, 5);
  return h;
}
