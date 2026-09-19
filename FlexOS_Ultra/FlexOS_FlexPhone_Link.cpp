// #############################################################
// ##  FLEX PHONE LINK -- implementacion
// ##  Ver FlexOS_FlexPhone_Link.h para el contrato completo y,
// ##  sobre todo, para el limite real de la seguridad que da.
// #############################################################
#include "FlexOS_FlexPhone_Link.h"
// Para las lineas de diagnostico del emparejamiento. Solo se usan
// cuando hay un gancho instalado (ver flexPhoneLinkSetLog).
#include <stdarg.h>
#include <stdio.h>

// =============================================================
//  DETECCION DE CAPACIDAD -- se le pregunta al SDK, no se supone
// =============================================================
// SOC_BLE_SUPPORTED solo existe en los chips con radio Bluetooth
// propia. En el ESP32-P4 NO esta definida. Es la misma comprobacion
// que ya usaba el .ino para el interruptor de BLE de Ajustes.
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
// asi que se exige un interruptor explicito. Sin el NO se afirma que
// el enlace BLE exista: preferimos decir "no disponible" a anunciar
// un BLE que se queda colgado en la placa del usuario.
#if !defined(FLEXOS_C6_BLE_HCI)
  #define FLEXOS_C6_BLE_HCI 0
#endif
#if FLEXOS_C6_BLE_HCI && __has_include(<NimBLEDevice.h>)
  #define FLP_HAS_HOSTED_BLE 1
#else
  #define FLP_HAS_HOSTED_BLE 0
#endif

// Wi-Fi. En las tres placas de Flex OS la radio existe (en el P4 a
// traves del C6, que es quien ya da el Wi-Fi de todo el sistema).
// En las pruebas de host tambien se da por buena: ahi el transporte
// es el de lazo, y lo que se prueba es la maquina de estados.
#if !defined(FLEXOS_ENABLE_WIFI)
  #define FLP_HAS_WIFI 1
#else
  #define FLP_HAS_WIFI (FLEXOS_ENABLE_WIFI ? 1 : 0)
#endif

bool flexPhoneBleReady(void){
  return (FLP_HAS_LOCAL_BLE || FLP_HAS_HOSTED_BLE) ? true : false;
}

const char* flexPhoneBleReason(void){
#if FLP_HAS_LOCAL_BLE
  return "BLE disponible en el chip";
#elif FLP_HAS_HOSTED_BLE
  return "BLE a traves del co-procesador C6";
#else
  // Este texto se ensena TAL CUAL en la app. Dice el hecho y dice
  // que falta, sin prometer nada.
  return "Este chip no tiene radio Bluetooth. En la placa P4 el BLE "
         "tendria que venir del co-procesador C6, y para eso hace falta "
         "firmware esp-hosted con Bluetooth en el C6 y compilar con "
         "FLEXOS_C6_BLE_HCI=1. Flex Phone funciona por Wi-Fi; BLE queda "
         "como transporte futuro. Ver docs/FLEX-PHONE.md.";
#endif
}

uint8_t flexPhoneLinkCap(void){
  // Wi-Fi PRIMERO: es el transporte que de verdad se usa hoy. Que
  // una placa tenga ademas radio BLE no cambia por donde va Flex
  // Phone; cambiaria el dia que exista el transporte BLE.
  if(FLP_HAS_WIFI) return FLP_LINK_CAP_WIFI;
#if FLP_HAS_LOCAL_BLE
  return FLP_LINK_CAP_BLE_LOCAL;
#elif FLP_HAS_HOSTED_BLE
  return FLP_LINK_CAP_BLE_HOSTED;
#else
  return FLP_LINK_CAP_NONE;
#endif
}

const char* flexPhoneLinkCapReason(void){
  switch(flexPhoneLinkCap()){
    case FLP_LINK_CAP_WIFI:
      return "Enlace por Wi-Fi en la red local";
    case FLP_LINK_CAP_BLE_LOCAL:
      return "Enlace por BLE del propio chip";
    case FLP_LINK_CAP_BLE_HOSTED:
      return "Enlace por BLE a traves del co-procesador C6";
    default:
      return "Esta placa no tiene ningun transporte disponible para Flex Phone. "
             "Hace falta Wi-Fi activo.";
  }
}

const char* flexPhoneLinkStateName(uint8_t st){
  switch(st){
    case FLP_LS_UNAVAILABLE: return "no disponible";
    case FLP_LS_OFF:         return "apagado";
    case FLP_LS_SEARCHING:   return "buscando telefono";
    case FLP_LS_CONNECTING:  return "conectando";
    case FLP_LS_PAIRING:     return "emparejando";
    case FLP_LS_AUTH:        return "autenticando";
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
  L->stateSinceMs = nowMs ? nowMs : 1;   // 0 significa "sin marca"
}

// =============================================================
//  Diagnostico
// =============================================================
// Ver la cabecera: el codigo NUNCA sale entero. Solo los dos
// primeros digitos, que bastan para comparar dos extremos y no para
// adivinar nada.
static FlexPhoneLogFn gLog = NULL;

void flexPhoneLinkSetLog(FlexPhoneLogFn fn){ gLog = fn; }

static void maskCode(const char* code, char out[FLP_LINK_CODE_LEN + 1]){
  size_t i = 0;
  for(; i < 2 && code && code[i]; i++) out[i] = code[i];
  for(; i < FLP_LINK_CODE_LEN; i++) out[i] = '-';
  out[FLP_LINK_CODE_LEN] = 0;
}

// Formatea SOLO si hay alguien escuchando. Sin gancho instalado esto
// es una comparacion contra NULL y se acabo.
//
// NO se llama `logf`: ese nombre es el logaritmo de <math.h>, y
// Arduino.h arrastra math.h. Un choque asi no se ve en las pruebas de
// host (que no incluyen math.h) y rompe la compilacion en la placa.
static void pairLog(const char* fmt, ...){
  if(!gLog) return;
  char line[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  gLog(line);
}

// =============================================================
//  La sesion de emparejamiento
// =============================================================
// Cerrar es BORRAR: el codigo, la sal, el reto y la clave a medio
// derivar dejan de existir a la vez. Dejar cualquiera de los cuatro
// vivo es lo que permitia que un intento viejo se colara en el
// siguiente.
static void pairClear(FlexPhonePairing* P, uint8_t endState){
  if(!P) return;
  memset(P, 0, sizeof(*P));
  P->state = endState;
}

// ¿Sigue viva? El plazo cuelga de `startedMs`, NO del estado del
// enlace: un corte de canal no le quita ni le regala tiempo.
static bool pairAlive(const FlexPhonePairing* P, uint32_t nowMs){
  if(!P || P->state != FLP_PAIR_OPEN || !P->code[0]) return false;
  if(!P->startedMs) return true;                 // sin marca: no caduca sola
  return (uint32_t)(nowMs - P->startedMs) <= FLP_LINK_PAIR_WINDOW_MS;
}

// LOS DOS lados. Confirmar solo en Flex OS no empareja nada, y una
// prueba valida sin confirmacion del usuario tampoco: hace falta que
// alguien delante del reloj diga que si.
static bool pairBothConfirmed(const FlexPhonePairing* P){
  return P && P->state == FLP_PAIR_OPEN &&
         P->userConfirmed && P->peerConfirmed && P->keyOk;
}

// Deriva la clave de ESTA sesion contra el telefono `peerId`.
//
// Se vuelve a llamar en cada WELCOME a proposito: el telefono puede
// reconectar (y lo hace) y lo unico que cambia entre un canal y el
// siguiente es quien esta al otro lado. El CODIGO y la SAL son los
// de la sesion y no se tocan aqui -- por eso el telefono puede seguir
// usando lo que ya tenia apuntado.
static void pairDeriveFor(FlexPhoneLink* L, const char* peerId){
  if(!L || !peerId || !peerId[0]) return;
  FlexPhonePairing* P = &L->pair;
  if(P->state != FLP_PAIR_OPEN || !P->code[0]) return;
  flexAuthDeriveKey(P->code, P->salt, L->selfId, peerId, P->key);
  flexLinkUtf8Copy(P->peerId, sizeof(P->peerId), peerId);
  P->keyOk = true;
}

// Manda la sal y el reto DE LA SESION. Nunca el codigo.
static bool pairSendSalt(FlexPhoneLink* L){
  if(!L || L->pair.state != FLP_PAIR_OPEN) return false;
  uint8_t body[FLXA_SALT_SIZE + FLXA_NONCE_SIZE + 1 + FLXA_ID_MAX];
  FlexLinkWr w; flexLinkWrInit(&w, body, sizeof(body));
  flexLinkWrBytes(&w, L->pair.salt,  FLXA_SALT_SIZE);
  flexLinkWrBytes(&w, L->pair.nonce, FLXA_NONCE_SIZE);
  flexLinkWrStr(&w, L->selfId, FLXA_ID_MAX - 1);
  if(!flexLinkWrOk(&w)) return false;
  return flexPhoneLinkSend(L, FLNK_T_PAIR_CODE, body, w.at, false);
}

// Arranca una sesion nueva: contadores, reensamblador y anti-repeticion
// desde cero. Se llama en CADA apertura, no solo en la primera: reusar
// la ventana anti-repeticion de la sesion anterior descartaria las
// primeras tramas de la nueva por "demasiado antiguas".
static void sessionReset(FlexPhoneLink* L){
  if(!L) return;
  L->txCounter = 0;
  L->txPacket  = 1;
  flexLinkReasmInit(&L->reasm);
  flexLinkAntiReplayInit(&L->anti);
  memset(L->tx, 0, sizeof(L->tx));
  L->pingSentMs = 0;
  L->rttMs = 0xFFFF;
  L->hostProven = false;
}

// Arrancar de cero el enlace: ademas de la sesion, se olvida que nos
// hubieramos presentado. Si no, tras un Stop/Start el canal podria
// seguir abierto y no se volveria a saludar.
static void linkResetAll(FlexPhoneLink* L){
  if(!L) return;
  sessionReset(L);
  L->helloSent = false;
}

// =============================================================
//  Ciclo de vida
// =============================================================
void flexPhoneLinkInit(FlexPhoneLink* L){
  if(!L) return;
  memset(L, 0, sizeof(*L));
  L->cap = flexPhoneLinkCap();
  L->tr  = flexPhoneTransportNull();
  L->rttMs = 0xFFFF;
  // MTU de partida. Solo sube cuando un transporte REAL dice la suya.
  L->mtu = FLNK_MIN_MTU;
  sessionReset(L);
  if(L->cap == FLP_LINK_CAP_NONE){
    L->state = FLP_LS_UNAVAILABLE;
    setErr(L, flexPhoneLinkCapReason());
  } else {
    L->state = FLP_LS_OFF;
  }
}

void flexPhoneLinkSetRandom(FlexPhoneLink* L, FlexAuthRandFn rnd){
  if(L) L->rnd = rnd;
}

void flexPhoneLinkSetIdentity(FlexPhoneLink* L, const char* selfId){
  if(!L) return;
  flexLinkUtf8Copy(L->selfId, sizeof(L->selfId), selfId ? selfId : "");
}

void flexPhoneLinkSetTransport(FlexPhoneLink* L, FlexPhoneTransport* tr){
  if(!L) return;
  if(L->tr && L->tr != tr) flexPhoneTrStop(L->tr);
  L->tr = tr ? tr : flexPhoneTransportNull();
  const uint16_t m = flexPhoneTrMtu(L->tr);
  L->mtu = m ? m : (uint16_t)FLNK_MIN_MTU;
}

void flexPhoneLinkSetBond(FlexPhoneLink* L, const FlexPhoneBond* b){
  if(!L) return;
  if(!b){ memset(&L->bond, 0, sizeof(L->bond)); return; }
  L->bond = *b;
}

bool flexPhoneLinkStart(FlexPhoneLink* L){
  if(!L) return false;
  L->cap = flexPhoneLinkCap();
  if(L->cap == FLP_LINK_CAP_NONE){
    // No se intenta nada ni se deja el estado a medias: se dice que
    // no y por que. La app pinta exactamente esto.
    L->state = FLP_LS_UNAVAILABLE;
    setErr(L, flexPhoneLinkCapReason());
    return false;
  }
  // El enlace ya esta vivo: encenderlo otra vez no puede tirar lo que
  // hay en marcha. EMPAREJANDO entra en la lista a proposito -- un
  // segundo toque en "Activar el enlace" no puede borrar el codigo que
  // el usuario tiene delante.
  if(L->state == FLP_LS_READY || L->state == FLP_LS_SEARCHING ||
     L->state == FLP_LS_CONNECTING || L->state == FLP_LS_AUTH ||
     L->state == FLP_LS_PAIRING) return true;
  L->err[0] = 0;
  L->session = 0;
  L->reconnectAttempt = 0;
  L->reconnectAtMs = 0;
  linkResetAll(L);
  if(!flexPhoneTrStart(L->tr)){
    setErr(L, flexPhoneTrStatus(L->tr));
    L->state = FLP_LS_ERROR;
    return false;
  }
  L->state = FLP_LS_SEARCHING;
  L->stateSinceMs = 0;
  return true;
}

void flexPhoneLinkStop(FlexPhoneLink* L){
  if(!L) return;
  if(L->state == FLP_LS_READY){
    // Desconexion LIMPIA: el telefono se entera de que nos vamos en
    // vez de quedarse esperando un tiempo de espera. Se encola y se
    // vacia la cola en el acto, porque despues se cierra el canal.
    flexPhoneLinkSend(L, FLNK_T_BYE, NULL, 0, false);
    flexPhoneLinkTick(L, NULL, L->lastTxMs ? L->lastTxMs + 1 : 1);
  }
  flexPhoneTrStop(L->tr);
  linkResetAll(L);
  L->session = 0;
  L->chanPeerId[0] = 0;
  // Apagar el enlace CIERRA el emparejamiento en curso. Guardar el
  // codigo "por si vuelve" es justo lo que hacia que un intento
  // abandonado reapareciera despues contra una sal que ya no existia.
  if(L->pair.state == FLP_PAIR_OPEN) pairLog("PAIR SESSION CANCELLED (link off)");
  pairClear(&L->pair, FLP_PAIR_NONE);
  L->state = (L->cap == FLP_LINK_CAP_NONE) ? FLP_LS_UNAVAILABLE : FLP_LS_OFF;
}

void flexPhoneLinkForget(FlexPhoneLink* L){
  if(!L) return;
  flexPhoneLinkStop(L);
  // La CLAVE se borra de verdad, no solo la bandera: dejarla en RAM
  // despues de desvincular seria material vivo de un vinculo que el
  // usuario ha pedido olvidar.
  memset(&L->bond, 0, sizeof(L->bond));
  // pairClear ya borra codigo, sal, reto y clave a medio derivar de
  // una vez: son los cuatro campos de la misma sesion.
  pairClear(&L->pair, FLP_PAIR_NONE);
  memset(L->nonce, 0, sizeof(L->nonce));
  memset(&L->anti, 0, sizeof(L->anti));
  L->txCounter = 0;
}

// =============================================================
//  Envio
// =============================================================
// Que tipos se permiten fuera de sesion. Todo lo demas exige una
// sesion autenticada: sin esto, alguien que llegue al puerto podria
// inyectar notificaciones sin haber emparejado nunca.
static bool typeAllowedWithoutSession(uint8_t type){
  switch(type){
    case FLNK_T_HELLO:
    case FLNK_T_WELCOME:
    case FLNK_T_BYE:
    case FLNK_T_ERR:
    case FLNK_T_PAIR_REQ:
    case FLNK_T_PAIR_CODE:
    case FLNK_T_PAIR_CONFIRM:
    case FLNK_T_AUTH_CHALLENGE:
    case FLNK_T_AUTH_RESPONSE:
    case FLNK_T_AUTH_OK:
    // PING/PONG entran en la lista porque son EL LATIDO, y el latido
    // hace falta justo cuando todavia no hay sesion: mientras se
    // empareja, el canal no tiene nada que transportar durante el
    // minuto largo que el usuario tarda en leer y teclear seis
    // digitos. Sin latido el telefono da el socket por muerto a los
    // 40 s y el emparejamiento se cae solo.
    //
    // No conceden nada: no llevan carga, no abren sesion y no tocan
    // el modelo. El telefono ya los aceptaba sin sesion por el mismo
    // motivo (ver HANDSHAKE_TYPES en WifiLinkServer.kt).
    case FLNK_T_PING:
    case FLNK_T_PONG:
      return true;
    default:
      return false;
  }
}

bool flexPhoneLinkSend(FlexPhoneLink* L, uint8_t type,
                       const uint8_t* payload, size_t len, bool needsAck){
  if(!L) return false;
  if(L->state != FLP_LS_READY && !typeAllowedWithoutSession(type)) return false;
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
  m->packet = L->txPacket++;
  if(L->txPacket == 0) L->txPacket = 1;     // 0 queda libre para "sin paquete"
  m->nextTryMs = 0;                          // 0 = sale en el proximo tick
  return true;
}

// #############################################################
// ##  ENTREGA FISICA DE UN MENSAJE
// ##  ------------------------------------------------------
// ##  Trocea la carga segun el MTU REAL del transporte y entrega
// ##  cada fragmento. Devuelve false si el canal no pudo con
// ##  alguno: entonces el mensaje se reintenta entero, porque un
// ##  mensaje medio entregado no se puede reensamblar.
// #############################################################
static bool deliver(FlexPhoneLink* L, FlexPhoneTxMsg* m){
  if(!L || !m) return false;
  uint16_t mtu = L->mtu;
  if(mtu > FLNK_MAX_FRAME) mtu = FLNK_MAX_FRAME;
  if(mtu < FLNK_MIN_MTU)   return false;      // canal inservible
  const size_t perFrag = (size_t)mtu - FLNK_HDR_SIZE;
  const int frags = flexLinkFragCount(m->len, mtu);
  if(frags <= 0) return false;

  uint8_t frame[FLNK_MAX_FRAME];
  size_t at = 0;
  for(int i = 0; i < frags; i++){
    const size_t take = (m->len - at) < perFrag ? (size_t)(m->len - at) : perFrag;
    FlexLinkHeader h;
    memset(&h, 0, sizeof(h));
    h.version   = FLNK_VERSION;
    h.type      = m->type;
    h.session   = L->session;
    h.packet    = m->packet;
    h.frag      = (uint8_t)i;
    h.fragCount = (uint8_t)frags;
    h.len       = (uint16_t)take;
    h.counter   = ++L->txCounter;
    const int n = flexLinkWriteFrame(frame, sizeof(frame), &h,
                                     take ? (m->data + at) : NULL, take);
    if(n <= 0) return false;
    if(flexPhoneTrSend(L->tr, frame, (size_t)n) != FLP_TR_OK) return false;
    at += take;
  }
  return true;
}

// =============================================================
//  Recepcion
// =============================================================
// Aplica un mensaje YA reensamblado al modelo. Devuelve true si se
// entendio. Nada de lo que entra aqui se da por bueno sin decodificar.
static bool applyMessage(FlexPhoneLink* L, FlexPhoneModel* M,
                         uint8_t type, const uint8_t* p, size_t n, uint32_t nowMs){
  if(!L) return false;
  switch(type){
    // -------------------------------------------------------
    //  Presentacion
    // -------------------------------------------------------
    case FLNK_T_WELCOME: {
      // El telefono contesta a nuestro HELLO. Aqui SOLO se aprende
      // quien dice ser: la sesion no se abre hasta autenticar.
      FlexLinkRd r; flexLinkRdInit(&r, p, n);
      const uint8_t ver = flexLinkRdU8(&r);
      char id[FLP_PEERID_MAX]   = {0};
      char nm[FLP_DEVNAME_MAX]  = {0};
      flexLinkRdStr(&r, id, sizeof(id));
      flexLinkRdStr(&r, nm, sizeof(nm));
      if(!flexLinkRdOk(&r) || !id[0]){ L->nBad++; return false; }
      if(ver < FLNK_VERSION_MIN){
        setErr(L, "la app del telefono es de una version anterior; actualizala");
        gotoState(L, FLP_LS_ERROR, nowMs);
        return true;
      }
      flexLinkUtf8Copy(L->chanPeerId, sizeof(L->chanPeerId), id);
      if(M) flexLinkUtf8Copy(M->phone.name, sizeof(M->phone.name), nm);
      pairLog("WELCOME de %s", id);

      // ¿Es el telefono que ya conocemos? Se compara el
      // identificador, no el nombre: el nombre lo cambia el usuario.
      const bool known = L->bond.valid &&
                         strncmp(L->bond.peerId, id, FLP_PEERID_MAX) == 0;
      if(known){
        // Vinculo guardado: reto de sesion. No se vuelve a emparejar.
        //
        // EL RETO SE GENERA AQUI, EN CADA SESION. Antes se sembraba en
        // flexPhoneLinkBeginPairing, y al pasar el emparejamiento a
        // tener su propio reto (pair.nonce) este se quedo sin sembrar:
        // un vinculo ya guardado reconectaba SIEMPRE con el mismo reto
        // (todo ceros). La sesion se abria igual -- los dos lados usan
        // el que viaja --, pero un reto predecible es justo lo que el
        // reto-respuesta existe para evitar.
        flexAuthRandomBytes(L->rnd, L->nonce, sizeof(L->nonce));
        L->session = (uint16_t)((nowMs | 1) & 0xFFFF);
        flexLinkAntiReplayInit(&L->anti);
        uint8_t body[FLXA_NONCE_SIZE + 2];
        FlexLinkWr w; flexLinkWrInit(&w, body, sizeof(body));
        flexLinkWrBytes(&w, L->nonce, FLXA_NONCE_SIZE);
        flexLinkWrU16(&w, L->session);
        gotoState(L, FLP_LS_AUTH, nowMs);
        if(flexLinkWrOk(&w))
          flexPhoneLinkSend(L, FLNK_T_AUTH_CHALLENGE, body, w.at, false);
      } else if(pairAlive(&L->pair, nowMs)){
        // #####################################################
        // ##  HAY SESION DE EMPAREJAMIENTO VIVA
        // ##  ----------------------------------------------
        // ##  Se reenvia LA SAL DE ESA SESION, tal cual. El
        // ##  codigo NO se regenera y NO viaja: el usuario lo lee
        // ##  en Flex OS y lo teclea en el telefono.
        // ##
        // ##  Que esto se ejecute tambien al RECONECTAR es la
        // ##  parte que faltaba. El telefono tira sus datos del
        // ##  emparejamiento cuando se cae el socket, asi que si
        // ##  al volver no se le reenviara la sal se quedaba con
        // ##  el boton de emparejar apagado ("esperando a Flex
        // ##  OS") contra un reloj que si estaba ensenando el
        // ##  codigo.
        // #####################################################
        gotoState(L, FLP_LS_PAIRING, nowMs);
        // La clave se deriva AQUI, que es cuando ya se sabe con quien.
        pairDeriveFor(L, id);
        const bool sent = pairSendSalt(L);
        char mc[FLP_LINK_CODE_LEN + 1];
        maskCode(L->pair.code, mc);
        pairLog("PAIR SALT SENT (%s) code=%s to=%s", sent ? "ok" : "FALLO", mc, id);
      } else {
        // Telefono desconocido y NADIE ha pedido emparejar.
        //
        // Antes esto pasaba a EMPAREJANDO, y era mentira por partida
        // doble: no habia codigo que ensenar, y el descubrimiento
        // anunciaba a este reloj como "ensenando codigo" ante
        // cualquier movil de la red. El canal esta abierto y no hay
        // sesion: eso es CONECTANDO.
        gotoState(L, FLP_LS_CONNECTING, nowMs);
        setErr(L, "telefono sin emparejar: pulsa Emparejar telefono en Flex OS");
      }
      return true;
    }

    // -------------------------------------------------------
    //  Emparejamiento
    // -------------------------------------------------------
    case FLNK_T_PAIR_CONFIRM: {
      // El telefono demuestra que derivo la MISMA clave, o sea que
      // el usuario tecleo el codigo correcto. Sin prueba valida no
      // se empareja: confirmar a ciegas es lo que permitiria que un
      // vecino se vinculara solo por estar en la red.
      // CADUCADO SE DICE COMO CADUCADO. Una prueba que llega tarde no
      // es un codigo mal tecleado, y decirle al usuario que reviso
      // mal los digitos cuando lo que paso es que se le acabo el
      // tiempo es mandarle a buscar donde no hay nada.
      if(L->pair.state == FLP_PAIR_OPEN && !pairAlive(&L->pair, nowMs)){
        pairLog("PAIR CONFIRM RECEIVED: CODE EXPIRED");
        setErr(L, "el emparejamiento caduco; vuelve a intentarlo");
        pairClear(&L->pair, FLP_PAIR_NONE);
        const uint8_t e = FLNK_E_TIMEOUT;
        flexPhoneLinkSend(L, FLNK_T_ERR, &e, 1, false);
        gotoState(L, FLP_LS_CONNECTING, nowMs);
        return true;
      }
      if(L->pair.state != FLP_PAIR_OPEN || !L->pair.keyOk){
        // No hay sesion abierta (o todavia no se sabe con quien se
        // deriva). Se dice, en vez de dejar al telefono esperando.
        L->nAuthFail++;
        pairLog("PAIR CONFIRM RECEIVED: NO PAIRING SESSION");
        const uint8_t e = FLNK_E_NOTPAIRED;
        flexPhoneLinkSend(L, FLNK_T_ERR, &e, 1, false);
        return false;
      }
      uint8_t proof[FLXA_PROOF_SIZE];
      FlexLinkRd r; flexLinkRdInit(&r, p, n);
      flexLinkRdBytes(&r, proof, sizeof(proof));
      if(!flexLinkRdOk(&r)){ L->nBad++; return false; }
      // La prueba va SIEMPRE sobre el reto de LA SESION DE
      // EMPAREJAMIENTO y sesion 0, que es lo que el telefono recibio
      // en PAIR_CODE. Usar aqui otro reto -- por ejemplo el de la
      // sesion de un vinculo anterior -- hacia fallar un codigo que
      // el usuario habia tecleado bien.
      const bool match = flexAuthVerify(L->pair.key, FLXA_ROLE_PHONE,
                                        L->pair.nonce, 0, proof);
      {
        char mc[FLP_LINK_CODE_LEN + 1];
        maskCode(L->pair.code, mc);
        pairLog("PAIR CONFIRM RECEIVED: expected=%s peer=%s MATCH=%s",
             mc, L->pair.peerId, match ? "true" : "false");
      }
      if(!match){
        L->nAuthFail++;
        if(L->pair.rejects < 255) L->pair.rejects++;
        setErr(L, "el codigo tecleado en el telefono no coincide");
        // Y SE LE DICE AL TELEFONO. Antes el fallo se anotaba solo
        // aqui: alli la pantalla se quedaba en "Comprobando..." hasta
        // que caducara la ventana de dos minutos, sin nada que
        // explicara por que. Con esto el usuario ve el motivo en el
        // acto y puede volver a teclear.
        //
        // LA SESION SIGUE VIVA a proposito: el codigo que se ensena
        // no cambia porque alguien se equivoque al teclearlo, asi que
        // el usuario puede corregir un digito y reintentar contra EL
        // MISMO codigo que tiene delante.
        const uint8_t e = FLNK_E_AUTH;
        flexPhoneLinkSend(L, FLNK_T_ERR, &e, 1, false);
        return true;
      }
      L->pair.peerConfirmed = true;
      if(pairBothConfirmed(&L->pair)){
        // Vinculo cerrado. A partir de aqui no hace falta el codigo.
        memcpy(L->bond.key, L->pair.key, FLXA_KEY_SIZE);
        flexLinkUtf8Copy(L->bond.peerId, sizeof(L->bond.peerId), L->pair.peerId);
        if(M) flexLinkUtf8Copy(L->bond.peerName, sizeof(L->bond.peerName), M->phone.name);
        L->bond.valid = true;
        L->session = (uint16_t)((nowMs | 1) & 0xFFFF);
        flexLinkAntiReplayInit(&L->anti);
        // Flex OS devuelve SU prueba. Sin esto el telefono no sabria
        // si ha emparejado con Flex OS o con otra cosa que escuchaba.
        // La prueba de Flex OS va sobre LA SESION QUE SE ACABA DE
        // ABRIR, que es la que viaja en la cabecera. El telefono la
        // comprueba con ese mismo numero; calcularla sobre otro valor
        // haria que el telefono no reconociera a Flex OS y tirara un
        // emparejamiento que en realidad era bueno.
        //
        // El reto sigue siendo el de la sesion de emparejamiento: es
        // el unico que el telefono tiene apuntado en este punto.
        uint8_t mine[FLXA_PROOF_SIZE];
        flexAuthProof(L->bond.key, FLXA_ROLE_HOST, L->pair.nonce, L->session, mine);
        // El codigo deja de existir AQUI, no antes: hasta este
        // momento seguia siendo la unica forma de reintentar.
        pairClear(&L->pair, FLP_PAIR_DONE);
        gotoState(L, FLP_LS_READY, nowMs);
        flexPhoneLinkSend(L, FLNK_T_AUTH_OK, mine, sizeof(mine), false);
        L->hostProven = true;
        L->lastTxMs = nowMs;
        // EL RELOJ DE "ENLACE MUERTO" ARRANCA AQUI. Un emparejamiento
        // puede durar minutos -- el usuario esta leyendo y tecleando
        // --, asi que al abrir la sesion `lastRxMs` puede ser mucho
        // mas viejo que los 30 s del plazo: sin esto, el enlace se
        // declaraba muerto en el cuadro siguiente a emparejar bien.
        L->lastRxMs = nowMs ? nowMs : 1;
        pairLog("PAIR COMPLETE: SESSION CREATED %u", (unsigned)L->session);
      } else {
        pairLog("CODE MATCH ok; falta confirmar en Flex OS");
      }
      return true;
    }

    // -------------------------------------------------------
    //  Autenticacion de sesion
    // -------------------------------------------------------
    case FLNK_T_AUTH_RESPONSE: {
      if(!L->bond.valid){ L->nAuthFail++; return false; }
      uint8_t proof[FLXA_PROOF_SIZE];
      FlexLinkRd r; flexLinkRdInit(&r, p, n);
      flexLinkRdBytes(&r, proof, sizeof(proof));
      if(!flexLinkRdOk(&r)){ L->nBad++; return false; }
      if(!flexAuthVerify(L->bond.key, FLXA_ROLE_PHONE, L->nonce, L->session, proof)){
        L->nAuthFail++;
        setErr(L, "el telefono no supero la autenticacion");
        L->session = 0;
        gotoState(L, FLP_LS_SEARCHING, nowMs);
        return true;
      }
      uint8_t mine[FLXA_PROOF_SIZE];
      flexAuthProof(L->bond.key, FLXA_ROLE_HOST, L->nonce, L->session, mine);
      gotoState(L, FLP_LS_READY, nowMs);
      L->reconnectAttempt = 0;
      L->err[0] = 0;
      flexPhoneLinkSend(L, FLNK_T_AUTH_OK, mine, sizeof(mine), false);
      L->hostProven = true;
      return true;
    }

    case FLNK_T_HELLO:
      // En v2 el que abre el canal es Flex OS, asi que un HELLO
      // entrante solo puede venir de un extremo confundido. Se
      // contesta con el error concreto en vez de callar.
      flexPhoneLinkSend(L, FLNK_T_ERR, (const uint8_t[]){ FLNK_E_VERSION }, 1, false);
      return true;

    case FLNK_T_BYE:
      // El telefono se despide: se vuelve a buscar, no a "error".
      L->session = 0;
      gotoState(L, FLP_LS_SEARCHING, nowMs);
      if(M) flexPhoneModelClear(M, false);   // el estado instantaneo deja de ser cierto
      return true;

    case FLNK_T_PING:
      flexPhoneLinkSend(L, FLNK_T_PONG, NULL, 0, false);
      return true;

    case FLNK_T_PONG: {
      // LATENCIA REAL: ida y vuelta de ESTE ping. No se estima ni se
      // interpola; si no hay ping en vuelo, no se apunta nada.
      if(L->pingSentMs){
        uint32_t rtt = nowMs - L->pingSentMs;
        if(rtt > 60000u) rtt = 60000u;
        L->rttMs = (uint16_t)(rtt > 0xFFFEu ? 0xFFFEu : rtt);
        L->pingSentMs = 0;
      }
      return true;
    }

    // -------------------------------------------------------
    //  Contenido. Todo esto exige sesion (lo garantiza onFrame).
    // -------------------------------------------------------
    case FLNK_T_CAPS: {
      if(!M) return false;
      FlexPhoneCaps c;
      if(!flexPhoneDecCaps(p, n, &c)){ L->nBad++; M->stats.badPacket++; return false; }
      c.stampMs = nowMs;
      M->caps = c;
      return true;
    }

    case FLNK_T_NOTIF_ADD:
    case FLNK_T_NOTIF_UPDATE: {
      if(!M) return false;
      FlexPhoneNotif nt;
      if(!flexPhoneDecNotif(p, n, &nt)){ L->nBad++; M->stats.badPacket++; return false; }
      if(flexPhoneNotifPut(M, &nt, nowMs) < 0) return false;
      flexPhoneConvRebuild(M);
      M->lastSyncMs = nowMs;
      return true;
    }
    case FLNK_T_NOTIF_REMOVE: {
      if(!M) return false;
      FlexLinkRd r; flexLinkRdInit(&r, p, n);
      const uint32_t id = flexLinkRdU32(&r);
      if(!flexLinkRdOk(&r)){ L->nBad++; M->stats.badPacket++; return false; }
      flexPhoneNotifRemove(M, id);
      flexPhoneConvRebuild(M);
      return true;
    }
    case FLNK_T_NOTIF_CLEAR:
      if(!M) return false;
      flexPhoneNotifClearAll(M);
      flexPhoneConvRebuild(M);
      return true;

    case FLNK_T_REPLY_RESULT: {
      if(!M) return false;
      uint32_t id = 0; uint8_t ec = 0;
      if(!flexPhoneDecReplyResult(p, n, &id, &ec)){ L->nBad++; return false; }
      // Aqui se registra el resultado REAL. Un exito solo se cuenta
      // cuando Android dijo que acepto la accion.
      if(ec == FLNK_E_NONE) M->stats.repliesOk++;
      else                  M->stats.repliesFail++;
      return true;
    }

    case FLNK_T_PHONE_STATE: {
      if(!M) return false;
      FlexPhoneState st;
      if(!flexPhoneDecPhoneState(p, n, &st)){ L->nBad++; M->stats.badPacket++; return false; }
      st.stampMs = nowMs;
      M->phone = st;
      M->lastSyncMs = nowMs;
      return true;
    }
    case FLNK_T_MEDIA_STATE: {
      if(!M) return false;
      FlexPhoneMedia md;
      if(!flexPhoneDecMedia(p, n, &md)){ L->nBad++; M->stats.badPacket++; return false; }
      md.stampMs = nowMs;
      M->media = md;
      return true;
    }
    case FLNK_T_RELAY_INFO: {
      if(!M) return false;
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
  if(!L || !frame) return false;

  // -- El enlace tiene que estar VIVO para aceptar nada --
  // Apagado, no disponible o en error: no se procesa ni una trama.
  // Sin esto, un dispositivo cualquiera podria seguir inyectando
  // mensajes despues de desvincular.
  if(L->state == FLP_LS_UNAVAILABLE || L->state == FLP_LS_OFF ||
     L->state == FLP_LS_ERROR){
    L->nDropped++;
    return false;
  }

  FlexLinkHeader h;
  const uint8_t* pay = NULL;
  size_t payN = 0;
  const int rc = flexLinkReadFrame(frame, n, &h, &pay, &payN);
  if(rc != FLNK_OK){
    L->nBad++;
    if(M) M->stats.badPacket++;
    if(rc == FLNK_ERR_VERSION){
      // Version incompatible: se dice UNA vez y se para. Reintentar
      // contra una app que no entiende este protocolo es gastar
      // bateria de los dos lados para nada.
      setErr(L, "la app Flex Phone del telefono no habla esta version del protocolo");
      gotoState(L, FLP_LS_ERROR, nowMs);
    }
    return false;
  }

  // -- Anti-repeticion --
  if(!flexLinkAntiReplayCheck(&L->anti, h.counter)){
    L->nDropped++;
    return false;
  }

  // -- Control de acceso por tipo --
  // Fuera de sesion SOLO pasan los mensajes del apreton de manos. Una
  // notificacion de un dispositivo sin autenticar se descarta aunque
  // la trama sea perfecta: ese es justo el ataque que hay que parar.
  if(L->state != FLP_LS_READY && !typeAllowedWithoutSession(h.type)){
    L->nDropped++;
    return false;
  }
  // Con sesion abierta, la sesion tiene que ser LA NUESTRA.
  if(L->state == FLP_LS_READY && L->session && h.session != L->session &&
     h.type != FLNK_T_BYE){
    L->nDropped++;
    return false;
  }

  L->nRx++;
  L->lastRxMs = nowMs ? nowMs : 1;

  // -- Reensamblado --
  const int rr = flexLinkReasmFeed(&L->reasm, &h, pay, payN, nowMs);
  if(rr == FLNK_R_DROP){ L->nDropped++; return false; }
  if(rr == FLNK_R_NEED_MORE) return true;

  return applyMessage(L, M, h.type, L->reasm.buf, L->reasm.len, nowMs);
}

// =============================================================
//  Emparejamiento
// =============================================================
// #############################################################
// ##  AQUI, Y SOLO AQUI, NACE UN CODIGO
// ##  ------------------------------------------------------
// ##  Esta funcion se llama desde UN sitio: el boton "Emparejar
// ##  telefono" de la interfaz. Ningun mensaje de la red llega
// ##  hasta aqui, y esa es la regla que arregla el fallo de raiz:
// ##  mientras la sesion viva, el codigo que ensena la pantalla es
// ##  exactamente el que el servidor valida, pase lo que pase con
// ##  el canal.
// #############################################################
void flexPhoneLinkBeginPairing(FlexPhoneLink* L, FlexAuthRandFn rnd, uint32_t nowMs){
  if(!L) return;
  if(L->state == FLP_LS_UNAVAILABLE || L->state == FLP_LS_OFF) return;

  // Una sesion nueva TIRA la anterior entera. Reaprovechar la sal o
  // la clave a medio derivar de un intento abandonado es como un
  // codigo viejo acababa validandose contra material nuevo.
  pairClear(&L->pair, FLP_PAIR_OPEN);
  FlexPhonePairing* P = &L->pair;
  flexAuthFormatCode(rnd ? rnd() : 0, P->code);
  flexAuthRandomBytes(rnd, P->salt,  sizeof(P->salt));
  flexAuthRandomBytes(rnd, P->nonce, sizeof(P->nonce));
  // El plazo cuelga de AQUI. No de `stateSinceMs`, que lo reinicia
  // cualquier vaiven del canal y dejaba el codigo sin caducar nunca.
  P->startedMs = nowMs ? nowMs : 1;
  L->err[0] = 0;
  gotoState(L, FLP_LS_PAIRING, nowMs);

  {
    char mc[FLP_LINK_CODE_LEN + 1];
    maskCode(P->code, mc);
    pairLog("PAIR SESSION CREATED: code=%s ventana=%u ms",
         mc, (unsigned)FLP_LINK_PAIR_WINDOW_MS);
  }

  // Si el telefono ya se presento, la sal sale ya. Si todavia no, sale
  // en cuanto llegue su WELCOME: el orden de los dos eventos depende
  // de la red y ninguno de los dos puede quedarse esperando al otro.
  if(L->chanPeerId[0]){
    pairDeriveFor(L, L->chanPeerId);
    const bool sent = pairSendSalt(L);
    pairLog("PAIR SALT SENT (%s) to=%s", sent ? "ok" : "FALLO", L->chanPeerId);
  }
}

void flexPhoneLinkConfirm(FlexPhoneLink* L, uint32_t nowMs){
  if(!L || L->pair.state != FLP_PAIR_OPEN) return;
  // Confirmar un codigo que ya caduco no empareja nada: se cierra la
  // sesion y se dice, en vez de dejar al usuario pulsando un boton
  // que no puede funcionar.
  if(!pairAlive(&L->pair, nowMs)){
    setErr(L, "el emparejamiento caduco; vuelve a intentarlo");
    pairClear(&L->pair, FLP_PAIR_NONE);
    gotoState(L, FLP_LS_CONNECTING, nowMs);
    return;
  }
  L->pair.userConfirmed = true;
  pairLog("USER CONFIRMED en Flex OS");
  if(pairBothConfirmed(&L->pair)){
    // El telefono ya habia demostrado la clave: solo faltaba el
    // usuario. Se cierra el vinculo aqui mismo.
    memcpy(L->bond.key, L->pair.key, FLXA_KEY_SIZE);
    flexLinkUtf8Copy(L->bond.peerId, sizeof(L->bond.peerId), L->pair.peerId);
    L->bond.valid = true;
    L->session = (uint16_t)((nowMs | 1) & 0xFFFF);
    flexLinkAntiReplayInit(&L->anti);
    uint8_t mine[FLXA_PROOF_SIZE];
    flexAuthProof(L->bond.key, FLXA_ROLE_HOST, L->pair.nonce, L->session, mine);
    pairClear(&L->pair, FLP_PAIR_DONE);
    gotoState(L, FLP_LS_READY, nowMs);
    flexPhoneLinkSend(L, FLNK_T_AUTH_OK, mine, sizeof(mine), false);
    L->hostProven = true;
    // Ver arriba: la sesion empieza con el reloj de inactividad a cero.
    L->lastTxMs = nowMs ? nowMs : 1;
    L->lastRxMs = nowMs ? nowMs : 1;
    pairLog("PAIR COMPLETE: SESSION CREATED %u", (unsigned)L->session);
  }
}

void flexPhoneLinkCancelPairing(FlexPhoneLink* L, uint32_t nowMs){
  if(!L || L->pair.state != FLP_PAIR_OPEN) return;
  // El telefono tiene que enterarse: si no, se queda esperando contra
  // una sal que ya no existe. Se manda ANTES de borrar nada.
  const uint8_t e = FLNK_E_TIMEOUT;
  flexPhoneLinkSend(L, FLNK_T_ERR, &e, 1, false);
  pairClear(&L->pair, FLP_PAIR_NONE);
  L->err[0] = 0;
  pairLog("PAIR SESSION CANCELLED por el usuario");
  // El canal NO se toca: cancelar el emparejamiento no es apagar el
  // enlace. Se vuelve al estado que corresponde al canal que haya.
  gotoState(L, flexPhoneTrState(L->tr) == FLP_TC_OPEN
                 ? FLP_LS_CONNECTING : FLP_LS_SEARCHING, nowMs);
}

bool flexPhoneLinkPairComplete(const FlexPhoneLink* L){
  // FLP_PAIR_DONE tambien cuenta. Al cerrarse el vinculo la sesion se
  // borra entera -- codigo incluido --, y quien pregunta justo
  // despues (la interfaz, para guardar la clave en NVS) tiene que
  // seguir recibiendo un si. Sin esto, emparejar bien se leia desde
  // fuera igual que no emparejar.
  if(!L) return false;
  if(L->pair.state == FLP_PAIR_DONE) return true;
  return pairBothConfirmed(&L->pair);
}

const char* flexPhoneLinkCode(const FlexPhoneLink* L){
  if(!L || L->pair.state != FLP_PAIR_OPEN) return "";
  return L->pair.code;
}

uint32_t flexPhoneLinkPairRemainingMs(const FlexPhoneLink* L, uint32_t nowMs){
  if(!L || L->pair.state != FLP_PAIR_OPEN || !L->pair.startedMs) return 0;
  const uint32_t gone = (uint32_t)(nowMs - L->pair.startedMs);
  return gone >= FLP_LINK_PAIR_WINDOW_MS ? 0u : (FLP_LINK_PAIR_WINDOW_MS - gone);
}

// =============================================================
//  Consultas
// =============================================================
bool flexPhoneLinkLatency(const FlexPhoneLink* L, uint16_t* outMs){
  if(!L || L->rttMs == 0xFFFF) return false;
  if(outMs) *outMs = L->rttMs;
  return true;
}

void flexPhoneLinkPeer(FlexPhoneLink* L, char* out, size_t outN){
  if(!out || !outN) return;
  out[0] = 0;
  if(L) flexPhoneTrPeer(L->tr, out, outN);
}

// ¿Hay ya un mensaje de este tipo esperando salida? Evita encolar un
// segundo latido mientras el primero sigue en la cola.
static bool txHasType(const FlexPhoneLink* L, uint8_t type){
  if(!L) return false;
  for(int i = 0; i < FLP_LINK_TXQ; i++)
    if(L->tx[i].used && L->tx[i].type == type) return true;
  return false;
}

// =============================================================
//  Tick
// =============================================================
void flexPhoneLinkTick(FlexPhoneLink* L, FlexPhoneModel* M, uint32_t nowMs){
  if(!L) return;
  if(L->state == FLP_LS_UNAVAILABLE || L->state == FLP_LS_OFF) return;

  // -----------------------------------------------------------
  //  0) El canal manda. Si se cae, la sesion deja de existir; si
  //     se abre, hay que presentarse.
  // -----------------------------------------------------------
  const uint8_t tc = flexPhoneTrState(L->tr);
  // UN SOLO dueno de la espera progresiva. El transporte reintenta su
  // conexion por su cuenta; lo que cuenta aqui son sus FALLOS, y al
  // agotarse los intentos se para y se dice. Dos temporizadores de
  // reintento -- uno aqui y otro abajo -- solo conseguian que ninguno
  // llegara nunca a rendirse.
  if(tc == FLP_TC_FAILED && L->state != FLP_LS_ERROR && nowMs >= L->reconnectAtMs){
    if(L->state == FLP_LS_READY || L->state == FLP_LS_AUTH){
      L->session = 0;
      L->nReconnects++;
      if(M){ M->stats.reconnects++; flexPhoneModelClear(M, false); }
    }
    const uint32_t d = flexLinkRetryDelayMs(L->reconnectAttempt);
    if(d == 0){
      setErr(L, "no se pudo reconectar con el telefono");
      // Rendirse del todo SI cierra el emparejamiento: el codigo que
      // hay en pantalla ya no puede llegar a ninguna parte, y dejarlo
      // ahi es pedirle al usuario que teclee algo que no sirve.
      if(L->pair.state == FLP_PAIR_OPEN){
        pairLog("PAIR SESSION CANCELLED: el canal se rindio");
        pairClear(&L->pair, FLP_PAIR_NONE);
      }
      gotoState(L, FLP_LS_ERROR, nowMs);
    } else {
      setErr(L, flexPhoneTrStatus(L->tr));
      L->reconnectAttempt++;
      L->reconnectAtMs = nowMs + d;
      // #####################################################
      // ##  EL EMPAREJAMIENTO SOBREVIVE AL CANAL
      // ##  ----------------------------------------------
      // ##  Antes esta linea sacaba al enlace de EMPAREJANDO
      // ##  pasara lo que pasara. Como el telefono cierra el
      // ##  socket mientras nadie ha autenticado, el ciclo era:
      // ##  se cae el canal -> se sale de EMPAREJANDO -> la
      // ##  tarjeta del codigo desaparece -> al reconectar el
      // ##  estado volvia a EMPAREJANDO con el codigo VIEJO
      // ##  todavia en memoria. Eso es el parpadeo que se veia
      // ##  en el reloj, y de paso reiniciaba el plazo.
      // ##
      // ##  El codigo es de la sesion, no del socket. Mientras
      // ##  la sesion viva, EMPAREJANDO se queda.
      // #####################################################
      if(!pairAlive(&L->pair, nowMs))
        gotoState(L, FLP_LS_SEARCHING, nowMs);
    }
  }
  if((tc == FLP_TC_DOWN || tc == FLP_TC_SEARCHING || tc == FLP_TC_OPENING) &&
     (L->state == FLP_LS_READY || L->state == FLP_LS_AUTH ||
      L->state == FLP_LS_CONNECTING)){
    // El canal se fue: no se conserva "conectado" ni un cuadro mas.
    L->session = 0;
    L->nReconnects++;
    if(M){ M->stats.reconnects++; flexPhoneModelClear(M, false); }
    gotoState(L, FLP_LS_SEARCHING, nowMs);
  }
  // Canal cerrado: el saludo de este canal deja de valer. Al abrirse
  // uno nuevo habra que volver a presentarse.
  if(tc != FLP_TC_OPEN) L->helloSent = false;

  // #############################################################
  // ##  PRESENTARSE AL ABRIRSE EL CANAL
  // ##  ------------------------------------------------------
  // ##  La condicion es "hay canal y todavia no me he presentado
  // ##  en el", NO "el enlace esta en tal o cual estado".
  // ##
  // ##  Atarlo a un par de estados concretos rompia el caso mas
  // ##  normal de todos: el usuario pulsa "Emparejar telefono", el
  // ##  enlace pasa a EMPAREJANDO en el acto y el transporte tarda
  // ##  unos segundos en encontrar el telefono. Cuando por fin
  // ##  abria el canal, el enlace ya no estaba en "buscando", asi
  // ##  que el saludo no salia: Flex OS ensenaba un codigo que no
  // ##  servia para nada y a los dos minutos decia que habia
  // ##  caducado, sin ninguna pista de por que.
  // #############################################################
  if(tc == FLP_TC_OPEN && !L->helloSent && L->state != FLP_LS_READY){
    const uint16_t m = flexPhoneTrMtu(L->tr);
    L->mtu = m ? m : (uint16_t)FLNK_MIN_MTU;
    sessionReset(L);
    L->session = 0;
    L->reconnectAttempt = 0;
    L->err[0] = 0;
    // Quien habia al otro lado era del canal ANTERIOR. Se vuelve a
    // aprender del WELCOME; la sesion de emparejamiento guarda por su
    // cuenta con quien derivo su clave.
    L->chanPeerId[0] = 0;
    // El estado de EMPAREJAMIENTO se conserva: el usuario esta
    // mirando un codigo en pantalla, y perderselo porque el Wi-Fi
    // parpadeo un segundo seria gratuito. El codigo y la sal siguen
    // siendo validos -- lo unico que cambia es el canal por el que
    // viajan --, y al llegar el WELCOME nuevo se reenvia la sal.
    if(!pairAlive(&L->pair, nowMs)) gotoState(L, FLP_LS_CONNECTING, nowMs);
    else                            gotoState(L, FLP_LS_PAIRING,    nowMs);
    uint8_t body[1 + FLXA_ID_MAX];
    FlexLinkWr w; flexLinkWrInit(&w, body, sizeof(body));
    flexLinkWrU8(&w, FLNK_VERSION);
    flexLinkWrStr(&w, L->selfId, FLXA_ID_MAX - 1);
    if(flexLinkWrOk(&w)) flexPhoneLinkSend(L, FLNK_T_HELLO, body, w.at, false);
    L->helloSent = true;
    L->lastRxMs = nowMs ? nowMs : 1;   // el reloj de "enlace muerto" arranca aqui
  }

  // -----------------------------------------------------------
  //  1) Parciales caducados: un mensaje que nunca se completo no
  //     puede quedarse ocupando el reensamblador para siempre.
  // -----------------------------------------------------------
  if(flexLinkReasmExpire(&L->reasm, nowMs, FLP_LINK_REASM_TIMEOUT_MS)){
    L->nTimeouts++;
    if(M) M->stats.badPacket++;
  }

  // -----------------------------------------------------------
  //  2) El codigo de emparejamiento caduca. Si nadie confirma, se
  //     vuelve a buscar en vez de dejar el codigo en pantalla.
  // -----------------------------------------------------------
  // EL PLAZO ES DE LA SESION, no del estado. Antes colgaba de
  // `stateSinceMs`, que lo reinicia cualquier cambio de estado: cada
  // vaiven del canal le regalaba al codigo otros dos minutos y el
  // aviso de caducidad no llegaba nunca.
  if(L->pair.state == FLP_PAIR_OPEN && !pairAlive(&L->pair, nowMs)){
    pairClear(&L->pair, FLP_PAIR_NONE);
    setErr(L, "el emparejamiento caduco; vuelve a intentarlo");
    pairLog("PAIR SESSION EXPIRED");
    // El telefono tiene que enterarse: si no, se queda esperando un
    // codigo que ya no vale contra una sal que ya no existe.
    { const uint8_t e = FLNK_E_TIMEOUT;
      flexPhoneLinkSend(L, FLNK_T_ERR, &e, 1, false); }
    if(L->state == FLP_LS_PAIRING)
      gotoState(L, flexPhoneTrState(L->tr) == FLP_TC_OPEN
                     ? FLP_LS_CONNECTING : FLP_LS_SEARCHING, nowMs);
  }

  // -----------------------------------------------------------
  //  3) El apreton de manos tampoco espera para siempre.
  // -----------------------------------------------------------
  if(L->state == FLP_LS_AUTH && L->stateSinceMs &&
     (uint32_t)(nowMs - L->stateSinceMs) > FLP_LINK_AUTH_TIMEOUT_MS){
    L->nAuthFail++;
    L->session = 0;
    setErr(L, "el telefono no completo la autenticacion");
    gotoState(L, FLP_LS_SEARCHING, nowMs);
  }

  // -----------------------------------------------------------
  //  4) Enlace muerto: hace mucho que no llega NADA estando en sesion.
  // -----------------------------------------------------------
  if(L->state == FLP_LS_READY && L->lastRxMs &&
     (uint32_t)(nowMs - L->lastRxMs) > FLP_LINK_DEAD_MS){
    L->session = 0;
    L->nReconnects++;
    if(M){
      M->stats.reconnects++;
      // El estado del telefono deja de ser cierto en cuanto se cae el
      // enlace: se limpia para no pintar una bateria de hace un rato
      // como si fuera de ahora.
      flexPhoneModelClear(M, false);
    }
    setErr(L, "el telefono dejo de responder");
    L->reconnectAttempt = 0;
    gotoState(L, FLP_LS_SEARCHING, nowMs);
  }

  // -----------------------------------------------------------
  //  6) RECEPCION. Un tope por vuelta: si el telefono manda una
  //     rafaga, se reparte entre cuadros en vez de comerse uno.
  // -----------------------------------------------------------
  {
    uint8_t frame[FLNK_MAX_FRAME];
    for(int i = 0; i < FLP_LINK_RX_PER_TICK; i++){
      const int n = flexPhoneTrRecv(L->tr, frame, sizeof(frame));
      if(n <= 0) break;
      flexPhoneLinkOnFrame(L, M, frame, (size_t)n, nowMs);
    }
  }

  // -----------------------------------------------------------
  //  7) ENVIO. Aqui SI se entrega de verdad al transporte.
  // -----------------------------------------------------------
  for(int i = 0; i < FLP_LINK_TXQ; i++){
    FlexPhoneTxMsg* m = &L->tx[i];
    if(!m->used) continue;
    if(m->nextTryMs && nowMs < m->nextTryMs) continue;
    const bool sent = deliver(L, m);
    if(!sent){
      // El canal no pudo. No se descarta todavia: se reintenta con
      // espera progresiva, que es lo que salva un corte de un segundo.
      if(m->attempts >= FLNK_RETRY_MAX){ m->used = false; L->nTimeouts++; continue; }
      const uint32_t d = flexLinkRetryDelayMs(m->attempts);
      m->attempts++;
      m->nextTryMs = nowMs + (d ? d : FLP_LINK_ACK_TIMEOUT_MS);
      continue;
    }
    L->nTx++;
    L->lastTxMs = nowMs ? nowMs : 1;
    // El reloj de la latencia arranca cuando el PING SALE DE VERDAD,
    // no cuando se encola: entre una cosa y otra hay al menos un
    // cuadro, y contarlo como tiempo de red seria inventarse ~16 ms.
    if(m->type == FLNK_T_PING && !L->pingSentMs) L->pingSentMs = nowMs ? nowMs : 1;
    if(!m->needsAck){ m->used = false; continue; }
    if(m->attempts >= FLNK_RETRY_MAX){ m->used = false; L->nTimeouts++; continue; }
    const uint32_t d = flexLinkRetryDelayMs(m->attempts);
    m->attempts++;
    m->nextTryMs = nowMs + (d ? d : FLP_LINK_ACK_TIMEOUT_MS);
  }

  // #############################################################
  // ##  8) LATIDO. Mantiene vivo el canal Y mide la latencia.
  // ##  ------------------------------------------------------
  // ##  TAMBIEN MIENTRAS SE EMPAREJA, y no por simetria: por un
  // ##  fallo concreto.
  // ##
  // ##  Durante el emparejamiento el canal se queda MUDO de
  // ##  verdad: Flex OS manda su sal una vez y se calla, y el
  // ##  telefono no tiene nada que decir hasta que el usuario
  // ##  teclea seis digitos. Eso son cuarenta, cincuenta o setenta
  // ##  segundos de silencio perfectamente normales -- y al otro
  // ##  lado hay un plazo de inactividad de 40 s que da el socket
  // ##  por muerto y lo cierra.
  // ##
  // ##  Resultado: el usuario terminaba de teclear, pulsaba
  // ##  "Emparejar" y leia "se corto la conexion al enviar el
  // ##  codigo" -- con el codigo correcto y la ventana todavia
  // ##  abierta en los dos lados. El emparejamiento moria de
  // ##  silencio.
  // ##
  // ##  El latido cuesta una trama de 18 bytes cada 8 s mientras
  // ##  hay un codigo en pantalla. El canal deja de estar mudo y
  // ##  el plazo de inactividad deja de dispararse.
  // #############################################################
  const bool beats = (L->state == FLP_LS_READY) ||
                     (L->state == FLP_LS_PAIRING) ||
                     (L->state == FLP_LS_AUTH);
  if(beats && !L->pingSentMs && L->lastTxMs &&
     (uint32_t)(nowMs - L->lastTxMs) > FLP_LINK_IDLE_PING_MS && !txHasType(L, FLNK_T_PING)){
    if(flexPhoneLinkSend(L, FLNK_T_PING, NULL, 0, false)) L->pingSeq++;
  }
  // Un PONG que no llega no puede dejar la medida colgada para
  // siempre: pasado el plazo se abandona y se vuelve a intentar.
  if(L->pingSentMs && (uint32_t)(nowMs - L->pingSentMs) > FLP_LINK_ACK_TIMEOUT_MS)
    L->pingSentMs = 0;

  // -----------------------------------------------------------
  //  9) Caducidad por privacidad. No se llama en cada frame: solo
  //     cuando el usuario configuro un plazo.
  // -----------------------------------------------------------
  if(M && M->priv.keepHours){
    const uint32_t maxAge = (uint32_t)M->priv.keepHours * 3600000u;
    flexPhoneNotifExpire(M, nowMs, maxAge);
  }
}
