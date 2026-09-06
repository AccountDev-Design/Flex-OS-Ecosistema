// #############################################################
// ##  FLEX OS ULTRA  ·  FRAMEWORK DE VENTANAS Y CICLO DE VIDA DE APPS
// ##  ----------------------------------------------------------
// ##  El marco estandar de una app (cabecera, layout responsivo, viewport
// ##  con scroll), las transiciones interrumpibles, la barra de navegacion
// ##  inferior y el ciclo de vida de aplicaciones (AppHooks, APP_REG).
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
#include "FlexOS_Ultra_HomeCfg.h"   // eslabon anterior de la cadena

// #############################################################
// ##  FRAMEWORK DE VENTANAS / APPS  (Milestone 2 - base)
// #############################################################
// Cada app expone dos callbacks: enter() dibuja su contenido inicial en
// el AREA DE VENTANA, y tick() (opcional) actualiza por frame. El marco
// (barra de estado + cabecera con "atras" + barra de navegacion) y los
// gestos de cierre los gestiona el framework: las apps solo pintan su
// contenido. Para rellenar una app, se reemplaza su entrada en APP_REG.

// Area de contenido. Embebida en DeX no hay barra de estado ni barra de
// navegacion que esquivar, asi que el contenido ocupa TODO el lienzo: es lo que
// elimina las franjas vacias de arriba y abajo dentro de la ventana.
// Son macros que se expanden en el punto de uso, y todos los usos quedan por
// debajo de donde se declaran gHosted y gAppH (justo aqui abajo, con los flags
// de APP_REG), asi que la expansion siempre las conoce.
#define WIN_TOP (gHosted ? 0 : 96)
#define WIN_BOT (gHosted ? gAppH : (SCR_H - 64))
#define WIN_BG  TH_WIN              // fondo de ventana: lo elige la paleta activa (ver TEMA SEMANTICO)

// REGISTRO CENTRAL DE APPS. Una app de Flex OS ES su entrada en APP_REG, y su
// ID UNICO es el indice de esa entrada (el mismo del enum IC_*). Todo lo demas
// cuelga de ese id, sin listas paralelas que puedan desincronizarse:
//   · id       -> indice en APP_REG (0..APP_N-1)
//   · nombre   -> appName(id), localizado en APP[APP_N][5]
//   · icono    -> drawAppIcon(id, x, y, S), vectorial
//   · callback -> enter() / tick() de esta misma estructura
//   · categoria-> campo 'cat' (APP_CAT_*), lo usa la Caja de aplicaciones
//   · visible / favoritaEnInicio -> bits en gAppHidden / gAppFav (NVS), con
//     'dflt' como valor de fabrica de cada app. Son bitmasks y no campos de la
//     estructura porque cambian en caliente y hay que persistirlos: dos enteros
//     en NVS en vez de dieciseis claves (mismo criterio que gAppLock).
typedef struct { void (*enter)(); void (*tick)(); uint8_t flags; uint8_t cat; uint8_t dflt; const AppHooks* hooks; } FlexApp;
#define APP_CUSTOM_HEADER 1   // la app pinta su propia cabecera (no la centrada)
#define APP_OWN_TOUCH     2   // la app gestiona TODOS sus toques (solo swipe-derecha cierra)
#define APP_LAND          4   // la app dibuja en LANDSCAPE (pone gLand por su cuenta)
// true mientras se re-ejecuta enter() SOLO para volver a maquetar tras un
// cambio de tamano. Una app cuyo enter() tambien inicializa estado (la
// Calculadora pone el display a "0", Ajustes reinicia scroll y seleccion) debe
// saltarse esa parte: al redimensionar se re-dibuja, no se reinicia.
static bool gRelayout = false;
#define APP_FLEX          8   // la app maqueta contra gAppW/gAppH -> se le da un
                              // lienzo del TAMANO REAL de la ventana y se dibuja
                              // 1:1, sin escalar ni barras de letterbox.
#define APP_BG_KEEP      16   // LISTA BLANCA de segundo plano: suspendida, la app puede
                              // seguir teniendo trabajo REAL en curso (lo decide su
                              // hook bgWork()). Ni "Cerrar todo" ni el desalojo por
                              // limite de sesiones la tocan mientras ese trabajo dure.
// Lienzo LOGICO de la app en curso. A pantalla completa es la pantalla entera;
// dentro de una ventana de DeX, para una app APP_FLEX, es el area de cliente.
// Las apps adaptativas maquetan contra esto en vez de contra SCR_W/SCR_H.
static int gAppW = SCR_W, gAppH = SCR_H;
// Hosting en Modo PC: una app corriendo dentro de una ventana de DeX NO puede
// navegar por el sistema (cerrarse a Home, abrir otra app a pantalla completa o
// saltar al selector) -- eso desmontaria el escritorio que la contiene. Cuando
// gHosted esta activo, esas tres salidas se capturan como una PETICION que DeX
// atiende luego a su manera: cerrar la ventana, abrir otra ventana, o abrir
// Recientes de DeX. Es el unico punto donde el sistema y el hosting se tocan.
static bool gHosted    = false;
static int  gHostReq   = 0;      // 0 nada · 1 cerrar ventana · 2 abrir app · 3 recientes
static int  gHostReqApp = -1;
static void settingsEnter(); static void settingsTick();   // Ajustes (M3), abajo
// Navegacion interna de Ajustes: devuelve true si "atras" tenia una pantalla
// de categoria que cerrar (y la cierra). Lo consulta appTick para que el boton
// atras del sistema no salte directo al escritorio desde una subpantalla.
static bool settingsHandleBack();
static void wifiSettingsEnter(); static void wifiTick();    // Ajustes -> Red e Internet -> Wi-Fi, abajo
static void calcEnter(); static void calcTick();           // Calculadora (M2), abajo
static void pcEnter(); static void pcTick();               // Modo PC (M4), abajo
static void pcSuspend(); static void pcResume(); static void pcCloseApp();  // ...y su ciclo de vida
static void calResume();                                   // Calendario: reanudar = repintar
static void galEnter(); static void galTick();              // Galeria (indice real de medios + Flex Vault)
static void galCloseApp();                                 // ...suelta la cache de miniaturas
// ---- Ganchos de la multitarea por memoria (shed / dirty) ----
static size_t galShed();     // Galeria: suelta la cache de miniaturas decodificadas
static size_t vidShed();     // Multimedia: suelta fotograma, descriptor y buffers
static size_t camShed();     // Camara: suelta el buffer del sensor
static bool   noteDirty();   // Notas: hay texto escrito sin volcar
static bool   paintDirty();  // Paint: hay trazo sin volcar
static void   navSuspendLife();  // Navegador: al pasar a segundo plano
static size_t navShedLife();     // Navegador: suelta la cache de fotogramas
static bool galBackLayer(); static bool galBackScreen(); static void galSuspend(); static void galResume();
static void bienEnter(); static void bienTick();           // Bienestar (M2)
static void calEnter(); static void calTick();             // Calendario (M2)
static void vidEnter(); static void vidTick();             // Multimedia (esqueleto)
static void camEnter(); static void camTick();             // Camara (esqueleto)
static void noteEnter(); static void noteTick();           // Notas + teclado 4 capas
static void almEnter(); static void eduEnter();                         // apps simples
static void navEnter(); static void navTick();                          // Navegador (FlexOS_Browser*)
static void ideEnter(); static void ideTick(); static void paintEnter(); static void paintTick();
static void almTick();                                     // Almacenamiento: tap en "Ver..."
static bool almBackScreen(); static void almSuspend(); static void almResume(); static void almCloseApp();
static void almDetailTick();                               // Almacenamiento: pantalla de detalle (Fase 5)
static void filesTick();                                   // Explorador de archivos (ST_FILES)
static void connEnter(); static void connTick();           // Conectividad (ST_CONN)
static void flexBleStop(); static bool flexBleStart();     // radio BLE real
static void connWifiSub(char* out, size_t n);              // SSID real para Ajustes
static void connBleSub(char* out, size_t n);
static void gamesEnter(); static void gamesTick(); // Juegos: Jumper (motor en FlexOS_Jumper.h)
static void wxAppEnter(); static void wxAppTick();  // Clima (Flex Weather) -- seccion propia mas abajo
static bool wxHandleBack(); static void wxSuspend(); static void wxResume();
static void storeEnter(); static void storeTick(); static void storeExit(); // Flex Store + runtime FLXP
// Flex Phone: la app vive en FlexOS_FlexPhone_Bridge.h (igual que el
// navegador y la tienda). Aqui solo el prototipo para APP_REG.
static void fphEnter(); static void fphTick(); static void fphExit();
static bool fphBackScreen(); static void fphSuspend(); static void fphResume();
static void flexPhoneBegin(); static void flexPhoneTick();
// Hooks opcionales. Las implementaciones viven junto a cada app.
static void setSuspend(); static void setResume(); static bool setSaveSess(); static void setLoadSess(); static bool setBgWork();
static void calcResume(); static bool calcSaveSess(); static void calcLoadSess();
static void vidSuspend(); static void vidResume(); static void vidCloseApp(); static bool vidSaveSess(); static void vidLoadSess(); static bool vidBackScreen();
static void camSuspend(); static void camResume(); static void camCloseApp();
static bool noteBackLayer(); static bool noteBackScreen(); static void noteSuspend(); static void noteResume(); static void noteCloseApp(); static bool noteSaveSess(); static void noteLoadSess();
static bool paintBackScreen(); static void paintSuspend(); static void paintResume(); static void paintCloseApp(); static bool paintSaveSess(); static void paintLoadSess();
static void gamesSuspend(); static void gamesResume(); static void gamesCloseApp();
static void navResumeLife(); static void navCloseLife();
static void storeResumeLife(); static void storeCloseLife();
// Rect del icono en el escritorio (para animar la apertura desde el)
static void getIconRect(int id, int &rx, int &ry, int &rs){
  if(id == gIconOvrApp){ rx = gIconOvrX; ry = gIconOvrY; rs = gIconOvrS; return; }
  // EL DOCK son EXACTAMENTE los ids 12..15.
  bool inDock = (id >= 12 && id <= 15);
  if(!inDock){
    // La rejilla se dibuja por SLOT (homeOrder[slot] = id de app), no por id.
    // Antes esto calculaba la casilla con id%4 e id/4, o sea daba por hecho que
    // cada app sigue en su casilla original. En cuanto se reordenaban los iconos
    // en Modo Edicion, la animacion de apertura crecia desde donde ESTABA la app
    // antes: mover Notas al hueco de la Calculadora hacia que Notas se abriera
    // desde el sitio de la Calculadora. Hay que buscar en que slot esta hoy.
    int slot = (id < homeSlotCount()) ? id : 0;   // respaldo acotado a la rejilla
    // Y solo cuenta si esta en la PAGINA VISIBLE: si el icono vive en
    // otra pagina no esta en pantalla, y hacer crecer la ventana desde
    // sus coordenadas dibujaria la animacion partiendo de un sitio
    // donde el usuario no ve nada. En ese caso se queda la casilla que
    // le tocaria por id, que es el comportamiento de siempre.
    int cells = homeSlotCount();
    for(int sl = 0; sl < cells; sl++)
      if(homeOrder[homeIdx(gHomePage, sl)] == id){ slot = sl; break; }
    int gS, ggx0, ggy0, gcs, grs, gcols, grows; homeGrid(gS, ggx0, ggy0, gcs, grs, gcols, grows);
    homeSlotXY(slot, rx, ry); rs = gS;
  } else {
    int dkx = 24, dky = SCR_H - 176, dkw = SCR_W - 48, dkh = 96, dS = 64;
    int inner = dkw - 32, dgap = (inner - 4 * dS) / 3, i = id - 12;
    rx = dkx + 16 + i * (dS + dgap); ry = dky + (dkh - dS) / 2; rs = dS;
  }
}

// Marco estandar de ventana (barra de estado + cabecera + nav bar)
static void appDrawChrome(int id){
  if(gHosted){ (void)id; return; }   // embebida: la ventana ya tiene su barra de titulo
  setBuf(fb);
  // Barra de estado y de navegacion del marco de app: van sobre el fondo de
  // VENTANA del tema (no sobre el wallpaper), asi que usan el color de iconos
  // de navegacion de la paleta activa.
  uint16_t W = TH_NAV;
  cronoBarClock(16, W);            // hora + capsula del cronometro (misma geometria que el Home)
  drawWifi(SCR_W - 66, 28, 11, W);
  drawBattery(SCR_W - 46, 20, 30, 15, 82, W);
  // MODO BOTONES: la barra inferior ya NO la dibuja el marco de la app. La
  // estampa el sistema dentro de flxFlush (navStampBar), asi que hay un solo
  // propietario de esos 64 px y ninguna app puede pisarlos ni hacerlos parpadear.
  if(gNavMode != 0) drawHomeIndicator(SCR_H, 180);
  (void)id;
}
// Cabecera estandar (chevron "atras" + titulo centrado). Las apps con
// APP_CUSTOM_HEADER se saltan esto y pintan su propia cabecera.
static void appDrawHeader(int id){
  if(gHosted) return;                // el nombre de la app lo pone la barra de titulo
  uint16_t W = TH_TXT;
  int hy = 50;
  strokeSegAA(30, hy + 16, 18, hy + 8, 2.4f, W);
  strokeSegAA(18, hy + 8, 30, hy, 2.4f, W);
  drawTextC(SCR_W / 2, hy + 3, appName(id), 3, W);
}


// #############################################################
// ##  LAYOUT RESPONSIVO  ·  toolkit compartido por las apps
// #############################################################
// Toda app APP_FLEX maqueta contra el LIENZO REAL: gAppW de ancho y
// [WIN_TOP..WIN_BOT] de alto. A pantalla completa eso es la pantalla menos su
// chrome; dentro de una ventana de DeX es EXACTAMENTE el area de cliente, y se
// dibuja 1:1. Ahi esta la diferencia con el camino antiguo: nada se dibuja a
// 480x800 para luego remuestrearlo al tamano de la ventana, que es lo que
// dejaba el texto emborronado ("como una imagen ampliada"). Al maquetar al
// tamano final, la fuente vectorial se rasteriza a su tamano real y sale
// nitida en cualquier ventana.

static void uiBox(int &x, int &y, int &w, int &h){
  x = 0; y = WIN_TOP; w = gAppW; h = WIN_BOT - WIN_TOP;
  if(w < 32) w = 32;
  if(h < 32) h = 32;
}
static inline int uiW(){ int x, y, w, h; uiBox(x, y, w, h); return w; }
static inline int uiH(){ int x, y, w, h; uiBox(x, y, w, h); return h; }
static inline int uiTop(){ int x, y, w, h; uiBox(x, y, w, h); return y; }
// Margen y separacion proporcionales, acotados para que no se coman la caja.
static int uiPad(){
  int w = uiW(), h = uiH(), m = (w < h ? w : h) / 24;
  if(m < 5) m = 5;
  if(m > 22) m = 22;
  return m;
}
static inline int uiGap(){ int g = uiPad() * 3 / 4; return g < 4 ? 4 : g; }
// Tamano de fuente mas grande que cabe en `maxw`, sin pasar de `maxSize`.
static int uiFontFit(const char* t, int maxw, int maxSize){
  int fs = maxSize; if(fs > 5) fs = 5; if(fs < 1) fs = 1;
  while(fs > 1 && textW(t, fs) > maxw) fs--;
  return fs;
}
// Tamano de fuente adecuado a una altura de linea.
static int uiFontH(int lineH){
  if(lineH >= 44) return 5;
  if(lineH >= 33) return 4;
  if(lineH >= 23) return 3;
  if(lineH >= 14) return 2;
  return 1;
}
// Altura aproximada de una linea de texto de tamano fs (para reservar sitio).
static inline int uiLineH(int fs){ return fs <= 1 ? 8 : fs * 9; }
// Titulo de seccion: se dibuja solo si cabe, y devuelve la Y siguiente.
static int uiTitle(int x, int y, int w, const char* t, uint16_t col, int maxSize){
  int fs = uiFontFit(t, w, maxSize);
  drawTextC(x + w / 2, y, t, fs, col);
  return y + uiLineH(fs) + uiGap() / 2;
}

// ---- Secciones opcionales con breakpoint y fundido ----
// Una seccion opcional aparece ENTERA cuando el lienzo da de si y desaparece
// ENTERA por debajo del umbral: nunca a medias ni interpolada de tamano. El
// umbral lo decide cada app segun lo que su seccion necesita para verse bien,
// no un numero arbitrario. Al cruzarlo en vivo (arrastrando el borde) se
// aplica un fundido corto para que no salte de golpe.
#define UI_FADE_MS 130
#define UI_SEC_MAX 12
struct UiSec { uint8_t app; uint8_t id; bool on; uint32_t t0; };
static UiSec  uiSecs[UI_SEC_MAX];
static uint8_t uiSecN = 0;
static bool    uiFading = false;      // hay algun fundido en curso (lo consulta DeX)

// Devuelve el alfa 0..255 con el que dibujar la seccion `id` de la app en
// curso. 0 = no dibujarla. `want` es el breakpoint ya evaluado por la app.
static uint8_t uiSection(uint8_t id, bool want){
  uint8_t app = (uint8_t)gAppId;
  UiSec* sc = NULL;
  for(uint8_t i = 0; i < uiSecN; i++) if(uiSecs[i].app == app && uiSecs[i].id == id){ sc = &uiSecs[i]; break; }
  if(!sc){
    if(uiSecN >= UI_SEC_MAX) return want ? 255 : 0;    // sin ranura: sin fundido, pero correcto
    sc = &uiSecs[uiSecN++];
    sc->app = app; sc->id = id; sc->on = want; sc->t0 = 0;
    return want ? 255 : 0;
  }
  if(want != sc->on){ sc->on = want; sc->t0 = millis(); }
  if(sc->t0 == 0) return sc->on ? 255 : 0;
  uint32_t e = millis() - sc->t0;
  if(e >= UI_FADE_MS){ sc->t0 = 0; return sc->on ? 255 : 0; }
  uiFading = true;
  uint32_t a = (uint32_t)255 * e / UI_FADE_MS;
  return (uint8_t)(sc->on ? a : 255 - a);
}
// Texto y relleno con alfa, para que las secciones opcionales puedan fundirse.
static inline void uiRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a){
  if(a == 0) return;
  if(a >= 255) fillRoundRect(x, y, w, h, r, c);
  else fillRoundRectA(x, y, w, h, r, c, a);
}
static inline void uiText(int x, int y, const char* t, int fs, uint16_t c, uint8_t a){
  if(a == 0) return;
  drawTextA(x, y, t, fs, c, a);
}
static inline void uiTextC(int cx, int y, const char* t, int fs, uint16_t c, uint8_t a){
  if(a == 0) return;
  drawTextCA(cx, y, t, fs, c, a);
}
static inline void uiTextR(int rx, int y, const char* t, int fs, uint16_t c, uint8_t a){
  if(a == 0) return;
  drawTextA(rx - textW(t, fs), y, t, fs, c, a);
}

// #############################################################
// ##  CABECERA DE APP  ·  una sola geometria para todas
// ##  ------------------------------------------------------
// ##  El chevron de volver estaba copiado como dos strokeSegAA con
// ##  coordenadas literales en trece sitios, y el titulo lo ponia
// ##  cada pantalla donde le parecia. En Archivos, Notas y Paint el
// ##  titulo empezaba en x=16 y el chevron ocupaba x=18..30: la
// ##  flecha quedaba DENTRO de la primera letra. No era un pixel de
// ##  mas -- se leia "Archivos" con un palo cruzado encima.
// ##
// ##  Aqui esta la geometria, una vez:
// ##
// ##    0            56              SCR_W-56        SCR_W
// ##    |--- atras ---|--- titulo ----|--- menu ------|
// ##
// ##  · las dos zonas tactiles miden 56x56, por encima de los 44x44
// ##    minimos, y el glifo va CENTRADO en la suya (antes estaba
// ##    pegado a la esquina, asi que la mitad de la zona tactil no
// ##    tenia nada debajo);
// ##  · el titulo empieza donde acaba la zona de atras mas un
// ##    margen, y se recorta antes de la del menu: no puede compartir
// ##    ni un pixel con ninguna de las dos por largo que sea;
// ##  · misma altura y misma linea base en todas las pantallas, para
// ##    que al pasar de una app a otra la cabecera no salte.
// #############################################################
#define UIHDR_ZONE   56                       // lado de las zonas tactiles (>= 44)
#define UIHDR_H      76                       // alto de la banda de cabecera
#define UIHDR_GAP     8                       // aire entre una zona y el texto
#define UIHDR_TX     (UIHDR_ZONE + UIHDR_GAP) // primera columna del titulo
#define UIHDR_TR     (SCR_W - UIHDR_ZONE - UIHDR_GAP)  // ultima columna util del titulo
#define UIHDR_CY     (UIHDR_ZONE / 2)         // centro vertical de los glifos

// Zonas tactiles. Se preguntan con estas funciones y no con literales,
// para que el sitio donde se dibuja y el sitio donde responde no puedan
// separarse nunca.
static inline bool uiHdrBackHit(int px, int py){
  return px >= 0 && px < UIHDR_ZONE && py >= 0 && py < UIHDR_ZONE;
}
static inline bool uiHdrMenuHit(int px, int py){
  return px >= SCR_W - UIHDR_ZONE && px < SCR_W && py >= 0 && py < UIHDR_ZONE;
}

// Chevron de volver, centrado en su zona.
static void uiHdrChevron(uint16_t col){
  int cx = UIHDR_ZONE / 2, cy = UIHDR_CY, a = 8;
  strokeSegAA((float)(cx + a / 2), (float)(cy - a), (float)(cx - a / 2), (float)cy, 2.4f, col);
  strokeSegAA((float)(cx - a / 2), (float)cy, (float)(cx + a / 2), (float)(cy + a), 2.4f, col);
}

// Tres puntos del menu, centrados en la suya.
static void uiHdrDots(uint16_t col){
  int cx = SCR_W - UIHDR_ZONE / 2;
  for(int i = -1; i <= 1; i++) fillCircle(cx, UIHDR_CY + i * 14, 4, col);
}

// La cabecera entera. `fs` es el tamano del titulo; si no cabe entre las
// dos zonas se reduce, y si aun asi no cabe se recorta -- nunca se
// desborda sobre un boton.
static void uiHdrDraw(const char* title, int fs, uint16_t txt, uint16_t nav, bool menu){
  uiHdrChevron(nav);
  if(menu) uiHdrDots(nav);
  if(!title || !title[0]) return;
  int avail = (menu ? UIHDR_TR : SCR_W - UIHDR_GAP) - UIHDR_TX;
  while(fs > 1 && textW(title, fs) > avail) fs--;
  drawTextClip(UIHDR_TX, UIHDR_CY - uiLineH(fs) / 2, title, fs, txt, UIHDR_TX + avail);
}

// #############################################################
// ##  VIEWPORT DE UNA LISTA CON SCROLL
// ##  ------------------------------------------------------
// ##  Toda pantalla con cabecera fija y contenido que se arrastra
// ##  tiene el mismo fallo latente: las filas se dibujan en
// ##  `y - scroll`, y la unica proteccion era un "if" por elemento
// ##  del tipo `if(y + alto >= 58)`. Ese if mira el borde de
// ##  ABAJO. Con la lista subida, `y` se vuelve muy negativo, la
// ##  condicion sigue siendo cierta y la tarjeta se dibuja
// ##  entera... encima de la cabecera. Eso es lo que en el video
// ##  de Flex Vault apila "Ultimo acceso", "Bloqueo automatico" e
// ##  "Intentos fallidos" unos sobre otros en la banda de arriba.
// ##
// ##  La solucion no es anadir el otro if en cada uno de los
// ##  cincuenta sitios que dibujan una fila: es que el area con
// ##  scroll tenga un RECORTE EXCLUSIVO. Dentro de el, un elemento
// ##  a medio salir se corta por donde toca; fuera, no se escribe
// ##  ni un pixel por mucho que se equivoque una coordenada.
// ##
// ##  No cuesta nada: gClipY0/gClipY1 ya los respetan todas las
// ##  primitivas, asi que esto no anade trabajo por frame.
// #############################################################
static inline void uiClipViewport(int top, int bot){
  if(top < 0) top = 0;
  if(bot > SCR_H - 1) bot = SCR_H - 1;
  gClipY0 = top; gClipY1 = bot;
  gClipX0 = 0;   gClipX1 = SCR_W - 1;
}
static inline void uiClipFull(){
  gClipY0 = 0; gClipY1 = SCR_H - 1;
  gClipX0 = 0; gClipX1 = SCR_W - 1;
}

// ---- Contenido de apps ----
// (1) Placeholder para apps aun no implementadas (dentro de la ventana)
static void appPlaceholderEnter(){
  setBuf(fb);
  int cy = (WIN_TOP + WIN_BOT) / 2;
  if(uiGlass) drawLiquidGlassPanel(36, cy - 168, SCR_W - 72, 268, 26, TH_GLASS2);  // modal glass
  drawAppIcon(gAppId, SCR_W / 2 - 44, cy - 130, 88);
  drawTextC(SCR_W / 2, cy + 6, t(S_SOON), 3, TH_TXT);
  drawTextC(SCR_W / 2, cy + 48, t(S_M2), 2, TH_TXT2);
}
// (2) App REAL de referencia: Reloj (prueba el patron completo)
//
// RELOJ CON PESTANAS. La app dejo de ser una sola pantalla: ahora es un
// contenedor con tres modos -- Hora (el de siempre, intacto), Cronometro (One
// UI) y Temporizador (aun "Proximamente"). El contenedor es el UNICO que
// limpia el lienzo, pinta la barra de pestanas y hace el flxFlush; cada
// pestana solo pinta SU cuerpo. Asi ninguna pestana puede dejar restos de
// otra ni duplicar el volcado.
#define RELOJ_TABS_H  46            // alto de la barra de pestanas
#define RELOJ_TAB_N    3
static void appRelojRender();       // el contenedor; lo llaman las pestanas al cambiar

// Cuerpo util de una pestana = caja de la app menos la barra de pestanas.
static void relojBody(int &x, int &y, int &w, int &h){
  uiBox(x, y, w, h);
  int tb = RELOJ_TABS_H;
  if(h < tb + 60) tb = 0;           // lienzo diminuto (ventana de DeX): sin pestanas
  y += tb; h -= tb;
  if(h < 32) h = 32;
}
static inline bool relojTabsVisible(){ return uiH() >= RELOJ_TABS_H + 60; }

// Barra de pestanas: un segmento por modo, el activo relleno con el acento.
static void relojDrawTabs(){
  if(!relojTabsVisible()) return;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad, h = RELOJ_TABS_H - 10, y = by + 5;
  int seg = w / RELOJ_TAB_N;
  // Barra de pestanas: material del sistema. Antes era un relleno PLANO con el
  // color del vidrio (thCard() devuelve el TINTE cuando Liquid Glass esta
  // activo), o sea que con Vidrio se veia plana -- justo lo que se reporto de
  // los controles dentro de las apps.
  uiSurface(x, y, w, h, h / 2, UIS_CARD);
  const int ids[RELOJ_TAB_N] = { S_CRN_HOUR, S_CRN_STOPW, S_CRN_TIMER };
  for(int i = 0; i < RELOJ_TAB_N; i++){
    int sx = x + i * seg;
    bool on = (gRelojTab == (uint8_t)i);
    if(on) fillRoundRect(sx + 2, y + 2, seg - 4, h - 4, (h - 4) / 2, TH_PRIM);
    int fs = uiFontFit(t(ids[i]), seg - 12, 2);
    drawTextC(sx + seg / 2, y + (h - uiLineH(fs)) / 2 + 1, t(ids[i]), fs,
              on ? TH_ONACC : TH_TXT2);
  }
}
// Devuelve true si el toque cayo en la barra de pestanas (y lo consume).
static bool relojTabsTouch(){
  if(!T.tap || !relojTabsVisible()) return false;
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  int pad = uiPad();
  int x = bx + pad, w = bw - 2 * pad, h = RELOJ_TABS_H - 10, y = by + 5;
  if(T.y < y || T.y > y + h || T.x < x || T.x > x + w) return false;
  int seg = w / RELOJ_TAB_N;
  int i = (T.x - x) / seg;
  if(i < 0) i = 0; if(i >= RELOJ_TAB_N) i = RELOJ_TAB_N - 1;
  T.tap = false; T.pressed = false;              // no se propaga al cuerpo ni al marco
  if((uint8_t)i == gRelojTab) return true;
  gRelojTab = (uint8_t)i;
  appRelojRender();
  return true;
}

// RELOJ · pestana HORA (adaptativa, tal cual estaba).
//   Esencial   : reloj gigante, centrado, con el trazo escalado al lienzo.
//   Opcional 1 : fecha larga -- aparece cuando quedan >= 26 px bajo el reloj.
//   Opcional 2 : tarjetas de fecha corta / dia del ano -- aparecen cuando el
//                lienzo tiene >= 250 px de ancho Y >= 90 px libres debajo, que
//                es lo que necesitan para no quedar apretadas.
static void appRelojHoraRender(){
  int bx, by, bw, bh; relojBody(bx, by, bw, bh);
  int pad = uiPad();
  char cs[8]; clkStr12(cs, sizeof(cs));
  // El reloj se lleva ~40% del alto, y nunca mas ancho de lo que cabe.
  int capH = bh * 2 / 5;
  int maxByW = (bw - 2 * pad) * 10 / (int)(strlen(cs) * 7 + 2);
  if(capH > maxByW) capH = maxByW;
  if(capH > 150) capH = 150;
  if(capH < 22) capH = 22;
  int thick = capH / 8; if(thick < 3) thick = 3;
  int cy = by + pad + bh / 12;
  drawBigClock(cs, bx + bw / 2, cy, capH, thick, TH_TXT);
  int y = cy + capH + pad;
  int rest = (by + bh) - y - pad;
  char ds[64]; buildLongDate(ds, sizeof(ds));
  uint8_t aDate = uiSection(0, rest >= 26);
  if(aDate){
    int fs = uiFontFit(ds, bw - 2 * pad, uiFontH(rest / 3 > 30 ? 30 : rest));
    uiTextC(bx + bw / 2, y, ds, fs, TH_TXT2, aDate);
    y += uiLineH(fs) + pad;
    rest = (by + bh) - y - pad;
  }
  // Panel opcional de tarjetas: solo con ancho y alto suficientes.
  uint8_t aCards = uiSection(1, bw >= 250 && rest >= 90);
  if(aCards){
    int n = 2, g = uiGap();
    int cw = (bw - 2 * pad - g) / n, chh = rest > 130 ? 130 : rest;
    char sd[40]; buildShortDate(sd, sizeof(sd));
    char doy[24]; snprintf(doy, sizeof(doy), "%s", g24h ? "24 h" : "12 h");
    const char* lbl[2] = { "Fecha", "Formato" };
    const char* val[2] = { sd, doy };
    for(int i = 0; i < n; i++){
      int x = bx + pad + i * (cw + g);
      uiRectA(x, y, cw, chh, uiPad(), thCard(), aCards);
      int fl = uiFontFit(lbl[i], cw - 16, 2);
      uiTextC(x + cw / 2, y + chh / 2 - uiLineH(fl) - 6, lbl[i], fl, TH_TXT2, aCards);
      int fv = uiFontFit(val[i], cw - 16, 3);
      uiTextC(x + cw / 2, y + chh / 2 + 2, val[i], fv, TH_TXT, aCards);
    }
    y += chh + pad;
  }
  int fy = by + bh - pad - uiLineH(2);
  if(fy > y) drawTextC(bx + bw / 2, fy, "Reloj de FlexOS", uiFontFit("Reloj de FlexOS", bw - 2 * pad, 2), TH_MUTE);
}

// RELOJ · pestana TEMPORIZADOR (aun sin motor propio; se anuncia como tal en
// vez de fingir que existe -- el criterio del resto del sistema).
static void appRelojTempRender(){
  int bx, by, bw, bh; relojBody(bx, by, bw, bh);
  int cy = by + bh / 2;
  drawTextC(bx + bw / 2, cy - 24, t(S_CRN_TIMER), uiFontFit(t(S_CRN_TIMER), bw - 40, 4), TH_TXT);
  drawTextC(bx + bw / 2, cy + 16, t(S_SOON),      uiFontFit(t(S_SOON),      bw - 40, 2), TH_TXT2);
}

// CONTENEDOR: limpia, pinta pestanas, delega el cuerpo y hace UN solo flush.
static void appRelojRender(){
  setBuf(fb);
  int bx, by, bw, bh; uiBox(bx, by, bw, bh);
  fillRect(bx, by, bw, bh, WIN_BG);
  relojDrawTabs();
  switch(gRelojTab){
    case 1:  cronoTabRender(); break;
    case 2:  appRelojTempRender(); break;
    default: appRelojHoraRender(); break;
  }
  flxFlush(WIN_TOP, WIN_BOT);
}
static void appRelojEnter(){ appRelojRender(); }
static void appRelojTick(){
  if(relojTabsTouch()) return;                 // cambio de pestana: ya repinto y consumio el toque
  switch(gRelojTab){
    case 1:  cronoTabTick(); break;            // el cronometro se refresca solo
    case 2:  break;                            // placeholder estatico
    default: if(gMinChanged) appRelojRender(); break;
  }
}

// ---- Categorias del registro (campo 'cat' de FlexApp) ----
// Numeros y no una cadena por app: la etiqueta visible se localiza en
// APP_CAT_NAME[cat][idioma], igual que APP[id][idioma] hace con los nombres.
#define APP_CAT_ESENCIAL 0
#define APP_CAT_MEDIA    1
#define APP_CAT_TRABAJO  2
#define APP_CAT_SISTEMA  3
#define APP_CAT_OCIO     4
#define APP_CAT_N        5
// Valor de fabrica del campo 'dflt': que apps nacen en el escritorio. Se usa
// UNA vez, la primera que arranca el equipo (o al restablecer): a partir de ahi
// manda lo guardado en NVS.
// De fabrica son favoritas las doce de la rejilla, exactamente las mismas que
// se veian antes de que existiera la caja: una placa que actualice no nota
// ningun cambio en su escritorio. Las cuatro del dock (12..15) NO llevan la
// marca porque el dock ya las tiene fijadas; el usuario puede anadirlas a la
// rejilla desde la caja si quiere.
#define APP_DEF_FAV      1   // aparece en la rejilla de Inicio de fabrica
#define APP_DEF_DOCK     0   // no en la rejilla (vive en el dock)
static const char* APP_CAT_NAME[APP_CAT_N][5] = {
  {"Esenciales","Essentials","Essentiels","Essenciais","Essenziali"},
  {"Multimedia","Media","M\xC3\xA9" "dias","M\xC3\xAD" "dia","Multimedia"},
  {"Productividad","Productivity","Productivit\xC3\xA9","Produtividade","Produttivit\xC3\xA0"},
  {"Sistema","System","Syst\xC3\xA8" "me","Sistema","Sistema"},
  {"Ocio","Fun","Loisirs","Lazer","Svago"},
};
// Almacenamiento: pantalla interna de detalle (atras vuelve a la lista) y
// suspension que apaga la medida cara de la flash. No guarda sesion: su estado
// es "que pantalla se estaba viendo", y eso se reconstruye al abrirla.
// Las dos ultimas columnas son los ganchos de la multitarea por memoria:
//   shed  -> soltar recursos PESADOS y RECONSTRUIBLES sin perder estado
//   dirty -> "tengo cambios del usuario sin guardar"
// NULL en las dos = esta app no tiene nada pesado que soltar ni nada que el
// usuario pueda perder. Se escriben explicitamente (y no se dejan implicitas)
// para que anadir una app obligue a decidir las dos cosas.
// Modo PC / DeX. Conserva la disposicion de ventanas al pasar a segundo plano
// y suelta los lienzos (768 KB por ventana) y el fondo compuesto. No lleva
// 'shed' porque su suspend ya suelta TODO lo pesado: no queda nada que soltar
// despues, y devolver 0 bytes seria lo unico honesto que podria hacer.
static const AppHooks H_MODOPC   = { NULL, NULL, pcSuspend, pcResume, pcCloseApp, NULL, NULL, NULL, NULL, NULL };
// Calendario. Solo muestra el mes EN CURSO (no tiene navegacion de meses), asi
// que su unico estado es "que dia es hoy" y lo da el reloj: reanudar es
// repintar. Se declara el gancho igual, para que la app no dependa de que
// enter() y resume() hagan por casualidad lo mismo.
static const AppHooks H_CALEND   = { NULL, NULL, NULL, calResume, NULL, NULL, NULL, NULL, NULL, NULL };
static const AppHooks H_ALM      = { NULL, almBackScreen, almSuspend, almResume, almCloseApp, NULL, NULL, NULL, NULL, NULL };
static const AppHooks H_SETTINGS = { NULL, settingsHandleBack, setSuspend, setResume, NULL, setSaveSess, setLoadSess, setBgWork, NULL, NULL };
static const AppHooks H_GALLERY  = { galBackLayer, galBackScreen, galSuspend, galResume, galCloseApp, NULL, NULL, NULL, galShed, NULL };
static const AppHooks H_CALC     = { NULL, NULL, NULL, calcResume, NULL, calcSaveSess, calcLoadSess, NULL, NULL, NULL };
static const AppHooks H_MEDIA    = { NULL, vidBackScreen, vidSuspend, vidResume, vidCloseApp, vidSaveSess, vidLoadSess, NULL, vidShed, NULL };
static const AppHooks H_CAMERA   = { NULL, NULL, camSuspend, camResume, camCloseApp, NULL, NULL, NULL, camShed, NULL };
// Notas y Paint no llevan 'shed': lo unico que reservan en PSRAM son 4 KB de
// texto y 2 KB de trazo en curso. Soltarlos no cambia nada medible y si
// arriesga el contenido del usuario, que es exactamente lo que no se hace.
static const AppHooks H_NOTES    = { noteBackLayer, noteBackScreen, noteSuspend, noteResume, noteCloseApp, noteSaveSess, noteLoadSess, NULL, NULL, noteDirty };
static const AppHooks H_PAINT    = { NULL, paintBackScreen, paintSuspend, paintResume, paintCloseApp, paintSaveSess, paintLoadSess, NULL, NULL, paintDirty };
static const AppHooks H_GAMES    = { NULL, NULL, gamesSuspend, gamesResume, gamesCloseApp, NULL, NULL, NULL, NULL, NULL };
static const AppHooks H_BROWSER  = { NULL, NULL, navSuspendLife, navResumeLife, navCloseLife, NULL, NULL, NULL, navShedLife, NULL };
static const AppHooks H_STORE    = { NULL, NULL, NULL, storeResumeLife, storeCloseLife, NULL, NULL, NULL, NULL, NULL };
static const AppHooks H_WEATHER  = { NULL, wxHandleBack, wxSuspend, wxResume, NULL, NULL, NULL, NULL, NULL, NULL };
// Flex Phone. backScreen cierra primero la conversacion y luego vuelve
// a Centro; closeApp vuelca el estado a disco (la escritura periodica
// esta agrupada, asi que al salir SI toca guardar).
static const AppHooks H_FLEXPHONE = { NULL, fphBackScreen, fphSuspend, fphResume, fphExit, NULL, NULL, NULL, NULL, NULL };
// ---- Registro de apps (indices = enum IC_*) ----
static FlexApp APP_REG[APP_N] = {
  { appRelojEnter, appRelojTick, APP_FLEX, APP_CAT_ESENCIAL, APP_DEF_FAV, NULL },
  { galEnter, galTick, APP_FLEX, APP_CAT_MEDIA, APP_DEF_FAV, &H_GALLERY },
  { vidEnter, vidTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH, APP_CAT_MEDIA, APP_DEF_FAV, &H_MEDIA },
  { almEnter, almTick, APP_FLEX, APP_CAT_SISTEMA, APP_DEF_FAV, &H_ALM },
  { pcEnter, pcTick, APP_CUSTOM_HEADER, APP_CAT_SISTEMA, APP_DEF_FAV, &H_MODOPC },
  { noteEnter, noteTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH, APP_CAT_TRABAJO, APP_DEF_FAV, &H_NOTES },
  { eduEnter, NULL, APP_FLEX, APP_CAT_TRABAJO, APP_DEF_FAV, NULL },
  { navEnter, navTick, APP_FLEX | APP_OWN_TOUCH, APP_CAT_ESENCIAL, APP_DEF_FAV, &H_BROWSER },
  { ideEnter, ideTick, APP_FLEX, APP_CAT_TRABAJO, APP_DEF_FAV, NULL },
  { bienEnter, bienTick, APP_FLEX, APP_CAT_SISTEMA, APP_DEF_FAV, NULL },
  { paintEnter, paintTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH, APP_CAT_OCIO, APP_DEF_FAV, &H_PAINT },
  { gamesEnter, gamesTick, APP_OWN_TOUCH | APP_CUSTOM_HEADER | APP_LAND, APP_CAT_OCIO, APP_DEF_FAV, &H_GAMES },
  { settingsEnter, settingsTick, APP_CUSTOM_HEADER, APP_CAT_SISTEMA, APP_DEF_DOCK, &H_SETTINGS },
  { calcEnter, calcTick, APP_FLEX, APP_CAT_TRABAJO, APP_DEF_DOCK, &H_CALC },
  { calEnter, calTick, APP_FLEX, APP_CAT_TRABAJO, APP_DEF_DOCK, &H_CALEND },
  { camEnter, camTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH, APP_CAT_MEDIA, APP_DEF_DOCK, &H_CAMERA },
  // 16 Clima (REAL: Open-Meteo). APP_OWN_TOUCH porque el buscador de ciudades
  // usa el teclado del sistema, que ocupa la MISMA franja que el boton "atras"
  // de la barra: si lo gestionara el framework, la barra espaciadora cerraria
  // la app. Ese boton lo atiende wxNavBack() con la misma geometria.
  // NO nace en la rejilla (APP_DEF_DOCK): el escritorio de fabrica se queda
  // EXACTAMENTE como estaba -- doce iconos en la pagina 0 y las siguientes
  // vacias -- y una placa que actualiza no ve su Inicio reordenado. Clima se
  // abre desde su widget (la fila de arriba) y desde la Caja de aplicaciones,
  // que es de donde el usuario puede anadirla a Inicio si quiere.
  { wxAppEnter, wxAppTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH, APP_CAT_ESENCIAL, APP_DEF_DOCK, &H_WEATHER },
  // 17 Flex Store. Gestiona su cabecera y tactil; las operaciones de red/flash
  // corren en una tarea de fondo para no bloquear la interfaz.
  { storeEnter, storeTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH | APP_FLEX, APP_CAT_SISTEMA, APP_DEF_DOCK, &H_STORE },
  // 18 Flex Phone. Gestiona su cabecera y su tactil (pestanas propias).
  // NO nace en la rejilla (APP_DEF_DOCK): una placa que actualiza no ve
  // su escritorio reordenado; se anade desde la Caja de aplicaciones.
  { fphEnter, fphTick, APP_CUSTOM_HEADER | APP_OWN_TOUCH | APP_FLEX, APP_CAT_ESENCIAL, APP_DEF_DOCK, &H_FLEXPHONE },
};
static const char* appCatName(int id){
  int c = (id >= 0 && id < APP_N) ? APP_REG[id].cat : APP_CAT_SISTEMA;
  if(c < 0 || c >= APP_CAT_N) c = APP_CAT_SISTEMA;
  return APP_CAT_NAME[c][LI()];
}
// Reparto de fabrica de favoritas/ocultas. Lo declara homeOrderLoad() mucho mas
// arriba (necesita llamarlo en el primer arranque) y se define AQUI porque es
// donde vive el registro del que sale: si manana una app nace fuera del
// escritorio, basta con quitarle APP_DEF_FAV en su fila de APP_REG.
static void drawerRegistryDefaults(){
  gAppFav = 0; gAppHidden = 0;
  for(int id = 0; id < APP_N; id++) if(APP_REG[id].dflt & APP_DEF_FAV) gAppFav |= (uint32_t)(1u << id);
}
static void drawerRegistryAdopt(int fromId){
  if(fromId < 0) fromId = 0;
  for(int id = fromId; id < APP_N; id++)
    if(APP_REG[id].dflt & APP_DEF_FAV) gAppFav |= (uint32_t)(1u << id);
}

// Animacion de apertura/cierre: la ventana crece/encoge desde el icono
// (WIN_ANIM_MS / winRevealAnim vivian aqui: un bucle for(;;) que no volvia
//  hasta terminar la animacion. Los sustituye el motor de TRANSICIONES
//  INTERRUMPIBLES de justo debajo, que hace lo mismo -- los tres estilos de
//  gAnimStyle incluidos -- pero un cuadro por vuelta de loop(), sin bloquear
//  el tactil ni la navegacion.)

// #############################################################
// ##  TRANSICIONES DE APP INTERRUMPIBLES  ·  navegacion != animacion
// ##  ------------------------------------------------------
// ##  QUE ARREGLA. winRevealAnim (justo arriba) es un bucle for(;;) que no
// ##  vuelve hasta que la animacion termina. Mientras corre no se lee el
// ##  tactil, no se despacha loop() y el estado logico aun no ha cambiado: el
// ##  sistema obligaba a ESPERAR a que acabase cada apertura y cada cierre.
// ##  Ese es el motivo real de que tocar otra app antes de que la anterior
// ##  terminara de cerrarse no hiciera nada -- el toque no llegaba a existir.
// ##
// ##  EL PRINCIPIO. El estado LOGICO (que app manda, quien recibe el toque) y
// ##  el estado VISUAL (que rectangulo se esta dibujando) son cosas distintas
// ##  y se mueven por separado:
// ##    · la intencion del usuario cambia el estado logico EN EL ACTO;
// ##    · la animacion es solo una capa que sigue dibujandose despues, y no
// ##      manda sobre nada.
// ##  Por eso Inicio acepta un toque nuevo mientras la app anterior todavia se
// ##  ve encogiendo, y por eso ese toque no puede perderse.
// ##
// ##  DOS CAPAS, UNA SOLA APP VIVA. Como mucho hay dos rectangulos en
// ##  pantalla (la que sale encogiendo y la que entra creciendo), pero nunca
// ##  dos apps renderizando: la saliente ya esta suspendida y la entrante aun
// ##  no ha corrido su enter(). Las dos capas son fillRoundRect sobre homeBuf,
// ##  asi que el coste por cuadro es el de la banda que ocupan, no el de dos
// ##  aplicaciones.
// ##
// ##  GENERACION. Cada intencion nueva incrementa gTrGen. Una finalizacion
// ##  que pertenezca a una generacion vieja se descarta: una transicion
// ##  reemplazada no puede volver a tocar el estado logico mas tarde.
// ##
// ##  CONTINUIDAD. Al re-dirigir se parte del progreso VISUAL actual (p0), no
// ##  de 0 ni de 1, y la duracion se escala con la distancia que queda. Asi no
// ##  hay salto entre la animacion vieja y la nueva.
// ##
// ##  TIEMPO REAL, NO CUADROS. El progreso sale de micros() -- que en el core de
// ##  ESP32 ES esp_timer_get_time(), y es la misma fuente monotonica que ya usa
// ##  el pacing de Jumper --: si un
// ##  cuadro se retrasa se SALTAN estados intermedios en vez de acumular deuda,
// ##  que es lo que exige no arrastrar latencia.
// #############################################################
// Duraciones. El video de referencia (S26 Ultra, 120 Hz) mide ~100-133 ms por
// tramo; aqui se usan las del encargo (cierre 180-220, apertura 200-240) porque
// a 60 Hz 100 ms son ~6 cuadros y el movimiento se ve a saltos. Lo que reproduce
// la sensacion del video no es la duracion bruta sino la RESPUESTA: el toque se
// acepta siempre y la re-direccion ocurre en el cuadro siguiente.
#define ATR_OPEN_MS    210                 // icono -> pantalla completa
#define ATR_CLOSE_MS   190                 // pantalla completa -> icono
#define ATR_MIN_MS      60                 // suelo al re-dirigir (nunca un salto instantaneo)
#define ATR_RAD_SMALL   26                 // radio con la tarjeta en el icono
#define ATR_RAD_FULL     4                 // radio a pantalla completa
#define ATR_FADE_P    0.35f                // por debajo de este progreso la saliente se desvanece

// (struct AppTrLayer se define en el bloque de tipos del principio del fichero:
//  el auto-prototipado del IDE de Arduino exige ver el tipo antes de la primera
//  funcion que lo use en su firma. Lo vigila tests/host/check_protos.py.)
static AppTrLayer gTrIn  = { false, -1, 0, 0, 72, 0, 0, 0, 0, 0, 0, -1 };   // app que ENTRA (crece)
static AppTrLayer gTrOut = { false, -1, 0, 0, 72, 0, 0, 0, 0, 0, 0, -1 };   // app que SALE (encoge)
static uint32_t   gTrGen = 0;             // generacion: invalida finalizaciones obsoletas
static bool       gTrEnterPending = false; // queda por correr el enter()/resume() de gTrIn.app
static uint32_t   gTrEnterGen = 0;
static uint8_t    gTrPrevLife = 0;         // ciclo de vida de gTrIn.app ANTES de abrirla
// Velocidad REAL del gesto de la barra al resolverse, en px/ms hacia arriba
// (>= 0; 0 = no vino de un gesto). La escribe handleiOSGestures y la lee
// appTrBeginClose para arrancar con el impulso del dedo en vez de con una
// duracion fija: es lo que evita el salto entre el seguimiento del gesto y la
// animacion que lo continua.
static float      gGbFireVel = 0;

// Hay capa visual de transicion en pantalla ahora mismo.
static bool appTrVisible(){ return gTrIn.on || gTrOut.on; }
// La transicion posee la pantalla: nadie mas compone bandas mientras dura.
static bool appTrOwnsScreen(){ return appTrVisible(); }

// Progreso de una capa en este instante. Ease-out cubico sobre el tramo que le
// queda: rapida al principio y frenado suave al final, en las dos direcciones.
static float appTrP(const AppTrLayer* L, uint32_t nowus){
  if(!L->on) return 0.0f;
  uint32_t el = nowus - L->t0us;                  // resta sin signo: inmune al desbordamiento
  if(L->durus == 0 || el >= L->durus) return L->p1;
  float u = (float)el / (float)L->durus;
  float e = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
  return L->p0 + (L->p1 - L->p0) * e;
}
// Rectangulo y radio de la tarjeta para un progreso dado. Respeta los TRES
// estilos de transicion que el usuario elige en Ajustes (gAnimStyle), los
// mismos que tenia winRevealAnim: 0 = zoom desde el icono, 1 = fundido a
// pantalla completa, 2 = deslizar desde el borde inferior. La interrupcion y la
// continuidad funcionan igual en los tres, porque todos son funcion de p.
static void appTrRect(const AppTrLayer* L, float p, int &x0, int &y0, int &x1, int &y1, int &rad){
  if(p < 0.0f) p = 0.0f; if(p > 1.0f) p = 1.0f;
  float q = 1.0f - p;
  if(gAnimStyle == 1){                       // FUNDIDO: siempre cubre la pantalla
    x0 = 0; y0 = 0; x1 = SCR_W; y1 = SCR_H; rad = 0; return;
  }
  if(gAnimStyle == 2){                       // DESLIZAR: sube desde abajo
    x0 = 0; x1 = SCR_W; y1 = SCR_H;
    y0 = (int)(SCR_H * q); rad = 0;
    if(y0 > SCR_H - 1) y0 = SCR_H - 1;
    return;
  }
  x0  = (int)(L->ix * q);                    // ZOOM: crece desde el icono
  y0  = (int)(L->iy * q);
  x1  = (int)((L->ix + L->is) * q + SCR_W * p);
  y1  = (int)((L->iy + L->is) * q + SCR_H * p);
  // El radio CRECE conforme la tarjeta encoge (es lo que se ve en el video:
  // casi una pildora cuando es pequena, el radio del panel cuando esta llena).
  rad = (int)(ATR_RAD_SMALL * q + ATR_RAD_FULL * p);
  if(x1 <= x0) x1 = x0 + 1;
  if(y1 <= y0) y1 = y0 + 1;
}
// Opacidad de una capa. En FUNDIDO la opacidad ES la animacion; en los otros dos
// la tarjeta es opaca y solo la SALIENTE se desvanece cuando ya es pequena, para
// que no se vea desaparecer de golpe sobre el escritorio.
static uint8_t appTrAlpha(float p, bool outgoing){
  if(p <= 0.0f) return 0;
  if(gAnimStyle == 1) return (uint8_t)(255.0f * (p > 1.0f ? 1.0f : p));
  if(!outgoing) return 255;
  if(p >= ATR_FADE_P) return 255;
  return (uint8_t)(255.0f * p / ATR_FADE_P);
}
// Arranca (o RE-DIRIGE) una capa hacia 'target', partiendo del progreso visual
// que tenga ahora mismo. La duracion se escala con la distancia que queda para
// que la velocidad no cambie al re-dirigir.
static void appTrAim(AppTrLayer* L, float target, uint32_t baseMs){
  uint32_t now = (uint32_t)micros();
  float cur = L->on ? appTrP(L, now) : (target >= 0.5f ? 0.0f : 1.0f);
  if(cur < 0.0f) cur = 0.0f; if(cur > 1.0f) cur = 1.0f;
  float dist = target - cur; if(dist < 0) dist = -dist;
  uint32_t ms = (uint32_t)(baseMs * dist);
  if(ms < ATR_MIN_MS) ms = ATR_MIN_MS;
  L->p0 = cur; L->p1 = target;
  L->t0us = now; L->durus = ms * 1000u;
  L->on = true;
}
// Congela una capa en su estado visual actual (deja de avanzar, sigue visible).
static void appTrFreeze(AppTrLayer* L){
  if(!L->on) return;
  uint32_t now = (uint32_t)micros();
  float cur = appTrP(L, now);
  L->p0 = cur; L->p1 = cur; L->t0us = now; L->durus = 0;
}

// #############################################################
// ##  NAVEGACION INFERIOR DEL SISTEMA  ·  un solo propietario
// ##  ------------------------------------------------------
// ##  Los tres botones (flecha / circulo / cuadrado) los dibuja y los
// ##  atiende EL SISTEMA, nunca la app. La banda de NAV_H pixeles de
// ##  abajo esta reservada: WIN_BOT ya la descontaba, el teclado de
// ##  Notas se sube por encima de ella (kbBotReserve) y Paint y Camara
// ##  suben su fila de herramientas. Ninguna app dibuja ahi.
// ##
// ##  POR QUE SE ESTAMPA DENTRO DE flxFlush: es el UNICO punto por el
// ##  que todo acaba llegando al panel. Estampando ahi (igual que hace
// ##  el candado del Modo Kiosco) la barra no puede quedar tapada por
// ##  el repintado de una app, no parpadea aunque la app refresque su
// ##  banda inferior en cada cuadro, y no hace falta acordarse de
// ##  redibujarla en ninguna ruta nueva.
// #############################################################
#define NAV_H       64                       // franja reservada al sistema (modo "Botones")
#define NAV_PRESS_MS 130                     // cuanto sigue viendose el destello tras soltar
static int      gNavPress = -1;              // boton HELD ahora mismo (0 atras · 1 inicio · 2 recientes)
static int      gNavGlow  = -1;              // boton que muestra el destello (sobrevive al soltar)
static uint32_t gNavGlowMs = 0;

static int  navBarH(){ return (gNavMode == 0) ? NAV_H : 0; }
// La barra existe cuando el sistema la posee: modo de botones, en una app, a
// pantalla completa y en portrait. En Modo Kiosco NO se dibuja a proposito: ahi
// no puede haber vias de escape (es exactamente la misma regla que ya aplican
// handleiOSGestures y activarMultitarea).
static bool navBarVisible(){
  if(gNavMode != 0) return false;
  if(gHosted || gLand) return false;
  if(KIOSK_ON && kioskOn) return false;
  if(gState != ST_APP) return false;
  // Una app LANDSCAPE (Juegos) es inmersiva: dibuja con las coordenadas
  // giradas y tiene su propia salida. Estampar aqui una barra portrait daria un
  // destello con el cuadro de la animacion de apertura, antes de que la app
  // ponga gLand por su cuenta.
  if(APP_REG[gAppId].flags & APP_LAND) return false;
  return true;
}
static int  navBarTop(){ return SCR_H - NAV_H; }
static uint16_t navBgCol(){  return gDark ? rgb565(13,15,22)    : rgb565(238,241,247); }
static uint16_t navFgCol(){  return gDark ? rgb565(232,236,245) : rgb565(44,48,60); }
static uint16_t navLineCol(){return gDark ? rgb565(30,34,46)    : rgb565(214,219,228); }

// Pinta la barra en el buffer activo. No vuelca: quien la llama decide.
static void navBarPaint(){
  int ny = SCR_H - 52, top = navBarTop();
  fillRect(0, top, SCR_W, NAV_H, navBgCol());
  fillRect(0, top, SCR_W, 1, navLineCol());
  uint16_t fg = navFgCol();
  // Destello de pulsacion: circulo tenue bajo el boton tocado. Se ve mientras el
  // dedo sigue encima y NAV_PRESS_MS mas despues de soltar, para que un toque
  // rapido tambien deje senal. Es lo unico "animado" de la barra y su tiempo
  // sale de millis(), no de un contador de cuadros.
  if(gNavGlow >= 0 && (gNavPress == gNavGlow || (millis() - gNavGlowMs) < NAV_PRESS_MS)){
    int cxs[3] = { SCR_W / 6, SCR_W / 2, SCR_W * 5 / 6 };
    fillCircleA(cxs[gNavGlow], ny + 8, 24, fg, 46);
  }
  int bx = SCR_W / 6;
  fillTriangle(bx - 10, ny + 8, bx + 8, ny - 2, bx + 8, ny + 18, fg);          // atras
  drawCircle(SCR_W / 2, ny + 8, 12, fg); drawCircle(SCR_W / 2, ny + 8, 11, fg); // inicio
  drawRoundRect(SCR_W * 5 / 6 - 11, ny - 3, 22, 22, 4, fg);                     // recientes
}

// Estampado dentro de flxFlush (ver el bloque de arriba). Solo toca fb, solo si
// la banda que se va a publicar cruza la franja de la barra.
static void navStampBar(int y0, int y1){
  if(!navBarVisible()) return;
  if(y1 < navBarTop() || y0 > SCR_H - 1) return;
  uint16_t* ob = gBuf; bool wl = gLand;
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  gLand = false;
  gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = navBarTop(); gClipY1 = SCR_H - 1;
  setBuf(fb);
  navBarPaint();
  setBuf(ob); gLand = wl;
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;
}

// Corta el episodio tactil en curso. Se llama en CADA transicion de pantalla:
// sin esto, el dedo que sigue apoyado despues de pulsar "inicio" genera un
// pressed/tap nuevo en la pantalla que acaba de entrar -- el toque fantasma.
static void touchDropAll(){
  T.pressed = T.released = T.tap = false;
  T.swipeUp = T.swipeDown = T.swipeLeft = T.swipeRight = false;
  T.down = false; T.moved = false;
  T.startX = T.x; T.startY = T.y;
  T.downMs = T.lastMs = millis();
  gTouchSwallow = true;              // ...y se ignora hasta que el dedo se levante de verdad
  gNavPress = -1;
  gNavGlow  = -1;
}

// #############################################################
// ##  CICLO DE VIDA DE APLICACIONES
// ##  ------------------------------------------------------
// ##  Cuatro estados por app (AppLife) y un contrato opcional por app
// ##  (AppHooks). El ESP32-P4 no simula multitarea ilimitada: solo UNA
// ##  app corre a la vez; las demas conservan su ESTADO LOGICO y dejan
// ##  de recibir tick() y toques, que es exactamente lo que hace que
// ##  suspender no cueste ni CPU ni cuadros.
// ##
// ##  NO se guarda un framebuffer por app: lo unico grafico que
// ##  sobrevive a la suspension es la miniatura de 150x250 de la
// ##  tarjeta de Recientes, y ni siquiera todas -- solo las
// ##  SW_THUMB_MAX mas recientes (ver swThumbTrim). Guardar una
// ##  captura de 480x800 por app serian 768 KB cada una.
// #############################################################
// YA NO HAY TOPE FIJO DE APPS SUSPENDIDAS. Antes eran cinco (mas la de primer
// plano) porque la lista de tarjetas tenia seis huecos. Ahora cabe una tarjeta
// por app y quien decide es el PRESUPUESTO DE MEMORIA MEDIDO
// (appEnforceMemoryBudget): el usuario puede tener abiertas tantas como quepan
// de forma segura, y lo que se recorta primero son recursos reconstruibles, no
// sesiones.
#define SESS_IDLE_MS        1200     // inactividad breve que dispara el guardado diferido
#define SESS_MAXWAIT_MS     30000    // tope duro: nunca mas de 30 s con cambios sin escribir

static uint8_t  gAppState[APP_N];       // AppLife de cada app (indice de APP_REG)
static uint32_t gAppSeenMs[APP_N];      // millis de la ultima vez en primer plano (para desalojar)
static int8_t   gSessDirtyApp   = -1;
static uint32_t gSessDirtyMs    = 0, gSessDirtyFirstMs = 0;

static const AppHooks* appHooks(int id){ return (id >= 0 && id < APP_N) ? APP_REG[id].hooks : NULL; }

// LISTA BLANCA de segundo plano. Una app solo cuenta como "no se puede tocar"
// si (a) esta declarada en la lista (APP_BG_KEEP) y (b) su propio hook dice que
// TIENE trabajo en curso ahora mismo. Sin trabajo real se cierra como las demas:
// la lista blanca no es un salvoconducto permanente.
static bool appBgBusy(int id){
  if(id < 0 || id >= APP_N) return false;
  if(!(APP_REG[id].flags & APP_BG_KEEP)) return false;
  const AppHooks* h = appHooks(id);
  return h && h->bgWork && h->bgWork();
}

// Marca por app de "el archivo de sesion esta desfasado". Evita reescribir en
// flash una sesion identica cada vez que se pulsa Inicio: sin esta puerta, salir
// y entrar de una app repetidamente escribiria en flash cada vez sin que hubiera
// cambiado nada. Es la medida directa contra el desgaste de la flash.
static bool gSessNeedSave[APP_N];
static bool appSaveSession(int id){
  if(id < 0 || id >= APP_N) return true;
  const AppHooks* h = appHooks(id);
  if(!h || !h->saveSess) return true;        // la app no tiene nada que guardar: exito trivial
  if(!gSessNeedSave[id]) return true;        // nada ha cambiado desde el ultimo volcado
  bool ok = h->saveSess();
  if(ok) gSessNeedSave[id] = false;
  return ok;
}
// RESTAURACION PEREZOSA. La sesion de disco de cada app se lee la primera vez
// que esa app se abre, no en el arranque: asi el arranque no paga la lectura
// (ni la reserva de buffers) de apps que quiza no se abran en toda la sesion.
static bool gSessLoaded[APP_N];
static void appLoadSessionOnce(int id){
  if(id < 0 || id >= APP_N || gSessLoaded[id]) return;
  gSessLoaded[id] = true;
  const AppHooks* h = appHooks(id);
  if(h && h->loadSess) h->loadSess();
}
// GUARDADO DIFERIDO. Las apps llaman aqui cuando cambian algo; no se escribe en
// ese momento. El volcado real ocurre tras SESS_IDLE_MS sin actividad (o al
// llegar al tope de SESS_MAXWAIT_MS), y nunca con el dedo apoyado. Eso es lo
// que impide una escritura en flash por letra, por pincelada o por cuadro.
static void sessMarkDirty(int id){
  if(id < 0 || id >= APP_N) return;
  if(gSessDirtyApp != id && gSessDirtyApp >= 0) appSaveSession(gSessDirtyApp);  // otra app pendiente: se cierra ya
  if(gSessDirtyApp != id){ gSessDirtyApp = (int8_t)id; gSessDirtyFirstMs = millis(); }
  gSessNeedSave[id] = true;
  gSessDirtyMs = millis();
}
static void sessFlushNow(){
  if(gSessDirtyApp < 0) return;
  int id = gSessDirtyApp; gSessDirtyApp = -1;
  appSaveSession(id);
}
static void sessAutosaveTick(){
  if(gSessDirtyApp < 0) return;
  if(gFrPending) return;                     // restablecimiento en curso: ninguna escritura normal
  if(T.down) return;                         // con el dedo apoyado manda la latencia tactil
  uint32_t now = millis();
  if(now - gSessDirtyMs < SESS_IDLE_MS && now - gSessDirtyFirstMs < SESS_MAXWAIT_MS) return;
  sessFlushNow();
}

static bool appTerminate(int id, bool force){
  if(id < 0 || id >= APP_N) return true;
  if(gAppState[id] == ALIFE_CLOSED) return true;
  const AppHooks* h = appHooks(id);
  if(gSessDirtyApp == id) gSessDirtyApp = -1;      // se guarda aqui abajo, no por el camino diferido
  bool saved = appSaveSession(id);
  // No se pudo guardar (p.ej. LittleFS lleno) y el cierre no es forzado: la app
  // NO se cierra y su contenido sigue vivo en RAM. Perder el trabajo del usuario
  // en silencio para "liberar recursos" no es una opcion.
  if(!saved && !force) return false;
  // El close() de una app puede tardar: el del navegador espera a que su tarea
  // de red muera de verdad (hasta 3 s). loopTask esta suscrito al Task Watchdog
  // y solo lo alimenta una vez por vuelta, asi que se le da de comer a ambos
  // lados de la llamada.
  flexFeedWdt();
  if(h && h->close) h->close();
  flexFeedWdt();
  gAppState[id] = ALIFE_CLOSED;
  gAppSeenMs[id] = 0;
  appMemForget(id);                 // su huella medida deja de existir con ella
  return true;
}

static int appSuspendedCount(){
  int n = 0;
  for(int i = 0; i < APP_N; i++) if(gAppState[i] == ALIFE_SUSPENDED) n++;
  return n;
}
