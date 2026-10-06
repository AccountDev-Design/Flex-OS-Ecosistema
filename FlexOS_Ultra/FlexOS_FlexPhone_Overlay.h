// #############################################################
// ##  FLEX OS ULTRA  ·  CENTRO DE NOTIFICACIONES Y BANNER
// ##  ----------------------------------------------------------
// ##  Dos overlays del SISTEMA y un modo No molestar de verdad:
// ##
// ##    · CENTRO DE NOTIFICACIONES -- borde izquierdo. Historial
// ##      completo: lo que llega del telefono Y lo que genera el
// ##      propio Flex OS, en la misma lista.
// ##
// ##    · BANNER FLOTANTE -- la notificacion que ACABA de llegar,
// ##      encima de lo que haya, tambien dentro de un juego.
// ##
// ##    · NO MOLESTAR -- un estado real, con su interruptor en el
// ##      panel rapido. No una casilla que no hace nada.
// ##
// ##  POR QUE NO ES LA ISLA DINAMICA
// ##  ----------------------------------------------------------
// ##  La isla (FlexOS_Ultra_Notif.h) solo vive en el escritorio:
// ##  compone sobre homeBuf, que solo es un fondo valido ahi. Una
// ##  notificacion del telefono tiene que verse ESTES DONDE ESTES,
// ##  asi que este modulo sigue el patron del aviso de caida y de
// ##  la tarjeta del cronometro: captura la banda que va a ocupar,
// ##  dibuja encima y la devuelve pixel a pixel al cerrarse.
// ##
// ##  LA DIFERENCIA CON EL AVISO DE CAIDA, Y ES LA IMPORTANTE:
// ##  este banner NO ES MODAL. No se queda la pantalla, no para la
// ##  app de debajo y no le roba el toque salvo que el usuario
// ##  toque el banner. Un juego sigue corriendo mientras el banner
// ##  entra, se ve y se va.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente.
// ##
// ##  Y NO es un eslabon de la cadena Types -> ... -> Recovery, por
// ##  eso no se llama FlexOS_Ultra_*. Lee el modelo de Flex Phone y
// ##  reutiliza sus componentes de interfaz, asi que tiene que ir
// ##  DESPUES del puente -- igual que FlexOS_FlexPhone_UI.h. Se
// ##  incluye una sola vez, junto a los puentes, al final del .ino.
// #############################################################
#pragma once

// =============================================================
//  1) NO MOLESTAR
// =============================================================
// Estado REAL y persistido. Silencio (flexAudioMuted) y No molestar
// son dos cosas distintas a proposito:
//
//   Silencio     -> sin sonido, pero el banner SI sale.
//   No molestar  -> ademas, NADA intrusivo: ni banner, ni sonido,
//                   ni vibracion.
//
// En los dos casos la notificacion SE REGISTRA igual y aparece en el
// Centro. Silenciar no es tirar.
static bool     gDnd = false;
#define FPN_NVS_DND "dnd"

static void phoneDndLoad(){
  Preferences p;
  if(p.begin("flexphone", true)){ gDnd = p.getBool(FPN_NVS_DND, false); p.end(); }
}
static void phoneDndSet(bool on){
  if(gDnd == on) return;
  gDnd = on;
  Preferences p;
  if(p.begin("flexphone", false)){ p.putBool(FPN_NVS_DND, on); p.end(); }
}

// ¿Se puede presentar algo INTRUSIVO ahora mismo?
// Es la unica puerta: si esto dice que no, no hay banner, no hay
// sonido y no hay vibracion. La notificacion se guarda igual.
static bool phoneCanInterrupt(){
  if(gDnd) return false;
  return true;
}
// ¿Y hacer ruido? El silencio del sistema manda aunque se pueda
// ensenar el banner.
static bool phoneCanSound(){
  return !gDnd && !flexAudioMuted();
}

// =============================================================
//  2) UNA ENTRADA DEL CENTRO
// =============================================================
// El Centro junta DOS fuentes: las notificaciones del telefono
// (fphModel) y los avisos del propio sistema. Se normalizan aqui
// para que la lista no tenga que saber de donde viene cada una.
enum { FPN_SRC_PHONE = 0, FPN_SRC_SYSTEM };

#define FPN_ENTRY_APP   32
#define FPN_ENTRY_TITLE 64
#define FPN_ENTRY_BODY  120
#define FPN_LIST_MAX    (FLP_NOTIF_MAX + NOTIF_MAX)

typedef struct {
  uint8_t  src;
  uint32_t id;          // id del telefono, o 0 en las del sistema
  int16_t  slot;        // indice en su origen, para poder descartarla
  char     app[FPN_ENTRY_APP];
  char     title[FPN_ENTRY_TITLE];
  char     body[FPN_ENTRY_BODY];
  uint32_t whenMs;      // millis() de recepcion: para ordenar
  uint8_t  pri;
  uint8_t  group;       // cuantas hay de la misma app
} FlexPhoneEntry;

// =============================================================
//  3) BANNER FLOTANTE
// =============================================================
// LA ULTIMA CAPA ANTES DEL PANEL
// -------------------------------------------------------------
// Un banner es una capa que flota SOBRE una pantalla que sigue viva: el escritorio, una app, la caja de
// aplicaciones, un video. La version anterior capturaba una banda de fb al armarse, la repintaba en cada
// cuadro desde esa copia y se la devolvia a fb al irse. Eso solo vale si lo de debajo NO cambia mientras
// el banner esta puesto, y casi nunca es asi:
//
//   · la caja de aplicaciones subia, repintaba su cabecera... y el banner seguia pegando encima las 98
//     filas del ESCRITORIO que habia capturado (el rectangulo partido de la foto), y al irse las dejaba;
//   · en una app que se anima, la banda quedaba congelada con el fondo viejo;
//   · el vidrio se calculaba como si debajo hubiera SIEMPRE el color de la pagina (drawGlassCardFlat con
//     TH_PAGE): sobre el escritorio o una foto salia un rectangulo oscuro, con la sombra opaca.
//
// AHORA EL BANNER NO TOCA fb. Su tarjeta (vidrio sobre lo que hay debajo + contenido) se resuelve UNA vez
// en un lienzo propio (fpbRender, en el bucle normal, con la pila entera) y se vuelve a resolver solo si lo
// de debajo repinta esas filas (como mucho cada FPB_REGLASS_MS). Cada vez que algo se manda al panel,
// flxFlush llama a fpbStampBegin: copia la tarjeta sobre las filas que van a salir, y fpbStampEnd devuelve
// a fb lo que tenia. fb NO guarda nunca el banner. Consecuencias, todas por construccion y no por parche:
//
//   · lo que hay debajo puede repintar, animarse o cambiar de pantalla: el banner siempre queda ENCIMA
//     de lo ultimo que se dibujo, y al irse el panel recibe esas filas LIMPIAS de fb;
//   · no hay copia que envejezca, ni restauracion de pixeles viejos, ni orden de dibujo que cuidar;
//   · quien manda de verdad sobre la pantalla (un modal, la cortina, el OTA, una transicion) apaga el banner
//     (fpbScreenAllows) y el panel se limpia en el mismo cuadro;
//   · un banner quieto no cuesta NADA: solo trabaja cuando alguien manda filas al panel que lo tocan.
//
// EL TACTIL. El banner es un overlay no modal: el toque es suyo SOLO si el dedo baja dentro de la tarjeta, y
// entonces el episodio entero (bajar, arrastrar, soltar) es suyo hasta que se levante -- y de nadie mas
// (touchHoldBack). Fuera de la tarjeta el toque es de la pantalla de debajo, como siempre.
// ---- Geometria ----------------------------------------------
// VERTICAL: banda compacta ARRIBA. Nunca en el centro: tapar el
// centro de la pantalla por un mensaje es lo que hace que la gente
// desactive las notificaciones.
#define FPB_V_X      14
#define FPB_V_W      (SCR_W - 28)
#define FPB_V_Y      18
#define FPB_V_H      72
// HORIZONTAL: coordenadas LOGICAS (lx 0..799, ly 0..479). Tambien
// arriba, y MAS ESTRECHO: en un juego apaisado, una banda a lo ancho
// se come la mitad util de la pantalla.
#define FPB_L_X      24
#define FPB_L_W      300
#define FPB_L_Y      14
#define FPB_L_H      64
#define FPB_RAD      18

#define FPB_IN_MS      220
#define FPB_OUT_MS     180
#define FPB_SPRING_MS  140
#define FPB_HOLD_MS    4200      // visible antes de irse solo
#define FPB_DRAG_PX    10        // hasta aqui el dedo solo tiembla; mas es un arrastre
#define FPB_FLING      0.7f      // px/ms: un lanzamiento que descarta aunque no llegue al umbral
#define FPB_FLING_MIN  40        // ...pero con al menos este recorrido
#define FPB_REGLASS_MS 80        // el vidrio se recalcula como mucho cada tanto si lo de debajo cambia
#define FPB_MIN_HOLD_MS 1500     // al volver tras perder la pantalla, como poco esto a la vista
#define FPB_SHADOW_A   60
#define FPB_MIN_MIX    150       // tinte minimo del vidrio: el texto se lee sobre cualquier cosa (el del visor)
#define FPB_SAVE_BYTES (80 * 1024)   // lo que fb tenia bajo la tarjeta durante UNA transferencia (<= 480x80x2)
// Cuantos avisos esperan turno. Al llenarse se resume: "y N mas".
// Una torre de tarjetas apiladas es justo lo que NO se quiere.
#define FPB_QUEUE    6

// #############################################################
// ##  PRESENTADOR UNICO DE AVISOS
// ##  ------------------------------------------------------
// ##  Este banner es el UNICO sitio que presenta avisos en
// ##  pantalla: los del telefono Y los del propio sistema. Antes
// ##  habia dos capas -- la isla (avisos del sistema, solo en el
// ##  escritorio, compuesta en bbuf) y este banner -- que no sabian
// ##  la una de la otra, asi que podian salir A LA VEZ en la misma
// ##  franja de arriba ("Google Play Store" encima de "Proteccion
// ##  contra robo restaurada"). Ahora la isla solo GUARDA (es el
// ##  modelo que leen el Centro, DeX y el widget) y presenta aqui.
// ##
// ##  REGLAS DE LA COLA
// ##    · Uno a la vez. Lo que llega mientras hay uno a la vista
// ##      espera: NADIE quita la pantalla al que se esta leyendo.
// ##    · Prioridad: SISTEMA > telefono urgente > telefono normal;
// ##      dentro de cada nivel, por orden de llegada.
// ##    · El mismo aviso del sistema (misma huella) no se repite: si
// ##      cambia su texto se actualiza donde este, a la vista o en
// ##      la cola.
// ##    · Si algo le quita la pantalla a medias (una transicion de
// ##      app, la cortina, un modal, girar la pantalla), el aviso NO
// ##      se pierde: vuelve AL FRENTE de la cola con el tiempo que le
// ##      quedaba, y reaparece -- con el vidrio de lo que haya debajo
// ##      ENTONCES y en la orientacion nueva -- en cuanto se puede.
// #############################################################
enum { FPB_HIDDEN = 0, FPB_ARMED, FPB_IN, FPB_SHOWN, FPB_OUT };

typedef struct {
  char     app[FPN_ENTRY_APP];
  char     title[FPN_ENTRY_TITLE];
  char     body[FPN_ENTRY_BODY];
  uint32_t id;
  uint32_t key;          // huella de un aviso del sistema (0 = sin huella)
  uint32_t shownMs;      // lo que ya estuvo a la vista antes de volver a la cola
  uint8_t  src;
  uint8_t  pri;
  uint8_t  icon;         // 1 + ModuleType de un aviso del sistema; 0 = sin icono
} FlexPhoneBannerMsg;

static int      fpbState = FPB_HIDDEN;
static uint32_t fpbT0 = 0;
static bool     fpbLand = false;
static float    fpbSlide = 0.0f;        // desplazamiento horizontal de la tarjeta (px logicos)
static float    fpbDrop = 0.0f;         // cuanto esta POR ENCIMA de su sitio (entrada y salida)
static int      fpbOutDir = 0;          // salida: 0 = sube y se va, -1 / +1 = sale deslizandose
static float    fpbOutFrom = 0.0f;
static bool     fpbSpring = false;      // no llego al umbral: vuelve a su sitio
static float    fpbSpringFrom = 0.0f;
static uint32_t fpbSpringT0 = 0;
static bool     fpbPaintWanted = false; // hay que mandar sus filas al panel en esta vuelta
static bool     fpbRefresh = false;     // el aviso a la vista cambio de texto: se resuelve otra vez
static FlexPhoneBannerMsg fpbCur;
static FlexPhoneBannerMsg fpbQueue[FPB_QUEUE];
static int      fpbQueueN = 0;
static uint32_t fpbMore = 0;            // cuantas se resumieron

// El gesto en curso: el dedo bajo DENTRO de la tarjeta. Es del banner hasta que se levante.
static bool     fpbGesture = false;
static bool     fpbGMoved = false;
static int      fpbGx0 = 0, fpbGLx = 0;
static uint32_t fpbGMs = 0;
static float    fpbGvx = 0.0f;

// Pixeles. Se piden al armarse y se sueltan al irse: el banner es excepcional, y retener memoria
// permanentemente por algo que puede no ocurrir en horas no se sostiene.
static uint16_t* fpbCv = NULL;          // la tarjeta ya resuelta, en su sitio de reposo, disposicion LOGICA (stride SCR_W)
static uint16_t* fpbSave = NULL;        // lo que fb tenia bajo la tarjeta mientras dura una transferencia
static bool     fpbLive = false;        // el banner se puede estampar ahora
static bool     fpbStamping = false;    // entre fpbStampBegin y fpbStampEnd
static bool     fpbOnPanel = false;     // el panel tiene (o pudo tener) pixeles del banner
static bool     fpbSuppressed = false;  // una pantalla de debajo tomo el control
static bool     fpbCleanNeed = false;   // hay que devolver al panel sus filas limpias
static bool     fpbOwnFlush = false;    // la transferencia en curso es del propio banner
static bool     fpbUnderDirty = false;  // lo de debajo repinto esas filas: el vidrio puede estar viejo
static uint32_t fpbCvMs = 0;
static int      fpbSavX = 0, fpbSavY = 0, fpbSavW = 0, fpbSavH = 0;
static int      fpbLastY0 = 0, fpbLastY1 = -1;   // filas FISICAS de la ultima tarjeta mandada al panel

static void fpbFreeBufs(){
  if(fpbCv){ free(fpbCv); fpbCv = NULL; }
  if(fpbSave){ free(fpbSave); fpbSave = NULL; }
}
static inline bool fpbVisible(){ return fpbState != FPB_HIDDEN; }

static inline int fpbCardW(){ return fpbLand ? FPB_L_W : FPB_V_W; }
static inline int fpbCardH(){ return fpbLand ? FPB_L_H : FPB_V_H; }
static inline int fpbRestX(){ return fpbLand ? FPB_L_X : FPB_V_X; }
static inline int fpbRestY(){ return fpbLand ? FPB_L_Y : FPB_V_Y; }
static inline int fpbDropMax(){ return fpbRestY() + fpbCardH() + 10; }
static inline int fpbCardX(){ return fpbRestX() + (int)fpbSlide; }
static inline int fpbCardY(){ return fpbRestY() - (int)fpbDrop; }
// Pixel LOGICO <-> pixel FISICO de fb (en horizontal la x fisica es (SCR_W-1)-ly y la y fisica es lx).
static inline void fpbToPhys(int lx, int ly, int &px, int &py){
  if(fpbLand){ px = (SCR_W - 1) - ly; py = lx; } else { px = lx; py = ly; }
}
static inline void fpbToLogical(int px, int py, int &lx, int &ly){
  if(fpbLand){ lx = py; ly = (SCR_W - 1) - px; } else { lx = px; ly = py; }
}
// Rectangulo FISICO de la tarjeta con su sombra (y un margen de antialias), acotado a la pantalla.
static bool fpbPhysRect(int &x0, int &y0, int &x1, int &y1){
  int ax, ay, bx, by;
  fpbToPhys(fpbCardX() - 1, fpbCardY() - 1, ax, ay);
  fpbToPhys(fpbCardX() + fpbCardW() + 4, fpbCardY() + fpbCardH() + 5, bx, by);
  x0 = ax < bx ? ax : bx; x1 = ax < bx ? bx : ax;
  y0 = ay < by ? ay : by; y1 = ay < by ? by : ay;
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > SCR_W - 1) x1 = SCR_W - 1;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
  return x1 >= x0 && y1 >= y0;
}
// ¿El punto FISICO (px,py) cae dentro de la tarjeta (sin la sombra)?
static bool fpbInsidePhys(int px, int py){
  int lx, ly; fpbToLogical(px, py, lx, ly);
  return lx >= fpbCardX() && lx < fpbCardX() + fpbCardW() && ly >= fpbCardY() && ly < fpbCardY() + fpbCardH();
}

// #############################################################
// ##  ¿SE PUEDE DIBUJAR EL BANNER AHORA MISMO?
// ##  ------------------------------------------------------
// ##  Las mismas pantallas en las que el sistema ya decide no
// ##  notificar nada, mas las que poseen la pantalla en exclusiva
// ##  (un modal, la cortina, el Centro, una transicion), mas DeX.
// ##  La pregunta se hace en DOS sitios: al armarse (fpbTick) y en
// ##  cada transferencia al panel (fpbStampBegin): si alguien se
// ##  queda la pantalla a mitad de banner, ese mismo cuadro ya sale
// ##  sin el.
// ##
// ##  DeX ES UNA EXCLUSION DURA. Con el escritorio de Modo PC
// ##  delante, el usuario esta trabajando con ventanas, teclado y
// ##  raton: un banner del telefono encima de eso es una
// ##  interrupcion desproporcionada, y ademas tapa ventanas que el
// ##  usuario ha colocado. La notificacion se registra igual y
// ##  esta en el Centro.
// #############################################################
static bool fpbDexActive(){
  return (gState == ST_APP && gAppId == IC_MODOPC) || gHosted;
}
static bool fpcBusy();                    // el Centro de notificaciones esta a la vista o moviendose (seccion 4)
static bool fpbScreenAllows(){
  if(notifSecureScreen()) return false;           // arranque, OOBE, bloqueo, apagado
  if(gFrPending || gState == ST_FACTORY || gSafeMode) return false;
  if(flexOtaOwnsScreen() || optActive()) return false;
  if(fpbDexActive()) return false;                // DeX: se registra, no se dibuja
  if(gSuspOn) return false;                       // pantalla apagada: no se pinta a oscuras
  if(appTrOwnsScreen()) return false;             // transicion de app dibujando
  if(faVisible() || cronoCardVisible() || spaVisible()) return false;   // ya hay un modal encima
  if(qsPanelY != 0 || qsAnimOn || qsDragging) return false;   // la cortina dibuja encima
  if(fpcBusy()) return false;                     // el Centro es dueno de la pantalla
  if(gState == ST_HOMECFG) return false;          // Personalizar inicio: ningun overlay compite con el modo
  return true;
}
// Un aviso del TELEFONO respeta No molestar; uno del propio sistema (el resultado de algo que el usuario
// acaba de pedir) no: no es una interrupcion, es la respuesta a su toque.
static bool fpbCanShow(uint8_t src){
  if(src == FPN_SRC_PHONE && !phoneCanInterrupt()) return false;
  return fpbScreenAllows();
}

// -------------------------------------------------------------
//  Encolar (ver PRESENTADOR UNICO DE AVISOS)
// -------------------------------------------------------------
// Nivel de prioridad: cuanto MAS alto, antes sale.
static inline int fpbRank(const FlexPhoneBannerMsg* m){
  if(m->src == FPN_SRC_SYSTEM) return 2;
  return m->pri >= FLP_PRI_HIGH ? 1 : 0;
}
// Mete un aviso en la cola ORDENADA por prioridad. `front` = delante de los de
// su nivel (el aviso que se estaba viendo y vuelve a la cola); si no, detras
// de los de su nivel (por orden de llegada). Llena: si hay algo de MENOS
// prioridad, sale el mas nuevo de ellos -- sigue en el Centro y se cuenta en el
// "+N" --; si no, el que se cuenta es el nuevo.
static void fpbEnqueue(const FlexPhoneBannerMsg* m, bool front){
  const int r = fpbRank(m);
  if(fpbQueueN >= FPB_QUEUE){
    int victim = -1;
    for(int i = fpbQueueN - 1; i >= 0; i--) if(fpbRank(&fpbQueue[i]) < r){ victim = i; break; }
    if(victim < 0 && !front){ fpbMore++; return; }
    if(victim < 0) victim = fpbQueueN - 1;           // el que vuelve tiene preferencia sobre el ultimo
    for(int i = victim; i < fpbQueueN - 1; i++) fpbQueue[i] = fpbQueue[i + 1];
    fpbQueueN--;
    fpbMore++;
  }
  int at = 0;
  if(front){ while(at < fpbQueueN && fpbRank(&fpbQueue[at]) > r) at++; }
  else     { for(int i = 0; i < fpbQueueN; i++) if(fpbRank(&fpbQueue[i]) >= r) at = i + 1; }
  for(int i = fpbQueueN; i > at; i--) fpbQueue[i] = fpbQueue[i - 1];
  fpbQueue[at] = *m;
  fpbQueueN++;
}
// Dos avisos del sistema son el MISMO si traen la misma huella (o, sin huella,
// el mismo texto).
static bool fpbSameSys(const FlexPhoneBannerMsg* a, const FlexPhoneBannerMsg* b){
  if(a->key && b->key) return a->key == b->key;
  return !strcmp(a->title, b->title) && !strcmp(a->body, b->body);
}
static void fpbOffer(const FlexPhoneBannerMsg* n){
  if(n->src == FPN_SRC_SYSTEM){
    // El MISMO aviso del sistema ya a la vista o esperando (dos toques seguidos sobre lo mismo, un estado
    // que se actualiza) no se repite: la respuesta es UNA, no una torre de tarjetas iguales. Si cambio el
    // texto, se actualiza donde este.
    if(fpbState != FPB_HIDDEN && fpbState != FPB_OUT && fpbCur.src == FPN_SRC_SYSTEM && fpbSameSys(&fpbCur, n)){
      if(strcmp(fpbCur.title, n->title) || strcmp(fpbCur.body, n->body)){
        memcpy(fpbCur.title, n->title, sizeof(fpbCur.title));
        memcpy(fpbCur.body,  n->body,  sizeof(fpbCur.body));
        fpbRefresh = true;
      }
      return;
    }
    for(int i = 0; i < fpbQueueN; i++)
      if(fpbQueue[i].src == FPN_SRC_SYSTEM && fpbSameSys(&fpbQueue[i], n)){
        memcpy(fpbQueue[i].title, n->title, sizeof(fpbQueue[i].title));
        memcpy(fpbQueue[i].body,  n->body,  sizeof(fpbQueue[i].body));
        return;
      }
  }
  fpbEnqueue(n, false);
}
static void fpbMsgInit(FlexPhoneBannerMsg* n, uint8_t src, uint32_t id, const char* app,
                       const char* title, const char* body, uint8_t pri){
  memset(n, 0, sizeof(*n));
  n->src = src;
  n->id = id;
  n->pri = pri;
  flexLinkUtf8Copy(n->app,   sizeof(n->app),   app   ? app   : "");
  flexLinkUtf8Copy(n->title, sizeof(n->title), title ? title : "");
  flexLinkUtf8Copy(n->body,  sizeof(n->body),  body  ? body  : "");
}
static void fpbPush(uint8_t src, uint32_t id, const char* app,
                    const char* title, const char* body, uint8_t pri){
  FlexPhoneBannerMsg n;
  fpbMsgInit(&n, src, id, app, title, body, pri);
  fpbOffer(&n);
}
// Un aviso del propio sistema (no viene del telefono): la misma capa, el mismo material.
static void fpbPushSystem(const char* app, const char* title, const char* body){
  fpbPush(FPN_SRC_SYSTEM, 0, app, title, body, FLP_PRI_DEFAULT);
}
// Un aviso del sistema que viene del MODELO de avisos (notifPush): con su huella
// -- el mismo aviso no se repite y se actualiza en su sitio -- y su icono.
static void fpbPushSystemKeyed(uint32_t key, uint8_t type, const char* title, const char* body){
  FlexPhoneBannerMsg n;
  fpbMsgInit(&n, FPN_SRC_SYSTEM, 0, "", title, body, FLP_PRI_DEFAULT);
  n.key = key;
  n.icon = (uint8_t)(1 + type);
  fpbOffer(&n);
}

// -------------------------------------------------------------
//  Dibujo: la tarjeta se resuelve en su lienzo
// -------------------------------------------------------------
// El contenido, con las coordenadas del propio lienzo (la tarjeta empieza en 0,0).
static void fpbDrawContent(int w, int h){
  // Barra de prioridad: se ve de un vistazo si es urgente sin leer.
  const uint16_t accent = (fpbCur.pri >= FLP_PRI_HIGH) ? TH_PRIM : TH_DIV;
  fillRoundRect(8, 12, 4, h - 24, 2, accent);
  int tx = 22;
  // Avisos del sistema: su icono, como los tenia la isla (sistema o multimedia).
  if(fpbCur.icon){
    const int is = h - 28 < 40 ? h - 28 : 40;
    drawModuleIcon((ModuleType)(fpbCur.icon - 1), 20, (h - is) / 2, is);
    tx = 20 + is + 12;
  }
  const int tw = w - tx - 18;
  fgTextEllipsis(tx, 10, tw, fpbCur.app[0] ? fpbCur.app : "Flex OS", 1, TH_MUTE);
  fgTextEllipsis(tx, 28, tw, fpbCur.title, 2, TH_TXT);
  if(fpbCur.body[0]) fgTextEllipsis(tx, 50, tw, fpbCur.body, 1, TH_TXT2);
  // Resumen de lo que espera. NO se apilan tarjetas.
  const int pend = fpbQueueN + (int)fpbMore;
  if(pend > 0){
    char more[24];
    snprintf(more, sizeof(more), "+%d", pend);
    drawTextR(w - 14, 10, more, 1, TH_MUTE);
  }
}

// Resuelve la tarjeta: vidrio -- el material del sistema, el MISMO que el resto de overlays -- sobre lo que
// fb tiene AHORA donde va a caer, mas el contenido. Corre en el bucle normal (nunca dentro de una
// transferencia) y como mucho cada FPB_REGLASS_MS.
static void fpbRender(){
  if(!fpbCv) return;
  const int w = fpbCardW(), h = fpbCardH(), x0 = fpbRestX(), y0 = fpbRestY();
  // 1) El fondo: el contenido LIMPIO de la pantalla de debajo (fb nunca guarda el banner).
  for(int j = 0; j < h; j++){
    uint16_t* d = fpbCv + (size_t)j * SCR_W;
    if(!fpbLand && (unsigned)(y0 + j) < (unsigned)SCR_H && x0 >= 0 && x0 + w <= SCR_W){
      memcpy(d, fb + (size_t)(y0 + j) * SCR_W + x0, (size_t)w * 2);
      continue;
    }
    for(int i = 0; i < w; i++){
      int px, py; fpbToPhys(x0 + i, y0 + j, px, py);
      d[i] = ((unsigned)px < (unsigned)SCR_W && (unsigned)py < (unsigned)SCR_H) ? fb[(size_t)py * SCR_W + px] : (uint16_t)0;
    }
  }
  // 2) El material, sobre ese lienzo y SIN las bandas pre-desenfocadas de otros duenos: el vidrio del banner
  //    se desenfoca sobre lo que hay debajo ahora, no sobre la banda cacheada de un menu o de una tarjeta.
  uint16_t* ob = gBuf; const bool wl = gLand;
  const int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
  const uint16_t* bd = gGlBd; const int rec = gGlRecSlot; const int band = uiGlBandY1; const uint8_t mm = gGlMinMix;
  gGlBd = NULL; gGlRecSlot = -1; uiGlBandY1 = -1; gGlMinMix = FPB_MIN_MIX;
  gBuf = fpbCv; gLand = false;
  gClipY0 = 0; gClipY1 = h - 1; gClipX0 = 0; gClipX1 = w - 1;
  uiSurface(0, 0, w, h, FPB_RAD, UIS_ELEVATED);
  gGlMinMix = mm; gGlBd = bd; gGlRecSlot = rec; uiGlBandY1 = band;
  // 3) El contenido.
  fpbDrawContent(w, h);
  gBuf = ob; gLand = wl;
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
  fpbCvMs = millis() | 1u;
  fpbUnderDirty = false;
}

// Mezcla de un pixel LOGICO de fb, acotado a las filas fisicas [a0,a1].
static inline void fpbMixPx(int lx, int ly, uint16_t col, uint8_t a, int a0, int a1){
  int px, py; fpbToPhys(lx, ly, px, py);
  if((unsigned)px >= (unsigned)SCR_W || py < a0 || py > a1) return;
  uint16_t* d = fb + (size_t)py * SCR_W + px;
  *d = mix565(*d, col, a);
}
// Estampa la sombra y la tarjeta en fb, SOLO en las filas fisicas [a0,a1].
static void fpbBlit(int a0, int a1){
  const int w = fpbCardW(), h = fpbCardH(), cx = fpbCardX(), cy = fpbCardY();
  const uint16_t sh = TH_SHADOW;
  // Sombra corta (3,4) con alpha: despega la tarjeta del fondo sin ser un rectangulo opaco. Primero, y solo
  // la parte que la tarjeta no tapa.
  for(int j = 4; j < h + 4; j++){
    const int ly = cy + j;
    if(!fpbLand && (ly < a0 || ly > a1)) continue;
    const int sIns = glInset(j - 4, h, FPB_RAD);
    int s0 = cx + 3 + sIns, s1 = cx + 3 + w - 1 - sIns;
    int c0 = 1, c1 = 0;                                   // (vacio)
    if(j < h){ const int cIns = glInset(j, h, FPB_RAD); c0 = cx + cIns; c1 = cx + w - 1 - cIns; }
    for(int lx = s0; lx <= s1; lx++){
      if(lx >= c0 && lx <= c1){ lx = c1; continue; }
      fpbMixPx(lx, ly, sh, FPB_SHADOW_A, a0, a1);
    }
  }
  // La tarjeta, dentro de su forma (las esquinas redondeadas del propio vidrio).
  for(int j = 0; j < h; j++){
    const int ly = cy + j;
    if(!fpbLand && (ly < a0 || ly > a1)) continue;
    const int ins = glInset(j, h, FPB_RAD);
    const uint16_t* src = fpbCv + (size_t)j * SCR_W;
    if(!fpbLand){
      if(ly < 0 || ly >= SCR_H) continue;
      int xs = cx + ins, xe = cx + w - 1 - ins;
      if(xs < 0) xs = 0;
      if(xe > SCR_W - 1) xe = SCR_W - 1;
      if(xs > xe) continue;
      memcpy(fb + (size_t)ly * SCR_W + xs, src + (xs - cx), (size_t)(xe - xs + 1) * 2);
      continue;
    }
    for(int i = ins; i < w - ins; i++){
      int px, py; fpbToPhys(cx + i, ly, px, py);
      if((unsigned)px >= (unsigned)SCR_W || py < a0 || py > a1) continue;
      fb[(size_t)py * SCR_W + px] = src[i];
    }
  }
}

// ---- Los dos extremos de la transferencia (los llama flxFlush) ----------
static bool fpbStampBegin(int y0, int y1){
  if(!fpbLive || fpbStamping || !fpbCv || !fpbSave) return false;
  if(!fpbScreenAllows() || gLand != fpbLand){
    // Alguien tomo la pantalla (o la giro): el banner se apaga YA y el panel recupera sus filas limpias.
    fpbLive = false; fpbSuppressed = true;
    if(fpbOnPanel) fpbCleanNeed = true;
    return false;
  }
  int rx0, ry0, rx1, ry1;
  if(!fpbPhysRect(rx0, ry0, rx1, ry1)) return false;
  const int a0 = ry0 > y0 ? ry0 : y0, a1 = ry1 < y1 ? ry1 : y1;
  if(a0 > a1) return false;                                   // esta banda no toca la tarjeta
  const int cw = rx1 - rx0 + 1, ch = a1 - a0 + 1;
  if((size_t)cw * (size_t)ch * 2u > (size_t)FPB_SAVE_BYTES) return false;
  if(!fpbOwnFlush) fpbUnderDirty = true;                      // otro repinto estas filas: el vidrio puede estar viejo
  for(int r = 0; r < ch; r++)
    memcpy(fpbSave + (size_t)r * cw, fb + (size_t)(a0 + r) * SCR_W + rx0, (size_t)cw * 2);
  fpbSavX = rx0; fpbSavY = a0; fpbSavW = cw; fpbSavH = ch;
  fpbStamping = true;
  fpbBlit(a0, a1);
  fpbOnPanel = true;
  return true;
}
static void fpbStampEnd(){
  if(!fpbStamping) return;
  for(int r = 0; r < fpbSavH; r++)
    memcpy(fb + (size_t)(fpbSavY + r) * SCR_W + fpbSavX, fpbSave + (size_t)r * fpbSavW, (size_t)fpbSavW * 2);
  fpbStamping = false;
}
static bool fpbCleanPending(){ return fpbCleanNeed; }
// El panel todavia tiene pixeles del banner en esas filas y ya no se puede dibujar: se le mandan limpias.
static void fpbCleanFlush(){
  fpbCleanNeed = false;
  const bool had = fpbOnPanel;
  fpbOnPanel = false;
  if(had && fpbLastY1 >= fpbLastY0) flxFlush(fpbLastY0, fpbLastY1);
}

// Manda al panel las filas que ocupa la tarjeta ahora y las que ocupaba en la ultima vez.
static void fpbPaint(){
  fpbPaintWanted = false;
  int x0, y0, x1, y1;
  const bool vis = fpbPhysRect(x0, y0, x1, y1);
  int r0 = vis ? y0 : 1, r1 = vis ? y1 : 0;
  if(fpbOnPanel && fpbLastY1 >= fpbLastY0){
    if(r1 < r0){ r0 = fpbLastY0; r1 = fpbLastY1; }
    else { if(fpbLastY0 < r0) r0 = fpbLastY0; if(fpbLastY1 > r1) r1 = fpbLastY1; }
  }
  if(r1 < r0) return;
  fpbOwnFlush = true;
  flxFlush(r0, r1);
  fpbOwnFlush = false;
  if(vis){ fpbLastY0 = y0; fpbLastY1 = y1; }
}

// Cierra el banner del todo y deja el panel limpio. `clean` = hay que devolver al panel sus filas.
static void fpbFinish(bool clean){
  const bool had = fpbOnPanel;
  fpbLive = false;
  fpbOnPanel = false;
  fpbState = FPB_HIDDEN;
  fpbSlide = 0.0f; fpbDrop = 0.0f; fpbOutDir = 0; fpbSpring = false; fpbPaintWanted = false;
  fpbCleanNeed = false;
  fpbUnderDirty = false;
  if(fpbGesture){
    // El dedo sigue abajo y el banner ya no esta: lo que quede del episodio no es de nadie
    // (ni del banner ni de la pantalla de debajo, que no vio el principio).
    fpbGesture = false;
    if(T.down) gTouchSwallow = true;
  }
  if(clean && had && fpbLastY1 >= fpbLastY0) flxFlush(fpbLastY0, fpbLastY1);
  fpbLastY0 = 0; fpbLastY1 = -1;
  fpbFreeBufs();
}
// Cierre inmediato: quien toma la pantalla (o el banner sin memoria) ya la repinta, pero el panel se
// limpia igualmente desde fb, que nunca tuvo el banner.
static void fpbAbandon(){ fpbFinish(true); }

static void fpbBeginOut(int dir){
  if(fpbState == FPB_HIDDEN || fpbState == FPB_OUT) return;
  if(fpbState == FPB_ARMED){ fpbAbandon(); return; }
  fpbState = FPB_OUT;
  fpbT0 = millis();
  fpbOutDir = dir;
  fpbOutFrom = fpbSlide;
  fpbSpring = false;
  fpbPaintWanted = true;
}
static void fpbClose(){ fpbBeginOut(0); }

// Algo le quito la pantalla al aviso a medias (transicion, cortina, modal, giro):
// el panel queda limpio y el aviso vuelve AL FRENTE de la cola con el tiempo que
// le quedaba (ver PRESENTADOR UNICO DE AVISOS). Uno que ya se estaba yendo, se va.
static void fpbRequeue(){
  const bool again = fpbState == FPB_ARMED || fpbState == FPB_IN || fpbState == FPB_SHOWN;
  FlexPhoneBannerMsg m = fpbCur;
  if(fpbState == FPB_SHOWN) m.shownMs += millis() - fpbT0;
  fpbAbandon();
  fpbRefresh = false;
  if(again) fpbEnqueue(&m, true);
}

// Reserva y primer dibujo. Corre UNA vez, al armarse.
static bool fpbArm(){
  const size_t cvBytes = (size_t)SCR_W * FPB_V_H * 2;
  if(!fpbCv || !fpbSave){
    // La reserva NO puede comerse la proteccion del sistema.
    if(memFreePsram() < FLEXMEM_CRIT_BYTES + cvBytes + FPB_SAVE_BYTES) return false;
    fpbCv   = (uint16_t*)heap_caps_aligned_alloc(64, cvBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    fpbSave = (uint16_t*)heap_caps_aligned_alloc(64, FPB_SAVE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!fpbCv || !fpbSave){ fpbFreeBufs(); return false; }
  }
  fpbRender();
  return true;
}

// -------------------------------------------------------------
//  Toque
// -------------------------------------------------------------
// El banner se queda el EPISODIO del dedo que baja dentro de su tarjeta, entero y hasta que se levante:
// la pantalla de debajo ni lo ve (touchHoldBack). Un episodio que empezo fuera es de la pantalla de debajo,
// aunque el dedo pase por encima de la tarjeta.
//   · arrastrar a izquierda o derecha sigue al dedo 1:1;
//   · soltar pasado 1/3 de su ancho, o con un lanzamiento, DESCARTA: sale deslizandose y la notificacion se
//     retira de verdad (del modelo y del telefono);
//   · soltar antes vuelve a su sitio (muelle) y no pasa nada;
//   · un toque sin arrastrar abre Flex Phone en Notificaciones.
static void fpbDismissed(){
  // Descartado por el usuario. Si viene del telefono, se descarta TAMBIEN alli: descartarla en un lado y que
  // siga en el otro es lo que hace molesto un puente de notificaciones.
  if(fpbCur.src == FPN_SRC_PHONE && fpbCur.id){
    if(flexPhoneLinkReady(&fphLink)){
      uint8_t body[4];
      FlexLinkWr w2; flexLinkWrInit(&w2, body, sizeof(body));
      flexLinkWrU32(&w2, fpbCur.id);
      if(flexLinkWrOk(&w2))
        flexPhoneLinkSend(&fphLink, FLNK_T_NOTIF_REMOVE, body, w2.at, false);
    }
    flexPhoneNotifRemove(&fphModel, fpbCur.id);
    flexPhoneConvRebuild(&fphModel);
    fphModel.dirty = true;
  }
}
static void fpbTouch(){
  const bool targetable = fpbLive && (fpbState == FPB_IN || fpbState == FPB_SHOWN);
  if(!fpbGesture){
    if(!targetable || !T.pressed || gTouchSwallow) return;     // el episodio es de quien este debajo
    if(!fpbInsidePhys(T.x, T.y)) return;
    int lx, ly; fpbToLogical(T.x, T.y, lx, ly);
    fpbGesture = true; fpbGMoved = false; fpbSpring = false;
    fpbGx0 = lx; fpbGLx = lx; fpbGMs = millis(); fpbGvx = 0.0f;
  }
  if(!targetable){
    // El banner desaparecio a mitad del gesto: lo que quede del episodio no es de nadie (ni del banner ni de
    // la pantalla de debajo, que no vio el principio).
    fpbGesture = false;
    if(T.down) gTouchSwallow = true;
    touchHoldBack();
    return;
  }
  int lx, ly; fpbToLogical(T.x, T.y, lx, ly);
  if(T.down){
    // Dedo apoyado: sigue al dedo, sin pasarse del ancho de la tarjeta + un margen.
    const int dx = lx - fpbGx0;
    if(!fpbGMoved && (dx > FPB_DRAG_PX || dx < -FPB_DRAG_PX)) fpbGMoved = true;
    if(fpbGMoved){
      const float lim = (float)(fpbCardW() + 40);
      float s = (float)dx;
      if(s < -lim) s = -lim;
      if(s > lim) s = lim;
      if(s != fpbSlide){ fpbSlide = s; fpbPaintWanted = true; }
      const uint32_t now = millis();
      if(now - fpbGMs >= 16u){                     // velocidad: lo que se movio en la ultima ventana
        fpbGvx = (float)(lx - fpbGLx) / (float)(now - fpbGMs);
        fpbGLx = lx; fpbGMs = now;
      }
    }
    touchHoldBack();
    return;
  }
  // Soltado.
  const bool tap = T.tap;
  fpbGesture = false;
  if(millis() - fpbGMs > 100u) fpbGvx = 0.0f;       // se paro antes de soltar: no es un lanzamiento
  const int w = fpbCardW();
  const float a = fpbSlide < 0 ? -fpbSlide : fpbSlide;
  const float v = fpbGvx < 0 ? -fpbGvx : fpbGvx;
  const bool sameWay = (fpbSlide < 0 && fpbGvx < 0) || (fpbSlide > 0 && fpbGvx > 0);
  if(fpbGMoved && (a >= (float)(w / 3) || (sameWay && v >= FPB_FLING && a >= (float)FPB_FLING_MIN))){
    fpbDismissed();
    fpbBeginOut(fpbSlide < 0 ? -1 : 1);
  } else if(fpbGMoved || fpbSlide != 0.0f){
    // No llego: vuelve a su sitio.
    fpbSpring = true; fpbSpringFrom = fpbSlide; fpbSpringT0 = millis();
    fpbPaintWanted = true;
  } else if(tap){
    // Toque: abre Flex Phone en la seccion de notificaciones. Es la unica accion real que se puede ofrecer.
    const bool phone = fpbCur.src == FPN_SRC_PHONE;
    fpbClose();
    if(phone){
      fphSection = FPH_NOTIFS;
      if(gState == ST_APP) appClose();
      enterApp(IC_FLEXPHONE);
    }
  }
  touchHoldBack();                                 // el tap, la suelta y el deslizamiento no llegan a la pantalla de debajo
}

// -------------------------------------------------------------
//  Tick
// -------------------------------------------------------------
static float fpbEase(float p){ return 1.0f - (1.0f - p) * (1.0f - p); }
static void fpbTick(){
  // Cola vacia y nada a la vista: salida barata. Es lo que hace que
  // este modulo no cueste FPS cuando no hay notificaciones.
  if(fpbState == FPB_HIDDEN && fpbQueueN == 0) return;

  // Arrancar el siguiente de la cola.
  if(fpbState == FPB_HIDDEN){
    if(!fpbCanShow(fpbQueue[0].src)) return;   // se espera: la cola no se pierde
    fpbCur = fpbQueue[0];
    for(int i = 1; i < fpbQueueN; i++) fpbQueue[i - 1] = fpbQueue[i];
    fpbQueueN--;
    fpbLand = gLand;
    fpbSlide = 0.0f;
    fpbDrop = (float)fpbDropMax();
    fpbSuppressed = false;
    fpbState = FPB_ARMED;
    fpbT0 = millis();
  }

  // Alguien tomo la pantalla, o la giro: el banner se retira, el panel se limpia y
  // el aviso espera AL FRENTE de la cola a poder volver (ver fpbRequeue).
  if(fpbSuppressed || gLand != fpbLand || !fpbScreenAllows()){ fpbRequeue(); return; }

  if(fpbState == FPB_ARMED){
    if(!fpbArm()){
      // Sin memoria para la tarjeta: NO se dibuja. La notificacion no se pierde -- esta en el Centro -- y el
      // sistema no se queda con un overlay que no puede pintar.
      fpbAbandon();
      return;
    }
    fpbLive = true;
    fpbState = FPB_IN;
    fpbT0 = millis();
    fpbPaint();
    return;
  }
  if(!fpbLive){ fpbAbandon(); return; }

  const uint32_t now = millis();
  const uint32_t e = now - fpbT0;

  // Lo de debajo repinto sus filas: el vidrio se resuelve otra vez (unas pocas veces por segundo, nunca por
  // cuadro). Y si el aviso a la vista cambio de texto, tambien -- y vuelve a contar su tiempo desde cero.
  if(fpbRefresh || (fpbUnderDirty && now - fpbCvMs >= FPB_REGLASS_MS)){
    if(fpbRefresh && fpbState == FPB_SHOWN){ fpbT0 = now; fpbCur.shownMs = 0; }
    fpbRefresh = false;
    fpbRender();
    fpbPaintWanted = true;
  }

  switch(fpbState){
    case FPB_IN: {
      float p = (e >= FPB_IN_MS) ? 1.0f : (float)e / (float)FPB_IN_MS;
      fpbDrop = (1.0f - fpbEase(p)) * (float)fpbDropMax();
      fpbPaint();
      if(e >= FPB_IN_MS){ fpbDrop = 0.0f; fpbState = FPB_SHOWN; fpbT0 = now; }
      break;
    }
    case FPB_SHOWN:
      if(fpbSpring){
        float p = (now - fpbSpringT0 >= FPB_SPRING_MS) ? 1.0f : (float)(now - fpbSpringT0) / (float)FPB_SPRING_MS;
        fpbSlide = fpbSpringFrom * (1.0f - fpbEase(p));
        if(p >= 1.0f){ fpbSlide = 0.0f; fpbSpring = false; }
        fpbPaintWanted = true;
      }
      // Mientras el dedo lo arrastra NO caduca: nadie quiere que se le vaya lo que esta tocando.
      if(fpbGesture || fpbSpring || fpbSlide != 0.0f){ fpbT0 = now; if(fpbPaintWanted) fpbPaint(); break; }
      if(fpbPaintWanted) fpbPaint();
      {
        // Lo que ya estuvo a la vista antes de perder la pantalla cuenta (pero
        // al volver se ve, como poco, FPB_MIN_HOLD_MS).
        const uint32_t hold = (fpbCur.shownMs + FPB_MIN_HOLD_MS < (uint32_t)FPB_HOLD_MS)
                              ? (uint32_t)FPB_HOLD_MS - fpbCur.shownMs : (uint32_t)FPB_MIN_HOLD_MS;
        if(e >= hold) fpbClose();
      }
      break;
    case FPB_OUT: {
      float p = (e >= FPB_OUT_MS) ? 1.0f : (float)e / (float)FPB_OUT_MS;
      if(fpbOutDir == 0) fpbDrop = fpbEase(p) * (float)fpbDropMax();               // sube por donde entro
      else fpbSlide = fpbOutFrom + ((float)fpbOutDir * (float)(fpbCardW() + 40) - fpbOutFrom) * fpbEase(p);
      fpbPaint();
      if(e >= FPB_OUT_MS){
        const uint32_t more = fpbMore;
        (void)more;
        fpbFinish(true);
        fpbMore = 0;
      }
      break;
    }
    default: break;
  }
}

// =============================================================
//  4) CENTRO DE NOTIFICACIONES
// =============================================================
// Panel a pantalla completa que entra desde el borde IZQUIERDO.
// Mientras esta a la vista es dueno de la pantalla, igual que la
// cortina del panel rapido: su contenido se desplaza, se descarta y
// se abre, y la pantalla de debajo no recibe toques.
#define FPC_EDGE_W    SYS_EDGE_LEFT_W   // franja de borde del gesto (FlexOS_Ultra_Touch.h: la comparten las pantallas con controles propios)
#define FPC_INTENT_PX 12      // lo que el dedo tiene que recorrer hacia dentro (y mas en horizontal que en vertical) para que cuente como el gesto
#define FPC_ANIM_MS   190
#define FPC_ROW_H     78
#define FPC_HDR_H     96

static int      fpcX = -SCR_W;        // desplazamiento: -SCR_W = cerrado
static bool     fpcDragging = false;
static bool     fpcDragMoved = false;
static int      fpcDragX0 = 0, fpcDragBase = 0;
static bool     fpcAnimOn = false;
static int      fpcAnimDst = 0;
static uint32_t fpcAnimT0 = 0;
static int      fpcAnimFrom = 0;
static FgScroll fpcScr = { 0, 0, FPC_HDR_H, SCR_H - 8 };
static bool     fpcDirty = true;
static FlexPhoneEntry fpcList[FPN_LIST_MAX];
static int      fpcListN = 0;

static inline bool fpcOpen(){ return fpcX > -SCR_W; }
// El Centro es dueno de la pantalla mientras esta abierto, se arrastra o se anima (lo pregunta el banner).
static bool fpcBusy(){ return fpcOpen() || fpcDragging || fpcAnimOn; }

// ¿Se puede abrir el Centro desde el borde? Mismas restricciones que
// la cortina: en DeX y en las pantallas en exclusiva, no.
static bool fpcCanOpen(){
  if(gLand)   return false;      // apaisado: version propia pendiente, no una vertical girada
  if(gHosted) return false;      // app dentro de una ventana de DeX
  if(editMode) return false;
  if(KIOSK_ON && kioskOn) return false;
  if(flexOtaOwnsScreen() || flexOtaOverlayActive()) return false;
  if(qsPanelY != 0 || qsDragging || qsAnimOn) return false;
  return (gState == ST_HOME || gState == ST_APP);
}

// -------------------------------------------------------------
//  Construccion de la lista
// -------------------------------------------------------------
// Se recompone al abrir y cuando cambia algo, NO en cada cuadro.
static void fpcBuild(){
  fpcListN = 0;
  // 1) Las del telefono.
  for(int i = 0; i < FLP_NOTIF_MAX && fpcListN < FPN_LIST_MAX; i++){
    const FlexPhoneNotif* n = &fphModel.notif[i];
    if(!n->used) continue;
    FlexPhoneEntry* e = &fpcList[fpcListN++];
    memset(e, 0, sizeof(*e));
    e->src = FPN_SRC_PHONE;
    e->id = n->id;
    e->slot = (int16_t)i;
    e->whenMs = n->rxMs;
    e->pri = n->pri;
    e->group = (uint8_t)flexPhoneNotifCountPkg(&fphModel, n->pkg);
    flexLinkUtf8Copy(e->app, sizeof(e->app), n->app[0] ? n->app : n->pkg);
    flexLinkUtf8Copy(e->title, sizeof(e->title), n->title);
    char body[FLP_TEXT_MAX];
    if(flexPhoneNotifBody(&fphModel, n, false, body, sizeof(body)))
      flexLinkUtf8Copy(e->body, sizeof(e->body), body);
    else
      flexLinkUtf8Copy(e->body, sizeof(e->body),
                       LI() == 1 ? "Content hidden" : "Contenido oculto");
  }
  // 2) Las del propio sistema: el historial que guarda la isla (notifPush).
  for(int i = 0; i < gNotifCount && fpcListN < FPN_LIST_MAX; i++){
    if(!gNotifs[i].active) continue;
    FlexPhoneEntry* e = &fpcList[fpcListN++];
    memset(e, 0, sizeof(*e));
    e->src = FPN_SRC_SYSTEM;
    e->slot = (int16_t)i;
    e->whenMs = gNotifs[i].bornMs;
    e->pri = FLP_PRI_DEFAULT;
    e->group = 1;
    flexLinkUtf8Copy(e->app, sizeof(e->app), "Flex OS");
    flexLinkUtf8Copy(e->title, sizeof(e->title), gNotifs[i].mod.name);
    flexLinkUtf8Copy(e->body, sizeof(e->body), gNotifs[i].mod.sub);
  }
  // 3) Mas reciente primero. Insercion: la lista es corta (<= 43) y
  //    esto evita traerse un qsort para ordenar cuarenta elementos.
  for(int i = 1; i < fpcListN; i++){
    FlexPhoneEntry tmp = fpcList[i];
    int j = i - 1;
    while(j >= 0 && fpcList[j].whenMs < tmp.whenMs){ fpcList[j + 1] = fpcList[j]; j--; }
    fpcList[j + 1] = tmp;
  }
}

// -------------------------------------------------------------
//  Dibujo
// -------------------------------------------------------------
static void fpcRender(){
  setBuf(fb);
  fgHitsReset();
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);

  // Cabecera
  drawText(20, 26, LI() == 1 ? "Notifications" : "Notificaciones", 3, TH_TXT);
  {
    char sub[48];
    if(fpcListN) snprintf(sub, sizeof(sub), LI() == 1 ? "%d in total" : "%d en total", fpcListN);
    else         snprintf(sub, sizeof(sub), LI() == 1 ? "Nothing pending" : "Nada pendiente");
    drawText(20, 60, sub, 1, TH_MUTE);
  }
  // Interruptor de No molestar, donde se necesita.
  {
    const int bw = 92, bx = SCR_W - bw - 16, by = 30;
    fillRoundRect(bx, by, bw, 34, 17, gDnd ? TH_PRIM : TH_SURF2);
    drawTextC(bx + bw / 2, by + 9, gDnd ? (LI() == 1 ? "DND on" : "Silencio")
                                        : (LI() == 1 ? "DND off" : "Avisos"),
              1, gDnd ? TH_ONACC : TH_TXT2);
    fgHitAdd(bx, by, bw, 34, 1);
  }
  hLine(20, FPC_HDR_H - 12, SCR_W - 40, TH_DIV);

  fgScrollSetView(&fpcScr, FPC_HDR_H, SCR_H - 8);
  if(fpcListN == 0){
    fgEmpty(FPC_HDR_H + 60,
            LI() == 1 ? "All clear" : "Todo al dia",
            gDnd ? (LI() == 1 ? "Do not disturb is on. Anything that arrives is kept here, "
                                "without a banner or a sound."
                              : "No molestar esta activado. Lo que llegue se guarda aqui, "
                                "sin banner y sin sonido.")
                 : (LI() == 1 ? "Notifications from Flex OS and from the linked phone show up here."
                              : "Aqui aparecen las notificaciones de Flex OS y las del telefono vinculado."));
    fpcScr.content = 0;
  } else {
    const int y0 = FPC_HDR_H - fpcScr.off;
    int y = y0;
    for(int i = 0; i < fpcListN; i++){
      if(y + FPC_ROW_H > FPC_HDR_H - FPC_ROW_H && y < SCR_H + FPC_ROW_H){
        const FlexPhoneEntry* e = &fpcList[i];
        const uint16_t accent = (e->pri >= FLP_PRI_HIGH) ? TH_PRIM
                              : (e->src == FPN_SRC_SYSTEM ? TH_ACCS : TH_DIV);
        fgCardAccent(16, y, SCR_W - 32, FPC_ROW_H - 8, accent);
        fgTextEllipsis(38, y + 8, SCR_W - 110, e->app, 1, TH_MUTE);
        if(e->group > 1){
          char g[10];
          snprintf(g, sizeof(g), "x%u", e->group);
          drawTextR(SCR_W - 34, y + 8, g, 1, TH_MUTE);
        }
        fgTextEllipsis(38, y + 26, SCR_W - 76, e->title, 2, TH_TXT);
        fgTextEllipsis(38, y + 50, SCR_W - 76, e->body, 1, TH_TXT2);
        fgHitAdd(16, y, SCR_W - 32, FPC_ROW_H - 8, (uint16_t)(100 + i));
      }
      y += FPC_ROW_H;
    }
    y += 6;
    fgButton(16, y, SCR_W - 32, 42,
             LI() == 1 ? "Clear all" : "Borrar todas", FG_BTN_DANGER, 2);
    y += 52;
    fpcScr.content = y - y0;
    fgScrollBar(&fpcScr);
  }

  // Deslizamiento del panel entero al entrar o salir. Se presenta
  // desde fb ya compuesto: una sola pasada por cuadro.
  flxFlushAll();
  fpcDirty = false;
}

static void fpcAnimTo(int target){
  if(fpcX == target){ fpcAnimOn = false; return; }
  fpcAnimFrom = fpcX;
  fpcAnimDst = target;
  fpcAnimT0 = millis();
  fpcAnimOn = true;
}

static void fpcForceClose(){
  fpcX = -SCR_W;
  fpcAnimOn = false;
  fpcDragging = false;
  fpcDragMoved = false;
  gHomeDirty = true;       // la pantalla de debajo se rehace entera
}

// -------------------------------------------------------------
//  Toques y estado
// -------------------------------------------------------------
static void fpcHandleHit(uint16_t id){
  if(id == 1){                                  // No molestar
    phoneDndSet(!gDnd);
    fpcDirty = true;
    return;
  }
  if(id == 2){                                  // Borrar todas
    flexPhoneNotifClearAll(&fphModel);
    flexPhoneConvRebuild(&fphModel);
    fphModel.dirty = true;
    gNotifCount = 0;
    memset(gNotifs, 0, sizeof(gNotifs));
    fpcBuild();
    fgScrollReset(&fpcScr, FPC_HDR_H, SCR_H - 8);
    fpcDirty = true;
    return;
  }
  if(id >= 100 && id < 100 + fpcListN){
    const FlexPhoneEntry* e = &fpcList[id - 100];
    if(e->src == FPN_SRC_PHONE){
      // Abrir Flex Phone en Notificaciones es la accion real que se
      // puede ofrecer: Flex OS no puede abrir una app del telefono.
      const uint32_t nid = e->id;
      (void)nid;
      fpcForceClose();
      fphSection = FPH_NOTIFS;
      if(gState == ST_APP) appClose();
      enterApp(IC_FLEXPHONE);
      return;
    }
    // Aviso del sistema: se descarta.
    if(e->slot >= 0 && e->slot < gNotifCount) notifRemove(e->slot);
    fpcBuild();
    fpcDirty = true;
    return;
  }
}

// Devuelve true si el Centro se queda el toque y la vuelta entera.
static bool fpcGlobalHandle(){
  // Cerrado y sin gesto de borde: no cuesta nada.
  if(!fpcOpen() && !fpcDragging && !fpcAnimOn){
    if(!fpcCanOpen()) return false;
    // EL GESTO CUENTA SI NACIO EN EL BORDE **Y SE MUEVE COMO UN GESTO** (hacia dentro y mas en horizontal que en vertical). Sin lo primero,
    // cualquier deslizamiento dentro de una app abriria el panel por accidente, que es justo lo que no puede pasar en un juego. Sin lo
    // segundo -- antes se armaba al APOYAR el dedo -- el Centro se quedaba el apoyo (T.pressed dura UNA vuelta), el arrastre moria en la
    // vuelta siguiente (leia T.pressed en vez de T.down: el gesto del borde no llegaba a abrir NUNCA) y el toque se escapaba a la pantalla de
    // debajo SIN su apoyo: un boton en esa franja ni funcionaba ni abria el Centro (el "atras" del visor, p. ej.). Ahora un toque en el
    // borde es un toque normal de la pantalla de debajo, y el deslizamiento lo agarra cuando se ve que lo es.
    if(!(T.down && T.startX < FPC_EDGE_W && T.startY > 40)) return false;
    const int dx = T.x - T.startX, dy = abs(T.y - T.startY);
    if(!(dx > FPC_INTENT_PX && dx > dy)) return false;
    fpcBuild();
    fgScrollReset(&fpcScr, FPC_HDR_H, SCR_H - 8);
    fpcDragging = true;
    fpcDragMoved = true;
    fpcDragX0 = T.startX;                                  // el panel sigue al dedo desde donde NACIO el gesto
    fpcDragBase = fpcX;
    fpcAnimOn = false;
    { const int nx = fpcDragBase + dx; fpcX = nx > 0 ? 0 : (nx < -SCR_W ? -SCR_W : nx); }
    fpcDirty = true;
    fpcRender();
    touchHoldBack();                                       // desde aqui el episodio es del Centro, entero, hasta que el dedo se levante
    return true;
  }
  if(!fpcCanOpen() && fpcOpen()){ fpcForceClose(); return false; }

  // Arrastre del panel.
  if(fpcDragging){
    if(T.down){                                            // el NIVEL del dedo, no el pulso de apoyo (que dura una vuelta)
      const int d = T.x - fpcDragX0;
      if(!fpcDragMoved && d > 10) fpcDragMoved = true;
      if(fpcDragMoved){
        int nx = fpcDragBase + d;
        if(nx > 0) nx = 0;
        if(nx < -SCR_W) nx = -SCR_W;
        if(nx != fpcX){ fpcX = nx; fpcDirty = true; }
      }
      // Mientras el panel entra solo se dibuja el, sin contenido
      // interactivo: a medio abrir los controles no existen todavia
      // como superficie tocable.
      if(fpcDirty && fpcX > -SCR_W) fpcRender();
      touchHoldBack();
      return true;
    }
    fpcDragging = false;
    touchHoldBack();                                       // la suelta y el deslizamiento tampoco llegan a la pantalla de debajo
    if(!fpcDragMoved){
      // Roce del borde sin arrastrar: no se abre nada. Un panel que
      // salta con un roce del borde es peor que no tenerlo.
      fpcForceClose();
      return true;
    }
    fpcAnimTo(fpcX > -SCR_W / 2 ? 0 : -SCR_W);
    return true;
  }

  // Animacion de apertura/cierre.
  if(fpcAnimOn){
    const uint32_t e = millis() - fpcAnimT0;
    float p = (e >= FPC_ANIM_MS) ? 1.0f : (float)e / (float)FPC_ANIM_MS;
    p = 1.0f - (1.0f - p) * (1.0f - p);
    fpcX = fpcAnimFrom + (int)((fpcAnimDst - fpcAnimFrom) * p);
    fpcDirty = true;
    if(e >= FPC_ANIM_MS){
      fpcX = fpcAnimDst;
      fpcAnimOn = false;
      if(fpcX <= -SCR_W){ fpcForceClose(); return false; }
    }
    fpcRender();
    return true;
  }

  if(!fpcOpen()) return false;

  // ---- Abierto del todo: contenido ----
  // Desplazamiento 1:1 y toque solo si el dedo NO se movio. La maquina
  // de estados es la MISMA que la de las pantallas de Flex Phone
  // (fgDragStep): tener dos copias escritas a mano es lo que hizo que
  // las dos arrastraran con el flanco en vez de con el nivel.
  static FgDrag sDrag = { false, false, 0, 0 };
  // Un arrastre hacia la derecha desde el contenido CIERRA el panel:
  // es el gesto inverso al de apertura y es lo que se espera.
  if(T.down && T.x - T.startX > 60 && abs(T.y - T.startY) < 40){
    fgDragReset(&sDrag);
    fpcAnimTo(-SCR_W);
    return true;
  }
  switch(fgDragStep(&sDrag, &fpcScr, T.down, T.pressed, T.y)){
    case FG_DRAG_SCROLLING: fpcDirty = true; fpcRender(); return true;
    case FG_DRAG_CONSUMED:  return true;     // fue un desplazamiento, no un toque
    default: break;
  }
  if(T.tap){
    const uint16_t id = fgHitAt(T.x, T.y);
    if(id) fpcHandleHit(id);
    fpcRender();
    return true;
  }
  if(fpcDirty) fpcRender();
  return true;
}

// =============================================================
//  5) PUENTE: DE UNA NOTIFICACION NUEVA AL BANNER
// =============================================================
// Lo llama flexPhoneTick cuando el modelo registra algo nuevo. La
// notificacion SE GUARDA siempre; lo que decide phoneCanInterrupt es
// solo si ademas se presenta.
static uint32_t fpnLastSeenId = 0;

static void phoneNotifyBridge(){
  // Se mira la mas reciente. Si es distinta de la ultima que se
  // presento, es nueva.
  const FlexPhoneNotif* newest = NULL;
  for(int i = 0; i < FLP_NOTIF_MAX; i++){
    const FlexPhoneNotif* n = &fphModel.notif[i];
    if(!n->used) continue;
    if(!newest || n->rxMs > newest->rxMs) newest = n;
  }
  if(!newest) return;
  if(newest->id == fpnLastSeenId) return;
  fpnLastSeenId = newest->id;

  // El Centro, si esta abierto, se entera en el acto.
  if(fpcOpen()){ fpcBuild(); fpcDirty = true; }

  // No molestar: se registra y se acaba. Sin banner, sin sonido.
  if(!phoneCanInterrupt()) return;

  char body[FLP_TEXT_MAX];
  const bool haveBody = flexPhoneNotifBody(&fphModel, newest, false, body, sizeof(body));
  fpbPush(FPN_SRC_PHONE, newest->id,
          newest->app[0] ? newest->app : newest->pkg,
          newest->title,
          haveBody ? body : "",
          newest->pri);

  // SONIDO: hoy no lo hay, y no se finge.
  //
  // El audio de Flex OS es una tuberia PCM (flexAudioStartPcm /
  // flexAudioWrite): no existe un camino de "sonido de notificacion",
  // y fabricar un tono desde aqui significaria escribir en el I2S
  // dentro del bucle grafico. Eso es exactamente lo que este modulo
  // no puede hacer.
  //
  // phoneCanSound() ya esta escrito y dice la verdad sobre si se
  // PODRIA sonar (No molestar y silencio del sistema). El dia que
  // exista un reproductor de avisos, el unico cambio es llamarlo
  // aqui dentro. Mientras tanto la interfaz no promete sonido.
  (void)phoneCanSound;
}

// =============================================================
//  6) LAS DOS LLAMADAS QUE HACE EL PUENTE DE FLEX PHONE
// =============================================================
// Declaradas en FlexOS_FlexPhone_Bridge.h y definidas aqui: el
// puente se incluye antes porque este modulo lee su modelo.
static void flexPhoneOverlayBegin(){ phoneDndLoad(); }
static void flexPhoneOverlayNotify(){ phoneNotifyBridge(); }

// -------------------------------------------------------------
//  Control de No molestar del panel rapido
// -------------------------------------------------------------
// Declarados en FlexOS_Ultra_QuickPanel.h y definidos aqui, que es
// donde vive el estado. El interruptor cambia algo REAL: con No
// molestar activado no hay banner, no hay sonido y no hay vibracion.
static bool qpStDnd(){ return gDnd; }
static void qpTapDnd(){ phoneDndSet(!gDnd); }
static void qpSubDnd(char* o, size_t n){
  // El subtitulo dice lo que de verdad pasa, no "activado/desactivado".
  snprintf(o, n, "%s", gDnd ? "Sin avisos"
                            : (flexAudioMuted() ? "Avisos, sin sonido" : "Avisos normales"));
}
