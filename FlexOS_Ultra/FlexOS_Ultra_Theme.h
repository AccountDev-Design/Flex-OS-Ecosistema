// #############################################################
// ##  FLEX OS ULTRA  ·  TEMA, LIQUID GLASS Y SUPERFICIES DEL SISTEMA
// ##  ----------------------------------------------------------
// ##  Fuente de verdad del color: tema semantico (TH_*), modo claro/oscuro,
// ##  acento, Liquid Glass (desenfoque real) frente al estilo plano, tarjeta
// ##  de vidrio cacheada y las superficies compartidas del sistema.
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
#include "FlexOS_Ultra_Wallpaper.h"   // eslabon anterior de la cadena

// #############################################################
// ##  LIQUID GLASS (aproximacion iOS 26 en software)
// ##  Panel reutilizable: desenfoca el fondo real (box-blur),
// ##  tinte sutil y gradiente de grosor.
// #############################################################
// MATERIAL de las superficies (Ajustes -> Personalizacion -> Personalizar UI).
// true = Liquid Glass (drawLiquidGlassPanel / drawGlassCardFlat con el tinte de
// la paleta activa). false = superficies PLANAS de la paleta activa.
// ORTOGONAL a gDark: el vidrio funciona y conserva contraste en las dos
// apariencias, porque el tinte y el texto salen del tema, no de un valor fijo.
static bool uiGlass = false;
// APARIENCIA (Ajustes -> Pantalla -> Modo de apariencia). true = oscura (valor
// por defecto de siempre, y el que reciben las placas que aun no tienen la
// clave NVS "dark"). false = clara.
// ALCANCE: GLOBAL. gDark elige la paleta semantica activa (ver TEMA SEMANTICO,
// justo debajo) y esa paleta es la fuente de verdad de color de TODO el
// sistema: shell de apps, barra de estado y navegacion, Inicio y widgets,
// Ajustes, Notas y su teclado, Paint, Almacenamiento, Wi-Fi, OTA, bloqueo/PIN,
// notificaciones, panel rapido, multitarea, Modo PC, dialogos y menus.
static bool gDark = true;
static int  gIconStyle = 0;                // estilo de iconos: 0 = Plano, 1 = Vidrio (fondo Liquid Glass en drawAppIcon)

// #############################################################
// ##  TEMA SEMANTICO GLOBAL  ·  fuente de verdad de color
// ##  ------------------------------------------------------
// ##  Los componentes piden color por SIGNIFICADO (fondo de pagina, texto
// ##  secundario, borde, accion destructiva...), nunca por valor RGB ni con
// ##  un ternario "gDark ? A : B" repetido por el archivo. Cambiar de
// ##  apariencia es cambiar QUE struct devuelve TH(); ningun llamante se
// ##  entera.
// ##
// ##  COSTE EN LA PLACA (restricciones del P4 + RGB565):
// ##   · dos structs const de 27 uint16_t -> 108 bytes en .rodata (FLASH),
// ##     no en RAM ni en PSRAM;
// ##   · TC() es una expresion constante en tiempo de COMPILACION (no la
// ##     funcion rgb565(), que no es constexpr), asi que los literales se
// ##     empaquetan a RGB565 al compilar y las tablas viven en flash;
// ##   · TH() es un simple select de puntero: cero asignaciones, cero
// ##     trabajo por pixel, cero buffers nuevos.
// #############################################################
// (TC() se define mas arriba, junto al catalogo de fondos: los fondos
// integrados son el primer codigo del archivo que empaqueta color en tiempo de
// compilacion, y el preprocesador exige verla antes de su primer uso.)

struct FlexTheme {
  uint16_t page;       // fondo principal de pagina (pantallas a pantalla completa)
  uint16_t win;        // fondo de ventana (marco estandar de app)
  uint16_t surf;       // superficie / tarjeta plana
  uint16_t surf2;      // superficie elevada (menu, dialogo, tecla, chip)
  uint16_t glass;      // tinte de Liquid Glass para 'surf'
  uint16_t glass2;     // tinte de Liquid Glass para 'surf2' (elevado)
  uint16_t txt;        // texto principal
  uint16_t txt2;       // texto secundario / valor
  uint16_t mute;       // texto atenuado (ayuda, pie, deshabilitado)
  uint16_t nav;        // iconos de navegacion y de barra de estado
  uint16_t border;     // borde de superficie
  uint16_t divider;    // divisor / separador dentro de una superficie
  uint16_t disabled;   // control desactivado (relleno)
  uint16_t track;      // track de switch/slider apagado
  uint16_t sel;        // seleccion / foco (resaltado de texto, fila activa)
  uint16_t scrim;      // overlay para atenuar lo que hay detras de un modal
  uint16_t shadow;     // sombra proyectada
  uint16_t onAcc;      // texto/icono SOBRE una accion primaria o destructiva
  uint16_t primary;    // accion primaria
  uint16_t danger;     // accion destructiva
  uint16_t ok;         // estado: exito
  uint16_t warn;       // estado: advertencia
  uint16_t err;        // estado: error
  uint16_t accSoft;    // acento suave (texto de enlace/valor destacado sobre superficie)
  // Teclado. Van en la paleta y no como "surf/surf2" reutilizados porque la
  // relacion entre panel y tecla se INVIERTE entre apariencias (igual que en
  // iOS y Gboard): en oscuro la tecla es mas CLARA que el panel, en claro la
  // tecla es blanca y el panel gris. Declararlo aqui evita el unico ternario
  // "gDark ? A : B" que si haria falta en el codigo de dibujo.
  uint16_t keyPanel;   // fondo del panel del teclado
  uint16_t keyFace;    // relleno de tecla normal
  uint16_t keyAlt;     // relleno de tecla de funcion (shift, ?123, borrar...)
};

// Paleta OSCURA: son los mismos valores afinados a mano que ya tenia el
// sistema (WIN_BG 18,20,28 / tarjeta 34,38,50 / texto 240,242,248...), asi que
// una placa que actualice ve EXACTAMENTE lo de siempre hasta que el usuario
// toque el selector.
static const FlexTheme kThemeDark = {
  /*page   */ TC( 18, 20, 28),
  /*win    */ TC( 18, 20, 28),
  /*surf   */ TC( 34, 38, 50),
  /*surf2  */ TC( 48, 54, 72),
  /*glass  */ TC( 48, 54, 72),
  /*glass2 */ TC( 40, 50, 90),
  /*txt    */ TC(240,242,248),
  /*txt2   */ TC(160,166,182),
  /*mute   */ TC(120,126,142),
  /*nav    */ TC(232,236,246),
  /*border */ TC( 66, 74, 94),
  /*divider*/ TC( 52, 58, 74),
  /*disabled*/TC(124,130,146),
  /*track  */ TC( 56, 62, 86),
  /*sel    */ TC( 48, 92,168),
  /*scrim  */ TC(  8, 10, 18),
  /*shadow */ TC(  0,  0,  0),
  /*onAcc  */ TC(255,255,255),
  /*primary*/ TC( 60,110,235),
  /*danger */ TC(200, 70, 70),
  /*ok     */ TC( 90,200,140),
  /*warn   */ TC(240,180, 90),
  /*err    */ TC(235, 80, 80),
  /*accSoft*/ TC(140,180,250),
  /*keyPanel*/TC( 30, 34, 46),
  /*keyFace */TC( 52, 56, 70),
  /*keyAlt  */TC( 66, 70, 86)
};
// Paleta CLARA: mismo esqueleto semantico. Los pares texto/superficie estan
// elegidos para no bajar de ~4.5:1 (texto principal) ni de ~3:1 (secundario)
// sobre su superficie, en plano y sobre el tinte de vidrio.
static const FlexTheme kThemeLight = {
  /*page   */ TC(244,247,251),
  /*win    */ TC(244,247,251),
  /*surf   */ TC(255,255,255),
  /*surf2  */ TC(236,240,248),
  /*glass  */ TC(246,248,252),
  /*glass2 */ TC(224,232,246),
  /*txt    */ TC( 20, 22, 30),
  /*txt2   */ TC( 96,102,116),
  /*mute   */ TC(132,138,152),
  /*nav    */ TC( 34, 38, 50),
  /*border */ TC(202,210,224),
  /*divider*/ TC(224,229,238),
  /*disabled*/TC(132,138,150),
  /*track  */ TC(202,208,220),
  /*sel    */ TC(178,206,246),
  /*scrim  */ TC( 34, 40, 54),
  /*shadow */ TC( 70, 80,100),
  /*onAcc  */ TC(255,255,255),
  /*primary*/ TC( 45, 95,225),
  /*danger */ TC(196, 52, 52),
  /*ok     */ TC( 22,140, 88),
  /*warn   */ TC(176,116, 12),
  /*err    */ TC(204, 46, 46),
  /*accSoft*/ TC( 40, 92,190),
  /*keyPanel*/TC(222,226,236),
  /*keyFace */TC(255,255,255),
  /*keyAlt  */TC(214,219,230)
};

// Paleta activa. Se expresa como macro para que el preprocesador de sketches de
// Arduino NO intente autogenerar un prototipo de funcion que use FlexTheme antes
// de haber visto la declaracion del struct. La expresion condicional conserva
// referencia al objeto const elegido: sin copias, sin RAM y sin trabajo extra.
#define TH() (gDark ? kThemeDark : kThemeLight)

// Atajos por SIGNIFICADO (lo que se usa en el codigo de dibujo).
#define TH_PAGE    (TH().page)
#define TH_WIN     (TH().win)
#define TH_SURF    (TH().surf)
#define TH_SURF2   (TH().surf2)
#define TH_GLASS   (TH().glass)
#define TH_GLASS2  (TH().glass2)
#define TH_TXT     (TH().txt)
#define TH_TXT2    (TH().txt2)
#define TH_MUTE    (TH().mute)
#define TH_NAV     (TH().nav)
#define TH_BORDER  (TH().border)
#define TH_DIV     (TH().divider)
#define TH_DIS     (TH().disabled)
#define TH_TRACK   (TH().track)
#define TH_SEL     (TH().sel)
#define TH_SCRIM   (TH().scrim)
#define TH_SHADOW  (TH().shadow)
#define TH_ONACC   (TH().onAcc)
#define TH_PRIM    (TH().primary)
#define TH_DANGER  (TH().danger)
#define TH_OK      (TH().ok)
#define TH_WARN    (TH().warn)
#define TH_ERR     (TH().err)
#define TH_ACCS    (TH().accSoft)
#define TH_KEYPANEL (TH().keyPanel)
#define TH_KEYFACE  (TH().keyFace)
#define TH_KEYALT   (TH().keyAlt)

// Chrome que vive SOBRE EL WALLPAPER: barra de estado del Inicio y del Bloqueo,
// etiquetas de los iconos, glifos de la barra de navegacion, reloj grande y
// texto de "desliza para desbloquear".
// EXCEPCION DELIBERADA, y centralizada aqui en vez de repartida en literales: el
// wallpaper es CONTENIDO (no se retine con el tema) y siempre es un degradado
// saturado y oscuro, asi que su chrome se mantiene claro en las DOS apariencias.
// Con la paleta clara quedaria texto casi negro sobre morado/azul: ilegible.
#define TH_ONWALL   TC(255,255,255)   // texto e iconos sobre el wallpaper
#define TH_ONWALL2  TC(215,222,238)   // secundario sobre el wallpaper
#define TH_WALLSURF TC( 44, 54, 92)   // tarjeta/tecla apoyada en el wallpaper (plano)
#define TH_WALLSURF2 TC(48, 60,110)   // idem, tinte de Liquid Glass
#define TH_WALLPANEL TC(36, 40, 58)   // panel grande (teclado) sobre el wallpaper

// ACENTO ACTIVO DEL SISTEMA. Con "Aplicar paleta al sistema" encendido manda el
// color extraido del fondo; si no, el acento del tema semantico. Ningun
// componente tiene que saber cual de los dos esta mandando: pide wallAccent().
// El contraste del texto que va encima se resuelve SIEMPRE con onColor().
static uint16_t wallAccent(){  return (gWallPalOn && gWallPalOk) ? gWallAcc  : TH_PRIM; }
static uint16_t wallAccent2(){ return (gWallPalOn && gWallPalOk) ? gWallAcc2 : TH_ACCS; }

// Superficie que toca usar segun el MATERIAL activo. Con Liquid Glass el
// llamante pinta el panel de vidrio con TH_GLASS/TH_GLASS2 de tinte; sin el,
// rellena plano con TH_SURF/TH_SURF2. Estas dos funciones existen para que un
// llamante que solo necesita "el color de la tarjeta" (bordes, textos de
// respaldo, glcBuild) no tenga que repetir el if.
static inline uint16_t thCard(){  return uiGlass ? TH().glass  : TH().surf;  }
static inline uint16_t thCard2(){ return uiGlass ? TH().glass2 : TH().surf2; }

// Propagacion inmediata de un cambio de tema/material. Se DEFINE mas abajo
// (necesita ver gHomeDirty, qsDirty, glcValid, el fondo cacheado de Modo PC y
// las miniaturas de multitarea); aqui solo el prototipo, porque los llamantes
// -- Ajustes y el panel de Modo PC -- aparecen antes en el archivo.
static void themeChanged(bool save = true);
static uint16_t* glassBuf = NULL;          // scratch de region (PSRAM)
// NOTA: aqui vivia 'wallBuf' ("wallpaper limpio para animar el brillo"). Era
// memoria muerta: se reservaban 768 KB y se copiaban enteros en CADA
// renderHome(), pero NINGUNA funcion lo leia jamas. Eliminado: -768 KB de
// PSRAM y -768 KB de memcpy por cada repintado del escritorio.
// Linea temporal para el blur. Se indexa por ANCHO (pasada horizontal) y
// por ALTO (pasada vertical) del panel de turno, asi que debe cubrir el
// mayor de los dos lados de la pantalla, no solo SCR_H, o un panel mas
// ancho que alto desbordaria este buffer y corromperia memoria vecina.
static uint16_t  glLine[(SCR_W > SCR_H ? SCR_W : SCR_H)];

static inline void un565(uint16_t c, int &r, int &g, int &b){ r = (c >> 11) & 0x1F; g = (c >> 5) & 0x3F; b = c & 0x1F; }
static inline uint16_t pk565(int r, int g, int b){ return (uint16_t)((r << 11) | (g << 5) | b); }

// Luma aproximada directamente en dominio 565, sin float y sin multiplicacion
// real: el compilador reduce *5 a (x<<2)+x y *2 a x<<1. Devuelve 0..266 en vez
// de 0..255, y NO se normaliza a proposito: solo se usa para comparar dos lumas
// entre si (la del fondo contra la del tinte), y ambas viven en este mismo
// dominio, asi que la resta es consistente. El error medio contra la luma
// perceptual real es de ~5 niveles sobre 255, de sobra para decidir cuanto
// tinte aplicar. Reutiliza un565 en vez de repetir el desempaquetado.
static inline int glassLuma(uint16_t c){
  int r, g, b; un565(c, r, g, b);
  return ((r + g) * 5 + b * 2) >> 1;
}

// box-blur (suma corrediza) sobre glassBuf de ancho w, alto h
static void glassBlur(int w, int h, int R){
  int r, g, b;
  for(int j = 0; j < h; j++){                         // horizontal
    uint16_t* row = glassBuf + (size_t)j * w;
    for(int i = 0; i < w; i++) glLine[i] = row[i];
    int sr = 0, sg = 0, sb = 0, win = 0;
    for(int i = 0; i <= R && i < w; i++){ un565(glLine[i], r, g, b); sr += r; sg += g; sb += b; win++; }
    for(int i = 0; i < w; i++){
      row[i] = pk565(sr / win, sg / win, sb / win);
      int add = i + R + 1, rem = i - R;
      if(add < w){ un565(glLine[add], r, g, b); sr += r; sg += g; sb += b; win++; }
      if(rem >= 0){ un565(glLine[rem], r, g, b); sr -= r; sg -= g; sb -= b; win--; }
    }
  }
  for(int i = 0; i < w; i++){                          // vertical
    for(int j = 0; j < h; j++) glLine[j] = glassBuf[(size_t)j * w + i];
    int sr = 0, sg = 0, sb = 0, win = 0;
    for(int j = 0; j <= R && j < h; j++){ un565(glLine[j], r, g, b); sr += r; sg += g; sb += b; win++; }
    for(int j = 0; j < h; j++){
      glassBuf[(size_t)j * w + i] = pk565(sr / win, sg / win, sb / win);
      int add = j + R + 1, rem = j - R;
      if(add < h){ un565(glLine[add], r, g, b); sr += r; sg += g; sb += b; win++; }
      if(rem >= 0){ un565(glLine[rem], r, g, b); sr -= r; sg -= g; sb -= b; win--; }
    }
  }
}
static int glInset(int j, int h, int rad){
  if(j < rad){ int dy = rad - 1 - j; return rad - isqrt32(rad * rad - dy * dy); }
  if(j >= h - rad){ int dy = j - (h - rad); return rad - isqrt32(rad * rad - dy * dy); }
  return 0;
}
// Panel Liquid Glass reutilizable (estatico: blur + tinte + gradiente).
// "Ex" permite fijar el radio del box-blur (blurR). glassBlur() es una suma
// corrediza O(w*h) que NO depende de blurR (ver mas arriba), asi que subir
// blurR no cuesta rendimiento extra -- solo cambia cuanto se difumina el
// fondo. drawLiquidGlassPanel() de siempre (abajo) sigue llamando a esta con
// blurR=6, es decir: mismo blur que antes en los ~19 sitios existentes que ya
// la usan. Se penso para el panel rapido, que quiere un vidrio mas
// "esmerilado" que el resto del sistema.
//
// TINTE ADAPTATIVO: el porcentaje de mezcla del tinte ya no es el 58 fijo de
// antes; se mueve dentro de [GLASS_TINT_MIN..GLASS_TINT_MAX] segun cuanto
// difiera la luminancia del tinte respecto a la del fondo que quedo debajo del
// panel. Esto SI cambia el aspecto de los ~19 sitios existentes (cambio pedido
// y aprobado a proposito, no un efecto colateral): el blur y la geometria son
// los de siempre, solo respira el tinte. GLASS_TINT_BASE es el valor historico
// y queda como respaldo defensivo por si no se pudo tomar ninguna muestra.
// GLASS_TINT_DIFF_MAX es potencia de dos a proposito: convierte la division
// del mapeo en un desplazamiento.
static const uint8_t GLASS_TINT_BASE = 58, GLASS_TINT_MIN = 46, GLASS_TINT_MAX = 70;
static const int     GLASS_TINT_DIFF_MAX = 128;
static void drawLiquidGlassPanelEx(int x, int y, int w, int h, int rad, uint16_t tint, int blurR){
  // GUARDA DE LANDSCAPE (Modo PC). Esta funcion lee y escribe el buffer con
  // indexacion VERTICAL directa (gBuf + (y+j)*SCR_W + x), asi que ignora por
  // completo la rotacion de gLand. En Modo PC cada drawAppIcon() de estilo
  // "Vidrio" pintaba su panel en coordenadas rotadas: por eso aparecian paneles
  // de cristal FANTASMA flotando por el escritorio (una columna a la derecha =
  // los iconos de la barra de tareas, una fila abajo = los del escritorio).
  // fillRoundRectA() si respeta la rotacion (pasa por putPhys), asi que en
  // landscape se usa el panel plano tintado: mismo sitio, sin fantasmas. El
  // cristal propio de Modo PC lo dibuja pcGlassPanel(), que si es landscape-safe.
  if(gLand){ fillRoundRectA(x, y, w, h, rad, tint, 210); return; }
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glassBuf){ fillRoundRectA(x, y, w, h, rad, tint, 210); return; }   // fallback sin PSRAM
  if(x < 0){ w += x; x = 0; } if(y < 0){ h += y; y = 0; }
  if(x + w > SCR_W) w = SCR_W - x; if(y + h > SCR_H) h = SCR_H - y;
  if(w <= 0 || h <= 0) return;
  if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;

  // BANDA UTIL DEL PANEL.
  //
  // El compositor casi nunca repinta la pantalla entera: publica BANDAS
  // (regiones sucias) y las listas desplazables dibujan sus filas dentro de un
  // viewport recortado. Las filas del panel que caen fuera de [gClipY0,gClipY1]
  // no llegan a escribirse -- el bucle de composicion de abajo ya las saltaba --
  // pero se copiaban y se DESENFOCABAN igual. Una lista con veinte tarjetas de
  // vidrio hacia veinte copias y veinte desenfoques completos para publicar dos
  // filas.
  //
  // Aqui el trabajo se acota a las filas visibles mas blurR filas de margen a
  // cada lado, y el resultado es IDENTICO pixel a pixel: glassBlur() es un
  // desenfoque separable de caja, de radio blurR y UNA sola pasada por eje, asi
  // que la fila j solo depende de las filas [j-blurR, j+blurR]. El margen mete
  // exactamente esas filas; y donde el margen topa con el borde del panel, la
  // ventana se encoge igual que se encogeria desenfocando el panel entero,
  // porque el borde es el mismo. La pasada horizontal es fila a fila, asi que
  // no depende de cuantas filas se copien.
  int vy0 = y         > gClipY0 ? y         : gClipY0;
  int vy1 = (y + h - 1) < gClipY1 ? (y + h - 1) : gClipY1;
  if(vy0 > vy1) return;                       // ni una fila visible: no hay nada que componer
  int j0 = (vy0 - y) - blurR; if(j0 < 0)     j0 = 0;
  int j1 = (vy1 - y) + blurR; if(j1 > h - 1) j1 = h - 1;
  int hc = j1 - j0 + 1;                       // filas realmente copiadas y desenfocadas

  // Sampleo del fondo para el tinte adaptativo. Lee 1 de cada 4 filas por 1 de
  // cada 8 columnas (1/32 de los pixeles) sobre el panel COMPLETO, no sobre la
  // banda: el tinte depende de la luminancia media de todo el panel, y medirlo
  // solo en la banda daria un color distinto en cada repintado parcial. Se lee
  // directamente de gBuf, que es lo mismo que leia antes de la copia.
  uint32_t lumaSum = 0; int lumaN = 0;
  for(int j = 0; j < h; j += 4){
    const uint16_t* srow = gBuf + (size_t)(y + j) * SCR_W + x;
    for(int i = 0; i < w; i += 8){ lumaSum += (uint32_t)glassLuma(srow[i]); lumaN++; }
  }
  for(int j = j0; j <= j1; j++)
    memcpy(glassBuf + (size_t)(j - j0) * w, gBuf + (size_t)(y + j) * SCR_W + x, w * 2);
  // Cuanto MAS se parecen en luminancia el tinte y el fondo, MENOS tinte se
  // aplica: si ya comparten tono, cargarle tinte solo lo aplana en un bloque
  // liso, y conviene dejar ver el fondo. Cuanto mas difieren, mas tinte, para
  // que el panel afirme su propio color en vez de lavarse contra el fondo. Un
  // solo valor absoluto cubre los cuatro casos sin ramas: tinte oscuro sobre
  // fondo oscuro y tinte claro sobre fondo claro dan diferencia chica (poco
  // tinte); los dos cruzados dan diferencia grande (mas tinte). Todo esto se
  // calcula UNA vez por panel, no por pixel.
  uint8_t tintMix = GLASS_TINT_BASE;
  if(lumaN > 0){
    int dif = (int)(lumaSum / (uint32_t)lumaN) - glassLuma(tint);
    if(dif < 0) dif = -dif;
    if(dif > GLASS_TINT_DIFF_MAX) dif = GLASS_TINT_DIFF_MAX;
    tintMix = (uint8_t)(GLASS_TINT_MIN + (dif * (GLASS_TINT_MAX - GLASS_TINT_MIN)) / GLASS_TINT_DIFF_MAX);
  }
  glassBlur(w, hc, blurR);
  // La geometria (inset redondeado, degradado, borde) sigue calculandose con j
  // y h del panel ENTERO: la banda solo decide que filas se recorren, nunca
  // como se ven.
  for(int j = vy0 - y; j <= vy1 - y; j++){
    int yy = y + j;
    int ins = glInset(j, h, rad);
    uint16_t* src = glassBuf + (size_t)(j - j0) * w;
    uint16_t* dst = gBuf + (size_t)yy * SCR_W + x;
    float fj = (float)j;
    for(int i = ins; i < w - ins; i++){
      uint16_t out = mix565(src[i], tint, tintMix);   // tinte adaptativo (ver arriba), antes fijo en 58
      // Especular y sombreado del MATERIAL: es un blanco y un negro de luz
      // (como el brillo de un cristal real), no un color de tema. Se aplican
      // SOBRE el tinte, que si viene del tema, asi que el vidrio se aclara u
      // oscurece solo con la paleta activa.
      if(fj < h * 0.45f) out = mix565(out, rgb565(255,255,255), (uint8_t)((1.0f - fj / (h * 0.45f)) * 26));
      else               out = mix565(out, rgb565(0,0,0), (uint8_t)(((fj - h * 0.45f) / (h * 0.55f)) * 30));
      dst[i] = out;
    }
    // Highlight direccional (luz simulada desde la esquina superior-izquierda):
    // mismo bcol de siempre por fila (blanco arriba, negro abajo), pero la
    // FUERZA de la mezcla se pondera distinto por lado en vez de usar 130 fijo
    // en los dos bordes. Asi las 4 esquinas quedan con peso propio (arriba-
    // izq. blanco fuerte, arriba-der. blanco tenue, abajo-izq. sombra tenue,
    // abajo-der. sombra fuerte) en vez de una franja horizontal identica en
    // ambos bordes. GLASS_CORNER_STRONG/WEAK promedian 130 (el valor de antes)
    // para no cambiar el "peso" total del borde, solo redistribuirlo: el delta
    // es de +-20% sobre ese 130. Sigue siendo funcion de j nada mas: mismos 2
    // pixeles por fila de siempre. Si hay que retocar la intensidad, mover los
    // dos valores de forma simetrica alrededor de 130 (STRONG = 130 + d,
    // WEAK = 130 - d) para que el borde no gane ni pierda peso total.
    const uint8_t GLASS_CORNER_STRONG = 156, GLASS_CORNER_WEAK = 104;
    bool topZone = (j < h / 2);
    uint8_t sL = topZone ? GLASS_CORNER_STRONG : GLASS_CORNER_WEAK;   // izquierda: blanco fuerte / sombra tenue
    uint8_t sR = topZone ? GLASS_CORNER_WEAK   : GLASS_CORNER_STRONG; // derecha: blanco tenue / sombra fuerte
    // Borde del cristal: reflejo claro arriba y sombra abajo. Requisito
    // GRAFICO del material (da el grosor), no una superficie del tema.
    uint16_t bcol = (j < 3) ? rgb565(255,255,255) : (j < h / 2 ? rgb565(205,214,228) : rgb565(22,28,40));
    dst[ins] = mix565(dst[ins], bcol, sL);
    dst[w - 1 - ins] = mix565(dst[w - 1 - ins], bcol, sR);
  }
}
// MODO VISUAL EFICIENTE  ·  TEMPORAL, y no es una preferencia
// ---------------------------------------------------------------------------
// Lo enciende "Optimizar Flex OS" SOLO si, despues de soltar todo lo seguro, la
// presion de memoria sigue alta; y lo apaga el propio sistema en cuanto la
// presion baja (ver memTick). NO se guarda en NVS, NO aparece en Ajustes y NO
// toca el estilo elegido por el usuario: si tiene Liquid Glass, sigue teniendo
// Liquid Glass -- con menos radio de desenfoque -- y si tiene Plano, aqui no
// cambia absolutamente nada porque esta ruta ni se llama.
//
// Que hace, y por que eso SI ahorra: drawLiquidGlassPanelEx trabaja sobre las
// filas visibles MAS blurR filas de margen a cada lado (ver j0/j1 en su
// cuerpo), asi que bajar el radio reduce de verdad las filas que se
// desenfocan en cada panel. Ademas Recientes conserva una sola miniatura en
// vez de cuatro (73 KB cada una).
static bool gEffMode = false;
#define GLASS_BLUR_R      6
#define GLASS_BLUR_R_EFF  2
// SOMBRAS. En modo eficiente pesan la mitad. Es la otra mitad del efecto
// pedido: cada sombra es un relleno redondeado con alpha que se repinta en
// cada cuadro de un arrastre de ventana en Modo PC, asi que bajar el alpha
// baja de verdad el trabajo de mezcla por pixel -- y ademas se ve mas plano,
// que es lo que se espera de un "modo eficiente".
static inline uint8_t effShadow(int a){
  if(a < 0)   a = 0;
  if(a > 255) a = 255;
  return (uint8_t)(gEffMode ? a / 2 : a);
}
static void drawLiquidGlassPanel(int x, int y, int w, int h, int rad, uint16_t tint){
  drawLiquidGlassPanelEx(x, y, w, h, rad, tint, gEffMode ? GLASS_BLUR_R_EFF : GLASS_BLUR_R);
}

// #############################################################
// ##  TARJETA LIQUID GLASS CACHEADA (fondos PLANOS)
// ##  ------------------------------------------------------
// ##  Por que existe: drawLiquidGlassPanel copia la region, la desenfoca y la
// ##  mezcla. Con ocho tarjetas por cuadro eso era demasiado caro para seguir
// ##  al dedo, asi que las listas con scroll (Ajustes, Ajustes del teclado)
// ##  DESACTIVABAN el vidrio mientras se arrastraba y lo devolvian al soltar:
// ##  de ahi que "al hacer scroll el material perdiera el desenfoque y las
// ##  transparencias".
// ##  La observacion que lo arregla: sobre un fondo de color UNIFORME el
// ##  box-blur devuelve ese mismo color, asi que el resultado del panel no
// ##  depende de DONDE se dibuje -- solo de (w, h, radio, tinte, color de
// ##  fondo). Se calcula UNA vez, se guarda y a partir de ahi cada tarjeta es
// ##  un memcpy por fila. El resultado en pantalla es identico pixel a pixel
// ##  al de la version cara, asi que el vidrio ya puede quedarse encendido
// ##  durante todo el desplazamiento.
// ##  Si el usuario tiene el Liquid Glass DESACTIVADO no se llama aqui
// ##  siquiera: cada llamante conserva su rama plana de siempre.
// #############################################################
#define GLC_MAX_H 96                       // alto maximo de tarjeta cacheable (las filas miden ~52-62)
static uint16_t* glcScratch = NULL;        // lienzo de trabajo (stride SCR_W, GLC_MAX_H filas)
static uint16_t* glcCard    = NULL;        // tarjeta ya resuelta (w x h compactos)
static int       glcW = 0, glcH = 0, glcRad = -1;
static uint16_t  glcTint = 0, glcBg = 0;
static bool      glcValid = false;

static bool glcBuild(int w, int h, int rad, uint16_t tint, uint16_t bg){
  if(!glcScratch) glcScratch = (uint16_t*)heap_caps_malloc((size_t)SCR_W * GLC_MAX_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glcCard)    glcCard    = (uint16_t*)heap_caps_malloc((size_t)SCR_W * GLC_MAX_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glcScratch || !glcCard) return false;
  for(int j = 0; j < h; j++){                       // fondo plano: la premisa de todo esto
    uint16_t* r = glcScratch + (size_t)j * SCR_W;
    for(int i = 0; i < w; i++) r[i] = bg;
  }
  uint16_t* oBuf = gBuf;                            // gBuf directo, no setBuf: no debe desviarse a un lienzo de DeX
  int oc0 = gClipY0, oc1 = gClipY1, ox0 = gClipX0, ox1 = gClipX1;
  gBuf = glcScratch;
  gClipY0 = 0; gClipY1 = h - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  drawLiquidGlassPanel(0, 0, w, h, rad, tint);
  gBuf = oBuf; gClipY0 = oc0; gClipY1 = oc1; gClipX0 = ox0; gClipX1 = ox1;
  for(int j = 0; j < h; j++)
    memcpy(glcCard + (size_t)j * w, glcScratch + (size_t)j * SCR_W, (size_t)w * 2);
  glcW = w; glcH = h; glcRad = rad; glcTint = tint; glcBg = bg; glcValid = true;
  return true;
}
// Tarjeta de vidrio sobre fondo plano. Cae al panel de siempre (mismo dibujo,
// solo mas caro) si el tamano no cabe en la cache o si estamos en landscape,
// donde la indexacion directa no vale.
static void drawGlassCardFlat(int x, int y, int w, int h, int rad, uint16_t tint, uint16_t bg){
  if(gLand || w <= 0 || h <= 0 || w > SCR_W || h > GLC_MAX_H){
    drawLiquidGlassPanel(x, y, w, h, rad, tint); return;
  }
  if(!glcValid || glcW != w || glcH != h || glcRad != rad || glcTint != tint || glcBg != bg){
    if(!glcBuild(w, h, rad, tint, bg)){ drawLiquidGlassPanel(x, y, w, h, rad, tint); return; }
  }
  for(int j = 0; j < h; j++){
    int yy = y + j;
    if(yy < 0 || yy >= SCR_H || yy < gClipY0 || yy > gClipY1) continue;
    int xs = x, xe = x + w - 1, sx = 0;
    if(xs < gClipX0){ sx = gClipX0 - xs; xs = gClipX0; }
    if(xe > gClipX1) xe = gClipX1;
    if(xs < 0){ sx += -xs; xs = 0; }
    if(xe > SCR_W - 1) xe = SCR_W - 1;
    if(xs > xe) continue;
    memcpy(gBuf + (size_t)yy * SCR_W + xs, glcCard + (size_t)j * w + sx, (size_t)(xe - xs + 1) * 2);
  }
}

// #############################################################
// ##  SUPERFICIES DEL SISTEMA  ·  UN SOLO COMPONENTE
// ##  ------------------------------------------------------
// ##  QUE ARREGLA. El COLOR ya tenia fuente de verdad (FlexTheme / TH()), pero
// ##  el MATERIAL no: cada overlay decidia por su cuenta si pintaba vidrio o
// ##  plano, y los que se animan resolvian todos el mismo problema -- "el
// ##  desenfoque es caro, no puedo pagarlo por cuadro" -- de una forma
// ##  distinta cada uno. El menu contextual de pulsacion larga y la tarjeta
// ##  desplegada del cronometro hacian literalmente esto:
// ##
// ##      fillRoundRectA(..., COLOR_PLANO, alpha);         // todos los cuadros
// ##      if(uiGlass && p >= 1.0f) drawLiquidGlassPanel(...); // solo el ultimo
// ##
// ##  Con Liquid Glass activado se veia, durante TODA la animacion, una capa
// ##  plana desvanecida que no era Liquid Glass -- exactamente lo que se
// ##  reporto. No era un color mal elegido: era que no habia componente.
// ##
// ##  COMO SE ARREGLA SIN PAGAR EL DESENFOQUE POR CUADRO. Estos overlays se
// ##  animan SOBRE UN FONDO QUIETO (el menu contextual sobre homeBuf; la
// ##  tarjeta del cronometro sobre la banda capturada al abrir): la entrada del
// ##  desenfoque es IDENTICA en todos los cuadros y lo unico que cambia es el
// ##  rectangulo. Asi que la banda se desenfoca UNA VEZ al empezar la animacion
// ##  y cada cuadro solo muestrea de ese resultado. Es correcto por
// ##  construccion: glassBlur es un box-blur separable, asi que desenfocar la
// ##  banda entera y recortar un sub-rectangulo da lo mismo que desenfocar ese
// ##  sub-rectangulo.
// ##
// ##  RESULTADO: transparencia, tinte, borde y desenfoque REALES durante toda
// ##  la animacion, al coste de un relleno redondeado por cuadro -- el mismo
// ##  que costaba la version plana que sustituye.
// #############################################################
// Roles de superficie. Se pide por SIGNIFICADO, nunca por color ni por material.
#define UIS_CARD      0    // tarjeta / panel apoyado en la pagina
#define UIS_ELEVATED  1    // menu, dialogo, tecla, chip: por encima de una tarjeta
#define UIS_ACCENT    2    // superficie de accion primaria (usa el acento del usuario)

// Color PLANO y TINTE de vidrio de cada rol. Los dos salen del tema semantico y
// del acento elegido por el usuario, asi que Claro/Oscuro y el color personal
// mandan igual en los dos materiales.
static uint16_t uiSurfFlat(int role){
  if(role == UIS_ACCENT)   return wallAccent();
  if(role == UIS_ELEVATED) return TH_SURF2;
  return TH_SURF;
}
static uint16_t uiSurfTint(int role){
  if(role == UIS_ACCENT)   return wallAccent();
  if(role == UIS_ELEVATED) return TH_GLASS2;
  return TH_GLASS;
}
// Color de texto/icono legible SOBRE ese rol.
static uint16_t uiSurfOn(int role){ return (role == UIS_ACCENT) ? TH_ONACC : TH_TXT; }

// ---- BANDA PRE-DESENFOCADA (vidrio durante una animacion) ----------------
#define UIGL_BAND_MAX_H 320              // peor caso real (cronometro 236, menu ~178)
static uint16_t* uiGlBand   = NULL;      // banda YA desenfocada (PSRAM, stride SCR_W)
static int       uiGlBandY0 = 0, uiGlBandY1 = -1;
static uint8_t   uiGlBandMix = GLASS_TINT_BASE;

static bool uiGlassBandActive(){ return uiGlBand && uiGlBandY1 >= uiGlBandY0; }
// Desenfoca la banda [y0,y1] del buffer ACTIVO una sola vez. El llamante debe
// haber dejado ya en gBuf el fondo autentico de debajo (lo que se vera a traves
// del vidrio). Devuelve false si no hay PSRAM: el llamante cae a plano y todo
// sigue funcionando, solo sin desenfoque.
static bool uiGlassBandBegin(int y0, int y1, uint16_t tint){
  uiGlBandY1 = -1;
  if(gLand) return false;                       // indexacion vertical directa: igual que el panel normal
  if(y0 < 0) y0 = 0; if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  if(y1 < y0) return false;
  int h = y1 - y0 + 1;
  // Se dimensiona al PEOR CASO REAL de los overlays que la usan (la banda del
  // cronometro, 236 filas; el menu contextual, ~178), no a pantalla completa:
  // 480 x 320 x 2 = 300 KB en vez de 768 KB. Una banda mas alta que esto NO se
  // cachea -- el llamante cae a la ruta plana/vidrio de siempre -- en vez de
  // desbordar el buffer.
  if(h > UIGL_BAND_MAX_H) return false;
  if(!uiGlBand)
    uiGlBand = (uint16_t*)heap_caps_malloc((size_t)SCR_W * UIGL_BAND_MAX_H * 2,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!uiGlBand) return false;                   // sin PSRAM: el llamante usa plano
  // Tinte adaptativo: MISMO criterio que drawLiquidGlassPanelEx (cuanto mas se
  // parecen tinte y fondo en luminancia, menos tinte), medido una sola vez sobre
  // toda la banda para que no cambie de color a mitad de la animacion.
  uint32_t lumaSum = 0; int lumaN = 0;
  for(int j = 0; j < h; j += 4){
    const uint16_t* srow = gBuf + (size_t)(y0 + j) * SCR_W;
    for(int i = 0; i < SCR_W; i += 8){ lumaSum += (uint32_t)glassLuma(srow[i]); lumaN++; }
  }
  uiGlBandMix = GLASS_TINT_BASE;
  if(lumaN > 0){
    int dif = (int)(lumaSum / (uint32_t)lumaN) - glassLuma(tint);
    if(dif < 0) dif = -dif;
    if(dif > GLASS_TINT_DIFF_MAX) dif = GLASS_TINT_DIFF_MAX;
    uiGlBandMix = (uint8_t)(GLASS_TINT_MIN + (dif * (GLASS_TINT_MAX - GLASS_TINT_MIN)) / GLASS_TINT_DIFF_MAX);
  }
  // Copiar la banda a glassBuf, desenfocarla ahi (glassBlur trabaja sobre
  // glassBuf con el ancho que se le pase) y guardarla en uiGlBand.
  if(!glassBuf) glassBuf = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!glassBuf) return false;
  for(int j = 0; j < h; j++)
    memcpy(glassBuf + (size_t)j * SCR_W, gBuf + (size_t)(y0 + j) * SCR_W, (size_t)SCR_W * 2);
  glassBlur(SCR_W, h, 6);                        // mismo radio que drawLiquidGlassPanel
  for(int j = 0; j < h; j++)
    memcpy(uiGlBand + (size_t)j * SCR_W, glassBuf + (size_t)j * SCR_W, (size_t)SCR_W * 2);
  uiGlBandY0 = y0; uiGlBandY1 = y1;
  return true;
}
static void uiGlassBandEnd(){ uiGlBandY1 = -1; }
// Libera la banda (la reclama quien necesite PSRAM: ver themeChanged/gfxReclaim).
static void uiGlassBandFree(){
  if(uiGlBand){ heap_caps_free(uiGlBand); uiGlBand = NULL; }
  uiGlBandY1 = -1;
}

// Panel de vidrio SOBRE la banda cacheada. Reproduce el material de
// drawLiquidGlassPanelEx -- tinte adaptativo, especular arriba, sombreado
// abajo, borde direccional -- pero leyendo el desenfoque YA calculado. 'a' es
// la opacidad del MATERIAL sobre lo que hubiera debajo, para que el panel pueda
// aparecer sin dejar de ser vidrio.
static void uiGlassPanelCached(int x, int y, int w, int h, int rad, uint16_t tint, uint8_t a){
  if(!uiGlassBandActive() || a == 0) return;
  if(x < 0){ w += x; x = 0; } if(y < 0){ h += y; y = 0; }
  if(x + w > SCR_W) w = SCR_W - x; if(y + h > SCR_H) h = SCR_H - y;
  if(w <= 0 || h <= 0) return;
  if(2 * rad > w) rad = w / 2; if(2 * rad > h) rad = h / 2;
  int vy0 = y > gClipY0 ? y : gClipY0;
  int vy1 = (y + h - 1) < gClipY1 ? (y + h - 1) : gClipY1;
  if(vy0 < uiGlBandY0) vy0 = uiGlBandY0;
  if(vy1 > uiGlBandY1) vy1 = uiGlBandY1;
  if(vy0 > vy1) return;
  const uint8_t CORNER_STRONG = 156, CORNER_WEAK = 104;
  for(int yy = vy0; yy <= vy1; yy++){
    int j = yy - y;
    int ins = glInset(j, h, rad);
    const uint16_t* src = uiGlBand + (size_t)(yy - uiGlBandY0) * SCR_W;
    uint16_t* dst = gBuf + (size_t)yy * SCR_W;
    float fj = (float)j;
    int i0 = x + ins, i1 = x + w - 1 - ins;
    if(i0 < gClipX0) i0 = gClipX0;
    if(i1 > gClipX1) i1 = gClipX1;
    for(int i = i0; i <= i1; i++){
      uint16_t out = mix565(src[i], tint, uiGlBandMix);
      if(fj < h * 0.45f) out = mix565(out, rgb565(255,255,255), (uint8_t)((1.0f - fj / (h * 0.45f)) * 26));
      else               out = mix565(out, rgb565(0,0,0), (uint8_t)(((fj - h * 0.45f) / (h * 0.55f)) * 30));
      dst[i] = (a == 255) ? out : mix565(dst[i], out, a);
    }
    // Borde del cristal (da el grosor). Solo si el lado cae dentro del recorte.
    uint16_t bcol = (j < 3) ? rgb565(255,255,255) : (j < h / 2 ? rgb565(205,214,228) : rgb565(22,28,40));
    bool topZone = (j < h / 2);
    uint8_t sL = topZone ? CORNER_STRONG : CORNER_WEAK;
    uint8_t sR = topZone ? CORNER_WEAK   : CORNER_STRONG;
    int lx = x + ins, rx = x + w - 1 - ins;
    if(lx >= gClipX0 && lx <= gClipX1) dst[lx] = mix565(dst[lx], bcol, (uint8_t)((int)sL * a / 255));
    if(rx >= gClipX0 && rx <= gClipX1) dst[rx] = mix565(dst[rx], bcol, (uint8_t)((int)sR * a / 255));
  }
}

// ---- LA SUPERFICIE QUE USA EL SISTEMA ------------------------------------
// Un solo punto de decision Plano/Liquid Glass para todo overlay del sistema.
// 'a' < 255 sirve para las animaciones: en Liquid Glass el panel sigue siendo
// vidrio (banda cacheada) y solo aparece con menos opacidad; en Plano es el
// relleno solido de la paleta, con su alpha. Ni un llamante vuelve a decidir
// material por su cuenta.
static void uiSurfaceA(int x, int y, int w, int h, int rad, int role, uint8_t a){
  if(a == 0) return;
  if(uiGlass){
    if(uiGlassBandActive()){ uiGlassPanelCached(x, y, w, h, rad, uiSurfTint(role), a); return; }
    if(a >= 255){ drawLiquidGlassPanel(x, y, w, h, rad, uiSurfTint(role)); return; }
    // Sin banda cacheada y con alpha: el tinte del vidrio como relleno. Es la
    // ruta de respaldo (sin PSRAM o fuera de una animacion preparada).
    fillRoundRectA(x, y, w, h, rad, uiSurfTint(role), a);
    return;
  }
  fillRoundRectA(x, y, w, h, rad, uiSurfFlat(role), a);   // PLANO: solido, sin vidrio ni blur
}
static void uiSurface(int x, int y, int w, int h, int rad, int role){
  uiSurfaceA(x, y, w, h, rad, role, 255);
}
// Superficie APOYADA EN EL WALLPAPER (bloqueo, apagado, verificacion de clave).
// Conserva los colores TH_WALL* -- que son una excepcion deliberada del tema,
// porque el wallpaper es contenido y no se retine -- pero el MATERIAL lo sigue
// eligiendo uiGlass, igual que en el resto del sistema: en Plano es solida.
static void uiWallSurface(int x, int y, int w, int h, int rad, uint16_t col, int blurR){
  if(uiGlass) drawLiquidGlassPanelEx(x, y, w, h, rad, col, blurR);
  else        fillRoundRect(x, y, w, h, rad, col);
}

// Wallpaper desenfocado reutilizable (fondo del desbloqueo y de Recientes, estilo iOS)
static uint16_t* blurBg = NULL;
static void ensureBlurBg(){
  if(blurBg) return;
  blurBg = (uint16_t*)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!blurBg) return;
  uint16_t* old = gBuf;
  // Este buffer se compone UNA SOLA VEZ en toda la sesion (el early-return de
  // arriba). Si el primer llamante llega con gLand=true o con el recorte
  // estrechado -- p.ej. saliendo del Modo Kiosco desde Juegos, que deja
  // gLand=true en cada frame-- el fillRectA de abajo se aplicaria girado o a
  // media pantalla y el fondo quedaria roto PARA SIEMPRE. Se fuerza portrait y
  // recorte completo aqui, no en cada llamante.
  bool wl = gLand; gLand = false;
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = 0; gClipY1 = SCR_H - 1;
  drawWallpaper(blurBg, true);
  setBuf(blurBg);
  // Velo del wallpaper DESENFOCADO. El wallpaper es contenido del usuario y no
  // se retine con el tema; este velo solo lo oscurece para que lo que se apoye
  // encima (Recientes, verificacion de clave, apagado) tenga contraste. Es el
  // mismo en las dos apariencias -- por eso esas pantallas usan TH_ONWALL.
  fillRectA(0, 0, SCR_W, SCR_H, rgb565(8,10,18), 70);
  setBuf(old);
  gLand = wl;
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
}
