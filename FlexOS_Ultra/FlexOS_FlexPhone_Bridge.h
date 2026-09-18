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
// ##    · el transporte Wi-Fi conectado al enlace
// ##    · las diez pantallas de la app
// ##    · la persistencia del vinculo (NVS) y del historial (LittleFS)
// ##
// ##  QUE **NO** VIVE AQUI
// ##    · logica de protocolo: en FlexOS_FlexLink.cpp
// ##    · emparejamiento y sesion: en FlexOS_FlexAuth.cpp
// ##    · maquina de estados del enlace: en FlexOS_FlexPhone_Link.cpp
// ##    · reglas del modelo: en FlexOS_FlexPhone.cpp
// ##    Los cuatro se prueban en el PC; este fichero es pintura,
// ##    cableado y almacenamiento.
// ##
// ##  ESTRUCTURA DE LA APP
// ##  ------------------------------------------------------
// ##  Una PORTADA con lo importante a la vista (que telefono, si
// ##  esta conectado, bateria, red y latencia) y nueve secciones
// ##  debajo. No son pestanas: nueve pestanas en 480 px de ancho
// ##  dejan cuatro letras por pestana y no se leen.
// #############################################################
#ifndef FLEXOS_FLEXPHONE_BRIDGE_H
#define FLEXOS_FLEXPHONE_BRIDGE_H

#include "FlexOS_FlexPhone.h"
#include "FlexOS_FlexPhone_Link.h"
#include "FlexOS_FlexPhone_UI.h"
#include "FlexOS_FlexPhone_WiFi.h"
#include "FlexOS_FS.h"
#include "FlexOS_Browser.h"   // fuente del navegador (flexBrSource*)

// =============================================================
//  ESTADO UNICO
// =============================================================
// Dos estructuras grandes en BSS: tamano conocido en COMPILACION,
// sin reservas dinamicas y sin fragmentar el heap que necesitan el
// navegador y la galeria.
static FlexPhoneModel fphModel;
static FlexPhoneLink  fphLink;

// -------------------------------------------------------------
//  Secciones
// -------------------------------------------------------------
enum {
  FPH_INICIO = 0,
  FPH_NOTIFS, FPH_SERVIDOR, FPH_NAVEGADOR, FPH_ESTADO,
  FPH_CONEXION, FPH_SYNC, FPH_DIAG, FPH_SEGURIDAD, FPH_AJUSTES,
  FPH_SEC_N
};
static const char* FPH_SEC_NAME[FPH_SEC_N][2] = {
  { "Flex Phone",      "Flex Phone" },
  { "Notificaciones",  "Notifications" },
  { "Servidor",        "Server" },
  { "Navegador",       "Browser" },
  { "Estado del telefono", "Phone status" },
  { "Conexion",        "Connection" },
  { "Sincronizacion",  "Sync" },
  { "Diagnostico",     "Diagnostics" },
  { "Seguridad",       "Security" },
  { "Ajustes",         "Settings" },
};
static inline const char* fphSecName(int s){
  if(s < 0 || s >= FPH_SEC_N) s = 0;
  return FPH_SEC_NAME[s][LI() == 1 ? 1 : 0];
}

static uint8_t   fphSection   = FPH_INICIO;
static FgScroll  fphScr       = { 0, 0, 96, SCR_H - 8 };
static bool      fphDirtyUi   = true;
static uint32_t  fphLastSave  = 0;
static char      fphToast[80] = {0};
static uint32_t  fphToastUntil = 0;
// Arrastre para desplazar: se sigue el dedo 1:1, igual que el resto
// del sistema. Un scroll "por gesto de pagina" se nota distinto a
// todo lo demas y delata que esta pantalla es de otro sitio.
static bool      fphDrag = false;
static int       fphDragY0 = 0, fphDragOff0 = 0;
static bool      fphDragMoved = false;

// Identificadores de zona tactil. Nunca 0: 0 significa "nada".
enum {
  FPHH_NONE = 0, FPHH_BACK, FPHH_SEC_BASE = 10,          // 10..18 -> secciones
  FPHH_PAIR = 40, FPHH_CONFIRM, FPHH_FORGET, FPHH_RETRY,
  FPHH_LINK_ON, FPHH_LINK_OFF, FPHH_FIND, FPHH_RELAY_TOGGLE,
  FPHH_SRC_AUTO, FPHH_SRC_PHONE, FPHH_SRC_PC,
  FPHH_HOST_CLEAR, FPHH_SYNC_NOW, FPHH_CLEAR_NOTIFS,
  FPHH_PRIV_BODY, FPHH_PRIV_SENS, FPHH_PRIV_HIST, FPHH_WIPE,
  FPHH_MEDIA_BASE = 80,                                   // 80..83
  FPHH_NOTIF_BASE = 100,                                  // 100+i
};

// Ruta del volcado del historial. Un solo fichero, escritura AGRUPADA.
#define FPH_STORE_PATH     "/flexphone/state.bin"
#define FPH_SAVE_EVERY_MS  30000u
// El VINCULO va en NVS, no en LittleFS: es material de clave y NVS
// es donde el sistema guarda ya las credenciales de Wi-Fi.
#define FPH_NVS_NS         "flexphone"
#define FPH_NVS_KEY        "bondkey"
#define FPH_NVS_PEER       "bondpeer"
#define FPH_NVS_NAME       "bondname"
#define FPH_NVS_SELF       "selfid"

static void fphToastShow(const char* msg){
  flexLinkUtf8Copy(fphToast, sizeof(fphToast), msg ? msg : "");
  fphToastUntil = millis() + 2800;
  fphDirtyUi = true;
}

// =============================================================
//  IDENTIDAD DE ESTE FLEX OS
// =============================================================
// Entra en la derivacion de la clave del vinculo, asi que tiene que
// ser ESTABLE entre reinicios. Se genera una vez y se guarda; la MAC
// serviria, pero eso publicaria un identificador de hardware en un
// mensaje que cualquiera de la red puede leer.
static char fphSelfId[FLXA_ID_MAX] = {0};

static void fphSelfIdLoad(){
  Preferences p;
  if(p.begin(FPH_NVS_NS, true)){
    String s = p.getString(FPH_NVS_SELF, "");
    p.end();
    if(s.length() > 0 && s.length() < FLXA_ID_MAX){
      flexLinkUtf8Copy(fphSelfId, sizeof(fphSelfId), s.c_str());
      return;
    }
  }
  uint32_t a = esp_random(), b = esp_random();
  snprintf(fphSelfId, sizeof(fphSelfId), "flexos-%08lx%08lx",
           (unsigned long)a, (unsigned long)b);
  if(p.begin(FPH_NVS_NS, false)){ p.putString(FPH_NVS_SELF, fphSelfId); p.end(); }
}

// =============================================================
//  VINCULO EN NVS
// =============================================================
static void fphBondSave(){
  Preferences p;
  if(!p.begin(FPH_NVS_NS, false)) return;
  if(fphLink.bond.valid){
    p.putBytes(FPH_NVS_KEY, fphLink.bond.key, FLXA_KEY_SIZE);
    p.putString(FPH_NVS_PEER, fphLink.bond.peerId);
    p.putString(FPH_NVS_NAME, fphLink.bond.peerName);
  } else {
    p.remove(FPH_NVS_KEY);
    p.remove(FPH_NVS_PEER);
    p.remove(FPH_NVS_NAME);
  }
  p.end();
}

static void fphBondLoad(){
  Preferences p;
  if(!p.begin(FPH_NVS_NS, true)) return;
  FlexPhoneBond b;
  memset(&b, 0, sizeof(b));
  const size_t n = p.getBytes(FPH_NVS_KEY, b.key, FLXA_KEY_SIZE);
  String peer = p.getString(FPH_NVS_PEER, "");
  String name = p.getString(FPH_NVS_NAME, "");
  p.end();
  // Una clave de tamano equivocado NO se usa a medias: eso daria un
  // vinculo que nunca autentica y un "el telefono no conecta" sin
  // ninguna pista. Media clave es lo mismo que ninguna.
  if(n != FLXA_KEY_SIZE || peer.length() == 0){
    memset(&b, 0, sizeof(b));
    return;
  }
  flexLinkUtf8Copy(b.peerId,   sizeof(b.peerId),   peer.c_str());
  flexLinkUtf8Copy(b.peerName, sizeof(b.peerName), name.c_str());
  b.valid = true;
  flexPhoneLinkSetBond(&fphLink, &b);
  memset(&b, 0, sizeof(b));       // no se deja la clave en la pila
}

// =============================================================
//  PERSISTENCIA DEL HISTORIAL (agrupada, nunca en el dibujo)
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
// Preferencia del usuario: ¿el enlace debe levantarse solo? De
// fabrica NO. Encender una radio en el arranque por si acaso es
// gastar bateria por algo que el usuario no ha pedido.
static bool fphAutoLink = false;
#define FPH_NVS_AUTO "autolink"

// El enlace vive aunque la app este cerrada: las notificaciones
// tienen que seguir llegando. Lo que NO ocurre con la app cerrada es
// dibujar nada.
static void flexPhoneBegin(){
  flexPhoneModelInit(&fphModel);
  flexPhoneLinkInit(&fphLink);
  fphSelfIdLoad();
  flexPhoneLinkSetIdentity(&fphLink, fphSelfId);
  flexPhoneLinkSetTransport(&fphLink, flexPhoneWifiTransport());
  fphBondLoad();
  fphLoad();
  { Preferences p;
    if(p.begin(FPH_NVS_NS, true)){ fphAutoLink = p.getBool(FPH_NVS_AUTO, false); p.end(); } }
  // Aunque este marcado, NO se arranca aqui: en el arranque todavia
  // no hay Wi-Fi y la tarea se pasaria el rato esperando. Lo levanta
  // flexPhoneTick en cuanto la red este de verdad arriba.
}

// Un paso por frame desde loop(). Con el enlace apagado o no
// disponible sale en la primera linea: Flex Phone inactivo no cuesta
// FPS al escritorio, al panel rapido ni a los juegos.
static void flexPhoneTick(){
  const uint32_t now = millis();
  if(fphLink.state == FLP_LS_UNAVAILABLE || fphLink.state == FLP_LS_OFF){
    // Arranque automatico: solo si el usuario lo pidio, solo con Wi-Fi
    // conectado de verdad y solo si hay un telefono vinculado. Sin
    // vinculo no hay nada que reanudar y encender la tarea solo
    // gastaria bateria.
    if(fphAutoLink && fphLink.state == FLP_LS_OFF && flexPhoneLinkBonded(&fphLink) &&
       WiFi.status() == WL_CONNECTED && !gAirplane){
      static uint32_t lastTry = 0;
      if(now - lastTry > 5000){ lastTry = now; flexPhoneLinkStart(&fphLink); }
    }
    return;
  }
  const bool wasReady = flexPhoneLinkReady(&fphLink);
  const bool hadBond  = flexPhoneLinkBonded(&fphLink);
  flexPhoneLinkTick(&fphLink, &fphModel, now);
  // El vinculo se guarda EN CUANTO se cierra, no al salir de la app:
  // un corte de corriente justo despues de emparejar dejaria al
  // usuario emparejando otra vez sin entender por que.
  if(!hadBond && flexPhoneLinkBonded(&fphLink)) fphBondSave();
  if(!wasReady && flexPhoneLinkReady(&fphLink)) fphDirtyUi = true;
  fphSave(false);                        // agrupada: casi siempre no hace nada
}

// -------------------------------------------------------------
//  Estado resumido, para la interfaz y para el resto del sistema
// -------------------------------------------------------------
static uint8_t fphLinkVisualState(){
  switch(fphLink.state){
    case FLP_LS_READY:       return FG_ST_OK;
    case FLP_LS_SEARCHING:
    case FLP_LS_CONNECTING:
    case FLP_LS_PAIRING:
    case FLP_LS_AUTH:        return FG_ST_BUSY;
    case FLP_LS_ERROR:       return FG_ST_BAD;
    case FLP_LS_UNAVAILABLE: return FG_ST_OFF;
    default:                 return FG_ST_OFF;    // apagado por el usuario
  }
}

// ¿Esta capacidad esta disponible AHORA? Es la unica puerta: si algo
// no esta aqui, la interfaz no lo ofrece como funcional.
static bool fphCapOn(uint16_t bit){
  return flexPhoneLinkReady(&fphLink) && fphModel.caps.valid &&
         (fphModel.caps.granted & bit) != 0;
}

// Aleatoriedad para el emparejamiento. El nucleo no depende de
// Arduino: se le pasa la fuente desde aqui.
static uint32_t fphRandom(){ return esp_random(); }

// -------------------------------------------------------------
//  EL NAVEGADOR, VISTO DESDE FLEX PHONE
// -------------------------------------------------------------
// Un solo sitio donde se traduce el estado REAL a lo que la pantalla
// del navegador ensena. La decision de que fuente se usa de verdad la
// sigue tomando flexBrSourceResolve, en el nucleo probado del
// navegador: aqui solo se le dan las disponibilidades reales.
static bool fphRelayUp(){ return fphModel.relay.state == FLP_RELAY_UP; }

static int fphBrSourcePref(){
  const BrSettings* st = flexBrowserSettings();
  return st ? (int)st->source : BRSRC_AUTO;
}
static int fphBrSourceActive(){
  const BrSettings* st = flexBrowserSettings();
  if(!st) return BRSRC_NONE;
  return flexBrSourceResolve(st, fphRelayUp(), st->cloudServer[0] != 0, st->server[0] != 0);
}
static const char* fphBrSourceActiveName(){
  const int a = fphBrSourceActive();
  if(a == BRSRC_NONE){
    const BrSettings* st = flexBrowserSettings();
    return st ? flexBrSourceWhyNone(st, fphRelayUp(), st->cloudServer[0] != 0,
                                    st->server[0] != 0)
              : "";
  }
  return flexBrSourceName(a);
}

// =============================================================
//  PINTURA
// =============================================================
#define FPH_MX     20                     // margen lateral
#define FPH_CW     (SCR_W - 2 * FPH_MX)   // ancho de tarjeta
#define FPH_TOP    96                     // primera fila bajo la cabecera

// Cabecera comun. El punto de estado es verde SOLO si hay sesion
// autenticada de verdad.
static void fphHeader(const char* title, bool nested){
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  if(nested){
    strokeSeg(38, 30, 26, 42, 3, TH_TXT);
    strokeSeg(26, 42, 38, 54, 3, TH_TXT);
    fgHitAdd(0, 0, 80, 76, FPHH_BACK);
  }
  fgTextEllipsis(nested ? 66 : 24, 26, SCR_W - (nested ? 130 : 90), title, 3, TH_TXT);
  fgStGlyph(SCR_W - 34, 42, 8, fphLinkVisualState());
  hLine(20, 78, SCR_W - 40, TH_DIV);
}

// -------------------------------------------------------------
//  1) INICIO
// -------------------------------------------------------------
// Lo importante SIN entrar en cinco pantallas: que telefono, si esta
// conectado, bateria, red y latencia. Y debajo, las secciones.
static void fphRenderInicio(){
  fphHeader("Flex Phone", false);
  const int top = FPH_TOP;
  fgScrollSetView(&fphScr, top, SCR_H - 6);
  const int y0 = top - fphScr.off;
  int y = y0;

  const bool ready = flexPhoneLinkReady(&fphLink);
  const uint8_t vs = fphLinkVisualState();

  // ---- Tarjeta del telefono ----
  const int cardH = 132;
  fgCard(FPH_MX, y, FPH_CW, cardH);
  {
    // Nombre: el que informo el telefono; si no hay sesion, el del
    // vinculo guardado. Nunca un nombre inventado.
    const char* nm = NULL;
    if(ready && fphModel.phone.name[0])      nm = fphModel.phone.name;
    else if(fphLink.bond.peerName[0])        nm = fphLink.bond.peerName;
    else if(flexPhoneLinkBonded(&fphLink))   nm = LI() == 1 ? "Paired phone" : "Telefono vinculado";
    else                                     nm = LI() == 1 ? "No phone linked" : "Sin telefono vinculado";
    fgTextEllipsis(FPH_MX + 18, y + 14, FPH_CW - 36, nm, 2, TH_TXT);
    fgStatusChip(FPH_MX + 18, y + 44, vs,
                 fphLink.state == FLP_LS_PAIRING ? (LI() == 1 ? "Pairing" : "Emparejando")
                                                 : fgStName(vs));

    // Tres datos REALES, o un guion. Nada se estima.
    char b1[24], b2[24], b3[24];
    if(ready && fphModel.phone.valid && fphModel.phone.battery <= 100)
      snprintf(b1, sizeof(b1), "%u%%%s", fphModel.phone.battery,
               fphModel.phone.charging ? " +" : "");
    else snprintf(b1, sizeof(b1), "--");

    const char* net = "--";
    if(ready && fphModel.phone.valid){
      switch(fphModel.phone.net){
        case FLP_NET_WIFI:   net = "Wi-Fi"; break;
        case FLP_NET_MOBILE: net = LI() == 1 ? "Mobile" : "Movil"; break;
        case FLP_NET_NONE:   net = LI() == 1 ? "No net" : "Sin red"; break;
        default: break;
      }
    }
    snprintf(b2, sizeof(b2), "%s", net);

    uint16_t rtt = 0;
    if(ready && flexPhoneLinkLatency(&fphLink, &rtt)) snprintf(b3, sizeof(b3), "%u ms", rtt);
    else snprintf(b3, sizeof(b3), "-- ms");

    const int bw = (FPH_CW - 36) / 3;
    const int by = y + 84;
    const char* lbl[3] = { LI() == 1 ? "Battery" : "Bateria",
                           LI() == 1 ? "Network" : "Red",
                           LI() == 1 ? "Latency" : "Latencia" };
    const char* val[3] = { b1, b2, b3 };
    for(int i = 0; i < 3; i++){
      drawTextC(FPH_MX + 18 + bw * i + bw / 2, by,      lbl[i], 1, TH_MUTE);
      drawTextC(FPH_MX + 18 + bw * i + bw / 2, by + 18, val[i], 2, TH_TXT);
    }
  }
  y += cardH + 12;

  // ---- Accion principal, la que toque AHORA ----
  if(fphLink.state == FLP_LS_PAIRING && fphLink.code[0]){
    const int h = 128;
    fgCardAccent(FPH_MX, y, FPH_CW, h, TH_PRIM);
    drawText(FPH_MX + 20, y + 12, LI() == 1 ? "Type this code on the phone"
                                            : "Teclea este codigo en el telefono", 1, TH_MUTE);
    drawTextC(SCR_W / 2, y + 34, fphLink.code, 4, TH_TXT);
    fgButton(FPH_MX + 20, y + 80, FPH_CW - 40, 36,
             LI() == 1 ? "Confirm here" : "Confirmar aqui", FG_BTN_PRIMARY, FPHH_CONFIRM);
    y += h + 12;
  } else if(fphLink.state == FLP_LS_OFF){
    fgButton(FPH_MX, y, FPH_CW, 44,
             LI() == 1 ? "Turn link on" : "Activar el enlace", FG_BTN_PRIMARY, FPHH_LINK_ON);
    y += 56;
  } else if(fphLink.state == FLP_LS_ERROR){
    y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Link failed" : "El enlace fallo",
                  fphLink.err[0] ? fphLink.err : flexPhoneTrStatus(fphLink.tr), TH_ERR) + 10;
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Retry" : "Reintentar",
             FG_BTN_PRIMARY, FPHH_RETRY);
    y += 56;
  } else if(!flexPhoneLinkBonded(&fphLink)){
    fgButton(FPH_MX, y, FPH_CW, 44,
             LI() == 1 ? "Pair a phone" : "Emparejar telefono", FG_BTN_PRIMARY, FPHH_PAIR);
    y += 56;
  }

  // ---- Secciones ----
  fgSectionHeader(FPH_MX + 4, y, LI() == 1 ? "SECTIONS" : "SECCIONES");
  y += 22;
  for(int s = FPH_NOTIFS; s < FPH_SEC_N; s++){
    // Subtitulo REAL de cada seccion: un numero, un estado o un
    // motivo. Nunca un texto decorativo.
    char sub[56]; sub[0] = 0;
    switch(s){
      case FPH_NOTIFS:
        if(fphModel.notifCount)
          snprintf(sub, sizeof(sub), LI() == 1 ? "%u in history" : "%u en el historial",
                   fphModel.notifCount);
        else snprintf(sub, sizeof(sub), LI() == 1 ? "Nothing yet" : "Todavia nada");
        break;
      case FPH_SERVIDOR:
        snprintf(sub, sizeof(sub), "%s",
                 fphModel.relay.state == FLP_RELAY_UP    ? (LI() == 1 ? "Running" : "Activo") :
                 fphModel.relay.state == FLP_RELAY_ERROR ? (LI() == 1 ? "Error" : "Con error") :
                                                           (LI() == 1 ? "Stopped" : "Parado"));
        break;
      case FPH_NAVEGADOR: snprintf(sub, sizeof(sub), "%s", fphBrSourceActiveName()); break;
      case FPH_ESTADO:
        snprintf(sub, sizeof(sub), "%s", (ready && fphModel.caps.valid && fphModel.caps.model[0])
                 ? fphModel.caps.model : (LI() == 1 ? "No data" : "Sin datos"));
        break;
      case FPH_CONEXION: {
        char peer[FLP_LINK_PEER_MAX];
        flexPhoneLinkPeer(&fphLink, peer, sizeof(peer));
        snprintf(sub, sizeof(sub), "%s", peer[0] ? peer : flexPhoneTransportName(FLP_TR_WIFI));
        break;
      }
      case FPH_SYNC:
        if(fphModel.lastSyncMs)
          snprintf(sub, sizeof(sub), LI() == 1 ? "%lu s ago" : "hace %lu s",
                   (unsigned long)((millis() - fphModel.lastSyncMs) / 1000));
        else snprintf(sub, sizeof(sub), LI() == 1 ? "Never" : "Nunca");
        break;
      case FPH_DIAG:
        snprintf(sub, sizeof(sub), LI() == 1 ? "%lu received · %lu bad"
                                             : "%lu recibidos · %lu malos",
                 (unsigned long)fphLink.nRx, (unsigned long)fphLink.nBad);
        break;
      case FPH_SEGURIDAD:
        snprintf(sub, sizeof(sub), "%s", flexPhoneLinkBonded(&fphLink)
                 ? (LI() == 1 ? "Paired device" : "Dispositivo vinculado")
                 : (LI() == 1 ? "Not paired" : "Sin vincular"));
        break;
      default: break;
    }
    fgNavRow(FPH_MX, y, FPH_CW, 62, fphSecName(s), sub,
             (uint16_t)(FPHH_SEC_BASE + s), true);
    y += 70;
  }

  fphScr.content = y - y0 + 10;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  2) NOTIFICACIONES
// -------------------------------------------------------------
static void fphRenderNotifs(){
  fphHeader(fphSecName(FPH_NOTIFS), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  if(!fphCapOn(FLP_CAP_NOTIF) && fphModel.notifCount == 0){
    const char* why = fphModel.caps.valid
      ? flexPhoneCapWhyNot(fphModel.caps.supported, fphModel.caps.granted, FLP_CAP_NOTIF)
      : (LI() == 1 ? "Connect the phone to receive its notifications."
                   : "Conecta el telefono para recibir sus notificaciones.");
    fgEmpty(FPH_TOP + 40, LI() == 1 ? "No notifications" : "Sin notificaciones", why);
    fphScr.content = 0;
    return;
  }

  if(fphModel.notifCount == 0){
    fgEmpty(FPH_TOP + 40, LI() == 1 ? "No notifications" : "Sin notificaciones",
            LI() == 1 ? "The phone is connected. Anything that arrives will show up here."
                      : "El telefono esta conectado. Lo que llegue aparecera aqui.");
    fphScr.content = 0;
    return;
  }

  // Agrupadas por aplicacion, la mas reciente primero.
  int shown = 0;
  for(int i = 0; i < FLP_NOTIF_MAX && shown < 40; i++){
    const FlexPhoneNotif* n = &fphModel.notif[i];
    if(!n->used) continue;
    const int h = 74;
    if(y + h > FPH_TOP - 80 && y < SCR_H + 40){     // solo lo visible se pinta
      const uint16_t accent = (n->pri >= FLP_PRI_HIGH) ? TH_PRIM : TH_DIV;
      fgCardAccent(FPH_MX, y, FPH_CW, h, accent);
      fgTextEllipsis(FPH_MX + 20, y + 10, FPH_CW - 130, n->app[0] ? n->app : n->pkg, 1, TH_MUTE);
      const int cnt = flexPhoneNotifCountPkg(&fphModel, n->pkg);
      if(cnt > 1){
        char c[12]; snprintf(c, sizeof(c), "x%d", cnt);
        drawTextR(FPH_MX + FPH_CW - 20, y + 10, c, 1, TH_MUTE);
      }
      fgTextEllipsis(FPH_MX + 20, y + 28, FPH_CW - 40, n->title, 2, TH_TXT);
      char body[FLP_TEXT_MAX];
      if(flexPhoneNotifBody(&fphModel, n, false, body, sizeof(body)))
        fgTextEllipsis(FPH_MX + 20, y + 52, FPH_CW - 40, body, 1, TH_TXT2);
      else
        drawText(FPH_MX + 20, y + 52,
                 LI() == 1 ? "Content hidden" : "Contenido oculto", 1, TH_DIS);
      fgHitAdd(FPH_MX, y, FPH_CW, h, (uint16_t)(FPHH_NOTIF_BASE + i));
    }
    y += 82;
    shown++;
  }

  y += 6;
  fgButton(FPH_MX, y, FPH_CW, 40, LI() == 1 ? "Clear history" : "Borrar el historial",
           FG_BTN_DANGER, FPHH_CLEAR_NOTIFS);
  y += 50;

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  3) SERVIDOR  (el servicio del telefono)
// -------------------------------------------------------------
static void fphRenderServidor(){
  fphHeader(fphSecName(FPH_SERVIDOR), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  const FlexPhoneRelay* r = &fphModel.relay;
  uint8_t st = FG_ST_OFF;
  const char* stName = LI() == 1 ? "Stopped" : "Parado";
  switch(r->state){
    case FLP_RELAY_UP:        st = FG_ST_OK;   stName = LI() == 1 ? "Running" : "Activo"; break;
    case FLP_RELAY_STARTING:  st = FG_ST_BUSY; stName = LI() == 1 ? "Starting" : "Arrancando"; break;
    case FLP_RELAY_ERROR:     st = FG_ST_BAD;  stName = LI() == 1 ? "Error" : "Error"; break;
    case FLP_RELAY_SUSPENDED: st = FG_ST_BAD;  stName = LI() == 1 ? "Suspended by Android"
                                                                  : "Suspendido por Android"; break;
    default: break;
  }

  fgCard(FPH_MX, y, FPH_CW, 132);
  drawText(FPH_MX + 18, y + 12, LI() == 1 ? "Phone server" : "Servidor del telefono", 2, TH_TXT);
  fgStatusChip(FPH_MX + 18, y + 42, st, stName);
  {
    char addr[40] = "--";
    if(r->state == FLP_RELAY_UP)
      snprintf(addr, sizeof(addr), "%u.%u.%u.%u:%u", r->ip[0], r->ip[1], r->ip[2], r->ip[3], r->port);
    fgRow(FPH_MX + 18, y + 76, FPH_CW - 36, LI() == 1 ? "Address" : "Direccion", addr);
    // Se dice sin adornos que no hay TLS: anunciar como cifrado algo
    // que no lo esta es peor que no decir nada.
    fgRow(FPH_MX + 18, y + 98, FPH_CW - 36, LI() == 1 ? "Transport" : "Transporte",
          r->state == FLP_RELAY_UP ? (r->tls ? "TLS" : (LI() == 1 ? "Plain (LAN)" : "Sin TLS (red local)"))
                                   : "--");
  }
  y += 144;

  if(r->state == FLP_RELAY_ERROR && r->err[0])
    y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "The phone reported" : "El telefono informo",
                  r->err, TH_ERR) + 10;

  // El boton solo existe si la capacidad esta CONCEDIDA. Si no, en su
  // lugar va el motivo.
  if(fphCapOn(FLP_CAP_RELAY)){
    const bool up = (r->state == FLP_RELAY_UP || r->state == FLP_RELAY_STARTING);
    fgButton(FPH_MX, y, FPH_CW, 44,
             up ? (LI() == 1 ? "Stop server" : "Detener el servidor")
                : (LI() == 1 ? "Start server" : "Iniciar el servidor"),
             up ? FG_BTN_DANGER : FG_BTN_PRIMARY, FPHH_RELAY_TOGGLE);
    y += 56;
  } else {
    const char* why = fphModel.caps.valid
      ? flexPhoneCapWhyNot(fphModel.caps.supported, fphModel.caps.granted, FLP_CAP_RELAY)
      : (LI() == 1 ? "Requires a connected phone." : "Requiere el telefono conectado.");
    y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Not available" : "No disponible", why, TH_DIS) + 10;
  }

  y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Real limits" : "Limites reales",
      LI() == 1
      ? "Android can stop the server when the screen goes off, on low memory or by "
        "manufacturer policy. If that happens it shows up here as an error, not as silence."
      : "Android puede detener el servidor al apagarse la pantalla, por memoria o por "
        "politica del fabricante. Si pasa, aparece aqui como error, no como silencio.",
      TH_MUTE) + 10;

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  4) NAVEGADOR
// -------------------------------------------------------------
static void fphRenderNavegador(){
  fphHeader(fphSecName(FPH_NAVEGADOR), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Where pages are rendered"
                                             : "Donde se dibujan las paginas",
      LI() == 1
      ? "Flex OS does not render the web itself. A backend does it and sends frames. "
        "Both backends speak the same protocol, so the browser needs no changes."
      : "Flex OS no dibuja la web por su cuenta. Lo hace un backend y manda los "
        "fotogramas. Los dos hablan el mismo protocolo, asi que el navegador no cambia.",
      TH_PRIM) + 12;

  fgSectionHeader(FPH_MX + 4, y, LI() == 1 ? "SOURCE" : "FUENTE");
  y += 22;

  // Las fuentes REALES del navegador, con el estado de cada una. Una
  // fuente que ahora mismo no puede servir se pinta apagada y NO
  // registra zona tactil: elegirla no llevaria a ninguna parte.
  const BrSettings* bst = flexBrowserSettings();
  const int pref = fphBrSourcePref();
  const bool cloudCfg  = bst && bst->cloudServer[0];
  const bool manualCfg = bst && bst->server[0];
  struct { int src; const char* sub; uint16_t id; bool on; } opt[3] = {
    { BRSRC_AUTO,
      LI() == 1 ? "Phone if its server is up, then the configured one"
                : "El telefono si su servidor esta arriba, si no el configurado",
      FPHH_SRC_AUTO, true },
    { BRSRC_PHONE,
      fphRelayUp() ? (LI() == 1 ? "Server running on the phone"
                                : "Servidor activo en el telefono")
                   : (LI() == 1 ? "The phone server is not running"
                                : "El servidor del telefono no esta activo"),
      FPHH_SRC_PHONE, fphRelayUp() },
    { BRSRC_MANUAL,
      manualCfg ? (LI() == 1 ? "The server set in Browser settings"
                             : "El servidor fijado en Ajustes del navegador")
                : (LI() == 1 ? "No server set in Browser settings"
                             : "Sin servidor en Ajustes del navegador"),
      FPHH_SRC_PC, manualCfg },
  };
  for(int i = 0; i < 3; i++){
    const bool sel = (pref == opt[i].src);
    fgCard(FPH_MX, y, FPH_CW, 62);
    if(sel) fillRoundRect(FPH_MX + 3, y + 10, 4, 42, 2, TH_PRIM);
    fgTextEllipsis(FPH_MX + 18, y + 12, FPH_CW - 70, flexBrSourceName(opt[i].src), 2,
                   opt[i].on ? TH_TXT : TH_DIS);
    fgTextEllipsis(FPH_MX + 18, y + 38, FPH_CW - 70, opt[i].sub, 1, TH_MUTE);
    if(sel) fgStGlyph(FPH_MX + FPH_CW - 28, y + 31, 7, opt[i].on ? FG_ST_OK : FG_ST_BAD);
    if(opt[i].on) fgHitAdd(FPH_MX, y, FPH_CW, 62, opt[i].id);
    y += 70;
  }
  // La nube solo se nombra si el usuario la configuro: ofrecer una
  // fuente que no existe seria un boton que no lleva a nada.
  if(cloudCfg){
    fgCard(FPH_MX, y, FPH_CW, 46);
    fgTextEllipsis(FPH_MX + 18, y + 14, FPH_CW - 40, flexBrSourceName(BRSRC_CLOUD), 1, TH_MUTE);
    y += 54;
  }

  y += 6;
  fgCard(FPH_MX, y, FPH_CW, 76);
  fgRow(FPH_MX + 18, y + 14, FPH_CW - 36, LI() == 1 ? "Active source" : "Fuente activa",
        fphBrSourceActiveName());
  fgRow(FPH_MX + 18, y + 42, FPH_CW - 36, LI() == 1 ? "Protocol" : "Protocolo", "FBP/1");
  y += 88;

  // Los modos exclusivos NO se caen en secreto a otra fuente.
  if(pref == BRSRC_PHONE && !fphRelayUp())
    y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Phone only" : "Solo telefono",
        LI() == 1 ? "This mode will not silently fall back to the PC. Start the server first."
                  : "Este modo no se cae en secreto al PC. Inicia antes el servidor.",
        TH_WARN) + 10;

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  5) ESTADO DEL TELEFONO
// -------------------------------------------------------------
static void fphRenderEstado(){
  fphHeader(fphSecName(FPH_ESTADO), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  if(!flexPhoneLinkReady(&fphLink) || !fphModel.phone.valid){
    fgEmpty(FPH_TOP + 40, LI() == 1 ? "No data" : "Sin datos",
            LI() == 1 ? "The phone has to be connected. Nothing is shown from an old session: "
                        "a battery level from ten minutes ago is not the battery level."
                      : "El telefono tiene que estar conectado. No se ensena nada de una sesion "
                        "anterior: una bateria de hace diez minutos no es la bateria de ahora.");
    fphScr.content = 0;
    return;
  }

  const FlexPhoneState* p = &fphModel.phone;
  const FlexPhoneCaps*  c = &fphModel.caps;

  // ---- Bateria ----
  fgCard(FPH_MX, y, FPH_CW, 92);
  drawText(FPH_MX + 18, y + 12, LI() == 1 ? "Battery" : "Bateria", 1, TH_MUTE);
  {
    char v[24];
    if(p->battery <= 100) snprintf(v, sizeof(v), "%u%%", p->battery);
    else                  snprintf(v, sizeof(v), "--");
    drawText(FPH_MX + 18, y + 32, v, 3, TH_TXT);
    if(p->charging)
      drawTextR(FPH_MX + FPH_CW - 18, y + 40, LI() == 1 ? "Charging" : "Cargando", 1, TH_OK);
    else if(p->powerSave)
      drawTextR(FPH_MX + FPH_CW - 18, y + 40, LI() == 1 ? "Power saver" : "Ahorro de bateria", 1, TH_WARN);
    const uint16_t col = (p->battery <= 15) ? TH_ERR : (p->battery <= 30 ? TH_WARN : TH_OK);
    fgMeter(FPH_MX + 18, y + 70, FPH_CW - 36, p->battery <= 100 ? p->battery : -1, col);
  }
  y += 104;

  // ---- Almacenamiento y memoria: solo si el telefono los mando ----
  if(p->storageTotalMb){
    fgCard(FPH_MX, y, FPH_CW, 74);
    drawText(FPH_MX + 18, y + 12, LI() == 1 ? "Storage" : "Almacenamiento", 1, TH_MUTE);
    char v[40];
    snprintf(v, sizeof(v), "%lu / %lu GB",
             (unsigned long)((p->storageTotalMb - p->storageFreeMb) / 1024),
             (unsigned long)(p->storageTotalMb / 1024));
    drawTextR(FPH_MX + FPH_CW - 18, y + 12, v, 1, TH_TXT);
    const int used = (int)(100u - (p->storageFreeMb * 100u) / p->storageTotalMb);
    fgMeter(FPH_MX + 18, y + 44, FPH_CW - 36, used, used >= 90 ? TH_WARN : TH_PRIM);
    y += 86;
  }
  if(p->ramTotalMb){
    fgCard(FPH_MX, y, FPH_CW, 74);
    drawText(FPH_MX + 18, y + 12, LI() == 1 ? "Memory" : "Memoria", 1, TH_MUTE);
    char v[40];
    snprintf(v, sizeof(v), "%lu / %lu MB",
             (unsigned long)(p->ramTotalMb - p->ramFreeMb), (unsigned long)p->ramTotalMb);
    drawTextR(FPH_MX + FPH_CW - 18, y + 12, v, 1, TH_TXT);
    const int used = (int)(100u - (p->ramFreeMb * 100u) / p->ramTotalMb);
    fgMeter(FPH_MX + 18, y + 44, FPH_CW - 36, used, TH_PRIM);
    y += 86;
  }

  // ---- Identidad del dispositivo ----
  if(c->valid){
    fgCard(FPH_MX, y, FPH_CW, 108);
    fgRow(FPH_MX + 18, y + 14, FPH_CW - 36, LI() == 1 ? "Model" : "Modelo",
          c->model[0] ? c->model : "--");
    fgRow(FPH_MX + 18, y + 42, FPH_CW - 36, LI() == 1 ? "Maker" : "Fabricante",
          c->vendor[0] ? c->vendor : "--");
    fgRow(FPH_MX + 18, y + 70, FPH_CW - 36, LI() == 1 ? "System" : "Sistema",
          c->osver[0] ? c->osver : "--");
    y += 120;
  }

  // ---- Capacidades: lo que este telefono PUEDE de verdad ----
  fgSectionHeader(FPH_MX + 4, y, LI() == 1 ? "CAPABILITIES" : "CAPACIDADES");
  y += 22;
  if(!c->valid){
    fgCard(FPH_MX, y, FPH_CW, 52);
    drawText(FPH_MX + 18, y + 18, LI() == 1 ? "The phone has not reported them yet"
                                            : "El telefono aun no las ha informado", 1, TH_MUTE);
    y += 62;
  } else {
    static const uint16_t bits[] = {
      FLP_CAP_NOTIF, FLP_CAP_REPLY, FLP_CAP_MEDIA,
      FLP_CAP_RELAY, FLP_CAP_STATE, FLP_CAP_TIME, FLP_CAP_FIND, FLP_CAP_BLE,
    };
    const int n = (int)(sizeof(bits) / sizeof(bits[0]));
    fgCard(FPH_MX, y, FPH_CW, 14 + n * 26);
    int ry = y + 12;
    for(int i = 0; i < n; i++){
      const char* why = flexPhoneCapWhyNot(c->supported, c->granted, bits[i]);
      const uint8_t st = why ? ((c->supported & bits[i]) ? FG_ST_BAD : FG_ST_OFF) : FG_ST_OK;
      fgRowState(FPH_MX + 12, ry, FPH_CW - 24, flexPhoneCapName(bits[i]), st, why);
      ry += 26;
    }
    y += 14 + n * 26 + 12;
  }

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  6) CONEXION
// -------------------------------------------------------------
static void fphRenderConexion(){
  fphHeader(fphSecName(FPH_CONEXION), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  const uint8_t vs = fphLinkVisualState();
  fgCard(FPH_MX, y, FPH_CW, 156);
  drawText(FPH_MX + 18, y + 12, LI() == 1 ? "Link" : "Enlace", 2, TH_TXT);
  fgStatusChip(FPH_MX + 18, y + 42, vs, flexPhoneLinkStateName(fphLink.state));
  {
    char peer[FLP_LINK_PEER_MAX];
    flexPhoneLinkPeer(&fphLink, peer, sizeof(peer));
    fgRow(FPH_MX + 18, y + 76,  FPH_CW - 36, LI() == 1 ? "Transport" : "Transporte",
          flexPhoneTransportName(fphLink.tr ? fphLink.tr->kind : (uint8_t)FLP_TR_NONE));
    fgRow(FPH_MX + 18, y + 98,  FPH_CW - 36, LI() == 1 ? "Phone address" : "Direccion del telefono",
          peer[0] ? peer : "--");
    char v[24];
    uint16_t rtt = 0;
    if(flexPhoneLinkLatency(&fphLink, &rtt)) snprintf(v, sizeof(v), "%u ms", rtt);
    else snprintf(v, sizeof(v), "--");
    fgRow(FPH_MX + 18, y + 120, FPH_CW - 36, LI() == 1 ? "Latency" : "Latencia", v);
  }
  y += 168;

  // Motivo REAL del transporte, sea bueno o malo.
  {
    const char* s = flexPhoneTrStatus(fphLink.tr);
    if(s && *s) y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Network" : "Red", s,
                              vs == FG_ST_BAD ? TH_ERR : TH_MUTE) + 10;
  }
  if(fphLink.state == FLP_LS_ERROR && fphLink.err[0])
    y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Last error" : "Ultimo error",
                  fphLink.err, TH_ERR) + 10;

  // Acciones
  if(fphLink.state == FLP_LS_OFF){
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Turn link on" : "Activar el enlace",
             FG_BTN_PRIMARY, FPHH_LINK_ON);
  } else {
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Turn link off" : "Apagar el enlace",
             FG_BTN_PLAIN, FPHH_LINK_OFF);
  }
  y += 54;
  if(fphLink.state == FLP_LS_ERROR || fphLink.state == FLP_LS_SEARCHING){
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Search again" : "Buscar otra vez",
             FG_BTN_PLAIN, FPHH_RETRY);
    y += 54;
  }
  if(flexPhoneWifiHostFixed()){
    fgButton(FPH_MX, y, FPH_CW, 40, LI() == 1 ? "Clear fixed address" : "Quitar la direccion fija",
             FG_BTN_PLAIN, FPHH_HOST_CLEAR);
    y += 50;
  }
  if(fphCapOn(FLP_CAP_FIND)){
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Ring the phone" : "Hacer sonar el telefono",
             FG_BTN_PLAIN, FPHH_FIND);
    y += 54;
  }

  y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "How it finds the phone" : "Como encuentra el telefono",
      LI() == 1
      ? "Flex OS asks on the local network and the phone answers with its address. Some routers "
        "isolate clients and block that; then the address has to be typed in the phone app."
      : "Flex OS pregunta en la red local y el telefono contesta con su direccion. Algunos "
        "routers aislan a los clientes y lo bloquean; entonces hay que fijar la direccion "
        "desde la app del telefono.",
      TH_MUTE) + 10;

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  7) SINCRONIZACION
// -------------------------------------------------------------
static void fphRenderSync(){
  fphHeader(fphSecName(FPH_SYNC), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "Event driven" : "Por eventos",
      LI() == 1
      ? "Nothing is polled. The phone sends each change as it happens and Flex OS applies it. "
        "That is why an arriving notification feels immediate and an idle link costs nothing."
      : "No se sondea nada. El telefono manda cada cambio cuando ocurre y Flex OS lo aplica. "
        "Por eso una notificacion que llega se siente inmediata y un enlace en reposo no cuesta nada.",
      TH_PRIM) + 12;

  fgCard(FPH_MX, y, FPH_CW, 158);
  {
    char v[40];
    if(fphModel.lastSyncMs)
      snprintf(v, sizeof(v), LI() == 1 ? "%lu s ago" : "hace %lu s",
               (unsigned long)((millis() - fphModel.lastSyncMs) / 1000));
    else snprintf(v, sizeof(v), LI() == 1 ? "Never" : "Nunca");
    fgRow(FPH_MX + 18, y + 16,  FPH_CW - 36, LI() == 1 ? "Last change" : "Ultimo cambio", v);
    snprintf(v, sizeof(v), "%u", fphModel.notifCount);
    fgRow(FPH_MX + 18, y + 44,  FPH_CW - 36, LI() == 1 ? "Notifications" : "Notificaciones", v);
    snprintf(v, sizeof(v), "%lu", (unsigned long)fphModel.stats.rxNotif);
    fgRow(FPH_MX + 18, y + 72,  FPH_CW - 36, LI() == 1 ? "Received" : "Recibidas", v);
    snprintf(v, sizeof(v), "%lu", (unsigned long)fphModel.stats.rxUpdate);
    fgRow(FPH_MX + 18, y + 100, FPH_CW - 36, LI() == 1 ? "Updated" : "Actualizadas", v);
    snprintf(v, sizeof(v), "%lu", (unsigned long)fphModel.stats.rxRemove);
    fgRow(FPH_MX + 18, y + 128, FPH_CW - 36, LI() == 1 ? "Removed" : "Retiradas", v);
  }
  y += 170;

  if(flexPhoneLinkReady(&fphLink)){
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Ask for a full sync" : "Pedir sincronizacion completa",
             FG_BTN_PRIMARY, FPHH_SYNC_NOW);
    y += 54;
  } else {
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Ask for a full sync" : "Pedir sincronizacion completa",
             FG_BTN_DISABLED, 0);
    y += 54;
    drawTextC(SCR_W / 2, y, LI() == 1 ? "Requires a connected phone"
                                      : "Requiere el telefono conectado", 1, TH_MUTE);
    y += 24;
  }

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  8) DIAGNOSTICO
// -------------------------------------------------------------
static void fphRenderDiag(){
  fphHeader(fphSecName(FPH_DIAG), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  // Cada fila es un servicio REAL con su estado REAL.
  struct { const char* name; uint8_t st; const char* detail; } rows[6];
  int n = 0;

  const uint8_t vs = fphLinkVisualState();
  rows[n].name = LI() == 1 ? "Connection" : "Conexion";
  rows[n].st = vs;
  rows[n].detail = flexPhoneLinkStateName(fphLink.state);
  n++;

  rows[n].name = LI() == 1 ? "Authentication" : "Autenticacion";
  rows[n].st = flexPhoneLinkReady(&fphLink) ? FG_ST_OK
             : (flexPhoneLinkBonded(&fphLink) ? FG_ST_BUSY : FG_ST_OFF);
  rows[n].detail = flexPhoneLinkReady(&fphLink) ? (LI() == 1 ? "Session open" : "Sesion abierta")
                 : (flexPhoneLinkBonded(&fphLink) ? (LI() == 1 ? "Paired, no session"
                                                               : "Vinculado, sin sesion")
                                                  : (LI() == 1 ? "Not paired" : "Sin vincular"));
  n++;

  rows[n].name = LI() == 1 ? "Notifications" : "Notificaciones";
  rows[n].st = fphCapOn(FLP_CAP_NOTIF) ? FG_ST_OK : FG_ST_OFF;
  rows[n].detail = fphCapOn(FLP_CAP_NOTIF) ? (LI() == 1 ? "Granted" : "Concedido")
                 : (fphModel.caps.valid
                    ? flexPhoneCapWhyNot(fphModel.caps.supported, fphModel.caps.granted, FLP_CAP_NOTIF)
                    : (LI() == 1 ? "Unknown" : "Sin datos"));
  n++;

  rows[n].name = LI() == 1 ? "Server" : "Servidor";
  rows[n].st = fphModel.relay.state == FLP_RELAY_UP ? FG_ST_OK
             : (fphModel.relay.state == FLP_RELAY_STARTING ? FG_ST_BUSY
             : (fphModel.relay.state == FLP_RELAY_ERROR ? FG_ST_BAD : FG_ST_OFF));
  rows[n].detail = fphModel.relay.state == FLP_RELAY_UP ? (LI() == 1 ? "Running" : "Activo")
                 : (fphModel.relay.state == FLP_RELAY_ERROR && fphModel.relay.err[0]
                    ? fphModel.relay.err : (LI() == 1 ? "Stopped" : "Parado"));
  n++;

  rows[n].name = LI() == 1 ? "Browser" : "Navegador";
  rows[n].st = (fphBrSourceActive() != BRSRC_NONE) ? FG_ST_OK : FG_ST_OFF;
  rows[n].detail = fphBrSourceActiveName();
  n++;

  rows[n].name = LI() == 1 ? "Sync" : "Sincronizacion";
  rows[n].st = fphModel.lastSyncMs ? FG_ST_OK : FG_ST_OFF;
  rows[n].detail = fphModel.lastSyncMs ? (LI() == 1 ? "Up to date" : "Al dia")
                                       : (LI() == 1 ? "Nothing yet" : "Todavia nada");
  n++;

  fgCard(FPH_MX, y, FPH_CW, 16 + n * 28);
  int ry = y + 14;
  for(int i = 0; i < n; i++){
    fgRowState(FPH_MX + 12, ry, FPH_CW - 24, rows[i].name, rows[i].st, rows[i].detail);
    ry += 28;
  }
  y += 16 + n * 28 + 14;

  // Contadores. SOLO numeros: esta pantalla se puede ensenar para
  // pedir ayuda sin revelar nada del contenido de las notificaciones.
  fgSectionHeader(FPH_MX + 4, y, LI() == 1 ? "COUNTERS" : "CONTADORES");
  y += 22;
  fgCard(FPH_MX, y, FPH_CW, 214);
  {
    char v[24];
    int ry2 = y + 14;
    #define FPH_CNT(lbl, val) do { snprintf(v, sizeof(v), "%lu", (unsigned long)(val)); \
      fgRow(FPH_MX + 18, ry2, FPH_CW - 36, (lbl), v); ry2 += 26; } while(0)
    uint16_t rtt = 0;
    if(flexPhoneLinkLatency(&fphLink, &rtt)) snprintf(v, sizeof(v), "%u ms", rtt);
    else snprintf(v, sizeof(v), "--");
    fgRow(FPH_MX + 18, ry2, FPH_CW - 36, LI() == 1 ? "Latency" : "Latencia", v); ry2 += 26;
    FPH_CNT(LI() == 1 ? "Frames in"  : "Tramas recibidas", fphLink.nRx);
    FPH_CNT(LI() == 1 ? "Frames out" : "Tramas enviadas",  fphLink.nTx);
    FPH_CNT(LI() == 1 ? "Bad frames" : "Tramas invalidas", fphLink.nBad);
    FPH_CNT(LI() == 1 ? "Dropped"    : "Descartadas",      fphLink.nDropped);
    FPH_CNT(LI() == 1 ? "Timeouts"   : "Tiempos agotados", fphLink.nTimeouts);
    FPH_CNT(LI() == 1 ? "Reconnects" : "Reconexiones",     fphLink.nReconnects);
    FPH_CNT(LI() == 1 ? "Auth failures" : "Fallos de autenticacion", fphLink.nAuthFail);
    #undef FPH_CNT
  }
  y += 226;

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  9) SEGURIDAD
// -------------------------------------------------------------
static void fphRenderSeguridad(){
  fphHeader(fphSecName(FPH_SEGURIDAD), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  // ---- Dispositivo vinculado ----
  fgSectionHeader(FPH_MX + 4, y, LI() == 1 ? "LINKED DEVICE" : "DISPOSITIVO VINCULADO");
  y += 22;
  if(flexPhoneLinkBonded(&fphLink)){
    fgCard(FPH_MX, y, FPH_CW, 132);
    fgTextEllipsis(FPH_MX + 18, y + 12, FPH_CW - 36,
                   fphLink.bond.peerName[0] ? fphLink.bond.peerName
                                            : (LI() == 1 ? "Paired phone" : "Telefono vinculado"),
                   2, TH_TXT);
    char peer[FLP_LINK_PEER_MAX];
    flexPhoneLinkPeer(&fphLink, peer, sizeof(peer));
    fgRow(FPH_MX + 18, y + 46, FPH_CW - 36, LI() == 1 ? "Identifier" : "Identificador",
          fphLink.bond.peerId);
    fgRow(FPH_MX + 18, y + 72, FPH_CW - 36, LI() == 1 ? "Address" : "Direccion",
          peer[0] ? peer : "--");
    char pv[16];
    snprintf(pv, sizeof(pv), "v%u", (unsigned)(fphModel.caps.valid ? fphModel.caps.protoVer
                                                                  : FLNK_VERSION));
    fgRow(FPH_MX + 18, y + 98, FPH_CW - 36, LI() == 1 ? "Protocol" : "Protocolo", pv);
    y += 144;
    fgButton(FPH_MX, y, FPH_CW, 44,
             LI() == 1 ? "Revoke and forget" : "Revocar y olvidar", FG_BTN_DANGER, FPHH_FORGET);
    y += 54;
    drawTextC(SCR_W / 2, y, LI() == 1 ? "Deletes the key and every synced notification"
                                      : "Borra la clave y todo lo sincronizado", 1, TH_MUTE);
    y += 28;
  } else {
    fgCard(FPH_MX, y, FPH_CW, 62);
    drawText(FPH_MX + 18, y + 20, LI() == 1 ? "No device paired" : "Ningun dispositivo vinculado",
             1, TH_MUTE);
    y += 74;
    fgButton(FPH_MX, y, FPH_CW, 44, LI() == 1 ? "Pair a phone" : "Emparejar telefono",
             FG_BTN_PRIMARY, FPHH_PAIR);
    y += 54;
  }

  // ---- Como funciona, sin adornos ----
  y += 6;
  y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "How pairing works" : "Como funciona el emparejamiento",
      LI() == 1
      ? "Flex OS shows a six digit code that you type on the phone. The code never travels: both "
        "ends derive the same key from it and then prove they have it, each one to the other."
      : "Flex OS ensena un codigo de seis digitos que tecleas en el telefono. El codigo no viaja: "
        "los dos extremos derivan de el la misma clave y luego se demuestran que la tienen, cada "
        "uno al otro.",
      TH_PRIM) + 10;

  y += fgNotice(FPH_MX, y, FPH_CW, LI() == 1 ? "What this does NOT protect"
                                             : "Lo que esto NO protege",
      LI() == 1
      ? "Content travels unencrypted on the local network. This stops a device that has not paired "
        "from opening a session; it does not protect against someone already listening on your "
        "network. It is not TLS and it is not shown as if it were."
      : "El contenido viaja sin cifrar por la red local. Esto impide que un dispositivo que no ha "
        "emparejado abra sesion; no protege frente a quien ya este escuchando tu red. No es TLS y "
        "no se ensena como si lo fuera.",
      TH_WARN) + 10;

  // ---- Privacidad ----
  fgSectionHeader(FPH_MX + 4, y, LI() == 1 ? "PRIVACY" : "PRIVACIDAD");
  y += 22;
  struct { const char* name; const char* sub; bool on; uint16_t id; } sw[3] = {
    { LI() == 1 ? "Hide body on lock screen" : "Ocultar el texto en el bloqueo",
      LI() == 1 ? "Only app and sender" : "Solo la app y el remitente",
      fphModel.priv.hideBodyOnLock, FPHH_PRIV_BODY },
    { LI() == 1 ? "Never show sensitive" : "No mostrar las sensibles",
      LI() == 1 ? "Codes, banking" : "Codigos, banca",
      fphModel.priv.hideSensitive, FPHH_PRIV_SENS },
    { LI() == 1 ? "Keep history" : "Conservar el historial",
      LI() == 1 ? "Survives a disconnect" : "Sobrevive a una desconexion",
      fphModel.priv.keepHistory, FPHH_PRIV_HIST },
  };
  for(int i = 0; i < 3; i++){
    fgCard(FPH_MX, y, FPH_CW, 58);
    fgTextEllipsis(FPH_MX + 18, y + 10, FPH_CW - 90, sw[i].name, 1, TH_TXT);
    fgTextEllipsis(FPH_MX + 18, y + 32, FPH_CW - 90, sw[i].sub, 1, TH_MUTE);
    // Interruptor: la posicion del punto dice el estado, no solo el
    // color de la pista.
    const int sx = FPH_MX + FPH_CW - 64, sy = y + 18;
    fillRoundRect(sx, sy, 46, 24, 12, sw[i].on ? TH_PRIM : TH_TRACK);
    fillCircle(sw[i].on ? sx + 34 : sx + 12, sy + 12, 9, sw[i].on ? TH_ONACC : TH_MUTE);
    fgHitAdd(FPH_MX, y, FPH_CW, 58, sw[i].id);
    y += 66;
  }

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  10) AJUSTES
// -------------------------------------------------------------
static void fphRenderAjustes(){
  fphHeader(fphSecName(FPH_AJUSTES), true);
  fgScrollSetView(&fphScr, FPH_TOP, SCR_H - 6);
  const int y0 = FPH_TOP - fphScr.off;
  int y = y0;

  // Reconectar solo al arrancar.
  fgCard(FPH_MX, y, FPH_CW, 58);
  fgTextEllipsis(FPH_MX + 18, y + 10, FPH_CW - 90,
                 LI() == 1 ? "Connect automatically" : "Conectar automaticamente", 1, TH_TXT);
  fgTextEllipsis(FPH_MX + 18, y + 32, FPH_CW - 90,
                 LI() == 1 ? "When Wi-Fi comes up, if a phone is paired"
                           : "Al haber Wi-Fi, si hay telefono vinculado", 1, TH_MUTE);
  {
    const int sx = FPH_MX + FPH_CW - 64, sy = y + 18;
    fillRoundRect(sx, sy, 46, 24, 12, fphAutoLink ? TH_PRIM : TH_TRACK);
    fillCircle(fphAutoLink ? sx + 34 : sx + 12, sy + 12, 9, fphAutoLink ? TH_ONACC : TH_MUTE);
    fgHitAdd(FPH_MX, y, FPH_CW, 58, FPHH_LINK_ON + 1000);   // id propio, ver fphTouch
  }
  y += 70;

  // Identidad y protocolo.
  fgCard(FPH_MX, y, FPH_CW, 108);
  {
    char v[24];
    snprintf(v, sizeof(v), "Flex Link v%u", FLNK_VERSION);
    fgRow(FPH_MX + 18, y + 16, FPH_CW - 36, LI() == 1 ? "Protocol" : "Protocolo", v);
    fgRow(FPH_MX + 18, y + 44, FPH_CW - 36, LI() == 1 ? "This device" : "Este dispositivo", fphSelfId);
    snprintf(v, sizeof(v), "%u", (unsigned)FLPW_TCP_PORT);
    fgRow(FPH_MX + 18, y + 72, FPH_CW - 36, LI() == 1 ? "Phone port" : "Puerto del telefono", v);
  }
  y += 120;

  // BLE: se dice lo que hay, sin esconderlo y sin prometerlo.
  fgSectionHeader(FPH_MX + 4, y, "BLUETOOTH LE");
  y += 22;
  y += fgNotice(FPH_MX, y, FPH_CW,
                flexPhoneBleReady() ? (LI() == 1 ? "Available" : "Disponible")
                                    : (LI() == 1 ? "Not used today" : "Hoy no se usa"),
                flexPhoneBleReason(),
                flexPhoneBleReady() ? TH_OK : TH_MUTE) + 12;

  // Borrado.
  fgSectionHeader(FPH_MX + 4, y, LI() == 1 ? "DATA" : "DATOS");
  y += 22;
  fgButton(FPH_MX, y, FPH_CW, 44,
           LI() == 1 ? "Delete Flex Phone data" : "Borrar los datos de Flex Phone",
           FG_BTN_DANGER, FPHH_WIPE);
  y += 54;
  drawTextC(SCR_W / 2, y, LI() == 1 ? "History, drafts and the pairing key"
                                    : "Historial, borradores y la clave del vinculo", 1, TH_MUTE);
  y += 30;

  fphScr.content = y - y0;
  fgScrollBar(&fphScr);
}

// -------------------------------------------------------------
//  Despacho de dibujo
// -------------------------------------------------------------
static void fphRender(){
  // Misma convencion que Flex Store: se dibuja sobre `fb` y se vuelca
  // con flxFlushAll(). present() es para las bandas parciales del
  // sistema; una pantalla de app se vuelca entera.
  setBuf(fb);
  fgHitsReset();
  switch(fphSection){
    case FPH_NOTIFS:    fphRenderNotifs();    break;
    case FPH_SERVIDOR:  fphRenderServidor();  break;
    case FPH_NAVEGADOR: fphRenderNavegador(); break;
    case FPH_ESTADO:    fphRenderEstado();    break;
    case FPH_CONEXION:  fphRenderConexion();  break;
    case FPH_SYNC:      fphRenderSync();      break;
    case FPH_DIAG:      fphRenderDiag();      break;
    case FPH_SEGURIDAD: fphRenderSeguridad(); break;
    case FPH_AJUSTES:   fphRenderAjustes();   break;
    default:            fphRenderInicio();    break;
  }
  if(fphToastUntil && (int32_t)(millis() - fphToastUntil) < 0){
    // El aviso se pinta ENCIMA de todo y no registra zona tactil: no
    // debe robar el toque de lo que hay debajo.
    const int h = 46;
    fillRoundRect(24, SCR_H - 118, SCR_W - 48, h, 14, TH_SCRIM);
    fgTextEllipsis(40, SCR_H - 118 + (h - 16) / 2, SCR_W - 80, fphToast, 1, TH_TXT);
  }
  flxFlushAll();
  fphDirtyUi = false;
}

// =============================================================
//  CICLO DE VIDA DE LA APP
// =============================================================
static void fphEnter(){
  // gRelayout: si solo se esta re-maquetando por un cambio de tamano,
  // NO se reinicia la seccion abierta ni el desplazamiento.
  if(!gRelayout){
    fphSection = FPH_INICIO;
    fgScrollReset(&fphScr, FPH_TOP, SCR_H - 6);
  }
  fphDrag = false;
  fphDirtyUi = true;
  fphRender();
}

static void fphExit(){
  fphSave(true);          // al salir SI se vuelca, aunque no toque por tiempo
  // El enlace NO se para al cerrar la app: las notificaciones tienen
  // que seguir llegando. Lo que se suelta es la pintura.
}

// Atras dentro de la app: primero vuelve a Inicio, y solo entonces
// cierra la app.
static bool fphBackScreen(){
  if(fphSection != FPH_INICIO){
    fphSection = FPH_INICIO;
    fgScrollReset(&fphScr, FPH_TOP, SCR_H - 6);
    fphDirtyUi = true;
    return true;
  }
  return false;
}

static void fphSuspend(){ fphSave(true); }
static void fphResume(){  fphDirtyUi = true; }

// -------------------------------------------------------------
//  Toques
// -------------------------------------------------------------
static void fphOpenSection(int s){
  if(s < 0 || s >= FPH_SEC_N || s == fphSection) return;
  fphSection = (uint8_t)s;
  fgScrollReset(&fphScr, FPH_TOP, SCR_H - 6);
  fphDirtyUi = true;
}

static void fphHandleHit(uint16_t id){
  const uint32_t now = millis();

  if(id >= FPHH_SEC_BASE && id < FPHH_SEC_BASE + FPH_SEC_N){
    fphOpenSection(id - FPHH_SEC_BASE);
    return;
  }
  if(id >= FPHH_NOTIF_BASE && id < FPHH_NOTIF_BASE + FLP_NOTIF_MAX){
    // Abrir la app asociada solo tiene sentido si el telefono la
    // ofrece. Mientras no haya una accion real detras, se descarta la
    // notificacion, que es lo que si se puede hacer de verdad.
    const int i = id - FPHH_NOTIF_BASE;
    if(i >= 0 && i < FLP_NOTIF_MAX && fphModel.notif[i].used){
      const uint32_t nid = fphModel.notif[i].id;
      uint8_t body[4];
      FlexLinkWr w; flexLinkWrInit(&w, body, sizeof(body));
      flexLinkWrU32(&w, nid);
      if(flexPhoneLinkReady(&fphLink) && flexLinkWrOk(&w))
        flexPhoneLinkSend(&fphLink, FLNK_T_NOTIF_REMOVE, body, w.at, false);
      flexPhoneNotifRemove(&fphModel, nid);
      flexPhoneConvRebuild(&fphModel);
      fphModel.dirty = true;
      fphDirtyUi = true;
    }
    return;
  }

  switch(id){
    case FPHH_BACK:
      if(!fphBackScreen()) appClose();
      return;

    case FPHH_LINK_ON:
      if(flexPhoneLinkStart(&fphLink)) fphToastShow(LI() == 1 ? "Link on" : "Enlace activado");
      else fphToastShow(fphLink.err[0] ? fphLink.err : flexPhoneTrStatus(fphLink.tr));
      fphDirtyUi = true;
      return;

    case FPHH_LINK_OFF:
      flexPhoneLinkStop(&fphLink);
      fphToastShow(LI() == 1 ? "Link off" : "Enlace apagado");
      fphDirtyUi = true;
      return;

    case FPHH_LINK_ON + 1000: {          // conmutador de Ajustes
      fphAutoLink = !fphAutoLink;
      Preferences p;
      if(p.begin(FPH_NVS_NS, false)){ p.putBool(FPH_NVS_AUTO, fphAutoLink); p.end(); }
      fphDirtyUi = true;
      return;
    }

    case FPHH_RETRY:
      // Un reintento limpia el error y vuelve a buscar. No se apila
      // un segundo intento sobre el anterior.
      flexPhoneLinkStop(&fphLink);
      if(flexPhoneLinkStart(&fphLink)) fphToastShow(LI() == 1 ? "Searching" : "Buscando");
      else fphToastShow(fphLink.err[0] ? fphLink.err : flexPhoneTrStatus(fphLink.tr));
      fphDirtyUi = true;
      return;

    case FPHH_PAIR:
      if(fphLink.state == FLP_LS_OFF && !flexPhoneLinkStart(&fphLink)){
        fphToastShow(fphLink.err[0] ? fphLink.err : flexPhoneTrStatus(fphLink.tr));
        return;
      }
      flexPhoneLinkBeginPairing(&fphLink, fphRandom, now);
      fphOpenSection(FPH_INICIO);
      fphToastShow(LI() == 1 ? "Open Flex Phone on the phone"
                             : "Abre Flex Phone en el telefono");
      fphDirtyUi = true;
      return;

    case FPHH_CONFIRM:
      flexPhoneLinkConfirm(&fphLink, now);
      if(flexPhoneLinkPairComplete(&fphLink)){
        fphBondSave();
        fphToastShow(LI() == 1 ? "Phone paired" : "Telefono emparejado");
      } else {
        fphToastShow(LI() == 1 ? "Now type the code on the phone"
                               : "Teclea ahora el codigo en el telefono");
      }
      fphDirtyUi = true;
      return;

    case FPHH_FORGET:
      flexPhoneLinkForget(&fphLink);
      fphBondSave();                              // borra la clave de NVS
      flexPhoneModelClear(&fphModel, true);       // revocar BORRA los datos
      fphSave(true);
      fphToastShow(LI() == 1 ? "Device revoked and data deleted"
                             : "Dispositivo revocado y datos borrados");
      fphOpenSection(FPH_INICIO);
      return;

    case FPHH_FIND:
      if(flexPhoneLinkSend(&fphLink, FLNK_T_FIND_START, NULL, 0, true))
        fphToastShow(LI() == 1 ? "Ringing on the phone" : "Sonando en el telefono");
      else
        fphToastShow(LI() == 1 ? "Could not send it" : "No se pudo enviar la orden");
      return;

    case FPHH_HOST_CLEAR:
      flexPhoneWifiSetHost(NULL, 0);
      fphToastShow(LI() == 1 ? "Searching on the network again"
                             : "Buscando otra vez en la red");
      fphDirtyUi = true;
      return;

    case FPHH_RELAY_TOGGLE: {
      const bool up = (fphModel.relay.state == FLP_RELAY_UP ||
                       fphModel.relay.state == FLP_RELAY_STARTING);
      if(up){
        if(flexPhoneLinkSend(&fphLink, FLNK_T_RELAY_STOP, NULL, 0, true)){
          fphModel.relay.state = FLP_RELAY_OFF;
          fphToastShow(LI() == 1 ? "Stopping" : "Deteniendo");
        } else fphToastShow(LI() == 1 ? "Could not send it" : "No se pudo enviar");
      } else {
        if(flexPhoneLinkSend(&fphLink, FLNK_T_RELAY_START, NULL, 0, true)){
          fphModel.relay.state = FLP_RELAY_STARTING;
          fphToastShow(LI() == 1 ? "Asking the phone" : "Pidiendolo al telefono");
        } else fphToastShow(LI() == 1 ? "Could not send it" : "No se pudo enviar");
      }
      fphDirtyUi = true;
      return;
    }

    case FPHH_SRC_AUTO:  flexBrowserSetSource(BRSRC_AUTO);   fphDirtyUi = true; return;
    case FPHH_SRC_PHONE: flexBrowserSetSource(BRSRC_PHONE);  fphDirtyUi = true; return;
    case FPHH_SRC_PC:    flexBrowserSetSource(BRSRC_MANUAL); fphDirtyUi = true; return;

    case FPHH_SYNC_NOW:
      // Se pide el estado entero. Es lo unico que se puede pedir de
      // verdad: el resto llega por eventos.
      if(flexPhoneLinkSend(&fphLink, FLNK_T_PHONE_STATE, NULL, 0, false))
        fphToastShow(LI() == 1 ? "Asked" : "Solicitado");
      else
        fphToastShow(LI() == 1 ? "Requires a connected phone"
                               : "Requiere el telefono conectado");
      return;

    case FPHH_CLEAR_NOTIFS:
      flexPhoneNotifClearAll(&fphModel);
      flexPhoneConvRebuild(&fphModel);
      fphModel.dirty = true;
      fphSave(true);
      fphToastShow(LI() == 1 ? "History cleared" : "Historial borrado");
      fphDirtyUi = true;
      return;

    case FPHH_PRIV_BODY:
      fphModel.priv.hideBodyOnLock = !fphModel.priv.hideBodyOnLock;
      fphModel.dirty = true; fphDirtyUi = true; return;
    case FPHH_PRIV_SENS:
      fphModel.priv.hideSensitive = !fphModel.priv.hideSensitive;
      fphModel.dirty = true; fphDirtyUi = true; return;
    case FPHH_PRIV_HIST:
      fphModel.priv.keepHistory = !fphModel.priv.keepHistory;
      fphModel.dirty = true; fphDirtyUi = true; return;

    case FPHH_WIPE:
      flexPhoneLinkForget(&fphLink);
      fphBondSave();
      flexPhoneModelClear(&fphModel, true);
      if(flexFsReady()) flexFsDelete(FPH_STORE_PATH);
      fphModel.dirty = false;
      fphToastShow(LI() == 1 ? "Flex Phone data deleted" : "Datos de Flex Phone borrados");
      fphOpenSection(FPH_INICIO);
      return;

    default: return;
  }
}

static void fphTouch(){
  // ---- Arrastre para desplazar ----
  // Se sigue el dedo 1:1. El toque solo cuenta como pulsacion si el
  // dedo NO se movio: si no, cada intento de desplazar acabaria
  // abriendo la tarjeta que hubiera debajo.
  if(T.pressed && !fphDrag){
    fphDrag = true; fphDragMoved = false;
    fphDragY0 = T.y; fphDragOff0 = fphScr.off;
  }
  if(fphDrag && T.pressed){
    const int dy = fphDragY0 - T.y;
    if(!fphDragMoved && abs(dy) > 8) fphDragMoved = true;
    if(fphDragMoved){
      fphScr.off = fphDragOff0 + dy;
      if(fgScrollClamp(&fphScr) || true) fphDirtyUi = true;
    }
    return;
  }
  if(fphDrag && !T.pressed){
    fphDrag = false;
    if(fphDragMoved){ fphDragMoved = false; return; }   // fue un desplazamiento
  }

  if(!T.tap) return;
  const uint16_t id = fgHitAt(T.x, T.y);
  if(id) fphHandleHit(id);
}

// Tick de la app: toques + repintado SOLO cuando algo cambio.
static void fphTick(){
  fphTouch();
  // Repintado por cambio real, no por frame: con la app abierta y sin
  // novedades no se redibuja nada. Es lo que permite que Flex Phone
  // abierto no cueste FPS.
  static uint32_t lastSync = 0, lastState = 0;
  const uint32_t syncNow = fphModel.lastSyncMs;
  const uint32_t stNow = (uint32_t)fphLink.state
                       | ((uint32_t)fphModel.notifCount << 8)
                       | ((uint32_t)fphModel.relay.state << 16)
                       | ((uint32_t)(fphLink.rttMs & 0xFF) << 20)
                       | ((uint32_t)flexPhoneTrState(fphLink.tr) << 28);
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
