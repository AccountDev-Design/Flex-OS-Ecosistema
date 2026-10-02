#ifndef FLEXOS_STORAGELINK_H
#define FLEXOS_STORAGELINK_H

// #############################################################
//  FLEX STORAGE · ENLACE CON EL TELEFONO (mitad de placa)
//  ------------------------------------------------------------
//  El telefono emparejado (NVS), el emparejamiento en curso (lo que la
//  pantalla del P4 tiene que aprobar) y la SESION con el telefono que usa
//  Flex Cloud cuando su destino es el telefono. Las decisiones viven en
//  FlexOS_StorageCore (probado en el PC con sanitizers); aqui solo hay NVS,
//  red y un cerrojo.
//
//  QUIEN LLAMA A QUE
//    · setup():           flexStorageBegin() ANTES de flexCloudBegin()
//    · servidor web:      flexStorageOffer / PairBegin / PairPoll
//    · interfaz:          flexStorageInfo / PairDecide / PairCancel /
//                         SetEnabled / Forget (ninguna bloquea)
//    · Flex Cloud:        flexStorageCopyBearer (puede abrir sesion: RED,
//                         unos segundos como mucho: SOLO desde sus tareas)
//
//  SEGURIDAD (docs/FLEX-STORAGE.md, secciones 4 a 6): la clave del
//  emparejamiento no sale nunca del P4; la sesion es un reto-respuesta con
//  autenticacion MUTUA y el token, temporal, solo vale desde la IP del P4.
//  Un telefono que no responde no bloquea nada: se reintenta con espera
//  creciente (2 s ... 60 s), nunca en bucle.
// #############################################################
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_StorageCore.h"

enum FlexStoragePhoneState : uint8_t {
  FSP_NONE = 0,     // no hay telefono emparejado
  FSP_OFF,          // emparejado, pero el usuario lo desconecto (sin olvidarlo)
  FSP_REJECTED,     // el telefono ya no reconoce este Flex OS: hay que volver a emparejar
  FSP_READY         // emparejado y en uso (si RESPONDE lo dice `reachable`)
};

typedef struct {
  uint8_t  state;                 // FSP_*
  char     name[FST_NAME_MAX];
  char     model[FST_MODEL_MAX];
  char     ip[FST_IP_MAX];
  uint16_t port;
  uint8_t  reachable;             // 1 = la ultima conversacion con el telefono salio bien
  uint32_t lastOkAgeS;            // desde la ultima respuesta (0xFFFFFFFF = ninguna en este arranque)
  // Emparejamiento esperando la decision de la persona (la pantalla de aprobacion).
  uint8_t  pairWaiting;
  char     pairName[FST_NAME_MAX];
  char     pairModel[FST_MODEL_MAX];
  char     pairIp[FST_IP_MAX];
  char     sas[7];                // el MISMO codigo que ensena el telefono
  uint8_t  pairLeftS;             // segundos que quedan para decidir
  uint32_t gen;                   // cambia cuando cambia algo de lo de arriba
} FlexStorageInfo;

// Carga el telefono emparejado y elige el destino de Flex Cloud. Llamar
// ANTES de flexCloudBegin() (el diario que se carga depende del destino).
void flexStorageBegin();
// Copia del estado para pintar. No bloquea nunca (si el cerrojo esta ocupado
// -un emparejamiento calculando su ECDH- devuelve la ultima copia).
void flexStorageInfo(FlexStorageInfo* out);
const char* flexStorageP4Id();

// ------------------------------------------------- servidor web (Flex Web)
// Oferta de un solo uso para el navegador que la pide (sesion web valida).
bool flexStorageOffer(char out[FST_HEX32]);
// POST /api/fs/phone/pair y POST /api/fs/phone/pair/<id>: devuelven el estado
// HTTP y dejan en `json` el cuerpo que hay que contestar (contrato de
// AttachClient.kt). Con 429, `*retryS` = segundos para "Retry-After".
int  flexStoragePairBegin(const FstPairReq* req, const char* peerIp, char* json, size_t cap, uint32_t* retryS);
int  flexStoragePairPoll(const char* pairId, const char* proofHex, char* json, size_t cap);

// ------------------------------------------------------------ interfaz
bool flexStoragePairDecide(bool allow);     // la persona aprueba o rechaza en la pantalla
void flexStoragePairCancel();               // se cerro la hoja / se apaga el servidor
bool flexStorageSetEnabled(bool on);        // Desconectar / Volver a conectar (sin olvidar)
void flexStorageForget();                   // Olvidar este telefono: borra la clave de la NVS

// --------------------------------------------------------- Flex Cloud
// Emparejado, activado y no rechazado. No bloquea (estado publicado).
bool flexStoragePhoneUsable();
// FSP_* sin cerrojo (para decidir el estado de la red de Flex Cloud).
uint8_t flexStoragePhoneState();
// "http://<ip>:<puerto>/api/cloud". false si no hay telefono que usar.
bool flexStoragePhoneBase(char* out, size_t cap);
// Token de la sesion con el telefono; si no hay (o caduco) la abre: RED.
// false: `why` = "no_phone", "phone_rejected" o "network" (sin respuesta o
// esperando el siguiente reintento).
bool flexStorageCopyBearer(char* out, size_t cap, char* why, size_t whyCap);
// El telefono contesto 401 a una peticion con el token: no vale, se abrira otra.
void flexStorageSessionRejected();
// Resultado de cada peticion al telefono: estado HTTP (<0 = no contesto). Para
// "Conectado" / "Telefono desconectado" sin preguntar nada mas a la red.
void flexStorageNoteResult(int httpStatus);

#ifdef FLEXOS_HOST_TEST
void flexStorageTestReset();                // se pierde la RAM (la NVS sigue)
#endif

#endif
