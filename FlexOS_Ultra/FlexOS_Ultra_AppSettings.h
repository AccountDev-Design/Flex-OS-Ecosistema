// #############################################################
// ##  FLEX OS ULTRA  ·  APP AJUSTES
// ##  ----------------------------------------------------------
// ##  Navegacion por categorias, todas las pantallas de configuracion y su
// ##  ciclo de vida (que categoria y que posiciones se conservan).
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
#include "FlexOS_Ultra_Core.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP AJUSTES  ·  navegacion de telefono moderno
// ##  ------------------------------------------------------
// ##  Antes eran DOS paneles a la vez: barra lateral de categorias a la
// ##  izquierda y detalle a la derecha, con el detalle encajado en 232 px de
// ##  ancho. Ahora funciona como en Android/One UI/iOS:
// ##    · la pantalla principal es SOLO la lista de categorias, a todo lo ancho;
// ##    · tocar una categoria ABRE su propia pantalla, dedicada, tambien a todo
// ##      lo ancho (mas sitio para cada fila y para su valor);
// ##    · la transicion es un empuje horizontal con paralaje (la pantalla que
// ##      sale se va mas despacio que la que entra), interpolado por TIEMPO;
// ##    · para volver: el chevron de la cabecera, el boton "atras" de la barra
// ##      de navegacion o un arrastre desde el borde izquierdo.
// ##  COMPATIBILIDAD CON DeX: la app sigue siendo la MISMA -- mismo enter(),
// ##  mismo tick(), mismo lienzo 480x800, y la navegacion vive en una variable
// ##  propia (setView), no en gState. Por eso dentro de una ventana de Modo PC
// ##  se comporta exactamente igual que a pantalla completa. Lo unico que se
// ##  omite hospedada es la ANIMACION (un tick hospedado solo produce el ultimo
// ##  cuadro: animar ahi seria trabajo tirado), que es justo lo que hacen ya
// ##  las demas animaciones bloqueantes del sistema.
// #############################################################
#define SET_CARD_X   14
#define SET_CARD_W   (SCR_W - 28)           // tarjeta a todo el ancho (452)
#define DP_X         SET_CARD_X             // el contenido de detalle usa DP_X/DP_W
#define DP_W         SET_CARD_W
#define SET_LIST_TOP 104                    // primera tarjeta de la lista de categorias
#define SET_LIST_BOT (SCR_H - 70)
#define DLIST_TOP    128                    // primera fila de la pantalla de categoria
#define DLIST_BOT    (SCR_H - 70)           // 730
#define SET_NAV_MS   270                    // duracion de la transicion entre pantallas
#define SET_ANIM_Y0  40                     // banda que se desplaza (la barra de estado
#define SET_ANIM_Y1  (SCR_H - 60)           //  y la de navegacion se quedan quietas)
// Los nombres PAGE_BG / SET_* se CONSERVAN (los usan decenas de llamantes y
// tambien las pantallas de clave), pero ya no son una segunda paleta: son
// alias del TEMA SEMANTICO GLOBAL, o sea de la misma fuente de verdad que usa
// el resto del sistema. Colores de MARCA/acento (los puntitos de colores de
// cada fila) siguen siendo fijos a proposito -- es el fondo y el texto lo que
// define si algo "se ve" claro u oscuro.
#define PAGE_BG        TH_PAGE      // fondo de pagina
#define SET_CARD_BG    TH_SURF      // fondo de tarjeta (fila/sidebar)
#define SET_CARD_GLASS TH_GLASS     // tinte de vidrio de la tarjeta
#define SET_TXT_HI     TH_TXT       // texto principal
#define SET_TXT_LO     TH_TXT2      // texto secundario / valor
#define SET_TXT_MUTE   TH_MUTE      // texto de ayuda / pie
#define SET_CHEV       TH_MUTE      // chevron
#define SET_SIDE_SUB   TH_TXT2      // subtitulo de la barra lateral
#define SET_NAVPILL    TH_NAV       // pildora de gestos (modo iOS)

static const char* SET_CAT[12] = {
  "General","Pantalla","Sonido","Red e Internet","Dispositivos",
  "Personalizaci\xC3\xB3n","Seguridad y privacidad","Bater\xC3\xAD" "a","Almacenamiento",
  "Desarrollador","Sistema","Acerca de" };
static const char* SET_SUB[12] = {
  "Idioma, fecha, hora","Brillo, fondo, tema","Volumen, tonos","WiFi, Bluetooth",
  "GPIO, perifericos","Temas, iconos","Bloqueo, Flex Vault, permisos","Ahorro de energia",
  "Interna, SD","Opciones dev","Sistema, logs","Version, creditos" };
static const char* SET_DESC[12] = {
  "Configura las opciones basicas del sistema.","Brillo, fondo de pantalla y modo oscuro.",
  "Volumen, tonos y notificaciones.","Conexiones de red (offline por ahora).",
  "GPIO, modulos y perifericos.","Temas, iconos y estilo del sistema.",
  "Bloqueo, Carpeta segura, permisos y privacidad.","Estado de la bateria y ahorro de energia.",
  "Memoria interna y tarjeta SD.","Herramientas y diagnostico de desarrollo.",
  "Informacion del sistema y registros.","Version, hardware y creditos de FlexOS." };

// setView es TODA la maquina de navegacion de la app: 0 = lista de categorias,
// 1 = pantalla de una categoria. A proposito NO es un gState nuevo -- asi la
// navegacion interna sobrevive intacta dentro de una ventana de Modo PC, donde
// dexHostRun descarta cualquier cambio de gState que haga la app hospedada.
static int setView = 0;
static int setSel = 0, setScroll = 0, setContentH = 0;
static int setListScroll = 0, setListH = 0;    // scroll propio de la lista de categorias
static int  setDragY0 = 0, setDragS0 = 0;      // arrastre de la lista activa
static bool setDragging = false;
static bool setBackSwipe = false;              // arrastre desde el borde izquierdo (volver)

// Texto recortado por la derecha (evita que se salga del panel)
static int drawTextClip(int x, int y, const char* s, int size, uint16_t col, int maxRight){
  if(size <= 1){
    while(*s){
      if(x + 6 > maxRight) break;
      uint8_t b = (uint8_t)*s++; uint32_t cp;
      if(b < 0x80) cp = b;
      else if((b & 0xE0) == 0xC0){ uint8_t b1 = *s ? (uint8_t)*s++ : 0; cp = ((b & 0x1F) << 6) | (b1 & 0x3F); }
      else if((b & 0xF0) == 0xE0){ if(*s) s++; if(*s) s++; cp = 0x3F; }
      else cp = 0x3F;
      uint8_t base, acc; mapCP(cp, base, acc);
      drawGlyphSmooth(x, y, base, 1, col, 255);
      if(acc) drawAccent(x, y, 1, acc, col);
      x += 6;
    }
    return x;
  }
  float sc = fontSc(size), penx = x;
  while(*s){
    uint32_t cp = nextCP(&s);
    const FGlyph* g = &FG[fontIdx(cp)];
    if(penx + g->adv * sc > maxRight) break;
    drawGlyphScaled((int)(penx + g->bx * sc + 0.5f), y + (int)((g->topoff - FONT_CAPOFF) * sc + 0.5f), g, sc, col, 255);
    penx += g->adv * sc;
  }
  return (int)(penx + 0.5f);
}

static const char* langNameCur(){ return (cfgLang == 5) ? "Chinese" : LANG_ENDONYM[cfgLang]; }
static void settingsDateTimeStr(char* out, size_t n){
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%02d/%02d/%04d  %d:%02d %s", rtcD, rtcMo, rtcY, h12, rtcMin, rtcH < 12 ? "AM" : "PM");
}
static void buildUptime(char* out, size_t n){
  unsigned long s = millis() / 1000UL;
  unsigned long d = s / 86400; s %= 86400;
  unsigned long h = s / 3600;  s %= 3600;
  unsigned long m = s / 60;
  if(d > 0)      snprintf(out, n, "%lud %luh %lum", d, h, m);
  else if(h > 0) snprintf(out, n, "%luh %lum", h, m);
  else           snprintf(out, n, "%lum", m);
}

// ---- iconos de fila (panel General) ----
enum { RI_GLOBE, RI_CAL, RI_CLOCK, RI_PIN, RI_REFRESH, RI_CLOUD, RI_RESET, RI_DOT };
static void drawRowGlyph(int k, int cx, int cy, uint16_t col){
  switch(k){
    case RI_GLOBE:
      drawCircle(cx, cy, 10, col); vLine(cx, cy - 10, 21, col); hLine(cx - 10, cy, 21, col);
      arcStroke(cx, cy, 5, 90, 270, 1, col); arcStroke(cx, cy, 5, -90, 90, 1, col); break;
    case RI_CAL:
      drawRoundRect(cx - 9, cy - 8, 18, 17, 3, col); fillRect(cx - 9, cy - 8, 18, 5, col);
      fillCircle(cx - 4, cy + 2, 1, col); fillCircle(cx + 3, cy + 2, 1, col); break;
    case RI_CLOCK:
      drawCircle(cx, cy, 10, col); strokeSegAA(cx, cy, cx, cy - 6, 1.4f, col);
      strokeSegAA(cx, cy, cx + 4, cy, 1.4f, col); break;
    case RI_PIN:
      fillCircle(cx, cy - 3, 7, col); fillTriangle(cx - 6, cy, cx + 6, cy, cx, cy + 9, col);
      fillCircle(cx, cy - 3, 3, thCard()); break;      // hueco del pin: color de la TARJETA, no blanco fijo
    case RI_REFRESH:
      arcStroke(cx, cy, 9, 30, 300, 2, col);
      fillTriangle(cx + 8, cy - 7, cx + 14, cy - 4, cx + 7, cy - 1, col); break;
    case RI_CLOUD:
      fillCircle(cx - 4, cy + 2, 5, col); fillCircle(cx + 4, cy + 2, 6, col);
      fillCircle(cx, cy - 2, 6, col); fillRect(cx - 8, cy + 2, 16, 5, col); break;
    case RI_RESET:
      arcStroke(cx, cy, 9, 40, 320, 2, col);
      fillTriangle(cx + 6, cy - 8, cx + 12, cy - 9, cx + 8, cy - 2, col); break;
    default: fillCircle(cx, cy, 4, col); break;
  }
}

// ---- iconos de categoria (barra lateral) ----
static void drawSetCatIcon(int cat, int x, int y, int S, uint16_t col){
  int cx = x + S / 2, cy = y + S / 2;
  switch(cat){
    case 0: // engranaje
      fillCircleAA(cx, cy, S * 0.18f, col);
      for(int k = 0; k < 8; k++){ float a = k * 0.7853982f;
        fillCircleAA(cx + cosf(a) * S * 0.30f, cy + sinf(a) * S * 0.30f, S * 0.065f, col); }
      fillCircleAA(cx, cy, S * 0.08f, thCard()); break; // hueco del engranaje: color de la TARJETA
    case 1: // sol
      fillCircleAA(cx, cy, S * 0.15f, col);
      for(int k = 0; k < 8; k++){ float a = k * 0.7853982f;
        strokeSegAA(cx + cosf(a) * S * 0.24f, cy + sinf(a) * S * 0.24f,
                    cx + cosf(a) * S * 0.34f, cy + sinf(a) * S * 0.34f, 1.4f, col); } break;
    case 2: // altavoz
      fillRect((int)(cx - S * 0.22f), (int)(cy - S * 0.06f), (int)(S * 0.10f), (int)(S * 0.12f), col);
      fillTriangle((int)(cx - S * 0.12f), (int)(cy - S * 0.14f), (int)(cx - S * 0.12f), (int)(cy + S * 0.14f), (int)(cx + S * 0.02f), cy, col);
      arcStroke(cx - S * 0.02f, cy, S * 0.14f, -55, 55, 2, col); break;
    case 3: drawWifi(cx, (int)(cy + S * 0.14f), (int)(S * 0.28f), col); break;
    case 4: // cubo
      fillQuad(cx, (int)(cy - S * 0.22f), (int)(cx + S * 0.20f), (int)(cy - S * 0.10f), cx, (int)(cy + S * 0.02f), (int)(cx - S * 0.20f), (int)(cy - S * 0.10f), col);
      fillQuad(cx, (int)(cy + S * 0.02f), (int)(cx + S * 0.20f), (int)(cy - S * 0.10f), (int)(cx + S * 0.20f), (int)(cy + S * 0.14f), cx, (int)(cy + S * 0.26f), mix565(col, rgb565(0,0,0), 70));
      fillQuad(cx, (int)(cy + S * 0.02f), (int)(cx - S * 0.20f), (int)(cy - S * 0.10f), (int)(cx - S * 0.20f), (int)(cy + S * 0.14f), cx, (int)(cy + S * 0.26f), mix565(col, rgb565(0,0,0), 120)); break;
    case 5: // pincel
      strokeSegAA(cx - S * 0.16f, cy + S * 0.18f, cx + S * 0.10f, cy - S * 0.16f, 2.4f, col);
      fillCircleAA(cx - S * 0.18f, cy + S * 0.20f, S * 0.08f, col); break;
    case 6: // candado
      fillRoundRect((int)(cx - S * 0.16f), (int)(cy - S * 0.02f), (int)(S * 0.32f), (int)(S * 0.24f), 3, col);
      arcStroke(cx, cy - S * 0.02f, S * 0.12f, 180, 360, 2, col); break;
    case 7: drawBattery((int)(x + S * 0.24f), (int)(y + S * 0.34f), (int)(S * 0.5f), (int)(S * 0.3f), 80, col); break;
    case 8: // discos apilados
      for(int i = 0; i < 3; i++)
        fillRoundRect((int)(cx - S * 0.22f), (int)(cy - S * 0.16f + i * S * 0.14f), (int)(S * 0.44f), (int)(S * 0.09f), 2, col); break;
    case 9: // </>
      strokeSegAA(cx - S * 0.05f, cy - S * 0.14f, cx - S * 0.20f, cy, 2.0f, col);
      strokeSegAA(cx - S * 0.20f, cy, cx - S * 0.05f, cy + S * 0.14f, 2.0f, col);
      strokeSegAA(cx + S * 0.05f, cy - S * 0.14f, cx + S * 0.20f, cy, 2.0f, col);
      strokeSegAA(cx + S * 0.20f, cy, cx + S * 0.05f, cy + S * 0.14f, 2.0f, col); break;
    default: // info (i)
      drawCircle(cx, cy, (int)(S * 0.30f), col); drawCircle(cx, cy, (int)(S * 0.30f) - 1, col);
      fillCircle(cx, (int)(cy - S * 0.13f), 2, col);
      fillRect(cx - 1, (int)(cy - S * 0.03f), 3, (int)(S * 0.18f), col); break;
  }
}

// Registro de las filas realmente dibujadas en el panel de detalle de
// Ajustes (se resetea en settingsDetailContent() y lo llena cada
// setRowCard()). El tap-handler de settingsTick() lo consulta en vez de
// asumir que todas las filas miden 60px exactos y estan pegadas -- ese
// supuesto se rompia en cuanto habia un titulo de seccion o un texto de
// ayuda entre filas (bug: Pantalla->Bloqueo detectaba la fila equivocada).
#define SET_ROW_MAX 16
static int setRowY0[SET_ROW_MAX], setRowY1[SET_ROW_MAX], setRowN = 0;
// Tarjeta de fila (icono + titulo + valor + chevron). Devuelve la y siguiente.
// EL VIDRIO YA NO SE APAGA AL ARRASTRAR. Antes, mientras el dedo movia la lista,
// las tarjetas se pintaban planas porque drawLiquidGlassPanel (copiar + desenfocar
// + mezclar, por tarjeta y por cuadro) no daba para seguir al dedo: por eso "al
// hacer scroll se perdia el desenfoque y las transparencias". drawGlassCardFlat
// resuelve la tarjeta UNA vez y luego la vuelca por filas, asi que el material se
// mantiene durante todo el desplazamiento y el cuadro sigue siendo barato. Si el
// usuario tiene el Liquid Glass desactivado, la rama de abajo respeta su ajuste.
static int setRowCard(int y, int rIcon, uint16_t iCol, const char* title, const char* val, bool chevron){
  int rh = 64, mr = DP_X + DP_W - 30;
  if(setRowN < SET_ROW_MAX){ setRowY0[setRowN] = y; setRowY1[setRowN] = y + rh; setRowN++; }  // registra el rango real de esta fila
  if(uiGlass) drawGlassCardFlat(DP_X, y, DP_W, rh - 8, 14, SET_CARD_GLASS, PAGE_BG);          // tarjeta vidrio (cacheada)
  else fillRoundRect(DP_X, y, DP_W, rh - 8, 14, SET_CARD_BG);
  drawRowGlyph(rIcon, DP_X + 26, y + (rh - 8) / 2, iCol);
  drawTextClip(DP_X + 52, y + 10, title, 2, SET_TXT_HI, mr);
  if(val) drawTextClip(DP_X + 52, y + 34, val, 1, SET_TXT_LO, mr);
  if(chevron){ int chx = DP_X + DP_W - 20, chy = y + (rh - 8) / 2;
    strokeSegAA(chx - 3, chy - 6, chx + 3, chy, 2.0f, SET_CHEV);
    strokeSegAA(chx + 3, chy, chx - 3, chy + 6, 2.0f, SET_CHEV); }
  return y + rh;
}
static int drawInfoLine(int y, const char* label, const char* val){
  drawText(DP_X, y, label, 1, SET_TXT_LO);
  drawTextR(SCR_W - 12, y, val, 1, SET_TXT_HI);
  return y + 24;
}
static int drawDeviceInfo(int y){
  drawText(DP_X, y, "Dispositivo", 2, SET_TXT_HI); y += 30;
  char v[48];
  y = drawInfoLine(y, "Nombre", cfgName);
  y = drawInfoLine(y, "Modelo", "ESP32-P4 DevKit");
  y = drawInfoLine(y, "Version", "FlexOS Ultra 1.0");
  buildUptime(v, sizeof(v)); y = drawInfoLine(y, "Actividad", v);
  snprintf(v, sizeof(v), "%u KB libre", (unsigned)(esp_get_free_heap_size() / 1024)); y = drawInfoLine(y, "RAM", v);
  snprintf(v, sizeof(v), "%u / %u MB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576),
           (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1048576)); y = drawInfoLine(y, "PSRAM", v);
  y = drawInfoLine(y, "Flash", "16 MB");
  y = drawInfoLine(y, "CPU", "RISC-V dual 360 MHz");
  return y;
}

// Contenido del panel de detalle (con clip vertical activo)
static void settingsDetailContent(int cat){
  setRowN = 0;                       // reinicia el registro de filas (ver setRowCard)
  int base = DLIST_TOP - setScroll;
  int y = base + 6;
  char v[48];
  // Los circulitos de color de cada fila son ACENTOS DE MARCA por categoria (el
  // patron de One UI / Ajustes de iOS): identifican la fila y son iguales en las
  // dos apariencias. Lo que define si algo "se ve" claro u oscuro es la tarjeta y
  // el texto, y esos si salen del tema.
  uint16_t ic = rgb565(70,120,225);
  if(cat == 0){
    y = setRowCard(y, RI_GLOBE,   rgb565(60,140,235), "Idioma", langNameCur(), true);
    settingsDateTimeStr(v, sizeof(v));
    y = setRowCard(y, RI_CAL,     rgb565(235,90,90),  "Fecha y hora", v, true);
    y = setRowCard(y, RI_CLOCK,   rgb565(90,120,230), "Formato de hora", g24h ? "24 horas" : "12 horas", true);
    y = setRowCard(y, RI_PIN,     rgb565(230,80,80),  "Zona horaria", "GMT-05:00 Lima (sin DST)", false);
    // --- HORA REAL POR NTP (filas 4, 5 y 6) ---
    // La fila de estado y la de "ultima sincronizacion" son informativas (sin
    // chevron): tocarlas no hace nada, igual que las de "Acerca de". La
    // accionable es la tercera.
    char nv[64]; ntpStateText(nv, sizeof(nv));
    y = setRowCard(y, RI_CLOUD,   gTimeNvsOk ? rgb565(60,170,220) : rgb565(220,80,80),
                   "Sincronizaci\xC3\xB3n autom\xC3\xA1tica", nv, false);
    char lv[48]; ntpLastSyncText(lv, sizeof(lv));
    y = setRowCard(y, RI_CLOCK,   rgb565(90,120,230), "\xC3\x9Altima sincronizaci\xC3\xB3n", lv, false);
    y = setRowCard(y, RI_REFRESH, rgb565(80,180,120), "Sincronizar ahora",
                   ntpIsBusy() ? "Sincronizando..." : "Consultar el servidor de hora", true);
    y = setRowCard(y, RI_REFRESH, rgb565(60,160,230), "Actualizaciones", flexOtaStatusText(), true);
    y = setRowCard(y, RI_CLOUD,   rgb565(120,160,230),"Copias de seguridad", "Proximamente", true);
    y = setRowCard(y, RI_RESET,   rgb565(220,80,80),  "Restablecer", "Opciones de fabrica", true);
    // FLEX ACCOUNT. Va AL FINAL de la categoria a proposito: setRowCard numera
    // las filas por orden de llamada, asi que anadirla aqui deja intactos los
    // indices que ya usa settingsRowAction (idioma=0, formato=2, NTP=6, OTA=7).
    // Es la via de vuelta para quien omitio la vinculacion en el primer
    // arranque; el subtitulo dice el estado real, nunca uno inventado.
    y += 8; drawText(DP_X, y, "Cuenta", 2, SET_TXT_HI); y += 30;
    { char av[64]; accountSettingsText(av, sizeof(av));
      y = setRowCard(y, RI_DOT, rgb565(105,91,230), "Flex Account", av, true); }
    y += 12; y = drawDeviceInfo(y);
  } else if(cat == 11){
    y = drawDeviceInfo(y);
    y += 8; drawText(DP_X, y, "FlexOS Ultra - desde cero", 1, SET_TXT_MUTE); y += 22;
    drawText(DP_X, y, "para ESP32-P4 - 2026", 1, SET_TXT_MUTE); y += 22;
  } else if(cat == 1){                     // Pantalla (funcional)
    char bv[16]; snprintf(bv, sizeof(bv), "%d%%", gBright);
    y = setRowCard(y, RI_DOT, rgb565(240,170,50), "Brillo", bv, true);
    y = setRowCard(y, RI_DOT, rgb565(90,110,235), "Estilo", uiGlass ? "Liquid Glass" : "Plano", true);
    y = setRowCard(y, RI_DOT, gDark ? rgb565(120,130,220) : rgb565(240,170,50), "Modo de apariencia", gDark ? "Oscuro" : "Claro", true);
    y = setRowCard(y, RI_DOT, rgb565(100,180,240), "Barra de navegacion", gNavMode == 0 ? "Botones" : "Gestos iOS", true);
    y += 8; drawText(DP_X, y, "Toca una fila para cambiarla", 1, SET_TXT_MUTE); y += 24;
    y += 8; drawText(DP_X, y, "Bloqueo", 2, SET_TXT_HI); y += 30;
    y = setRowCard(y, RI_CLOCK, rgb565(90,120,230), "Reloj grande", (gLockWidgets & LW_CLOCK) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_CLOUD, rgb565(90,170,235), "Clima", (gLockWidgets & LW_WEATHER) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_CAL, rgb565(235,110,90), "Calendario", (gLockWidgets & LW_CAL) ? "Activado" : "Desactivado", true);
    y = setRowCard(y, RI_DOT, rgb565(230,180,90), "Notificaciones", (gLockWidgets & LW_NOTIF) ? "Activado" : "Desactivado", true);
    y += 8; drawText(DP_X, y, "Elige los widgets de la pantalla de bloqueo", 1, SET_TXT_MUTE); y += 24;
  } else if(cat == 5){                      // Personalizacion (funcional)
    y = setRowCard(y, RI_DOT, rgb565(90,110,235), "Personalizar UI", uiGlass ? "Liquid Glass" : "Plano", true);
    y = setRowCard(y, RI_DOT, rgb565(150,90,210), "Iconos", gIconStyle == 1 ? "Vidrio" : "Plano", true);
    const char* av = gAnimStyle == 1 ? "Fundido" : gAnimStyle == 2 ? "Deslizar" : "Zoom";
    y = setRowCard(y, RI_DOT, rgb565(90,200,160), "Transiciones", av, true);
#if KB_SETTINGS_ON
    // FASE E: puerta de entrada a los Ajustes del teclado (la otra es el
    // engranaje de la barra superior del propio teclado).
    y = setRowCard(y, RI_DOT, rgb565(235,150,60), "Teclado",
                   gKbSize == KB_SIZE_COMPACT ? "Compacto" : gKbSize == KB_SIZE_BIG ? "Grande" : "Normal", true);
#endif
    y += 8; drawText(DP_X, y, "Toca una fila para cambiar su estilo", 1, SET_TXT_MUTE); y += 24;
  } else if(cat == 3){                      // Red e Internet (datos REALES)
    char rv0[64]; connWifiSub(rv0, sizeof(rv0));
    // Se quitan los parentesis del subtitulo de la pantalla de Conectividad:
    // aqui la fila ya tiene su propio titulo delante.
    char* rvp = rv0; int rvl = strlen(rv0);
    if(rvl > 1 && rv0[0] == '(' && rv0[rvl - 1] == ')'){ rv0[rvl - 1] = 0; rvp = rv0 + 1; }
    char rv1[64]; connBleSub(rv1, sizeof(rv1));
    char* rbp = rv1; int rbl = strlen(rv1);
    if(rbl > 1 && rv1[0] == '(' && rv1[rbl - 1] == ')'){ rv1[rbl - 1] = 0; rbp = rv1 + 1; }
    y = setRowCard(y, RI_GLOBE, rgb565(60,150,235), "Conectividad",
                   gAirplane ? "Modo avi\xC3\xB3n activo" : "Wifi, BLE y modo avi\xC3\xB3n", true);
    y = setRowCard(y, RI_CLOUD, rgb565(70,120,225), "Wi-Fi", rvp, true);
    y = setRowCard(y, RI_DOT,   rgb565(90,110,235), "Bluetooth (BLE)", rbp, true);
    y += 8; drawText(DP_X, y, "Los interruptores encienden la radio de verdad", 1, SET_TXT_MUTE); y += 24;
  } else if(cat == 6){                      // Seguridad (funcional)
    const char* lt = gLockType == 1 ? "PIN configurado" : gLockType == 2 ? "Contrase\xC3\xB1" "a configurada" : "Deslizar";
    y = setRowCard(y, RI_DOT, rgb565(220,120,120), "Bloqueo", lt, true);
    y = setRowCard(y, RI_CLOCK, rgb565(120,150,235), "Bloqueo de inactividad", autoLockName(), true);
    // FLEX VAULT (Carpeta segura). El subtitulo dice el estado REAL de la
    // boveda; nunca cuantos elementos tiene ni sus nombres -- eso solo se ve
    // dentro, con la clave delante.
    { char vv[64]; vaultStatusText(vv, sizeof(vv));
      y = setRowCard(y, RI_PIN, rgb565(150,110,220), "Flex Vault", vv, true); }
#if POWEROFF_ON && POWEROFF_PIN_ON
    // Apagado seguro: pide el PIN/contrasena antes de apagar del todo. NO afecta
    // a la suspension (doble-tap de 2 dedos), que nunca pide clave.
    y = setRowCard(y, RI_DOT, rgb565(220,120,120), "Apagado seguro",
                   gLockType == 0 ? "Configura antes un PIN" : (gPoffPin ? "Activado" : "Desactivado"), true);
#endif
    y += 8; drawText(DP_X, y, "Toca para configurar PIN o contrase\xC3\xB1" "a", 1, SET_TXT_MUTE); y += 24;
  } else {
    const char* rt[3] = {0,0,0}; const char* rv[3] = {0,0,0}; int rn = 0;
    switch(cat){
      case 2: {   // SONIDO: el estado REAL del codec, no un 70% de relleno
        static char s2a[32];
        if(flexAudioAvailable()){
          snprintf(s2a, sizeof(s2a), "%d%%%s", (int)flexAudioVolume(),
                   flexAudioMuted() ? " (silenciado)" : "");
          rt[0]="Volumen"; rv[0]=s2a;
          rt[1]="Salida";  rv[1]="Altavoz (ES8311)";
        } else {
          // Sin codec no se ensena un porcentaje que no controla nada:
          // se dice que no hay salida y por que.
          rt[0]="Volumen"; rv[0]="Sin salida de audio";
          rt[1]="Motivo";  rv[1]=flexAudioError();
        }
        rn=2; } break;
      // (la categoria 3 tiene ahora su propia rama, con datos reales)
      case 4: rt[0]="GPIO";rv[0]="Configurable"; rt[1]="Perifericos";rv[1]="Ninguno"; rn=2; break;
      case 6: rt[0]="Bloqueo";rv[0]="Deslizar"; rt[1]="PIN";rv[1]="No configurado"; rn=2; break;
      case 7: rt[0]="Nivel";rv[0]="82%"; rt[1]="Ahorro";rv[1]="Desactivado"; rn=2; break;
      case 8: {   // valores REALES de la particion de datos
        static char s8a[32], s8b[32];
        char u8[16], t8[16];
        flexFsFmtSize(flexFsUsedBytes(), u8, sizeof(u8));
        flexFsFmtSize(flexFsTotalBytes(), t8, sizeof(t8));
        snprintf(s8a, sizeof(s8a), "%s / %s", u8, t8);
        flexFsFmtSize(flexFsCatSize(FLEXFS_CAT_TRASH), s8b, sizeof(s8b));
        rt[0]="Memoria interna"; rv[0]= flexFsReady() ? s8a : "No montada";
        rt[1]="Papelera"; rv[1]=s8b; rn=2; } break;
      case 9: rt[0]="Depuracion";rv[0]="En pantalla"; rt[1]="Banda reinicio";rv[1]="Solo crash"; rn=2; break;
      case 10: rt[0]="Version";rv[0]="FlexOS 1.0"; rt[1]="Logs";rv[1]="Puerto serie"; rn=2; break;
      default: rn=0; break;
    }
    for(int i = 0; i < rn; i++) y = setRowCard(y, RI_DOT, ic, rt[i], rv[i], true);
    y += 8; drawText(DP_X, y, "Mas opciones proximamente", 1, SET_TXT_MUTE); y += 24;
  }
  setContentH = (y - base) + 10;
}

// Acentos de cada categoria (los mismos de siempre, solo que ahora viven en la
// tarjeta de la lista principal en vez de en la barra lateral).
static const uint16_t SET_ACCENT[12] = {
  rgb565(70,120,235), rgb565(240,170,50), rgb565(70,120,235), rgb565(60,150,235),
  rgb565(80,180,120), rgb565(90,110,235), rgb565(90,95,110), rgb565(80,190,110),
  rgb565(150,90,210), rgb565(70,75,90), rgb565(70,120,235), rgb565(70,120,235) };

// ---- PANTALLA 1: lista de categorias (a todo el ancho, con scroll) ----
#define SET_CATCARD_H  66
#define SET_CATCARD_GAP 8
static int setCatY0[12], setCatY1[12];      // rango real de cada tarjeta (para el tap)
static void settingsDrawListHead(){
  drawText(16, 40, "Ajustes", 4, SET_TXT_HI);
}
static void settingsListContent(){
  int base = SET_LIST_TOP - setListScroll;
  for(int i = 0; i < 12; i++){
    int y = base + i * (SET_CATCARD_H + SET_CATCARD_GAP);
    setCatY0[i] = y; setCatY1[i] = y + SET_CATCARD_H;
    if(y > SET_LIST_BOT || y + SET_CATCARD_H < SET_LIST_TOP) continue;   // fuera de la ventana: ni se dibuja
    if(uiGlass) drawGlassCardFlat(SET_CARD_X, y, SET_CARD_W, SET_CATCARD_H, 16, SET_CARD_GLASS, PAGE_BG);
    else fillRoundRect(SET_CARD_X, y, SET_CARD_W, SET_CATCARD_H, 16, SET_CARD_BG);
    drawSetCatIcon(i, SET_CARD_X + 16, y + (SET_CATCARD_H - 32) / 2, 32, SET_ACCENT[i]);
    int tx = SET_CARD_X + 62, mr = SET_CARD_X + SET_CARD_W - 30;
    drawTextClip(tx, y + 14, SET_CAT[i], 2, SET_TXT_HI, mr);
    drawTextClip(tx, y + 38, SET_SUB[i], 1, SET_SIDE_SUB, mr);
    int chx = SET_CARD_X + SET_CARD_W - 20, chy = y + SET_CATCARD_H / 2;
    strokeSegAA(chx - 3, chy - 6, chx + 3, chy, 2.0f, SET_CHEV);
    strokeSegAA(chx + 3, chy, chx - 3, chy + 6, 2.0f, SET_CHEV);
  }
  setListH = 12 * (SET_CATCARD_H + SET_CATCARD_GAP) + 10;
}

// ---- PANTALLA 2: cabecera de la categoria abierta (con "atras") ----
#define SET_BACK_W 52                       // zona tactil del chevron de volver
static void settingsDrawDetailHead(){
  uint16_t c = SET_TXT_HI;
  strokeSegAA(30, 46, 18, 58, 2.6f, c);     // chevron "<" (centrado con el titulo)
  strokeSegAA(18, 58, 30, 70, 2.6f, c);
  drawTextClip(SET_BACK_W, 42, SET_CAT[setSel], 4, SET_TXT_HI, SCR_W - 12);
  drawTextClip(SET_BACK_W, 94, SET_DESC[setSel], 1, SET_TXT_LO, SCR_W - 12);
}
static void settingsDrawChromeDark(){
  if(gHosted) return;                // idem: hora, wifi, bateria y gestos son del UI principal
  uint16_t D = SET_TXT_HI;
  char cs[12]; clkStrBar(cs, sizeof(cs));
  drawText(16, 16, cs, 2, D);
  char sd[40]; buildShortDate(sd, sizeof(sd));
  drawText(16 + textW(cs, 2) + 14, 20, sd, 1, D);
  drawWifi(SCR_W - 66, 28, 11, D);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, D);
  if(gNavMode == 0){
    int ny = SCR_H - 52;
    fillTriangle(SCR_W / 6 - 10, ny + 8, SCR_W / 6 + 8, ny - 2, SCR_W / 6 + 8, ny + 18, D);
    drawCircle(SCR_W / 2, ny + 8, 12, D); drawCircle(SCR_W / 2, ny + 8, 11, D);
    drawRoundRect(SCR_W * 5 / 6 - 11, ny - 3, 22, 22, 4, D);
  } else {
    drawHomeIndicator(SCR_H, 210, SET_NAVPILL);   // pildora de gestos, a juego con el fondo de la pagina
  }
}
// Ventana con scroll de la vista activa.
static inline int setBandTop(){ return setView == 0 ? SET_LIST_TOP : DLIST_TOP; }
static inline int setBandBot(){ return setView == 0 ? SET_LIST_BOT : DLIST_BOT; }
static inline int setMaxScroll(){
  int h = (setView == 0) ? setListH : setContentH;
  int m = h - (setBandBot() - setBandTop());
  return m > 0 ? m : 0;
}
// Pinta la PAGINA COMPLETA de una vista en el gBuf actual. Es la unica fuente de
// verdad del aspecto de la app: la usan el repintado normal y los dos lienzos de
// la transicion, asi que lo que se anima es exactamente lo que luego se ve.
static void settingsPaintPage(int view){
  fillRect(0, 0, SCR_W, SCR_H, PAGE_BG);
  settingsDrawChromeDark();
  int top, bot;
  if(view == 0){ settingsDrawListHead();   top = SET_LIST_TOP; bot = SET_LIST_BOT; }
  else         { settingsDrawDetailHead(); top = DLIST_TOP;    bot = DLIST_BOT;    }
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = top; gClipY1 = bot - 1;
  if(view == 0) settingsListContent(); else settingsDetailContent(setSel);
  gClipY0 = c0; gClipY1 = c1;
}
// Repintado completo. Se compone en bbuf y se publica de una sola vez con
// present(): un cuadro entero o ninguno. Antes se dibujaba fila a fila DIRECTO
// sobre fb mientras DMA2D podia estar leyendolo, que es de donde salian
// los parpadeos y las costuras al repintar.
static void settingsRender(){
  setBuf(bbuf);
  settingsPaintPage(setView);
  present(0, SCR_H - 1);
  setBuf(fb);
}
// Repintado de SOLO la banda con scroll (cada cuadro de un arrastre). Tambien
// via bbuf + present: el vidrio de las tarjetas se mantiene y no hay costuras.
static void settingsRenderBandOnly(){
  int top = setBandTop(), bot = setBandBot();
  setBuf(bbuf);
  fillRect(0, top, SCR_W, bot - top, PAGE_BG);
  int c0 = gClipY0, c1 = gClipY1;
  gClipY0 = top; gClipY1 = bot - 1;
  if(setView == 0) settingsListContent(); else settingsDetailContent(setSel);
  gClipY0 = c0; gClipY1 = c1;
  present(top, bot - 1);
  setBuf(fb);
}
// Nombre historico: lo siguen llamando las acciones de fila (settingsRowAction).
static void settingsRenderDetailOnly(){ settingsRenderBandOnly(); }

// ---- TRANSICION ENTRE PANTALLAS (empuje horizontal con paralaje) ----
// Dos lienzos de pagina en PSRAM. Se reservan la primera vez que se navega y se
// conservan: reservarlos y liberarlos en cada transicion solo fragmentaria el
// monton. Si no hay PSRAM, la navegacion sigue funcionando -- solo se pierde la
// animacion, nunca la funcion.
static uint16_t *setPgOut = NULL, *setPgIn = NULL;
static bool settingsPagesReady(){
  if(!setPgOut) setPgOut = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!setPgIn)  setPgIn  = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return setPgOut && setPgIn;
}
// Una fila desplazada 'off' px, recortada a la pantalla.
static inline void setRowShift(uint16_t* dst, const uint16_t* src, int off){
  int ds = off, ss = 0, n = SCR_W;
  if(ds < 0){ ss = -ds; n = SCR_W + ds; ds = 0; }
  else if(ds > 0) n = SCR_W - ds;
  if(n > 0) memcpy(dst + ds, src + ss, (size_t)n * 2);
}
// dir = +1 al ABRIR una categoria (la nueva entra desde la derecha),
// dir = -1 al VOLVER (la de detalle se va por la derecha y descubre la lista).
static void settingsAnimate(int fromView, int toView, int dir){
  // Hospedada en una ventana de Modo PC no se anima: de un tick hospedado solo
  // se ve el ultimo cuadro. El cambio de pantalla es instantaneo, como antes.
  if(gHosted || !settingsPagesReady()){ setView = toView; settingsRender(); return; }
  int keep = setView;
  setView = fromView; setBuf(setPgOut); settingsPaintPage(fromView);
  setView = toView;   setBuf(setPgIn);  settingsPaintPage(toView);
  setView = keep;
  setBuf(fb);
  const int y0 = SET_ANIM_Y0, y1 = SET_ANIM_Y1 - 1;
  const int PARA = (SCR_W * 28) / 100;               // paralaje: la que sale recorre un 28%
  int lastO = 0x7FFF, lastI = 0x7FFF;                // ultimo par de desplazamientos ya pintado
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > (uint32_t)SET_NAV_MS) e = SET_NAV_MS;
    float p = (float)e / (float)SET_NAV_MS;
    float ip = 1.0f - p;
    p = (p < 0.5f) ? (4.0f * p * p * p) : (1.0f - 4.0f * ip * ip * ip);   // ease-in-out cubica
    int oOff, iOff;
    if(dir > 0){ oOff = (int)(-PARA * p);            // sale hacia la izquierda, despacio
                 iOff = (int)(SCR_W * (1.0f - p)); }  // entra desde la derecha
    else       { oOff = (int)(SCR_W * p);            // sale hacia la derecha
                 iOff = (int)(-PARA * (1.0f - p)); }  // la de debajo vuelve a su sitio
    if(oOff == lastO && iOff == lastI){          // el reloj aun no ha movido nada
      if(e < (uint32_t)SET_NAV_MS){ delay(1); continue; }
      break;
    }
    lastO = oOff; lastI = iOff;
    for(int j = y0; j <= y1; j++){
      uint16_t* d = bbuf + (size_t)j * SCR_W;
      // Orden de pintado = orden de profundidad. Al abrir, la nueva va ENCIMA;
      // al volver, la que se va es la de encima y descubre a la de abajo.
      if(dir > 0){ setRowShift(d, setPgOut + (size_t)j * SCR_W, oOff);
                   setRowShift(d, setPgIn  + (size_t)j * SCR_W, iOff); }
      else       { setRowShift(d, setPgIn  + (size_t)j * SCR_W, iOff);
                   setRowShift(d, setPgOut + (size_t)j * SCR_W, oOff); }
      // Sombra de 8 px en el borde de la pagina de encima: da profundidad y
      // tapa la costura entre las dos capas.
      int edge = (dir > 0) ? iOff : oOff;
      if(edge > 0 && edge <= SCR_W){
        for(int k = 1; k <= 8; k++){
          int x = edge - k; if(x < 0) break;
          d[x] = mix565(d[x], TH_SHADOW, effShadow(70 - k * 8));
        }
      }
    }
    present(y0, y1);
    if(e >= (uint32_t)SET_NAV_MS) break;
  }
  setView = toView;
  settingsRender();                                   // estado final limpio, sin restos del paralaje
}
static void settingsOpenCat(int cat){
  if(cat < 0 || cat > 11) return;
  setSel = cat; setScroll = 0; setDragging = false;
  sessMarkDirty(IC_AJUSTES);              // categoria abierta: estado de sesion
  settingsAnimate(0, 1, +1);
}
// Volver a la lista. Devuelve false si ya estabamos en la lista: asi el boton
// "atras" del sistema cierra la app solo cuando no queda pantalla que cerrar
// (igual que en Android).
static bool settingsHandleBack(){
  if(setView != 1) return false;
  setDragging = false; setBackSwipe = false;
  sessMarkDirty(IC_AJUSTES);
  settingsAnimate(1, 0, -1);
  return true;
}
static void settingsEnter(){
  // La sesion (categoria abierta + las dos posiciones de scroll) se recupera la
  // primera vez que se abre Ajustes tras el arranque. Antes esto reiniciaba
  // SIEMPRE la vista, asi que tampoco respetaba el re-maquetado de DeX.
  appLoadSessionOnce(IC_AJUSTES);
  setDragging = false; setBackSwipe = false;
  settingsRender();
}
// Accion al tocar una fila del panel de detalle (ajustes funcionales)
// Todo ajuste que cambie el ASPECTO del escritorio marca gHomeDirty: homeBuf es
// una cache ya pintada y hay que recomponerla antes de volver a mostrarla.
// (gBright no lo hace: es PWM del backlight, no repinta nada.)
static void settingsRowAction(int cat, int idx){
  if(cat == 0){
    if(idx == 0){ cfgLang = (cfgLang + 1) % 6; cfgSavePrefs(); gHomeDirty = true; settingsRender(); }         // idioma (cicla)
    else if(idx == 2){ g24h = !g24h; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }         // formato 12/24
    // idx 3 (zona horaria), 4 (estado NTP) y 5 (ultima sincronizacion) son
    // informativas: no tienen accion. La zona de Lima es fija por diseno.
    else if(idx == 6){ ntpRequestSync(true); settingsRenderDetailOnly(); }                                    // Sincronizar ahora
    else if(idx == 7) flexOtaOpenSettings();                                                                 // Actualizaciones -> pantalla OTA
    // idx 8 (Copias de seguridad) sigue informativa. Restablecer abre el
    // asistente protegido; todavia no borra nada en este toque.
    else if(idx == 9) frEnterWizard();
    else if(idx == 10) accountSettingsEnter();                                                               // General -> Flex Account
  } else if(cat == 1){
    if(idx == 0){ gBright += 25; if(gBright > 100) gBright = 25; setBacklight(gBright); cfgSavePrefs(); settingsRenderDetailOnly(); }  // brillo real
    // MATERIAL (Liquid Glass <-> plano) y APARIENCIA (oscuro <-> claro) son dos
    // preferencias ORTOGONALES: cada una toca SOLO su flag y delega en
    // themeChanged(), que guarda en NVS, invalida las caches visuales y repinta.
    else if(idx == 1){ uiGlass = !uiGlass; themeChanged(); }
    else if(idx == 2){ gDark   = !gDark;   themeChanged(); }
    else if(idx == 3){ gNavMode = (gNavMode == 0) ? 1 : 0; cfgSavePrefs(); gHomeDirty = true; settingsRender(); } // barra: botones <-> gestos (redibuja tambien la barra inferior)
    else if(idx == 4){ gLockWidgets ^= LW_CLOCK;   cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: reloj grande
    else if(idx == 5){ gLockWidgets ^= LW_WEATHER; cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: clima
    else if(idx == 6){ gLockWidgets ^= LW_CAL;     cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: calendario
    else if(idx == 7){ gLockWidgets ^= LW_NOTIF;   cfgSavePrefs(); settingsRenderDetailOnly(); }  // Bloqueo: notificaciones
  } else if(cat == 5){
    if(idx == 0){ uiGlass = !uiGlass; themeChanged(); }                                                       // Personalizar UI (material)
    else if(idx == 1){ gIconStyle = (gIconStyle == 0) ? 1 : 0; cfgSavePrefs(); gHomeDirty = true; settingsRenderDetailOnly(); }  // estilo de iconos: Plano <-> Vidrio
    else if(idx == 2){ gAnimStyle = (gAnimStyle + 1) % 3; cfgSavePrefs(); settingsRenderDetailOnly(); }  // transiciones: zoom -> fundido -> deslizar -> zoom
#if KB_SETTINGS_ON
    else if(idx == 3) kbsEnter();                                                                       // FASE E: Ajustes del teclado
#endif
  } else if(cat == 3){
    if(idx == 0) connEnter();                                                            // Conectividad (Wifi/BLE/Modo avion)
    else if(idx == 1) wifiSettingsEnter();                                               // Red e Internet -> Wi-Fi
    else if(idx == 2){                                                                   // Bluetooth (BLE): conmuta de verdad
      if(!FLEXOS_ENABLE_BLE || gAirplane) return;
      if(gBleOn) flexBleStop(); else flexBleStart();
      settingsRenderDetailOnly();
    }
  } else if(cat == 6){
    if(idx == 0) lsuEnter();                                                                 // Seguridad -> Bloqueo (PIN/Contraseña)
    else if(idx == 1){                                                                       // Seguridad -> Bloqueo de inactividad
      // Cicla 30 s -> 1 min -> 5 min -> 10 min -> 30 min -> Nunca -> 30 s, igual
      // que hacen las demas filas de opcion de Ajustes (Transiciones, Iconos...).
      gAutoLockMs = AUTOLOCK_OPTS[(autoLockIdx() + 1) % AUTOLOCK_NOPT];
      gLastTouchMs = millis();                    // el temporizador nuevo cuenta desde ahora
      cfgSavePrefs();
      settingsRenderDetailOnly();
    }
    else if(idx == 2) vaultSettingsEnter();                                              // Seguridad y privacidad -> Flex Vault
#if POWEROFF_ON && POWEROFF_PIN_ON
    else if(idx == 3){                                                                   // Seguridad -> Apagado seguro
      // Sin PIN/contrasena configurada no hay nada que pedir: activarlo seria una
      // proteccion de mentira. Se deja tal cual y la fila ya avisa ("Configura
      // antes un PIN").
      if(gLockType == 0) return;
      gPoffPin = !gPoffPin;
      cfgSavePrefs();
      settingsRenderDetailOnly();
    }
#endif
  }
}
static void settingsTick(){
  int top = setBandTop(), bot = setBandBot(), maxS = setMaxScroll();

  // ---- Volver: chevron de la cabecera o arrastre desde el borde izquierdo ----
  if(setView == 1){
    if(T.tap && T.x < SET_BACK_W && T.y >= 20 && T.y <= 84){ settingsHandleBack(); return; }
    // Gesto de retroceso: empieza pegado al borde izquierdo y se lleva el dedo a
    // la derecha. Se decide al soltar (no a media pulsacion) para que un scroll
    // que empiece cerca del borde no lo dispare por accidente.
    if(T.pressed && T.startX < 28) setBackSwipe = true;
    if(setBackSwipe && T.released){
      setBackSwipe = false;
      if((T.x - T.startX) > 70 && abs(T.y - T.startY) < 90){ settingsHandleBack(); return; }
    }
    if(!T.down) setBackSwipe = false;
  }

  // ---- Tap ----
  if(T.tap && !setDragging && T.y >= top && T.y <= bot){
    if(setView == 0){
      for(int i = 0; i < 12; i++)
        if(T.y >= setCatY0[i] && T.y < setCatY1[i]){ settingsOpenCat(i); return; }
    } else {
      for(int i = 0; i < setRowN; i++)
        if(T.y >= setRowY0[i] && T.y < setRowY1[i]){ settingsRowAction(setSel, i); return; }
    }
  }

  // ---- Scroll de la vista activa ----
  // Arrastre real: el contenido va pegado al dedo cuadro a cuadro, con umbral de
  // 6 px para no confundir un toque con un arrastre. Ahora vale para las DOS
  // pantallas (la lista de categorias tambien se desplaza) y ya no apaga el
  // vidrio de las tarjetas mientras dura.
  int* scroll = (setView == 0) ? &setListScroll : &setScroll;
  if(T.pressed && T.y >= top - 24 && T.y <= bot){ setDragY0 = T.y; setDragS0 = *scroll; setDragging = false; }
  if(T.down && maxS > 0 && T.startY >= top - 24 && T.startY <= bot){
    int dy = setDragY0 - T.y;
    if(!setDragging && abs(dy) > 6) setDragging = true;
    if(setDragging){
      int ns = setDragS0 + dy;
      if(ns < 0) ns = 0; if(ns > maxS) ns = maxS;
      if(ns != *scroll){ *scroll = ns; settingsRenderBandOnly(); }
      return;
    }
  }
  if(T.released && setDragging){ setDragging = false; sessMarkDirty(IC_AJUSTES); return; }  // el scroll es estado de sesion
}

// #############################################################
// ##  AJUSTES · CICLO DE VIDA Y SESION
// ##  ------------------------------------------------------
// ##  Lo que se conserva: la CATEGORIA abierta y las DOS posiciones
// ##  de scroll (la de la lista de categorias y la de la pantalla de
// ##  detalle). No se guarda nada mas: los valores de configuracion ya
// ##  viven en NVS por su cuenta y duplicarlos aqui seria tener dos
// ##  fuentes de verdad para lo mismo.
// #############################################################
#define SET_SESS_VER   1
#define SET_SESS_PATH  FS_DIR_SESS "/ajustes.bin"
struct SetSessV1 { int16_t view, sel, scroll, listScroll; };

static void setSuspend(){
  // Se corta cualquier arrastre en curso: al volver, el dedo es otro.
  setDragging = false; setBackSwipe = false;
}
static void setResume(){
  setDragging = false; setBackSwipe = false;
  settingsRender();                       // misma categoria, mismo scroll
}
static bool setSaveSess(){
  if(!flexFsReady()) return true;
  SetSessV1 v;
  v.view = (int16_t)setView; v.sel = (int16_t)setSel;
  v.scroll = (int16_t)setScroll; v.listScroll = (int16_t)setListScroll;
  return sessWrite(SET_SESS_PATH, SET_SESS_VER, IC_AJUSTES, &v, sizeof(v));
}
static void setLoadSess(){
  if(!flexFsReady()) return;
  SetSessV1 v;
  if(sessRead(SET_SESS_PATH, SET_SESS_VER, IC_AJUSTES, &v, sizeof(v)) != sizeof(v)) return;
  if(v.sel < 0 || v.sel > 11) return;     // sesion incoherente: se ignora entera
  setView = (v.view == 1) ? 1 : 0;
  setSel = v.sel;
  setScroll = (v.scroll > 0) ? v.scroll : 0;
  setListScroll = (v.listScroll > 0) ? v.listScroll : 0;
}
// LISTA BLANCA: mientras el OTA -- que se lanza desde esta app -- tiene trabajo
// de red en curso, Ajustes no se cierra ni por "Cerrar todo" ni por el limite de
// sesiones. Es el unico proceso de segundo plano REAL que hay hoy en el sistema.
static bool setBgWork(){ return flexOtaBusy(); }
