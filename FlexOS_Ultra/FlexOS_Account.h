#ifndef FLEXOS_ACCOUNT_H
#define FLEXOS_ACCOUNT_H

#include <stddef.h>
#include <stdint.h>

// Estado observable del enlace. Toda la red corre en una tarea FreeRTOS;
// ninguna llamada de esta API bloquea el loop grafico de FlexOS.
enum FlexAccountState : uint8_t {
  FLEX_ACCOUNT_UNLINKED = 0,
  FLEX_ACCOUNT_REQUESTING,
  FLEX_ACCOUNT_CODE_READY,
  FLEX_ACCOUNT_LINKED,
  FLEX_ACCOUNT_EXPIRED,
  FLEX_ACCOUNT_CANCELLED,
  FLEX_ACCOUNT_ERROR
};

struct FlexAccountSnapshot {
  FlexAccountState state;
  uint8_t progress;
  bool linked;
  char stage[72];
  char error[144];
  char code[9];
  char activationUrl[192];
  char flexAddress[48];
  char displayName[64];
};

// Inicializa NVS y la tarea de fondo. No enciende Wi-Fi ni abre una conexion.
void flexAccountBegin();

// Genera una credencial aleatoria dentro del ESP32-P4 y publica UNICAMENTE su
// SHA-256. La credencial real nunca sale del dispositivo durante el enlace.
bool flexAccountRequestCode(const char* deviceLabel);
void flexAccountCancel();

bool flexAccountLinked();
FlexAccountState flexAccountState();
void flexAccountSnapshot(FlexAccountSnapshot* out);

// Copia segura para futuras APIs autenticadas (resenas, soporte, mensajes).
// El llamador debe usar TLS validado; esta funcion nunca devuelve un puntero a
// la memoria interna ni imprime la credencial.
bool flexAccountCopyBearer(char* out, size_t capacity);

// Borra solo la sesion local. La revocacion definitiva se hace desde la web,
// para que perder o reiniciar la pantalla no permita secuestrar el dispositivo.
void flexAccountForgetLocal();

#endif
