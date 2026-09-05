// #############################################################
// ##  FLEX OS ULTRA  ·  ESCRITORIO: PAGINAS, DESLIZAMIENTO Y MODO EDICION
// ##  ----------------------------------------------------------
// ##  El escritorio por paginas, el arrastre entre paginas con homeBuf y
// ##  hpBuf, y el modo edicion con jiggle y drag & drop de iconos.
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
#include "FlexOS_Ultra_Shell.h"   // eslabon anterior de la cadena

// #############################################################
// ##  ESCRITORIO POR PAGINAS
// ##  ------------------------------------------------------
// ##  Los tres circulos de debajo de la rejilla llevaban ahi desde el
// ##  Milestone 1, pero solo el primero se pintaba encendido: no habia
// ##  segunda pagina que mostrar. Ahora si la hay, y son EXACTAMENTE
// ##  esas tres: la rejilla pasa de 12 ranuras a 3x12 = 36.
// ##
// ##  COMPATIBILIDAD CON LO YA GUARDADO: la clave NVS "hord" media 12
// ##  bytes. Al leerla se migra -- esos 12 bytes son la PAGINA 0 y las
// ##  otras dos nacen vacias -- asi que una placa que actualice se
// ##  encuentra su escritorio exactamente como lo dejo, con sus
// ##  iconos en sus sitios, y las apps nuevas caen solas en la
// ##  pagina 2. Ni un icono se mueve.
// #############################################################
// PAGINAS VARIABLES Y REJILLA CONFIGURABLE. Antes eran tres paginas fijas de
// 4x3. Ahora el numero de paginas (1..5) y la rejilla (4x3, 5x3, 4x4, 5x4) los
// elige el usuario en Personalizar inicio.
//
// EL PASO ENTRE PAGINAS EN homeOrder[] ES FIJO (HOME_STRIDE = 20 ranuras
// reservadas), NO el numero de ranuras usables. Es lo que permite cambiar de
// rejilla sin reinterpretar el array entero ni perder un icono: las ranuras
// sobrantes de cada pagina se mantienen SIEMPRE vacias (homeOrderNormalize las
// vacia), y al reducir la rejilla los iconos que se quedan fuera se recolocan
// en el primer hueco util en vez de desaparecer.
//
// COMPATIBILIDAD: la clave "hordp" (36 B, 3 paginas x 12) se deja INTACTA, igual
// que en su dia se dejo "hord" (12 B). Se lee para MIGRAR a la clave nueva
// "hordq" y ya no se vuelve a escribir: si el usuario baja de version por USB,
// se encuentra su escritorio de tres paginas tal cual estaba.
#define HOME_PAGES_MAX 5
#define HOME_COLS_MAX  5
#define HOME_ROWS_MAX  4
#define HOME_STRIDE    (HOME_COLS_MAX * HOME_ROWS_MAX)      // 20 ranuras reservadas por pagina
#define HOME_TOTAL     (HOME_PAGES_MAX * HOME_STRIDE)       // 100 B en RAM y en NVS
#define HOME_LEGACY_PAGES 3
#define HOME_LEGACY_SLOTS 12
#define HOME_LEGACY_TOTAL (HOME_LEGACY_PAGES * HOME_LEGACY_SLOTS)
static uint8_t homeOrder[HOME_TOTAL] = { 0,1,2,3,4,5,6,7,8,9,10,11 };  // app id por ranura (reordenable)
static int     gHomePage  = 0;                         // pagina visible (0..gHomePageN-1)
static uint8_t gHomePageN = HOME_LEGACY_PAGES;         // paginas existentes (1..HOME_PAGES_MAX)
static uint8_t gHomeMain  = 0;                         // pagina principal (la de la casita)
static uint8_t gHomeCols  = 4, gHomeRows = 3;          // rejilla activa
static uint8_t gHomeIconSz = 1;                        // 0 pequeno · 1 normal · 2 grande
static bool    gHomeLabels = true;                     // nombres de app bajo el icono
static bool    gHomeLocked = false;                    // diseno bloqueado (sin arrastrar ni borrar)
static bool    gHomeDots   = true;                     // indicadores de pagina
static bool    gHomePinch  = true;                     // gesto de pellizco de dos dedos
static bool    gHomeReduce = false;                    // reducir animaciones del escritorio
// Ranuras USABLES de la rejilla activa y paso FIJO dentro de homeOrder[].
static inline int homeSlotCount(){ return (int)gHomeCols * (int)gHomeRows; }
static inline int homeIdx(int page, int local){ return page * HOME_STRIDE + local; }
// Primera ranura USABLE libre recorriendo las paginas en orden, o -1.
// WIDGETS COLOCADOS. Las tablas viven AQUI, con el resto del modelo del
// escritorio, porque la normalizacion de iconos necesita saber que celdas
// ocupan; el dibujo y la edicion estan mas abajo, con las primitivas.
static HomeWidget gHomeWg[HOME_PAGES_MAX][HOME_WG_MAX];
static uint8_t    gHomeWgN[HOME_PAGES_MAX] = { 0 };
// Blob de widgets en NVS: magic + version + por pagina (cuenta + 3 x 5 bytes).
#define HOME_WG_BLOB (2 + HOME_PAGES_MAX * (1 + HOME_WG_MAX * 5))
static uint32_t homeCellMask(int page, int skipWg);
static void     homeWgNormalize();
static void     homeWgSerialize(uint8_t* b);
static bool     homeWgDeserialize(const uint8_t* b);
static void     homeDrawWidgets(int page, int xoff);
static bool     homeWgFits(int page, int c, int r, int w, int h, int skipWg);
static void     homeWgRemove(int page, int idx);
static void     wgRect(const HomeWidget* w, int &x, int &y, int &ww, int &hh);
static void     wgDrawCell(const HomeWidget* wg, int x, int y, int w, int h, bool mini);
static int      homeWgAt(int page, int px, int py);

// Primera ranura USABLE y LIBRE (ni icono ni widget encima), o -1.
static int homeFirstFree(){
  int n = homeSlotCount();
  for(int p = 0; p < gHomePageN; p++){
    uint32_t m = homeCellMask(p, -1);
    for(int i = 0; i < n; i++) if(!(m & (1u << i))) return homeIdx(p, i);
  }
  return -1;
}
// Anade una pagina VACIA al final. SIN aviso y SIN guardar: lo usan la
// normalizacion (que corre en cada arranque) y la colocacion automatica, y
// quien las llama ya guarda al terminar su operacion. Escribir NVS aqui seria
// una escritura de flash por cada arranque.
static bool homePageAppendQuiet(){
  if(gHomePageN >= HOME_PAGES_MAX) return false;
  for(int i = 0; i < HOME_STRIDE; i++) homeOrder[homeIdx(gHomePageN, i)] = HOME_EMPTY;
  gHomeWgN[gHomePageN] = 0;
  gHomePageN++;
  return true;
}
// PRIMERA RANURA LIBRE, CREANDO PAGINA SI HACE FALTA.
// ---------------------------------------------------------------------------
// Aqui estaba el fallo de "se agrega una app y se coloca encima de otra / se
// pierde": homeFirstFree() solo mira las paginas que YA existen, y sus dos
// llamantes reaccionaban al -1 de la peor manera posible --
// homeOrderNormalize() borraba la app de favoritos (gAppFav &= ~bit: la app
// desaparecia del escritorio sin decir nada) y drwFavToggle() se negaba a
// anadirla. Ninguno de los dos creaba la pagina que faltaba.
//
// Ahora, cuando no queda ni una celda util en ninguna pagina, se crea otra y el
// icono cae en su primera celda. Solo se devuelve -1 en el unico caso en el que
// de verdad no cabe: HOME_PAGES_MAX paginas y todas sin un hueco. El modelo
// sigue siendo "una app por ranura", asi que dos iconos en la misma celda son
// imposibles por construccion; lo que faltaba era el crecimiento.
static int homeFirstFreeGrow(){
  int slot = homeFirstFree();
  if(slot >= 0) return slot;
  if(!homePageAppendQuiet()) return -1;   // ya hay el maximo de paginas y todas llenas
  return homeFirstFree();
}
static bool gMinChanged = false;   // lo pone loop(): true cuando cambia el minuto

// RITMO DEL SISTEMA. Vueltas completas de loop() por segundo. Se cuenta con un
// entero y una comparacion de millis(), asi que no cuesta nada medible, y es
// una medida REAL de la capacidad de respuesta -- a diferencia de un "FPS"
// global, que este sistema no tiene: cada pantalla publica su propia banda
// cuando le toca y no hay un unico punto de presentacion que contar.
static uint32_t gLoopRate  = 0;    // ultima medida (vueltas/s)
static uint32_t gLoopCount = 0;
static uint32_t gLoopMs    = 0;
static void loopRateTick(){
  gLoopCount++;
  uint32_t now = millis();
  if(now - gLoopMs < 1000) return;
  gLoopRate = gLoopCount * 1000u / (now - gLoopMs);
  gLoopCount = 0; gLoopMs = now;
}

// ---- Invalidacion de caches de pantalla ----
// homeBuf (y la cortina qsBuf, que se compone de el) son escritorios YA
// pintados. Si cambia un ajuste que altera su aspecto -idioma, formato de
// hora, Liquid Glass, estilo de iconos- hay que volver a componerlos, o al
// salir de Ajustes se ve el escritorio VIEJO hasta que cambie el minuto.
// Ese era el motivo de que "algo no coincidiera" tras tocar un ajuste.
static bool gHomeDirty = false;    // homeBuf no refleja los ajustes actuales
static bool qsDirty    = true;     // qsBuf debe recomponerse
// teclado
static int  keyN = 0;
static int  kX[48], kY[48], kW[48], kH[48], kCode[48];

static void blitToFb(uint16_t* src){ fbCopyBand(src, 0, SCR_H - 1); }

// ---------------- Banda forense de arranque ----------------
static const char* resetReasonStr(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC (crash)";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (voltaje)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default:                return "OTRO";
  }
}
// Solo se muestra tras un reinicio ANORMAL (crash/watchdog/brownout),
// para que puedas leer el motivo sin monitor serie. En un encendido
// normal NO aparece: el arranque va directo al splash limpio.
static void showBootBanner(){
  setBuf(fb);
  // SPLASH: negro sobre negro a proposito, en las dos apariencias. Es el arranque
  // del panel (backlight subiendo desde 0) y un fondo claro aqui daria un fogonazo
  // en la cara del usuario; ademas corre antes de que la UI del tema exista.
  fillRect(0, 0, SCR_W, SCR_H, rgb565(0,0,0));
  drawTextC(SCR_W / 2, SCR_H / 2 - 24, "FlexOS Ultra", 3, rgb565(235,238,245));
  char b[72];
  snprintf(b, sizeof(b), "ultimo reinicio: %s", resetReasonStr());
  drawTextC(SCR_W / 2, SCR_H / 2 + 22, b, 1, rgb565(240,185,90));
  drawTextC(SCR_W / 2, SCR_H / 2 + 42, "P4 480x800 - modo offline", 1, rgb565(140,150,170));
  flxFlushAll();
  delay(2200);
}

// ---------------- SPLASH (fundido sobre NEGRO ABSOLUTO) ----------------
static void splashFrame(uint8_t a){
  int size = 6;
  int ty = SCR_H / 2 - 40;
  int ss = 2, sty = ty + size * 7 + 20;
  int y0 = ty - 10, y1 = sty + ss * 7 + 8;
  setBuf(fb);
  fillRect(0, y0, SCR_W, y1 - y0 + 1, rgb565(0,0,0));                  // banda negra
  drawTextCA(SCR_W / 2, ty, "FlexOS Ultra", size, rgb565(255,255,255), a);
  drawTextCA(SCR_W / 2, sty, "ESP32-P4", ss, rgb565(170,182,200), (uint8_t)(a * 7 / 10));
  flxFlush(y0, y1);
  // puntos de carga (estilo movil): uno se ilumina en secuencia
  int dy = SCR_H - 128, phase = (int)((millis() / 320) % 3);
  fillRect(0, dy - 7, SCR_W, 15, rgb565(0,0,0));
  for(int i = 0; i < 3; i++)
    fillCircleAA(SCR_W / 2 - 16 + i * 16, dy, 4.0f,
                 (i == phase) ? rgb565(255,255,255) : rgb565(70,74,82));
  flxFlush(dy - 8, dy + 8);
}
static void splashTick(){
  unsigned long e = millis() - splashStart;
  uint8_t a;
  if(e < 600)       a = (uint8_t)(e * 255 / 600);
  else if(e < 2000) a = 255;
  else if(e < 2600) a = (uint8_t)(255 - (e - 2000) * 255 / 600);
  else {
    // MODO SEGURO: ni bloqueo ni escritorio. La pantalla de Modo seguro es la
    // primera y unica del arranque, con la causa real y las salidas.
    if(gSafeMode){ safeEnter(); return; }
    if(!cfgOobeDone) enterOobeLang();
    // FASE 4: si el telefono se apago con el kiosco puesto, se vuelve a entrar en
    // la misma app SIN pasar por el escritorio. La unica salida sigue siendo el
    // gesto del candado + PIN/contrasena.
    else if(KIOSK_ON && kioskOn && kioskApp >= 0){
      renderHome();                      // la transicion compone sobre homeBuf
      enterApp(kioskApp);
      kioskShowBadge();
    }
    else { renderHome(); renderLock(); showLock(); gState = ST_LOCK; lockOff = 0; lastLockOff = -1; }
    return;
  }
  splashFrame(a);
  delay(16);
}

// ---------------- OOBE: idioma ----------------
static void renderOobeLang(){
  drawWallpaper(fb, false); setBuf(fb);
  // OOBE (primer arranque): se pinta SOBRE EL WALLPAPER y ocurre ANTES de que el
  // usuario haya podido elegir apariencia -- todavia no hay preferencia que
  // respetar. Misma regla que el resto del chrome sobre wallpaper: claro siempre.
  drawTextC(SCR_W / 2, 78, t(S_SELLANG), 3, rgb565(255,255,255));
  int rowH = 74, gap = 14, x = 44, w = SCR_W - 88, y0 = 158;
  for(int i = 0; i < NLANG; i++){
    int y = y0 + i * (rowH + gap);
    bool sel = (i == oobeSel);
    fillRoundRectA(x, y, w, rowH, 18, rgb565(255,255,255), sel ? 235 : 55);
    uint16_t tc = sel ? rgb565(28,28,38) : rgb565(255,255,255);
    const char* lbl = (i == 5) ? "Chinese" : LANG_ENDONYM[i];
    drawText(x + 28, y + rowH / 2 - 10, lbl, 3, tc);
    if(sel){
      int chx = x + w - 48, chy = y + rowH / 2;
      strokeSeg(chx - 8, chy, chx - 2, chy + 8, 2, rgb565(40,160,90));
      strokeSeg(chx - 2, chy + 8, chx + 12, chy - 10, 2, rgb565(40,160,90));
    }
  }
  int by = SCR_H - 96, bw = SCR_W - 88, bx = 44;
  fillRoundRect(bx, by, bw, 60, 30, rgb565(255,255,255));
  drawTextC(SCR_W / 2, by + 21, t(S_CONTINUE), 3, rgb565(40,80,200));
  flxFlushAll();
}
static void enterOobeLang(){
  // ARRANQUE LIMPIO CONFIRMADO. Llegar aqui como dispositivo nuevo es la prueba
  // de que el restablecimiento termino bien, asi que este es el momento -- y el
  // unico -- de borrar el marcador de recuperacion.
  if(gFrConfirmPending){ gFrConfirmPending = false; frClearMarker(); }
  cfgLang = 0; oobeSel = 0; gState = ST_OOBE_LANG; renderOobeLang();
}
static void oobeLangTick(){
  if(!T.tap) return;
  int rowH = 74, gap = 14, x = 44, w = SCR_W - 88, y0 = 158;
  for(int i = 0; i < NLANG; i++){
    int y = y0 + i * (rowH + gap);
    if(T.x >= x && T.x <= x + w && T.y >= y && T.y <= y + rowH){
      oobeSel = i; cfgLang = i; renderOobeLang(); return;
    }
  }
  int by = SCR_H - 96, bw = SCR_W - 88, bx = 44;
  if(T.x >= bx && T.x <= bx + bw && T.y >= by && T.y <= by + 60){ enterOobeName(); return; }
}

// ---------------- OOBE: nombre (teclado QWERTY) ----------------
static void kbAdd(int x, int y, int w, int h, int code, const char* cap){
  kX[keyN] = x; kY[keyN] = y; kW[keyN] = w; kH[keyN] = h; kCode[keyN] = code; keyN++;
  fillRoundRect(x, y, w, h, 8, rgb565(250,250,252));
  if(cap) drawTextC(x + w / 2, y + h / 2 - 7, cap, 2, rgb565(28,28,38));
  else if(code == -2) fillRoundRect(x + w / 2 - 30, y + h / 2 - 2, 60, 4, 2, rgb565(90,90,100)); // barra espacio
}
static void drawKeyboard(){
  keyN = 0;
  const char* r1 = "QWERTYUIOP";
  const char* r2 = "ASDFGHJKL";
  const char* r3 = "ZXCVBNM";
  int kw = 40, kh = 52, g = 6, step = 60, kbTop = 544;
  { int n = 10; int sx = (SCR_W - (n * kw + (n - 1) * g)) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r1[i], 0 }; kbAdd(sx + i * (kw + g), kbTop, kw, kh, r1[i], c); } }
  { int n = 9; int sx = (SCR_W - (n * kw + (n - 1) * g)) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r2[i], 0 }; kbAdd(sx + i * (kw + g), kbTop + step, kw, kh, r2[i], c); } }
  { int n = 7; int backW = 2 * kw + g;
    int totalW = n * kw + (n - 1) * g + g + backW;
    int sx = (SCR_W - totalW) / 2;
    for(int i = 0; i < n; i++){ char c[2] = { r3[i], 0 }; kbAdd(sx + i * (kw + g), kbTop + 2 * step, kw, kh, r3[i], c); }
    kbAdd(sx + n * (kw + g), kbTop + 2 * step, backW, kh, -1, "<-"); }
  { int okW = 120, y = kbTop + 3 * step;
    int spX = 8 + kw, spW = SCR_W - spX - (okW + g) - 8;
    kbAdd(spX, y, spW, kh, -2, NULL);
    kbAdd(spX + spW + g, y, okW, kh, -3, "OK"); }
}
static void drawNameField(){
  int fx = 44, fy = 150, fw = SCR_W - 88, fh = 64;
  fillRoundRect(fx, fy, fw, fh, 16, rgb565(255,255,255));
  if(strlen(cfgName) == 0)
    drawText(fx + 20, fy + fh / 2 - 8, t(S_NAMEHINT), 2, rgb565(150,150,158));
  else {
    int ex = drawText(fx + 20, fy + fh / 2 - 10, cfgName, 3, rgb565(24,24,30));
    fillRect(ex + 2, fy + 16, 3, fh - 32, rgb565(55,120,240));
  }
  flxFlush(fy - 2, fy + fh + 2);
}
static void enterOobeName(){
  gState = ST_OOBE_NAME;
  cfgName[0] = 0;
  drawWallpaper(fb, false); setBuf(fb);
  drawTextC(SCR_W / 2, 70, t(S_YOURNAME), 3, rgb565(255,255,255));
  drawKeyboard();
  drawNameField();
  flxFlushAll();
}
static bool hitKey(int px, int py, int &code){
  for(int i = 0; i < keyN; i++)
    if(px >= kX[i] && px <= kX[i] + kW[i] && py >= kY[i] && py <= kY[i] + kH[i]){ code = kCode[i]; return true; }
  return false;
}
static void oobeNameTick(){
  if(!T.tap) return;
  int code;
  if(!hitKey(T.x, T.y, code)) return;
  int L = strlen(cfgName);
  if(code >= 32){ if(L < 20){ cfgName[L] = (char)code; cfgName[L + 1] = 0; drawNameField(); } }
  else if(code == -1){ if(L > 0){ cfgName[L - 1] = 0; drawNameField(); } }
  else if(code == -2){ if(L > 0 && L < 20){ cfgName[L] = ' '; cfgName[L + 1] = 0; drawNameField(); } }
  else if(code == -3){
    if(strlen(cfgName) == 0) strcpy(cfgName, "FlexOS Ultra");
    // Guarda idioma y nombre, pero OOBE no termina hasta vincular u omitir
    // Flex Account en la pantalla siguiente.
    prefs.begin("flexos", false);
    prefs.putInt("lang", cfgLang);
    prefs.putString("name", cfgName);
    prefs.end();
    accountOobeEnter();
  }
}

// ---------------- Fechas localizadas ----------------
static void buildLongDate(char* out, size_t n){
  int li = LI();
  const char* wd = WD_FULL[li][rtcWd];
  const char* mo = MO_FULL[li][rtcMo - 1];
  switch(cfgLang){
    case 0: case 3: snprintf(out, n, "%s, %d de %s", wd, rtcD, mo); break; // ES, PT
    case 2: case 4: snprintf(out, n, "%s %d %s", wd, rtcD, mo); break;     // FR, IT
    default:        snprintf(out, n, "%s, %s %d", wd, mo, rtcD); break;    // EN / ZH
  }
}
static void buildShortDate(char* out, size_t n){
  int li = LI();
  snprintf(out, n, "%s, %d %s", WD_SHORT[li][rtcWd], rtcD, MO_SHORT[li][rtcMo - 1]);
}

// ---------------- LOCK ----------------
// Barra de gestos (estilo iOS): pildora fina centrada cerca del borde inferior.
// yBottom = borde inferior de referencia (normalmente SCR_H). col por defecto blanca.
static void drawHomeIndicator(int yBottom, uint8_t alpha, uint16_t col = TH_ONWALL){
  int barW = 130, barH = 5, radius = 2;
  int x = (SCR_W - barW) / 2;
  int y = yBottom - 20 - barH;          // 20 px de margen desde el borde
  fillRoundRectA(x, y, barW, barH, radius, col, alpha);
}


// Glifos pequeños para las tarjetas de widgets del bloqueo. Copia minima e
// independiente de los de RI_* (Ajustes -> drawRowGlyph), que se definen
// mas abajo en el archivo; asi esta funcion no depende de nada definido
// despues de ella (evita sorpresas con el auto-prototipado de Arduino).
// kind: 0 = nube (clima), 1 = calendario, cualquier otro = campana (notif).
static void lockGlyph(int kind, int cx, int cy, uint16_t col){
  switch(kind){
    case 0:
      fillCircle(cx - 4, cy + 2, 5, col); fillCircle(cx + 4, cy + 2, 6, col);
      fillCircle(cx, cy - 2, 6, col); fillRect(cx - 8, cy + 2, 16, 5, col); break;
    case 1:
      drawRoundRect(cx - 9, cy - 8, 18, 17, 3, col); fillRect(cx - 9, cy - 8, 18, 5, col);
      fillCircle(cx - 4, cy + 2, 1, col); fillCircle(cx + 3, cy + 2, 1, col); break;
    default:   // campana (notificaciones) -- domo + cuerpo conico + reborde + badajo
      fillCircle(cx, cy - 6, 5, col);
      fillTriangle(cx - 8, cy + 4, cx + 8, cy + 4, cx, cy - 6, col);
      fillRect(cx - 9, cy + 3, 18, 3, col);
      fillCircle(cx, cy + 9, 2, col); break;
  }
}
// Tarjeta compacta para un widget opcional del bloqueo (clima/calendario/
// notificaciones). Estilo Vidrio u overlay plano segun uiGlass, igual que
// el resto de superficies de la app.
static void lockWidgetCard(int y, int kind, const char* title, const char* val, uint16_t accent){
  int x = 28, w = SCR_W - 56, h = 50;
  // Superficie del TEMA sobre el wallpaper: casi opaca (deja asomar el fondo,
  // que es lo que le daba identidad) pero con el color y el texto de la paleta
  // activa, asi la tarjeta se lee igual de bien en claro y en oscuro.
  if(uiGlass){ drawLiquidGlassPanel(x, y, w, h, 16, TH_GLASS2); }
  else fillRoundRectA(x, y, w, h, 16, TH_SURF, 215);
  lockGlyph(kind, x + 30, y + h / 2, accent);            // accent = color de CONTENIDO del widget
  drawText(x + 54, y + 9, title, 2, TH_TXT);
  drawText(x + 54, y + 30, val, 1, TH_TXT2);
}

static void renderLock(){
  drawWallpaperRowsId(lockBuf, gWallLock, false, 0, SCR_H - 1); setBuf(lockBuf);   // el bloqueo tiene su propio fondo
  // Barra de estado y reloj van DIRECTAMENTE sobre el wallpaper -> TH_ONWALL
  // (ver la nota de la excepcion en el bloque TEMA SEMANTICO).
  drawWifi(SCR_W - 66, 40, 12, TH_ONWALL);
  drawBattery(SCR_W - 46, 31, 30, 15, 82, TH_ONWALL);
  if(gLockWidgets & LW_CLOCK){
    if(uiGlass){ drawLiquidGlassPanel(28, 198, SCR_W - 56, 252, 28, TH_GLASS2); }  // vidrio tras el reloj
    char cs[8]; clkStr12(cs, sizeof(cs));
    drawBigClock(cs, SCR_W / 2, 242, 140, 18, TH_ONWALL);
    char ds[64]; buildLongDate(ds, sizeof(ds));
    drawTextC(SCR_W / 2, 242 + 140 + 36, ds, 3, TH_ONWALL);
  }
  // widgets opcionales (clima/calendario/notificaciones), apilados debajo
  // del reloj -- o mas arriba si el reloj esta desactivado, para no dejar
  // media pantalla vacia. El panel del reloj (y=198,h=252) termina en
  // y=450 -- el primer widget tiene que empezar DESPUES de eso, con margen
  // (antes empezaba en 448 y se solapaba 2px con el panel: el corte raro
  // que se ve en la foto justo debajo de la fecha).
  int wy = (gLockWidgets & LW_CLOCK) ? 462 : 200;
  if(gLockWidgets & LW_WEATHER){
    wxLockCard(wy);          // datos REALES del WeatherState (cero red al componer el bloqueo)
    wy += 60;
  }
  if(gLockWidgets & LW_CAL){
    lockWidgetCard(wy, 1, appName(IC_CALEND), t(S_NOEVENTS), rgb565(235,110,90));  // mock: sin eventos reales aun
    wy += 60;
  }
  if(gLockWidgets & LW_NOTIF){
    const char* val = gNotifCount > 0 ? gNotifs[gNotifCount - 1].mod.name : t(S_NONOTIFS);  // dato real
    lockWidgetCard(wy, 2, t(S_NOTIFS), val, rgb565(230,180,90));
    wy += 60;
  }
  fillRoundRect(SCR_W / 2 - 70, SCR_H - 150, 140, 10, 5, TH_ONWALL);
  drawTextC(SCR_W / 2, SCR_H - 118, t(S_SWIPE), 2, TH_ONWALL);
  setBuf(fb);
}
static void showLock(){ blitToFb(lockBuf); flxFlushAll(); }

// ---------------- HOME ----------------
// Geometria unica de los dos widgets fijos. La usa tanto el dibujo como el
// toque para que Clima y Calendario nunca tengan zonas pulsables desplazadas.
#define HOME_FW_X       24
#define HOME_FW_Y       72
#define HOME_FW_W       208
#define HOME_FW_H       120
#define HOME_FW_GAP     16
#define HOME_CAL_X      (HOME_FW_X + HOME_FW_W + HOME_FW_GAP)

static int homeFixedWidgetAppAt(int px, int py){
  if(py < HOME_FW_Y || py >= HOME_FW_Y + HOME_FW_H) return -1;
  if(px >= HOME_FW_X && px < HOME_FW_X + HOME_FW_W) return IC_CLIMA;
  if(px >= HOME_CAL_X && px < HOME_CAL_X + HOME_FW_W) return IC_CALEND;
  return -1;
}

// Calendario real del mes actual. No mantiene una fecha paralela ni inventa
// eventos: consume rtcY/rtcMo/rtcD/rtcWd, la misma fuente sincronizada por NTP
// que usan la app Calendario, el bloqueo y la barra del sistema.
static void calHomeWidget(int x, int y, int w, int h){
  uint16_t base;
  if(uiGlass){
    drawLiquidGlassPanel(x, y, w, h, 20, TH_GLASS2);
    base = TH_GLASS2;
  } else {
    // En modo Plano la tarjeta es gris, con una variante clara u oscura para
    // conservar contraste al respetar la apariencia elegida por el usuario.
    base = gDark ? rgb565(62,66,74) : rgb565(216,219,224);
    fillRoundRect(x, y, w, h, 20, base);
  }
  uint16_t fg = onColor(base), muted = mix565(fg, base, 104);

  char title[32];
  snprintf(title, sizeof(title), "%s %d", MO_SHORT[LI()][rtcMo - 1], rtcY);
  drawText(x + 12, y + 9, title, 2, fg);

  // Iniciales localizadas, domingo primero, igual que rtcWd y la app completa.
  static const char* const wd1[5][7] = {
    {"D","L","M","M","J","V","S"},
    {"S","M","T","W","T","F","S"},
    {"D","L","M","M","J","V","S"},
    {"D","S","T","Q","Q","S","S"},
    {"D","L","M","M","G","V","S"},
  };
  int li = LI(), pad = 9, cw = (w - 2 * pad) / 7;
  int weekY = y + 33;
  for(int i = 0; i < 7; i++) drawTextC(x + pad + i * cw + cw / 2, weekY, wd1[li][i], 1, muted);

  int first = ((rtcWd - (rtcD - 1)) % 7 + 7) % 7;
  int dim = daysInMonth(rtcY, rtcMo);
  int gridY = y + 49, rh = (h - 54) / 6;
  if(rh < 9) rh = 9;
  for(int d = 1; d <= dim; d++){
    int cell = first + d - 1, row = cell / 7, col = cell % 7;
    int cx = x + pad + col * cw + cw / 2;
    int cy = gridY + row * rh + rh / 2;
    bool today = (d == rtcD);
    if(today) fillCircle(cx, cy, 8, TH_PRIM);
    char ds[4]; snprintf(ds, sizeof(ds), "%d", d);
    drawTextC(cx, cy - 4, ds, 1, today ? TH_ONACC : fg);
  }
}

// Clima y Calendario comparten la franja superior; el material de ambos sigue
// el mismo uiGlass global elegido en Personalizacion.
static void drawHomeWidgets(uint32_t tm){
  (void)tm;
  wxHomeWidget(HOME_FW_X, HOME_FW_Y, HOME_FW_W, HOME_FW_H, false);
  calHomeWidget(HOME_CAL_X, HOME_FW_Y, HOME_FW_W, HOME_FW_H);
  int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96;
  if(uiGlass) drawLiquidGlassPanel(dkx, dky, dkw, dkh, 28, TH_GLASS2);
  else fillRoundRectA(dkx, dky, dkw, dkh, 28, TH_SURF, 90);
  int dS = 64, inner = dkw - 32, dgap = (inner - 4 * dS) / 3;
  for(int i = 0; i < 4; i++){ int ix = dkx + 16 + i * (dS + dgap), iy = dky + (dkh - dS) / 2; drawAppIcon(12 + i, ix, iy, dS); }
}

// GEOMETRIA DE LA REJILLA. Estaba repetida como numeros sueltos en
// renderHome, hitHomeIcon, edSlotXY, edSlotAt y getIconRect: cinco
// copias de "24, 212, 120, 112, 72" que habia que cambiar a la vez o
// dejar iconos que se ven en un sitio y responden en otro. Ahora sale
// de aqui, y la banda de la rejilla (HOME_BAND_*) es ademas lo unico
// que hay que recomponer al pasar de pagina.
#define HOME_ICON_S   72
#define HOME_GX0      24
#define HOME_GY0      212
#define HOME_COLSTEP  120
#define HOME_ROWSTEP  112
#define HOME_DOTS_Y   (HOME_GY0 + 2 * HOME_ROWSTEP + HOME_ICON_S + 34)   // 542
// Franja que cambia al pasar de pagina: rejilla + etiquetas + puntos.
// Todo lo de arriba (barra de estado, widgets) y lo de abajo (dock,
// barra de navegacion) es identico en todas las paginas, asi que el
// deslizamiento solo mueve esta banda -- ni un pixel mas.
#define HOME_BAND_TOP (HOME_GY0 - 6)                                     // 206
// El borde INFERIOR de la banda depende de la rejilla (con cuatro filas los
// puntos bajan), asi que es una funcion. El cache del deslizamiento se reserva
// para el caso MAS ALTO: asi cambiar de rejilla no obliga a reservar de nuevo.
// 596 = banda de la rejilla MAS ALTA (5x4). Es un limite con consecuencia
// medible: hpBuf + hpBg tienen que seguir cabiendo en menos de lo que ocupa un
// framebuffer completo, o el cache del deslizamiento dejaria de ser una mejora.
// Lo comprueba tests/host (testDeslizarPaginas).
#define HOME_BAND_BOT_MAX 596
#define HOME_BAND_H   (HOME_BAND_BOT_MAX - HOME_BAND_TOP)

// GEOMETRIA DE LA REJILLA ACTIVA. Con los valores de fabrica (4x3, iconos
// normales) devuelve EXACTAMENTE los numeros de siempre -- S=72, gx0=24,
// gy0=212, paso 120x112 --, asi que una placa que actualice no ve moverse ni
// un icono hasta que el usuario cambie la rejilla a mano.
static void homeGrid(int &S, int &gx0, int &gy0, int &cstep, int &rstep, int &cols, int &rows){
  static const int ICON_PX[3] = { 60, 72, 84 };
  cols = gHomeCols;
  if(cols < 4) cols = 4;
  if(cols > HOME_COLS_MAX) cols = HOME_COLS_MAX;
  rows = gHomeRows;
  if(rows < 3) rows = 3;
  if(rows > HOME_ROWS_MAX) rows = HOME_ROWS_MAX;
  S     = ICON_PX[gHomeIconSz > 2 ? 1 : gHomeIconSz];
  cstep = SCR_W / cols;                          // 4 columnas -> 120 · 5 -> 96
  gy0   = HOME_GY0;
  rstep = (rows >= 4) ? 92 : HOME_ROWSTEP;       // con cuatro filas la banda se aprieta
  // El icono nunca invade la celda vecina ni pisa la etiqueta de la fila de abajo.
  if(S > cstep - 20) S = cstep - 20;
  if(S > rstep - 26) S = rstep - 26;
  if(S < 44) S = 44;
  gx0 = (cstep - S) / 2;
}
static int homeDotsY(){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  // Con cuatro filas los puntos se acercan a la ultima etiqueta (24 en vez de
  // 34): asi la banda movil no crece tanto que el cache del deslizamiento pase
  // a costar mas que un framebuffer entero.
  return gy0 + (rows - 1) * rs + S + (rows >= 4 ? 24 : 34);   // 4x3 normales -> 542, el de siempre
}
static int homeBandBot(){
  int y = homeDotsY() + 18;
  return y > HOME_BAND_BOT_MAX ? HOME_BAND_BOT_MAX : y;
}
static inline void homeSlotXY(int slot, int &x, int &y){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  if(cols < 1) cols = 1;
  int c = slot % cols, r = slot / cols;
  x = gx0 + c * cs;
  y = gy0 + r * rs;
}

// Cede el nucleo durante composiciones largas. Ademas de alimentar loopTask,
// el vTaskDelay deja correr al idle task que tambien vigila el TWDT del P4.
// Se usa entre iconos, nunca dentro de un bucle por pixel.
static void uiRenderCooperate(){
  if(esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(1));
}

// Dibuja la rejilla de UNA pagina en el buffer activo. No limpia el
// fondo: quien llama ya puso ahi el wallpaper. cooperative=true se usa al
// preparar una pagina fuera de pantalla: doce iconos Liquid Glass seguidos no
// pueden monopolizar loopTask hasta disparar TASK_WDT.
static void homeDrawGridWork(int page, int xoff, bool cooperative){
  if(page < 0 || page >= gHomePageN) return;
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  // Con cinco columnas la celda mide 96 px: un nombre largo a tamano 2 invadiria
  // la celda vecina, asi que baja a tamano 1. Con la rejilla de siempre (celda
  // de 120) no cambia nada.
  int lblSz = (cs >= 110) ? 2 : 1;
  int n = homeSlotCount();
  for(int i = 0; i < n; i++){
    uint8_t id = homeOrder[homeIdx(page, i)];
    if(id == HOME_EMPTY) continue;
    int ix, iy; homeSlotXY(i, ix, iy);
    ix += xoff;
    // Fuera de pantalla del todo: ni se dibuja. Durante el arrastre
    // esto ahorra la mitad de los iconos en cuanto la pagina lleva
    // medio recorrido, que es justo cuando mas hay que ir rapido.
    if(ix + S < 0 || ix > SCR_W) continue;
    drawAppIcon(id, ix, iy, S);
    if(gHomeLabels) drawTextC(ix + S / 2, iy + S + 6, appName(id), lblSz, TH_ONWALL);
    if(cooperative && (i & 1)) uiRenderCooperate();
  }
}

// Los tres puntos. El activo va opaco y algo mas gordo; los otros,
// tenues. Con `frac` (0..1 del recorrido hacia `to`) el punto se
// desplaza CON el dedo en vez de saltar al soltar: es el detalle que
// hace que el indicador se sienta parte del gesto y no un aviso
// posterior.
static void homeDrawDots(int from, int to, float frac){
  if(!gHomeDots) return;
  int n = gHomePageN, dy = homeDotsY();
  int x0 = SCR_W / 2 - (n - 1) * 9;
  for(int i = 0; i < n; i++){
    if(i == gHomeMain){
      // La pagina PRINCIPAL se marca con una casita en vez de un punto: es la
      // misma convencion que usa el modo de personalizacion, asi que el usuario
      // ve en el escritorio lo mismo que eligio alli.
      int x = x0 + i * 18;
      fillTriangle(x, dy - 7, x - 6, dy - 1, x + 6, dy - 1, TH_ONWALL);
      fillRect(x - 4, dy - 1, 8, 6, TH_ONWALL);
    } else fillCircleA(x0 + i * 18, dy, 4, TH_ONWALL, 110);
  }
  float pos = (float)from + ((float)(to - from)) * frac;
  int   cx  = (int)(x0 + pos * 18.0f + 0.5f);
  if(from != gHomeMain || to != gHomeMain) fillCircleA(cx, dy, 5, TH_ONWALL, 255);
}

// Compone el escritorio de `page` ENTERO en `dst`. Es la funcion que
// antes se llamaba renderHome y solo sabia de una pagina.
static void renderHomeInto(uint16_t* dst, int page){
  if(!dst) return;
  uint16_t* old = gBuf;
  drawWallpaper(dst, true); setBuf(dst);
  // barra de estado. La hora y la capsula del cronometro salen de la MISMA
  // funcion (cronoBarClock) que usa el marco de las apps: es el unico sitio
  // donde se decide esa geometria, asi que las dos barras no pueden divergir.
  cronoBarClock(16, TH_ONWALL);
  char sd[48]; buildShortDate(sd, sizeof(sd));
  drawText(20, 40, sd, 1, TH_ONWALL2);
  drawWifi(SCR_W - 66, 28, 11, TH_ONWALL);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, TH_ONWALL);
  // widgets de clima/calendario + dock: estilo Liquid Glass o plano
  drawHomeWidgets(millis());
  // rejilla de apps 4x3 de ESTA pagina. homeOrder[] puede tener ranuras
  // vacias (HOME_EMPTY) desde que existe la Caja de aplicaciones: quitar una
  // app de Inicio deja su hueco, no recoloca el resto de la rejilla.
  if(!editMode){
    homeDrawWidgets(page, 0);                   // widgets colocados de ESTA pagina
    homeDrawGridWork(page, 0, true);            // cede entre iconos: nunca monopoliza loopTask
  }
  homeDrawDots(page, page, 0.0f);
  // barra de navegacion: botones clasicos o barra de gestos (modo iOS)
  if(gNavMode == 0){
    int ny = SCR_H - 52; uint16_t nv = TH_ONWALL;
    int bx = SCR_W / 6;
    fillTriangle(bx - 10, ny + 8, bx + 8, ny - 2, bx + 8, ny + 18, nv);   // atras
    drawCircle(SCR_W / 2, ny + 8, 12, nv); drawCircle(SCR_W / 2, ny + 8, 11, nv); // inicio
    int rx = SCR_W * 5 / 6;
    drawRoundRect(rx - 11, ny - 3, 22, 22, 4, nv);                        // recientes
  } else {
    drawHomeIndicator(SCR_H, 220);                                        // barra de gestos
  }
  if(gSafeMode){
    fillRoundRect(146, 56, 188, 38, 19, rgb565(186,112,48));
    drawTextC(SCR_W / 2, 66, "Modo seguro", 2, rgb565(255,255,255));
  }
  setBuf(old);
}

static void renderHome(){
  gHomeDirty = false;                      // homeBuf ya refleja los ajustes actuales
  qsDirty    = true;                       // la cortina se compone de homeBuf: invalidar su cache
  renderHomeInto(homeBuf, gHomePage);
  setBuf(fb);
}
// Vuelca el escritorio ENTERO. Quien llama a esto se queda la pantalla, asi que
// ninguna capa de transicion puede sobrevivirle: se cancela aqui, en el unico
// punto por el que pasan las trece rutas que vuelven a Inicio desde otra
// pantalla (Recientes, Caja de apps, menu contextual, seguridad...), en vez de
// tener que acordarse en cada una. Las rutas que SI deben conservar la capa
// (homeTick y el cambio de minuto) ya no llaman aqui mientras dura.
static void showHome(){ appTrCancel(); blitToFb(homeBuf); flxFlushAll(); }
static bool hitHomeIcon(int px, int py, int &id){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  int n = homeSlotCount();
  if(homeWgAt(gHomePage, px, py) >= 0) return false;   // un widget colocado gana siempre
  for(int i = 0; i < n; i++){
    uint8_t a = homeOrder[homeIdx(gHomePage, i)];
    if(a == HOME_EMPTY) continue;          // hueco: no hay nada que tocar ahi
    int ix, iy; homeSlotXY(i, ix, iy);
    if(px >= ix - 6 && px <= ix + S + 6 && py >= iy && py <= iy + S + 16){ id = a; return true; }
  }
  int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96, dS = 64, inner = dkw - 32, dgap = (inner - 4 * dS) / 3;
  for(int i = 0; i < 4; i++){
    int ix = dkx + 16 + i * (dS + dgap), iy = dky + (dkh - dS) / 2;
    if(px >= ix && px <= ix + dS && py >= iy && py <= iy + dS){ id = 12 + i; return true; }
  }
  return false;
}

// #############################################################
// ##  DESLIZAMIENTO ENTRE PAGINAS DEL ESCRITORIO
// ##  ------------------------------------------------------
// ##  COMO NO SE HACE: recomponer el escritorio entero en cada frame
// ##  del arrastre. Serian dos wallpapers, dos juegos de widgets con
// ##  su Liquid Glass y hasta 24 iconos vectoriales por cuadro -- muy
// ##  lejos de poder seguir al dedo.
// ##
// ##  COMO SE HACE: se aprovecha que las dos paginas comparten TODO
// ##  menos la rejilla. La barra de estado, los widgets, el dock y la
// ##  barra de navegacion son identicos, asi que:
// ##
// ##    · la pagina vecina se compone UNA sola vez, al empezar el
// ##      gesto, en un cache COMPACTO de solo la banda movil (hpBuf),
// ##      y el wallpaper limpio se conserva aparte en hpBg;
// ##    · cada frame restaura hpBg SIN desplazar y mueve encima solo
// ##      los pixeles que difieren de ese fondo en homeBuf/hpBuf;
// ##      por eso viajan iconos y etiquetas, pero nunca el wallpaper;
// ##    · los tres puntos se redibujan fijos encima;
// ##    · el resto de la pantalla NI SE TOCA: fuera de esa banda, fb
// ##      ya es correcto.
// ##
// ##  Todo sigue precompuesto: durante el gesto no se recalcula el
// ##  degradado, el blur ni ningun icono vectorial.
// ##
// ##  SIN PSRAM PARA hpBuf no hay animacion, pero SI hay cambio de
// ##  pagina: se salta directo a la pagina destino. Se degrada, no se
// ##  rompe.
// #############################################################
#define HP_DRAG_MIN     18        // px que hay que recorrer para que esto sea un arrastre de pagina
#define HP_SETTLE_MS   190        // duracion del acomodo al soltar
#define HP_FLICK_MS    320        // por debajo de esto, un gesto corto cuenta como golpe seco
#define HP_FLICK_PX     42        // ...y con esta distancia minima
static uint16_t* hpBuf     = NULL;     // cache compacto: HOME_BAND_H filas de la pagina vecina
static uint16_t* hpBg      = NULL;     // wallpaper limpio de la banda: nunca se desplaza
static int       hpBufPage = -1;       // que pagina hay compuesta ahi
static bool      hpDragging = false;
static int       hpDx      = 0;        // desplazamiento actual del dedo (px, + = hacia la derecha)
static int       hpFrom = 0, hpTo = 0; // paginas implicadas
static bool      hpSettling = false;
static uint32_t  hpSettleT0 = 0;
static int       hpSettleFrom = 0;     // dx del que arranca el acomodo
static int       hpSettleTo   = 0;     // dx al que llega (0 = se queda, -+SCR_W = cambia)
static uint32_t  hpFrameMs = 0;

#define HP_FRAME_MS      33        // 30 fps estables: no saturar PSRAM + DMA2D
// Reserva: la banda MAS ALTA posible (rejilla de cuatro filas). Lo que se copia
// de verdad es solo la banda ACTIVA -- hpBandPixels() --, porque copiar de mas
// escribiria en homeBuf filas que ya no pertenecen a la banda y borraria el dock.
#define HP_BUF_PIXELS    ((size_t)SCR_W * HOME_BAND_H)
static inline size_t hpBandPixels(){ return (size_t)SCR_W * (size_t)(homeBandBot() - HOME_BAND_TOP); }

static bool hpEnsureBuf(){
  if(hpBuf && hpBg) return true;
  if(!hpBuf)
    hpBuf = (uint16_t*)heap_caps_aligned_alloc(64, HP_BUF_PIXELS * 2,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!hpBg)
    hpBg = (uint16_t*)heap_caps_aligned_alloc(64, HP_BUF_PIXELS * 2,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  hpBufPage = -1;
  return hpBuf != NULL && hpBg != NULL;
}

// Deja compuesta en hpBuf la pagina `page`. Devuelve false si no hay
// PSRAM: el llamante entonces cambia de pagina sin animar.
static bool hpPrepare(int page){
  if(page < 0 || page >= gHomePageN) return false;
  if(!hpEnsureBuf()) return false;
  if(hpBufPage == page) return true;      // ya esta: ir y volver no recompone

  // La banda de iconos no contiene widgets, dock ni barra de estado: debajo
  // solo hay wallpaper. Se regenera ESA banda en bbuf, se dibuja la rejilla y
  // se guarda compacta. Antes se rehacia una pantalla completa de 768 KB aqui,
  // justo al reconocer el gesto, con blur de widgets y dock incluidos; era el
  // pico que congelaba el dedo y podia dejar al TWDT sin respirar.
  drawWallpaperRows(bbuf, true, HOME_BAND_TOP, homeBandBot() - 1);
  // El fondo vive separado de las paginas. Si se desplazara la imagen ya
  // compuesta (wallpaper + iconos), el degradado y sus manchas viajarian con
  // cada pagina y aparecerian costuras verticales, exactamente como en la
  // grabacion. Esta copia se hace UNA vez al empezar el gesto; cada frame solo
  // restaura filas mediante memcpy y mueve los pixeles que difieren del fondo.
  memcpy(hpBg, bbuf + (size_t)HOME_BAND_TOP * SCR_W, hpBandPixels() * 2);
  uiRenderCooperate();                    // el fondo ya recorrio ~170k pixeles
  uint16_t* old = gBuf; setBuf(bbuf);
  int cx0 = gClipX0, cx1 = gClipX1, cy0 = gClipY0, cy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
  gClipY0 = HOME_BAND_TOP; gClipY1 = homeBandBot() - 1;
  homeDrawWidgets(page, 0);
  homeDrawGridWork(page, 0, true);
  homeDrawDots(page, page, 0.0f);
  uiRenderCooperate();                    // tambien cede si la pagina esta vacia
  gClipX0 = cx0; gClipX1 = cx1; gClipY0 = cy0; gClipY1 = cy1;
  setBuf(old);
  memcpy(hpBuf, bbuf + (size_t)HOME_BAND_TOP * SCR_W, hpBandPixels() * 2);
  hpBufPage = page;
  return true;
}

// Un frame del arrastre/acomodo. `dx` es lo que se ha movido la pagina
// ACTUAL; la vecina va pegada a su lado.
// VIEWPORT DE CADA PAGINA. Las dos paginas se mueven juntas pero cada
// una solo puede escribir en SU tramo de columnas, y los dos tramos son
// complementarios: juntos cubren [0, SCR_W) exactamente una vez. Esto se
// calcula aqui, en un solo sitio, en vez de deducirlo dos veces con
// signos distintos dentro del bucle de filas -- que es como se acaba con
// una franja que nadie escribe y por tanto conserva el frame anterior.
//
//   dst   primera columna de PANTALLA que le toca
//   src   primera columna de su LIENZO que se copia ahi
//   w     cuantas columnas (0 = esta pagina no se ve en este frame)
//
// Con parametros de salida y no una estructura: un tipo propio en la
// firma tendria que estar definido antes del bloque de prototipos que
// autogenera el IDE de Arduino (lo vigila tests/host/check_protos.py), y
// no vale la pena subir un detalle de tres enteros hasta alli.
static inline void hpViewport(int off, int &dst, int &src, int &w){
  if(off <= -SCR_W || off >= SCR_W){ dst = 0; src = 0; w = 0; return; }
  dst = off > 0 ?  off : 0;
  src = off < 0 ? -off : 0;
  w   = SCR_W - (off < 0 ? -off : off);
  if(dst + w > SCR_W) w = SCR_W - dst;              // por si acaso: nunca fuera del ancho
}

static void hpRenderFrame(int dx){
  if(!homeBuf || !hpBg) return;
  setBuf(bbuf);
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  int dir = (hpTo > hpFrom) ? 1 : -1;     // +1 = la vecina entra por la derecha
  int nx  = dx + dir * SCR_W;             // desplazamiento de la vecina
  int aDst, aSrc, aW; hpViewport(dx, aDst, aSrc, aW);
  int bDst = 0, bSrc = 0, bW = 0;
  if(hpBuf && hpBufPage == hpTo) hpViewport(nx, bDst, bSrc, bW);
  int bandBot = homeBandBot(), dotsY = homeDotsY();
  for(int y = HOME_BAND_TOP; y < bandBot; y++){
    uint16_t* row = bbuf + (size_t)y * SCR_W;
    const uint16_t* bg = hpBg + (size_t)(y - HOME_BAND_TOP) * SCR_W;
    // El wallpaper se restaura SIN DESPLAZAR en todas las columnas. Encima se
    // copian solo los pixeles de primer plano de cada pagina: un pixel forma
    // parte de un icono/etiqueta si difiere del wallpaper limpio en SU
    // coordenada de origen. Asi los iconos siguen al dedo y el fondo queda
    // anclado a la pantalla.
    memcpy(row, bg, (size_t)SCR_W * 2);
    // La franja de puntos se recompone fija mas abajo; no debe viajar como si
    // perteneciera a una pagina.
    if(y < dotsY - 8 || y > dotsY + 10){
      if(aW > 0){
        const uint16_t* src = homeBuf + (size_t)y * SCR_W + aSrc;
        for(int x = 0; x < aW; x++) if(src[x] != bg[aSrc + x]) row[aDst + x] = src[x];
      }
      if(bW > 0){
        const uint16_t* src = hpBuf + (size_t)(y - HOME_BAND_TOP) * SCR_W + bSrc;
        for(int x = 0; x < bW; x++) if(src[x] != bg[bSrc + x]) row[bDst + x] = src[x];
      }
    }
  }
  // Los puntos NO se desplazan con las paginas: se quedan fijos y lo
  // que se mueve es cual esta encendido. Van encima de la banda ya
  // compuesta, asi que hay que repintar su fondo primero -- que es la
  // fila de la pagina de origen sin desplazar.
  { float frac = (float)(dx < 0 ? -dx : dx) / (float)SCR_W;
    if(frac > 1.0f) frac = 1.0f;
    homeDrawDots(hpFrom, hpTo, frac); }
  present(HOME_BAND_TOP, bandBot - 1);
  setBuf(fb);
}

// Salta a `page` sin animar (sin PSRAM, o desde la Caja de apps).
static void homeGoPage(int page){
  if(page < 0) page = 0;
  if(page >= gHomePageN) page = gHomePageN - 1;
  if(page == gHomePage) return;
  gHomePage = page;
  renderHome();
  showHome();
}

// Empieza un arrastre si el gesto lo es de verdad. Devuelve true si a
// partir de ahora el toque es suyo.
static bool hpTryStart(){
  if(hpDragging || hpSettling || editMode) return false;
  if(!T.down) return false;
  int dx = T.x - T.startX, dy = T.y - T.startY;
  int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  // Horizontal DE VERDAD: mas del doble que la componente vertical.
  // Sin esta condicion, el gesto de abrir la Caja de aplicaciones
  // (hacia arriba, casi nunca perfectamente recto) empezaria a
  // arrastrar la pagina en cuanto se torciera un poco.
  if(adx < HP_DRAG_MIN || adx < ady * 2) return false;
  // Solo dentro de la banda de la rejilla: sobre los widgets, el dock
  // o la barra de navegacion el gesto sigue siendo de quien era.
  if(T.startY < HOME_BAND_TOP || T.startY >= homeBandBot()) return false;
  int to = gHomePage + (dx < 0 ? 1 : -1);
  if(to < 0 || to >= gHomePageN){
    // Borde: no hay pagina a ese lado. No se arrastra -- ni siquiera
    // con resistencia: el escritorio tiene tres paginas y fingir que
    // hay una cuarta a medias solo confunde.
    return false;
  }
  hpFrom = gHomePage; hpTo = to;
  if(!hpPrepare(to)) return false;
  hpDragging = true;
  hpDx = dx;
  return true;
}

// Arranca el acomodo hacia `settle` (0 = vuelve, +-SCR_W = cambia).
static void hpStartSettle(int settle){
  hpDragging   = false;
  hpSettling   = true;
  hpSettleT0   = millis();
  hpSettleFrom = hpDx;
  hpSettleTo   = settle;
}

// Un paso del deslizamiento. Devuelve true mientras se quede el toque
// y la pantalla.
static bool hpTick(){
  if(hpSettling){
    // Acomodo por TIEMPO, no por pasos: a 30 o a 60 fps dura lo mismo.
    uint32_t e = millis() - hpSettleT0;
    if(e >= HP_SETTLE_MS){
      hpSettling = false;
      if(hpSettleTo != 0){
        gHomePage = hpTo;
        // Solo cambia la banda de rejilla. hpBuf es compacto, asi que se copia
        // a su posicion dentro de homeBuf; barra, widgets, dock y navegacion
        // permanecen intactos. Son ~340 KB una vez al terminar, frente a
        // intercambiar dos pantallas completas y conservar 768 KB de cache.
        if(hpBuf && hpBufPage == hpTo){
          memcpy(homeBuf + (size_t)HOME_BAND_TOP * SCR_W, hpBuf, hpBandPixels() * 2);
          hpBufPage = -1;
          qsDirty = true;      // la cortina se compone de homeBuf: su cache ya no vale
        } else renderHome();
      }
      // El ultimo frame puede no haber caido exactamente en +-SCR_W. Publicar
      // solo la banda deja el resultado final exacto sin otro flush de 768 KB.
      fbCopyBand(homeBuf, HOME_BAND_TOP, homeBandBot() - 1);
      flxFlush(HOME_BAND_TOP, homeBandBot() - 1);
      return true;
    }
    float p = (float)e / (float)HP_SETTLE_MS;
    float q = 1.0f - p; q = 1.0f - q * q * q;      // ease-out cubica, la del resto del sistema
    hpRenderFrame(hpSettleFrom + (int)((hpSettleTo - hpSettleFrom) * q));
    return true;
  }
  if(!hpDragging) return false;
  if(T.down){
    int dx = T.x - T.startX;
    // ACOTADO A UNA PAGINA Y A SU DIRECCION. Lo primero es obvio: mas
    // alla de una pantalla no hay nada que enseñar. Lo segundo no
    // tanto: una vez elegida la pagina destino, arrastrar hacia el
    // OTRO lado dejaria un hueco en pantalla sin nada detras -- la
    // pagina vecina compuesta esta a un solo lado --, y ese hueco
    // enseñaria lo que hubiera quedado en el buffer de composicion.
    // Se acota a cero por ese lado: la pagina se para, no se despega.
    int dir = (hpTo > hpFrom) ? 1 : -1;
    if(dir > 0){ if(dx > 0) dx = 0; if(dx < -SCR_W) dx = -SCR_W; }
    else       { if(dx < 0) dx = 0; if(dx >  SCR_W) dx =  SCR_W; }
    hpDx = dx;
    T.tap = false; T.swipeLeft = false; T.swipeRight = false; T.swipeUp = false; T.swipeDown = false;
    uint32_t now = millis();
    if(now - hpFrameMs >= HP_FRAME_MS){ hpFrameMs = now; hpRenderFrame(hpDx); }
    return true;
  }
  // Soltar: se acomoda a la pagina mas cercana, salvo que el gesto
  // haya sido un golpe seco -- corto pero rapido --, que cuenta como
  // intencion de cambiar aunque no haya llegado a la mitad.
  int adx = hpDx < 0 ? -hpDx : hpDx;
  unsigned long dur = millis() - T.downMs;
  bool flick  = (dur < HP_FLICK_MS && adx > HP_FLICK_PX);
  bool change = flick || adx > SCR_W / 4;
  int dir = (hpTo > hpFrom) ? 1 : -1;
  hpStartSettle(change ? -dir * SCR_W : 0);
  T.tap = false; T.released = false; T.swipeLeft = false; T.swipeRight = false;
  return true;
}

// ---------------- Desbloqueo con fisica (composicion) ----------------
static void composeUnlock(int off){
  if(off < 0) off = 0; if(off > SCR_H) off = SCR_H;
  for(int y = 0; y < SCR_H; y++){
    if(y < SCR_H - off)
      memcpy(fb + (size_t)y * SCR_W, lockBuf + (size_t)(y + off) * SCR_W, SCR_W * 2);
    else
      memcpy(fb + (size_t)y * SCR_W, homeBuf + (size_t)y * SCR_W, SCR_W * 2);
  }
  flxFlushAll();
}
// Deslizamiento del bloqueo. Antes eran 14 pasos fijos con delay(14) en medio:
// 14 frames en ~200 ms pasara lo que pasara, con el procesador parado la mitad
// del tiempo. Ahora es la MISMA duracion pero basada en tiempo y sin delay, asi
// que el bucle mete todos los frames que el compositor sea capaz de dar. Mismo
// recorrido y mismo ease-out; solo cambia la cadencia.
#define UNLOCK_ANIM_MS 200
static void animateTo(int from, int to){
  uint32_t t0 = millis();
  for(;;){
    uint32_t e = millis() - t0; if(e > (uint32_t)UNLOCK_ANIM_MS) e = UNLOCK_ANIM_MS;
    float p = (float)e / (float)UNLOCK_ANIM_MS;
    p = 1 - (1 - p) * (1 - p);                               // ease-out
    composeUnlock(from + (int)((to - from) * p));
    if(e >= (uint32_t)UNLOCK_ANIM_MS) break;
  }
  composeUnlock(to);
}
// Arranca la verificacion DESDE la pantalla de Bloqueo.
// Si el bloqueo lo puso el despertar de una suspension y antes habia una app
// abierta, se pide la verificacion con destino "abrir esa app": al acertar el
// PIN se vuelve exactamente donde estaba el usuario, no al escritorio. Se
// reutiliza LSU_AFTER_OPENAPP tal cual, sin inventar una ruta nueva.
static void lockStartVerify(){
#if SUSPEND_ON && SUSPEND_LOCK_ON
  int ret = gSuspRetState, app = gSuspRetApp;
  gSuspRetState = -1; gSuspRetApp = -1;      // se consume: solo vale para este desbloqueo
  if(ret == ST_APP && app >= 0){
    lsuStartVerifyFor(LSU_AFTER_OPENAPP, app);
    gLockVerifyLocked = true;                // (va DESPUES: lsuStartVerify resetea estado)
    return;
  }
#endif
  lsuStartVerify();
  gLockVerifyLocked = true;
}
static void lockTick(){
  if(gLockType > 0){
    // Con bloqueo: deslizar arriba lleva DIRECTO a verificar (nunca se revela el escritorio)
    if(T.down && (T.startY - T.y) > 60){ lockStartVerify(); return; }
    if(T.released && T.swipeUp){ lockStartVerify(); return; }
    return;
  }
  if(T.down){
    int off = T.startY - T.y; if(off < 0) off = 0; if(off > SCR_H) off = SCR_H;
    if(off != lastLockOff){ composeUnlock(off); lastLockOff = off; }
    lockOff = off;
  } else if(T.released){
    if(lockOff > SCR_H / 3 || T.swipeUp){ animateTo(lockOff, SCR_H); enterHome(); }
    else { animateTo(lockOff, 0); lockOff = 0; lastLockOff = -1; showLock(); }
  }
}
// #############################################################
// ##  MODO EDICION del Home (long-press, jiggle, drag & drop)
// #############################################################
static float edCurX[HOME_STRIDE], edCurY[HOME_STRIDE];   // posiciones animadas (resorte)
static int   edDrag = -1, edHoverSlot = -1; // icono arrastrado / slot bajo el dedo
static float edDragX = 0, edDragY = 0;
// Posicion del icono arrastrado, acotada al area de rejilla. Antes este limite
// solo se aplicaba en los frames de MOVIMIENTO: en el frame del agarre se
// escribia T.x-36 en crudo y el icono podia dibujarse hasta 36 px fuera.
static void edSetDrag(int tx, int ty){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  float dx = (float)(tx - S / 2), dy = (float)(ty - S / 2);
  if(dx < 8) dx = 8;
  if(dx > SCR_W - S - 8) dx = (float)(SCR_W - S - 8);
  if(dy < HOME_GY0 - 72) dy = (float)(HOME_GY0 - 72);
  if(dy > homeBandBot() - S - 10) dy = (float)(homeBandBot() - S - 10);
  edDragX = dx; edDragY = dy;
}
static unsigned long edHoverMs = 0, edMs = 0;
// Arrastrar un icono contra un borde durante 700 ms lo mueve a la pagina
// vecina. Es independiente del gesto normal de pasar pagina, que se desactiva
// mientras el escritorio esta en Modo Edicion.
static int      edEdgeDir = 0;
static int      edWDrag   = -1;      // widget de la pagina que se esta moviendo
static uint32_t edEdgeMs  = 0;

// PERSISTENCIA DEL ESCRITORIO Y DEL REGISTRO. Tres claves en la misma
// namespace "flexos" y una sola apertura de NVS por guardado: el orden de las
// ranuras (12 bytes) y los dos bitmasks. Se llama SOLO al cambiar algo (soltar
// un icono en Modo Edicion, o una accion del menu contextual de la caja), nunca
// por frame ni desde un bucle. Sin String: buffers fijos.
static void homeOrderSave(){
  prefs.begin("flexos", false);
  // Clave NUEVA ("hordp") para las 36 ranuras. La vieja ("hord", 12
  // bytes) se deja INTACTA a proposito: si el usuario vuelve a una
  // version anterior del firmware por USB, se encuentra su escritorio
  // de una pagina tal cual estaba en vez de una clave con un tamano
  // que esa version no sabe leer.
  // Clave "hordq" (100 B, paso fijo de 20 ranuras). Ver la cabecera del modelo:
  // "hordp" y "hord" quedan congeladas para poder bajar de version.
  prefs.putBytes("hordq", homeOrder, HOME_TOTAL);
  prefs.putInt("hpgn",  (int)gHomePageN);
  prefs.putInt("hpmain",(int)gHomeMain);
  prefs.putInt("hgrid", ((int)gHomeCols << 8) | (int)gHomeRows);
  prefs.putInt("hicon", (int)gHomeIconSz);
  prefs.putInt("hflag", (int)((gHomeLabels ? 1 : 0) | (gHomeLocked ? 2 : 0) |
                              (gHomeDots   ? 4 : 0) | (gHomePinch  ? 8 : 0) |
                              (gHomeReduce ? 16 : 0)));
  // Widgets: un blob de tamano FIJO con magic y version, validado entero al
  // cargar. 2 + 5 paginas x (1 + 3x5) = 82 B.
  { uint8_t wb[HOME_WG_BLOB]; homeWgSerialize(wb); prefs.putBytes("hwg", wb, HOME_WG_BLOB); }
  prefs.putInt("appn", APP_N);          // cuantas apps conocia este firmware (ver homeOrderLoad)
  prefs.putInt("appfav",  (int)gAppFav);
  prefs.putInt("apphide", (int)gAppHidden);
  prefs.end();
}
// PREFERENCIAS DE ASPECTO DEL INICIO (fondo, encuadre, tema integrado y
// paleta). Van aparte de homeOrderSave() porque se tocan en momentos muy
// distintos -- elegir un fondo no reordena iconos -- y asi ninguna de las dos
// reescribe claves de la otra. Una sola apertura de NVS por guardado y sin
// String: la ruta de la imagen es un buffer fijo.
static void homeCfgSave(){
  prefs.begin("flexos", false);
  prefs.putInt("wallh",  (int)gWallHome);
  prefs.putInt("walll",  (int)gWallLock);
  prefs.putInt("wallfit",(int)gWallFit);
  prefs.putBool("wallpal", gWallPalOn);
  prefs.putInt("hlook",  (int)gHomeLook);
  // Ruta de la imagen: se guarda como BYTES de tamano fijo, no como String.
  // putString/getString obligarian a un String en la ruta de carga, y esta
  // corre en el arranque; con putBytes el buffer es siempre el mismo char[80].
  prefs.putBytes("wallpb", gWallPath, sizeof(gWallPath));
  prefs.end();
}
static void homeCfgLoad(){
  prefs.begin("flexos", true);
  int wh = prefs.getInt("wallh", 0), wl = prefs.getInt("walll", 0);
  int wf = prefs.getInt("wallfit", 0), lk = prefs.getInt("hlook", 0);
  gWallPalOn = prefs.getBool("wallpal", false);
  char pth[sizeof(gWallPath)];
  memset(pth, 0, sizeof(pth));
  size_t pn = prefs.getBytes("wallpb", pth, sizeof(pth));
  prefs.end();
  // Terminador SIEMPRE, pase lo que pase con lo que hubiera en NVS: si la clave
  // no existe (pn == 0) o trae basura sin cerrar, la ruta queda vacia y el
  // fondo cae al integrado por defecto.
  if(pn == 0 || pn > sizeof(pth)) pth[0] = 0;
  pth[sizeof(pth) - 1] = 0;
  // Acotado SIEMPRE: unas prefs corruptas no pueden dejar un fondo inexistente.
  gWallHome = (wh == WALL_IMG || (wh >= 0 && wh < WALL_N)) ? (uint8_t)wh : 0;
  gWallLock = (wl == WALL_IMG || (wl >= 0 && wl < WALL_N)) ? (uint8_t)wl : 0;
  gWallFit  = (wf >= 0 && wf <= 2) ? (uint8_t)wf : 0;
  gHomeLook = (lk >= 0 && lk < LOOK_N) ? (uint8_t)lk : 0;
  snprintf(gWallPath, sizeof(gWallPath), "%s", pth);
  // Si la imagen ya no se puede cargar (archivo borrado, ya no es un JPEG, sin
  // PSRAM), wallEnsureImage vuelve al fondo integrado por defecto SIN reiniciar.
  wallEnsureImage();
  if(gWallHome != WALL_IMG && gWallLock != WALL_IMG && !gWallPalOn){
    gWallAcc  = lookAcc(gHomeLook);
    gWallAcc2 = lookAcc2(gHomeLook);
    gWallPalOk = true;
  }
}

// Valores de fabrica: se derivan del campo 'dflt' de APP_REG, que es el unico
// sitio donde se declara que apps nacen en el escritorio. APP_REG esta definido
// mas abajo en el archivo, asi que esto se declara aqui y se define alli.
static void drawerRegistryDefaults();
// Aplica el valor de fabrica SOLO a las apps con id >= fromId. Se define
// junto a APP_REG (mas abajo) porque de ahi sale el campo 'dflt'.
static void drawerRegistryAdopt(int fromId);
// Deja homeOrder[] y los bitmasks en un estado COHERENTE pase lo que pase con
// lo que hubiera en NVS (version anterior del firmware, prefs corruptas, una
// app retirada del registro). Reglas, en este orden:
//   1. una ranura con un id fuera de 0..15, repetido, oculto o no favorito -> se vacia;
//   2. toda app favorita que no tenga ranura -> ocupa el primer hueco;
//   3. si ya no quedan huecos, se le quita la marca de favorita (las tres
//      paginas tienen 36 ranuras en total).
static void homeOrderNormalize(){
  if(gHomePageN < 1) gHomePageN = 1;
  homeWgNormalize();          // los widgets mandan sobre las celdas: van primero
  if(gHomePageN > HOME_PAGES_MAX) gHomePageN = HOME_PAGES_MAX;
  if(gHomeMain >= gHomePageN) gHomeMain = 0;
  if(gHomePage < 0) gHomePage = 0;
  if(gHomePage >= gHomePageN) gHomePage = gHomeMain;
  int n = homeSlotCount();
  bool seen[APP_N] = { false };
  // 0. Toda ranura que no exista en la rejilla o en la pagina ACTIVAS se
  //    vacia, y su icono se recoloca abajo (regla 2). Es lo que hace que
  //    reducir la rejilla o borrar una pagina no pierda ni un icono.
  uint8_t rescue[HOME_TOTAL]; int nres = 0;
  for(int p = 0; p < HOME_PAGES_MAX; p++)
    for(int i = 0; i < HOME_STRIDE; i++){
      int k = homeIdx(p, i);
      if(p < gHomePageN && i < n) continue;
      if(homeOrder[k] != HOME_EMPTY){ rescue[nres++] = homeOrder[k]; homeOrder[k] = HOME_EMPTY; }
    }
  for(int p = 0; p < gHomePageN; p++){
    for(int i = 0; i < n; i++){
      int k = homeIdx(p, i);
      uint8_t v = homeOrder[k];
      if(v == HOME_EMPTY) continue;
      // Un icono no puede quedarse DEBAJO de un widget: se rescata y se recoloca.
      bool underWg = false;
      { uint32_t bit = (1u << i);
        homeOrder[k] = HOME_EMPTY;
        underWg = (homeCellMask(p, -1) & bit) != 0;
        homeOrder[k] = v; }
      if(underWg){ rescue[nres++] = v; homeOrder[k] = HOME_EMPTY; continue; }
      if(v >= APP_N || seen[v] || !appIsFav(v) || appIsHidden(v)){ homeOrder[k] = HOME_EMPTY; continue; }
      seen[v] = true;
    }
  }
  // 1. los iconos rescatados de ranuras que ya no existen vuelven a entrar.
  //    Si no queda hueco se CREA pagina (homeFirstFreeGrow): un icono no se
  //    pierde por falta de sitio, que es justo lo que pasaba antes.
  for(int r = 0; r < nres; r++){
    uint8_t v = rescue[r];
    if(v >= APP_N || seen[v] || !appIsFav(v) || appIsHidden(v)) continue;
    int slot = homeFirstFreeGrow();
    if(slot < 0){ gAppFav &= (uint32_t)~(1u << v); continue; }   // maximo de paginas Y todas llenas
    homeOrder[slot] = v; seen[v] = true;
  }
  // 2. toda app favorita sin ranura ocupa el PRIMER hueco libre (creando
  //    pagina si el escritorio esta lleno)
  for(int id = 0; id < APP_N; id++){
    if(!appIsFav(id) || appIsHidden(id) || seen[id]) continue;
    int slot = homeFirstFreeGrow();
    if(slot < 0){ gAppFav &= (uint32_t)~(1u << id); continue; }   // escritorio lleno de verdad
    homeOrder[slot] = (uint8_t)id; seen[id] = true;
  }
}
static void homeOrderLoad(){
  prefs.begin("flexos", true);
  // Primero la clave nueva (36 ranuras). Si no existe, se MIGRA la
  // vieja de 12 bytes: esos doce iconos son la pagina 0 y las otras
  // dos nacen vacias. Asi una placa que actualiza conserva su
  // escritorio exactamente como estaba.
  // Ajustes del escritorio. -1 hace de centinela de "la clave no existe".
  int pgn  = prefs.getInt("hpgn",  -1);
  int pmn  = prefs.getInt("hpmain", 0);
  int grid = prefs.getInt("hgrid", -1);
  int icsz = prefs.getInt("hicon", 1);
  int hfl  = prefs.getInt("hflag", -1);
  size_t n = prefs.getBytes("hordq", homeOrder, HOME_TOTAL);
  if(n != HOME_TOTAL){
    // MIGRACION en dos escalones, de la mas reciente a la mas antigua:
    //   "hordp" = 3 paginas x 12 ranuras (paso 12)  -> paso 20, 3 paginas
    //   "hord"  = 1 pagina  x 12 ranuras            -> pagina 0
    uint8_t legacy[HOME_LEGACY_TOTAL];
    size_t ln = prefs.getBytes("hordp", legacy, HOME_LEGACY_TOTAL);
    for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
    if(ln == HOME_LEGACY_TOTAL){
      for(int p = 0; p < HOME_LEGACY_PAGES; p++)
        for(int i = 0; i < HOME_LEGACY_SLOTS; i++)
          homeOrder[homeIdx(p, i)] = legacy[p * HOME_LEGACY_SLOTS + i];
      n = HOME_TOTAL;                       // migrada: cuenta como valida
    } else {
      size_t l1 = prefs.getBytes("hord", legacy, HOME_LEGACY_SLOTS);
      if(l1 == HOME_LEGACY_SLOTS){
        for(int i = 0; i < HOME_LEGACY_SLOTS; i++) homeOrder[homeIdx(0, i)] = legacy[i];
        n = HOME_TOTAL;
      }
    }
  }
  // -1 como valor por defecto hace de centinela de "la clave no existe": un
  // bitmask valido siempre cae en 0..0xFFFF, asi que no hay ambiguedad. Se
  // prefiere a isKey() para no depender de una API que no esta en todos los
  // cores de ESP32.
  int fav  = prefs.getInt("appfav",  -1);
  int hide = prefs.getInt("apphide", -1);
  int known = prefs.getInt("appn", 16);      // cuantas apps conocia el firmware que guardo esto
  { uint8_t wb[HOME_WG_BLOB];
    size_t wn = prefs.getBytes("hwg", wb, HOME_WG_BLOB);
    if(wn != HOME_WG_BLOB || !homeWgDeserialize(wb))
      for(int p = 0; p < HOME_PAGES_MAX; p++) gHomeWgN[p] = 0;   // sin widgets: estado valido
  }
  prefs.end();
  // Ajustes del escritorio, siempre acotados: unas prefs corruptas no pueden
  // dejar una rejilla imposible ni una pagina principal fuera de rango.
  gHomePageN = (pgn >= 1 && pgn <= HOME_PAGES_MAX) ? (uint8_t)pgn : HOME_LEGACY_PAGES;
  gHomeMain  = (pmn >= 0 && pmn < gHomePageN) ? (uint8_t)pmn : 0;
  if(grid >= 0){
    int c = (grid >> 8) & 0xFF, r = grid & 0xFF;
    gHomeCols = (c >= 4 && c <= HOME_COLS_MAX) ? (uint8_t)c : 4;
    gHomeRows = (r >= 3 && r <= HOME_ROWS_MAX) ? (uint8_t)r : 3;
  }
  gHomeIconSz = (icsz >= 0 && icsz <= 2) ? (uint8_t)icsz : 1;
  if(hfl >= 0){
    gHomeLabels = (hfl & 1)  != 0;
    gHomeLocked = (hfl & 2)  != 0;
    gHomeDots   = (hfl & 4)  != 0;
    gHomePinch  = (hfl & 8)  != 0;
    gHomeReduce = (hfl & 16) != 0;
  }
  gHomePage = gHomeMain;                    // al arrancar se entra por la pagina principal
  // Primer arranque (o actualizacion desde una version sin estas claves): el
  // reparto de fabrica sale del registro, no de constantes sueltas.
  if(fav < 0){ drawerRegistryDefaults(); }
  else {
    gAppFav = (uint32_t)fav; gAppHidden = (uint32_t)(hide < 0 ? 0 : hide);
    // Las apps nuevas reciben su valor de fabrica una vez al actualizar.
    if(known < APP_N) drawerRegistryAdopt(known);
  }
  // Primer arranque de verdad (ni clave nueva ni vieja): las doce de
  // siempre en la pagina 0, y el resto vacio.
  if(n != HOME_TOTAL){
    for(int i = 0; i < HOME_TOTAL; i++) homeOrder[i] = HOME_EMPTY;
    for(int i = 0; i < HOME_LEGACY_SLOTS; i++) homeOrder[homeIdx(0, i)] = (uint8_t)i;
  }
  const uint32_t validApps = (1u << APP_N) - 1u;
  gAppFav &= validApps;
  gAppHidden &= validApps;
  gAppLock &= validApps;                 // limpia bits de apps retiradas del registro
  gAppHidden &= (uint32_t)~(1u << IC_AJUSTES);        // Ajustes nunca oculto (ver appCanHide)
  homeOrderNormalize();
}
// MODO EDICION Y PAGINAS. Las ranuras que maneja el Modo Edicion son
// LOCALES a la pagina visible (0..11) y se traducen a la ranura global
// con edSlot(). Se hace asi -- y no reordenando las 36 a la vez --
// porque arrastrar un icono de una pagina a otra necesitaria ademas un
// gesto de "empujar el borde para cambiar de pagina" mientras se
// sostiene el icono, y mezclar eso con el dwell de 400 ms daria dos
// gestos peleandose por el mismo dedo. Reordenar dentro de la pagina
// funciona igual que siempre; mover una app de pagina se hace desde la
// Caja de aplicaciones, que ya sabe hacerlo.
static inline int edSlot(int local){ return homeIdx(gHomePage, local); }
static void edSlotXY(int slot, int &x, int &y){ homeSlotXY(slot, x, y); }
static int  edSlotAt(int px, int py){
  int S, gx0, gy0, cs, rs, cols, rows; homeGrid(S, gx0, gy0, cs, rs, cols, rows);
  if(py < gy0) return -1;
  int c = px / cs, r = (py - gy0) / rs;
  if(c < 0 || c >= cols || r < 0 || r >= rows) return -1;
  int slot = r * cols + c;
  return slot < homeSlotCount() ? slot : -1;
}
static void edMove(int from, int to){        // reinserta el icono (desplaza los demas)
  int cells = homeSlotCount();
  if(from == to || from < 0 || to < 0 || from >= cells || to >= cells) return;
  uint8_t v = homeOrder[edSlot(from)];
  if(from < to) for(int i = from; i < to; i++) homeOrder[edSlot(i)] = homeOrder[edSlot(i + 1)];
  else          for(int i = from; i > to; i--) homeOrder[edSlot(i)] = homeOrder[edSlot(i - 1)];
  homeOrder[edSlot(to)] = v;
}
// Borde inferior de la banda que recompone el Modo Edicion. Sale de la rejilla
// activa (con cuatro filas la ultima etiqueta baja), nunca de un 580 fijo.
static int edBandBot(){
  int b = homeBandBot() - 1;
  return b > SCR_H - 1 ? SCR_H - 1 : b;
}
static void edRender(){
  // Los iconos en Modo Edicion ahora usan el gIconStyle REAL (Vidrio si esta
  // activo en Ajustes) en vez de forzarse a Plano. Cada icono Vidrio pasa por
  // drawLiquidGlassPanel() -- un blur real, no gratis -- y aqui se dibujan
  // hasta 12 por frame. Para no trompicar el jiggle/arrastre en la P4, si el
  // estilo es Vidrio se limita el refresco de ESTA funcion a ~20 fps (50 ms).
  // Ojo: esto es un throttle LOCAL (reutiliza edMs, declarada mas arriba y
  // hasta ahora sin usar) -- a proposito NO se toca uiAnimMs, que es el
  // throttle compartido de qsPanel/ripple y no debe frenarse por esto.
  // En estilo Plano no hay throttle: se conserva el mismo refresco fluido de
  // siempre.
  if(gIconStyle == 1){
    unsigned long now = millis();
    if(now - edMs < 50) return;
    edMs = now;
  }
  setBuf(bbuf);
  for(int j = 120; j <= edBandBot(); j++) memcpy(bbuf + (size_t)j * SCR_W, homeBuf + (size_t)j * SCR_W, SCR_W * 2);  // fondo (sin rejilla)
  uint32_t t = millis();
  int gS, ggx0, ggy0, gcs, grs, gcols, grows; homeGrid(gS, ggx0, ggy0, gcs, grs, gcols, grows);
  // Widgets colocados: se dibujan bajo los iconos y llevan su insignia de
  // quitar. El que se esta moviendo se resalta con el acento activo.
  for(int k = 0; k < gHomeWgN[gHomePage] && k < HOME_WG_MAX; k++){
    if(gHomeWg[gHomePage][k].type == WG_NONE) continue;
    int wx, wy, ww, wh; wgRect(&gHomeWg[gHomePage][k], wx, wy, ww, wh);
    if(k == edWDrag) fillRoundRectA(wx - 4, wy - 4, ww + 8, wh + 8, 22, wallAccent(), 120);
    wgDrawCell(&gHomeWg[gHomePage][k], wx, wy, ww, wh, false);
    if(!gHomeLocked){
      fillCircle(wx + 12, wy + 12, 11, TH_DANGER);
      strokeSegAA(wx + 8.0f, wy + 8.0f, wx + 16.0f, wy + 16.0f, 2.0f, TH_ONACC);
      strokeSegAA(wx + 16.0f, wy + 8.0f, wx + 8.0f, wy + 16.0f, 2.0f, TH_ONACC);
    }
  }
  int cells = homeSlotCount();
  for(int i = 0; i < cells; i++){
    if(i == edDrag || homeOrder[edSlot(i)] == HOME_EMPTY) continue;   // ranura vacia: nada que temblar
    int tx, ty; edSlotXY(i, tx, ty);
    edCurX[i] += (tx - edCurX[i]) * 0.2f; edCurY[i] += (ty - edCurY[i]) * 0.2f;   // resorte
    float ph = i * 0.6f;
    int ox = (int)(2 * sinf(t * 0.02f + ph)), oy = (int)(2 * cosf(t * 0.017f + ph));  // temblor +-2px
    int s = gS * 8 / 9, off = (gS - s) / 2;                                      // escala ~90%
    drawAppIcon(homeOrder[edSlot(i)], (int)edCurX[i] + off + ox, (int)edCurY[i] + off + oy, s);
  }
  if(edDrag >= 0){                                                              // icono arrastrado (translucido)
    int dx = (int)edDragX, dy = (int)edDragY, s = gS;
    if(uiGlass) drawLiquidGlassPanel(dx - 6, dy - 6, s + 12, s + 12, 16, TH_GLASS2);
    else fillRoundRectA(dx - 6, dy - 6, s + 12, s + 12, 16, TH_SEL, 150);
    drawAppIcon(homeOrder[edSlot(edDrag)], dx, dy, s);
  }
  drawTextC(SCR_W / 2, 176, "Arrastra los iconos - Inicio para salir", 1, TH_ONWALL2);   // sobre el wallpaper
  present(120, edBandBot());
}
static void edEnter(){
  editMode = true;
  renderHome();                              // homeBuf sin rejilla (editMode salta el grid)
  for(int i = 0; i < HOME_STRIDE; i++){ int x, y; edSlotXY(i, x, y); edCurX[i] = (float)x; edCurY[i] = (float)y; }
  edDrag = -1; edHoverSlot = -1; edEdgeDir = 0; edWDrag = -1;
}
static void edExit(){
  editMode = false; edDrag = -1; edWDrag = -1;
  homeOrderSave();
  renderHome(); showHome();
}
static void edTick(){
  if(T.pressed){
    // Widgets: primero su insignia de quitar, luego el agarre para moverlo.
    int wi = homeWgAt(gHomePage, T.x, T.y);
    if(wi >= 0){
      int wx, wy, ww, wh; wgRect(&gHomeWg[gHomePage][wi], wx, wy, ww, wh);
      if(!gHomeLocked && abs(T.x - (wx + 12)) <= 14 && abs(T.y - (wy + 12)) <= 14){
        homeWgRemove(gHomePage, wi); homeOrderNormalize(); homeOrderSave();
        renderHome(); edRender(); return;
      }
      if(!gHomeLocked) edWDrag = wi;
      edDrag = -1; edRender(); return;
    }
    edDrag = edSlotAt(T.x, T.y);
    if(edDrag >= 0 && homeOrder[edSlot(edDrag)] == HOME_EMPTY) edDrag = -1;   // no se arrastra un hueco
    if(gHomeLocked) edDrag = -1;                                             // diseno bloqueado
    edSetDrag(T.x, T.y); edHoverSlot = -1; edRender(); return;
  }
  if(T.down && edWDrag >= 0){
    // Mover el widget por celdas: solo se acepta la posicion si CABE de verdad
    // ahi (homeWgFits ignora el propio widget, para que no choque consigo mismo).
    int cell = edSlotAt(T.x, T.y);
    if(cell >= 0){
      int wS, wgx0, wgy0, wcs, wrs, wcols, wrows; homeGrid(wS, wgx0, wgy0, wcs, wrs, wcols, wrows);
      int c = cell % wcols, r = cell / wcols;
      HomeWidget* w = &gHomeWg[gHomePage][edWDrag];
      if((c != w->col || r != w->row) && homeWgFits(gHomePage, c, r, w->w, w->h, edWDrag)){
        w->col = (uint8_t)c; w->row = (uint8_t)r;
        renderHome();
      }
    }
    edRender(); return;
  }
  if(T.down && edDrag >= 0){
    edSetDrag(T.x, T.y);
    // Sostener el icono en el borde cambia a la pagina vecina y lo lleva
    // consigo solo si alli existe una ranura libre.
    const int EDGE_W = 34, EDGE_MS = 700;
    int dir = 0;
    if(T.x <= EDGE_W)               dir = -1;
    else if(T.x >= SCR_W - EDGE_W)  dir =  1;
    if(dir != 0 && gHomePage + dir >= 0 && gHomePage + dir < gHomePageN){
      if(edEdgeDir != dir){ edEdgeDir = dir; edEdgeMs = millis(); }
      else if(millis() - edEdgeMs > (uint32_t)EDGE_MS){
        int dst = -1, cells2 = homeSlotCount();
        for(int i = 0; i < cells2 && dst < 0; i++)
          if(homeOrder[homeIdx(gHomePage + dir, i)] == HOME_EMPTY) dst = i;
        if(dst >= 0){
          uint8_t v = homeOrder[edSlot(edDrag)];
          homeOrder[edSlot(edDrag)] = HOME_EMPTY;
          gHomePage += dir;
          homeOrder[edSlot(dst)] = v;
          edDrag = dst;
          edHoverSlot = -1;
          edEdgeDir = 0;
          homeOrderSave();
          renderHome();
          for(int i = 0; i < HOME_STRIDE; i++){
            int x, y; edSlotXY(i, x, y); edCurX[i] = (float)x; edCurY[i] = (float)y;
          }
        } else edEdgeMs = millis();
      }
    } else edEdgeDir = 0;
    int dS, dgx0, dgy0, dcs, drs, dcols, drows; homeGrid(dS, dgx0, dgy0, dcs, drs, dcols, drows);
    int over = edSlotAt((int)edDragX + dS / 2, (int)edDragY + dS / 2);          // slot bajo el centro
    if(over >= 0 && over != edDrag){
      if(over != edHoverSlot){ edHoverSlot = over; edHoverMs = millis(); }
      else if(millis() - edHoverMs > 400){ edMove(edDrag, over); edDrag = over; edHoverSlot = -1; }  // dwell 400ms
    } else edHoverSlot = -1;
    edRender(); return;
  }
  if(T.released){
    edEdgeDir = 0;
    if(edWDrag >= 0){ edWDrag = -1; homeOrderNormalize(); homeOrderSave(); edRender(); return; }
    // NORMALIZAR TAMBIEN AL SOLTAR UN ICONO. El arrastre reinserta desplazando
    // (edMove), y ese desplazamiento puede empujar un icono a una celda que
    // ocupa un WIDGET: se veian superpuestos hasta la siguiente normalizacion.
    // El arrastre de widgets ya lo hacia (linea de arriba); el de iconos no.
    if(edDrag >= 0){ edDrag = -1; homeOrderNormalize(); homeOrderSave(); edRender(); }   // soltar -> fija
    else if(T.tap) edExit();                                                    // toque en vacio/Inicio -> salir
    return;
  }
  // reposo: el jiggle continuo lo mueve uiTick()
}
