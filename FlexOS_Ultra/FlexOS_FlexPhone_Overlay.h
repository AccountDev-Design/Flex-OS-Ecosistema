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

#define FPB_IN_MS    220
#define FPB_OUT_MS   180
#define FPB_HOLD_MS  4200      // visible antes de irse solo
// Cuantos avisos esperan turno. Al llenarse se resume: "y N mas".
// Una torre de tarjetas apiladas es justo lo que NO se quiere.
#define FPB_QUEUE    6

enum { FPB_HIDDEN = 0, FPB_ARMED, FPB_IN, FPB_SHOWN, FPB_OUT };

typedef struct {
  char     app[FPN_ENTRY_APP];
  char     title[FPN_ENTRY_TITLE];
  char     body[FPN_ENTRY_BODY];
  uint32_t id;
  uint8_t  src;
  uint8_t  pri;
} FlexPhoneBannerMsg;

static int      fpbState = FPB_HIDDEN;
static uint32_t fpbT0 = 0;
static bool     fpbLand = false;
static float    fpbSlide = 0.0f;        // desplazamiento del descarte
static bool     fpbDragging = false;
static int      fpbDragX0 = 0;
static FlexPhoneBannerMsg fpbCur;
static FlexPhoneBannerMsg fpbQueue[FPB_QUEUE];
static int      fpbQueueN = 0;
static uint32_t fpbMore = 0;            // cuantas se resumieron

// Banda capturada. Se pide al abrir y se suelta al cerrar: el banner
// es excepcional, y retener memoria permanentemente por algo que
// puede no ocurrir en horas no se sostiene.
static uint16_t* fpbBak = NULL;
static size_t    fpbBakCap = 0;
static int       fpbBakY0 = 0, fpbBakY1 = -1;

static void fpbFreeBand(){
  if(fpbBak){ free(fpbBak); fpbBak = NULL; fpbBakCap = 0; }
}
static inline void fpbInvalidateBand(){ fpbBakY1 = fpbBakY0 - 1; }
static inline bool fpbBandReady(){ return fpbBak && fpbBakY1 >= fpbBakY0; }
static inline bool fpbVisible(){ return fpbState != FPB_HIDDEN; }

// Banda FISICA (filas del panel). En landscape la x logica ES la
// fila fisica, por eso la banda sale del eje contrario.
static void fpbBand(bool land, int &y0, int &y1){
  if(land){ y0 = FPB_L_X - 8; y1 = FPB_L_X + FPB_L_W + 8; }
  else    { y0 = FPB_V_Y - 8; y1 = FPB_V_Y + FPB_V_H + 8; }
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}

// #############################################################
// ##  ¿SE PUEDE DIBUJAR EL BANNER AHORA MISMO?
// ##  ------------------------------------------------------
// ##  Las mismas pantallas en las que el sistema ya decide no
// ##  notificar nada, mas las que poseen la pantalla en exclusiva,
// ##  mas DeX.
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
static bool fpbCanShow(){
  if(!phoneCanInterrupt()) return false;          // No molestar
  if(notifSecureScreen()) return false;           // arranque, OOBE, bloqueo, apagado
  if(gFrPending || gState == ST_FACTORY || gSafeMode) return false;
  if(flexOtaOwnsScreen() || optActive()) return false;
  if(fpbDexActive()) return false;                // DeX: se registra, no se dibuja
  if(gSuspOn) return false;                       // pantalla apagada: no se pinta a oscuras
  if(appTrOwnsScreen()) return false;             // transicion de app dibujando
  if(faVisible() || cronoCardVisible()) return false;  // ya hay un modal encima
  if(qsPanelY != 0 || qsAnimOn || qsDragging) return false;   // la cortina dibuja encima
  return true;
}

// -------------------------------------------------------------
//  Encolar
// -------------------------------------------------------------
// UNO visible a la vez. Lo que llega detras espera turno; al
// llenarse la cola, se cuenta y se resume en el propio banner.
static void fpbPush(uint8_t src, uint32_t id, const char* app,
                    const char* title, const char* body, uint8_t pri){
  if(fpbQueueN >= FPB_QUEUE){ fpbMore++; return; }
  FlexPhoneBannerMsg* m = &fpbQueue[fpbQueueN++];
  memset(m, 0, sizeof(*m));
  m->src = src;
  m->id = id;
  m->pri = pri;
  flexLinkUtf8Copy(m->app,   sizeof(m->app),   app   ? app   : "");
  flexLinkUtf8Copy(m->title, sizeof(m->title), title ? title : "");
  flexLinkUtf8Copy(m->body,  sizeof(m->body),  body  ? body  : "");
}

// -------------------------------------------------------------
//  Dibujo
// -------------------------------------------------------------
static void fpbDrawCard(int x, int y, int w, int h, float p){
  // Entrada: cae desde arriba con desaceleracion. Corta (220 ms) y
  // sin bloquear nada: es una interpolacion, no una espera.
  const int drop = (int)((1.0f - p) * 26.0f);
  const int sx = x + (int)fpbSlide;
  const int sy = y - drop;
  // Sombra corta debajo, para que despegue del fondo sin pagar un
  // desenfoque de pantalla completa.
  fillRoundRect(sx + 3, sy + 4, w, h, 18, TH_SHADOW);
  drawGlassCardFlat(sx, sy, w, h, 18, uiGlass ? TH_GLASS : TH_SURF, TH_PAGE);

  // Barra de prioridad: se ve de un vistazo si es urgente sin leer.
  const uint16_t accent = (fpbCur.pri >= FLP_PRI_HIGH) ? TH_PRIM : TH_DIV;
  fillRoundRect(sx + 8, sy + 12, 4, h - 24, 2, accent);

  const int tx = sx + 22;
  const int tw = w - 40;
  fgTextEllipsis(tx, sy + 10, tw, fpbCur.app[0] ? fpbCur.app : "Flex OS", 1, TH_MUTE);
  fgTextEllipsis(tx, sy + 28, tw, fpbCur.title, 2, TH_TXT);
  if(fpbCur.body[0]) fgTextEllipsis(tx, sy + 50, tw, fpbCur.body, 1, TH_TXT2);

  // Resumen de lo que espera. NO se apilan tarjetas.
  const int pend = fpbQueueN + (int)fpbMore;
  if(pend > 0){
    char more[24];
    snprintf(more, sizeof(more), "+%d", pend);
    drawTextR(sx + w - 14, sy + 10, more, 1, TH_MUTE);
  }
}

static void fpbCompose(float p){
  if(!fpbBandReady()) return;
  const int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
  const bool wl = gLand;
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  bbufSys(); setBuf(bbuf);
  memcpy(bbuf + (size_t)fpbBakY0 * SCR_W, fpbBak,
         (size_t)SCR_W * (fpbBakY1 - fpbBakY0 + 1) * 2);
  gLand = fpbLand;
  if(fpbLand) fpbDrawCard(FPB_L_X, FPB_L_Y, FPB_L_W, FPB_L_H, p);
  else        fpbDrawCard(FPB_V_X, FPB_V_Y, FPB_V_W, FPB_V_H, p);
  gLand = wl;
  // Volcado ATOMICO de una banda ya terminada: DMA2D nunca ve un fb a
  // medio pintar, y por eso no hay parpadeo.
  present(fpbBakY0, fpbBakY1);
  setBuf(fb);
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
}

// Devuelve la banda a como estaba. Es lo que impide que quede un
// rastro del banner detras de la app.
static void fpbRestore(){
  if(!fpbBandReady()) return;
  fbLock();
  memcpy(fb + (size_t)fpbBakY0 * SCR_W, fpbBak,
         (size_t)SCR_W * (fpbBakY1 - fpbBakY0 + 1) * 2);
  fbUnlock();
  flxFlush(fpbBakY0, fpbBakY1);
  fpbInvalidateBand();
  // Y ademas se pide repintado a quien tuviera la pantalla: si la app
  // de debajo repinto esa banda mientras el banner estaba encima, la
  // copia ya no la describe. Restaurar y pedir repintado cubre los dos
  // casos sin que ninguno deje residuos.
  if(gState == ST_HOME) gHomeDirty = true;
}

// Reserva y captura. Corre UNA vez, en la fase de dibujo.
static bool fpbArm(){
  fpbBand(fpbLand, fpbBakY0, fpbBakY1);
  if(fpbBakY1 < fpbBakY0) return false;
  const size_t need = (size_t)SCR_W * (fpbBakY1 - fpbBakY0 + 1) * 2;
  // La reserva NO puede comerse la proteccion del sistema.
  if(fpbBakCap < need && memFreePsram() < FLEXMEM_CRIT_BYTES + need) return false;
  if(fpbBakCap < need) fpbFreeBand();
  if(!fpbBak){
    fpbBak = (uint16_t*)heap_caps_aligned_alloc(64, need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    fpbBakCap = fpbBak ? need : 0;
  }
  if(!fpbBak) return false;
  fbLock();
  memcpy(fpbBak, fb + (size_t)fpbBakY0 * SCR_W, need);
  fbUnlock();
  return fpbBandReady();
}

// Cierre inmediato SIN restaurar. Lo usa quien toma la pantalla por
// su cuenta: la pantalla nueva se pinta entera y restaurar encima
// solo devolveria un fotograma viejo.
static void fpbAbandon(){
  fpbState = FPB_HIDDEN;
  fpbSlide = 0.0f;
  fpbDragging = false;
  fpbInvalidateBand();
  fpbFreeBand();
}

static void fpbClose(){
  if(fpbState == FPB_HIDDEN || fpbState == FPB_OUT) return;
  if(fpbState == FPB_ARMED){ fpbAbandon(); return; }
  fpbState = FPB_OUT;
  fpbT0 = millis();
}

// -------------------------------------------------------------
//  Toque
// -------------------------------------------------------------
// Devuelve true si el banner se queda el toque. SOLO se lo queda
// cuando el dedo esta DENTRO de su tarjeta: fuera de ahi el toque es
// de la app de debajo, que sigue funcionando con normalidad.
static bool fpbTouch(){
  if(fpbState != FPB_SHOWN && fpbState != FPB_IN) return false;
  if(!fpbBandReady()) return false;

  // Zona de la tarjeta en coordenadas FISICAS.
  int x0, y0, x1, y1;
  if(fpbLand){ y0 = FPB_L_X; y1 = FPB_L_X + FPB_L_W; x0 = FPB_L_Y; x1 = FPB_L_Y + FPB_L_H; }
  else       { x0 = FPB_V_X; x1 = FPB_V_X + FPB_V_W; y0 = FPB_V_Y; y1 = FPB_V_Y + FPB_V_H; }

  const bool inside = (T.startX >= x0 && T.startX <= x1 && T.startY >= y0 && T.startY <= y1);
  if(!inside && !fpbDragging) return false;

  if(T.pressed){
    if(!fpbDragging){ fpbDragging = true; fpbDragX0 = T.x; }
    // Arrastre para descartar: se sigue el dedo 1:1 hacia la
    // izquierda. Hacia la derecha no se mueve, porque ahi no hay
    // ningun gesto detras y un muelle que no lleva a nada confunde.
    const int d = T.x - fpbDragX0;
    fpbSlide = (d < 0) ? (float)d : 0.0f;
    return true;
  }

  if(fpbDragging){
    fpbDragging = false;
    const int w = fpbLand ? FPB_L_W : FPB_V_W;
    if(fpbSlide < -(w / 3)){
      // Descartado por el usuario. Si viene del telefono, se descarta
      // TAMBIEN alli: descartarla en un lado y que siga en el otro es
      // lo que hace molesto un puente de notificaciones.
      if(fpbCur.src == FPN_SRC_PHONE && fpbCur.id && flexPhoneLinkReady(&fphLink)){
        uint8_t body[4];
        FlexLinkWr w2; flexLinkWrInit(&w2, body, sizeof(body));
        flexLinkWrU32(&w2, fpbCur.id);
        if(flexLinkWrOk(&w2))
          flexPhoneLinkSend(&fphLink, FLNK_T_NOTIF_REMOVE, body, w2.at, false);
        flexPhoneNotifRemove(&fphModel, fpbCur.id);
        flexPhoneConvRebuild(&fphModel);
        fphModel.dirty = true;
      }
      fpbClose();
      return true;
    }
    // No llego: vuelve a su sitio.
    fpbSlide = 0.0f;
    if(T.tap){
      // Toque: abre Flex Phone en la seccion de notificaciones. Es la
      // unica accion real que se puede ofrecer desde aqui.
      fpbClose();
      if(fpbCur.src == FPN_SRC_PHONE){
        fphSection = FPH_NOTIFS;
        if(gState == ST_APP) appClose();
        enterApp(IC_FLEXPHONE);
      }
      return true;
    }
    return true;
  }
  return false;
}

// -------------------------------------------------------------
//  Tick
// -------------------------------------------------------------
static void fpbTick(){
  // Cola vacia y nada a la vista: salida barata. Es lo que hace que
  // este modulo no cueste FPS cuando no hay notificaciones.
  if(fpbState == FPB_HIDDEN && fpbQueueN == 0) return;

  // Arrancar el siguiente de la cola.
  if(fpbState == FPB_HIDDEN){
    if(!fpbCanShow()) return;              // se espera: la cola no se pierde
    fpbCur = fpbQueue[0];
    for(int i = 1; i < fpbQueueN; i++) fpbQueue[i - 1] = fpbQueue[i];
    fpbQueueN--;
    fpbLand = gLand;
    fpbSlide = 0.0f;
    fpbState = FPB_ARMED;
    fpbT0 = millis();
  }

  // La orientacion cambio debajo (se abrio un juego, Modo PC, un
  // video): la banda capturada ya no describe nada.
  if(gLand != fpbLand){ fpbAbandon(); return; }
  // Alguien tomo la pantalla: se abandona sin restaurar.
  if(!fpbCanShow()){ fpbAbandon(); return; }

  if(fpbState == FPB_ARMED){
    if(!fpbArm()){
      // Sin memoria para la banda: NO se dibuja. La notificacion no
      // se pierde -- esta en el Centro -- y el sistema no se queda
      // con un overlay que no puede pintar.
      fpbAbandon();
      return;
    }
    fpbState = FPB_IN;
    fpbT0 = millis();
    fpbCompose(0.0f);
    return;
  }
  if(!fpbBandReady()){ fpbAbandon(); return; }

  const uint32_t e = millis() - fpbT0;
  switch(fpbState){
    case FPB_IN: {
      float p = (e >= FPB_IN_MS) ? 1.0f : (float)e / (float)FPB_IN_MS;
      p = 1.0f - (1.0f - p) * (1.0f - p);
      fpbCompose(p);
      if(e >= FPB_IN_MS){ fpbState = FPB_SHOWN; fpbT0 = millis(); }
      break;
    }
    case FPB_SHOWN:
      // Mientras el dedo lo arrastra NO caduca: nadie quiere que se
      // le vaya lo que esta tocando.
      if(fpbDragging || fpbSlide != 0.0f){ fpbCompose(1.0f); fpbT0 = millis(); break; }
      fpbCompose(1.0f);
      if(e >= FPB_HOLD_MS) fpbClose();
      break;
    case FPB_OUT: {
      // Sale por arriba, por donde entro.
      float p = (e >= FPB_OUT_MS) ? 1.0f : (float)e / (float)FPB_OUT_MS;
      fpbCompose(1.0f - p);
      if(e >= FPB_OUT_MS){
        fpbRestore();
        fpbFreeBand();
        fpbState = FPB_HIDDEN;
        fpbSlide = 0.0f;
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
#define FPC_EDGE_W    26      // franja de borde que arma el gesto
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
  // 2) Las del propio sistema, las que siguen vivas en la isla.
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
    // EL GESTO SOLO CUENTA SI NACIO EN EL BORDE. Sin esto, cualquier
    // deslizamiento dentro de una app abriria el panel por accidente,
    // que es justo lo que no puede pasar en un juego.
    if(!(T.pressed && T.startX < FPC_EDGE_W && T.startY > 40)) return false;
    fpcBuild();
    fgScrollReset(&fpcScr, FPC_HDR_H, SCR_H - 8);
    fpcDragging = true;
    fpcDragMoved = false;
    fpcDragX0 = T.x;
    fpcDragBase = fpcX;
    fpcAnimOn = false;
    fpcDirty = true;
    return true;
  }
  if(!fpcCanOpen() && fpcOpen()){ fpcForceClose(); return false; }

  // Arrastre del panel.
  if(fpcDragging){
    if(T.pressed){
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
      return true;
    }
    fpcDragging = false;
    if(!fpcDragMoved){
      // Toque en el borde sin arrastrar: no se abre nada. Un panel que
      // salta con un roce del borde es peor que no tenerlo.
      fpcForceClose();
      return false;
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
