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
// ##    blur      radio del box-blur del fondo, en pixeles. BAJO A PROPOSITO
// ##              con material avanzado: el desenfoque de radio 6 que tenia
// ##              el material clasico borra el detalle fino ANTES de poder
// ##              doblarlo, y entonces la refraccion no se ve porque no hay
// ##              nada que refractar. Un radio 3 deja estructura -- lineas,
// ##              texto grande, iconos -- que SI se puede doblar, y el
// ##              material sigue leyendose como vidrio y no como una
// ##              ventana. El perfil BAJA conserva el 6 de siempre.
// ##    interior  refraccion del INTERIOR, en 1/16 de pixel MEDIDA EN EL BORDE
// ##              del panel. Es el campo que hace que el material sea una
// ##              lente y no un bisel: sin el, el 87 % de los pixeles de un
// ##              panel tenian desplazamiento exactamente cero y lo que
// ##              habia detras salia intacto.
// ##    waveAmp   amplitud del frente de onda de un impacto (1/16 px).
// ##    waveSpd   velocidad del frente, en pixeles por cada 16 ms.
// ##    velAmp    cuanto suma la velocidad del dedo, al tope (1/16 px).
// ##    trail     amplitud de los impactos que siembra el arrastre; 0 = sin
// ##              estela.
// #############################################################
struct FlexGlassCfg {
  uint8_t band;
  uint8_t blur;          // NUEVO: radio del box-blur del fondo
  uint8_t interior;      // NUEVO: refraccion del INTERIOR del panel (1/16 px en su borde)
  uint8_t refract;
  uint8_t chroma;
  uint8_t fresnel;
  uint8_t fresPow;
  uint8_t highlight;
  uint8_t irid;
  uint8_t touchAmp;
  uint8_t touchR;
  uint8_t waveAmp;       // NUEVO: amplitud de la onda que se propaga (1/16 px)
  uint8_t waveSpd;       // NUEVO: velocidad del frente (px por 16 ms)
  uint8_t velAmp;        // NUEVO: cuanto suma la velocidad del dedo (1/16 px al tope)
  uint8_t trail;         // NUEVO: amplitud de los impactos de estela (0 = sin estela)
};

// WATER va POR ENCIMA de ULTRA (indice 0 = mejor). ULTRA se quedaba en 4 px
// de desvio a proposito -- "mas que esto ya no parece vidrio curvado sino
// agua", decia el comentario de abajo -- y agua es exactamente lo que se
// pide ahora: el objetivo es acercarse al Liquid Glass de la referencia, no
// al bisel discreto. ULTRA y los demas NO se tocan, asi que quien estuviera
// conforme con el material anterior lo tiene intacto un escalon mas abajo.
#define GLQ_WATER   0
#define GLQ_ULTRA   1
#define GLQ_HIGH    2
#define GLQ_MEDIUM  3
#define GLQ_LOW     4
#define GLQ_N       5

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
//
// WATER rompe ese techo a proposito: refract 190/16 = 11,9 px (3,4 veces
// ULTRA) sobre una banda de 24 px (1,7 veces). Es la magnitud que hace falta
// para que el fondo se vea DOBLADO y no solo mordido en el canto, que es la
// diferencia que se nota al lado de la referencia. El resto de sus valores
// suben en la misma proporcion para que el material siga siendo coherente:
// un canto que desvia 12 px con el Fresnel de ULTRA se veria despegado.
static const FlexGlassCfg kGlassQ[GLQ_N] = {
  //              band blur  int refr chr fres pow  hi irid tAmp  tR wAmp wSpd vAmp trail
  /* WATER  */ {   24,   6, 110, 190, 18,  65,  4,  55,  14, 140,  80,  85,   6,  55,  35 },
  /* ULTRA  */ {   14,   3,  48,  64, 12,  52,  4,  46,  14,  72,  60,  40,   6,  26,  22 },
  /* HIGH   */ {   11,   3,  40,  56,  8,  46,  4,  42,  10,  62,  54,  32,   6,  20,  18 },
  /* MEDIUM */ {    8,   4,  28,  44,  0,  38,  3,  34,   0,  46,  46,  20,   5,  14,   0 },
  /* LOW    */ {    0,   6,   0,   0,  0,   0,  0,   0,   0,   0,   0,   0,   0,   0,   0 },
};

// Perfil elegido por el usuario (techo) y perfil EFECTIVO (lo que se dibuja).
// El efectivo nunca sube por encima del techo; solo puede bajar, y lo baja la
// calidad adaptativa o el modo visual eficiente.
static uint8_t gGlassQWant = GLQ_WATER;
static uint8_t gGlassQNow  = GLQ_WATER;
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

// CURVA DEL CANTO CON HOMBRO. Convierte edgeFactor (0 en el centro, 255 en
// el canto) en la fraccion de desplazamiento que toca aplicar.
//
// Antes esto era ef^2, que concentra TODA la curvatura en el ultimo tercio:
// a media banda queda el 25 % del desvio, asi que por mucho que se suba
// 'refract' la deformacion se lee como una linea fina pegada al borde y no
// como una lente. Subir refract con ef^2 no ensancha nada -- solo hace la
// misma linea mas violenta.
//
// El hombro mantiene el desvio alto durante un tramo ANCHO y luego lo suelta
// poco a poco hacia dentro. A media banda da el 62 % en vez del 25 %. Es lo
// que hace que se vea un volumen de vidrio y no un bisel.
//
// 17 entradas para que el indice sea ef8>>4 (0..15) mas la centinela del
// redondeo: un desplazamiento, ni una division. 17 bytes en FLASH.
static const uint8_t kGlShoulder[17] = {
    0,  14,  34,  60,  90, 120, 148, 172,
  192, 208, 221, 231, 239, 245, 250, 253, 255
};

// Perfil de UN frente de onda. Indice 0..64 = el lobulo completo, con el
// frente justo en el centro (k=32). Es un solo ciclo -- compresion y despues
// rarefaccion -- y no un tren de senos: un tren se lee como agua, y esto
// tiene que leerse como un impacto en un material solido. Generado con
// sin(pi*t)*exp(-2.2*t^2) y normalizado a +-127.
static const int8_t kGlWave[65] = {
     0,   -3,   -6,   -9,  -13,  -19,  -25,  -31,  -38,  -47,  -55,  -64,  -74,
   -83,  -92, -100, -108, -115, -121, -126, -127, -127, -126, -121, -115, -108,
   -96,  -84,  -69,  -53,  -37,  -18,    0,   18,   37,   53,   69,   84,   96,
   108,  115,  121,  126,  127,  127,  126,  121,  115,  108,  100,   92,   83,
    74,   64,   55,   47,   38,   31,   25,   19,   13,    9,    6,    3,    0
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
// ##  INTERACCION  ·  una maquina de estados, no tres variables
// ##  ----------------------------------------------------------
// ##  Ciclo completo, y ninguno de sus estados se queda atascado:
// ##
// ##      IDLE -> DOWN -> ACTIVE -> MOVING -> RELEASE -> DECAY -> IDLE
// ##
// ##  El manejador tactil SOLO escribe aqui. No captura pantalla, no
// ##  desenfoca, no calcula SDF y no lanza ningun hilo: eso volveria a
// ##  meter el coste del efecto dentro de la ruta del tacto, que es
// ##  exactamente donde no puede estar. Quien interpola es el
// ##  compositor, en el cuadro que ya iba a dibujar.
// ##
// ##  Cuatro cosas deforman, y las cuatro salen de este estado:
// ##    1. PRESION    -- hundimiento bajo el dedo mientras esta apoyado.
// ##    2. ONDA       -- frentes que se expanden desde cada impacto.
// ##    3. VELOCIDAD  -- el arrastre estira el material en su direccion.
// ##    4. ESTELA     -- impactos pequenos sembrados por el recorrido.
// ##
// ##  Los impactos viven en un array FIJO de GLI_MAX. No hay lista
// ##  dinamica, no hay malloc por toque y un gesto rapido no puede
// ##  hacer crecer nada: el hueco mas viejo se reutiliza.
// #############################################################
#define GLI_MAX        3        // impactos simultaneos (el mas viejo se recicla)
#define GL_PRESS_MS    90       // subida de la presion al apoyar
#define GL_DECAY_MS    260      // recuperacion despues de soltar
#define GL_WAVE_MS     520      // vida de un frente de onda
#define GL_TRAIL_STEP  22       // pixeles de recorrido entre dos impactos de estela
#define GL_VEL_MAX     64       // tope de px por muestra que se acepta como velocidad

struct GlassImpact {
  int16_t  x, y;
  uint32_t t0;
  uint8_t  amp;                 // 0..255; los de estela nacen mas flojos
};
static GlassImpact glImp[GLI_MAX];
static uint8_t     glImpNext = 0;

static int      glTx = 0, glTy = 0;          // dedo, en coordenadas de pantalla
static int      glVx = 0, glVy = 0;          // velocidad suavizada (px por muestra)
static int      glSeedX = 0, glSeedY = 0;    // ultimo punto donde se sembro estela
static bool     glDown = false;              // dedo apoyado
static uint32_t glDownMs = 0;                // millis del contacto
static uint32_t glUpMs = 0;                  // millis del release (0 = no hubo)

// Rectangulo que la interaccion puede estar tocando ESTE cuadro, y el del
// cuadro anterior: quien fuerce un repintado necesita los dos, porque la cola
// de la onda tiene que borrarse de donde estaba.
static int glDirty0X = 0, glDirty0Y = 0, glDirty1X = -1, glDirty1Y = -1;
static int glPrevDX0 = 0, glPrevDY0 = 0, glPrevDX1 = -1, glPrevDY1 = -1;

// ---- ciclo de vida -------------------------------------------------------
// Alcance de un impacto AHORA: el frente ha viajado waveSpd px cada 16 ms y
// el lobulo de la onda ocupa otro tanto por delante.
static inline int glImpReach(const FlexGlassCfg& c, uint32_t age){
  return (int)((age * c.waveSpd) >> 4) + 2 * c.touchR;
}
// Amplitud de un impacto AHORA. Cae con el cuadrado del tiempo: rapido al
// principio y con una cola suave, que es como muere una onda real.
static inline int glImpAmp(const GlassImpact& im, uint32_t now){
  if(!im.amp) return 0;
  uint32_t age = now - im.t0;
  if(age >= GL_WAVE_MS) return 0;
  uint32_t k = 255u - (255u * age) / GL_WAVE_MS;
  return (int)(((k * k) >> 8) * im.amp) >> 8;
}
// Presion bajo el dedo: sube al apoyar, se mantiene, y decae al soltar.
// Es la unica parte que distingue DOWN de RELEASE.
static uint8_t glPressure(){
  uint32_t now = millis();
  if(glDown){
    uint32_t e = now - glDownMs;
    return (e < GL_PRESS_MS) ? (uint8_t)(255u * e / GL_PRESS_MS) : 255;
  }
  if(!glUpMs) return 0;
  uint32_t e = now - glUpMs;
  if(e >= GL_DECAY_MS){ glUpMs = 0; return 0; }
  uint32_t k = 255u - (255u * e) / GL_DECAY_MS;
  return (uint8_t)((k * k) >> 8);            // retorno suave, no lineal
}
// Hay algo vivo. Es la primera pregunta de todo compositor, asi que ademas
// CADUCA lo que ya no vale: sin esto, el estado seguiria "vivo" para quien
// solo preguntase, y la interaccion nunca volveria a cero.
static bool glassTouchLive(){
  uint32_t now = millis();
  bool live = glDown || glPressure() != 0;
  for(int k = 0; k < GLI_MAX; k++){
    if(!glImp[k].amp) continue;
    if(now - glImp[k].t0 >= GL_WAVE_MS) glImp[k].amp = 0;
    else live = true;
  }
  if(!live){ glVx = glVy = 0; }
  return live;
}

// Siembra un impacto. Reutiliza el hueco mas viejo: el array es fijo y un
// gesto rapido no puede hacer crecer nada.
static void glImpAdd(int x, int y, uint8_t amp){
  if(!amp) return;
  uint32_t now = millis();
  int slot = -1, oldest = 0;
  for(int k = 0; k < GLI_MAX; k++){
    if(!glImp[k].amp || now - glImp[k].t0 >= GL_WAVE_MS){ slot = k; break; }
    uint32_t age = now - glImp[k].t0;
    if((int)age > oldest){ oldest = (int)age; slot = k; }
  }
  if(slot < 0) slot = glImpNext;
  glImpNext = (uint8_t)((slot + 1) % GLI_MAX);
  glImp[slot].x = (int16_t)x; glImp[slot].y = (int16_t)y;
  glImp[slot].t0 = now; glImp[slot].amp = amp;
}

// ---- lo que llama el tactil: tres funciones, ninguna cara ----------------
static void glassTouchDown(int x, int y){
  if(!glassAdvanced()) return;
  const FlexGlassCfg& c = GLC();
  glTx = glSeedX = x; glTy = glSeedY = y;
  glVx = glVy = 0;
  glDown = true; glDownMs = millis(); glUpMs = 0;
  glImpAdd(x, y, c.waveAmp ? 255 : 0);        // el impacto del contacto, a tope
}
static void glassTouchMove(int x, int y){
  if(!glDown) return;
  const FlexGlassCfg& c = GLC();
  int dx = x - glTx, dy = y - glTy;
  // ACOTADO EN LA ENTRADA, no mas tarde. Un salto de cientos de pixeles
  // entre dos muestras no es un gesto: es el dedo reapareciendo en otro
  // sitio, o una lectura suelta del GT911. Dejarlo entrar obligaba a
  // "arreglarlo" luego al normalizar, y ahi el reciproco tabulado se queda
  // corto y el vector sale mas largo que la unidad -- o sea un tiron, justo
  // lo que se queria evitar. 64 px por muestra ya son ~3.800 px/s.
  if(dx >  GL_VEL_MAX) dx =  GL_VEL_MAX;
  if(dx < -GL_VEL_MAX) dx = -GL_VEL_MAX;
  if(dy >  GL_VEL_MAX) dy =  GL_VEL_MAX;
  if(dy < -GL_VEL_MAX) dy = -GL_VEL_MAX;
  // Velocidad suavizada: media movil de 1/4. Sin suavizar, una sola muestra
  // ruidosa del GT911 daria un tiron en la deformacion.
  glVx += (dx - glVx) >> 2;
  glVy += (dy - glVy) >> 2;
  glTx = x; glTy = y;
  // ESTELA. Se siembra por DISTANCIA RECORRIDA, no por tiempo ni por muestra:
  // asi un arrastre lento no siembra nada y uno rapido siembra a intervalos
  // regulares en el espacio, que es lo que se ve como una estela y no como
  // una racha de impactos amontonados.
  if(c.trail){
    int sx = x - glSeedX, sy = y - glSeedY;
    if(glOctLen(sx, sy) >= GL_TRAIL_STEP){
      int sp = glOctLen(glVx, glVy);           // px por muestra
      if(sp > 24) sp = 24;
      glImpAdd(x, y, (uint8_t)((c.trail * sp) / 24));
      glSeedX = x; glSeedY = y;
    }
  }
}
static void glassTouchUp(){
  if(!glDown) return;
  glDown = false; glUpMs = millis();
  // Al soltar, el material rebota: un impacto suave en el punto de salida.
  // Es lo que hace que soltar se vea como soltar y no como un corte.
  const FlexGlassCfg& c = GLC();
  if(c.waveAmp) glImpAdd(glTx, glTy, 150);
}
// Corta la interaccion en seco y sin residuos. La llama todo cambio de
// pantalla: una onda de la pantalla anterior no puede seguir viva en la
// siguiente.
static void glassTouchReset(){
  glDown = false; glUpMs = 0; glVx = glVy = 0;
  for(int k = 0; k < GLI_MAX; k++) glImp[k].amp = 0;
  glDirty1X = -1; glDirty1Y = -1;
  glPrevDX1 = -1; glPrevDY1 = -1;
}

// #############################################################
// ##  REGION SUCIA DE LA INTERACCION
// ##  ----------------------------------------------------------
// ##  La union de lo que la interaccion puede estar tocando este
// ##  cuadro y lo que tocaba el anterior. Los dos hacen falta: sin el
// ##  anterior, la cola de la onda se quedaria pintada donde estuvo.
// ##
// ##  Esto es lo que permite animar SIN repintar 480x800. Un impacto
// ##  recien nacido ocupa unos 120 px de lado; uno a punto de morir
// ##  puede llegar a la pantalla entera, pero para entonces su
// ##  amplitud ya es casi cero y glassDirtyRect deja de contarlo.
// #############################################################
// CONSULTA PURA: la caja que la interaccion toca AHORA. Sin efectos
// secundarios, asi que puede llamarse tantas veces por cuadro como haga
// falta -- y se llama una vez por tarjeta de vidrio cacheada, para saber si
// esa tarjeta necesita componerse en vivo.
static bool glassIaBox(int& x0, int& y0, int& x1, int& y1){
  const FlexGlassCfg& c = GLC();
  uint32_t now = millis();
  int a0 = 0x7FFF, b0 = 0x7FFF, a1 = -1, b1 = -1;
  auto add = [&](int cx, int cy, int r){
    if(cx - r < a0) a0 = cx - r;
    if(cy - r < b0) b0 = cy - r;
    if(cx + r > a1) a1 = cx + r;
    if(cy + r > b1) b1 = cy + r;
  };
  if(glDown || glPressure()) add(glTx, glTy, c.touchR + (glOctLen(glVx, glVy) << 1));
  for(int k = 0; k < GLI_MAX; k++){
    if(!glImp[k].amp) continue;
    uint32_t age = now - glImp[k].t0;
    if(age >= GL_WAVE_MS) continue;
    if(glImpAmp(glImp[k], now) < 4) continue;   // ya no mueve un pixel: no ensucia
    add(glImp[k].x, glImp[k].y, glImpReach(c, age));
  }
  if(a1 < a0 || b1 < b0) return false;
  x0 = a0; y0 = b0; x1 = a1; y1 = b1;
  return true;
}

// LA DE CADA CUADRO: la caja de arriba UNIDA con la del cuadro anterior, y
// recortada a la pantalla. Tiene efecto secundario -- hace avanzar el
// historial -- asi que la llama UNA sola vez por cuadro quien dirige el
// repintado, nunca un compositor.
static bool glassDirtyRect(int& x0, int& y0, int& x1, int& y1){
  int a0, b0, a1, b1;
  bool any = glassIaBox(a0, b0, a1, b1);
  if(!any){ a0 = 0x7FFF; b0 = 0x7FFF; a1 = -1; b1 = -1; }
  glPrevDX0 = glDirty0X; glPrevDY0 = glDirty0Y;
  glPrevDX1 = glDirty1X; glPrevDY1 = glDirty1Y;
  glDirty0X = a0; glDirty0Y = b0; glDirty1X = a1; glDirty1Y = b1;
  // Union con el cuadro anterior, para que la cola de la onda se borre de
  // donde estuvo en vez de quedarse pintada.
  if(glPrevDX1 >= glPrevDX0){
    if(glPrevDX0 < a0) a0 = glPrevDX0;
    if(glPrevDY0 < b0) b0 = glPrevDY0;
    if(glPrevDX1 > a1) a1 = glPrevDX1;
    if(glPrevDY1 > b1) b1 = glPrevDY1;
  }
  if(a1 < a0 || b1 < b0) return false;
  if(a0 < 0) a0 = 0;
  if(b0 < 0) b0 = 0;
  if(a1 > SCR_W - 1) a1 = SCR_W - 1;
  if(b1 > SCR_H - 1) b1 = SCR_H - 1;
  x0 = a0; y0 = b0; x1 = a1; y1 = b1;
  return true;
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
  // --- campo interior: la parte que convierte el bisel en una LENTE ---
  // kxIn/kyIn son el desplazamiento por unidad normalizada, ya en Q4 y ya
  // divididos por el semieje: el pixel solo hace un producto y un
  // desplazamiento, sin division y sin tocar el SDF.
  int kxIn, kyIn;
  int halfW, halfH;
  // --- material, copiado del perfil una sola vez por panel ---
  int refract, chroma, fresnel, fresPow, highlight, irid;
  // COLOR DEL REALCE, TABULADO POR DIRECCION DE LA NORMAL.
  // La iridiscencia solo depende de nx, asi que resolverla por pixel era
  // pagar dos mezclas para obtener uno de diecisiete colores. Se resuelve
  // una vez por panel. 17 entradas son un paso de 1/8 de la normal: por
  // debajo del escalon de color de RGB565, o sea invisible.
  uint16_t hiLut[17];
  // --- interaccion, resuelta a coordenadas del PANEL una vez por panel ---
  // Es una copia local a proposito: el bucle de pixeles no vuelve a mirar el
  // estado global ni a llamar a millis(), asi que un panel se compone entero
  // con UN instante de tiempo y no puede salir con la onda a medio avanzar
  // entre su primera fila y la ultima.
  bool iaOn;                     // hay algo interactivo que afecte a ESTE panel
  int  prX, prY, prAmp, prR;     // presion: centro, amplitud (Q4) y radio
  int  vdx4, vdy4;               // estiramiento por velocidad (Q4, constante)
  int  nImp;
  int  impX[GLI_MAX], impY[GLI_MAX];
  int  impFront[GLI_MAX];        // radio del frente AHORA, en pixeles
  int  impAmp[GLI_MAX];          // amplitud AHORA (Q4)
  int  impLobe[GLI_MAX];         // medio ancho del lobulo, en pixeles
  int  impRecip[GLI_MAX];        // 2^16 / lobe, para no dividir por pixel
  int  iaX0, iaY0, iaX1, iaY1;   // caja de todo lo interactivo, en el panel
  // --- fila actual (lo pone glEdgeRow) ---
  int qy4, sgnY, row;
  bool rowAll;           // esta fila es banda de lado a lado (canto recto)
  bool rowCap;           // esta fila cruza un arco de esquina
  bool iaRow;            // algo interactivo alcanza esta fila
  int  ix0, ix1;         // columnas que alcanza lo interactivo en esta fila
  int  rowOy4;           // desplazamiento interior VERTICAL de la fila (Q4)
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

// #############################################################
// ##  EL CAMPO INTERIOR, TABULADO POR COLUMNA
// ##  ----------------------------------------------------------
// ##  El desplazamiento horizontal interior depende SOLO de la columna,
// ##  no de la fila. Resolverlo por pixel costaba una division entera
// ##  -- lo mas caro que hay en el RISC-V del P4, y justo lo que el
// ##  resto del motor evita con reciprocos tabulados --, repetida en
// ##  los ~400 pixeles de cada una de las ~200 filas para obtener 400
// ##  valores distintos que se repiten fila tras fila.
// ##
// ##  Se calcula UNA vez por panel, en glEdgeBegin -- despues de
// ##  resolver la interaccion, porque el estiramiento por velocidad del
// ##  dedo tambien se hornea aqui -- y el pixel se queda en una lectura
// ##  de tabla. Cuesta 960 B de RAM interna.
// ##  Se guarda la coordenada de origen ABSOLUTA ya en Q4, no el
// ##  desplazamiento, para ahorrar tambien la suma.
// #############################################################
// INVARIANTE: las dos tablas de abajo son de UN SOLO panel a la vez. Se
// llenan en glEdgeBegin y se leen hasta que termina la composicion de ese
// panel. Eso vale porque ningun compositor de vidrio se llama a si mismo ni
// llama a otro a mitad de una fila: drawGlassCardFlat decide ANTES si va por
// la cache o por el panel en vivo, y glcBuild compone su tarjeta entera
// antes de que nadie la use. Si alguna vez hiciera falta anidar, estas dos
// tendrian que pasar a ser locales del panel.
static int16_t glIntSx4[SCR_W];                  // (i<<4) + ox4(i), por columna
// Fila de origen con las dos filas vecinas YA mezcladas. Convierte el
// muestreo bilineal del tramo interior en uno lineal: 2 lecturas y 1 mezcla
// por pixel en vez de 4 y 3. Otros 960 B de RAM interna.
static uint16_t glIntRowBuf[SCR_W];

static void glIntBuildLut(GlassEdge& e){
  const int n = e.w < SCR_W ? e.w : SCR_W;
  // Reciproco de 16 bits en vez de la division: t = dx*256/halfW exacto
  // salvo el redondeo, que aqui no se ve porque alimenta un perfil suave.
  const int recip = 65536 / e.halfW;
  for(int i = 0; i < n; i++){
    int t = ((i - e.halfW) * recip) >> 8;
    if(t > 256) t = 256;
    if(t < -256) t = -256;
    int shape = (t * (t < 0 ? -t : t)) >> 8;     // -256..256
    glIntSx4[i] = (int16_t)((i << 4) - ((e.kxIn * shape) >> 8) + e.vdx4);
  }
}
// Coordenada de origen (Q4) de la columna i en el tramo interior.
static inline int glIntSx(int i){ return glIntSx4[i]; }

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
  e.bandPx = band;
  e.capPx  = rad + band;
  if(e.bandPx > w / 2) e.bandPx = (w + 1) / 2;
  if(e.capPx  > w / 2) e.capPx  = (w + 1) / 2;
  e.refract = c.refract; e.chroma = c.chroma;
  e.fresnel = c.fresnel; e.fresPow = c.fresPow;
  e.highlight = c.highlight; e.irid = c.irid;

  // ---- CAMPO INTERIOR: lo que hace que esto sea una lente ----------------
  // El material clasico solo deformaba la banda de borde, asi que el 87 % de
  // los pixeles de un panel salian con desplazamiento EXACTAMENTE cero y lo
  // que hubiera detras -- una imagen, un texto, un icono -- aparecia intacto.
  //
  // Ahora todo el panel refracta. El campo es radial desde el centro y crece
  // con el CUADRADO de la distancia normalizada: casi nada en el centro,
  // visible a media altura, y su maximo justo donde empieza la banda, que es
  // donde el termino del SDF toma el relevo. Por eso las dos partes empalman
  // sin costura: en el centro vale 0 y en el borde vale 'interior'.
  e.halfW = w / 2; if(e.halfW < 1) e.halfW = 1;
  e.halfH = h / 2; if(e.halfH < 1) e.halfH = 1;
  // La normalizacion a Q8 ya la hace la propia columna/fila (ver glIntOx4),
  // asi que aqui la constante es directamente la amplitud del perfil.
  e.kxIn = c.interior;
  e.kyIn = c.interior;

  // ---- INTERACCION: se resuelve UNA vez por panel -------------------------
  e.iaOn = false; e.nImp = 0; e.prAmp = 0; e.vdx4 = e.vdy4 = 0;
  e.prX = e.prY = 0; e.prR = c.touchR;
  e.iaX0 = 0x7FFF; e.iaY0 = 0x7FFF; e.iaX1 = -1; e.iaY1 = -1;
  if(!gGlassNoTouch && glassTouchLive()){
    const uint32_t now = millis();
    auto box = [&](int cx, int cy, int r){
      if(cx - r < e.iaX0) e.iaX0 = cx - r;
      if(cy - r < e.iaY0) e.iaY0 = cy - r;
      if(cx + r > e.iaX1) e.iaX1 = cx + r;
      if(cy + r > e.iaY1) e.iaY1 = cy + r;
    };
    // 1. PRESION bajo el dedo.
    int pr = glPressure();
    if(pr && c.touchAmp){
      int lx = glTx - panelX, ly = glTy - panelY;
      if(lx > -c.touchR && lx < w + c.touchR && ly > -c.touchR && ly < h + c.touchR){
        e.prX = lx; e.prY = ly;
        e.prAmp = (pr * (int)c.touchAmp) >> 8;
        box(lx, ly, c.touchR);
      }
    }
    // 2. VELOCIDAD del arrastre. Estira el material en la direccion del
    //    movimiento, con tope: una muestra rapida no puede dar un tiron.
    if(pr && c.velAmp){
      int sp = glOctLen(glVx, glVy);               // <= 96 por el tope de entrada
      if(sp > 0){
        int spc = sp > 24 ? 24 : sp;               // saturado: mas rapido no deforma mas
        int mag = ((int)c.velAmp * spc) / 24;      // Q4, tope exacto = velAmp
        int inv = kGlRecip[sp > 80 ? 80 : sp];     // ~ 4096/sp
        int ux8 = (glVx * inv) >> 4;               // componente unitaria en Q8
        int uy8 = (glVy * inv) >> 4;
        if(ux8 >  256) ux8 =  256;                 // el octogono se pasa hasta un 6 %
        if(ux8 < -256) ux8 = -256;
        if(uy8 >  256) uy8 =  256;
        if(uy8 < -256) uy8 = -256;
        e.vdx4 = -((ux8 * mag) >> 8);              // en contra del movimiento
        e.vdy4 = -((uy8 * mag) >> 8);
      }
    }
    // 3. ONDAS. Cada impacto vivo aporta un frente que se expande.
    for(int k = 0; k < GLI_MAX && e.nImp < GLI_MAX; k++){
      if(!glImp[k].amp) continue;
      uint32_t age = now - glImp[k].t0;
      if(age >= GL_WAVE_MS) continue;
      int amp = glImpAmp(glImp[k], now);
      if(amp < 4) continue;                        // no llega ni a un pixel
      int lx = glImp[k].x - panelX, ly = glImp[k].y - panelY;
      int front = (int)((age * c.waveSpd) >> 4);
      int lobe  = c.touchR;                        // medio ancho del lobulo
      if(lobe < 4) lobe = 4;
      int reach = front + lobe;
      if(lx + reach < 0 || lx - reach > w || ly + reach < 0 || ly - reach > h) continue;
      int q = e.nImp++;
      e.impX[q] = lx; e.impY[q] = ly;
      e.impFront[q] = front;
      e.impAmp[q] = (amp * (int)c.waveAmp) >> 8;   // Q4
      e.impLobe[q] = lobe;
      e.impRecip[q] = 65536 / lobe;                // 2^16/lobe: sin division por pixel
      box(lx, ly, reach);
    }
    // OJO: la velocidad NO entra en esta caja. Es un campo UNIFORME -- el
    // mismo desplazamiento en todo el panel -- asi que se hornea en la tabla
    // del campo interior (ver glIntBuildLut) y en el desplazamiento vertical
    // de la fila. Contarla aqui obligaria a tratar el panel ENTERO por la
    // ruta lenta durante todo un arrastre, que es justo el momento en el que
    // menos se puede pagar: un scroll con seis tarjetas de vidrio pasaria de
    // ~20 % de pixeles caros al 100 %. Horneada en la tabla cuesta cero.
    if(e.prAmp || e.nImp){
      e.iaOn = true;
      if(e.iaX0 < 0) e.iaX0 = 0;
      if(e.iaY0 < 0) e.iaY0 = 0;
      if(e.iaX1 > w - 1) e.iaX1 = w - 1;
      if(e.iaY1 > h - 1) e.iaY1 = h - 1;
      if(e.iaX1 < e.iaX0 || e.iaY1 < e.iaY0) e.iaOn = false;
    }
  }
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
  glIntBuildLut(e);
  return true;
}

// Constantes de la fila. Se llama UNA vez por fila, nunca por pixel.
static inline void glEdgeRow(GlassEdge& e, int j){
  int py4 = (j << 4) + 8;
  int d   = py4 - e.cy4;
  e.sgnY  = (d >= 0) ? 1 : -1;
  e.qy4   = (d >= 0 ? d : -d) - e.by4;
  e.rowAll = (j < e.bandPx) || (j >= e.h - e.bandPx);        // canto recto de arriba/abajo
  e.rowCap = (j < e.capPx)  || (j >= e.h - e.capPx);         // la fila cruza un arco
  e.row = j;
  // CAMPO INTERIOR, componente vertical: solo depende de la fila, asi que se
  // resuelve aqui una vez. v es la separacion normalizada al centro; el
  // desplazamiento crece con su cuadrado (suave en el centro, maximo en el
  // borde) y apunta HACIA DENTRO, igual que el termino del SDF.
  {
    int dy = j - e.halfH;                        // pixeles desde el centro
    int t  = (dy * 256) / e.halfH;               // Q8 normalizado, -256..256
    if(t > 256) t = 256;
    if(t < -256) t = -256;
    // perfil v*|v| normalizado a Q8, escalado por la amplitud: 0 en el
    // centro y exactamente 'interior' en el borde del panel.
    int shape = (t * (t < 0 ? -t : t)) >> 8;     // -256..256
    // El estiramiento por velocidad del dedo se suma AQUI, no por pixel:
    // es el mismo valor en toda la fila y en todas las filas.
    e.rowOy4 = -((e.kyIn * shape) >> 8) + e.vdy4;
  }
  // Franja interactiva DENTRO de esta fila. Sin interaccion, o con ella lejos
  // de la fila, queda vacia y el tramo central se resuelve por la via rapida.
  e.iaRow = false;
  if(e.iaOn && j >= e.iaY0 && j <= e.iaY1){
    e.iaRow = true; e.ix0 = e.iaX0; e.ix1 = e.iaX1;
  }
}


// #############################################################
// ##  LO QUE APORTA LA INTERACCION EN UN PIXEL
// ##  ----------------------------------------------------------
// ##  Presion + onda + velocidad, sumadas al desplazamiento que el
// ##  material ya iba a aplicar. No es una capa nueva ni una pasada
// ##  nueva: son dos enteros mas en el mismo muestreo.
// ##
// ##  La onda es lo que la hace PROPAGARSE: el frente esta a
// ##  impFront pixeles del impacto y avanza con el tiempo, asi que un
// ##  pixel fijo ve pasar el lobulo -- primero compresion, despues
// ##  rarefaccion -- y vuelve a quedarse quieto. Eso es una onda que
// ##  viaja, no un circulo que crece de brillo.
// #############################################################
static inline void glInteract(const GlassEdge& e, int i, int j, int& ox4, int& oy4){
  // La VELOCIDAD no esta aqui: es un campo uniforme y va horneada en la
  // tabla del campo interior, asi que no cuesta ni una suma por pixel.
  // 1. PRESION: hundimiento radial bajo el dedo.
  if(e.prAmp){
    int dx = i - e.prX, dy = j - e.prY;
    int len = glOctLen(dx, dy);
    if(len < e.prR){
      int k = (len << 5) / e.prR;                 // 0..31
      if(k > 31) k = 31;
      int amp = (e.prAmp * kGlFall[k]) >> 8;
      if(amp){
        int inv = kGlRecip[len > 80 ? 80 : len];
        ox4 += (dx * inv * amp) >> 12;
        oy4 += (dy * inv * amp) >> 12;
      }
    }
  }
  // 2. ONDAS.
  for(int q = 0; q < e.nImp; q++){
    int dx = i - e.impX[q], dy = j - e.impY[q];
    int len = glOctLen(dx, dy);
    int rel = len - e.impFront[q];                // distancia AL FRENTE
    if(rel <= -e.impLobe[q] || rel >= e.impLobe[q]) continue;   // fuera del lobulo
    // rel en [-lobe, lobe] -> indice 0..64 de kGlWave
    int idx = 32 + ((rel * e.impRecip[q]) >> 11);
    if(idx < 0) idx = 0;
    if(idx > 64) idx = 64;
    int wv = kGlWave[idx];
    if(!wv) continue;
    int amp = (e.impAmp[q] * wv) >> 7;            // Q4, con signo
    if(!amp) continue;
    if(len == 0) continue;                        // en el epicentro no hay direccion
    int inv = kGlRecip[len > 80 ? 80 : len];
    ox4 += (dx * inv * amp) >> 12;
    oy4 += (dy * inv * amp) >> 12;
  }
}

// Direccion de la luz: arriba-izquierda, la misma que el sistema ya usaba en
// GLASS_CORNER_STRONG/WEAK. En Q8 normalizada: (-181, -181) ~= (-0,707, -0,707).
#define GL_LX  (-181)
#define GL_LY  (-181)

// #############################################################
// ##  EL TRAMO CENTRAL DE LA FILA  ·  la via rapida
// ##  ----------------------------------------------------------
// ##  Aqui es donde este efecto deja de costar el AREA del panel y
// ##  pasa a costar su PERIMETRO. Devuelve el intervalo [c0,c1] en el
// ##  que no hay banda de borde ni interaccion.
// ##
// ##  OJO: "centro" ya NO significa "sin efecto". El campo interior SI
// ##  se aplica ahi -- es lo que hace que una imagen o un texto detras
// ##  del panel se doblen en vez de salir intactos --, pero se resuelve
// ##  con un producto por pixel y sin SDF, sin normal, sin Fresnel, sin
// ##  aberracion y sin cobertura. Es el tramo barato, no el tramo
// ##  muerto.
// ##
// ##  Devuelve false si la fila no tiene centro: filas de las tapas,
// ##  paneles estrechos, o una fila que la interaccion cruza entera.
// #############################################################
static inline bool glEdgeCenterSpan(const GlassEdge& e, int& c0, int& c1){
  if(e.rowAll) return false;
  const int ext = e.rowCap ? e.capPx : e.bandPx;
  c0 = ext; c1 = e.w - 1 - ext;
  if(c0 > c1) return false;
  if(!e.iaRow) return true;
  // Con la interaccion cruzando la fila el centro se parte en dos. Se
  // devuelve el trozo mas grande, que es el que de verdad ahorra; el otro
  // cae en la ruta lenta y no pasa nada.
  int la = e.ix0 - c0, lb = c1 - e.ix1;
  if(la <= 0 && lb <= 0) return false;
  if(la >= lb) c1 = e.ix0 - 1;
  else         c0 = e.ix1 + 1;
  return c0 <= c1;
}

// Esta esta columna dentro del alcance de la INTERACCION en esta fila. Es la
// guarda que impide que un toque convierta el panel entero en ruta lenta.
static inline bool glEdgeInIa(const GlassEdge& e, int i){
  return e.iaRow && i >= e.ix0 && i <= e.ix1;
}

// #############################################################
// ##  EL PIXEL  ·  SDF -> normal -> refraccion -> Fresnel -> luz
// ##  ----------------------------------------------------------
// ##  Una sola funcion y una sola pasada. Devuelve false cuando al pixel
// ##  solo le toca el CAMPO INTERIOR: ni banda de borde, ni interaccion.
// ##  El llamante lo resuelve entonces por su via rapida -- que tambien
// ##  refracta, con un producto por pixel. Ese "false" es lo que hace que
// ##  la parte CARA cueste el perimetro y no el area.
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
    if(d4 <= -e.band4 && !glEdgeInIa(e, i)) return false;
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
    if(d4 <= -e.band4 && !glEdgeInIa(e, i)) return false;
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
  // Perfil de la superficie, por la curva con hombro (ver kGlShoulder). Se
  // interpola entre las dos entradas vecinas para que no queden escalones de
  // 1/16 de banda visibles en un canto ancho: dos lecturas de tabla, un
  // producto y un desplazamiento, sin division.
  int curve;
  {
    int kk = ef8 >> 4, fr = ef8 & 15;
    int a = kGlShoulder[kk], b = kGlShoulder[kk + 1];
    curve = a + (((b - a) * fr) >> 4);
  }

  // ---- 4. refraccion = CAMPO INTERIOR + termino de borde ----
  // Los dos empalman sin costura: el interior alcanza su maximo justo donde
  // el del borde arranca desde cero, asi que no hay escalon entre el tramo
  // barato y el caro. Muestrear hacia dentro (y no hacia fuera) es lo que
  // hace que el canto ESTIRE lo que hay detras, como el borde de una lente
  // real; hacia fuera daria el efecto contrario y se leeria como un glitch.
  o.ox4 = glIntSx(i) - (i << 4);        // el campo interior, de la tabla
  o.oy4 = e.rowOy4;
  int mag = (e.refract * curve) >> 8;          // 1/16 px
  o.ox4 -= (nx8 * mag) >> 8;
  o.oy4 -= (ny8 * mag) >> 8;

  // ---- 5. interaccion: presion + onda + velocidad ----
  if(glEdgeInIa(e, i)) glInteract(e, i, e.row, o.ox4, o.oy4);

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

// Prepara glIntRowBuf para la fila 'srow' de un origen COMPACTO: mezcla las
// dos filas vecinas con el peso que pide el desplazamiento interior vertical.
// Una pasada por fila que ahorra la mitad del muestreo en cada pixel.
static void glIntRowFrom(const GlassEdge& e, const uint16_t* sbase, int sstride,
                         int sw, int shc, int srow){
  int fy4 = (srow << 4) + e.rowOy4;
  int iy = fy4 >> 4, ty = (fy4 & 15) << 4;
  if(iy < 0){ iy = 0; ty = 0; }
  if(iy > shc - 1){ iy = shc - 1; ty = 0; }
  const uint16_t* r0 = sbase + (size_t)iy * sstride;
  int n = sw < SCR_W ? sw : SCR_W;
  if(ty == 0){ memcpy(glIntRowBuf, r0, (size_t)n * 2); return; }
  const uint16_t* r1 = sbase + (size_t)((iy + 1 < shc) ? iy + 1 : iy) * sstride;
  for(int i = 0; i < n; i++) glIntRowBuf[i] = mix565(r0[i], r1[i], (uint8_t)ty);
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

// #############################################################
// ##  MUESTREO DEL TRAMO INTERIOR  ·  la fila de origen, ya resuelta
// ##  ----------------------------------------------------------
// ##  En el interior del panel el desplazamiento VERTICAL es constante
// ##  para toda la fila (solo depende de j, ver glEdgeRow). Asi que las
// ##  dos filas de origen y el peso entre ellas se resuelven UNA vez y
// ##  salen del bucle: el pixel se queda en un producto para el
// ##  desplazamiento horizontal, dos lecturas y una mezcla.
// ##
// ##  Esto es lo que hace asequible que TODO el panel refracte. La ruta
// ##  general (glSample) tendria que recalcular la fila en cada pixel
// ##  para un valor que no cambia en los 400 de la fila.
// #############################################################
static inline uint16_t glSampleRow(int w, int fx4){
  int ix = fx4 >> 4, tx = (fx4 & 15) << 4;
  if(ix < 0){ ix = 0; tx = 0; }
  if(ix > w - 1){ ix = w - 1; tx = 0; }
  if(tx == 0) return glIntRowBuf[ix];
  int ix1 = (ix + 1 < w) ? ix + 1 : ix;
  return mix565(glIntRowBuf[ix], glIntRowBuf[ix1], (uint8_t)tx);
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
    // EL SUELO DE LA ESCALERA ES MEDIUM, NO LOW -- y esto arregla un fallo
    // real, no es una preferencia.
    //
    // LOW tiene band=0, o sea glassAdvanced()==false: ahi el material
    // AVANZADO se apaga ENTERO. Sin refraccion, sin Fresnel y, sobre todo,
    // sin toque -- glassTouchDown() se va por su primera linea. Y quien
    // empuja el presupuesto por encima del 22 % es justamente la
    // recomposicion en vivo de un dedo apoyado, asi que la escalera bajaba
    // HIGH -> MEDIUM -> LOW MIENTRAS SE TOCABA y el boton se quedaba
    // congelado en mitad del gesto, con el estado interno avanzando y nada
    // moviendose en pantalla. Era exactamente el sintoma reportado.
    //
    // MEDIUM sigue siendo barato (banda de 8 px, sin cromatica y sin estela)
    // y conserva lo unico que no se puede perder: el desplazamiento real del
    // fondo. LOW se queda para la emergencia de MEMORIA (modo eficiente), que
    // es otra cosa y la decide gEffMode.
    if(gGlassQNow < GLQ_MEDIUM) gGlassQNow++;
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
