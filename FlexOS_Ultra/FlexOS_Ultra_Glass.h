// #############################################################
// ##  FLEX OS ULTRA  ·  MATERIAL DE VIDRIO: SDF, REFRACCION Y LUZ
// ##  ----------------------------------------------------------
// ##  La FISICA del Liquid Glass, separada de la POLITICA (que superficie
// ##  usa que tinte: eso sigue en FlexOS_Ultra_Theme.h). Aqui vive:
// ##    · el perfil de calidad unico de todo el sistema (FlexGlassCfg),
// ##    · el campo de distancia con signo (SDF) del rectangulo redondeado
// ##      y la normal derivada de el,
// ##    · la refraccion del fondo desenfocado y su dispersion cromatica,
// ##    · el termino de luz (Fresnel + especular direccional + iridiscencia),
// ##    · la deformacion por toque,
// ##    · y la calidad adaptativa que baja el coste si el sistema se ahoga.
// ##
// ##  NO dibuja nada por si mismo y NO reserva ni un byte de PSRAM. Es
// ##  matematica por pixel que los tres compositores de vidrio del sistema
// ##  -- drawLiquidGlassPanelEx, uiGlassPanelCached y qpGlassSurface --
// ##  llaman DENTRO de la pasada que ya hacian. No hay una pasada nueva.
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
#include "FlexOS_Ultra_Gfx.h"   // eslabon anterior de la cadena

// #############################################################
// ##  DESEMPAQUETADO 565 Y LUMA
// ##  ----------------------------------------------------------
// ##  Viven aqui, y no en Theme.h como antes, porque a partir de ahora
// ##  los usa tambien el muestreo con refraccion: son el eslabon mas
// ##  bajo del material y tienen que estar definidos antes que nadie.
// ##  El codigo es el mismo byte a byte; solo cambio de archivo.
// #############################################################
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

// #############################################################
// ##  MODO VISUAL EFICIENTE  ·  TEMPORAL, y no es una preferencia
// ##  ----------------------------------------------------------
// ##  Lo enciende "Optimizar Flex OS" SOLO si, despues de soltar todo lo
// ##  seguro, la presion de memoria sigue alta; y lo apaga el propio
// ##  sistema en cuanto la presion baja (ver memTick). NO se guarda en
// ##  NVS, NO aparece en Ajustes y NO toca el estilo elegido por el
// ##  usuario: si tiene Liquid Glass, sigue teniendo Liquid Glass -- con
// ##  menos radio de desenfoque -- y si tiene Plano, aqui no cambia nada.
// ##
// ##  Vive en este archivo, y no en Theme.h como antes, porque es una
// ##  decision de CALIDAD y ahora hay una sola fuente de verdad para eso
// ##  (ver el perfil de calidad, justo debajo): gEffMode tambien pone un
// ##  techo al perfil del vidrio.
// #############################################################
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

// #############################################################
// ##  PERFIL DE CALIDAD  ·  LA UNICA FUENTE DE NUMEROS DEL MATERIAL
// ##  ----------------------------------------------------------
// ##  Ni un valor magico del vidrio fuera de esta tabla. Los cuatro
// ##  perfiles son el mismo material con menos trabajo, no materiales
// ##  distintos: se apaga por orden de "coste dividido por lo que se
// ##  nota", que es exactamente el orden pedido -- iridiscencia,
// ##  aberracion cromatica, deformacion por toque, ancho de banda
// ##  (= resolucion efectiva del efecto) y refraccion.
// ##
// ##  UNIDADES, y son enteras a proposito (ver PRECISION NUMERICA
// ##  al final del archivo):
// ##    band      pixeles de banda de borde. 0 = material clasico.
// ##    refract   desplazamiento maximo del muestreo, en 1/16 de pixel.
// ##    chroma    separacion R/B maxima, en 1/16 de pixel y por lado.
// ##    fresnel   alpha maximo del realce omnidireccional de borde (0-255).
// ##    fresPow   exponente del Fresnel (2..5).
// ##    highlight alpha maximo del especular DIRECCIONAL (0-255).
// ##    irid      cuanto tine la luz segun la normal (0-255 = 0-100%).
// ##    touchAmp  amplitud de la deformacion por toque, en 1/16 de pixel.
// ##    touchR    radio del hundimiento del dedo, en pixeles.
// #############################################################
struct FlexGlassCfg {
  uint8_t band;
  uint8_t refract;
  uint8_t chroma;
  uint8_t fresnel;
  uint8_t fresPow;
  uint8_t highlight;
  uint8_t irid;
  uint8_t touchAmp;
  uint8_t touchR;
};

#define GLQ_ULTRA   0
#define GLQ_HIGH    1
#define GLQ_MEDIUM  2
#define GLQ_LOW     3
#define GLQ_N       4

// Los valores salen de la restriccion visual, no de "lo que se puede subir":
//  · refract 64/16 = 4 px de desplazamiento en el borde mismo, cayendo a 0 en
//    el centro. Mas que esto ya no parece vidrio curvado sino agua.
//  · chroma 12/16 = 0,75 px por lado -> 1,5 px de separacion R-B total, que es
//    el techo que se pidio. A simple vista no se lee como "RGB", solo como
//    que el canto del cristal tiene color.
//  · fresnel/highlight rondan el 17-20 % de blanco EN EL BORDE MISMO y caen
//    con la cuarta potencia: a 3 px de profundidad ya no llegan al 2 %. Por
//    eso no producen un contorno blanco permanente.
//  · irid 14/255 = 5,5 %: dentro del 0-10 % pedido.
static const FlexGlassCfg kGlassQ[GLQ_N] = {
  /* ULTRA  */ { 14, 64, 12, 52, 4, 46, 14, 56, 44 },
  /* HIGH   */ { 11, 56,  8, 46, 4, 42, 10, 48, 40 },
  /* MEDIUM */ {  8, 44,  0, 38, 3, 34,  0, 38, 34 },
  /* LOW    */ {  0,  0,  0,  0, 0,  0,  0,  0,  0 },
};

// Perfil elegido por el usuario (techo) y perfil EFECTIVO (lo que se dibuja).
// El efectivo nunca sube por encima del techo; solo puede bajar, y lo baja la
// calidad adaptativa o el modo visual eficiente.
static uint8_t gGlassQWant = GLQ_HIGH;
static uint8_t gGlassQNow  = GLQ_HIGH;
// GENERACION DEL MATERIAL. Sube cada vez que el perfil efectivo cambia. Quien
// CACHEA vidrio ya compuesto (la tarjeta de fondo plano, drawGlassCardFlat)
// mete este numero en su firma: sin el, un cambio de calidad dejaria en
// pantalla tarjetas compuestas con el perfil anterior hasta que cambiase su
// tamano o su color.
static uint32_t gGlassGen = 0;
// VETO DEL TOQUE. Lo enciende quien compone vidrio FUERA de la pantalla para
// cachearlo: ahi las coordenadas no son las de la pantalla y, sobre todo, el
// resultado se reutiliza en otro sitio y en otro momento -- un hundimiento de
// dedo grabado en la cache se quedaria pegado para siempre en el sitio
// equivocado. Es una sola comparacion en glEdgeBegin.
static bool gGlassNoTouch = false;
static inline const FlexGlassCfg& GLC(){ return kGlassQ[gGlassQNow]; }
static inline bool glassAdvanced(){ return kGlassQ[gGlassQNow].band != 0; }

// #############################################################
// ##  DEFORMACION POR TOQUE  ·  estado, no animacion
// ##  ----------------------------------------------------------
// ##  El manejador tactil SOLO escribe aqui (tres enteros). No captura
// ##  pantalla, no desenfoca, no calcula SDF y no lanza ningun hilo: eso
// ##  volveria a meter latencia justo donde no puede haberla.
// ##
// ##  Quien interpola es el compositor, en el cuadro que ya iba a
// ##  dibujar. En reposo glTouchAmp() devuelve 0 y todo el bloque de
// ##  toque se salta con una comparacion: CERO coste cuando no hay dedo.
// ##
// ##  LIMITACION HONESTA, y es deliberada: esto NO fuerza a repintar
// ##  ningun panel. La deformacion se ve en las superficies que el
// ##  sistema ya estaba repintando durante esos ~190 ms -- que son
// ##  justo las que el dedo esta tocando (destello del control del panel
// ##  rapido, fila resaltada de una lista, boton pulsado, icono del
// ##  escritorio). Forzar un repintado global por cada toque seria
// ##  exactamente el "full-screen redraw por cada touch" prohibido.
// #############################################################
#define GL_TOUCH_MS 190                 // duracion del hundimiento + retorno
static int      glTouchX  = 0, glTouchY = 0;
static uint32_t glTouchT0 = 0;
static bool     glTouchDn = false;      // dedo todavia apoyado

// Hay deformacion en curso. CADUCA AQUI, y no en quien pregunte por la
// amplitud: si la caducidad viviera en glTouchAmp(), el estado seguiria
// "vivo" para todo el que solo preguntase si hay dedo -- y eso es justo lo
// que hace el compositor en su primera linea, antes de pedir nada mas.
static inline bool glassTouchLive(){
  if(!glTouchT0) return false;
  if(glTouchDn)  return true;
  if((uint32_t)(millis() - glTouchT0) >= GL_TOUCH_MS){ glTouchT0 = 0; return false; }
  return true;
}
// Curva del gesto: sube deprisa (hundimiento) y vuelve despacio (elastico).
// Con el dedo APOYADO se queda en el tope; al soltar decae. Devuelve 0..255.
static uint8_t glTouchAmp(){
  if(!glassTouchLive()) return 0;
  uint32_t e = millis() - glTouchT0;
  if(glTouchDn) return (e < 60u) ? (uint8_t)(255u * e / 60u) : 255;
  uint32_t k = 255u - (255u * e) / GL_TOUCH_MS;
  return (uint8_t)((k * k) >> 8);        // retorno suave, no lineal
}
// Los llama el tactil de alto nivel. Son tres asignaciones: nada mas.
static inline void glassTouchDown(int x, int y){
  if(!glassAdvanced() || GLC().touchAmp == 0) return;
  glTouchX = x; glTouchY = y; glTouchT0 = millis(); glTouchDn = true;
}
static inline void glassTouchMove(int x, int y){
  if(!glTouchDn) return;
  glTouchX = x; glTouchY = y;
}
static inline void glassTouchUp(){
  if(!glTouchDn) return;
  glTouchDn = false; glTouchT0 = millis();   // reinicia el reloj para el retorno
}

// #############################################################
// ##  TABLAS PRECALCULADAS  ·  ni una division ni un pow por pixel
// ##  ----------------------------------------------------------
// ##  Las tres viven en .rodata (FLASH), no en RAM: son const y el
// ##  compilador no necesita copiarlas. Suman 194 bytes.
// #############################################################
// Caida del hundimiento del dedo: 255*exp(-3*k/32) para k = 0..31, o sea la
// exponencial pedida resuelta en tiempo de compilacion. El indice se acota a
// 31 en el unico sitio que la lee, asi que no hay entrada centinela.
static const uint8_t kGlFall[32] = {
  255, 232, 211, 192, 175, 160, 145, 132, 120, 110, 100,  91,  83,  75,  69,  62,
   57,  52,  47,  43,  39,  36,  32,  30,  27,  24,  22,  20,  18,  17,  15,  14
};
// Reciproco 1/n en Q12 para n = 0..80. Normaliza el vector radial del toque
// sin dividir: el radio esta acotado por touchR (<= 44 px) y la longitud
// octogonal aproximada nunca pasa de ~1,5 veces ese radio.
static const uint16_t kGlRecip[81] = {
  4096, 4096, 2048, 1365, 1024,  819,  683,  585,  512,  455,  410,  372,  341,  315,  293,  273,
   256,  241,  228,  216,  205,  195,  186,  178,  171,  164,  158,  152,  146,  141,  137,  132,
   128,  124,  120,  117,  114,  111,  108,  105,  102,  100,   98,   95,   93,   91,   89,   87,
    85,   84,   82,   80,   79,   77,   76,   74,   73,   72,   71,   69,   68,   67,   66,   65,
    64,   63,   62,   61,   60,   59,   59,   58,   57,   56,   55,   55,   54,   53,   53,   52,
    51
};

// Longitud euclidea aproximada por la regla del octogono: max + min/2. El
// error es de +-6 % y ahi no se nota nada, porque la MAGNITUD del efecto la
// fija edgeFactor y no esta normalizacion. A cambio se ahorra una raiz por
// pixel en toda la zona recta del borde, que es la mayoria de la banda.
static inline int glOctLen(int a, int b){
  if(a < 0) a = -a;
  if(b < 0) b = -b;
  return (a > b) ? (a + (b >> 1)) : (b + (a >> 1));
}

// #############################################################
// ##  GEOMETRIA DEL VIDRIO  ·  SDF DEL RECTANGULO REDONDEADO
// ##  ----------------------------------------------------------
// ##  Todo -- cobertura del borde, refraccion, Fresnel, especular y
// ##  aberracion -- sale de UN solo numero por pixel: la distancia con
// ##  signo al borde. Negativa dentro, cero justo en el canto.
// ##
// ##      qx = |px - cx| - (W/2 - r)          (por columna)
// ##      qy = |py - cy| - (H/2 - r)          (por FILA, una vez)
// ##      d  = (qx>0 && qy>0) ? hypot(qx,qy) - r      <- esquina
// ##                          : max(qx,qy)   - r      <- lado recto
// ##
// ##  Los pixeles se miden por su CENTRO (px = i + 0,5). Eso no es un
// ##  detalle: con el centro, el pixel mas exterior de un lado recto da
// ##  d = -0,5 -- cobertura completa -- y el panel conserva EXACTAMENTE
// ##  la huella que tenia con glInset(). Solo las esquinas cambian, y
// ##  cambian para bien: donde antes habia un escalon binario ahora hay
// ##  una rampa de cobertura de un pixel.
// ##
// ##  TODO EN Q4 (1/16 de pixel). Es la precision justa: el muestreo con
// ##  refraccion interpola en 1/16, que sobre un fondo YA desenfocado es
// ##  indistinguible de una interpolacion continua, y todos los productos
// ##  intermedios caben de sobra en int32 (qx4 <= 3840, su cuadrado
// ##  14,7 M, la suma de dos 29,5 M).
// #############################################################
struct GlassEdge {
  // --- geometria del panel, en Q4 ---
  int cx4, cy4;          // centro
  int bx4, by4;          // semiejes menos el radio
  int r4;                // radio
  int band4;             // ancho de la banda de borde
  uint32_t recipBand;    // 2^20 / band4 + 1 (evita la division de edgeFactor)
  int bandPx;            // grosor de la banda, en pixeles
  int capPx;             // alcance horizontal de la banda en las filas del arco
  int w, h;
  // --- material, copiado del perfil una sola vez por panel ---
  int refract, chroma, fresnel, fresPow, highlight, irid;
  // COLOR DEL REALCE, TABULADO POR DIRECCION DE LA NORMAL.
  // La iridiscencia solo depende de nx, asi que resolverla por pixel era
  // pagar dos mezclas para obtener uno de diecisiete colores. Se resuelve
  // una vez por panel. 17 entradas son un paso de 1/8 de la normal: por
  // debajo del escalon de color de RGB565, o sea invisible.
  uint16_t hiLut[17];
  // --- fila actual (lo pone glEdgeRow) ---
  int qy4, sgnY, row;
  bool rowAll;           // esta fila es banda de lado a lado (canto recto)
  bool rowCap;           // esta fila cruza un arco de esquina
  bool tRow;             // el dedo alcanza esta fila
  int  tx0, tx1;         // columnas que alcanza el dedo en esta fila
  // --- toque, ya resuelto a coordenadas RELATIVAS al panel ---
  bool tOn;
  int  tx, ty, tAmp, tR;
};

// Resultado por pixel del bloque geometrico. Lo rellena glEdgePx() y lo
// consumen el muestreo y la luz, para no recalcular nada dos veces.
struct GlassPx {
  uint8_t  cov;          // cobertura del borde (255 = dentro del todo)
  int      ox4, oy4;     // desplazamiento del muestreo (Q4)
  int      chx4, chy4;   // paso de la dispersion cromatica (Q4)
  uint8_t  hiA, loA;     // alpha del realce y de su sombra opuesta
  uint16_t hiCol;        // color del realce (iridiscencia ya aplicada)
};

// Prepara el panel. Devuelve false si el material avanzado no toca aqui
// (perfil LOW, panel degenerado): el llamante sigue con su ruta clasica.
static bool glEdgeBegin(GlassEdge& e, int w, int h, int rad,
                        int panelX, int panelY){
  const FlexGlassCfg& c = GLC();
  if(c.band == 0 || w <= 2 || h <= 2) return false;
  if(rad < 0) rad = 0;
  int band = c.band;
  // La banda nunca puede comerse el panel entero: en una capsula de 28 px de
  // alto una banda de 14 dejaria el centro sin existir y el material seria
  // solo borde. Se acota a un tercio del lado menor.
  int lim = (w < h ? w : h) / 3;
  if(lim < 2) return false;
  if(band > lim) band = lim;
  e.w = w; e.h = h;
  e.cx4 = w << 3; e.cy4 = h << 3;                 // (w/2) en Q4 == w*8
  e.bx4 = (w << 3) - (rad << 4);
  e.by4 = (h << 3) - (rad << 4);
  if(e.bx4 < 0) e.bx4 = 0;
  if(e.by4 < 0) e.by4 = 0;
  e.r4 = rad << 4;
  e.band4 = band << 4;
  e.recipBand = 1048576u / (uint32_t)e.band4 + 1u;
  // GROSOR frente a ALCANCE, y la diferencia importa mucho para el coste.
  // La banda es una tira de grosor CONSTANTE que sigue el contorno, asi que
  // en el canto recto solo entra 'band' pixeles hacia dentro -- no 'rad +
  // band'. El radio solo cuenta en las filas que cruzan un arco de esquina,
  // porque ahi la tira se tumba y llega mas lejos en horizontal.
  //
  // Distinguir los dos casos es lo que baja los pixeles con efecto del 50 %
  // al 21 % en una tarjeta de 440x160 con radio 22: 2,4 veces menos trabajo,
  // con el mismo resultado pixel a pixel (glEdgePx sigue descartando por su
  // cuenta cualquier pixel cuyo edgeFactor sea 0).
  e.bandPx = band;
  e.capPx  = rad + band;
  if(e.bandPx > w / 2) e.bandPx = (w + 1) / 2;
  if(e.capPx  > w / 2) e.capPx  = (w + 1) / 2;
  e.refract = c.refract; e.chroma = c.chroma;
  e.fresnel = c.fresnel; e.fresPow = c.fresPow;
  e.highlight = c.highlight; e.irid = c.irid;
  // Sin iridiscencia el realce es blanco puro y la tabla es constante: se
  // rellena igual, y asi el bucle de pixeles no necesita ni una rama.
  {
    const uint16_t white = rgb565(255,255,255);
    if(c.irid){
      const uint16_t cool = rgb565(196, 214, 255), warm = rgb565(255, 226, 206);
      for(int k = 0; k <= 16; k++)
        e.hiLut[k] = mix565(white, mix565(cool, warm, (uint8_t)(k * 255 / 16)), c.irid);
    } else {
      for(int k = 0; k <= 16; k++) e.hiLut[k] = white;
    }
  }
  // Toque: se pasa a coordenadas del panel UNA vez y se descarta aqui mismo
  // si cae lejos. Un panel que el dedo no toca no paga absolutamente nada.
  e.tOn = false; e.tAmp = 0; e.tR = c.touchR; e.tx = 0; e.ty = 0;
  if(c.touchAmp && !gGlassNoTouch && glassTouchLive()){
    int amp = glTouchAmp();
    if(amp){
      int lx = glTouchX - panelX, ly = glTouchY - panelY;
      if(lx > -c.touchR && lx < w + c.touchR && ly > -c.touchR && ly < h + c.touchR){
        e.tOn = true; e.tx = lx; e.ty = ly;
        e.tAmp = (amp * (int)c.touchAmp) >> 8;     // 1/16 px de hundimiento
      }
    }
  }
  return true;
}

// Constantes de la fila. Se llama UNA vez por fila, nunca por pixel.
static inline void glEdgeRow(GlassEdge& e, int j){
  int py4 = (j << 4) + 8;
  int d   = py4 - e.cy4;
  e.sgnY  = (d >= 0) ? 1 : -1;
  e.qy4   = (d >= 0 ? d : -d) - e.by4;
  e.rowAll = (j < e.bandPx) || (j >= e.h - e.bandPx);        // canto recto de arriba/abajo
  e.rowCap = (j < e.capPx)  || (j >= e.h - e.capPx);        // la fila cruza un arco
  // Franja del dedo DENTRO de esta fila. Sin dedo, o con el dedo lejos de la
  // fila, queda vacia y el tramo central de la fila se salta entero.
  e.tRow = false;
  if(e.tOn){
    int dy = j - e.ty; if(dy < 0) dy = -dy;
    if(dy < e.tR){ e.tRow = true; e.tx0 = e.tx - e.tR; e.tx1 = e.tx + e.tR; }
  }
  e.row = j;
}

// #############################################################
// ##  EL TRAMO CENTRAL DE LA FILA  ·  lo que NO se toca
// ##  ----------------------------------------------------------
// ##  Aqui es donde este efecto deja de costar el AREA del panel y pasa
// ##  a costar su PERIMETRO. Devuelve el intervalo [c0,c1] de columnas
// ##  en el que edgeFactor vale exactamente 0 y no hay dedo encima: el
// ##  llamante lo resuelve con su mezcla de siempre, sin SDF, sin
// ##  muestreo y sin luz. En una tarjeta de 440x160 con radio 22 y banda
// ##  de 11 px eso es el 79 % de los pixeles.
// ##
// ##  Devuelve false si la fila no tiene centro (filas de las tapas,
// ##  paneles estrechos, o una fila cruzada por el dedo).
// #############################################################
static inline bool glEdgeCenterSpan(const GlassEdge& e, int& c0, int& c1){
  if(e.rowAll) return false;
  const int ext = e.rowCap ? e.capPx : e.bandPx;
  c0 = ext; c1 = e.w - 1 - ext;
  if(c0 > c1) return false;
  if(!e.tRow) return true;
  // Con el dedo en la fila el centro se parte en dos. Se devuelve el trozo
  // mas grande de los dos, que es el que de verdad ahorra; el otro cae en la
  // ruta lenta y no pasa nada: son como mucho 44 columnas.
  int la = e.tx0 - c0, lb = c1 - e.tx1;
  if(la <= 0 && lb <= 0) return false;
  if(la >= lb) c1 = e.tx0 - 1;
  else         c0 = e.tx1 + 1;
  return c0 <= c1;
}

// Direccion de la luz: arriba-izquierda, la misma que el sistema ya usaba en
// GLASS_CORNER_STRONG/WEAK. En Q8 normalizada: (-181, -181) ~= (-0,707, -0,707).
#define GL_LX  (-181)
#define GL_LY  (-181)

// Esta esta columna dentro del alcance del dedo EN ESTA FILA. Es la guarda
// que impide que un toque convierta el panel entero en ruta lenta: sin ella,
// tocar una tarjeta grande haria trabajo de borde en sus 50.000 pixeles.
static inline bool glEdgeInTouch(const GlassEdge& e, int i){
  return e.tRow && i >= e.tx0 && i <= e.tx1;
}

// #############################################################
// ##  EL PIXEL  ·  SDF -> normal -> refraccion -> Fresnel -> luz
// ##  ----------------------------------------------------------
// ##  Una sola funcion y una sola pasada. Devuelve false cuando el pixel
// ##  esta en el CENTRO del panel (edgeFactor = 0): ahi no hay nada que
// ##  hacer y el llamante toma su camino rapido de siempre. Ese "false"
// ##  es lo que hace que todo esto cueste el perimetro y no el area.
// #############################################################
static inline bool glEdgePx(const GlassEdge& e, int i, GlassPx& o){
  int px4 = (i << 4) + 8;
  int dx  = px4 - e.cx4;
  int sgnX = (dx >= 0) ? 1 : -1;
  int qx4 = (dx >= 0 ? dx : -dx) - e.bx4;

  // ---- 1. distancia con signo, y salida temprana del centro ----
  int d4, nx8, ny8;
  if(qx4 > 0 && e.qy4 > 0){
    int len4 = isqrt32(qx4 * qx4 + e.qy4 * e.qy4);
    d4 = len4 - e.r4;
    if(d4 <= -e.band4 && !glEdgeInTouch(e, i)) return false;
    // Normal EXACTA en la esquina: el gradiente del SDF es el radio
    // unitario. Una sola division por pixel de esquina, y las esquinas son
    // unos pocos cientos de pixeles por panel.
    if(len4 > 0){
      uint32_t inv = 16777216u / (uint32_t)len4;   // 2^24 / len4
      nx8 = (int)(((int64_t)qx4 * (int)inv) >> 16) * sgnX;
      ny8 = (int)(((int64_t)e.qy4 * (int)inv) >> 16) * e.sgnY;
    } else { nx8 = 0; ny8 = 0; }
  } else {
    d4 = (qx4 > e.qy4 ? qx4 : e.qy4) - e.r4;
    if(d4 <= -e.band4 && !glEdgeInTouch(e, i)) return false;
    // Normal en el lado recto. El gradiente del max() es un escalon en la
    // diagonal, y un escalon ahi se ve como un pliegue de 45 grados saliendo
    // de cada esquina -- el "halo cuadrado" que hay que evitar. Se reparte el
    // peso entre los dos ejes en una franja de 8 px alrededor de la diagonal:
    // el resultado ya no es unitario, y da igual, porque la magnitud la pone
    // edgeFactor (ver glOctLen).
    int t = qx4 - e.qy4;
    if(t >  128) t =  128;
    if(t < -128) t = -128;
    int wx = t + 128;                 // 0..256: 256 = normal puramente en X
    nx8 = sgnX * wx;
    ny8 = e.sgnY * (256 - wx);
  }
  // Fuera del panel del todo: ni siquiera hay cobertura que mezclar. Se
  // devuelve true (el pixel ES del borde) con cobertura 0, y el resto de
  // GlassPx queda SIN RELLENAR a proposito: los dos llamantes comprueban
  // cov == 0 en la linea siguiente y se saltan el pixel entero.
  if(d4 >= 8){ o.cov = 0; return true; }

  // ---- 2. cobertura antialias (1 px de rampa centrada en el canto) ----
  {
    int c = (8 - d4) << 4;            // d4=-8 -> 256 ; d4=+8 -> 0
    if(c > 255) c = 255;
    if(c < 0)   c = 0;
    o.cov = (uint8_t)c;
  }

  // ---- 3. edgeFactor: 0 en el centro, 255 en el canto ----
  int ef8;
  {
    int num = e.band4 + d4;           // 0 .. band4
    if(num < 0) num = 0;
    ef8 = (int)(((uint32_t)num * e.recipBand) >> 12);
    if(ef8 > 255) ef8 = 255;
  }
  // Perfil de la superficie. El vidrio no es un bisel plano: la curvatura se
  // concentra en el ultimo tercio. ef^2 es justo eso, y cuesta un producto.
  int curve = (ef8 * ef8) >> 8;

  // ---- 4. refraccion: se muestrea HACIA DENTRO del panel ----
  // Muestrear hacia dentro (y no hacia fuera) es lo que hace que el canto
  // ESTIRE lo que hay detras, que es como se comporta el borde de una lente
  // real. Hacia fuera daria el efecto contrario y se leeria como un glitch.
  int mag = (e.refract * curve) >> 8;          // 1/16 px
  o.ox4 = -((nx8 * mag) >> 8);
  o.oy4 = -((ny8 * mag) >> 8);

  // ---- 5. hundimiento bajo el dedo ----
  // Se SUMA al desplazamiento que ya se iba a aplicar: no es una capa nueva
  // ni una pasada nueva, son dos enteros mas. El empuje es radial desde el
  // punto de contacto y se apaga con la distancia segun kGlFall, que es la
  // exponencial pedida resuelta en tiempo de compilacion.
  if(glEdgeInTouch(e, i)){
    int dx = i - e.tx, dy = e.row - e.ty;
    int len = glOctLen(dx, dy);
    if(len < e.tR){
      int k = (len << 5) / e.tR;                  // 0..31 (tR > 0 por construccion)
      if(k > 31) k = 31;
      int amp = (e.tAmp * kGlFall[k]) >> 8;       // 1/16 px
      if(amp){
        // (dx/len) * amp. inv es Q12 y amp ya viene en Q4, asi que el
        // desplazamiento sale en Q4 quitando SOLO los 12 bits del reciproco.
        int inv = kGlRecip[len > 80 ? 80 : len];
        o.ox4 += (dx * inv * amp) >> 12;
        o.oy4 += (dy * inv * amp) >> 12;
      }
    }
  }

  // ---- 6. dispersion cromatica, solo en el tercio exterior ----
  // Si se aplicase en toda la banda se leeria como "RGB" y no como cristal.
  if(e.chroma && ef8 > 170){
    int cm = (e.chroma * (ef8 - 170)) / 85;    // 0 en ef=170, maximo en el canto
    o.chx4 = -((nx8 * cm) >> 8);
    o.chy4 = -((ny8 * cm) >> 8);
  } else { o.chx4 = 0; o.chy4 = 0; }

  // ---- 7. Fresnel y luz, en el mismo bloque ----
  // Adaptacion 2D del Fresnel: no hay camara, pero si hay una superficie que
  // se inclina hacia el observador segun se acerca al canto. Ese "cuanto se
  // aparta la normal de la vista" es exactamente edgeFactor, asi que
  // pow(1 - dot(N,V), p) se resuelve como pow(ef, p) por cuadrados.
  int fres = ef8;
  { int e2 = (fres * fres) >> 8;
    if(e.fresPow >= 4) fres = (e2 * e2) >> 8;
    else if(e.fresPow == 3) fres = (e2 * fres) >> 8;
    else fres = e2;
    if(e.fresPow == 5) fres = (fres * ef8) >> 8;
  }
  int ndl = (nx8 * GL_LX + ny8 * GL_LY) >> 8;   // -256..256
  int lit = (e.fresnel * fres) >> 8;            // realce omnidireccional
  if(ndl > 0) lit += (e.highlight * ((fres * ndl) >> 8)) >> 8;
  if(lit > 255) lit = 255;
  o.hiA = (uint8_t)lit;
  // La cara opuesta a la luz no se ilumina: se oscurece, y la mitad de fuerte.
  // Eso es lo que da GROSOR al canto sin pintar una linea.
  int dark = (ndl < 0) ? ((e.highlight * ((fres * (-ndl)) >> 8)) >> 9) : 0;
  o.loA = (uint8_t)(dark > 255 ? 255 : dark);
  // Iridiscencia: el realce se tine de frio o de calido segun hacia donde
  // mire la normal. Es el unico sitio donde el vidrio tiene color propio, y
  // aqui ya es una sola lectura de tabla (ver hiLut).
  {
    int k = (nx8 + 256) >> 5;          // [-256,256] -> [0,16]
    if(k < 0) k = 0;
    if(k > 16) k = 16;
    o.hiCol = e.hiLut[k];
  }
  return true;
}

// #############################################################
// ##  MUESTREO DEL FONDO DESENFOCADO CON SUBPIXEL
// ##  ----------------------------------------------------------
// ##  Bilineal, pero degenerado: en los lados rectos una de las dos
// ##  componentes del desplazamiento es exactamente 0, asi que la mayor
// ##  parte de la banda se resuelve con DOS lecturas y UNA mezcla. Las
// ##  cuatro lecturas completas solo ocurren en las esquinas.
// ##
// ##  Sin subpixel (redondeando a pixel entero) el degradado de la
// ##  refraccion sale escalonado: son solo 4 px de recorrido repartidos
// ##  en 14, o sea saltos de un cuarto de banda. Por eso se interpola.
// #############################################################
static inline uint16_t glSample(const uint16_t* base, int stride, int w, int h,
                                int fx4, int fy4){
  int ix = fx4 >> 4, iy = fy4 >> 4;
  int tx = (fx4 & 15) << 4, ty = (fy4 & 15) << 4;      // 0..240, Q8
  if(ix < 0){ ix = 0; tx = 0; }
  if(iy < 0){ iy = 0; ty = 0; }
  if(ix > w - 1){ ix = w - 1; tx = 0; }
  if(iy > h - 1){ iy = h - 1; ty = 0; }
  int ix1 = (ix + 1 < w) ? ix + 1 : ix;
  const uint16_t* r0 = base + (size_t)iy * stride;
  if(ty == 0){
    if(tx == 0) return r0[ix];
    return mix565(r0[ix], r0[ix1], (uint8_t)tx);
  }
  const uint16_t* r1 = base + (size_t)((iy + 1 < h) ? iy + 1 : iy) * stride;
  if(tx == 0) return mix565(r0[ix], r1[ix], (uint8_t)ty);
  return mix565(mix565(r0[ix], r0[ix1], (uint8_t)tx),
                mix565(r1[ix], r1[ix1], (uint8_t)tx), (uint8_t)ty);
}

// #############################################################
// ##  MUESTREO CUANDO ORIGEN Y DESTINO SON EL MISMO BUFFER
// ##  ----------------------------------------------------------
// ##  El panel rapido compone EN SITIO: lee y escribe sobre qsBuf. Un
// ##  muestreo que se desplaza hacia arriba leeria filas que esta misma
// ##  superficie acaba de escribir, y el tinte se aplicaria dos veces --
// ##  una banda de color mas saturado de unos pocos pixeles justo en el
// ##  canto inferior.
// ##
// ##  Se resuelve igual que ya lo resolvia el box-blur del sistema (ver
// ##  glbRing): un anillo con las ultimas filas TAL COMO ERAN. Solo hace
// ##  falta mirar hacia atras, porque las filas de abajo aun no se han
// ##  tocado y se leen del buffer directamente.
// ##
// ##  COSTE: GL_RING_N x SCR_W x 2 = 7,5 KB de RAM INTERNA (no PSRAM).
// ##  Es el unico buffer que anade todo este trabajo, y solo lo usa el
// ##  compositor en sitio; los otros dos leen de un buffer aparte y no
// ##  pasan por aqui.
// ##
// ##  POR QUE OCHO FILAS Y NO MAS. El desplazamiento vertical del perfil
// ##  ULTRA son 4,0 px de refraccion + 0,75 de aberracion = 4,7 px, que
// ##  caben con holgura. Solo se pasa si el hundimiento del dedo (hasta
// ##  3,5 px) apunta ademas EN LA MISMA DIRECCION: 8,2 px en el peor
// ##  caso. Ahi glRingRow devuelve la fila mas vieja que conserva, o sea
// ##  un error de 0,2 px sobre un fondo ya desenfocado -- invisible --, y
// ##  nunca una lectura fuera de rango. Doblar el anillo para cubrir ese
// ##  caso costaria 7,5 KB mas de RAM interna y no se veria la
// ##  diferencia; se prefiere la memoria.
// #############################################################
#define GL_RING_N 8
#define GL_RING_M (GL_RING_N - 1)
static uint16_t glRing[GL_RING_N][SCR_W];
static int      glRingFirst = 0x7FFFFFFF;   // primera fila que guardo la superficie actual

// Abre el anillo para una superficie nueva. Dos asignaciones.
static inline void glRingBegin(){ glRingFirst = 0x7FFFFFFF; }
// Guarda la fila 'y' ANTES de componerla. Solo las columnas del panel.
static inline void glRingPut(int y, const uint16_t* row, int x0, int w){
  if(y < glRingFirst) glRingFirst = y;
  if(x0 < 0) x0 = 0;
  if(x0 + w > SCR_W) w = SCR_W - x0;
  if(w > 0) memcpy(glRing[y & GL_RING_M] + x0, row + x0, (size_t)w * 2);
}
// La fila 'y' tal como era. Del buffer si aun no se escribio o si es
// anterior a esta superficie; del anillo si la escribimos nosotros.
static inline const uint16_t* glRingRow(const uint16_t* buf, int stride, int y, int curY){
  if(y > curY || y < glRingFirst) return buf + (size_t)y * stride;
  if(curY - y < GL_RING_N) return glRing[y & GL_RING_M];
  return glRing[(curY - GL_RING_N + 1) & GL_RING_M];   // la mas vieja que queda
}

// Muestreo bilineal en sitio. Misma matematica que glSample; lo unico que
// cambia es de donde sale cada una de las dos filas. 'x0/y0' son el origen
// del panel dentro del buffer y 'curY' la fila que se esta componiendo.
static inline uint16_t glSampleIP(const uint16_t* buf, int stride, int x0, int y0,
                                  int w, int h, int curY, int fx4, int fy4){
  int ix = fx4 >> 4, iy = fy4 >> 4;
  int tx = (fx4 & 15) << 4, ty = (fy4 & 15) << 4;
  if(ix < 0){ ix = 0; tx = 0; }
  if(iy < 0){ iy = 0; ty = 0; }
  if(ix > w - 1){ ix = w - 1; tx = 0; }
  if(iy > h - 1){ iy = h - 1; ty = 0; }
  int ax = x0 + ix, ax1 = x0 + ((ix + 1 < w) ? ix + 1 : ix);
  const uint16_t* r0 = glRingRow(buf, stride, y0 + iy, curY);
  if(ty == 0){
    if(tx == 0) return r0[ax];
    return mix565(r0[ax], r0[ax1], (uint8_t)tx);
  }
  const uint16_t* r1 = glRingRow(buf, stride, y0 + ((iy + 1 < h) ? iy + 1 : iy), curY);
  if(tx == 0) return mix565(r0[ax], r1[ax], (uint8_t)ty);
  return mix565(mix565(r0[ax], r0[ax1], (uint8_t)tx),
                mix565(r1[ax], r1[ax1], (uint8_t)tx), (uint8_t)ty);
}

// Version en sitio de glRefract: misma dispersion cromatica, mismo numero de
// muestras, leyendo por el camino de arriba.
static inline uint16_t glRefractIP(const GlassPx& o, const uint16_t* buf, int stride,
                                   int x0, int y0, int w, int h, int curY,
                                   int bx4, int by4){
  int fx = bx4 + o.ox4, fy = by4 + o.oy4;
  if(o.chx4 == 0 && o.chy4 == 0) return glSampleIP(buf, stride, x0, y0, w, h, curY, fx, fy);
  uint16_t cr = glSampleIP(buf, stride, x0, y0, w, h, curY, fx + o.chx4, fy + o.chy4);
  uint16_t cg = glSampleIP(buf, stride, x0, y0, w, h, curY, fx,         fy);
  uint16_t cb = glSampleIP(buf, stride, x0, y0, w, h, curY, fx - o.chx4, fy - o.chy4);
  return (uint16_t)((cr & 0xF800) | (cg & 0x07E0) | (cb & 0x001F));
}

// Fondo refractado CON dispersion cromatica. Tres muestras a lo largo de la
// normal (R la mas desplazada, B la menos) y se recombinan por canal. Cuando
// el perfil no tiene aberracion, o el pixel no esta en el tercio exterior,
// esto es UNA muestra: el caso barato es el caso normal.
static inline uint16_t glRefract(const GlassPx& o, const uint16_t* base,
                                 int stride, int w, int h, int bx4, int by4){
  int fx = bx4 + o.ox4, fy = by4 + o.oy4;
  if(o.chx4 == 0 && o.chy4 == 0) return glSample(base, stride, w, h, fx, fy);
  uint16_t cr = glSample(base, stride, w, h, fx + o.chx4, fy + o.chy4);
  uint16_t cg = glSample(base, stride, w, h, fx,          fy);
  uint16_t cb = glSample(base, stride, w, h, fx - o.chx4, fy - o.chy4);
  return (uint16_t)((cr & 0xF800) | (cg & 0x07E0) | (cb & 0x001F));
}

// Termino de luz sobre el color YA tintado. Dos mezclas como mucho, y la
// segunda casi nunca: es el ultimo paso de la misma pasada.
static inline uint16_t glLight(const GlassPx& o, uint16_t c){
  if(o.hiA) c = mix565(c, o.hiCol, o.hiA);
  if(o.loA) c = mix565(c, rgb565(6, 10, 20), o.loA);
  return c;
}

// #############################################################
// ##  CALIDAD ADAPTATIVA  ·  por ventana de tiempo, nunca por cuadro
// ##  ----------------------------------------------------------
// ##  Se mide lo unico que esta funcion controla: los microsegundos que
// ##  el sistema gasta COMPONIENDO VIDRIO. No se inventa un "FPS
// ##  global" -- este sistema no tiene uno, cada pantalla publica su
// ##  banda cuando le toca -- sino que se compara el gasto real contra
// ##  un presupuesto sobre el tiempo transcurrido.
// ##
// ##  Histeresis a proposito: se BAJA con una ventana mala y se SUBE
// ##  solo despues de tres ventanas buenas seguidas. Sin eso, un perfil
// ##  oscilando entre dos niveles se veria como un parpadeo del
// ##  material, que es peor que quedarse en el nivel bajo.
// #############################################################
#define GL_WIN_MS      1000u    // ventana de evaluacion
#define GL_BUDGET_PCT    22     // techo: 22 % del tiempo compuesto vidrio
#define GL_RELAX_PCT     10     // por debajo de esto se puede recuperar
static uint32_t glStatUs = 0;          // us de composicion en la ventana
static uint32_t glWinMs  = 0;
static uint8_t  glGoodWins = 0;
static inline void glStatAdd(uint32_t us){ glStatUs += us; }

static void glassQualityTick(){
  uint32_t now = millis();
  if(!glWinMs){ glWinMs = now; glStatUs = 0; return; }
  uint32_t dt = now - glWinMs;
  if(dt < GL_WIN_MS) return;
  // Techo del usuario y techo del modo eficiente, en ese orden.
  uint8_t cap = gGlassQWant;
  if(gEffMode && cap < GLQ_MEDIUM) cap = GLQ_MEDIUM;
  uint32_t pct = (glStatUs / 10u) / dt;        // us/1000 sobre ms -> %
  const uint8_t before = gGlassQNow;
  if(pct > GL_BUDGET_PCT){
    if(gGlassQNow < GLQ_LOW) gGlassQNow++;
    glGoodWins = 0;
  } else if(pct < GL_RELAX_PCT){
    if(++glGoodWins >= 3){ glGoodWins = 0; if(gGlassQNow > cap) gGlassQNow--; }
  } else {
    glGoodWins = 0;
  }
  if(gGlassQNow < cap) gGlassQNow = cap;       // el techo manda siempre
  if(gGlassQNow != before) gGlassGen++;
  glWinMs = now; glStatUs = 0;
}
// Vuelve al perfil pedido por el usuario. Lo llama todo cambio que invalida
// la medida anterior (cambio de tema, de material, salir de modo eficiente).
static void glassQualityReset(){
  uint8_t cap = gGlassQWant;
  if(gEffMode && cap < GLQ_MEDIUM) cap = GLQ_MEDIUM;
  if(gGlassQNow != cap) gGlassGen++;
  gGlassQNow = cap; glStatUs = 0; glWinMs = 0; glGoodWins = 0;
}

// #############################################################
// ##  DEPURACION VISUAL  ·  SOLO EN COMPILACION, nunca en release
// ##  ----------------------------------------------------------
// ##  Se activa definiendo FLEXOS_GLASS_DEBUG a uno de los valores de
// ##  abajo en la linea de compilacion. Sin la macro, glDebug() no
// ##  existe y el compilador no genera ni un byte.
// ##     1 SDF   2 NORMALES   3 REFRACCION   4 FRESNEL   5 COBERTURA
// #############################################################
#ifdef FLEXOS_GLASS_DEBUG
static inline uint16_t glDebug(const GlassPx& o, uint16_t c){
  switch(FLEXOS_GLASS_DEBUG){
    case 1: return rgb565(o.cov, o.cov, o.cov);
    case 2: return rgb565((uint8_t)(128 + (o.ox4 << 3)), (uint8_t)(128 + (o.oy4 << 3)), 128);
    case 3: { int m = glOctLen(o.ox4, o.oy4) << 2; if(m > 255) m = 255;
              return rgb565((uint8_t)m, 0, (uint8_t)(255 - m)); }
    case 4: return rgb565(o.hiA, o.hiA, o.hiA);
    case 5: return mix565(rgb565(255,0,255), c, o.cov);
    default: return c;
  }
}
#define GL_DEBUG_PX(o, c)  glDebug((o), (c))
#else
#define GL_DEBUG_PX(o, c)  (c)
#endif

// #############################################################
// ##  PRECISION NUMERICA  ·  por que no hay ni un double aqui
// ##  ----------------------------------------------------------
// ##  El P4 tiene FPU de simple precision. Un double se emula por
// ##  software y cuesta cientos de ciclos; un float costaria pocos,
// ##  pero la conversion float<->entero en un bucle que ya trabaja con
// ##  RGB565 empaquetado cuesta mas que la aritmetica misma.
// ##
// ##  Por eso todo el material es entero:
// ##    · Q4 para geometria y desplazamientos (1/16 px es medio orden de
// ##      magnitud mas fino que el paso visible sobre un fondo borroso),
// ##    · Q8 para normales, alphas y factores (es el dominio en el que
// ##      mix565 ya trabaja, asi que no hay conversion),
// ##    · isqrt32 -- que ya existia en el motor grafico -- para la unica
// ##      raiz real, y solo en las esquinas,
// ##    · tablas para exp() y para 1/n,
// ##    · reciprocos de 20 bits para las dos divisiones que quedaban
// ##      (edgeFactor y la normal de esquina), igual que ya hacia el
// ##      box-blur con glbRecip.
// ##
// ##  No se sustituyo ninguna funcion "a ciegas": las que se tabularon
// ##  son las que se ejecutan por pixel. pow() se resolvio por cuadrados
// ##  porque el exponente es entero y pequeno, no porque pow sea lento.
// #############################################################
