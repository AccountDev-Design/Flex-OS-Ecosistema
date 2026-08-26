// #############################################################
// ##  FlexOS_FlexPhone_Bridge.h  ·  PUENTE FLEX PHONE <-> FLEXOS
// ##  ------------------------------------------------------
// ##  Mismo papel y mismo motivo que FlexOS_Browser_Bridge.h y
// ##  FlexOS_Store_Bridge.h: las primitivas graficas de FlexOS
// ##  (fillRect, drawText, present...) son `static` DENTRO del
// ##  .ino, asi que los modulos que son unidad de traduccion
// ##  aparte (FlexOS_FlexPhone.cpp, FlexOS_FlexPhone_Link.cpp) no
// ##  pueden llamarlas. Este fichero conoce los dos mundos.
// ##
// ##  Se incluye UNA sola vez, junto a los demas puentes, casi al
// ##  final del .ino: para entonces el tema, el teclado y todas
// ##  las primitivas ya estan definidos.
// ##
// ##  QUE VIVE AQUI
// ##    · la instancia unica del modelo y del enlace
// ##    · las cinco secciones de la app (Centro, Notificaciones,
// ##      Mensajes, Navegador, Dispositivo)
// ##    · el arranque/parada del anunciado BLE REAL, si la placa
// ##      puede (ver flexPhoneLinkCap)
// ##    · la persistencia agrupada contra FlexOS_FS
// ##
// ##  QUE **NO** VIVE AQUI
// ##    · logica de protocolo: esta en FlexOS_FlexLink.cpp
// ##    · reglas del modelo: en FlexOS_FlexPhone.cpp
// ##    Los dos se prueban en el PC; este fichero es solo pintura
// ##    y cableado.
// ##
// ##  COMO SE USA (3 lineas en el .ino):
// ##      arriba, con los includes :  #include "FlexOS_FlexPhone.h"
// ##      junto a los demas puentes:  #include "FlexOS_FlexPhone_Bridge.h"
// ##      en APP_REG, entrada 18   :  { fphEnter, fphTick, ... }
// #############################################################
#ifndef FLEXOS_FLEXPHONE_BRIDGE_H
#define FLEXOS_FLEXPHONE_BRIDGE_H

#include "FlexOS_FlexPhone.h"
#include "FlexOS_FlexPhone_Link.h"
#include "FlexOS_FS.h"
#include "FlexOS_Browser.h"   // fuente del navegador (flexBrSource*)

// =============================================================
//  ESTADO UNICO
// =============================================================
// Dos estructuras grandes (~20 KB juntas) en BSS. Es a proposito:
// tamano conocido en compilacion, sin reservas dinamicas y sin
// fragmentar el heap que necesitan el navegador y la galeria.
static FlexPhoneModel fphModel;
static FlexPhoneLink  fphLink;

// Secciones de la app. El indice se PERSISTE: al volver a entrar se
// abre donde estabas.
enum { FPH_CENTRO = 0, FPH_NOTIFS, FPH_MENSAJES, FPH_NAVEGADOR, FPH_DISPOSITIVO, FPH_SEC_N };
static uint8_t fphSection   = FPH_CENTRO;
static int     fphScroll    = 0;
static int     fphSelConv   = -1;      // conversacion abierta (-1 = lista)
static bool    fphDirtyUi   = true;    // hay que repintar
static uint32_t fphLastSave = 0;
static uint32_t fphLastDraw = 0;
static char    fphToast[72] = {0};
static uint32_t fphToastUntil = 0;

// Ruta del volcado. Un solo fichero, escritura AGRUPADA.
#define FPH_STORE_PATH "/flexphone/state.bin"
#define FPH_SAVE_EVERY_MS 30000u       // no se escribe en flash por cada evento

static const char* FPH_SEC_NAME[FPH_SEC_N][2] = {
  { "Centro",       "Center" },
  { "Notificaciones","Notifications" },
  { "Mensajes",     "Messages" },
  { "Navegador",    "Browser" },
  { "Dispositivo",  "Device" },
};
static inline const char* fphSecName(int s){
  if(s < 0 || s >= FPH_SEC_N) s = 0;
  return FPH_SEC_NAME[s][LI() == 1 ? 1 : 0];
}

static void fphToastShow(const char* msg){
  flexLinkUtf8Copy(fphToast, sizeof(fphToast), msg ? msg : "");
  fphToastUntil = millis() + 2600;
  fphDirtyUi = true;
}

// =============================================================
//  PERSISTENCIA (agrupada, nunca en el bucle de dibujo)
// =============================================================
static void fphSave(bool force){
  if(!fphModel.dirty && !force) return;
  const uint32_t now = millis();
  if(!force && (uint32_t)(now - fphLastSave) < FPH_SAVE_EVERY_MS) return;
  if(!flexFsReady()) return;
  // El buffer sale de PSRAM y se libera enseguida: 12 KB en la pila
  // del bucle principal no son aceptables.
  uint8_t* buf = (uint8_t*)heap_caps_malloc(FLP_BLOB_CAP, MALLOC_CAP_SPIRAM);
  if(!buf) buf = (uint8_t*)malloc(FLP_BLOB_CAP);
  if(!buf) return;                       // sin memoria: se reintenta luego
  const size_t n = flexPhoneSerialize(&fphModel, buf, FLP_BLOB_CAP);
  if(n) flexFsWriteBinAtomic(FPH_STORE_PATH, buf, n);
  free(buf);
  fphModel.dirty = false;
  fphLastSave = now;
}

static void fphLoad(){
  if(!flexFsReady()) return;
  const uint32_t sz = flexFsSize(FPH_STORE_PATH);
  if(sz == 0 || sz > FLP_BLOB_CAP) return;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if(!buf) buf = (uint8_t*)malloc(sz);
  if(!buf) return;
  const int got = flexFsReadBin(FPH_STORE_PATH, buf, sz);
  if(got > 0) flexPhoneDeserialize(&fphModel, buf, (size_t)got);
  free(buf);
}

// =============================================================
//  ARRANQUE / TICK DEL SISTEMA  (fuera de la app)
// =============================================================
// El enlace vive aunque la app este cerrada: las notificaciones
// tienen que seguir llegando. Lo que NO ocurre con la app cerrada
// es dibujar nada.
static void flexPhoneBegin(){
  flexPhoneModelInit(&fphModel);
  flexPhoneLinkInit(&fphLink);
  fphLoad();
  // NO se enciende la radio en el arranque. Misma regla que el Wi-Fi:
  // el enlace se activa desde la app, para que un fallo del C6 se vea
  // dentro de una pantalla y no como un cuelgue del arranque.
}

// Un paso por frame desde loop(). Con el enlace apagado o no
// disponible sale en la primera linea: Flex Phone inactivo no cuesta
// FPS al escritorio, al panel rapido ni a los juegos.
static void flexPhoneTick(){
  if(fphLink.state == FLP_LS_UNAVAILABLE || fphLink.state == FLP_LS_OFF) return;
  const uint32_t now = millis();
  flexPhoneLinkTick(&fphLink, &fphModel, now);
  fphSave(false);                        // agrupada: casi siempre no hace nada
}

// =============================================================
//  PINTURA
// =============================================================
static uint16_t fphCard(){ return uiGlass ? TH_GLASS : TH_SURF; }

// Cabecera con el mismo lenguaje visual que Flex Store.
static void fphHeader(const char* title, bool nested){
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  if(nested){
    strokeSeg(38, 30, 26, 42, 3, TH_TXT);
    strokeSeg(26, 42, 38, 54, 3, TH_TXT);
  }
  drawText(nested ? 66 : 24, 28, title, 3, TH_TXT);
  // Punto de estado: verde SOLO si hay sesion de verdad.
  const bool ready = flexPhoneLinkReady(&fphLink);
  fillCircle(SCR_W - 34, 42, 7, ready ? TH_OK : TH_DIS);
  hLine(20, 76, SCR_W - 40, TH_DIV);
}

// Pestanas de seccion.
static void fphTabs(){
  const int y = 84, h = 46;
  const int w = SCR_W / FPH_SEC_N;
  for(int i = 0; i < FPH_SEC_N; i++){
    const bool on = (i == fphSection);
    if(on) fillRoundRect(i * w + 4, y, w - 8, h, 12, TH_SEL);
    drawTextC(i * w + w / 2, y + 16, fphSecName(i), 1, on ? TH_TXT : TH_MUTE);
  }
  hLine(20, y + h + 4, SCR_W - 40, TH_DIV);
}

// Aviso grande y honesto cuando el hardware no puede.
static void fphUnavailableCard(int y){
  fillRoundRect(20, y, SCR_W - 40, 150, 16, fphCard());
  drawText(36, y + 14, "BLE no disponible", 2, TH_WARN);
  // El motivo se parte en lineas de ancho fijo, sin cortar palabras.
  const char* s = flexPhoneLinkCapReason();
  int ty = y + 42;
  char line[46];
  size_t at = 0;
  const size_t len = strlen(s);
  while(at < len && ty < y + 138){
    size_t take = sizeof(line) - 1;
    if(at + take > len) take = len - at;
    else {                                   // retrocede al ultimo espacio
      size_t k = take;
      while(k > 0 && s[at + k] != ' ') k--;
      if(k > 8) take = k;
    }
    memcpy(line, s + at, take); line[take] = 0;
    drawText(36, ty, line, 1, TH_TXT2);
    ty += 16;
    at += take;
    while(at < len && s[at] == ' ') at++;
  }
}

// ---- 1) CENTRO ----
static void fphRenderCentro(){
  fphHeader("Flex Phone", false);
  fphTabs();
  int y = 146;
  if(fphLink.cap == FLP_LINK_CAP_NONE){ fphUnavailableCard(y); return; }

  const bool ready = flexPhoneLinkReady(&fphLink);
  // -- Tarjeta del telefono --
  fillRoundRect(20, y, SCR_W - 40, 96, 16, fphCard());
  drawText(36, y + 12, ready && fphModel.phone.name[0] ? fphModel.phone.name : "Sin telefono", 2, TH_TXT);
  drawText(36, y + 40, flexPhoneLinkStateName(fphLink.state), 1, ready ? TH_OK : TH_MUTE);
  if(ready && fphModel.phone.valid){
    char b[40];
    if(fphModel.phone.battery == 255) snprintf(b, sizeof(b), "Bateria: desconocida");
    else snprintf(b, sizeof(b), "Bateria %u%%%s", (unsigned)fphModel.phone.battery,
                  fphModel.phone.charging ? " · cargando" : "");
    drawText(36, y + 62, b, 1, TH_TXT2);
  } else {
    // Sin datos REALES no se inventa un porcentaje.
    drawText(36, y + 62, "Sin datos del telefono", 1, TH_MUTE);
  }
  y += 108;

  // -- Multimedia: solo si hay sesion valida --
  fillRoundRect(20, y, SCR_W - 40, 92, 16, fphCard());
  if(ready && fphModel.media.valid){
    drawText(36, y + 10, fphModel.media.title, 2, TH_TXT);
    if(fphModel.media.artist[0]) drawText(36, y + 34, fphModel.media.artist, 1, TH_TXT2);
    const int by = y + 58, bw = 56;
    for(int i = 0; i < 4; i++){
      fillRoundRect(36 + i * (bw + 8), by, bw, 26, 10, TH_SURF2);
      static const char* lbl[4] = { "<<", "P", "||", ">>" };
      drawTextC(36 + i * (bw + 8) + bw / 2, by + 6, lbl[i], 1, TH_TXT);
    }
  } else {
    drawText(36, y + 10, "Multimedia", 2, TH_MUTE);
    drawText(36, y + 38, ready ? "Nada reproduciendose" : "Requiere telefono conectado", 1, TH_MUTE);
  }
  y += 104;

  // -- Encontrar telefono / emparejar --
  if(fphLink.state == FLP_LS_PAIRING && fphLink.code[0]){
    fillRoundRect(20, y, SCR_W - 40, 84, 16, TH_ACCS);
    drawText(36, y + 10, "Codigo de emparejamiento", 1, TH_TXT2);
    drawText(36, y + 30, fphLink.code, 4, TH_TXT);
    drawTextR(SCR_W - 36, y + 54, "Confirmar", 1, TH_PRIM);
  } else {
    fillRoundRect(20, y, SCR_W - 40, 52, 16, fphCard());
    drawText(36, y + 16, ready ? "Encontrar mi telefono" : "Emparejar telefono", 2,
             ready ? TH_TXT : TH_PRIM);
  }
}

// ---- 2) NOTIFICACIONES ----
static void fphRenderNotifs(){
  fphHeader("Flex Phone", false);
  fphTabs();
  int y = 146 - fphScroll;
  int shown = 0;
  for(int i = 0; i < FLP_NOTIF_MAX; i++){
    const FlexPhoneNotif* n = &fphModel.notif[i];
    if(!n->used) continue;
    shown++;
    if(y > SCR_H - 70){ continue; }
    if(y < 130){ y += 78; continue; }
    fillRoundRect(20, y, SCR_W - 40, 70, 14, fphCard());
    // Icono: de la TABLA LOCAL, nunca una imagen venida por BLE.
    const uint8_t ico = flexPhoneIconFor(n->pkg, n->cat);
    fillRoundRect(30, y + 12, 40, 40, 12, TH_SURF2);
    drawTextC(50, y + 26, flexPhoneIconName(ico), 1, TH_TXT2);
    drawText(80, y + 10, n->app[0] ? n->app : n->pkg, 1, TH_MUTE);
    drawText(80, y + 26, n->title, 2, TH_TXT);
    char body[FLP_TEXT_MAX];
    if(flexPhoneNotifBody(&fphModel, n, false, body, sizeof(body)))
      drawText(80, y + 50, body, 1, TH_TXT2);
    else
      drawText(80, y + 50, n->sensitive ? "Contenido protegido" : "Contenido oculto", 1, TH_DIS);
    // Solo se marca "responder" si Android dio RemoteInput DE VERDAD.
    if(n->canReply) drawTextR(SCR_W - 34, y + 50, "Responder", 1, TH_PRIM);
    y += 78;
  }
  if(shown == 0){
    drawTextC(SCR_W / 2, 260, flexPhoneLinkReady(&fphLink) ? "Sin notificaciones"
                                                           : "Requiere telefono conectado",
              2, TH_MUTE);
    if(!flexPhoneLinkReady(&fphLink))
      drawTextC(SCR_W / 2, 292, "El historial guardado aparecera aqui", 1, TH_DIS);
  }
}

// ---- 3) MENSAJES ----
static void fphRenderMensajes(){
  fphHeader("Flex Phone", false);
  fphTabs();
  int y = 146;
  int shown = 0;
  for(int i = 0; i < FLP_CONV_MAX; i++){
    const FlexPhoneConv* c = &fphModel.conv[i];
    if(!c->used) continue;
    shown++;
    if(y > SCR_H - 80) break;
    fillRoundRect(20, y, SCR_W - 40, 64, 14, fphCard());
    drawText(36, y + 10, c->who, 2, TH_TXT);
    drawText(36, y + 34, c->pkg, 1, TH_MUTE);
    // El estado REAL de si se puede contestar ahora mismo.
    if(c->canReply && flexPhoneLinkReady(&fphLink))
      drawTextR(SCR_W - 36, y + 34, "Responder", 1, TH_PRIM);
    else
      drawTextR(SCR_W - 36, y + 34, "No disponible", 1, TH_DIS);
    y += 72;
  }
  // Borradores locales: SI funcionan sin telefono.
  for(int i = 0; i < FLP_DRAFT_MAX && y < SCR_H - 80; i++){
    const FlexPhoneDraft* d = &fphModel.draft[i];
    if(!d->used) continue;
    shown++;
    fillRoundRect(20, y, SCR_W - 40, 56, 14, TH_SURF2);
    drawText(36, y + 8,  d->who, 1, TH_TXT2);
    drawText(36, y + 26, d->text, 1, TH_TXT);
    drawTextR(SCR_W - 36, y + 8, "Borrador", 1, TH_WARN);
    y += 64;
  }
  if(shown == 0){
    drawTextC(SCR_W / 2, 250, "Sin conversaciones", 2, TH_MUTE);
    // Se dice el limite REAL, sin disimularlo.
    drawTextC(SCR_W / 2, 282, "Solo aparecen las apps cuya notificacion", 1, TH_DIS);
    drawTextC(SCR_W / 2, 300, "permite responder desde Android", 1, TH_DIS);
  }
}

// ---- 4) NAVEGADOR ----
static void fphRenderNavegador(){
  fphHeader("Flex Phone", false);
  fphTabs();
  int y = 146;
  fillRoundRect(20, y, SCR_W - 40, 110, 16, fphCard());
  drawText(36, y + 12, "Browser Relay", 2, TH_TXT);
  const FlexPhoneRelay* r = &fphModel.relay;
  switch(r->state){
    case FLP_RELAY_UP: {
      char ln[64];
      snprintf(ln, sizeof(ln), "Activo · %u.%u.%u.%u:%u",
               r->ip[0], r->ip[1], r->ip[2], r->ip[3], (unsigned)r->port);
      drawText(36, y + 40, ln, 1, TH_OK);
      drawText(36, y + 60, r->tls ? "Cifrado TLS" : "Sin TLS (red local)", 1, TH_TXT2);
      drawTextR(SCR_W - 36, y + 84, "Detener", 1, TH_DANGER);
    } break;
    case FLP_RELAY_STARTING:
      drawText(36, y + 40, "Iniciando en el telefono...", 1, TH_TXT2);
      break;
    case FLP_RELAY_ERROR:
      drawText(36, y + 40, "Error", 1, TH_ERR);
      drawText(36, y + 60, r->err, 1, TH_TXT2);
      break;
    case FLP_RELAY_SUSPENDED:
      // Android puede matar el proceso. Se dice, no se disimula.
      drawText(36, y + 40, "Android suspendio el relay", 1, TH_WARN);
      drawText(36, y + 60, "Excluye Flex Phone del ahorro de bateria", 1, TH_TXT2);
      break;
    default:
      drawText(36, y + 40, flexPhoneLinkReady(&fphLink) ? "Detenido" : "Requiere telefono conectado",
               1, TH_MUTE);
      if(flexPhoneLinkReady(&fphLink)) drawTextR(SCR_W - 36, y + 84, "Iniciar", 1, TH_PRIM);
      break;
  }
  y += 122;
  // Fuente actual del navegador. NO se adivina: se resuelve con las
  // disponibilidades REALES (relay arriba, nube configurada, servidor
  // manual configurado) usando la misma funcion que usa el navegador.
  fillRoundRect(20, y, SCR_W - 40, 76, 16, fphCard());
  drawText(36, y + 12, "Fuente del navegador", 1, TH_TXT2);
  const BrSettings* bs = flexBrowserSettings();
  const bool phoneUp   = (fphModel.relay.state == FLP_RELAY_UP);
  const bool cloudCfg  = bs && bs->cloudServer[0] != 0;
  const bool manualCfg = bs && bs->server[0] != 0;
  const int  useSrc    = flexBrSourceResolve(bs, phoneUp, cloudCfg, manualCfg);
  drawText(36, y + 34, flexBrSourceName(useSrc), 2, useSrc == BRSRC_NONE ? TH_MUTE : TH_TXT);
  if(useSrc == BRSRC_NONE)
    drawText(36, y + 58, flexBrSourceWhyNone(bs, phoneUp, cloudCfg, manualCfg), 1, TH_DIS);
  else
    drawText(36, y + 58, "Se cambia en Navegador > Ajustes", 1, TH_DIS);
}

// ---- 5) DISPOSITIVO ----
static void fphRenderDispositivo(){
  fphHeader("Flex Phone", false);
  fphTabs();
  int y = 146;
  fillRoundRect(20, y, SCR_W - 40, 96, 16, fphCard());
  drawText(36, y + 10, "Conexion", 1, TH_TXT2);
  drawText(36, y + 30, flexPhoneLinkStateName(fphLink.state), 2, TH_TXT);
  char ln[64];
  snprintf(ln, sizeof(ln), "MTU %u · protocolo v%u", (unsigned)fphLink.mtu, FLNK_VERSION);
  drawText(36, y + 60, ln, 1, TH_MUTE);
  y += 108;

  // Privacidad
  fillRoundRect(20, y, SCR_W - 40, 110, 16, fphCard());
  drawText(36, y + 10, "Privacidad", 1, TH_TXT2);
  drawText(36, y + 30, fphModel.priv.hideBodyOnLock ? "Ocultar cuerpo en bloqueo: SI"
                                                    : "Ocultar cuerpo en bloqueo: NO", 1, TH_TXT);
  drawText(36, y + 52, fphModel.priv.hideSensitive ? "Ocultar sensibles (OTP): SI"
                                                   : "Ocultar sensibles (OTP): NO", 1, TH_TXT);
  snprintf(ln, sizeof(ln), "Borrado automatico: %u h", (unsigned)fphModel.priv.keepHours);
  drawText(36, y + 74, fphModel.priv.keepHours ? ln : "Borrado automatico: apagado", 1, TH_TXT);
  y += 122;

  // Diagnostico: SOLO contadores. Nunca contenido de mensajes.
  fillRoundRect(20, y, SCR_W - 40, 96, 16, TH_SURF2);
  drawText(36, y + 8, "Diagnostico", 1, TH_TXT2);
  snprintf(ln, sizeof(ln), "rx %u · malos %u · descartes %u",
           (unsigned)fphLink.nRx, (unsigned)fphLink.nBad, (unsigned)fphLink.nDropped);
  drawText(36, y + 28, ln, 1, TH_MUTE);
  snprintf(ln, sizeof(ln), "cola llena %u · expulsadas %u · reconex. %u",
           (unsigned)fphModel.stats.queueFull, (unsigned)fphModel.stats.evicted,
           (unsigned)fphModel.stats.reconnects);
  drawText(36, y + 46, ln, 1, TH_MUTE);
  snprintf(ln, sizeof(ln), "respuestas ok %u · fallidas %u",
           (unsigned)fphModel.stats.repliesOk, (unsigned)fphModel.stats.repliesFail);
  drawText(36, y + 64, ln, 1, TH_MUTE);
  y += 108;

  if(fphLink.bonded) drawTextC(SCR_W / 2, y + 6, "Desvincular telefono", 2, TH_DANGER);
}

static void fphRender(){
  // Misma convencion que Flex Store: se dibuja sobre `fb` y se vuelca
  // con flxFlushAll(). present() es para las bandas parciales del
  // sistema; una pantalla de app se vuelca entera.
  setBuf(fb);
  switch(fphSection){
    case FPH_NOTIFS:      fphRenderNotifs();      break;
    case FPH_MENSAJES:    fphRenderMensajes();    break;
    case FPH_NAVEGADOR:   fphRenderNavegador();   break;
    case FPH_DISPOSITIVO: fphRenderDispositivo(); break;
    default:              fphRenderCentro();      break;
  }
  if(fphToastUntil && (int32_t)(millis() - fphToastUntil) < 0){
    fillRoundRect(24, SCR_H - 118, SCR_W - 48, 44, 12, TH_SCRIM);
    drawTextC(SCR_W / 2, SCR_H - 104, fphToast, 1, TH_TXT);
  }
  flxFlushAll();
  fphDirtyUi = false;
  fphLastDraw = millis();
}

// =============================================================
//  CICLO DE VIDA DE LA APP
// =============================================================
static void fphEnter(){
  // gRelayout: si solo se esta re-maquetando por un cambio de tamano,
  // NO se reinicia la seccion abierta ni el scroll.
  if(!gRelayout){
    fphSelConv = -1;
    // fphSection se CONSERVA a proposito: volver a la app te devuelve
    // a la seccion donde estabas.
  }
  fphDirtyUi = true;
  fphRender();
}

static void fphExit(){
  fphSave(true);          // al salir SI se vuelca, aunque no toque por tiempo
}

// Atras dentro de la app: primero cierra la conversacion, luego vuelve
// a Centro, y solo entonces cierra la app.
static bool fphBackScreen(){
  if(fphSelConv >= 0){ fphSelConv = -1; fphDirtyUi = true; return true; }
  if(fphSection != FPH_CENTRO){ fphSection = FPH_CENTRO; fphScroll = 0; fphDirtyUi = true; return true; }
  return false;
}

static void fphSuspend(){ fphSave(true); }
static void fphResume(){  fphDirtyUi = true; }

// Toques de la app.
static void fphTouch(){
  if(!T.tap && !T.swipeUp && !T.swipeDown) return;

  // Pestanas
  if(T.tap && T.y >= 84 && T.y <= 130){
    const int w = SCR_W / FPH_SEC_N;
    int s = T.x / (w ? w : 1);
    if(s < 0) s = 0;
    if(s >= FPH_SEC_N) s = FPH_SEC_N - 1;
    if(s != fphSection){ fphSection = (uint8_t)s; fphScroll = 0; fphDirtyUi = true; }
    return;
  }
  // Atras de la cabecera
  if(T.tap && T.x < 76 && T.y < 76){
    if(!fphBackScreen()) appClose();
    return;
  }
  // Scroll de la lista de notificaciones
  if(fphSection == FPH_NOTIFS){
    if(T.swipeUp){   fphScroll += 78; fphDirtyUi = true; return; }
    if(T.swipeDown){ fphScroll -= 78; if(fphScroll < 0) fphScroll = 0; fphDirtyUi = true; return; }
  }

  if(fphSection == FPH_CENTRO && T.tap){
    // Confirmar emparejamiento
    if(fphLink.state == FLP_LS_PAIRING && fphLink.code[0] && T.y > 350){
      flexPhoneLinkConfirm(&fphLink, millis());
      fphToastShow(flexPhoneLinkPairComplete(&fphLink) ? "Telefono emparejado"
                                                       : "Confirma tambien en el telefono");
      return;
    }
    // Emparejar / encontrar
    if(T.y > 350){
      if(fphLink.cap == FLP_LINK_CAP_NONE){
        fphToastShow("Esta placa no puede usar BLE");
        return;
      }
      if(flexPhoneLinkReady(&fphLink)){
        if(flexPhoneLinkSend(&fphLink, FLNK_T_FIND_START, NULL, 0, true))
          fphToastShow("Sonando en el telefono");
        else
          fphToastShow("No se pudo enviar la orden");
      } else {
        if(flexPhoneLinkStart(&fphLink)){
          flexPhoneLinkBeginPairing(&fphLink, esp_random(), millis());
          fphToastShow("Abre Flex Phone en el telefono");
        } else {
          fphToastShow(fphLink.err);      // el motivo REAL, no un generico
        }
      }
      return;
    }
    // Botones multimedia: solo hacen algo si hay sesion Y sesion valida.
    if(fphModel.media.valid && flexPhoneLinkReady(&fphLink) && T.y >= 312 && T.y <= 344){
      static const uint8_t cmds[4] = { FLP_MCMD_PREV, FLP_MCMD_PLAY, FLP_MCMD_PAUSE, FLP_MCMD_NEXT };
      const int idx = (T.x - 36) / 64;
      if(idx >= 0 && idx < 4){
        uint8_t body[1];
        const int n = flexPhoneEncMediaCmd(body, sizeof(body), cmds[idx]);
        if(n > 0 && flexPhoneLinkSend(&fphLink, FLNK_T_MEDIA_CMD, body, (size_t)n, false))
          fphToastShow("Orden enviada");
        else
          fphToastShow("No se pudo enviar");
      }
      return;
    }
  }

  if(fphSection == FPH_DISPOSITIVO && T.tap && T.y > SCR_H - 120 && fphLink.bonded){
    flexPhoneLinkForget(&fphLink);
    flexPhoneModelClear(&fphModel, true);   // desvincular BORRA los datos
    fphSave(true);
    fphToastShow("Telefono desvinculado y datos borrados");
    return;
  }

  if(fphSection == FPH_NAVEGADOR && T.tap && T.y >= 220 && T.y <= 250){
    if(!flexPhoneLinkReady(&fphLink)){ fphToastShow("Requiere telefono conectado"); return; }
    if(fphModel.relay.state == FLP_RELAY_UP){
      flexPhoneLinkSend(&fphLink, FLNK_T_RELAY_STOP, NULL, 0, true);
      fphModel.relay.state = FLP_RELAY_OFF;
      fphToastShow("Deteniendo el relay");
    } else {
      if(flexPhoneLinkSend(&fphLink, FLNK_T_RELAY_START, NULL, 0, true)){
        fphModel.relay.state = FLP_RELAY_STARTING;
        fphToastShow("Pidiendo el relay al telefono");
      } else fphToastShow("No se pudo pedir el relay");
    }
    fphDirtyUi = true;
    return;
  }
}

// Tick de la app: toques + repintado SOLO cuando algo cambio.
static void fphTick(){
  fphTouch();
  // Repintado por cambio real, no por frame: con la app abierta y sin
  // novedades no se redibuja nada.
  static uint32_t lastSync = 0, lastState = 0;
  const uint32_t syncNow = fphModel.lastSyncMs;
  const uint32_t stNow = (uint32_t)fphLink.state | ((uint32_t)fphModel.notifCount << 8)
                       | ((uint32_t)fphModel.relay.state << 20);
  if(syncNow != lastSync || stNow != lastState){
    lastSync = syncNow; lastState = stNow;
    fphDirtyUi = true;
  }
  if(fphToastUntil && (int32_t)(millis() - fphToastUntil) >= 0){
    fphToastUntil = 0; fphDirtyUi = true;
  }
  if(fphDirtyUi) fphRender();
}

#endif // FLEXOS_FLEXPHONE_BRIDGE_H
