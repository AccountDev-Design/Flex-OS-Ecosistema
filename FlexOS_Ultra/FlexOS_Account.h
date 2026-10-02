#ifndef FLEXOS_ACCOUNT_H
#define FLEXOS_ACCOUNT_H

#include <stddef.h>
#include <stdint.h>

// Estado observable del ENLACE (el proceso de vincular). Toda la red corre en
// una tarea FreeRTOS; ninguna llamada de esta API bloquea el loop grafico.
enum FlexAccountState : uint8_t {
  FLEX_ACCOUNT_UNLINKED = 0,
  FLEX_ACCOUNT_REQUESTING,
  FLEX_ACCOUNT_CODE_READY,
  FLEX_ACCOUNT_LINKED,      // hay una credencial guardada en NVS (ver FlexAccountLink)
  FLEX_ACCOUNT_EXPIRED,     // el CODIGO de vinculacion caduco (no la sesion)
  FLEX_ACCOUNT_CANCELLED,
  FLEX_ACCOUNT_ERROR
};

// -------------------------------------------------------------
//  ESTADO DEL VINCULO (Flex Community / Flex Cloud)
//  ------------------------------------------------------------
//  "No hay red" NUNCA significa "desvinculado". Una cuenta guardada en NVS
//  sigue vinculada aunque el P4 arranque sin Wi-Fi, aunque el servidor no
//  conteste o aunque el certificado no valide: solo cambia lo que se puede
//  hacer con ella AHORA. Solo dos cosas la desvinculan: que el usuario la
//  olvide en el propio aparato (o restablezca de fabrica) o que no haya
//  ninguna credencial guardada.
//
//    UNLINKED            no hay credencial guardada
//    LINKED              credencial guardada y validada con el servidor
//    LINKED_OFFLINE      credencial guardada; sin Wi-Fi o aun sin validar
//    NETWORK_UNAVAILABLE hay Wi-Fi pero el servicio no contesta (se reintenta)
//    AUTH_REQUIRED       el servidor ya no reconoce la credencial (revocada)
//    TOKEN_EXPIRED       el servidor dice que la credencial caduco
//    ERROR               el almacenamiento seguro no responde
// -------------------------------------------------------------
enum FlexAccountLink : uint8_t {
  FLEX_LINK_UNLINKED = 0,
  FLEX_LINK_LINKED,
  FLEX_LINK_LINKED_OFFLINE,
  FLEX_LINK_NETWORK_UNAVAILABLE,
  FLEX_LINK_AUTH_REQUIRED,
  FLEX_LINK_TOKEN_EXPIRED,
  FLEX_LINK_ERROR
};

struct FlexAccountSnapshot {
  FlexAccountState state;
  uint8_t progress;
  bool linked;                // == hay credencial guardada (no depende de la red)
  char stage[72];
  char error[144];
  char code[9];
  char activationUrl[192];
  char flexAddress[48];
  char displayName[64];
  FlexAccountLink link;       // estado del vinculo (ver arriba)
  char linkDetail[96];        // motivo legible del estado del vinculo
  uint32_t verifiedAgeS;      // segundos desde la ultima validacion (0xFFFFFFFF = nunca en esta sesion)
};

// Inicializa NVS y la tarea de fondo. No enciende Wi-Fi ni abre una conexion.
// Recupera SIEMPRE primero el estado persistente: si habia cuenta, arranca
// como LINKED_OFFLINE y se valida en cuanto haya Wi-Fi.
void flexAccountBegin();

// Genera una credencial aleatoria dentro del ESP32-P4 y publica UNICAMENTE su
// SHA-256. La credencial real nunca sale del dispositivo durante el enlace.
// Si ya habia una cuenta guardada (p. ej. AUTH_REQUIRED) y el nuevo enlace se
// cancela o falla, la cuenta guardada se conserva tal cual.
bool flexAccountRequestCode(const char* deviceLabel);
void flexAccountCancel();

// true si hay credencial guardada (independiente de la red).
bool flexAccountLinked();
// true si la credencial guardada se puede usar contra las APIs (no rechazada).
bool flexAccountUsable();
FlexAccountState flexAccountState();
FlexAccountLink flexAccountLinkState();
void flexAccountSnapshot(FlexAccountSnapshot* out);
// Texto corto y estable para la interfaz ("Conectada", "Sin conexion"...).
const char* flexAccountLinkLabel(FlexAccountLink link);

// Pide validar la credencial con el servidor en cuanto haya red (no bloquea).
void flexAccountRequestValidation();
// Otro modulo (Flex Cloud) recibio un 401 con la credencial: se revalida ya,
// sin desvincular por una sola respuesta.
void flexAccountReportRejected();

// Copia segura para APIs autenticadas (Flex Cloud, resenas, soporte...).
// El llamador debe usar TLS validado; esta funcion nunca devuelve un puntero a
// la memoria interna ni imprime la credencial.
bool flexAccountCopyBearer(char* out, size_t capacity);

// DESVINCULA este aparato: borra la credencial (NVS y RAM) y deja el estado en
// FLEX_ACCOUNT_UNLINKED / FLEX_LINK_UNLINKED, que sigue asi tras reiniciar. Escribe
// en la NVS una sola vez y solo si habia algo que borrar. No es "sin red": una
// cuenta desvinculada no se recupera sola, hay que volver a vincular.
// La revocacion definitiva del dispositivo se hace desde la web de Flex Account,
// para que perder o reiniciar la pantalla no permita secuestrar el dispositivo.
void flexAccountForgetLocal();

// -------------------------------------------------------------
//  Nucleo puro (sin red ni NVS), expuesto para las pruebas de host.
// -------------------------------------------------------------
enum FlexSessionVerdict : uint8_t {
  FLEX_SESSION_OK = 0,
  FLEX_SESSION_AUTH_REQUIRED,
  FLEX_SESSION_TOKEN_EXPIRED,
  FLEX_SESSION_UNAVAILABLE      // red, servidor, TLS o respuesta ilegible: se reintenta
};
// Clasifica la respuesta del servicio de sesion. `httpStatus` < 0 es un fallo
// de transporte. `errorCode` es el campo error.code del JSON (puede ser NULL).
FlexSessionVerdict flexAccountClassify(int httpStatus, const char* errorCode);
// Espera antes del reintento numero `failures` (1, 2, ...): 30 s, 60 s, 2 min,
// 4 min, 8 min y despues 15 min como mucho. Nunca un bucle agresivo.
uint32_t flexAccountBackoffMs(uint8_t failures);
// Longitud util de una cadena leida con Preferences::getString(char*): esa
// llamada devuelve la longitud CON el terminador (semantica de nvs_get_str),
// mientras putString devuelve strlen. Confundirlas era el fallo que borraba la
// vinculacion en cada arranque.
size_t flexNvsStrLen(size_t nvsReturned, const char* buf, size_t cap);

#ifdef FLEXOS_HOST_TEST
// Solo pruebas de host: una vuelta de la tarea de fondo y un "apagado"
// (se pierde la RAM, se conserva la NVS).
void flexAccountTestStep();
void flexAccountTestPowerCycle();
void flexAccountTestSetKey(const uint8_t pub[65]);
#endif

#endif
