// #############################################################
// ##  FLEX OS ULTRA  ·  TECLADO DE 4 CAPAS (ES/EN/NUM/EMOJI)
// ##  ----------------------------------------------------------
// ##  Geometria en variables (Fase A), seguimiento de contactos (B), barra
// ##  de accesos rapidos (C), portapapeles de varias ranuras (D),
// ##  autocompletado local (F), destello de tecla (G) y la maquetacion,
// ##  scroll y cursor del texto que usa el editor de Notas.
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
#include "FlexOS_Ultra_AppCamera.h"   // eslabon anterior de la cadena

// #############################################################
// ##  APP NOTAS + TECLADO 4 CAPAS (ES/EN/NUM/EMOJI)
// ##  Punteros dinamicos (mapaActivo), buffer UTF-8 seguro,
// ##  long-press para acentos y mapeo por cuadricula tactil.
// #############################################################
#define KB_COLS 10
#define KB_ROWS 3

// 4 matrices independientes de cadenas (const char*). La N con "\xC3\xB1".
// Suben aqui arriba (antes estaban debajo de la geometria) porque la altura de
// la franja de chips depende de QUE capa esta activa: en la capa numerica esa
// franja muestra los simbolos personalizados de la Fase E.
static const char* LAYOUT_ES[KB_ROWS][KB_COLS] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l","\xC3\xB1"},
  {"z","x","c","v","b","n","m",",",".","?"} };
static const char* LAYOUT_EN[KB_ROWS][KB_COLS] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l",";"},
  {"z","x","c","v","b","n","m",",",".","?"} };
static const char* LAYOUT_NUM[KB_ROWS][KB_COLS] = {
  {"1","2","3","4","5","6","7","8","9","0"},
  {"@","#","$","%","&","-","_","(",")","/"},
  {"*","\"","'",":",";","!","?","+","=","."} };
static const char* LAYOUT_EMOJI[KB_ROWS][KB_COLS] = {   // emoticones de texto (la fuente los dibuja)
  {":)",":D",":(",";)",":P","xD",":o",":|","<3",":3"},
  {"^^","o_o",">:(",":'(","B)","-_-","=)","D:",":v",":c"},
  {"uwu",":*","<_<",">_>","(y)","!!",":]","[:","T_T","o/"} };

static const char* (*mapaActivo)[KB_COLS] = LAYOUT_ES;   // <<< puntero maestro
static bool kbShift = false, kbLangEs = true;

// Resuelve lo que se muestra y lo que se escribe para UNA tecla del mapa.
// Ademas de las mayusculas, Shift en la capa ?123 convierte los parentesis
// en llaves: ( -> { y ) -> }. Asi las llaves quedan disponibles en TODO
// editor que reutilice el teclado del sistema, sin quitar simbolos ni cambiar
// la geometria 10x3. Al escribir una variante, Shift vuelve a apagarse.
static const char* kbResolveKey(const char* base, char out[6], bool consumeShift){
  if(!base || !kbShift) return base;
  bool changed = false;
  if(base[1] == 0 && base[0] >= 'a' && base[0] <= 'z'){
    out[0] = (char)(base[0] - 32); out[1] = 0; changed = true;
  } else if(!strcmp(base, "\xC3\xB1")){
    out[0] = (char)0xC3; out[1] = (char)0x91; out[2] = 0; changed = true;
  } else if(mapaActivo == LAYOUT_NUM && base[1] == 0 && (base[0] == '(' || base[0] == ')')){
    out[0] = base[0] == '(' ? '{' : '}'; out[1] = 0; changed = true;
  }
  if(!changed) return base;
  if(consumeShift) kbShift = false;
  return out;
}

// #############################################################
// ##  FASE A · GEOMETRIA DEL TECLADO EN VARIABLES
// ##  ------------------------------------------------------
// ##  Antes KB_X/KB_KW/KB_KH/KB_GAP eran #define fijos: no habia
// ##  forma de cambiar el tamano sin recompilar. Ahora son
// ##  VARIABLES que salen de gKbSize, y los nombres KB_* se
// ##  conservan como macros para que las ~80 referencias que ya
// ##  existian en Notas, Bloqueo y Wi-Fi sigan leyendose igual.
// ##
// ##  Los tres tamanos (ancho de tecla / alto / separacion / margen):
// ##    Compacto  39 / 42 / 4 / 27   -> 10*39 + 9*4 = 426 px de rejilla
// ##    Normal    43 / 48 / 4 / 6    -> 466 px  (EXACTAMENTE lo de siempre)
// ##    Grande    45 / 56 / 2 / 6    -> 468 px
// ##  Los tres caben en SCR_W=480 con margen a los dos lados, y la
// ##  cuarta fila (funciones) termina siempre por encima del borde
// ##  inferior. Ver kbSizeCheck().
// #############################################################
#if KB_SIZE_CONFIG_ON
static int kbKW = 45, kbKH = 60, kbGap = 2, kbX = 6;
// Recalcula la geometria a partir de gKbSize. Se llama al cargar las
// preferencias y cada vez que el usuario cambia el tamano en Ajustes: aplica
// YA, sin reiniciar, porque toda la geometria se consulta en cada repintado.
//
// POR QUE ESTOS NUMEROS (revisados tras probar en la placa: el teclado
// anterior salia pequeno e incomodo):
//  · El ANCHO esta topado por las 10 columnas. Con 480 px de pantalla, el
//    maximo razonable es 45 px de tecla y 2 px de separacion (450+18=468, o
//    sea 6 px de margen a cada lado). Por ahi ya no se puede crecer mas.
//  · El ALTO es donde SI hay sitio, y es lo que de verdad se nota con el dedo:
//    la pantalla tiene 800 px de alto y las 4 filas ocupan como mucho ~300.
//    Por eso el tamano crece sobre todo hacia abajo.
//      Compacto  43 x 50   (lo que antes era "Normal")
//      Normal    45 x 60   (por defecto: 25% mas alto que antes)
//      Grande    45 x 72   (50% mas alto que antes)
static void kbApplySize(){
  if(gKbSize == KB_SIZE_COMPACT){ kbKW = 43; kbKH = 50; kbGap = 4; }
  else if(gKbSize == KB_SIZE_BIG){ kbKW = 45; kbKH = 72; kbGap = 2; }
  else { kbKW = 45; kbKH = 60; kbGap = 2; }
  int gw = KB_COLS * kbKW + (KB_COLS - 1) * kbGap;    // ancho real de la rejilla
  kbX = (SCR_W - gw) / 2; if(kbX < 2) kbX = 2;        // centrada, nunca pegada al borde
}
#else
// Interruptor a 0: exactamente los numeros de siempre, y kbApplySize no hace nada.
static const int kbKW = 43, kbKH = 48, kbGap = 4, kbX = 6;
static void kbApplySize(){}
#endif
#define KB_KW   kbKW
#define KB_KH   kbKH
#define KB_GAP  kbGap
#define KB_X    kbX

// Extras que se dibujan ENCIMA de las teclas (barra superior de la Fase C y
// franja de chips de la Fase F). Solo los muestra la superficie que los pide
// (hoy: Notas). El Bloqueo y el Wi-Fi ponen kbExtrasOn=false a proposito --
// un boton de "portapapeles" o de "ajustes" accesible desde la pantalla de
// contrasena seria un agujero de seguridad, no una comodidad.
static bool kbExtrasOn = false;
static int  kbToolbarH(){ return (KB_TOOLBAR_ON && kbExtrasOn && gKbToolbar) ? 56 : 0; }
// La franja fina de arriba tiene DOS inquilinos que nunca coinciden: los chips
// de autocompletado (capas de letras) y los simbolos personalizados de la Fase E
// (capa numerica). Sugerir palabras mientras se teclean numeros no tendria
// sentido, asi que se reparten la misma franja en vez de sumar dos.
static bool kbChipsWant(){
  if(!kbExtrasOn) return false;
  if(mapaActivo == LAYOUT_NUM) return KB_SETTINGS_ON ? true : false;
  return KB_AUTOCOMPLETE_ON && gKbPredict;
}
static int  kbChipsH(){   return kbChipsWant() ? 32 : 0; }
static int  kbTopH(){     return kbToolbarH() + kbChipsH(); }
// FRANJA RESERVADA DEBAJO DEL TECLADO. La barra de navegacion del sistema vive
// en los ultimos NAV_H pixeles y es del sistema, no de la superficie que abre el
// teclado. Notas pone aqui NAV_H para que el teclado se apoye ENCIMA de la barra
// (igual que en Android); las pantallas de clave, Wi-Fi y Ajustes del teclado lo
// dejan en 0 a proposito -- ahi no hay barra de navegacion que respetar (y en la
// de clave, ofrecer una via de escape seria un agujero de seguridad).
// Toda la geometria del teclado -- dibujo Y hit-test -- sale de kbRowsTop(), asi
// que cambiar esta sola variable mueve el teclado entero de forma coherente.
static int kbBotReserve = 0;
// Y de la PRIMERA FILA DE TECLAS. Es lo que siempre significo KB_Y, y sigue
// significando lo mismo: los extras crecen hacia ARRIBA, no empujan las teclas.
static int  kbRowsTop(){  return SCR_H - kbBotReserve - 4 * (KB_KH + KB_GAP) - 6; }
#define KB_Y kbRowsTop()
// Y donde empieza el PANEL entero (con barra y chips incluidos). Es el limite
// de abajo del area de texto y el borde superior de la banda a volcar.
static int  kbPanelTop(){ return kbRowsTop() - 4 - kbTopH(); }
static int  kbToolbarY(){ return kbPanelTop() + 4; }
static int  kbChipsY(){   return kbToolbarY() + kbToolbarH(); }
// Y de la fila de funciones (shift, capa, idioma, espacio, borrar, enter).
static int  kbFuncY(){    return kbRowsTop() + 3 * (KB_KH + KB_GAP); }

// ---- Fila de funciones: geometria proporcional a la rejilla ----
// Antes las 6 teclas tenian x y ancho HARDCODEADOS (6, 68, 126, 178, 372, 420)
// en cuatro sitios distintos, y el hit-test los repetia como numeros sueltos
// ("if(px < 64) ... else if(px < 122)"). Con la rejilla ya no fija habria que
// tocarlos en todos lados; peor: cambiar uno y olvidar otro deja teclas que se
// ven en un sitio y responden en otro. Ahora salen de una sola tabla de pesos.
#define KB_FKEYS 6
static const float KB_FW[KB_FKEYS] = { 0.135f, 0.125f, 0.110f, 0.420f, 0.100f, 0.110f };
static int kbFKeyW(int i){
  if(i < 0 || i >= KB_FKEYS) return 0;
  int gw = KB_COLS * KB_KW + (KB_COLS - 1) * KB_GAP;
  int usable = gw - (KB_FKEYS - 1) * KB_GAP;
  int w = (int)(usable * KB_FW[i] + 0.5f);
  return w < 20 ? 20 : w;
}
static int kbFKeyX(int i){
  int x = KB_X;
  for(int k = 0; k < i; k++) x += kbFKeyW(k) + KB_GAP;
  return x;
}
// Devuelve 0..5 (shift, capa, idioma, espacio, borrar, enter) o -1.
// Mismo criterio que kbCellAt: cada tecla se queda con la separacion que tiene
// a su derecha, asi que no hay franjas muertas entre botones.
static int kbFRowHit(int px, int py){
  int fy = kbFuncY();
  if(py < fy - KB_GAP || py > fy + KB_KH + KB_GAP) return -1;
  if(px < KB_X - KB_GAP) return -1;
  for(int i = 0; i < KB_FKEYS; i++){
    int x = kbFKeyX(i);
    if(px <= x + kbFKeyW(i) + KB_GAP) return i;
  }
  return -1;
}
// Comprobacion de que NINGUN tamano se sale de la pantalla. No dibuja nada: es
// una asercion barata que corre una vez al arrancar y deja rastro en el log.
static bool kbSizeCheck(){
  int gw = KB_COLS * KB_KW + (KB_COLS - 1) * KB_GAP;
  int fw = kbFKeyX(KB_FKEYS - 1) + kbFKeyW(KB_FKEYS - 1);
  int bot = kbFuncY() + KB_KH;
  return (KB_X >= 0) && (KB_X + gw <= SCR_W) && (fw <= SCR_W) &&
         (bot <= SCR_H - kbBotReserve - 4) && (kbPanelTop() > 120);
}

// ---- Colores del teclado (Fase E: contraste alto + opacidad + estilo) ----
// Se resuelven aqui, en un solo sitio, en vez de estar escritos a mano en cada
// funcion de dibujo. Dos ejes INDEPENDIENTES:
//   · gKbHiCon (Ajustes del teclado -> contraste alto) gana siempre: negro puro
//     con texto blanco y ambar de foco. Es una ayuda de ACCESIBILIDAD, asi que a
//     proposito NO sigue la paleta -- su valor es justamente ser extrema.
//   · si no, salen del TEMA SEMANTICO, o sea que el teclado se aclara u oscurece
//     con el resto del sistema en vez de quedarse en su gris fijo.
static uint16_t kbColKey(){     return gKbHiCon ? rgb565(0,0,0)       : TH_KEYFACE; }
static uint16_t kbColKeyTxt(){  return gKbHiCon ? rgb565(255,255,255) : TH_TXT; }
static uint16_t kbColFn(){      return gKbHiCon ? rgb565(24,24,24)    : TH_KEYALT; }
static uint16_t kbColFnOn(){    return gKbHiCon ? rgb565(255,210,0)   : TH_PRIM; }
static uint16_t kbColFnOnTxt(){ return gKbHiCon ? rgb565(0,0,0)       : TH_ONACC; }
// Panel: con Liquid Glass el TINTE del tema; plano, el fondo de teclado del tema.
static uint16_t kbColPanel(){   return gKbHiCon ? rgb565(0,0,0)       : (uiGlass ? TH_GLASS : TH_KEYPANEL); }
static uint16_t kbColEdge(){    return gKbHiCon ? rgb565(255,255,255) : TH_BORDER; }
// Tecla PRESIONADA: acento primario mezclado hacia la cara de la tecla, asi
// destaca sobre ella tanto en oscuro como en claro.
static uint16_t kbColPress(){   return gKbHiCon ? rgb565(255,210,0)   : mix565(TH_PRIM, TH_KEYFACE, 60); }
// Tamano de letra de las teclas (Fase E). El sistema de tallas de drawText es
// entero (1,2,3...), asi que "Tamano de fuente" mueve esa talla, no un factor.
static int kbFontSize(){ return gKbFontSc == 0 ? 1 : gKbFontSc == 2 ? 3 : 2; }
// Alto de linea aproximado de esa talla, para centrar el glifo en la tecla.
static int kbFontDy(){   return gKbFontSc == 0 ? 4 : gKbFontSc == 2 ? 12 : 8; }
static int kbRadius(){   return gKbStyle == 1 ? 0 : 6; }   // 1 = "Cuadrada"

// Pinta UNA tecla con el estilo elegido (Fase E) y el destello de pulsada
// (Fase G). Solo primitivos en la firma, como manda el proyecto.
static void kbPaintKey(int x, int y, int w, int h, const char* label, int fontSize, uint16_t bg, uint16_t txt, bool pressed){
  int r = kbRadius();
  uint16_t fill = pressed ? kbColPress() : bg;
  if(gKbStyle == 2){                       // "Contorno": sin relleno, solo borde
    if(pressed) fillRoundRect(x, y, w, h, r, fill);
    drawRoundRect(x, y, w, h, r, kbColEdge());
  } else {
    fillRoundRect(x, y, w, h, r, fill);
  }
  if(label && label[0]) drawTextC(x + w / 2, y + h / 2 - kbFontDy(), label, fontSize, txt);
}

// Fondo del panel del teclado. Un solo sitio para las tres superficies, con la
// opacidad de la Fase E aplicada: al 100% es el vidrio/relleno de siempre; por
// debajo se usa un relleno con alfa para que se transparente lo que hay detras
// (drawLiquidGlassPanel no admite alfa, asi que a opacidad parcial se cambia a
// la ruta plana con alfa -- documentado, no es un olvido).
static void kbPaintPanel(int y0, uint16_t tint){
  int h = SCR_H - y0;
  if(h <= 0) return;
  uint8_t a = (uint8_t)(gKbOpacity * 255 / 100);
  if(uiGlass && gKbOpacity >= 100){ drawLiquidGlassPanel(0, y0, SCR_W, h, 0, tint); return; }
  if(gKbOpacity >= 100) fillRect(0, y0, SCR_W, h, tint);
  else                  fillRectA(0, y0, SCR_W, h, tint, a);
}

// ---- Mapeo por cuadricula: (x,y) -> celda (fila*COLS+col) o -1 ----
// Sube aqui (antes vivia dentro del bloque de Notas) porque ahora la usan
// tambien la ruta multitoque de la Fase B y el editor de atajos de la Fase E,
// que se definen antes que la app.
// El area sensible de cada tecla es su PASO COMPLETO (tecla + separacion), no
// solo el rectangulo pintado. Antes, caer en los 2-4 px de separacion no era
// ninguna tecla: el toque se perdia en silencio y la sensacion era la de un
// teclado que "no responde bien". Ahora no hay huecos muertos: cada punto de la
// rejilla pertenece a alguna tecla. Lo que se ve sigue siendo igual; lo que
// cambia es lo que se puede tocar.
static int kbCellAt(int px, int py){
  int pitchX = KB_KW + KB_GAP, pitchY = KB_KH + KB_GAP;
  int dx = px - KB_X, dy = py - KB_Y;
  if(dx < 0 || dy < 0) return -1;
  int c = dx / pitchX, r = dy / pitchY;
  if(c < 0 || c >= KB_COLS || r < 0 || r >= KB_ROWS) return -1;
  return r * KB_COLS + c;
}

// ---- FASE E: paleta de simbolos personalizables ----
// El usuario elige 4 de estos 16. Se guardan como indice, no como texto, para
// que sea IMPOSIBLE acabar con un caracter que la fuente 5x7 no dibuje.
#define KB_SYM_POOL_N 16
static const char* KB_SYM_POOL[KB_SYM_POOL_N] = {
  "@","#","$","%","&","*","+","=","/","\\","(",")","[","]","<",">" };
static const char* kbSymAt(int i){
  if(i < 0 || i >= KB_SYMS) return "";
  int k = gKbSym[i]; if(k < 0 || k >= KB_SYM_POOL_N) k = 0;
  return KB_SYM_POOL[k];
}

// #############################################################
// ##  FASE B · SEGUIMIENTO DE CONTACTOS DEL TECLADO
// ##  ------------------------------------------------------
// ##  Esto NO sustituye a struct Touch: es una via rapida que
// ##  corre EN PARALELO y solo dentro de las superficies de
// ##  teclado. Todo lo demas (arrastre de manijas, long-press
// ##  de acentos, menu contextual, gestos) sigue leyendo T.
// ##
// ##  Idea: cada dedo que el GT911 reporta trae su TRACK ID. Un
// ##  id que aparece por primera vez sobre una tecla = "key
// ##  down" -> la tecla se escribe YA, sin esperar a que ese
// ##  dedo se levante. Cuando el id desaparece se libera su
// ##  hueco y no se escribe nada otra vez. Asi, apoyar la
// ##  siguiente tecla mientras la anterior aun se esta soltando
// ##  no se traba ni pierde pulsaciones (sensacion de rollover).
// #############################################################
#define KB_TRACK_MAX KB_MAXPOINTS
#define KB_EV_MAX    8
static int  kbTrkId[KB_TRACK_MAX]   = { -1, -1, -1, -1, -1 };
static int  kbTrkCell[KB_TRACK_MAX] = { -1, -1, -1, -1, -1 };
static int  kbEvCell[KB_EV_MAX];      // celda de la rejilla del evento i (-1 si no la hay)
static int  kbEvFn[KB_EV_MAX];        // tecla de funcion del evento i (-1 si no la hay)
static int  kbEvN = 0;
// kbMtOk: la via rapida ha demostrado que FUNCIONA en esta superficie (ha
// disparado al menos una tecla desde que se entro). Es lo que decide quien
// escribe: mientras sea false manda la ruta clasica de soltar, y en cuanto se
// pone a true la ruta de soltar deja de escribir del todo. Asi es IMPOSIBLE que
// las dos escriban la misma tecla (letras duplicadas en la hoja), y si el panel
// no diera puntos multiples el teclado seguiria funcionando igual que siempre.
static bool kbMtOk     = false;
static int  kbMtMaxPts = 0;           // maximo de dedos vistos a la vez (diagnostico de Ajustes)
static uint32_t gKbTypingMs = 0;      // millis del ultimo frame con dedos sobre el teclado

static void kbMtReset(){
  for(int t = 0; t < KB_TRACK_MAX; t++){ kbTrkId[t] = -1; kbTrkCell[t] = -1; }
  kbEvN = 0;
}
// Reinicio COMPLETO al entrar en una superficie de teclado: ademas de olvidar
// los contactos, se vuelve a poner en duda si la via rapida funciona. Asi cada
// pantalla decide por si misma quien escribe, y una placa cuyo panel no diera
// puntos multiples cae sola en el comportamiento de siempre.
static void kbMtSurfaceReset(){ kbMtReset(); kbMtOk = false; }
// Devuelve cuantas teclas NUEVAS se han tocado en este frame (0 si ninguna).
//
// ORDEN DE ESCRITURA (esto es lo que se pidio: quien toca primero, escribe
// primero). Se resuelve en dos niveles:
//  1) Entre FRAMES: la tecla se dispara en el mismo frame en que aparece su
//     dedo, y los frames se procesan en orden. El GT911 publica cada ~10 ms, o
//     sea que dos toques separados por 20 ms caen en frames distintos y salen
//     SIEMPRE en el orden correcto.
//  2) Dentro de un MISMO frame (dos dedos a menos de ~10 ms): el instante real
//     ya no se puede saber -- el chip los reporta juntos, sin marca de tiempo.
//     Se respeta el orden en que los publica el propio GT911, que es el orden
//     en que su firmware los detecto. No se inventa nada mejor que eso.
static int kbMtPoll(){
  kbEvN = 0;
  if(!KB_MULTITOUCH_ON || !gKbFastType) return 0;
  int n = gtPollMulti();
  if(n < 0) return 0;                       // sin dato utilizable: no se toca el seguimiento
  int live = 0;
  for(int p = 0; p < KB_MAXPOINTS; p++) if(gKbPoints[p].active) live++;
  if(live > kbMtMaxPts) kbMtMaxPts = live;  // diagnostico: cuantos puntos da de verdad este panel
  bool seen[KB_TRACK_MAX];
  for(int t = 0; t < KB_TRACK_MAX; t++) seen[t] = false;
  for(int p = 0; p < KB_MAXPOINTS; p++){
    if(!gKbPoints[p].active) continue;
    int id = gKbPoints[p].id, slot = -1;
    for(int t = 0; t < KB_TRACK_MAX; t++) if(kbTrkId[t] == id){ slot = t; break; }
    if(slot >= 0){ seen[slot] = true; gKbTypingMs = millis(); continue; }   // dedo ya conocido
    int cell = kbCellAt(gKbPoints[p].x, gKbPoints[p].y);
    int fn   = kbFRowHit(gKbPoints[p].x, gKbPoints[p].y);
    if(cell < 0 && fn < 0) continue;                       // fuera del teclado: eso es cosa de T
    for(int t = 0; t < KB_TRACK_MAX; t++) if(kbTrkId[t] < 0){ slot = t; break; }
    if(slot < 0) continue;                                 // los 5 huecos ocupados
    kbTrkId[slot] = id; kbTrkCell[slot] = cell; seen[slot] = true;
    gKbTypingMs = millis();
    if(kbEvN < KB_EV_MAX){ kbEvCell[kbEvN] = cell; kbEvFn[kbEvN] = fn; kbEvN++; }
  }
  for(int t = 0; t < KB_TRACK_MAX; t++) if(kbTrkId[t] >= 0 && !seen[t]){ kbTrkId[t] = -1; kbTrkCell[t] = -1; }
  if(kbEvN > 0) kbMtOk = true;
  return kbEvN;
}
// true mientras se este tecleando de verdad (hay dedos sobre la rejilla). Lo
// consulta el gesto de suspension: ver suspGestureUpdate. La marca la ponen
// tanto la via rapida como los ticks de Notas y Bloqueo, asi que vale tambien
// con la escritura rapida desactivada.
static bool kbTypingNow(){ return (millis() - gKbTypingMs) < 500; }
static void kbTypingMark(){ gKbTypingMs = millis(); }
// QUIEN ESCRIBE. true = manda la via rapida (al tocar) y la ruta de soltar NO
// escribe ni una letra. Esta es la regla que hace imposible que salgan letras
// duplicadas en la hoja: nunca hay dos caminos activos a la vez.
static bool kbFastActive(){ return KB_MULTITOUCH_ON && gKbFastType && kbMtOk; }
// true si esa celda tiene ahora mismo un dedo encima (para pintarla hundida).
static bool kbCellHeld(int cell){
  if(cell < 0) return false;
  for(int t = 0; t < KB_TRACK_MAX; t++) if(kbTrkId[t] >= 0 && kbTrkCell[t] == cell) return true;
  return false;
}

// #############################################################
// ##  FASE G · DESTELLO DE TECLA PRESIONADA
// ##  Se repinta SOLO el rectangulo de esa tecla y se vuelca su
// ##  banda: ni pantalla completa, ni fillScreen, ni parpadeo.
// #############################################################
static int      kbFxCell = -1;      // celda con destello vivo
static uint32_t kbFxT0   = 0;
static void kbFxStart(int cell){
  if(!KB_ANIM_POLISH_ON || cell < 0) return;
  kbFxCell = cell; kbFxT0 = millis();
}
static bool kbFxActive(){ return KB_ANIM_POLISH_ON && kbFxCell >= 0; }
// Progreso 0..255 del destello (255 = recien tocada, 0 = apagado).
static int kbFxLevel(int cell){
  if(!kbFxActive() || cell != kbFxCell) return 0;
  uint32_t dt = millis() - kbFxT0;
  if((int)dt >= gKbFxMs) return 0;
  return 255 - (int)(dt * 255 / (uint32_t)gKbFxMs);
}
// Repinta UNA celda ya, en su sitio, sin tocar el resto del teclado. Es la
// pieza que hace que el destello no cueste un repintado entero.
static void kbPaintCellNow(int cell, bool pressed, uint16_t bg, uint16_t txt){
  if(cell < 0) return;
  int r = cell / KB_COLS, c = cell % KB_COLS;
  int x = KB_X + c * (KB_KW + KB_GAP), y = KB_Y + r * (KB_KH + KB_GAP);
  char up[6];
  const char* k = kbResolveKey(mapaActivo[r][c], up, false);
  setBuf(fb);
  kbPaintKey(x, y, KB_KW, KB_KH, k, kbFontSize(), bg, txt, pressed);
  flxFlush(y - 1, y + KB_KH + 1);
}
// Enciende el destello Y lo pinta en el acto. Es la ruta para cuando la
// escritura rapida esta apagada: ahi el feedback tiene que salir en T.pressed,
// porque la tecla no se escribe hasta soltar y sin esto no habria ninguna
// senal de que el toque llego.
static void kbFxPress(int cell, uint16_t bg, uint16_t txt){
  if(!KB_ANIM_POLISH_ON || cell < 0) return;
  kbFxStart(cell);
  kbPaintCellNow(cell, true, bg, txt);
}
// Repinta la tecla del destello cuando se apaga. La llaman los ticks de las
// superficies con SUS colores (primitivos en la firma, como pide el proyecto).
static bool kbFxTick(uint16_t bg, uint16_t txt){
  if(!kbFxActive()) return false;
  if((int)(millis() - kbFxT0) < gKbFxMs) return false;
  int cell = kbFxCell; kbFxCell = -1;
  kbPaintCellNow(cell, false, bg, txt);
  return true;
}

// #############################################################
// ##  FASE D · PORTAPAPELES DE VARIAS RANURAS
// ##  ------------------------------------------------------
// ##  PERSISTENCIA (decision explicita): solo sobreviven al
// ##  reinicio las ranuras FIJADAS. El resto son de sesion. Se
// ##  guardan en NVS con una clave por ranura ("clip0".."clip11")
// ##  dentro de la misma namespace "flexos" del resto del
// ##  sistema. Motivo: el portapapeles de trabajo cambia
// ##  constantemente y escribir flash en cada copia gastaria
// ##  ciclos de NVS para nada; lo que el usuario marca con el
// ##  pin es justo lo que dice "esto quiero conservarlo".
// #############################################################
static char clipboard[512] = "";      // <<< buffer clasico: sigue existiendo (KB_CLIPBOARD_MULTI_ON 0) >>>

static void clipSavePinned(){
  if(!KB_CLIPBOARD_MULTI_ON) return;
  prefs.begin("flexos", false);
  char key[10];
  for(int i = 0; i < CLIP_SLOTS; i++){
    snprintf(key, sizeof(key), "clip%d", i);
    if(gClip[i].used && gClip[i].pinned) prefs.putString(key, gClip[i].text);
    else                                 prefs.remove(key);
  }
  prefs.end();
}
static void clipLoadPinned(){
  if(!KB_CLIPBOARD_MULTI_ON) return;
  prefs.begin("flexos", true);
  char key[10];
  for(int i = 0; i < CLIP_SLOTS; i++){
    snprintf(key, sizeof(key), "clip%d", i);
    String s = prefs.getString(key, "");
    if(s.length() > 0){
      s.toCharArray(gClip[i].text, CLIP_TXT_MAX);
      gClip[i].used = true; gClip[i].pinned = true; gClip[i].ts = 0;
    }
  }
  prefs.end();
}
static int clipCount(){
  int n = 0;
  for(int i = 0; i < CLIP_SLOTS; i++) if(gClip[i].used) n++;
  return n;
}
// Inserta un texto nuevo. Si no hay hueco libre, sobrescribe la ranura NO
// FIJADA mas antigua -- una fijada no se descarta jamas.
static void clipPush(const char* s){
  if(!s || !s[0]) return;
  if(s != clipboard){ strncpy(clipboard, s, sizeof(clipboard) - 1); clipboard[sizeof(clipboard) - 1] = 0; }
  if(!KB_CLIPBOARD_MULTI_ON) return;
  for(int i = 0; i < CLIP_SLOTS; i++)                     // repetido: solo se refresca la fecha
    if(gClip[i].used && !strncmp(gClip[i].text, s, CLIP_TXT_MAX - 1)){ gClip[i].ts = millis(); return; }
  int slot = -1;
  for(int i = 0; i < CLIP_SLOTS; i++) if(!gClip[i].used){ slot = i; break; }
  if(slot < 0){
    uint32_t oldest = 0xFFFFFFFFu;
    for(int i = 0; i < CLIP_SLOTS; i++) if(!gClip[i].pinned && gClip[i].ts <= oldest){ oldest = gClip[i].ts; slot = i; }
    if(slot < 0) return;                                  // las 12 estan fijadas: no se toca ninguna
  }
  strncpy(gClip[slot].text, s, CLIP_TXT_MAX - 1);
  gClip[slot].text[CLIP_TXT_MAX - 1] = 0;
  gClip[slot].used = true; gClip[slot].pinned = false; gClip[slot].ts = millis();
}
static void clipDel(int i){
  if(i < 0 || i >= CLIP_SLOTS) return;
  bool wasPinned = gClip[i].pinned;
  gClip[i].used = false; gClip[i].pinned = false; gClip[i].text[0] = 0; gClip[i].ts = 0;
  if(wasPinned) clipSavePinned();
}
static void clipTogglePin(int i){
  if(i < 0 || i >= CLIP_SLOTS || !gClip[i].used) return;
  gClip[i].pinned = !gClip[i].pinned;
  clipSavePinned();
}
static void clipClearUnpinned(){
  for(int i = 0; i < CLIP_SLOTS; i++) if(gClip[i].used && !gClip[i].pinned){ gClip[i].used = false; gClip[i].text[0] = 0; gClip[i].ts = 0; }
}

// #############################################################
// ##  FASE F · AUTOCOMPLETADO SIMULADO (LISTA LOCAL FIJA)
// ##  ------------------------------------------------------
// ##  QUE ES: dos listas de palabras frecuentes, una por idioma,
// ##  escritas a mano en el propio .ino. Se busca por PREFIJO y
// ##  se ofrecen hasta 3 coincidencias.
// ##  QUE NO ES: un modelo de lenguaje. No aprende, no entiende
// ##  el contexto y no predice la palabra siguiente. La pantalla
// ##  "Sobre teclado" lo dice con esas mismas palabras -- vender
// ##  esto como "IA" seria mentir.
// ##  COSTE: ~250 entradas por idioma, unos 3 KB de flash cada
// ##  lista. La busqueda es un recorrido lineal que corta en
// ##  cuanto junta 3 resultados: microsegundos por tecla.
// #############################################################
static const char* KB_DICT_ES[] = {
  "a","abajo","abrir","acaso","aceptar","acuerdo","adelante","adem\xC3\xA1s","agua","ahora",
  "algo","alguien","alguno","alli","alto","amigo","amor","antes","a\xC3\xB1o","apagar",
  "aplicacion","aprender","aqui","archivo","arriba","asi","ayer","ayuda","bajar","bastante",
  "bien","borrar","brillo","buenas","bueno","buscar","caja","calle","cambiar","camino",
  "cargar","casa","caso","celular","cerca","cerrar","cielo","ciudad","claro","codigo",
  "color","comenzar","comida","como","completo","compartir","comprar","con","conectar","conocer",
  "contacto","contra","copiar","correo","cosa","crear","cuando","cuenta","dar","datos",
  "deber","decir","dejar","delante","dentro","desde","despu\xC3\xA9s","dia","dinero","dispositivo",
  "donde","dormir","durante","el","ella","empezar","encender","encontrar","entender","entonces",
  "entrar","enviar","error","escribir","escuchar","espacio","esperar","est\xC3\xA1","este","estar",
  "falta","familia","favor","fecha","final","forma","foto","fuera","fuerte","funcionar",
  "gente","grande","gracias","guardar","gustar","haber","hablar","hacer","hasta","hecho",
  "hola","hombre","hora","hoy","idea","idioma","imagen","importante","informacion","instalar",
  "internet","ir","juego","jugar","junto","lado","largo","leer","lento","letra",
  "libro","limpiar","llamar","llegar","llevar","luego","lugar","luz","madre","mal",
  "mandar","manera","ma\xC3\xB1" "ana","mano","mas","mayor","mejor","memoria","menos","mensaje",
  "mes","mientras","minuto","mirar","mismo","modo","momento","mostrar","mover","mucho",
  "mujer","mundo","musica","muy","nada","necesitar","ni\xC3\xB1" "o","noche","nombre","normal",
  "nosotros","noticia","nuevo","numero","nunca","ocurrir","oir","opcion","orden","otro",
  "padre","pagina","palabra","pantalla","papel","para","parecer","parte","pasar","pedir",
  "pelicula","pensar","peque\xC3\xB1o","perder","pero","persona","poco","poder","poner","porque",
  "posible","primero","probar","problema","pronto","propio","punto","quedar","querer","qui\xC3\xA9n",
  "quitar","rapido","razon","recibir","reiniciar","respuesta","resultado","saber","salir","seguir",
  "segundo","seguro","seleccionar","semana","sentir","se\xC3\xB1" "al","ser","servicio","siempre","siguiente",
  "silencio","sistema","sitio","sobre","solo","sonido","tama\xC3\xB1o","tambi\xC3\xA9n","tarde","teclado",
  "telefono","tema","tener","texto","tiempo","tipo","tocar","todo","tomar","trabajo",
  "traer","tratar","ultimo","usar","usuario","valor","venir","ventana","ver","verdad",
  "vez","viaje","vida","volver","voz","ya","zona" };
static const char* KB_DICT_EN[] = {
  "about","above","accept","account","add","after","again","against","all","allow",
  "almost","also","always","and","another","answer","any","app","apply","are",
  "around","ask","away","back","battery","because","become","been","before","begin",
  "behind","being","believe","below","best","better","between","big","bit","book",
  "both","bring","build","button","buy","call","camera","can","cancel","car",
  "care","carry","case","change","check","child","choose","city","clean","clear",
  "click","close","code","cold","color","come","company","computer","connect","contact",
  "continue","copy","could","country","create","cut","dark","data","day","delete",
  "device","different","display","does","done","door","down","download","draw","drive",
  "during","each","early","easy","edit","email","end","enough","enter","error",
  "even","ever","every","example","exit","face","fact","fail","family","far",
  "fast","feel","field","file","fill","find","fine","first","folder","follow",
  "font","food","for","force","form","free","friend","from","full","game",
  "get","give","good","great","group","hand","happen","happy","hard","have",
  "head","hear","help","here","high","hold","home","hope","hour","house",
  "how","idea","image","import","inside","install","just","keep","key","keyboard",
  "kind","know","language","large","last","late","learn","leave","left","less",
  "let","letter","level","life","light","like","line","list","little","live",
  "load","local","lock","long","look","love","made","make","many","mark",
  "may","mean","memory","menu","message","might","mind","minute","miss","mode",
  "money","month","more","morning","most","move","much","music","must","name",
  "near","need","network","never","new","news","next","nice","night","none",
  "note","nothing","now","number","off","offer","often","once","only","open",
  "option","order","other","over","page","paper","part","password","people","phone",
  "photo","pick","place","play","please","point","power","press","print","question",
  "quick","quit","read","ready","real","reason","record","remove","repeat","reply",
  "report","reset","rest","result","return","right","room","run","same","save",
  "say","screen","search","second","see","select","send","server","service","set",
  "settings","share","short","should","show","side","sign","since","size","small",
  "some","soon","sound","space","speak","start","state","stay","step","still",
  "stop","store","story","study","such","support","sure","system","table","take",
  "talk","tell","test","text","than","thank","that","their","them","then",
  "there","these","they","thing","think","this","time","today","together","too",
  "tool","touch","try","turn","type","under","until","update","upload","use",
  "user","very","view","wait","walk","want","watch","water","way","week",
  "well","what","when","where","which","while","white","who","why","will",
  "window","with","word","work","world","would","write","year","yes","your" };
#define KB_DICT_ES_N ((int)(sizeof(KB_DICT_ES) / sizeof(KB_DICT_ES[0])))
#define KB_DICT_EN_N ((int)(sizeof(KB_DICT_EN) / sizeof(KB_DICT_EN[0])))

// Devuelve el siguiente caracter "plegado": minuscula y sin acento. Asi
// escribir "mas" encuentra "m\xC3\xA1s" y "man" encuentra "ma\xC3\xB1" "ana", que es lo que
// espera cualquiera que escriba rapido sin pararse a poner tildes.
static char kbFoldCh(const char** ps){
  const char* s = *ps;
  unsigned char c = (unsigned char)s[0];
  if(c == 0){ return 0; }
  if(c < 0x80){ *ps = s + 1; return (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); }
  if(c == 0xC3 && s[1]){
    unsigned char d = (unsigned char)s[1];
    *ps = s + 2;
    if((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) return 'a';
    if((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) return 'e';
    if((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) return 'i';
    if((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) return 'o';
    if((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) return 'u';
    if(d == 0x91 || d == 0xB1) return 'n';                 // N/n con virgulilla
    return '?';
  }
  s++; while(((unsigned char)*s & 0xC0) == 0x80) s++;      // otro multibyte: se salta entero
  *ps = s; return '?';
}
static bool kbStartsWith(const char* word, const char* pref){
  const char* w = word; const char* p = pref;
  for(;;){
    const char* pp = p; char pc = kbFoldCh(&pp);
    if(pc == 0) return true;                                // el prefijo se acabo: encaja
    const char* ww = w; char wc = kbFoldCh(&ww);
    if(wc == 0 || wc != pc) return false;
    p = pp; w = ww;
  }
}
static bool kbSameWord(const char* a, const char* b){
  const char* x = a; const char* y = b;
  for(;;){
    const char* xx = x; char ac = kbFoldCh(&xx);
    const char* yy = y; char bc = kbFoldCh(&yy);
    if(ac != bc) return false;
    if(ac == 0) return true;
    x = xx; y = yy;
  }
}
// Revision ortografica basica (Fase E/F): "conocida" = esta en el diccionario
// del idioma activo o es uno de los atajos del usuario. Es EXACTAMENTE eso, no
// un corrector gramatical.
static bool kbDictHas(const char* w){
  if(!w || !w[0]) return true;
  const char* const* d = kbLangEs ? KB_DICT_ES : KB_DICT_EN;
  int n = kbLangEs ? KB_DICT_ES_N : KB_DICT_EN_N;
  for(int i = 0; i < n; i++) if(kbSameWord(d[i], w)) return true;
  for(int i = 0; i < KB_SC_MAX; i++) if(gKbScAbr[i][0] && kbSameWord(gKbScAbr[i], w)) return true;
  return false;
}
// Emojis sugeridos (solo glifos que la fuente ya dibuja, los mismos de
// LAYOUT_EMOJI). Es una tabla disparador->emoticon, no un clasificador.
#define KB_EMOSUG_N 10
static const char* KB_EMOSUG_W[KB_EMOSUG_N] = { "risa","amor","triste","guino","abrazo","laugh","love","sad","wink","hug" };
static const char* KB_EMOSUG_E[KB_EMOSUG_N] = { ":D",  "<3",  ":(",    ";)",    "(y)",   ":D",   "<3",  ":(", ";)",  "(y)" };

// Busca hasta maxn coincidencias por prefijo. Orden: primero los atajos de
// texto del usuario (una abreviacion escrita entera gana a cualquier palabra),
// luego el diccionario, luego el emoji sugerido si toca.
static int kbSuggest(const char* pref, const char** out, int maxn){
  int n = 0;
  if(!KB_AUTOCOMPLETE_ON || !gKbPredict || !pref || !pref[0] || maxn <= 0) return 0;
  for(int i = 0; i < KB_SC_MAX && n < maxn; i++)
    if(gKbScAbr[i][0] && gKbScExp[i][0] && kbSameWord(gKbScAbr[i], pref)) out[n++] = gKbScExp[i];
  const char* const* d = kbLangEs ? KB_DICT_ES : KB_DICT_EN;
  int dn = kbLangEs ? KB_DICT_ES_N : KB_DICT_EN_N;
  for(int i = 0; i < dn && n < maxn; i++){
    if(!kbStartsWith(d[i], pref)) continue;
    bool dup = false;
    for(int k = 0; k < n; k++) if(kbSameWord(out[k], d[i])) dup = true;
    if(!dup) out[n++] = d[i];
  }
  if(gKbEmojiSug && n < maxn)
    for(int i = 0; i < KB_EMOSUG_N && n < maxn; i++)
      if(kbStartsWith(KB_EMOSUG_W[i], pref)){ out[n++] = KB_EMOSUG_E[i]; break; }
  return n;
}
// Palabra en construccion: del ultimo espacio/salto hasta el cursor. Copia a
// out (primitivos en la firma) y devuelve su longitud en bytes.
static int kbCurrentWord(const char* buf, int cur, char* out, int outsz){
  out[0] = 0;
  if(!buf || cur <= 0 || outsz <= 1) return 0;
  int a = cur;
  while(a > 0){
    unsigned char c = (unsigned char)buf[a - 1];
    if(c == ' ' || c == '\n' || c == '\t') break;
    a--;
  }
  int n = cur - a; if(n > outsz - 1) n = outsz - 1;
  memcpy(out, buf + a, n); out[n] = 0;
  return n;
}

// ---- Memoria de trabajo del editor de Notas -------------------------------
// El texto que se esta editando es un buffer de TRABAJO, no el fichero: vive
// en PSRAM cuando la placa la tiene (Ultra / Ultra S3), porque son 4 KB que no
// hacen ninguna falta en la RAM interna, que es el recurso escaso. El fichero
// real en /Notas se escribe al salir del editor y 2 s despues de la ultima
// tecla (ver noteSave). En una placa sin PSRAM (Pro) se usa el arreglo
// estatico de siempre: menos capacidad, pero exactamente el mismo
// comportamiento.
#define NOTE_BUF_PSRAM 4096
static char   noteBufStatic[512] = "";
static char*  noteBuffer = noteBufStatic;
static size_t noteBufMax = sizeof(noteBufStatic);
static char   noteTitleBar[FLEXFS_NAME_MAX] = "Notas";   // titulo del editor = nombre real del fichero
static void noteBufInit(){
  if(noteBuffer != noteBufStatic) return;                // ya se amplio
  if(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) return;
  char* p = (char*)heap_caps_malloc(NOTE_BUF_PSRAM, MALLOC_CAP_SPIRAM);
  if(!p) return;
  memcpy(p, noteBufStatic, sizeof(noteBufStatic));
  noteBuffer = p; noteBufMax = NOTE_BUF_PSRAM;
}
// estado del pop-up de acentos
static int kbLpKey = -1, kbPopX = 0, kbPopY = 0, kbPopN = 0, kbPopW = 40, kbPopG = 4;
static bool kbPopup = false;

// ---- Insercion/borrado UTF-8 seguros ----
// ---- Modelo de texto editable (cursor + seleccion) + PORTAPAPELES GLOBAL ----
static int  noteCur = 0;                          // cursor (indice en bytes)
static int  noteSelA = -1, noteSelB = -1;         // seleccion A..B en bytes (-1 = ninguna)
static bool noteMenu = false;                     // menu contextual visible
static int  noteHandleDrag = 0;                   // 0 no, 1 manija izq, 2 der
// ---- Continuidad de Notas ----
// noteEditorScroll -> desplazamiento vertical del area de texto, en pixeles.
//                     Es estado de sesion de pleno derecho: al volver desde
//                     Recientes la nota aparece en la MISMA posicion, no arriba.
// notePath         -> ruta del fichero activo dentro del gestor real de notas.
//                     Viaja en la sesion sin duplicar el contenido del .txt.
static int   noteEditorScroll = 0;
static int   noteEditorDragY0 = 0, noteEditorDragS0 = 0;
static bool  noteEditorScrollDrag = false;
// El teclado es un recurso global compartido por Notas, Wi-Fi, Clima y las
// pantallas de seguridad. Esta copia privada evita que abrir una de esas
// pantallas mientras Notas esta suspendida cambie su idioma/capa/panel al
// volver. Los punteros de layout no se escriben crudos: se codifican 0..3.
static uint8_t noteKbLayout = 0;
static uint8_t noteKbFlags = 0;
static bool    noteKbStateValid = false;
static void noteCaptureKbState();
static void noteRestoreKbState();
// El siguiente repintado debe reencuadrar la vista sobre el cursor. Lo ponen las
// primitivas de edicion; NO se hace en todo repintado, o un arrastre de scroll
// del usuario saltaria de vuelta al cursor en cuanto se refrescara la pantalla.
static bool  noteEditorCursorFollow = false;
// (el portapapeles vive ahora arriba: buffer clasico + las 12 ranuras de la Fase D)

static int  utf8Prev(const char* s, int i){ if(i <= 0) return 0; i--; while(i > 0 && (s[i] & 0xC0) == 0x80) i--; return i; }
static int  utf8Next(const char* s, int i){ int L = strlen(s); if(i >= L) return L; i++; while(i < L && (s[i] & 0xC0) == 0x80) i++; return i; }
static bool isWordByte(unsigned char c){ return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static bool noteHasSel(){ return noteSelA >= 0 && noteSelB > noteSelA; }
static void noteClearSel(){ noteSelA = noteSelB = -1; noteMenu = false; }
static void noteDeleteSel(){
  if(!noteHasSel()) return;
  int a = noteSelA, b = noteSelB, L = strlen(noteBuffer);
  memmove(noteBuffer + a, noteBuffer + b, L - b + 1);
  noteCur = a; noteClearSel();
  noteEditorCursorFollow = true; sessMarkDirty(IC_NOTAS);
}
static void noteInsert(const char* s){            // inserta en el cursor (reemplaza seleccion si hay)
  if(noteHasSel()) noteDeleteSel();
  int L = strlen(noteBuffer), sl = strlen(s);
  if(L + sl >= (int)noteBufMax - 1) return;
  if(noteCur < 0) noteCur = 0; if(noteCur > L) noteCur = L;
  memmove(noteBuffer + noteCur + sl, noteBuffer + noteCur, L - noteCur + 1);
  memcpy(noteBuffer + noteCur, s, sl);
  noteCur += sl; noteMenu = false;
  noteEditorCursorFollow = true; sessMarkDirty(IC_NOTAS);
}
static void noteBackspace(){                       // borra antes del cursor (multibyte) o la seleccion
  if(noteHasSel()){ noteDeleteSel(); return; }
  if(noteCur <= 0) return;
  int p = utf8Prev(noteBuffer, noteCur), L = strlen(noteBuffer);
  memmove(noteBuffer + p, noteBuffer + noteCur, L - noteCur + 1);
  noteCur = p; noteMenu = false;
  noteEditorCursorFollow = true; sessMarkDirty(IC_NOTAS);
}
// FASE G: chip flotante "Copiado". No bloquea nada -- es una marca de tiempo
// que el tick de Notas mira y borra sola a los ~1.2 s.
static uint32_t kbToastMs = 0;
static char     kbToastTxt[24] = "";
static void kbToast(const char* t){
  snprintf(kbToastTxt, sizeof(kbToastTxt), "%s", t);
  kbToastMs = millis();
  if(!KB_ANIM_POLISH_ON) kbToastMs = 0;      // sin animaciones: ni toast ni corte, simplemente no sale
}
// FASE D: copiar ahora ALIMENTA las 12 ranuras ademas del buffer clasico, asi
// que clipboard[] sigue siendo valido aunque el interruptor este a 0.
static void clipCopy(){
  if(!noteHasSel()) return;
  int a = noteSelA, b = noteSelB, n = b - a;
  if(n >= (int)sizeof(clipboard)) n = sizeof(clipboard) - 1;
  memcpy(clipboard, noteBuffer + a, n); clipboard[n] = 0;
  clipPush(clipboard);
  kbToast("Copiado");
}
static void clipCut(){ clipCopy(); noteDeleteSel(); }
static void clipPaste(){ if(clipboard[0]) noteInsert(clipboard); }
static void selectAllTxt(){ noteSelA = 0; noteSelB = strlen(noteBuffer); noteCur = noteSelB; noteMenu = noteHasSel(); }
static void selectWordAt(int bi){
  int L = strlen(noteBuffer); if(L == 0) return;
  if(bi >= L) bi = utf8Prev(noteBuffer, L);
  int a = bi; while(a > 0){ int p = utf8Prev(noteBuffer, a); if(!isWordByte((unsigned char)noteBuffer[p])) break; a = p; }
  int b = bi; while(b < L){ if(!isWordByte((unsigned char)noteBuffer[b])) break; b = utf8Next(noteBuffer, b); }
  if(b > a){ noteSelA = a; noteSelB = b; noteCur = b; noteMenu = true; }
}
static void kbPressChar(const char* s){
  char out[6];
  noteInsert(kbResolveKey(s, out, true));
}
// variantes acentuadas (solo las que tiene la fuente). Devuelve el numero.
static int kbGetVariants(char b, const char* var[4]){
  switch(b){
    case 'a': var[0]="\xC3\xA1"; var[1]="\xC3\xA0"; var[2]="\xC3\xA2"; var[3]="\xC3\xA3"; return 4;
    case 'e': var[0]="\xC3\xA9"; var[1]="\xC3\xA8"; var[2]="\xC3\xAA"; return 3;
    case 'i': var[0]="\xC3\xAD"; var[1]="\xC3\xAC"; var[2]="\xC3\xAE"; return 3;
    case 'o': var[0]="\xC3\xB3"; var[1]="\xC3\xB2"; var[2]="\xC3\xB4"; var[3]="\xC3\xB5"; return 4;
    case 'u': var[0]="\xC3\xBA"; var[1]="\xC3\xB9"; var[2]="\xC3\xBB"; var[3]="\xC3\xBC"; return 4;
  }
  return 0;
}
static bool kbIsVowelCell(int cell){
  if(cell < 0 || !(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN)) return false;
  const char* k = mapaActivo[cell / KB_COLS][cell % KB_COLS];
  return k[1] == 0 && (k[0]=='a'||k[0]=='e'||k[0]=='i'||k[0]=='o'||k[0]=='u');
}

// Limite de abajo del AREA DE TEXTO. Antes era KB_Y-8 escrito a mano en cinco
// sitios; ahora sale del panel del teclado, que puede crecer hacia arriba si
// estan la barra de la Fase C o los chips de la Fase F.
static int noteTxtBot(){ return kbPanelTop() - 8; }
static int curSX, curSY, hASX, hASY, hBSX, hBSY, noteMenuX, noteMenuY;

// #############################################################
// ##  NOTAS · MAQUETACION, SCROLL Y CURSOR
// ##  ------------------------------------------------------
// ##  Un solo recorrido del texto con EXACTAMENTE las mismas reglas
// ##  de salto de linea que usa noteDrawText (mismo ancho util, mismo
// ##  interlineado, mismas metricas de fuente). Devuelve el alto total
// ##  del contenido y, si se pide, la fila (sin scroll aplicado) del
// ##  byte que se le indique. Tenerlo en UNA funcion es lo que impide
// ##  que el dibujo, el hit-test y el auto-scroll se desincronicen.
// #############################################################
#define NOTE_LH   26
#define NOTE_TOP  60
static int noteEditorWalk(int wantBi, int* outY){
  int x = 18, y = NOTE_TOP, maxX = SCR_W - 18;
  float sc = fontSc(2);
  const char* s = noteBuffer; int bi = 0;
  if(outY) *outY = y;
  while(*s){
    if(bi == wantBi && outY) *outY = y;
    if(*s == '\n'){ s++; bi++; x = 18; y += NOTE_LH; continue; }
    const char* save = s; uint32_t cp = nextCP(&s); int nb = s - save;
    int w = (int)(FG[fontIdx(cp)].adv * sc + 0.5f);
    if(x + w > maxX){ x = 18; y += NOTE_LH; }
    if(bi == wantBi && outY) *outY = y;
    x += w; bi += nb;
  }
  if(bi == wantBi && outY) *outY = y;
  return y + NOTE_LH - NOTE_TOP;             // alto total del contenido
}
static int noteEditorMaxScroll(){
  int visible = (noteTxtBot() - 22) - NOTE_TOP;
  if(visible < NOTE_LH) visible = NOTE_LH;
  int m = noteEditorWalk(-1, NULL) - visible;
  return m > 0 ? m : 0;
}
static void noteEditorClampScroll(){
  int m = noteEditorMaxScroll();
  if(noteEditorScroll > m) noteEditorScroll = m;
  if(noteEditorScroll < 0) noteEditorScroll = 0;
}
// Deja el cursor SIEMPRE dentro del area visible. Se llama despues de cada
// edicion y de cada movimiento del cursor: es lo que hace que escribir al final
// de una nota larga siga el texto en vez de escribir a ciegas fuera de pantalla.
static void noteEditorEnsureCursor(){
  int cy = NOTE_TOP;
  noteEditorWalk(noteCur, &cy);
  int top = 56, bot = noteTxtBot() - 8;
  if(cy - noteEditorScroll < top)              noteEditorScroll = cy - top;
  if(cy - noteEditorScroll + NOTE_LH > bot)    noteEditorScroll = cy + NOTE_LH - bot;
  noteEditorClampScroll();
}
// Punto UNICO por el que Notas anuncia que su contenido cambio: rearma el
// temporizador de guardado diferido (no escribe nada aqui: escribir por tecla
// es justo lo que hay que evitar) y reencuadra el cursor.
static void noteEditorTouched(){
  noteEditorEnsureCursor();
  sessMarkDirty(IC_NOTAS);
}

static void noteDrawText(){
  if(noteEditorCursorFollow){ noteEditorCursorFollow = false; noteEditorEnsureCursor(); }
  else noteEditorClampScroll();                        // el texto pudo encoger: no dejar hueco en blanco
  setBuf(fb);
  fillRect(8, 48, SCR_W - 16, noteTxtBot() - 48, TH_SURF);       // hoja de la nota
  // Las lineas desplazadas por encima de la hoja no pueden invadir la
  // cabecera. Se conserva y restaura el recorte porque Notas comparte el motor
  // grafico con overlays y ventanas DeX.
  int sy0 = gClipY0, sy1 = gClipY1;
  gClipY0 = 48; gClipY1 = noteTxtBot() - 1;
  int x = 18, y = NOTE_TOP - noteEditorScroll, maxX = SCR_W - 18, lh = NOTE_LH, size = 2;
  float sc = fontSc(size);
  const char* s = noteBuffer; int bi = 0;
  bool hasSel = noteHasSel();
  int yBreak = noteTxtBot() - 22;
  curSX = 18; curSY = NOTE_TOP - noteEditorScroll; hASX = hBSX = 18; hASY = hBSY = curSY;
  // FASE F - revision ortografica basica: se sigue la palabra en curso (donde
  // empezo y en que linea) y al cerrarla se subraya con puntitos si NO esta en
  // el diccionario del idioma activo. Es eso y nada mas: comparacion contra una
  // lista local, sin gramatica ni sugerencias de correccion.
  bool spellOn = (KB_AUTOCOMPLETE_ON && gKbSpell);
  int wsX = x, wsY = y, wsBi = 0; bool inWord = false;
  while(*s){
    if(*s == '\n'){
      if(spellOn && inWord){
        char wtmp[40]; int wn = bi - wsBi; if(wn > 39) wn = 39;
        memcpy(wtmp, noteBuffer + wsBi, wn); wtmp[wn] = 0;
        if(wsY == y && wn >= 3 && !kbDictHas(wtmp)) for(int px = wsX; px < x; px += 3) fillRect(px, y + lh - 7, 2, 2, TH_ERR);
        inWord = false;
      }
      if(bi == noteCur){ curSX = x; curSY = y; } if(bi == noteSelA){ hASX = x; hASY = y; } if(bi == noteSelB){ hBSX = x; hBSY = y; }
      s++; bi++; x = 18; y += lh; if(y > yBreak) break; continue;
    }
    const char* save = s; uint32_t cp = nextCP(&s); int nb = s - save;
    int w = (int)(FG[fontIdx(cp)].adv * sc + 0.5f);
    if(x + w > maxX){ x = 18; y += lh; if(y > yBreak) break; }
    if(spellOn){
      bool wb = isWordByte((unsigned char)*save);
      if(wb && !inWord){ inWord = true; wsX = x; wsY = y; wsBi = bi; }
      else if(!wb && inWord){
        char wtmp[40]; int wn = bi - wsBi; if(wn > 39) wn = 39;
        memcpy(wtmp, noteBuffer + wsBi, wn); wtmp[wn] = 0;
        if(wsY == y && wn >= 3 && !kbDictHas(wtmp)) for(int px = wsX; px < x; px += 3) fillRect(px, y + lh - 7, 2, 2, TH_ERR);
        inWord = false;
      }
    }
    if(bi == noteCur){ curSX = x; curSY = y; } if(bi == noteSelA){ hASX = x; hASY = y; } if(bi == noteSelB){ hBSX = x; hBSY = y; }
    // El recorrido continua para mantener cursor/seleccion correctos, pero los
    // glifos fuera del viewport no se rasterizan: una nota larga no anade lag
    // al escribir ni al arrastrar el scroll.
    if(y + lh > 48 && y <= yBreak){
      if(hasSel && bi >= noteSelA && bi < noteSelB) fillRect(x - 1, y - 2, w + 1, lh - 4, TH_SEL);
      char one[6]; int n = nb; if(n > 5) n = 5; for(int i = 0; i < n; i++) one[i] = save[i]; one[n] = 0;
      drawText(x, y, one, size, TH_TXT);
    }
    x += w; bi += nb;
  }
  // Ultima palabra del texto. Si el cursor esta justo ahi es que se esta
  // escribiendo TODAVIA: marcarla en rojo mientras se teclea seria ruido puro,
  // asi que esa se deja en paz hasta que se cierre con un espacio.
  if(spellOn && inWord && noteCur != bi){
    char wtmp[40]; int wn = bi - wsBi; if(wn > 39) wn = 39;
    memcpy(wtmp, noteBuffer + wsBi, wn); wtmp[wn] = 0;
    if(wsY == y && wn >= 3 && !kbDictHas(wtmp)) for(int px = wsX; px < x; px += 3) fillRect(px, y + lh - 7, 2, 2, TH_ERR);
  }
  if(bi == noteCur){ curSX = x; curSY = y; } if(bi == noteSelA){ hASX = x; hASY = y; } if(bi == noteSelB){ hBSX = x; hBSY = y; }
  if(hasSel){                                       // manijas (gotas arrastrables)
    vLine(hASX, hASY - 2, 24, TH_PRIM); fillCircle(hASX, hASY + 24, 7, TH_PRIM);
    vLine(hBSX, hBSY - 2, 24, TH_PRIM); fillCircle(hBSX, hBSY + 24, 7, TH_PRIM);
  } else {
    fillRect(curSX + 1, curSY - 2, 2, 22, TH_PRIM);   // cursor
  }
  if(noteMenu){                                     // menu contextual flotante
    const char* it[4] = { "Cortar", "Copiar", "Pegar", "Todo" };
    int bw = 92, gap = 4, tot = 4 * bw + 3 * gap, mx = (SCR_W - tot) / 2, my = hASY - 44; if(my < 50) my = 50;
    noteMenuX = mx; noteMenuY = my;
    uiSurface(mx - 6, my - 6, tot + 12, 40, 8, UIS_ELEVATED);       // menu flotante de seleccion
    for(int i = 0; i < 4; i++){ int bx = mx + i * (bw + gap); uiSurface(bx, my, bw, 28, 6, UIS_ELEVATED); drawTextC(bx + bw / 2, my + 7, it[i], 2, TH_TXT); }
  }
  // FASE G - chip flotante "Copiado": vive DENTRO del area de texto, asi que se
  // borra solo con el siguiente repintado de esta misma banda. Se desvanece en
  // el ultimo tercio en vez de desaparecer de golpe.
  if(KB_ANIM_POLISH_ON && kbToastMs){
    uint32_t dt = millis() - kbToastMs;
    if(dt < 1200){
      uint8_t a = (dt < 800) ? 230 : (uint8_t)(230 - (dt - 800) * 230 / 400);
      int tw = textW(kbToastTxt, 2) + 34, tx = (SCR_W - tw) / 2, ty = noteTxtBot() - 46;
      fillRoundRectA(tx, ty, tw, 32, 16, thCard2(), a);
      drawTextCA(SCR_W / 2, ty + 9, kbToastTxt, 2, TH_TXT, a);
    }
  }
  gClipY0 = sy0; gClipY1 = sy1;                    // se restaura el recorte anterior
  // Se vuelca hasta el borde mismo del panel del teclado: entre el final del
  // area de texto y kbPanelTop() hay unos pixeles de fondo que si no quedarian
  // en tierra de nadie (ni esta banda ni la del teclado los publicaria).
  flxFlush(44, kbPanelTop() - 1);
}
// mapea un toque (px,py) al indice de byte mas cercano en el texto
static int noteLayoutHit(int px, int py){
  int x = 18, y = NOTE_TOP - noteEditorScroll, maxX = SCR_W - 18, lh = NOTE_LH, size = 2; float sc = fontSc(size);
  const char* s = noteBuffer; int bi = 0, best = 0; long bestd = 1L << 30;
  while(*s){
    if(*s == '\n'){ long d = (long)abs(px - x) + (long)abs(py - (y + 8)) * 2; if(d < bestd){ bestd = d; best = bi; } s++; bi++; x = 18; y += lh; continue; }
    const char* save = s; uint32_t cp = nextCP(&s); int nb = s - save;
    int w = (int)(FG[fontIdx(cp)].adv * sc + 0.5f);
    if(x + w > maxX){ x = 18; y += lh; }
    long d0 = (long)abs(px - x) + (long)abs(py - (y + 8)) * 2; if(d0 < bestd){ bestd = d0; best = bi; }
    long d1 = (long)abs(px - (x + w)) + (long)abs(py - (y + 8)) * 2; if(d1 < bestd){ bestd = d1; best = bi + nb; }
    x += w; bi += nb;
  }
  long d = (long)abs(px - x) + (long)abs(py - (y + 8)) * 2; if(d < bestd){ bestd = d; best = bi; }
  return best;
}
static int noteMenuHit(int px, int py){
  if(!noteMenu || py < noteMenuY || py > noteMenuY + 28) return -1;
  int bw = 92, gap = 4;
  for(int i = 0; i < 4; i++){ int bx = noteMenuX + i * (bw + gap); if(px >= bx && px <= bx + bw) return i; }
  return -1;
}
// Tecla de funcion. Misma firma de siempre (la usan Bloqueo y Wi-Fi), pero por
// dentro ya pasa por kbPaintKey: hereda estilo, contraste alto y tamano de
// fuente de la Fase E sin que esas tres superficies tengan que enterarse.
static void kbFKey(int x, int fy, int w, const char* label, bool on){
  kbPaintKey(x, fy, w, KB_KH, label, kbFontSize() > 2 ? 2 : kbFontSize(),
             on ? kbColFnOn() : kbColFn(), on ? kbColFnOnTxt() : kbColKeyTxt(), false);
}
// Etiqueta de la tecla de capa (?123 / emoji / ABC), en un solo sitio.
static const char* kbLayerLabel(){
  return (mapaActivo == LAYOUT_NUM) ? "emoji" : (mapaActivo == LAYOUT_EMOJI) ? "ABC" : "?123";
}
static void noteDrawFuncRow(int yoff){
  int fy = kbFuncY() + yoff;
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "ent" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
}

// #############################################################
// ##  FASE C · BARRA SUPERIOR DE ACCESOS RAPIDOS
// ##  emoji · idioma · portapapeles · ajustes · mas opciones
// ##  ------------------------------------------------------
// ##  Iconos VECTORIALES con las primitivas del propio motor
// ##  (nada de assets nuevos). Solo se dibuja en Notas: en la
// ##  pantalla de contrasena un boton de portapapeles o de
// ##  ajustes seria una via de escape, no una comodidad.
// #############################################################
#define KB_TB_EMOJI 0
#define KB_TB_LANG  1
#define KB_TB_CLIP  2
#define KB_TB_SET   3
#define KB_TB_MORE  4
#define KB_TB_N     5
static int kbToolX(int i){                 // centro X del boton i
  int step = SCR_W / KB_TB_N;
  return step / 2 + i * step;
}
static int kbToolHit(int px, int py){
  if(kbToolbarH() == 0) return -1;
  int y0 = kbToolbarY();
  if(py < y0 || py > y0 + 52) return -1;
  for(int i = 0; i < KB_TB_N; i++) if(abs(px - kbToolX(i)) <= 26) return i;
  return -1;
}
static void kbToolIcon(int idx, int cx, int cy, uint16_t col){
  switch(idx){
    case KB_TB_EMOJI:                                   // carita
      drawCircle(cx, cy, 11, col); drawCircle(cx, cy, 10, col);
      fillCircle(cx - 4, cy - 3, 2, col); fillCircle(cx + 4, cy - 3, 2, col);
      arcStroke(cx, cy + 1, 6, 20, 160, 2, col); break;
    case KB_TB_LANG:                                    // globo (cambiar idioma)
      drawCircle(cx, cy, 11, col);
      hLine(cx - 11, cy, 22, col);
      arcStroke(cx, cy, 11, 250, 290, 2, col); arcStroke(cx, cy, 11, 70, 110, 2, col);
      vLine(cx, cy - 11, 22, col); break;
    case KB_TB_CLIP:                                    // portapapeles
      drawRoundRect(cx - 8, cy - 10, 16, 21, 3, col);
      fillRoundRect(cx - 5, cy - 13, 10, 5, 2, col);
      hLine(cx - 4, cy - 1, 8, col); hLine(cx - 4, cy + 4, 8, col); break;
    case KB_TB_SET:                                     // engranaje
      drawCircle(cx, cy, 5, col);
      for(int k = 0; k < 6; k++){ float a = k * 1.0471976f;
        strokeSegAA(cx + cosf(a) * 7, cy + sinf(a) * 7, cx + cosf(a) * 11, cy + sinf(a) * 11, 2.2f, col); }
      break;
    default:                                            // mas opciones
      fillCircle(cx - 8, cy, 2, col); fillCircle(cx, cy, 2, col); fillCircle(cx + 8, cy, 2, col); break;
  }
}
static void kbDrawToolbar(int yoff){
  if(kbToolbarH() == 0) return;
  int y0 = kbToolbarY() + yoff, cy = y0 + 26;
  for(int i = 0; i < KB_TB_N; i++){
    int cx = kbToolX(i);
    fillCircle(cx, cy, 21, kbColKey());
    kbToolIcon(i, cx, cy, kbColKeyTxt());
  }
}

// #############################################################
// ##  FASE F · FRANJA DE CHIPS (sugerencias / simbolos)
// ##  En capas de letras: hasta 3 palabras del diccionario local.
// ##  En la capa numerica: los 4 simbolos personalizados (Fase E),
// ##  o sea "un tap extra" desde ?123, como se pidio.
// #############################################################
static const char* kbChipTxt[4];
static int      kbChipN = 0;
static uint32_t kbChipMs = 0;          // cuando cambio la lista (para el fundido de la Fase G)
static void kbChipsBuild(){
  const char* prev[4]; int prevN = kbChipN;
  for(int i = 0; i < prevN && i < 4; i++) prev[i] = kbChipTxt[i];
  kbChipN = 0;
  if(!kbChipsWant()) return;
  if(mapaActivo == LAYOUT_NUM){
    for(int i = 0; i < KB_SYMS; i++) kbChipTxt[kbChipN++] = kbSymAt(i);
  } else {
    char w[40];
    if(kbCurrentWord(noteBuffer, noteCur, w, sizeof(w)) > 0) kbChipN = kbSuggest(w, kbChipTxt, 3);
  }
  bool changed = (kbChipN != prevN);
  for(int i = 0; i < kbChipN && !changed; i++) if(kbChipTxt[i] != prev[i]) changed = true;
  if(changed) kbChipMs = millis();
}
static int kbChipHit(int px, int py){
  if(kbChipsH() == 0 || kbChipN <= 0) return -1;
  int y0 = kbChipsY();
  if(py < y0 || py > y0 + 32) return -1;
  int cw = (SCR_W - 12) / kbChipN;
  for(int i = 0; i < kbChipN; i++){ int x = 6 + i * cw; if(px >= x && px <= x + cw) return i; }
  return -1;
}
static void kbDrawChips(int yoff){
  if(kbChipsH() == 0) return;
  int y0 = kbChipsY() + yoff;
  // FASE G: los chips no aparecen de golpe, entran con un fundido corto.
  uint8_t a = 255;
  if(KB_ANIM_POLISH_ON && kbChipMs){
    uint32_t dt = millis() - kbChipMs;
    if(dt < 140) a = (uint8_t)(60 + dt * 195 / 140);
  }
  if(kbChipN <= 0) return;
  int cw = (SCR_W - 12) / kbChipN;
  for(int i = 0; i < kbChipN; i++){
    int x = 6 + i * cw;
    if(i > 0) fillRectA(x, y0 + 8, 1, 16, gKbHiCon ? kbColEdge() : TH_DIV, 160);   // divisor entre chips
    drawTextCA(x + cw / 2, y0 + 8, kbChipTxt[i], 2, kbColKeyTxt(), a);
  }
}

// Dibuja el teclado completo (panel + extras + teclas) desplazado yoff pixeles
// hacia abajo. yoff != 0 solo durante la animacion de apertura de la Fase G.
static void noteRenderKeyboard(int yoff){
  setBuf(fb);
  int top = kbPanelTop(), py = top + yoff;
  // BORRADO OBLIGATORIO DE LA BANDA, SIEMPRE. Esto arregla el "teclado fantasma
  // borroso detras del teclado" y los chips que se emborronaban mas con cada
  // tecla:
  //   drawLiquidGlassPanel COPIA lo que hay debajo del panel, lo desenfoca y lo
  //   mezcla. Como aqui se dibuja directamente sobre fb, "lo que hay debajo" era
  //   el teclado del CUADRO ANTERIOR. Resultado: cada repintado desenfocaba el
  //   teclado anterior y lo dejaba pegado al fondo, y al repintar otra vez
  //   desenfocaba el desenfoque... por eso empeoraba tecla a tecla.
  //   Con el fondo plano debajo, el vidrio muestrea siempre lo mismo que cuando
  //   se repinta la pantalla entera: identico aspecto, cero acumulacion.
  fillRect(0, top - 2, SCR_W, SCR_H - (top - 2), TH_PAGE);
  kbPaintPanel(py, kbColPanel());
  kbDrawToolbar(yoff);
  kbDrawChips(yoff);
  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = KB_Y + r * (KB_KH + KB_GAP) + yoff;
    char u[6];
    const char* k = kbResolveKey(mapaActivo[r][c], u, false);
    int cell = r * KB_COLS + c;
    bool hot = kbCellHeld(cell) || kbFxLevel(cell) > 0;
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, kbColKey(), kbColKeyTxt(), hot);
  }
  noteDrawFuncRow(yoff);
  flxFlush(top - 2, SCR_H - 1);
}
static void noteRenderAll(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  strokeSegAA(30, 26, 18, 18, 2.4f, TH_NAV);
  strokeSegAA(18, 18, 30, 10, 2.4f, TH_NAV);
  drawTextC(SCR_W / 2, 14, noteTitleBar, 3, TH_TXT);   // nombre REAL del fichero abierto
  kbChipsBuild();
  noteDrawText();
  noteRenderKeyboard(0);
  flxFlushAll();
}
static void kbRenderPopup(int cell){
  int r = cell / KB_COLS, c = cell % KB_COLS;
  int kx = KB_X + c * (KB_KW + KB_GAP), ky = KB_Y + r * (KB_KH + KB_GAP);
  const char* var[4]; int n = kbGetVariants(mapaActivo[r][c][0], var);
  if(n == 0) return;
  int pw = 40, ph = 46, gap = 4, totw = n * pw + (n - 1) * gap;
  int px0 = kx + KB_KW / 2 - totw / 2; if(px0 < 4) px0 = 4; if(px0 + totw > SCR_W - 4) px0 = SCR_W - 4 - totw;
  int py0 = ky - ph - 10;
  kbPopX = px0; kbPopY = py0; kbPopN = n; kbPopW = pw; kbPopG = gap;
  setBuf(fb);
  uiSurface(px0 - 6, py0 - 6, totw + 12, ph + 12, 10, UIS_ELEVATED);     // popup de acentos
  for(int i = 0; i < n; i++){
    int x = px0 + i * (pw + gap);
    fillRoundRect(x, py0, pw, ph, 8, kbColKey());
    drawTextC(x + pw / 2, py0 + ph / 2 - 12, var[i], 3, kbColKeyTxt());
  }
  flxFlush(py0 - 8, ky + KB_KH);
}
static int kbPopupHit(int px, int py){
  for(int i = 0; i < kbPopN; i++){ int x = kbPopX + i * (kbPopW + kbPopG); if(px >= x && px <= x + kbPopW && py >= kbPopY && py <= kbPopY + 46) return i; }
  return -1;
}
// Accion de una tecla de FUNCION (0..5). Un solo sitio para las dos rutas de
// entrada: el disparo rapido de la Fase B y el disparo clasico al soltar.
static void noteFuncKey(int i){
  if(i == 0) kbShift = !kbShift;
  else if(i == 1){                                       // cicla ABC -> NUM -> EMOJI
    if(mapaActivo == LAYOUT_NUM) mapaActivo = LAYOUT_EMOJI;
    else if(mapaActivo == LAYOUT_EMOJI) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN;
    else mapaActivo = LAYOUT_NUM;
  }
  else if(i == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
  else if(i == 3) noteInsert(" ");
  else if(i == 4) noteBackspace();
  else if(i == 5) noteInsert("\n");
}
// FASE F: aceptar un chip. En la capa numerica el chip es un simbolo y se
// inserta tal cual; en las de letras SUSTITUYE la palabra en construccion y
// deja un espacio detras.
static void kbApplyChip(int i){
  if(i < 0 || i >= kbChipN) return;
  if(mapaActivo == LAYOUT_NUM){ noteInsert(kbChipTxt[i]); return; }
  const char* w = kbChipTxt[i];
  char tmp[40];
  while(kbCurrentWord(noteBuffer, noteCur, tmp, sizeof(tmp)) > 0) noteBackspace();
  noteInsert(w); noteInsert(" ");
}

// #############################################################
// ##  FASE D · PANEL DE PORTAPAPELES (rejilla de 2 columnas)
// ##  tap = pegar · pin = fijar/soltar · x = borrar esa ficha
// ##  cabecera: volver · filtro de fijados · vaciar (con aviso)
// #############################################################
static bool clipPanelOn   = false;
static bool clipFilterPin = false;
static bool clipAskClear  = false;      // se pidio vaciar: la cabecera pide confirmacion
static int  clipVis[CLIP_SLOTS], clipVisN = 0;
static void clipBuildVis(){
  clipVisN = 0;
  for(int i = 0; i < CLIP_SLOTS; i++){
    if(!gClip[i].used) continue;
    if(clipFilterPin && !gClip[i].pinned) continue;
    clipVis[clipVisN++] = i;
  }
}
static void clipCardRect(int k, int &x, int &y, int &w, int &h){
  int col = k % 2, row = k / 2;
  w = (SCR_W - 3 * 12) / 2; h = 116;
  x = 12 + col * (w + 12);
  y = 108 + row * (h + 12);
}
static void clipRenderPanel(){
  clipBuildVis();
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  // Cabecera, al espiritu de la captura: icono de teclado a la izquierda, pin y
  // papelera a la derecha.
  fillRect(0, 0, SCR_W, 64, thCard());
  drawRoundRect(14, 22, 26, 20, 4, TH_NAV);
  for(int i = 0; i < 3; i++) fillRect(19 + i * 7, 28, 4, 3, TH_NAV);
  hLine(20, 36, 14, TH_NAV);
  drawText(54, 20, "Portapapeles", 3, TH_TXT);
  { int cx = SCR_W - 96, cy = 32;                                  // pin (filtra fijados)
    fillCircle(cx, cy, 20, clipFilterPin ? TH_PRIM : TH_SURF2);
    fillRect(cx - 2, cy - 2, 4, 12, clipFilterPin ? TH_ONACC : TH_TXT);
    fillRoundRect(cx - 7, cy - 11, 14, 9, 3, clipFilterPin ? TH_ONACC : TH_TXT); }
  { int cx = SCR_W - 42, cy = 32;                                  // papelera (vaciar no fijados)
    fillCircle(cx, cy, 20, clipAskClear ? TH_DANGER : TH_SURF2);     // vaciar: accion destructiva
    fillRoundRect(cx - 8, cy - 6, 16, 15, 3, clipAskClear ? TH_ONACC : TH_TXT);
    fillRect(cx - 10, cy - 9, 20, 3, clipAskClear ? TH_ONACC : TH_TXT);
    fillRect(cx - 3, cy - 12, 6, 3, clipAskClear ? TH_ONACC : TH_TXT); }
  if(clipAskClear){
    drawTextC(SCR_W / 2, 74, "Toca otra vez la papelera para vaciar (los fijados se quedan)", 1, TH_WARN);
  } else {
    char sub[64]; snprintf(sub, sizeof(sub), "%d visibles - %d de %d ranuras en uso%s",
                           clipVisN, clipCount(), CLIP_SLOTS, clipFilterPin ? " - filtro: fijadas" : "");
    drawTextC(SCR_W / 2, 76, sub, 1, TH_TXT2);
  }
  if(clipVisN == 0){
    drawTextC(SCR_W / 2, 300, "Nada copiado todavia", 2, TH_TXT2);
    drawTextC(SCR_W / 2, 330, "Selecciona texto y toca Copiar", 1, TH_MUTE);
  }
  for(int k = 0; k < clipVisN && k < 8; k++){
    int x, y, w, h; clipCardRect(k, x, y, w, h);
    int i = clipVis[k];
    if(uiGlass) drawLiquidGlassPanel(x, y, w, h, 12, TH_GLASS);
    else fillRoundRect(x, y, w, h, 12, TH_SURF);
    // Texto recortado a 3 lineas con elipsis (la ficha no crece: la rejilla es fija)
    const char* s = gClip[i].text;
    int ty = y + 10, lx = x + 10, maxr = x + w - 34, lines = 0;
    char ln[40]; int lp = 0;
    for(int p = 0; s[p] && lines < 3; p++){
      ln[lp++] = s[p]; ln[lp] = 0;
      bool last = (s[p + 1] == 0);
      if(textW(ln, 1) > maxr - lx - 6 || lp >= 38 || s[p] == '\n' || last){
        if(!last && lines == 2){ ln[lp > 2 ? lp - 2 : 0] = 0; strncat(ln, "...", sizeof(ln) - strlen(ln) - 1); }
        drawTextClip(lx, ty, ln, 1, TH_TXT, maxr);
        ty += 16; lines++; lp = 0; ln[0] = 0;
      }
    }
    { int px2 = x + w - 20, py2 = y + 16;                          // pin de la ficha
      fillRect(px2 - 2, py2 - 1, 4, 9, gClip[i].pinned ? TH_PRIM : TH_MUTE);
      fillRoundRect(px2 - 6, py2 - 9, 12, 8, 2, gClip[i].pinned ? TH_PRIM : TH_MUTE); }
    { int px2 = x + w - 20, py2 = y + h - 18;                      // borrar esta ficha
      strokeSegAA(px2 - 5, py2 - 5, px2 + 5, py2 + 5, 2.0f, TH_DANGER);
      strokeSegAA(px2 + 5, py2 - 5, px2 - 5, py2 + 5, 2.0f, TH_DANGER); }
  }
  drawTextC(SCR_W / 2, SCR_H - 44, "Toca una ficha para pegarla en el cursor", 1, TH_MUTE);
  flxFlushAll();
}
// Devuelve true si el toque era para el panel (siempre que este abierto).
static bool clipPanelTick(){
  if(!clipPanelOn) return false;
  if(!T.tap) return true;
  if(T.y < 64){
    if(T.x < 50){ clipPanelOn = false; clipAskClear = false; noteRenderAll(); return true; }        // volver
    if(abs(T.x - (SCR_W - 96)) <= 22){ clipFilterPin = !clipFilterPin; clipAskClear = false; clipRenderPanel(); return true; }
    if(abs(T.x - (SCR_W - 42)) <= 22){                                                              // vaciar (2 toques)
      if(clipAskClear){ clipClearUnpinned(); clipAskClear = false; }
      else clipAskClear = true;
      clipRenderPanel(); return true;
    }
    return true;
  }
  clipAskClear = false;
  for(int k = 0; k < clipVisN && k < 8; k++){
    int x, y, w, h; clipCardRect(k, x, y, w, h);
    if(T.x < x || T.x > x + w || T.y < y || T.y > y + h) continue;
    int i = clipVis[k];
    if(T.x > x + w - 34 && T.y < y + 34){ clipTogglePin(i); clipRenderPanel(); return true; }        // pin
    if(T.x > x + w - 34 && T.y > y + h - 34){ clipDel(i); clipRenderPanel(); return true; }          // borrar
    noteInsert(gClip[i].text);                                                                      // pegar
    clipPanelOn = false; noteRenderAll(); return true;
  }
  return true;
}

// ---- FASE C: menu "mas opciones" (2 acciones reales, sin relleno) ----
static bool kbMoreOn = false;
static int  kbMoreX = 0, kbMoreY = 0;
#define KB_MORE_N 2
static const char* KB_MORE_LBL[KB_MORE_N] = { "Seleccionar todo", "Insertar fecha y hora" };
static void kbDrawMore(){
  if(!kbMoreOn) return;
  int w = 250, h = KB_MORE_N * 38 + 12;
  kbMoreX = SCR_W - w - 10; kbMoreY = kbToolbarY() - h - 6;
  if(kbMoreY < 60) kbMoreY = 60;
  setBuf(fb);
  uiSurface(kbMoreX, kbMoreY, w, h, 12, UIS_ELEVATED);          // menu "mas" del teclado
  for(int i = 0; i < KB_MORE_N; i++)
    drawText(kbMoreX + 14, kbMoreY + 10 + i * 38 + 8, KB_MORE_LBL[i], 2, TH_TXT);
  flxFlush(kbMoreY - 2, kbMoreY + h + 2);
}
static int kbMoreHit(int px, int py){
  if(!kbMoreOn) return -1;
  int w = 250;
  if(px < kbMoreX || px > kbMoreX + w) return -1;
  for(int i = 0; i < KB_MORE_N; i++){ int y = kbMoreY + 10 + i * 38; if(py >= y && py < y + 38) return i; }
  return -1;
}

// true si el caracter que hay JUSTO ANTES del cursor es (ignorando mayusculas y
// tildes) el mismo que s. Se usa para no borrar de mas al elegir un acento.
static bool kbLastCharIs(const char* s){
  if(noteCur <= 0 || noteCur > (int)strlen(noteBuffer)) return false;
  const char* a = noteBuffer + utf8Prev(noteBuffer, noteCur);
  const char* b = s;
  return kbFoldCh(&a) == kbFoldCh(&b);
}
static void handleKeyRelease(int px, int py){
  if(px < 48 && py < 48){ appClose(); return; }
  // Con la via rapida confirmada, ESTA ruta ya no escribe: la tecla se escribio
  // al tocar. Sin esta linea la misma pulsacion entraria dos veces.
  if(kbFastActive()) return;
  int fi = kbFRowHit(px, py);
  if(fi >= 0){ noteFuncKey(fi); noteRenderAll(); return; }
  int cell = kbCellAt(px, py);
  if(cell >= 0){ kbFxStart(cell); kbPressChar(mapaActivo[cell / KB_COLS][cell % KB_COLS]); noteRenderAll(); return; }
}
// FASE G: animacion de apertura del teclado en Notas (0.3 s, interpolada,
// mismo espiritu que lsuKbAnim del Bloqueo, que ya la tenia).
static uint32_t noteKbAnim = 0;
static void noteEditorEnter(){
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false; kbLpKey = -1; kbPopup = false;
  noteHandleDrag = 0; noteEditorScrollDrag = false;
  kbExtrasOn = true;                     // Notas SI muestra barra y chips
  kbBotReserve = navBarVisible() ? NAV_H : 0;   // el teclado se apoya SOBRE la barra del sistema
  kbApplySize(); kbMtSurfaceReset();
  clipPanelOn = false; kbMoreOn = false; clipAskClear = false; kbToastMs = 0;
  kbChipsBuild();
  noteCur = (int)strlen(noteBuffer); noteClearSel();
  noteEditorScroll = 0; noteEditorCursorFollow = true;
  noteKbAnim = KB_ANIM_POLISH_ON ? millis() : 0;
  noteCaptureKbState();
  noteRenderAll();
}
static void noteEditorTick(){
  int txTop = 48, txBot = noteTxtBot();

  // 0) FASE G - apertura del teclado: se interpola el desplazamiento vertical y
  // se vuelca SOLO la banda del teclado. Mientras dura, no se lee entrada.
  if(noteKbAnim){
    float p = (millis() - noteKbAnim) / 300.0f;
    if(p >= 1){ p = 1; noteKbAnim = 0; }
    noteRenderKeyboard((int)((1.0f - p) * (SCR_H - kbPanelTop())));
    return;
  }

  // Panel de portapapeles abierto: se lo come todo hasta que se cierre.
  if(clipPanelOn){ clipPanelTick(); return; }

  // FASE G - apagar el destello de la ultima tecla cuando cumple su tiempo.
  kbFxTick(kbColKey(), kbColKeyTxt());
  // FASE G - el chip "Copiado" se borra solo repintando su banda al caducar.
  if(KB_ANIM_POLISH_ON && kbToastMs && millis() - kbToastMs >= 1200){ kbToastMs = 0; noteDrawText(); }

  // 0-bis) FASE B - VIA RAPIDA: la tecla se escribe al TOCAR, por ID de
  // contacto, sin esperar a que el dedo se levante. Convive con T: todo lo de
  // abajo (manijas, long-press, menu) sigue siendo exactamente igual que antes.
  if(T.down && T.y >= kbPanelTop()) kbTypingMark();   // veto del gesto de suspension mientras se teclea
  if(KB_MULTITOUCH_ON && gKbFastType && !noteMenu && !kbPopup && !kbMoreOn){
    int n = kbMtPoll();
    for(int e = 0; e < n; e++){
      if(kbEvCell[e] >= 0){ kbFxStart(kbEvCell[e]); kbPressChar(mapaActivo[kbEvCell[e] / KB_COLS][kbEvCell[e] % KB_COLS]); }
      else if(kbEvFn[e] >= 0) noteFuncKey(kbEvFn[e]);
    }
    if(n > 0){ kbChipsBuild(); noteDrawText(); noteRenderKeyboard(0); }
  } else {
    // Con la via rapida en pausa (menu abierto, popup de acentos, menu "mas")
    // se OLVIDAN los contactos seguidos. Si no, al reanudar quedarian ids
    // "vivos" de dedos que ya se levantaron y la siguiente pulsacion de ese
    // mismo id no dispararia nada. Mientras tanto sigue funcionando la ruta
    // clasica de soltar, asi que no se pierde ninguna tecla.
    kbMtReset();
  }

  // 1) Menu contextual: intercepta toques en el area de texto
  if(noteMenu && T.released && T.tap && T.y < kbPanelTop()){
    int mi = noteMenuHit(T.x, T.y);
    if(mi >= 0){
      if(mi == 0) clipCut(); else if(mi == 1) clipCopy(); else if(mi == 2) clipPaste(); else selectAllTxt();
      if(mi != 3) noteMenu = false;
      noteRenderAll(); return;
    }
    noteMenu = false; noteClearSel(); noteCur = noteLayoutHit(T.x, T.y); noteRenderAll(); return;
  }

  // 2) Inicio de gesto: manija de seleccion, arrastre de scroll o long-press
  if(T.pressed){
    noteHandleDrag = 0; kbLpKey = -1; kbPopup = false;
    noteEditorScrollDrag = false; noteEditorDragY0 = T.y; noteEditorDragS0 = noteEditorScroll;
    if(noteHasSel() && T.y >= txTop && T.y < txBot + 30){
      if(abs(T.x - hASX) < 24 && abs(T.y - (hASY + 20)) < 28) noteHandleDrag = 1;
      else if(abs(T.x - hBSX) < 24 && abs(T.y - (hBSY + 20)) < 28) noteHandleDrag = 2;
    }
    if(!noteHandleDrag && T.y >= KB_Y){
      int cell = kbCellAt(T.x, T.y);
      kbLpKey = kbIsVowelCell(cell) ? cell : -1;
      // FASE G: con la escritura rapida APAGADA la tecla no se escribe hasta
      // soltar, asi que el destello es la unica senal de que el toque entro.
      if(!(KB_MULTITOUCH_ON && gKbFastType)) kbFxPress(cell, kbColKey(), kbColKeyTxt());
    }
    return;
  }

  // 3) Arrastre de manija -> extender seleccion
  if(noteHandleDrag && T.down){
    int bi = noteLayoutHit(T.x, T.y);
    if(noteHandleDrag == 1){ if(bi >= 0 && bi < noteSelB) noteSelA = bi; }
    else { if(bi > noteSelA) noteSelB = bi; }
    noteCur = (noteHandleDrag == 1) ? noteSelA : noteSelB;
    noteRenderAll(); return;
  }

  // 3-bis) Arrastre vertical dentro del area de texto -> scroll de la nota.
  // Solo se activa si de verdad hay contenido fuera de pantalla, para que en una
  // nota corta el gesto siga sirviendo para seleccionar palabra.
  if(!noteHandleDrag && kbLpKey < 0 && T.down && T.startY >= txTop && T.startY < txBot){
    int dy = T.y - noteEditorDragY0;
    if(!noteEditorScrollDrag && abs(dy) > 12 && noteEditorMaxScroll() > 0) noteEditorScrollDrag = true;
    if(noteEditorScrollDrag){
      noteEditorScroll = noteEditorDragS0 - dy;
      noteEditorClampScroll();
      noteDrawText();
      return;
    }
  }

  // 4) Long-press en texto -> seleccionar palabra
  if(!noteHandleDrag && kbLpKey < 0 && !noteEditorScrollDrag && T.down && !noteHasSel()
     && T.startY >= txTop && T.startY < txBot && (millis() - T.downMs) > 500
     && abs(T.x - T.startX) < 12 && abs(T.y - T.startY) < 12){
    selectWordAt(noteLayoutHit(T.startX, T.startY)); noteRenderAll(); return;
  }

  // 5) Long-press en tecla -> popup de acentos (umbral configurable, Fase E)
  if(kbLpKey >= 0 && T.down && !kbPopup && (millis() - T.downMs) > (unsigned long)gKbLpMs){ kbPopup = true; kbRenderPopup(kbLpKey); }

  // 6) Soltar
  if(T.released){
    if(noteEditorScrollDrag){ noteEditorScrollDrag = false; sessMarkDirty(IC_NOTAS); return; }  // el scroll es estado de sesion
    if(noteHandleDrag){ noteHandleDrag = 0; noteMenu = true; noteRenderAll(); return; }
    if(kbPopup){
      int v = kbPopupHit(T.x, T.y); const char* var[4];
      const char* base = mapaActivo[kbLpKey / KB_COLS][kbLpKey % KB_COLS];
      // Con la via rapida, la letra base YA se escribio al tocar. Se comprueba
      // que el caracter anterior al cursor sea EXACTAMENTE esa letra antes de
      // borrar nada: asi, si por lo que fuera no llego a escribirse, no se come
      // el caracter de al lado.
      bool baseYaEscrita = kbFastActive() && kbLastCharIs(base);
      if(v >= 0){
        if(baseYaEscrita) noteBackspace();
        kbGetVariants(base[0], var); noteInsert(var[v]);
      }
      else if(!baseYaEscrita) kbPressChar(base);
      kbPopup = false; kbLpKey = -1; noteRenderAll(); return;
    }
    if(T.tap){
      // Chevron "atras" de la cabecera de Notas: misma logica que el boton de
      // la barra del sistema (cierra capa -> pantalla -> suspende y sale).
      if(T.x < 52 && T.y < 44){ sysBack(); return; }
      // FASE C - barra superior (solo si esta visible)
      if(kbMoreOn){
        int mi = kbMoreHit(T.x, T.y);
        kbMoreOn = false;
        if(mi == 0){ selectAllTxt(); noteRenderAll(); return; }
        if(mi == 1){ char d[48], h[12]; buildShortDate(d, sizeof(d)); clkStrBar(h, sizeof(h));
                     char ln[64]; snprintf(ln, sizeof(ln), "%s %s", d, h); noteInsert(ln); noteRenderAll(); return; }
        noteRenderAll(); return;
      }
      int ti = kbToolHit(T.x, T.y);
      if(ti >= 0){
        if(ti == KB_TB_EMOJI){ mapaActivo = LAYOUT_EMOJI; noteRenderAll(); }
        else if(ti == KB_TB_LANG){ noteFuncKey(2); noteRenderAll(); }              // misma logica que la tecla ES/EN
        else if(ti == KB_TB_CLIP){ if(KB_CLIPBOARD_MULTI_ON){ clipPanelOn = true; clipAskClear = false; clipRenderPanel(); } else { clipPaste(); noteRenderAll(); } }
        else if(ti == KB_TB_SET){ if(KB_SETTINGS_ON) kbsEnter(); }
        else { kbMoreOn = true; kbDrawMore(); }
        return;
      }
      int ci = kbChipHit(T.x, T.y);
      if(ci >= 0){ kbApplyChip(ci); noteRenderAll(); return; }
      if(T.y >= txTop && T.y < txBot){              // tap en texto -> posicionar cursor
        noteClearSel(); noteCur = noteLayoutHit(T.x, T.y); noteRenderAll(); return;
      }
    }
    handleKeyRelease(T.x, T.y); kbLpKey = -1;         // teclado
  }
}
