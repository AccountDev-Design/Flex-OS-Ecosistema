// #############################################################
// ##  FLEX PHONE LINK -- transporte del enlace con el telefono
// ##  ---------------------------------------------------------
// ##  Esta capa decide COMO viajan las tramas de Flex Link y
// ##  mantiene la maquina de estados del enlace (anuncio,
// ##  emparejamiento, sesion, reconexion, desconexion limpia).
// ##
// ##  LO MAS IMPORTANTE DE ESTE FICHERO ES LO QUE **NO** AFIRMA
// ##  ------------------------------------------------------------
// ##  El ESP32-P4 **NO TIENE RADIO BLUETOOTH**. No es una opinion:
// ##  soc_caps.h del SDK no define SOC_BLE_SUPPORTED para el P4,
// ##  que es exactamente la comprobacion que ya hacia el .ino para
// ##  desactivar el interruptor de BLE en Ajustes.
// ##
// ##  En esta placa el unico camino posible a BLE es el
// ##  co-procesador ESP32-C6, que es tambien quien da el Wi-Fi por
// ##  esp-hosted/SDIO. Y eso exige DOS cosas que este repositorio
// ##  no puede dar por sentadas:
// ##
// ##    1) Firmware "slave" de esp-hosted en el C6 compilado CON
// ##       Bluetooth, exponiendo HCI por el mismo transporte.
// ##    2) Una pila de host BLE (NimBLE) en el P4 configurada
// ##       contra ese HCI remoto.
// ##
// ##  Mientras esas dos condiciones no se cumplan, este modulo
// ##  informa FLP_LINK_CAP_NONE y la aplicacion Flex Phone dice la
// ##  verdad en pantalla: "BLE no disponible en esta placa" con el
// ##  motivo concreto. NO se simula un enlace, NO se inventa un
// ##  telefono conectado y NO se dan por buenas notificaciones que
// ##  nadie ha enviado.
// ##
// ##  El Wi-Fi del C6 NO SE TOCA. Este modulo no reinicializa el
// ##  transporte hosted, no reflashea el C6 y no cambia pines: solo
// ##  se apoya en la pila BLE si el toolchain la ofrece.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "FlexOS_FlexLink.h"
#include "FlexOS_FlexPhone.h"

// =============================================================
//  1) CAPACIDAD REAL DEL HARDWARE  (decidida en compilacion)
// =============================================================
enum {
  FLP_LINK_CAP_NONE = 0,   // no hay forma de hacer BLE en esta placa
  FLP_LINK_CAP_LOCAL,      // el propio chip tiene radio BLE (S3, C3, ESP32)
  FLP_LINK_CAP_HOSTED,     // BLE a traves del co-procesador C6 (esp-hosted)
};

// Devuelve FLP_LINK_CAP_*. Es la unica fuente de verdad: la
// interfaz NO debe deducir la capacidad por su cuenta.
uint8_t     flexPhoneLinkCap(void);
// Motivo legible de por que no hay BLE. Cadena estatica, nunca NULL.
// Se enseria tal cual en la app: el usuario merece saber que falta.
const char* flexPhoneLinkCapReason(void);
// true si el enlace puede intentarse siquiera.
static inline bool flexPhoneLinkAvailable(void){ return flexPhoneLinkCap() != FLP_LINK_CAP_NONE; }

// =============================================================
//  2) ESTADO DEL ENLACE
// =============================================================
enum {
  FLP_LS_UNAVAILABLE = 0, // el hardware no puede (ver flexPhoneLinkCapReason)
  FLP_LS_OFF,             // apagado por el usuario
  FLP_LS_ADVERTISING,     // anunciando, esperando al telefono
  FLP_LS_CONNECTING,      // hay conexion GATT, aun sin sesion Flex Link
  FLP_LS_PAIRING,         // mostrando codigo, esperando confirmacion en AMBOS
  FLP_LS_READY,           // sesion abierta y emparejada: aqui SI hay telefono
  FLP_LS_ERROR,           // fallo real; `err` explica cual
};

#define FLP_LINK_CODE_LEN   6      // codigo de emparejamiento que muestra Flex OS
#define FLP_LINK_ERR_MAX    72
#define FLP_LINK_TXQ        8      // mensajes salientes en vuelo
#define FLP_LINK_TXBUF      512    // carga maxima de un mensaje saliente

// Un mensaje esperando salida. Tamano fijo: la cola no crece.
typedef struct {
  uint8_t  type;
  uint16_t len;
  uint8_t  data[FLP_LINK_TXBUF];
  uint8_t  attempts;
  uint32_t nextTryMs;
  bool     used;
  bool     needsAck;
} FlexPhoneTxMsg;

typedef struct {
  uint8_t  state;
  uint8_t  cap;
  char     err[FLP_LINK_ERR_MAX];
  char     code[FLP_LINK_CODE_LEN + 1];   // codigo visible durante el emparejamiento
  uint16_t session;
  uint16_t mtu;              // MTU BLE NEGOCIADO (no el deseado)
  uint32_t txCounter;        // contador monotono de salida (anti-repeticion)
  uint32_t lastRxMs;
  uint32_t lastTxMs;
  uint32_t stateSinceMs;
  uint8_t  reconnectAttempt;
  uint32_t reconnectAtMs;
  bool     bonded;           // hay bonding BLE guardado
  bool     userConfirmed;    // el usuario confirmo EN FLEX OS
  bool     peerConfirmed;    // el telefono confirmo EN ANDROID
  FlexLinkReasm      reasm;
  FlexLinkAntiReplay anti;
  FlexPhoneTxMsg     tx[FLP_LINK_TXQ];
  // Contadores de diagnostico. NUNCA contenido de mensajes.
  uint32_t nRx, nTx, nBad, nDropped, nTimeouts, nReconnects;
} FlexPhoneLink;

// -------------------------------------------------------------
//  Ciclo de vida. NADA de esto bloquea ni llama a delay().
// -------------------------------------------------------------
void flexPhoneLinkInit(FlexPhoneLink* L);
// Enciende el enlace: anuncia si se puede. Devuelve false y deja el
// motivo en L->err si el hardware no lo permite.
bool flexPhoneLinkStart(FlexPhoneLink* L);
// Desconexion LIMPIA: manda FLNK_T_BYE si hay sesion, cierra y libera.
void flexPhoneLinkStop(FlexPhoneLink* L);
// Olvida el bonding y borra las claves. Tras esto el telefono tiene
// que volver a emparejarse desde cero.
void flexPhoneLinkForget(FlexPhoneLink* L);
// Un paso de la maquina de estados. Se llama UNA VEZ por frame desde
// el bucle principal. No espera respuestas: si no hay nada que
// hacer, vuelve enseguida.
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
// Genera un codigo nuevo y pasa a FLP_LS_PAIRING. `rnd` es una
// fuente de aleatoriedad del llamador (esp_random en la placa) para
// que este modulo siga sin depender de Arduino.
void flexPhoneLinkBeginPairing(FlexPhoneLink* L, uint32_t rnd, uint32_t nowMs);
// El usuario pulso "Confirmar" EN FLEX OS.
void flexPhoneLinkConfirm(FlexPhoneLink* L, uint32_t nowMs);
// El emparejamiento solo se cierra cuando confirman LOS DOS lados.
bool flexPhoneLinkPairComplete(const FlexPhoneLink* L);

// -------------------------------------------------------------
//  Consultas para la interfaz
// -------------------------------------------------------------
const char* flexPhoneLinkStateName(uint8_t st);
// true SOLO si hay sesion abierta de verdad. La interfaz nunca debe
// pintar "conectado" sin preguntar por aqui.
static inline bool flexPhoneLinkReady(const FlexPhoneLink* L){
  return L && L->state == FLP_LS_READY;
}

// -------------------------------------------------------------
//  Tiempos del protocolo
// -------------------------------------------------------------
#define FLP_LINK_ACK_TIMEOUT_MS    3000
#define FLP_LINK_REASM_TIMEOUT_MS  5000
#define FLP_LINK_IDLE_PING_MS      20000
#define FLP_LINK_DEAD_MS           60000   // sin nada recibido -> se da por caido
#define FLP_LINK_PAIR_WINDOW_MS    120000  // el codigo caduca a los 2 minutos
