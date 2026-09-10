// #############################################################
// ##  FLEX OS ULTRA  ·  AVISO GLOBAL DE POSIBLE CAIDA
// ##  ----------------------------------------------------------
// ##  El overlay que aparece ENCIMA de lo que haya en pantalla cuando
// ##  la deteccion de caidas supera su umbral de confianza. Dos
// ##  maquetas de verdad -- una vertical y otra horizontal -- porque
// ##  girar y estirar la vertical daria un cuadro con el texto de
// ##  canto y los botones fuera del alcance del pulgar.
// ##
// ##  POR QUE NO ES UNA NOTIFICACION DE LA ISLA
// ##  ----------------------------------------------------------
// ##  La isla dinamica (FlexOS_Ultra_Notif.h) solo vive en el
// ##  escritorio: fuera de ST_HOME se pausa a proposito, porque
// ##  compone sobre homeBuf. Un aviso de caida tiene que verse
// ##  ESTANDO EN CUALQUIER SITIO -- dentro de una app, con el
// ##  navegador abierto, en Juegos --, asi que sigue el patron del
// ##  OTA y de la tarjeta del cronometro: captura la banda que va a
// ##  ocupar, se dibuja encima y, al cerrarse, la devuelve pixel a
// ##  pixel. NO se crea un segundo gestor de notificaciones: los
// ##  avisos normales del sistema siguen saliendo por notifPush.
// ##
// ##  EXCEPCION DEX / MODO PC
// ##  ----------------------------------------------------------
// ##  Con el escritorio de Modo PC en primer plano NO se dibuja nada:
// ##  ahi el usuario esta trabajando con ventanas y un cuadro modal a
// ##  pantalla completa seria una interrupcion desproporcionada. El
// ##  evento se registra igual en el historial (eso ocurre en
// ##  dcSensorTick, antes de llegar aqui) y queda disponible en Flex
// ##  Device Care -> Historial y en el Post-Impact Check posterior.
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
#include "FlexOS_Ultra_DeviceTests.h"   // eslabon anterior de la cadena

// ---- Estado del overlay ----
// FA_ARMED es el estado entre "hay que ensenar el aviso" y "la banda ya
// esta capturada". Existe para que la DETECCION no pague el trabajo
// pesado: faRaise() la llama dcSensorTick(), que corre en la parte
// temprana de loop() junto al tactil y al barrido I2C, y ahi no puede
// haber ni una reserva de medio megabyte ni dos memcpy de ese tamano ni
// una composicion entera. Todo eso ocurre en faTick(), en la fase de
// dibujo, que es donde el aviso ya es dueno de la pantalla.
enum { FA_HIDDEN = 0, FA_ARMED, FA_IN, FA_SHOWN, FA_OUT };
static int       faState  = FA_HIDDEN;
static uint32_t  faT0     = 0;
static bool      faLand   = false;      // maqueta con la que se dibujo
static FlexFallEvent faEvt;
static bool      faHaveEvt = false;
// Copia de la banda que ocupa el aviso, para devolverla al cerrarse.
//
// SE PIDE AL ABRIR Y SE SUELTA AL CERRAR, y ahi se aparta a proposito de
// la tarjeta del cronometro, que conserva la suya: esa se abre muchas
// veces al dia y pedir y soltar su banda en cada apertura seria
// castigar la PSRAM sin motivo. Un aviso de caida es -- con suerte --
// excepcional, y retener medio megabyte permanentemente por algo que
// puede no ocurrir nunca no se sostiene. Ademas la banda del apaisado
// es mucho mayor que la del vertical (en landscape la x logica ES la
// fila fisica), asi que una reserva unica tendria que ser la peor de
// las dos.
static uint16_t* faBak = NULL;
static size_t    faBakCap = 0;
static int       faBakY0 = 0, faBakY1 = -1;

// Suelta el BUFFER. No toca la geometria a proposito: son dos cosas
// distintas y mezclarlas costo un bloqueo de la interfaz. faRaise pedia
// la banda con faBand(), llamaba aqui para redimensionar el buffer y se
// llevaba por delante el faBakY1 recien calculado; a partir de ahi
// faCompose y faRestore salian sin hacer nada -- el aviso era dueno de
// la pantalla y no dibujaba NUNCA. Quien quiera invalidar la geometria
// lo dice: faInvalidateBand().
static void faFreeBand(){
  if(faBak){ heap_caps_free(faBak); faBak = NULL; }
  faBakCap = 0;
}
static void faInvalidateBand(){ faBakY1 = faBakY0 - 1; }
// La banda esta lista cuando hay buffer Y geometria. Es el INVARIANTE
// del overlay: sin esto no puede dibujar, y sin poder dibujar no puede
// quedarse la pantalla.
static inline bool faBandReady(){ return faBak && faBakY1 >= faBakY0; }

#define FA_ANIM_MS   220
// VALVULA DE SEGURIDAD. El aviso es modal: mientras esta a la vista es
// el unico que dibuja y el unico que recibe toques, asi que si algo lo
// dejara colgado el sistema quedaria inaccesible. Pasado este tiempo
// sin que nadie lo toque se retira solo. NO se pierde nada: el evento
// ya esta en el historial y el Post-Impact Check sigue disponible en
// Flex Device Care.
#define FA_AUTO_MS   20000
// Zonas pulsables del aviso.
enum { FA_HIT_NONE = 0, FA_HIT_CHECK, FA_HIT_DISMISS };
static int16_t faBtnCk[4] = {0,0,0,0};   // x0,y0,x1,y1 en coords FISICAS
static int16_t faBtnDs[4] = {0,0,0,0};

// #############################################################
// ##  ¿SE PUEDE ENSENAR AHORA MISMO?
// ##  ------------------------------------------------------
// ##  Las mismas pantallas en las que el sistema ya decide no
// ##  notificar nada (arranque, OOBE, bloqueo, alta de PIN, boveda,
// ##  apagado) mas las que poseen la pantalla en exclusiva (OTA,
// ##  restablecimiento de fabrica, optimizador) y el Modo PC.
// #############################################################
static bool faDexActive(){
  // Modo PC en primer plano: la app 4 abierta y el motor en landscape.
  return (gState == ST_APP && gAppId == IC_MODOPC) || gHosted;
}
static bool faCanShow(){
  if(notifSecureScreen()) return false;
  if(gFrPending || gState == ST_FACTORY || gSafeMode) return false;
  if(flexOtaOwnsScreen() || optActive()) return false;
  if(faDexActive()) return false;                 // excepcion DeX: se registra, no se dibuja
  if(gSuspOn) return false;                       // pantalla apagada: no se pinta a oscuras
  if(appTrOwnsScreen()) return false;             // hay una transicion de app dibujando
  if(cronoCardVisible()) return false;            // ya hay un modal encima
  // LA CORTINA DEL PANEL RAPIDO. loop() le cede la pantalla ANTES de
  // llegar al aviso, asi que abrirse debajo de ella dejaria el cuadro
  // congelado y con una captura que ya no describe lo que hay detras.
  if(qsPanelY != 0 || qsAnimOn) return false;
  // Dentro del propio Post-Impact Check el aviso sobra: el usuario ya
  // esta justo donde el aviso le llevaria.
  if(gState == ST_APP && gAppId == IC_DEVCARE &&
     (dcScreen == DC_POST || dcScreen == DC_RESULT)) return false;
  return true;
}

static inline bool faVisible(){ return faState != FA_HIDDEN; }

// #############################################################
// ##  AVISO EN ESPERA
// ##  ------------------------------------------------------
// ##  Si la caida se detecta con la pantalla ocupada (cortina abierta,
// ##  OTA descargando, bloqueo, pantalla suspendida, una transicion en
// ##  curso), el aviso NO se pierde: se queda esperando y sale en
// ##  cuanto la pantalla vuelve a ser normal. Es la misma politica que
// ##  ya sigue la isla de notificaciones -- un aviso que caduca
// ##  mientras desbloqueas es un aviso que nunca llegaste a ver.
// ##
// ##  Pasada la ventana deja de esperar: un cuadro de "posible caida"
// ##  que aparece diez minutos despues del golpe confunde mas de lo
// ##  que ayuda. El evento sigue en el historial, que es donde tiene
// ##  que estar.
// ##
// ##  DeX es la excepcion: ahi no se espera a nada, porque ahi no se
// ##  ensena nunca.
// #############################################################
#define FA_PEND_MS 60000
static FlexFallEvent faPendEvt;
static bool          faPending = false;
static uint32_t      faPendMs  = 0;

// #############################################################
// ##  GEOMETRIA
// ##  ------------------------------------------------------
// ##  VERTICAL: tarjeta ancha centrada en el tercio superior, con los
// ##  dos botones apilados -- que es donde cae el pulgar en 480x800.
// ##  HORIZONTAL: tarjeta a lo ancho con el icono a la izquierda, el
// ##  texto en el centro y los dos botones EN FILA a la derecha. No es
// ##  la vertical girada: cambia el reparto, no la escala.
// #############################################################
#define FA_V_X    24
#define FA_V_W    (SCR_W - 48)
#define FA_V_Y    120
#define FA_V_H    300
// Landscape: coordenadas LOGICAS (lx 0..799, ly 0..479).
#define FA_L_X    120
#define FA_L_W    (SCR_H - 240)
#define FA_L_Y    120
#define FA_L_H    240

// Banda FISICA (filas del panel) que ocupa el aviso en cada maqueta.
// En landscape la x logica ES la fila fisica, por eso la banda se
// calcula del eje contrario.
static void faBand(bool land, int &y0, int &y1){
  if(land){ y0 = FA_L_X - 12; y1 = FA_L_X + FA_L_W + 12; }
  else    { y0 = FA_V_Y - 12; y1 = FA_V_Y + FA_V_H + 12; }
  if(y0 < 0) y0 = 0;
  if(y1 > SCR_H - 1) y1 = SCR_H - 1;
}

// Icono de aviso: triangulo con exclamacion, dibujado (no un glifo de
// fuente) para que escale limpio en las dos maquetas.
static void faWarnIcon(int cx, int cy, int r, uint16_t col, uint16_t on){
  fillTriangle(cx, cy - r, cx - r, cy + r * 3 / 4, cx + r, cy + r * 3 / 4, col);
  fillRect(cx - r / 10 - 1, cy - r / 3, r / 5 + 2, r * 5 / 6, on);
  fillCircle(cx, cy + r / 2, r / 7 + 1, on);
}

// #############################################################
// ##  DIBUJO
// ##  ------------------------------------------------------
// ##  Se compone en bbuf sobre la copia de la banda y se publica con
// ##  UN solo present(): mismo camino anti-parpadeo que la tarjeta del
// ##  cronometro. `p` (0..1) es la entrada/salida animada.
// #############################################################
static void faDrawVertical(float p){
  int h = (int)(FA_V_H * (0.72f + 0.28f * p));
  int y = FA_V_Y + (FA_V_H - h) / 2;
  uint8_t a = (uint8_t)(255.0f * p);
  // Velo detras del cuadro: solo dentro de su banda, para no repintar
  // la pantalla entera por un aviso.
  int by0, by1; faBand(false, by0, by1);
  fillRectA(0, by0, SCR_W, by1 - by0 + 1, TH_SCRIM, (uint8_t)(120 * p));

  uiSurfaceA(FA_V_X, y, FA_V_W, h, 28, UIS_ELEVATED, a);
  drawRoundRect(FA_V_X, y, FA_V_W, h, 28, TH_BORDER);
  if(p < 0.6f) return;                      // el contenido entra al final

  int cx = SCR_W / 2;
  faWarnIcon(cx, y + 54, 30, TH_WARN, TH_WIN);
  int fs = uiFontFit(dct(DCS_FALLTITLE), FA_V_W - 24, 3);
  drawTextC(cx, y + 96, dct(DCS_FALLTITLE), fs, TH_TXT);
  drawTextC(cx, y + 134, dct(DCS_FALLBODY), 1, TH_TXT2);
  if(faHaveEvt){
    char b[64];
    snprintf(b, sizeof(b), "%s %u/100  \xC2\xB7  %s %.1f g",
             dct(DCS_CONFIDENCE), (unsigned)faEvt.confidence,
             dct(DCS_IMPACT), (double)faEvt.peakG);
    drawTextC(cx, y + 158, b, 1, TH_MUTE);
  }
  int bw = FA_V_W - 48, bx = FA_V_X + 24;
  int b1y = y + h - 118, b2y = y + h - 60;
  fillRoundRect(bx, b1y, bw, 46, 23, TH_PRIM);
  dcBtnGlyphCheck(bx + 30, b1y + 23, 8, TH_ONACC);
  drawTextC(bx + bw / 2 + 16, b1y + 14, dct(DCS_REVIEW), 2, TH_ONACC);
  fillRoundRect(bx, b2y, bw, 44, 22, TH_SURF);
  drawTextC(bx + bw / 2, b2y + 13, dct(DCS_DISMISS), 2, TH_TXT2);

  faBtnCk[0] = (int16_t)bx;      faBtnCk[1] = (int16_t)b1y;
  faBtnCk[2] = (int16_t)(bx + bw); faBtnCk[3] = (int16_t)(b1y + 46);
  faBtnDs[0] = (int16_t)bx;      faBtnDs[1] = (int16_t)b2y;
  faBtnDs[2] = (int16_t)(bx + bw); faBtnDs[3] = (int16_t)(b2y + 44);
}

static void faDrawLandscape(float p){
  int h = (int)(FA_L_H * (0.72f + 0.28f * p));
  int y = FA_L_Y + (FA_L_H - h) / 2;
  uint8_t a = (uint8_t)(255.0f * p);
  fillRectA(FA_L_X - 12, 0, FA_L_W + 24, SCR_W, TH_SCRIM, (uint8_t)(120 * p));

  uiSurfaceA(FA_L_X, y, FA_L_W, h, 26, UIS_ELEVATED, a);
  drawRoundRect(FA_L_X, y, FA_L_W, h, 26, TH_BORDER);
  if(p < 0.6f) return;

  // Reparto propio del apaisado: icono | texto | botones EN FILA.
  int ix = FA_L_X + 60, iy = y + h / 2;
  faWarnIcon(ix, iy, 34, TH_WARN, TH_WIN);
  int tx = FA_L_X + 116;
  drawText(tx, y + 34, dct(DCS_FALLTITLE), 3, TH_TXT);
  drawText(tx, y + 70, dct(DCS_FALLBODY), 1, TH_TXT2);
  if(faHaveEvt){
    char b[64];
    snprintf(b, sizeof(b), "%s %u/100  \xC2\xB7  %s %.1f g",
             dct(DCS_CONFIDENCE), (unsigned)faEvt.confidence,
             dct(DCS_IMPACT), (double)faEvt.peakG);
    drawText(tx, y + 90, b, 1, TH_MUTE);
  }
  int bw = 230, bh = 46;
  int b1x = tx, b2x = tx + bw + 16, byy = y + h - bh - 26;
  fillRoundRect(b1x, byy, bw, bh, bh / 2, TH_PRIM);
  dcBtnGlyphCheck(b1x + 28, byy + bh / 2, 8, TH_ONACC);
  drawTextC(b1x + bw / 2 + 14, byy + 14, dct(DCS_REVIEW), 2, TH_ONACC);
  fillRoundRect(b2x, byy, bw, bh, bh / 2, TH_SURF);
  drawTextC(b2x + bw / 2, byy + 14, dct(DCS_DISMISS), 2, TH_TXT2);

  faBtnCk[0] = (int16_t)b1x; faBtnCk[1] = (int16_t)byy;
  faBtnCk[2] = (int16_t)(b1x + bw); faBtnCk[3] = (int16_t)(byy + bh);
  faBtnDs[0] = (int16_t)b2x; faBtnDs[1] = (int16_t)byy;
  faBtnDs[2] = (int16_t)(b2x + bw); faBtnDs[3] = (int16_t)(byy + bh);
}

static void faCompose(float p){
  if(!faBandReady()) return;
  int c0 = gClipY0, c1 = gClipY1, cx0 = gClipX0, cx1 = gClipX1;
  bool wl = gLand;
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;
  setBuf(bbuf);
  memcpy(bbuf + (size_t)faBakY0 * SCR_W, faBak,
         (size_t)SCR_W * (faBakY1 - faBakY0 + 1) * 2);
  gLand = faLand;
  if(faLand) faDrawLandscape(p);
  else       faDrawVertical(p);
  gLand = wl;
  present(faBakY0, faBakY1);
  setBuf(fb);
  gClipY0 = c0; gClipY1 = c1; gClipX0 = cx0; gClipX1 = cx1;
}

// Devuelve la banda a como estaba antes del aviso.
static void faRestore(){
  if(!faBandReady()) return;
  fbLock();
  memcpy(fb + (size_t)faBakY0 * SCR_W, faBak,
         (size_t)SCR_W * (faBakY1 - faBakY0 + 1) * 2);
  fbUnlock();
  flxFlush(faBakY0, faBakY1);
  faInvalidateBand();        // ya devuelta: la copia no describe nada nuevo
}

// #############################################################
// ##  APERTURA
// ##  ------------------------------------------------------
// ##  La llama dcSensorTick() cuando una evaluacion supera el umbral.
// ##  El evento YA esta en el historial cuando se llega aqui: si no se
// ##  puede dibujar (DeX, OTA, bloqueo...), lo unico que se pierde es
// ##  el cuadro, nunca el registro.
// #############################################################
static void faRaise(const FlexFallEvent* e){
  if(e){ faEvt = *e; faHaveEvt = true; }
  if(faVisible()) return;                    // ya hay uno a la vista: no se apilan
  if(faDexActive()){                         // excepcion DeX: ni ahora ni luego
    faPending = false;
    return;
  }
  if(!faCanShow()){                          // ocupado AHORA: se queda esperando
    if(e) faPendEvt = *e;
    if(!faPending) faPendMs = millis();      // la ventana cuenta desde el evento
    faPending = true;
    return;
  }
  faPending = false;

  // Solo se ARMA. La reserva, la captura y el primer cuadro los hace
  // faTick() en la fase de dibujo (ver faArm). Aqui no se toca ni la
  // PSRAM ni el framebuffer: esta funcion corre dentro del tick del
  // sensor, en la misma vuelta que el tactil.
  faLand  = gLand;
  faState = FA_ARMED;
  faT0    = millis();
  touchDropAll();
}

// #############################################################
// ##  PREPARACION  ·  reserva, captura y primer cuadro
// ##  ------------------------------------------------------
// ##  Corre UNA sola vez, desde faTick(), o sea con el aviso ya dueno
// ##  de la pantalla y con fb conteniendo el ultimo cuadro publicado.
// ##  Devuelve false si no se pudo preparar; entonces el aviso NO se
// ##  queda la pantalla -- se retira y el usuario recibe el aviso por
// ##  la via normal del sistema. El evento ya esta en el historial, asi
// ##  que lo unico que se pierde es el cuadro.
// #############################################################
static bool faArm(){
  faBand(faLand, faBakY0, faBakY1);
  if(faBakY1 < faBakY0) return false;                 // banda imposible
  size_t need = (size_t)SCR_W * (faBakY1 - faBakY0 + 1) * 2;
  // La reserva NO puede comerse la proteccion del sistema: si sacar
  // medio megabyte dejaria la PSRAM por debajo del suelo, no se saca.
  if(faBakCap < need && memFreePsram() < FLEXMEM_CRIT_BYTES + need) return false;
  if(faBakCap < need) faFreeBand();                   // solo el buffer; la banda se conserva
  if(!faBak){
    faBak = (uint16_t*)heap_caps_aligned_alloc(64, need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    faBakCap = faBak ? need : 0;
  }
  if(!faBak) return false;
  fbLock();
  memcpy(faBak, fb + (size_t)faBakY0 * SCR_W, need);
  fbUnlock();
  return faBandReady();
}

// Reintento del aviso en espera. Lo llama loop() en cada vuelta, junto
// al tick del sensor: es barato (dos comparaciones) cuando no hay nada
// esperando, que es siempre.
static void faPendingTick(){
  if(!faPending) return;
  if(faDexActive()){ faPending = false; return; }
  if(millis() - faPendMs > FA_PEND_MS){ faPending = false; return; }
  if(faVisible() || !faCanShow()) return;
  FlexFallEvent e = faPendEvt;
  faPending = false;
  faRaise(&e);
}

static void faAbandon();          // definido justo debajo; lo usa faClose

static void faClose(){
  if(faState == FA_HIDDEN || faState == FA_OUT) return;
  if(faState == FA_ARMED){ faAbandon(); return; }   // aun no hay nada que retirar
  faState = FA_OUT;
  faT0    = millis();
}
// Cierre inmediato SIN restaurar. Lo usa quien toma la pantalla por su
// cuenta (una app que se abre, el OTA): la banda guardada ya no
// describe lo que hay debajo y devolverla pintaria un fotograma muerto.
static void faAbandon(){
  faState = FA_HIDDEN;
  faFreeBand();
  faInvalidateBand();
}
// Abandono que ademas deja el aviso EN ESPERA: se usa cuando el usuario
// navega con la barra del sistema. La pantalla nueva se pinta entera por
// su cuenta, asi que no hay nada que restaurar -- pero el aviso no se
// pierde: vuelve a salir alli mismo dentro de su ventana.
static void faAbandonToPending(){
  if(faHaveEvt){ faPendEvt = faEvt; if(!faPending) faPendMs = millis(); faPending = true; }
  faAbandon();
}

// #############################################################
// ##  TICK  ·  animacion y toque
// ##  ------------------------------------------------------
// ##  Se engancha en loop() con el mismo patron que la tarjeta del
// ##  cronometro: mientras el aviso esta a la vista es el UNICO que
// ##  dibuja, y la pantalla de debajo no hace tick. Asi la banda
// ##  capturada sigue siendo valida y el cierre es pixel a pixel.
// #############################################################
static void faTick(){
  if(faState == FA_HIDDEN) return;

  // La orientacion cambio debajo del aviso (se abrio Juegos, Modo PC,
  // un video apaisado): la banda capturada ya no vale.
  if(gLand != faLand){ faAbandon(); return; }
  // Alguien tomo la pantalla en exclusiva: se abandona sin restaurar.
  if(flexOtaOwnsScreen() || gFrPending || optActive()){ faAbandon(); return; }
  if(qsPanelY != 0 || qsAnimOn){ faAbandon(); return; }   // la cortina dibuja encima

  // ---- PREPARACION, una sola vez ------------------------------------
  // Aqui se paga la reserva, la captura y el primer cuadro: en la fase
  // de dibujo, no en el tick del sensor. Si no se puede preparar, el
  // aviso NO se queda la pantalla ni una vuelta mas.
  if(faState == FA_ARMED){
    if(!faArm()){
      faAbandon();
      sysNotify(dct(DCS_EVFALL), dct(DCS_REVIEW));   // por la via normal del sistema
      return;
    }
    faState = FA_IN;
    faT0    = millis();
    faCompose(0.0f);
    return;
  }

  // ---- INVARIANTE ---------------------------------------------------
  // El aviso solo puede ser dueno de la pantalla si PUEDE dibujar. Si la
  // banda dejara de estar lista por cualquier motivo, se suelta la
  // pantalla en el acto en vez de quedarse con ella sin pintar: eso es
  // exactamente lo que congelaba la interfaz (todo muerto menos el panel
  // rapido, que loop() despacha antes que este bloque).
  if(!faBandReady()){ faAbandon(); return; }

  // ---- LA BARRA DEL SISTEMA SIGUE VIVA ------------------------------
  // El aviso es modal para la app de debajo, pero no puede secuestrar la
  // navegacion: esos 64 px son del sistema. Si el usuario navega, el
  // aviso se retira sin restaurar (la pantalla nueva se pinta entera por
  // su cuenta) y queda EN ESPERA para volver a salir alli.
  if(navBarVisible() && navBarHandle()){ faAbandonToPending(); return; }

  uint32_t e = millis() - faT0;
  if(faState == FA_IN){
    float p = (e >= FA_ANIM_MS) ? 1.0f : (float)e / (float)FA_ANIM_MS;
    p = 1.0f - (1.0f - p) * (1.0f - p);
    faCompose(p);
    if(e >= FA_ANIM_MS) faState = FA_SHOWN;
    return;
  }
  if(faState == FA_OUT){
    float p = (e >= FA_ANIM_MS) ? 0.0f : 1.0f - (float)e / (float)FA_ANIM_MS;
    if(e >= FA_ANIM_MS){
      faRestore();
      faState = FA_HIDDEN;
      faFreeBand();
      faInvalidateBand();
      touchDropAll();
      return;
    }
    faCompose(p);
    return;
  }

  // FA_SHOWN: el aviso es MODAL y no caduca a los cinco segundos como
  // una notificacion normal -- una caida la cierra el usuario. Lo unico
  // que lo retira solo es la valvula de seguridad de FA_AUTO_MS.
  if(e >= FA_AUTO_MS){ faClose(); return; }
  if(T.tap){
    int px = T.x, py = T.y;
    // En landscape los botones estan en coordenadas logicas: se traduce
    // el toque con el mismo mapeo que usa Modo PC.
    if(faLand){ int lx = T.y, ly = (SCR_W - 1) - T.x; px = lx; py = ly; }
    T.tap = false;
    if(px >= faBtnCk[0] && px < faBtnCk[2] && py >= faBtnCk[1] && py < faBtnCk[3]){
      // "Revisar dispositivo": abre Device Care y ARRANCA el
      // Post-Impact Check. El usuario no tiene que buscar nada.
      faRestore();
      faState = FA_HIDDEN;
      faFreeBand();
      faInvalidateBand();
      touchDropAll();
      dcPrev = DC_POST;
      dcPendingPost = true;      // lo recoge dcEnter/dcResume al abrirse la app
      enterApp(IC_DEVCARE);
      return;
    }
    if(px >= faBtnDs[0] && px < faBtnDs[2] && py >= faBtnDs[1] && py < faBtnDs[3]){
      faClose();
      return;
    }
    // Un toque fuera de los botones NO cierra: es un aviso modal y
    // cerrarlo sin querer con la palma seria justo lo que no debe pasar.
  }
  // Sin animacion en reposo: el cuadro ya esta publicado y no cambia.
  T.swipeLeft = T.swipeRight = T.swipeUp = T.swipeDown = false;
  T.pressed = T.released = false;
}
