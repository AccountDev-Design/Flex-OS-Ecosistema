// #############################################################
// ##  FLEX PHONE LINK -- implementacion del transporte
// ##  Ver FlexOS_FlexPhone_Link.h para el contrato y, sobre todo,
// ##  para la explicacion de por que en el P4 esto puede quedarse
// ##  en FLP_LINK_CAP_NONE sin que eso sea un fallo.
// #############################################################
#include "FlexOS_FlexPhone_Link.h"

// =============================================================
//  DETECCION DE CAPACIDAD -- se le pregunta al SDK, no se supone
// =============================================================
// Mismo criterio que ya usaba el .ino para el interruptor de BLE de
// Ajustes: SOC_BLE_SUPPORTED solo existe en los chips con radio
// Bluetooth propia. En el ESP32-P4 NO esta definida.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  #if __has_include("soc/soc_caps.h")
    #include "soc/soc_caps.h"
  #endif
#endif

#if defined(SOC_BLE_SUPPORTED) && SOC_BLE_SUPPORTED
  #define FLP_HAS_LOCAL_BLE 1
#else
  #define FLP_HAS_LOCAL_BLE 0
#endif

// BLE a traves del C6. Hacen falta LAS DOS cosas:
//   - una pila de host BLE en el P4 (NimBLE),
//   - y que ese host hable con el controlador REMOTO del C6.
// La segunda no se puede comprobar en compilacion de forma fiable,
// asi que se exige un interruptor explicito: quien haya flasheado el
// firmware slave del C6 con Bluetooth compila con
// -DFLEXOS_C6_BLE_HCI=1. Sin ese interruptor NO se afirma que el
// enlace exista. Es deliberado: preferimos decir "no disponible" a
// anunciar un BLE que se queda colgado en la placa del usuario.
#if !defined(FLEXOS_C6_BLE_HCI)
  #define FLEXOS_C6_BLE_HCI 0
#endif
#if FLEXOS_C6_BLE_HCI && __has_include(<NimBLEDevice.h>)
  #define FLP_HAS_HOSTED_BLE 1
#else
  #define FLP_HAS_HOSTED_BLE 0
#endif

uint8_t flexPhoneLinkCap(void){
#if FLP_HAS_LOCAL_BLE
  return FLP_LINK_CAP_LOCAL;
#elif FLP_HAS_HOSTED_BLE
  return FLP_LINK_CAP_HOSTED;
#else
  return FLP_LINK_CAP_NONE;
#endif
}

const char* flexPhoneLinkCapReason(void){
#if FLP_HAS_LOCAL_BLE
  return "BLE disponible en el chip";
#elif FLP_HAS_HOSTED_BLE
  return "BLE a traves del co-procesador C6";
#else
  // Este texto se enseria TAL CUAL en la app. Dice el hecho y dice
  // que falta, sin prometer nada.
  return "Este chip no tiene radio Bluetooth. En la placa P4 el BLE "
         "tendria que venir del co-procesador C6, y para eso hace falta "
         "firmware esp-hosted con Bluetooth en el C6 y compilar con "
         "FLEXOS_C6_BLE_HCI=1. Ver docs/FLEX-PHONE.md.";
#endif
}

const char* flexPhoneLinkStateName(uint8_t st){
  switch(st){
    case FLP_LS_UNAVAILABLE: return "no disponible";
    case FLP_LS_OFF:         return "apagado";
    case FLP_LS_ADVERTISING: return "buscando telefono";
    case FLP_LS_CONNECTING:  return "conectando";
    case FLP_LS_PAIRING:     return "emparejando";
    case FLP_LS_READY:       return "conectado";
    default:                 return "error";
  }
}

// -------------------------------------------------------------
//  Utilidades internas
// -------------------------------------------------------------
static void setErr(FlexPhoneLink* L, const char* msg){
  if(!L) return;
  flexLinkUtf8Copy(L->err, sizeof(L->err), msg ? msg : "");
}

static void gotoState(FlexPhoneLink* L, uint8_t st, uint32_t nowMs){
  if(!L || L->state == st) return;
  L->state = st;
  L->stateSinceMs = nowMs;
}

// =============================================================
//  Ciclo de vida
// =============================================================
void flexPhoneLinkInit(FlexPhoneLink* L){
  if(!L) return;
  memset(L, 0, sizeof(*L));
  L->cap = flexPhoneLinkCap();
  // MTU de partida: el minimo que garantiza BLE (23 - 3 de ATT).
  // Solo sube cuando el telefono NEGOCIA uno mayor de verdad.
  L->mtu = 20;
  flexLinkReasmInit(&L->reasm);
  flexLinkAntiReplayInit(&L->anti);
  if(L->cap == FLP_LINK_CAP_NONE){
    L->state = FLP_LS_UNAVAILABLE;
    setErr(L, flexPhoneLinkCapReason());
  } else {
    L->state = FLP_LS_OFF;
  }
}

bool flexPhoneLinkStart(FlexPhoneLink* L){
  if(!L) return false;
  if(L->cap == FLP_LINK_CAP_NONE){
    // No se intenta nada ni se deja el estado a medias: se dice que
    // no y por que. La app pinta exactamente esto.
    L->state = FLP_LS_UNAVAILABLE;
    setErr(L, flexPhoneLinkCapReason());
    return false;
  }
  if(L->state == FLP_LS_READY || L->state == FLP_LS_ADVERTISING) return true;
  L->err[0] = 0;
  L->session = 0;
  L->txCounter = 0;
  L->reconnectAttempt = 0;
  flexLinkReasmInit(&L->reasm);
  flexLinkAntiReplayInit(&L->anti);
  memset(L->tx, 0, sizeof(L->tx));
  L->state = FLP_LS_ADVERTISING;
  L->stateSinceMs = 0;
  // El arranque REAL del anunciado GATT vive en el puente del .ino
  // (FlexOS_FlexPhone_Bridge.h), que es quien tiene NimBLE delante.
  // Aqui solo se fija el estado logico.
  return true;
}

void flexPhoneLinkStop(FlexPhoneLink* L){
  if(!L) return;
  if(L->state == FLP_LS_READY){
    // Desconexion LIMPIA: el telefono se entera de que nos vamos en
    // vez de quedarse esperando un tiempo de espera.
    flexPhoneLinkSend(L, FLNK_T_BYE, NULL, 0, false);
  }
  memset(L->tx, 0, sizeof(L->tx));
  flexLinkReasmInit(&L->reasm);
  L->session = 0;
  L->userConfirmed = false;
  L->peerConfirmed = false;
  L->code[0] = 0;
  L->state = (L->cap == FLP_LINK_CAP_NONE) ? FLP_LS_UNAVAILABLE : FLP_LS_OFF;
}

void flexPhoneLinkForget(FlexPhoneLink* L){
  if(!L) return;
  flexPhoneLinkStop(L);
  L->bonded = false;
  // Las claves de bonding viven en la pila BLE; borrarlas de verdad
  // es cosa del puente (NimBLEDevice::deleteAllBonds). Aqui se deja
  // el estado logico coherente para que la interfaz no muestre un
  // dispositivo vinculado que ya no lo esta.
  memset(&L->anti, 0, sizeof(L->anti));
  L->txCounter = 0;
}

// =============================================================
//  Envio
// =============================================================
bool flexPhoneLinkSend(FlexPhoneLink* L, uint8_t type,
                       const uint8_t* payload, size_t len, bool needsAck){
  if(!L) return false;
  // BYE se permite en cualquier estado vivo; el resto exige sesion.
  if(L->state != FLP_LS_READY && type != FLNK_T_BYE &&
     type != FLNK_T_HELLO && type != FLNK_T_WELCOME &&
     type != FLNK_T_PAIR_CODE && type != FLNK_T_PAIR_CONFIRM) return false;
  if(len > FLP_LINK_TXBUF){ L->nDropped++; return false; }
  if(len && !payload)     { L->nDropped++; return false; }

  int slot = -1;
  for(int i = 0; i < FLP_LINK_TXQ; i++) if(!L->tx[i].used){ slot = i; break; }
  if(slot < 0){
    // Cola llena. NO se bloquea esperando hueco: se descarta y se
    // cuenta. Bloquear aqui congelaria la interfaz.
    L->nDropped++;
    return false;
  }
  FlexPhoneTxMsg* m = &L->tx[slot];
  memset(m, 0, sizeof(*m));
  m->type = type;
  m->len  = (uint16_t)len;
  if(len) memcpy(m->data, payload, len);
  m->needsAck = needsAck;
  m->used = true;
  m->nextTryMs = 0;      // 0 = sale en el proximo tick
  return true;
}

// =============================================================
//  Recepcion
// =============================================================
// Aplica un mensaje YA reensamblado al modelo. Devuelve true si se
// entendio. Nada de lo que entra aqui se da por bueno sin decodificar.
static bool applyMessage(FlexPhoneLink* L, FlexPhoneModel* M,
                         uint8_t type, const uint8_t* p, size_t n, uint32_t nowMs){
  if(!L || !M) return false;
  switch(type){
    case FLNK_T_HELLO: {
      // El telefono se presenta. La sesion NO se da por abierta aqui:
      // primero tiene que completarse el emparejamiento.
      if(L->state == FLP_LS_ADVERTISING || L->state == FLP_LS_CONNECTING)
        gotoState(L, L->bonded ? FLP_LS_READY : FLP_LS_PAIRING, nowMs);
      if(L->state == FLP_LS_READY) L->session = (uint16_t)(nowMs | 1);
      return true;
    }
    case FLNK_T_PAIR_CONFIRM:
      L->peerConfirmed = true;
      if(flexPhoneLinkPairComplete(L)){
        L->bonded = true;
        L->session = (uint16_t)(nowMs | 1);
        L->code[0] = 0;
        gotoState(L, FLP_LS_READY, nowMs);
      }
      return true;

    case FLNK_T_BYE:
      // El telefono se despide: se vuelve a anunciar, no a "error".
      L->session = 0;
      gotoState(L, FLP_LS_ADVERTISING, nowMs);
      flexPhoneModelClear(M, false);      // el estado instantaneo deja de ser cierto
      return true;

    case FLNK_T_PING:
      flexPhoneLinkSend(L, FLNK_T_PONG, NULL, 0, false);
      return true;
    case FLNK_T_PONG:
      return true;

    case FLNK_T_NOTIF_ADD:
    case FLNK_T_NOTIF_UPDATE: {
      FlexPhoneNotif nt;
      if(!flexPhoneDecNotif(p, n, &nt)){ L->nBad++; M->stats.badPacket++; return false; }
      if(flexPhoneNotifPut(M, &nt, nowMs) < 0) return false;
      flexPhoneConvRebuild(M);
      M->lastSyncMs = nowMs;
      return true;
    }
    case FLNK_T_NOTIF_REMOVE: {
      FlexLinkRd r; flexLinkRdInit(&r, p, n);
      const uint32_t id = flexLinkRdU32(&r);
      if(!flexLinkRdOk(&r)){ L->nBad++; M->stats.badPacket++; return false; }
      flexPhoneNotifRemove(M, id);
      flexPhoneConvRebuild(M);
      return true;
    }
    case FLNK_T_NOTIF_CLEAR:
      flexPhoneNotifClearAll(M);
      flexPhoneConvRebuild(M);
      return true;

    case FLNK_T_REPLY_RESULT: {
      uint32_t id = 0; uint8_t ec = 0;
      if(!flexPhoneDecReplyResult(p, n, &id, &ec)){ L->nBad++; return false; }
      // Aqui se registra el resultado REAL. Un exito solo se cuenta
      // cuando Android dijo que acepto la accion.
      if(ec == FLNK_E_NONE) M->stats.repliesOk++;
      else                  M->stats.repliesFail++;
      return true;
    }

    case FLNK_T_PHONE_STATE: {
      FlexPhoneState st;
      if(!flexPhoneDecPhoneState(p, n, &st)){ L->nBad++; M->stats.badPacket++; return false; }
      st.stampMs = nowMs;
      M->phone = st;
      M->lastSyncMs = nowMs;
      return true;
    }
    case FLNK_T_MEDIA_STATE: {
      FlexPhoneMedia md;
      if(!flexPhoneDecMedia(p, n, &md)){ L->nBad++; M->stats.badPacket++; return false; }
      md.stampMs = nowMs;
      M->media = md;
      return true;
    }
    case FLNK_T_RELAY_INFO: {
      FlexPhoneRelay rl;
      if(!flexPhoneDecRelayInfo(p, n, &rl)){ L->nBad++; M->stats.badPacket++; return false; }
      rl.stampMs = nowMs;
      M->relay = rl;
      return true;
    }
    case FLNK_T_ACK:
      return true;
    case FLNK_T_ERR: {
      FlexLinkRd r; flexLinkRdInit(&r, p, n);
      const uint8_t code = flexLinkRdU8(&r);
      if(flexLinkRdOk(&r)) setErr(L, flexLinkErrName(code));
      return true;
    }
    default:
      // Tipo desconocido: puede ser una version futura del telefono.
      // No es un fallo -- se ignora en silencio, que es justo lo que
      // permite anadir mensajes sin romper este firmware.
      return true;
  }
}

bool flexPhoneLinkOnFrame(FlexPhoneLink* L, FlexPhoneModel* M,
                          const uint8_t* frame, size_t n, uint32_t nowMs){
  if(!L || !M || !frame) return false;

  // -- El enlace tiene que estar VIVO para aceptar nada --
  // Apagado, no disponible o en error: no se procesa ni una trama.
  // Sin esto, un dispositivo cercano podria seguir inyectando
  // mensajes despues de desvincular.
  if(L->state == FLP_LS_UNAVAILABLE || L->state == FLP_LS_OFF ||
     L->state == FLP_LS_ERROR){
    L->nDropped++;
    return false;
  }

  FlexLinkHeader h; const uint8_t* pl = NULL; size_t pn = 0;
  const int rc = flexLinkReadFrame(frame, n, &h, &pl, &pn);
  if(rc != FLNK_OK){
    L->nBad++;
    M->stats.badPacket++;
    if(rc == FLNK_ERR_VERSION){
      // Version incompatible: se le contesta para que el telefono lo
      // sepa, en vez de dejarlo reintentando a ciegas.
      uint8_t body[1] = { FLNK_E_VERSION };
      flexPhoneLinkSend(L, FLNK_T_ERR, body, 1, false);
    }
    return false;
  }

  // Anti-repeticion ANTES de tocar el reensamblador: un paquete
  // repetido no puede alterar un mensaje a medias.
  if(!flexLinkAntiReplayCheck(&L->anti, h.counter)){
    L->nDropped++;
    return false;
  }

  // -- RECHAZO DE DISPOSITIVOS NO EMPAREJADOS --
  // Mientras no haya sesion abierta, lo UNICO que se acepta son los
  // mensajes del propio apreton de manos. Una notificacion o una
  // orden multimedia de alguien que no ha emparejado se descarta,
  // aunque la trama sea perfectamente valida.
  const bool handshake = (h.type == FLNK_T_HELLO) || (h.type == FLNK_T_PAIR_REQ) ||
                         (h.type == FLNK_T_PAIR_CONFIRM) || (h.type == FLNK_T_BYE);
  if(L->state != FLP_LS_READY && !handshake){
    L->nDropped++;
    return false;
  }
  // Con sesion abierta, solo se acepta la sesion en curso.
  if(L->session && h.session != L->session && !handshake){
    L->nDropped++;
    return false;
  }

  L->lastRxMs = nowMs;
  L->nRx++;

  const int rr = flexLinkReasmFeed(&L->reasm, &h, pl, pn, nowMs);
  if(rr == FLNK_R_DROP){ L->nDropped++; return false; }
  if(rr == FLNK_R_NEED_MORE) return true;      // aceptada, faltan trozos

  return applyMessage(L, M, h.type, L->reasm.buf, L->reasm.len, nowMs);
}

// =============================================================
//  Emparejamiento
// =============================================================
void flexPhoneLinkBeginPairing(FlexPhoneLink* L, uint32_t rnd, uint32_t nowMs){
  if(!L || L->cap == FLP_LINK_CAP_NONE) return;
  // Codigo de 6 digitos derivado de la aleatoriedad del llamador
  // (en la placa, esp_random(), que es el generador por hardware).
  uint32_t v = rnd % 1000000u;
  for(int i = FLP_LINK_CODE_LEN - 1; i >= 0; i--){
    L->code[i] = (char)('0' + (v % 10));
    v /= 10;
  }
  L->code[FLP_LINK_CODE_LEN] = 0;
  L->userConfirmed = false;
  L->peerConfirmed = false;
  gotoState(L, FLP_LS_PAIRING, nowMs);
}

void flexPhoneLinkConfirm(FlexPhoneLink* L, uint32_t nowMs){
  if(!L || L->state != FLP_LS_PAIRING) return;
  L->userConfirmed = true;
  flexPhoneLinkSend(L, FLNK_T_PAIR_CONFIRM, NULL, 0, false);
  if(flexPhoneLinkPairComplete(L)){
    L->bonded = true;
    L->session = (uint16_t)(nowMs | 1);
    L->code[0] = 0;
    gotoState(L, FLP_LS_READY, nowMs);
  }
}

bool flexPhoneLinkPairComplete(const FlexPhoneLink* L){
  // LOS DOS lados. Confirmar solo en Flex OS no empareja nada: es la
  // regla que impide que un dispositivo cercano se vincule solo.
  return L && L->userConfirmed && L->peerConfirmed;
}

// =============================================================
//  Tick
// =============================================================
void flexPhoneLinkTick(FlexPhoneLink* L, FlexPhoneModel* M, uint32_t nowMs){
  if(!L || !M) return;
  if(L->state == FLP_LS_UNAVAILABLE || L->state == FLP_LS_OFF) return;

  // 1) Parciales caducados: un mensaje que nunca se completo no
  //    puede quedarse ocupando el reensamblador para siempre.
  if(flexLinkReasmExpire(&L->reasm, nowMs, FLP_LINK_REASM_TIMEOUT_MS)){
    L->nTimeouts++;
    M->stats.badPacket++;
  }

  // 2) El codigo de emparejamiento caduca. Si nadie confirma, se
  //    vuelve a anunciar en vez de dejar el codigo en pantalla.
  if(L->state == FLP_LS_PAIRING && L->stateSinceMs &&
     (uint32_t)(nowMs - L->stateSinceMs) > FLP_LINK_PAIR_WINDOW_MS){
    L->code[0] = 0;
    L->userConfirmed = L->peerConfirmed = false;
    setErr(L, "el emparejamiento caduco; vuelve a intentarlo");
    gotoState(L, FLP_LS_ADVERTISING, nowMs);
  }

  // 3) Enlace muerto: hace mucho que no llega NADA estando en sesion.
  if(L->state == FLP_LS_READY && L->lastRxMs &&
     (uint32_t)(nowMs - L->lastRxMs) > FLP_LINK_DEAD_MS){
    L->session = 0;
    L->nReconnects++;
    M->stats.reconnects++;
    // El estado del telefono deja de ser cierto en cuanto se cae el
    // enlace: se limpia para no pintar una bateria de hace un rato
    // como si fuera de ahora.
    flexPhoneModelClear(M, false);
    setErr(L, "el telefono dejo de responder");
    L->reconnectAttempt = 0;
    gotoState(L, FLP_LS_ADVERTISING, nowMs);
  }

  // 4) Reconexion con espera progresiva y LIMITE. Al agotarse los
  //    intentos no se reintenta en bucle: se para y se dice.
  if(L->state == FLP_LS_ADVERTISING && L->reconnectAttempt){
    if(nowMs >= L->reconnectAtMs){
      const uint32_t d = flexLinkRetryDelayMs(L->reconnectAttempt);
      if(d == 0){
        setErr(L, "no se pudo reconectar con el telefono");
        gotoState(L, FLP_LS_ERROR, nowMs);
      } else {
        L->reconnectAttempt++;
        L->reconnectAtMs = nowMs + d;
      }
    }
  }

  // 5) Cola de salida. Solo se ENTREGA aqui lo que ya toca; el envio
  //    fisico lo hace el puente. Nada de esto espera respuesta.
  for(int i = 0; i < FLP_LINK_TXQ; i++){
    FlexPhoneTxMsg* m = &L->tx[i];
    if(!m->used) continue;
    if(m->nextTryMs && nowMs < m->nextTryMs) continue;
    if(!m->needsAck){
      // Sin confirmacion: sale una vez y se olvida.
      m->used = false;
      L->nTx++;
      L->lastTxMs = nowMs;
      continue;
    }
    if(m->attempts >= FLNK_RETRY_MAX){
      m->used = false;
      L->nTimeouts++;
      continue;
    }
    const uint32_t d = flexLinkRetryDelayMs(m->attempts);
    m->attempts++;
    m->nextTryMs = nowMs + (d ? d : FLP_LINK_ACK_TIMEOUT_MS);
    L->nTx++;
    L->lastTxMs = nowMs;
  }

  // 6) Latido: mantiene viva la sesion sin sondear agresivamente.
  if(L->state == FLP_LS_READY && L->lastTxMs &&
     (uint32_t)(nowMs - L->lastTxMs) > FLP_LINK_IDLE_PING_MS){
    flexPhoneLinkSend(L, FLNK_T_PING, NULL, 0, false);
  }

  // 7) Caducidad por privacidad. No se llama en cada frame: solo
  //    cuando el usuario configuro un plazo.
  if(M->priv.keepHours){
    const uint32_t maxAge = (uint32_t)M->priv.keepHours * 3600000u;
    flexPhoneNotifExpire(M, nowMs, maxAge);
  }
}
