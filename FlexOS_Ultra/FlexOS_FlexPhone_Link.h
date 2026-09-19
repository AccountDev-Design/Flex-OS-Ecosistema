// #############################################################
// ##  FLEX PHONE LINK -- maquina de estados del enlace
// ##  ---------------------------------------------------------
// ##  Esta capa mantiene el enlace con el telefono: descubrimiento,
// ##  emparejamiento, autenticacion, sesion, latido, reconexion y
// ##  desconexion limpia.
// ##
// ##  POR DONDE VIAJAN LAS TRAMAS YA NO SE DECIDE AQUI
// ##  ------------------------------------------------------------
// ##  Antes este fichero era "el transporte BLE", y en el ESP32-P4
// ##  eso significaba no hablar con nadie: el chip no tiene radio
// ##  Bluetooth. La aplicacion entera vivia en "no disponible".
// ##
// ##  Ahora el canal es un FlexPhoneTransport (ver
// ##  FlexOS_FlexPhone_Transport.h). Hoy el que se usa es Wi-Fi.
// ##  BLE sigue siendo posible el dia que el co-procesador C6 lo
// ##  ofrezca, y para entonces sera OTRO transporte, no otra version
// ##  de este fichero.
// ##
// ##  QUE SIGUE SIENDO CIERTO
// ##    · Nada de aqui bloquea ni llama a delay().
// ##    · Nada de aqui reserva memoria.
// ##    · Nada de aqui habla con una radio: solo con la interfaz
// ##      del transporte. Por eso se prueba entero en el PC.
// ##
// ##  SEGURIDAD -- y el limite real
// ##  ------------------------------------------------------------
// ##  BLE autenticaba y cifraba por debajo con su bonding. Un socket
// ##  TCP en la red local no hace nada de eso, asi que la
// ##  autenticacion la pone este modulo: reto-respuesta MUTUO sobre
// ##  la clave del vinculo (FlexOS_FlexAuth.h). Sin sesion
// ##  autenticada no se acepta ni una notificacion.
// ##
// ##  Lo que esto NO da: la carga viaja EN CLARO por la red local.
// ##  Impide que un dispositivo NO emparejado abra sesion; no
// ##  protege frente a quien ya este escuchando la misma red. La
// ##  interfaz lo dice tal cual, no se anuncia como cifrado.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "FlexOS_FlexLink.h"
#include "FlexOS_FlexAuth.h"
#include "FlexOS_FlexPhone.h"
#include "FlexOS_FlexPhone_Transport.h"

// =============================================================
//  1) CAPACIDAD DE TRANSPORTE  (que caminos existen en esta placa)
// =============================================================
// Se mantiene el nombre historico porque lo usa la interfaz, pero
// ya NO habla solo de BLE: dice si el enlace puede intentarse.
enum {
  FLP_LINK_CAP_NONE = 0,   // ningun transporte posible
  FLP_LINK_CAP_WIFI,       // Wi-Fi disponible  -- EL CAMINO ACTUAL
  FLP_LINK_CAP_BLE_LOCAL,  // el propio chip tiene radio BLE
  FLP_LINK_CAP_BLE_HOSTED, // BLE a traves del co-procesador C6
};

// Devuelve FLP_LINK_CAP_*. Unica fuente de verdad: la interfaz NO
// debe deducir la capacidad por su cuenta.
uint8_t     flexPhoneLinkCap(void);
// Motivo legible. Cadena estatica, nunca NULL. Se ensena tal cual.
const char* flexPhoneLinkCapReason(void);
static inline bool flexPhoneLinkAvailable(void){ return flexPhoneLinkCap() != FLP_LINK_CAP_NONE; }

// #############################################################
// ##  BLE: QUE HAY Y QUE FALTA
// ##  ------------------------------------------------------
// ##  El ESP32-P4 NO tiene radio Bluetooth: soc_caps.h del SDK no
// ##  define SOC_BLE_SUPPORTED para el P4. En esta placa el unico
// ##  camino a BLE es el C6, y eso exige dos cosas que este
// ##  repositorio no puede dar por hechas:
// ##    1) firmware esp-hosted en el C6 compilado CON Bluetooth,
// ##    2) una pila NimBLE en el P4 contra ese controlador remoto.
// ##
// ##  Nada de esto se elimina y nada se finge. La arquitectura deja
// ##  el hueco listo (un segundo FlexPhoneTransport) y esta funcion
// ##  dice la verdad mientras tanto.
// #############################################################
bool        flexPhoneBleReady(void);
const char* flexPhoneBleReason(void);

// =============================================================
//  2) ESTADO DEL ENLACE
// =============================================================
enum {
  FLP_LS_UNAVAILABLE = 0, // no hay transporte posible
  FLP_LS_OFF,             // apagado por el usuario
  FLP_LS_SEARCHING,       // buscando al telefono en la red
  FLP_LS_CONNECTING,      // canal abierto, aun sin sesion
  FLP_LS_PAIRING,         // mostrando codigo, esperando confirmacion en AMBOS
  FLP_LS_AUTH,            // reto-respuesta en curso
  FLP_LS_READY,           // sesion abierta y autenticada: aqui SI hay telefono
  FLP_LS_ERROR,           // fallo real; `err` explica cual
};

#define FLP_LINK_CODE_LEN   6      // codigo de emparejamiento que muestra Flex OS
#define FLP_LINK_ERR_MAX    96
#define FLP_LINK_TXQ        8      // mensajes salientes en vuelo
#define FLP_LINK_TXBUF      512    // carga maxima de un mensaje saliente
#define FLP_LINK_PEER_MAX   40     // "192.168.1.34:47820"

// Un mensaje esperando salida. Tamano fijo: la cola no crece.
typedef struct {
  uint8_t  type;
  uint16_t len;
  uint8_t  data[FLP_LINK_TXBUF];
  uint8_t  attempts;
  uint32_t nextTryMs;
  uint16_t packet;        // id del mensaje: sus fragmentos lo comparten
  bool     used;
  bool     needsAck;
} FlexPhoneTxMsg;

// #############################################################
// ##  VINCULO GUARDADO
// ##  ------------------------------------------------------
// ##  Lo que sobrevive a un reinicio. La CLAVE no se ensena nunca
// ##  en la interfaz, no se escribe en los registros y no sale por
// ##  el enlace: solo se usa para calcular pruebas.
// #############################################################
typedef struct {
  uint8_t key[FLXA_KEY_SIZE];
  char    peerId[FLP_PEERID_MAX];    // identificador del telefono
  char    peerName[FLP_DEVNAME_MAX]; // nombre legible, para la interfaz
  bool    valid;
  uint32_t pairedAtEpoch;            // cuando se emparejo (0 = desconocido)
} FlexPhoneBond;

typedef struct {
  uint8_t  state;
  uint8_t  cap;
  char     err[FLP_LINK_ERR_MAX];
  char     code[FLP_LINK_CODE_LEN + 1];   // codigo visible durante el emparejamiento
  uint16_t session;
  uint16_t mtu;              // carga maxima REAL del transporte
  uint16_t txPacket;         // id del proximo mensaje saliente
  uint32_t txCounter;        // contador monotono de salida (anti-repeticion)
  uint32_t lastRxMs;
  uint32_t lastTxMs;
  uint32_t stateSinceMs;
  uint8_t  reconnectAttempt;
  uint32_t reconnectAtMs;
  bool     userConfirmed;    // el usuario confirmo EN FLEX OS
  bool     peerConfirmed;    // el telefono demostro la clave

  // -- identidad y material del vinculo --
  char     selfId[FLXA_ID_MAX];       // id de este Flex OS
  FlexPhoneBond bond;                 // vinculo guardado
  uint8_t  salt[FLXA_SALT_SIZE];      // sal del emparejamiento en curso
  uint8_t  nonce[FLXA_NONCE_SIZE];    // reto de la sesion en curso
  uint8_t  pendKey[FLXA_KEY_SIZE];    // clave derivada, aun sin confirmar
  bool     pendKeyOk;
  char     pendPeerId[FLP_PEERID_MAX];
  bool     hostProven;                // Flex OS ya mando su prueba
  // ¿Ya nos hemos presentado en EL CANAL QUE ESTA ABIERTO AHORA?
  //
  // No se puede deducir del estado del enlace. El usuario pulsa
  // "Emparejar telefono" y ese boton enciende el enlace y empieza el
  // emparejamiento en la misma vuelta, mientras el transporte todavia
  // esta buscando el telefono por la red. Cuando el canal se abre unos
  // segundos despues, el enlace ya esta en EMPAREJANDO, no en
  // "buscando" -- y atar el saludo a un par de estados concretos
  // dejaba el apreton de manos sin arrancar nunca.
  bool     helloSent;

  // -- latencia REAL, medida con PING/PONG --
  uint32_t pingSentMs;       // 0 = no hay ping en vuelo
  uint16_t rttMs;            // 0xFFFF = aun no medida
  uint32_t pingSeq;

  FlexLinkReasm      reasm;
  FlexLinkAntiReplay anti;
  FlexPhoneTxMsg     tx[FLP_LINK_TXQ];
  FlexPhoneTransport* tr;    // NUNCA NULL: por defecto, el transporte nulo

  // Contadores de diagnostico. NUNCA contenido de mensajes.
  uint32_t nRx, nTx, nBad, nDropped, nTimeouts, nReconnects, nAuthFail;
} FlexPhoneLink;

// -------------------------------------------------------------
//  Ciclo de vida. NADA de esto bloquea ni llama a delay().
// -------------------------------------------------------------
void flexPhoneLinkInit(FlexPhoneLink* L);
// Fija la identidad de este Flex OS. Entra en la derivacion de la
// clave, asi que cambiarla invalida los vinculos existentes.
void flexPhoneLinkSetIdentity(FlexPhoneLink* L, const char* selfId);
// Conecta un transporte. Se puede cambiar con el enlace parado; con
// el enlace vivo, primero se para. NULL restaura el transporte nulo.
void flexPhoneLinkSetTransport(FlexPhoneLink* L, FlexPhoneTransport* tr);
// Carga un vinculo guardado (de flash). Sin esto, tras reiniciar hay
// que volver a emparejar.
void flexPhoneLinkSetBond(FlexPhoneLink* L, const FlexPhoneBond* b);
static inline bool flexPhoneLinkBonded(const FlexPhoneLink* L){
  return L && L->bond.valid;
}

// Enciende el enlace. Devuelve false y deja el motivo en L->err si
// no hay transporte posible.
bool flexPhoneLinkStart(FlexPhoneLink* L);
// Desconexion LIMPIA: manda FLNK_T_BYE si hay sesion, cierra y libera.
void flexPhoneLinkStop(FlexPhoneLink* L);
// Olvida el vinculo y borra la clave. Tras esto el telefono tiene
// que volver a emparejarse desde cero.
void flexPhoneLinkForget(FlexPhoneLink* L);
// Un paso de la maquina de estados. Se llama UNA VEZ por frame desde
// el bucle principal. Mueve las tramas del transporte en los dos
// sentidos y no espera a nadie.
void flexPhoneLinkTick(FlexPhoneLink* L, FlexPhoneModel* M, uint32_t nowMs);

// -------------------------------------------------------------
//  Envio
// -------------------------------------------------------------
// Encola un mensaje. Devuelve false si la cola esta llena (y lo
// cuenta) o si no hay sesion. NO bloquea esperando confirmacion.
bool flexPhoneLinkSend(FlexPhoneLink* L, uint8_t type,
                       const uint8_t* payload, size_t len, bool needsAck);

// -------------------------------------------------------------
//  Recepcion
// -------------------------------------------------------------
// Entra UNA trama cruda tal como llega del transporte. Hace todo el
// camino: validar, anti-repeticion, reensamblar y aplicar al modelo.
// Devuelve true si la trama era valida y se acepto.
bool flexPhoneLinkOnFrame(FlexPhoneLink* L, FlexPhoneModel* M,
                          const uint8_t* frame, size_t n, uint32_t nowMs);

// -------------------------------------------------------------
//  Emparejamiento
// -------------------------------------------------------------
// Genera sal y codigo nuevos y pasa a FLP_LS_PAIRING. `rnd` es la
// fuente de aleatoriedad del llamador (esp_random en la placa) para
// que este modulo siga sin depender de Arduino.
void flexPhoneLinkBeginPairing(FlexPhoneLink* L, FlexAuthRandFn rnd, uint32_t nowMs);
// El usuario pulso "Confirmar" EN FLEX OS. Deriva la clave con el
// codigo que se esta ensenando y queda a la espera de la prueba.
void flexPhoneLinkConfirm(FlexPhoneLink* L, uint32_t nowMs);
// El emparejamiento solo se cierra cuando confirma el usuario Y el
// telefono demuestra que tiene la misma clave.
bool flexPhoneLinkPairComplete(const FlexPhoneLink* L);

// -------------------------------------------------------------
//  Consultas para la interfaz
// -------------------------------------------------------------
const char* flexPhoneLinkStateName(uint8_t st);
// true SOLO si hay sesion abierta y autenticada. La interfaz nunca
// debe pintar "conectado" sin preguntar por aqui.
static inline bool flexPhoneLinkReady(const FlexPhoneLink* L){
  return L && L->state == FLP_LS_READY;
}
// Latencia medida. Devuelve false si todavia no hay ninguna: la
// interfaz debe ensenar "--", no un numero inventado.
bool flexPhoneLinkLatency(const FlexPhoneLink* L, uint16_t* outMs);
// Direccion del telefono, tal como la ve el transporte. Puede ir
// vacia si el transporte no la conoce.
void flexPhoneLinkPeer(FlexPhoneLink* L, char* out, size_t outN);

// -------------------------------------------------------------
//  Tiempos del protocolo
// -------------------------------------------------------------
#define FLP_LINK_ACK_TIMEOUT_MS    3000
#define FLP_LINK_REASM_TIMEOUT_MS  5000
#define FLP_LINK_IDLE_PING_MS      8000    // latido: tambien mide la latencia
#define FLP_LINK_DEAD_MS           30000   // sin nada recibido -> se da por caido
#define FLP_LINK_PAIR_WINDOW_MS    120000  // el codigo caduca a los 2 minutos
#define FLP_LINK_AUTH_TIMEOUT_MS   10000   // el apreton de manos no espera para siempre
// Cuantas tramas se recogen del transporte en UNA vuelta. Existe un
// tope porque el tick corre en el bucle grafico: si el telefono
// manda una rafaga, se reparte entre varios cuadros en vez de
// comerse uno entero y perder frames.
#define FLP_LINK_RX_PER_TICK       6
