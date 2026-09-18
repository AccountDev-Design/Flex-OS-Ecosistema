// #############################################################
// ##  FLEX PHONE TRANSPORT -- por donde viajan las tramas
// ##  ---------------------------------------------------------
// ##  Antes, el enlace de Flex Phone hablaba BLE directamente. En
// ##  el ESP32-P4 eso significaba no hablar con nadie: el chip no
// ##  tiene radio Bluetooth, asi que la aplicacion entera vivia en
// ##  "no disponible".
// ##
// ##  Este fichero separa DOS cosas que estaban pegadas:
// ##
// ##      logica del enlace          <- FlexOS_FlexPhone_Link
// ##            |
// ##      FlexPhoneTransport         <- ESTE FICHERO (interfaz)
// ##            |
// ##      +-----+------+
// ##      |            |
// ##    Wi-Fi         BLE
// ##   (ACTUAL)      (FUTURO)
// ##
// ##  La logica de negocio ya no sabe si debajo hay un socket o una
// ##  caracteristica GATT. Anadir BLE cuando el C6 pueda darlo es
// ##  escribir un segundo transporte, no rehacer Flex Phone.
// ##
// ##  NUCLEO PURO: la INTERFAZ no incluye Arduino ni reserva
// ##  memoria. Las implementaciones concretas (Wi-Fi sobre
// ##  lwIP, o la de pruebas) viven en otros ficheros.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "FlexOS_FlexLink.h"

// ---- Que transporte es ----------------------------------------
enum {
  FLP_TR_NONE = 0,   // ninguno: el enlace no puede mover un byte
  FLP_TR_WIFI,       // TCP sobre la red local  -- EL ACTUAL
  FLP_TR_BLE,        // GATT                    -- preparado, no usado hoy
  FLP_TR_LOOP,       // lazo en memoria         -- solo pruebas de host
};
const char* flexPhoneTransportName(uint8_t kind);

// ---- Estado del transporte ------------------------------------
// Es el estado del CANAL, no el del enlace. Un canal abierto no
// significa sesion abierta: eso lo decide la autenticacion.
enum {
  FLP_TC_DOWN = 0,   // parado
  FLP_TC_SEARCHING,  // buscando al otro extremo (descubrimiento)
  FLP_TC_OPENING,    // abriendo el canal
  FLP_TC_OPEN,       // canal abierto: se pueden mover tramas
  FLP_TC_FAILED,     // fallo real; `status()` dice cual
};

// ---- Codigos de retorno de send/recv ---------------------------
enum {
  FLP_TR_OK      = 0,    // recv: nada que leer / send: aceptado
  FLP_TR_EAGAIN  = -1,   // no cabe ahora, reintenta luego (no es fallo)
  FLP_TR_ECLOSED = -2,   // el canal se ha caido
  FLP_TR_EBAD    = -3,   // argumentos imposibles
};

// #############################################################
// ##  LA INTERFAZ
// ##  ------------------------------------------------------
// ##  NINGUNA de estas funciones puede bloquear. Se llaman desde
// ##  el tick del enlace, que corre en el bucle principal junto al
// ##  tactil y al dibujo: una espera de red aqui congela el
// ##  sistema entero. El trabajo lento vive en la tarea propia de
// ##  cada implementacion.
// #############################################################
typedef struct FlexPhoneTransport {
  uint8_t kind;                                             // FLP_TR_*

  // Arranca el canal. Devuelve false y deja el motivo en status().
  bool        (*start)(struct FlexPhoneTransport* t);
  // Cierra y libera. Idempotente.
  void        (*stop)(struct FlexPhoneTransport* t);
  // FLP_TC_*. Es la unica fuente de verdad del canal.
  uint8_t     (*state)(struct FlexPhoneTransport* t);
  // Carga maxima por trama. NUNCA se emite una trama mayor.
  uint16_t    (*mtu)(struct FlexPhoneTransport* t);
  // Entrega UNA trama completa. FLP_TR_OK, o un FLP_TR_E*.
  int         (*send)(struct FlexPhoneTransport* t, const uint8_t* frame, size_t n);
  // Recoge UNA trama completa. >0 = bytes escritos en `out`.
  // FLP_TR_OK (0) = no habia nada. <0 = fallo.
  int         (*recv)(struct FlexPhoneTransport* t, uint8_t* out, size_t cap);
  // Texto legible del estado o del fallo. Estatico, nunca NULL.
  const char* (*status)(struct FlexPhoneTransport* t);
  // Direccion del otro extremo, para la interfaz. Puede ir vacia.
  void        (*peer)(struct FlexPhoneTransport* t, char* out, size_t outN);

  void* ctx;   // datos privados de la implementacion
} FlexPhoneTransport;

// Transporte nulo: no mueve nada y lo dice. Es el valor por
// defecto para que el enlace nunca tenga punteros a NULL que
// comprobar en cada llamada.
FlexPhoneTransport* flexPhoneTransportNull(void);

// Ayudas seguras: llaman al transporte solo si existe el metodo.
// El enlace las usa en vez de desreferenciar a mano.
bool        flexPhoneTrStart(FlexPhoneTransport* t);
void        flexPhoneTrStop(FlexPhoneTransport* t);
uint8_t     flexPhoneTrState(FlexPhoneTransport* t);
uint16_t    flexPhoneTrMtu(FlexPhoneTransport* t);
int         flexPhoneTrSend(FlexPhoneTransport* t, const uint8_t* frame, size_t n);
int         flexPhoneTrRecv(FlexPhoneTransport* t, uint8_t* out, size_t cap);
const char* flexPhoneTrStatus(FlexPhoneTransport* t);
void        flexPhoneTrPeer(FlexPhoneTransport* t, char* out, size_t outN);

// #############################################################
// ##  COLA DE TRAMAS
// ##  ------------------------------------------------------
// ##  Un anillo de tamano FIJO decidido en compilacion. Lo usan la
// ##  implementacion Wi-Fi (para pasar tramas entre la tarea de red
// ##  y el bucle grafico) y la de pruebas.
// ##
// ##  Por que un anillo y no una lista: en el P4 no puede haber
// ##  reservas dinamicas dentro del camino de recepcion. Si la cola
// ##  se llena, se DESCARTA LA MAS ANTIGUA y se cuenta. Descartar y
// ##  contar es honesto; crecer sin limite es un reinicio esperando
// ##  a que llegue trafico.
// #############################################################
#define FLP_RING_SLOTS   12
#define FLP_RING_FRAME   FLNK_MAX_FRAME

typedef struct {
  uint8_t  buf[FLP_RING_SLOTS][FLP_RING_FRAME];
  uint16_t len[FLP_RING_SLOTS];
  uint8_t  head, tail;      // head = siguiente a leer, tail = siguiente a escribir
  uint32_t nPushed, nPopped, nDropped;
} FlexFrameRing;

void flexRingInit(FlexFrameRing* r);
bool flexRingEmpty(const FlexFrameRing* r);
bool flexRingFull(const FlexFrameRing* r);
// Copia la trama. Si esta lleno tira la MAS ANTIGUA y sigue, para
// que una rafaga vieja no tape lo que acaba de llegar. Devuelve
// false solo si la trama era imposible (vacia o mayor que el hueco).
bool flexRingPush(FlexFrameRing* r, const uint8_t* frame, size_t n);
// Saca la mas antigua. Devuelve los bytes escritos, o 0 si no hay.
size_t flexRingPop(FlexFrameRing* r, uint8_t* out, size_t cap);
