// #############################################################
// ##  FLEX OS ULTRA  ·  ISLA DINAMICA  ·  notificaciones
// ##  ----------------------------------------------------------
// ##  Cola de notificaciones, banda de la isla, animacion, descarte por
// ##  arrastre y las pantallas en las que no se notifica nada.
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
#include "FlexOS_Ultra_Conn.h"   // eslabon anterior de la cadena

// #############################################################
// ##  ISLA DINAMICA · logica y render  (FASE 1, parche anti-flicker)
// ##  ------------------------------------------------------
// ##  FIX aplicado tras el bug de parpadeo + tarjetas pegadas:
// ##  la isla ya NO compone sobre fb (el buffer que DMA2D transfiere).
// ##  Ahora restaura la banda limpia
// ##  copiando desde homeBuf hacia bbuf, dibuja las tarjetas sobre bbuf, y cruza
// ##  a fb con un unico present() atomico. bbuf es de un solo
// ##  escritor (el loop task); nadie mas lo lee, y flxFlush espera
// ##  a que la transferencia termine antes del cuadro siguiente.
// ##  Por eso el compose (restore+dibujar+present) solo corre con
// ##  gState==ST_HOME: homeBuf solo es un fondo valido ahi. El
// ##  avance de fases sigue sin condicion (es aritmetica pura).
// ##
// ##  En Fase 1 NO hay deteccion I2C real: las notificaciones se
// ##  disparan con un trigger de prueba (demo al primer Home + tap
// ##  arriba-derecha) para validar render/animacion/descarte de
// ##  forma AISLADA.
// ##
// ##  DESVIACION DELIBERADA respecto al plan original: el vidrio
// ##  se RE-HORNEA cada frame (drawLiquidGlassPanel) en vez de
// ##  "hornear una vez". Se hace asi solo porque son <=3 tarjetas
// ##  pequenas (448x64) y el coste es bajo; permite que el
// ##  deslizamiento reutilice el mismo camino sin cachear un buffer
// ##  por tarjeta. El blur costoso (pantalla completa) se sigue evitando.
// #############################################################

// #############################################################
// ##  PANTALLAS EN LAS QUE NO SE NOTIFICA NADA
// ##  ------------------------------------------------------
// ##  La isla del escritorio consulta una sola funcion para no dibujarse
// ##  sobre pantallas sensibles, incluido el alta del PIN.
// ##
// ##  Las notificaciones NO se pierden: se quedan en la cola, sin
// ##  armar, y su animacion de entrada y su cuenta atras arrancan
// ##  cuando el usuario vuelve a una pantalla normal. Un aviso que
// ##  caduca mientras estas desbloqueando es un aviso que nunca
// ##  llegaste a ver.
// #############################################################
static bool notifSecureScreen(){
  switch(gState){
    case ST_SPLASH:            // arranque: aun no hay sesion
    case ST_OOBE_LANG:         // primera configuracion
    case ST_OOBE_NAME:
    case ST_OOBE_ACCOUNT:
    case ST_LOCK:              // bloqueo
    case ST_LOCKSETUP:         // alta y VERIFICACION de PIN/contrasena
    case ST_VAULT:             // Flex Vault: clave y contenido privado
    case ST_POWEROFF_CONFIRM:  // apagado en curso
    case ST_POWEROFF_ANIM:
      return true;
    default:
      return false;
  }
}

// Restaura la banda limpia EN bbuf, copiando desde homeBuf (siempre al dia:
// se recompone solo en cada cambio de minuto, al salir de edicion, etc.).
// homeBuf es la fuente y bbuf el lienzo de trabajo. Ya no hace falta
// snapshot manual (notifSnapshotBg desaparece).
static void notifRestoreBg(){
  if(!homeBuf || !bbuf) return;
  memcpy(bbuf + (size_t)NOTIF_BAND_TOP * SCR_W, homeBuf + (size_t)NOTIF_BAND_TOP * SCR_W,
         (size_t)SCR_W * NOTIF_BAND_H * 2);
}

// Quita visualmente la isla antes de que la Caja de aplicaciones empiece a
// subir. No descarta ni desarma la notificacion: congela su reloj y la deja en
// cola para que vuelva sobre un fondo nuevo al regresar al escritorio.
static void notifPauseForDrawer(){
  if(gNotifCount == 0 && !notifBandOn) return;
  if(!notifPaused){ notifPaused = true; notifPauseT0 = millis(); }
  notifDragIdx = -1;
  if(!notifBandOn || !homeBuf || !bbuf) return;
  notifRestoreBg();
  present(NOTIF_BAND_TOP, NOTIF_BAND_BOT - 1);
  setBuf(fb);
  notifBandOn = false;
}

// Huella de identidad de un aviso real. FNV-1a sobre el tipo, la direccion
// I2C y el nombre. El texto secundario NO entra: "0x18 detectado" y "0x18
// listo" son el MISMO dispositivo y deben refrescar la tarjeta, no anadir otra.
static uint32_t notifKeyOf(const DetectedModule* m){
  uint32_t h = 2166136261u;
  if(m){
    h = (h ^ (uint8_t)m->type)    * 16777619u;
    h = (h ^ (uint8_t)m->i2cAddr) * 16777619u;
    for(const char* p = m->name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  }
  return h ? h : 1u;               // 0 queda reservado a "sin huella"
}

// Ranura activa con esa huella, o -1. Las que ya se estan yendo (NP_OUT)
// no cuentan: el usuario acaba de descartarlas y volver a rellenarlas
// seria devolverle el aviso que quito.
static int notifFindKey(uint32_t key){
  if(!key) return -1;
  for(int i = 0; i < gNotifCount; i++)
    if(gNotifs[i].active && gNotifs[i].phase != NP_OUT && gNotifs[i].key == key) return i;
  return -1;
}

// Encola una notificacion a partir de un modulo
static void notifPush(const DetectedModule* m){
  uint32_t key = notifKeyOf(m);
  int dup = notifFindKey(key);
  if(dup >= 0){
    // Ya esta en la cola: se refresca en su sitio. Si estaba VISIBLE se le
    // reinicia la cuenta atras (el aviso vuelve a ser reciente); si estaba
    // esperando, sigue esperando su turno y no se cuela por delante.
    gNotifs[dup].mod = *m;
    if(gNotifs[dup].armed) gNotifs[dup].bornMs = millis();
    return;
  }
  if(gNotifCount >= NOTIF_MAX) return;           // cola llena: se descarta (Fase 1)
  Notification* n = &gNotifs[gNotifCount++];
  n->mod    = *m;
  n->active = true;
  n->phase  = NP_IN;
  n->bornMs = millis();
  n->slideX = 0.0f;
  n->armed  = false;            // se arma (entrada + 5 s) al hacerse visible en Home
  n->key    = key;
}

// Elimina la ranura idx y compacta la cola
static void notifRemove(int idx){
  if(idx < 0 || idx >= gNotifCount) return;
  for(int j = idx; j < gNotifCount - 1; j++) gNotifs[j] = gNotifs[j + 1];
  gNotifCount--;
  // Borrar TODA la ranura, no solo active. Si era la ultima conservaba
  // armed=true y phase=NP_OUT; cualquier lectura defensiva posterior podia
  // confundirla con una tarjeta que aun estaba saliendo.
  memset(&gNotifs[gNotifCount], 0, sizeof(gNotifs[gNotifCount]));
  if(notifDragIdx == idx)      notifDragIdx = -1;
  else if(notifDragIdx > idx)  notifDragIdx--;
}

// Ease-out cubica (0..1)
static inline float notifEaseOut(float p){ float q = 1.0f - p; return 1.0f - q * q * q; }

// Cola de burbuja de chat: un triangulo apuntando hacia ARRIBA, porque las
// tarjetas de notificacion caen desde el borde superior de la pantalla (no
// hay un icono de app en el Home al que apuntar -- estas son detecciones de
// hardware I2C via hwDetectTick(), no notificaciones que vengan de una app
// abierta). Solido, no vidrio: es demasiado pequeña para que el blur se
// note, y agrandar el panel solo para la cola no vale la pena.
static void notifDrawTail(int cx, int topY, uint16_t col){
  fillTriangle(cx - 8, topY, cx + 8, topY, cx, topY - 9, col);
}
// Dibuja una tarjeta en la coordenada Y dada (aplica su slideX horizontal)
static void notifDrawCard(Notification* n, int cardY){
  int x = NOTIF_MARGIN_X + (int)n->slideX;       // al deslizar a la izq, x se vuelve negativo
  int y = cardY, w = NOTIF_CARD_W, h = NOTIF_CARD_H;
  notifDrawTail(x + w / 2, y, thCard2());   // primero: la tarjeta se dibuja justo debajo, sin solaparla
  // SUPERFICIE DEL SISTEMA. Antes esto llamaba a drawLiquidGlassPanel() SIN
  // MIRAR uiGlass: con el estilo Plano elegido, las notificaciones seguian
  // saliendo de vidrio -- el fallo que se reporto. uiSurface decide el material
  // una sola vez: relleno solido de la paleta en Plano, panel de vidrio con su
  // tinte en Liquid Glass. Recorta x<0 conservando el borde derecho, asi que el
  // deslizamiento a la izquierda sigue saliendo natural.
  uiSurface(x, y, w, h, NOTIF_RAD, UIS_ELEVATED);
  // Degradado estilo burbuja (mas claro arriba). Es BRILLO DE MATERIAL, asi que
  // solo existe con Liquid Glass: en Plano la superficie es solida y sin brillo,
  // que es justo lo que pide ese estilo. Fila a fila con glInset() -- igual que
  // drawLiquidGlassPanel -- para no salirse de las esquinas redondeadas.
  if(uiGlass){
    for(int j = 0; j < h; j++){
      int ins = glInset(j, h, NOTIF_RAD);
      uint8_t a = (uint8_t)(42 - 42 * j / h);
      if(a > 0) hLineA(x + ins, y + j, w - 2 * ins, TH_TXT, a);
    }
  }
  drawRoundRect(x, y, w, h, NOTIF_RAD, TH_BORDER);
  // Icono 40x40 (las primitivas acotan coords negativas: seguro fuera de pantalla)
  drawModuleIcon(n->mod.type, x + 12, y + (h - 40) / 2, 40);
  // Textos. RECORTADOS al borde derecho de la tarjeta: algunos nombres de
  // modulo son largos y sin esto se saldrian por encima del boton de cerrar.
  drawTextClip(x + 62, y + 14, n->mod.name, 2, TH_TXT,  x + w - 32);
  drawTextClip(x + 62, y + 38, n->mod.sub,  1, TH_TXT2, x + w - 32);
  // Boton cerrar (X)
  int cx = x + w - 22, cy = y + 20;
  strokeSegAA(cx - 5, cy - 5, cx + 5, cy + 5, 1.8f, TH_TXT);
  strokeSegAA(cx - 5, cy + 5, cx + 5, cy - 5, 1.8f, TH_TXT);
}

// ---- Toque de la isla: intercepta SOLO dentro de las tarjetas ----
// Se llama en loop() justo despues de flexPollTouch() y antes del switch de
// estado. Consume unicamente los flags de evento que usa (tap/pressed/released/
// swipeLeft); NUNCA toca T.down (lo gestiona flexPollTouch) para no corromper la
// maquina de estados del tactil.
static void notifHandleTouch(){
  // La isla solo recibe toques cuando es visible (Home principal desbloqueado).
  if(gState != ST_HOME || qsPanelY != 0 || editMode || notifSecureScreen() ||
     hpDragging || hpSettling){ notifDragIdx = -1; return; }
  // Toques en tarjetas (cerrar, flick, iniciar arrastre). SOLO las
  // visibles: las de la cola no tienen pixeles en pantalla, asi que un
  // deslizamiento sobre la tarjeta de arriba no puede descartarlas de
  // paso -- descarta la que se ve, y solo esa.
  int shown = gNotifCount < NOTIF_VISIBLE ? gNotifCount : NOTIF_VISIBLE;
  for(int i = 0; i < shown; i++){
    if(!gNotifs[i].active || !gNotifs[i].armed || gNotifs[i].phase == NP_OUT) continue;
    int cardY = NOTIF_Y0 + i * (NOTIF_CARD_H + NOTIF_GAP);
    int x0 = NOTIF_MARGIN_X, x1 = NOTIF_MARGIN_X + NOTIF_CARD_W;
    int y0 = cardY, y1 = cardY + NOTIF_CARD_H;
    // Boton cerrar (X) arriba-derecha
    int cx = NOTIF_MARGIN_X + NOTIF_CARD_W - 22, cy = cardY + 20;
    if(T.tap && abs(T.x - cx) < 16 && abs(T.y - cy) < 16){
      gNotifs[i].phase = NP_OUT; T.tap = false; T.pressed = false; return;
    }
    // Flick rapido a la izquierda sobre la tarjeta
    if(T.swipeLeft && T.startY >= y0 && T.startY <= y1){
      gNotifs[i].phase = NP_OUT; T.swipeLeft = false; T.tap = false; return;
    }
    // Iniciar arrastre (dedo dentro de la tarjeta)
    if(T.pressed && T.x >= x0 && T.x <= x1 && T.y >= y0 && T.y <= y1){
      notifDragIdx = i; T.pressed = false;
    }
  }
  // Arrastre en curso
  if(notifDragIdx >= 0 && notifDragIdx < gNotifCount){
    Notification* n = &gNotifs[notifDragIdx];
    if(T.down){
      float dx = (float)(T.x - T.startX);
      if(dx > 0) dx = 0;                                    // solo hacia la izquierda
      if(dx < -(NOTIF_CARD_W + 40)) dx = -(NOTIF_CARD_W + 40);
      n->slideX = dx; n->phase = NP_DRAG;
      T.tap = false; T.swipeLeft = false;                  // no propagar a la pantalla
    } else {
      // Soltar: descartar si paso el umbral, si no volver a su sitio
      if(n->slideX < -NOTIF_CARD_W / 4) n->phase = NP_OUT;
      else                              n->phase = NP_SPRING;
      notifDragIdx = -1;
      T.tap = false; T.released = false;
    }
  }
}

// ---- Tick de la isla: anima y compone (se llama al final de loop) ----
static void notifTick(){
  // Throttle ~30 fps
  if(millis() - notifLastMs < 33) return;
  notifLastMs = millis();

  // Nada que mostrar y banda ya limpia -> salida barata
  if(gNotifCount == 0 && !notifBandOn) return;

  // La isla SOLO vive en el Home principal desbloqueado (sin cortina ni edicion).
  // Fuera de ahi no avanzamos fases ni dibujamos: las notificaciones detectadas
  // durante el bloqueo esperan congeladas y su animacion de entrada + los 5 s
  // arrancan al llegar aqui. Asi tambien evitamos el conflicto de dibujo con
  // otras pantallas (que son quienes deben poseer el fb en ese momento).
  //
  // Y MIENTRAS SE PASA DE PAGINA, tampoco. hpRenderFrame() esta escribiendo
  // en bbuf las mismas filas que la isla y presentandolas desplazadas; si la
  // isla repusiera ahi el escritorio SIN desplazar, media pantalla quedaria
  // en la pagina vieja y media en la nueva. Se cede el turno: la isla vuelve
  // -- con su tiempo intacto, via notifPaused -- en cuanto el gesto acaba.
  if(gState != ST_HOME || qsPanelY != 0 || editMode || notifSecureScreen() ||
     hpDragging || hpSettling){
    if(!notifPaused){ notifPaused = true; notifPauseT0 = millis(); }   // marca el inicio de la pausa (p.ej. se abrio una app)
    return;
  }
  if(notifPaused){
    // Reanudando tras una pausa (p.ej. se cerro la app que se abrio encima):
    // sumar el tiempo pausado a bornMs de cada tarjeta activa para que
    // conserven el tiempo que les quedaba, en vez de que millis()-bornMs se
    // dispare de golpe y todas pasen de fase (y se reindexen) en el mismo
    // frame -- eso era el parpadeo/"se queda bugeado" al volver de una app.
    uint32_t paused = millis() - notifPauseT0;
    for(int i = 0; i < gNotifCount; i++) gNotifs[i].bornMs += paused;
    notifPaused = false;
  }
  if(gNotifCount > 0) notifBandOn = true;

  // UNA a la vez: solo las NOTIF_VISIBLE primeras de la cola se arman,
  // se animan y se dibujan. Las de detras esperan su turno intactas --
  // ni cuentan los 5 s ni ocupan pixeles -- y entran en cuanto la de
  // delante se va. Eso es lo que convierte gNotifs[] en una cola de
  // verdad en vez de una pila de tarjetas superpuestas.
  int shown = gNotifCount < NOTIF_VISIBLE ? gNotifCount : NOTIF_VISIBLE;

  // Armar la entrada de las notificaciones aun no mostradas
  for(int i = 0; i < shown; i++){
    if(!gNotifs[i].armed){
      gNotifs[i].armed  = true;
      gNotifs[i].phase  = NP_IN;
      gNotifs[i].bornMs = millis();
      gNotifs[i].slideX = 0.0f;
    }
  }

  // Avanzar fases de animacion
  for(int i = 0; i < shown; ){
    Notification* n = &gNotifs[i];
    // Al salir la de delante, la siguiente sube a esta ranura en el MISMO
    // frame, todavia sin armar. No se le avanza la fase aqui: su bornMs es
    // el de cuando se encolo, asi que la caducidad de 5 s la mataria de
    // golpe sin haberse visto nunca. Se arma en la vuelta siguiente.
    if(!n->armed){ i++; continue; }
    bool removed = false;
    switch(n->phase){
      case NP_IN:
        if(millis() - n->bornMs >= 280) n->phase = NP_IDLE;
        break;
      case NP_IDLE:
        if(millis() - n->bornMs >= NOTIF_HOLD_MS) n->phase = NP_OUT;   // auto-descarte a los 5 s
        break;
      case NP_SPRING:
        n->slideX += (0.0f - n->slideX) * 0.35f;           // muelle de vuelta
        if(n->slideX > -0.5f){ n->slideX = 0.0f; n->phase = NP_IDLE; }
        break;
      case NP_OUT:
        n->slideX -= (NOTIF_CARD_W + NOTIF_MARGIN_X) * 0.18f + 6.0f;  // sale por la izquierda
        if(n->slideX < -(NOTIF_CARD_W + NOTIF_MARGIN_X + 4)){
          notifRemove(i);
          // `shown` era una instantanea tomada antes de compactar. Con la
          // ultima tarjeta quedaba en 1 aunque gNotifCount ya fuera 0; el
          // antiguo i-- volvia a procesar para siempre la ranura eliminada y
          // el TASK_WDT reiniciaba todo el OS. Recalcular el limite y mantener
          // i hace que la tarjeta siguiente (si existe) ocupe la ranura de
          // forma segura, sin saltarla ni leer una entrada muerta.
          shown = gNotifCount < NOTIF_VISIBLE ? gNotifCount : NOTIF_VISIBLE;
          removed = true;
        }
        break;
      default: break;
    }
    if(!removed) i++;
  }

  // Recorte completo (por si una app lo dejo estrecho) antes de componer
  gClipY0 = 0; gClipY1 = SCR_H - 1; gClipX0 = 0; gClipX1 = SCR_W - 1;

  // Componer en bbuf (nadie mas lo lee): restaurar fondo limpio y dibujar las
  // tarjetas encima. Nadie mas presenta esta banda -> sin parpadeo.
  setBuf(bbuf);
  notifRestoreBg();
  if(gNotifCount < shown) shown = gNotifCount;         // alguna se fue en el bucle de fases
  for(int i = 0; i < shown; i++){
    if(!gNotifs[i].active || !gNotifs[i].armed) continue;
    int cardY = NOTIF_Y0 + i * (NOTIF_CARD_H + NOTIF_GAP);
    if(gNotifs[i].phase == NP_IN){
      float p = (millis() - gNotifs[i].bornMs) / 280.0f; if(p > 1.0f) p = 1.0f;
      cardY -= (int)((1.0f - notifEaseOut(p)) * NOTIF_ENTER_DROP);   // entrada: cae desde arriba
    }
    notifDrawCard(&gNotifs[i], cardY);
  }
  // Volcado atomico bbuf->fb de una banda ya terminada. DMA2D nunca ve
  // un fb a medio pintar.
  present(NOTIF_BAND_TOP, NOTIF_BAND_BOT - 1);

  // Banda vaciada: el frame de limpieza ya se compuso y volco arriba.
  if(gNotifCount == 0) notifBandOn = false;
}
