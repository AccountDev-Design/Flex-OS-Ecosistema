// #############################################################
// ##  FLEX PHONE  -- modelo de datos del telefono en Flex OS
// ##  ---------------------------------------------------------
// ##  Segunda capa por encima de Flex Link: aqui ya no hay
// ##  tramas, hay NOTIFICACIONES, CONVERSACIONES, MULTIMEDIA y
// ##  ESTADO DEL TELEFONO.
// ##
// ##  Igual que FlexOS_FlexLink: NUCLEO PURO. Sin Arduino, sin
// ##  String, sin reservas dinamicas, sin delay, sin radio. Todo
// ##  vive en estructuras de tamano FIJO decidido en compilacion,
// ##  para que el coste en RAM del P4 sea conocido y constante.
// ##  Se prueba entero en el PC (tests/host/test_flexphone.cpp).
// ##
// ##  LIMITES REALES QUE ESTE MODELO REPRESENTA (no se disimulan):
// ##    - Una notificacion solo se puede responder si Android
// ##      expuso una accion con RemoteInput. Si no, `canReply`
// ##      es false y la interfaz NO ofrece el boton.
// ##    - Sin telefono conectado no hay envio posible: el modelo
// ##      guarda BORRADORES, no mensajes "enviados".
// ##    - El cuerpo puede venir OCULTO por privacidad; entonces
// ##      `hidden` es true y no hay texto que enseriar.
// #############################################################
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "FlexOS_FlexLink.h"

// ---- Presupuesto de memoria (fijo y explicito) ----------------
// La cola es CIRCULAR: al llenarse expulsa, nunca crece. 40 es el
// valor inicial que pide el diseno; subirlo cuesta
// sizeof(FlexPhoneNotif) por hueco (~360 B), asi que 40 -> ~14 KB.
#define FLP_NOTIF_MAX       40
#define FLP_CONV_MAX        12
#define FLP_DRAFT_MAX       8
// Cuantas notificaciones llegan a FLASH. Deliberadamente MENOS que
// las que caben en RAM: el historial completo es una comodidad de
// sesion, no algo que merezca 20 KB de escritura en cada volcado.
// Se guardan las mas RECIENTES; el resto vive solo mientras el
// sistema este encendido.
#define FLP_PERSIST_MAX     20

#define FLP_PKG_MAX         64    // "com.whatsapp"
#define FLP_APPNAME_MAX     32
#define FLP_TITLE_MAX       64
#define FLP_TEXT_MAX        160   // resumen, NO el mensaje entero
#define FLP_ACTION_MAX      4     // acciones por notificacion
#define FLP_ACTLABEL_MAX    24
#define FLP_REPLY_MAX       256   // tope de una respuesta rapida
#define FLP_DEVNAME_MAX     32
#define FLP_MEDIA_TXT_MAX   48

// ---- Categoria y prioridad (mapeadas desde Android) -----------
enum {
  FLP_CAT_OTHER = 0, FLP_CAT_MSG, FLP_CAT_CALL, FLP_CAT_EMAIL,
  FLP_CAT_SOCIAL, FLP_CAT_ALARM, FLP_CAT_TRANSPORT, FLP_CAT_SYS,
  FLP_CAT_N
};
enum { FLP_PRI_MIN = 0, FLP_PRI_LOW, FLP_PRI_DEFAULT, FLP_PRI_HIGH, FLP_PRI_URGENT };

// ---- Una notificacion -----------------------------------------
typedef struct {
  uint32_t id;            // id ESTABLE que asigna el telefono (clave de update/remove)
  char     pkg[FLP_PKG_MAX];
  char     app[FLP_APPNAME_MAX];
  char     title[FLP_TITLE_MAX];
  char     text[FLP_TEXT_MAX];
  uint32_t whenMs;        // hora del telefono (epoch/1000 recortado)
  uint32_t rxMs;          // millis() local de recepcion: para ordenar y caducar
  uint8_t  cat;           // FLP_CAT_*
  uint8_t  pri;           // FLP_PRI_*
  bool     hidden;        // el cuerpo viene oculto por privacidad
  bool     sensitive;     // marcada como sensible (OTP, banca): NUNCA en bloqueo
  bool     canReply;      // Android expuso RemoteInput -> se puede responder DE VERDAD
  uint8_t  replyAction;   // indice de la accion con RemoteInput
  uint8_t  actionCount;
  char     actions[FLP_ACTION_MAX][FLP_ACTLABEL_MAX];
  bool     used;
} FlexPhoneNotif;

// ---- Conversacion (derivada de notificaciones respondibles) ----
typedef struct {
  char     pkg[FLP_PKG_MAX];
  char     who[FLP_TITLE_MAX];    // titulo de la notificacion = remitente
  uint32_t lastNotifId;           // notificacion viva que sostiene la respuesta
  uint32_t lastMs;
  bool     canReply;              // false en cuanto la notificacion se va
  bool     used;
} FlexPhoneConv;

// ---- Borrador local (vive SIN telefono) -----------------------
typedef struct {
  char     pkg[FLP_PKG_MAX];
  char     who[FLP_TITLE_MAX];
  char     text[FLP_REPLY_MAX];
  uint32_t savedMs;
  bool     used;
} FlexPhoneDraft;

// ---- Estado del telefono --------------------------------------
enum { FLP_NET_UNKNOWN = 0, FLP_NET_NONE, FLP_NET_WIFI, FLP_NET_MOBILE };
typedef struct {
  char     name[FLP_DEVNAME_MAX];
  uint8_t  battery;       // 0..100 · 255 = desconocido
  bool     charging;
  uint8_t  net;           // FLP_NET_*
  bool     valid;         // false = nunca hemos recibido estado REAL
  uint32_t stampMs;
} FlexPhoneState;

// ---- Multimedia -----------------------------------------------
enum { FLP_MEDIA_STOP = 0, FLP_MEDIA_PLAYING, FLP_MEDIA_PAUSED };
enum { FLP_MCMD_PLAY = 1, FLP_MCMD_PAUSE, FLP_MCMD_NEXT, FLP_MCMD_PREV };
typedef struct {
  char     title[FLP_MEDIA_TXT_MAX];
  char     artist[FLP_MEDIA_TXT_MAX];
  char     app[FLP_APPNAME_MAX];
  uint8_t  status;        // FLP_MEDIA_*
  bool     valid;
  uint32_t stampMs;
} FlexPhoneMedia;

// ---- Browser Relay --------------------------------------------
enum {
  FLP_RELAY_OFF = 0,      // no pedido
  FLP_RELAY_STARTING,     // pedido, esperando FLNK_T_RELAY_INFO
  FLP_RELAY_UP,           // el telefono dice que escucha en ip:port
  FLP_RELAY_ERROR,        // el telefono contesto un error REAL
  FLP_RELAY_SUSPENDED,    // Android lo suspendio (bateria/memoria)
};
typedef struct {
  uint8_t  state;
  uint8_t  ip[4];
  uint16_t port;
  uint8_t  protoVer;      // version FBP que ofrece el telefono
  bool     tls;
  uint16_t caps;          // mapa de capacidades declaradas
  uint32_t stampMs;
  char     err[64];       // motivo REAL si state == FLP_RELAY_ERROR
} FlexPhoneRelay;

// ---- Privacidad -----------------------------------------------
typedef struct {
  bool hideBodyOnLock;    // en bloqueo: solo app y remitente
  bool hideSensitive;     // nunca mostrar las marcadas sensibles
  bool keepHistory;       // conservar historial al desconectar
  uint16_t keepHours;     // borrado automatico (0 = no borrar por tiempo)
} FlexPhonePrivacy;

// ---- Contadores de diagnostico (NUNCA contenido) --------------
typedef struct {
  uint32_t rxNotif, rxUpdate, rxRemove;
  uint32_t evicted, queueFull;
  uint32_t badPacket, reconnects;
  uint32_t repliesOk, repliesFail;
  uint32_t framesDropped;
} FlexPhoneStats;

// =============================================================
//  ESTADO COMPLETO
// =============================================================
// Una sola instancia global en el firmware. Se declara aqui como
// tipo para que el test de host pueda tener la suya.
typedef struct {
  FlexPhoneNotif  notif[FLP_NOTIF_MAX];
  FlexPhoneConv   conv[FLP_CONV_MAX];
  FlexPhoneDraft  draft[FLP_DRAFT_MAX];
  FlexPhoneState  phone;
  FlexPhoneMedia  media;
  FlexPhoneRelay  relay;
  FlexPhonePrivacy priv;
  FlexPhoneStats  stats;
  uint16_t        notifCount;
  bool            dirty;        // hay cambios sin persistir (escritura agrupada)
  uint32_t        lastSyncMs;
} FlexPhoneModel;

// -------------------------------------------------------------
//  Ciclo de vida
// -------------------------------------------------------------
void flexPhoneModelInit(FlexPhoneModel* m);
// Borra TODO lo que venga del telefono (notificaciones,
// conversaciones, estado, multimedia). Los borradores locales y la
// configuracion de privacidad se conservan salvo `wipeDrafts`.
void flexPhoneModelClear(FlexPhoneModel* m, bool wipeDrafts);

// -------------------------------------------------------------
//  Notificaciones
// -------------------------------------------------------------
// Inserta o ACTUALIZA por id. Devuelve el indice o -1.
// Si la cola esta llena expulsa segun la politica de abajo.
int  flexPhoneNotifPut(FlexPhoneModel* m, const FlexPhoneNotif* n, uint32_t nowMs);
int  flexPhoneNotifFind(const FlexPhoneModel* m, uint32_t id);
bool flexPhoneNotifRemove(FlexPhoneModel* m, uint32_t id);
void flexPhoneNotifClearAll(FlexPhoneModel* m);
// Cuantas hay de un paquete concreto (agrupacion por app).
int  flexPhoneNotifCountPkg(const FlexPhoneModel* m, const char* pkg);
// Politica de expulsion cuando la cola se llena: primero la MAS
// ANTIGUA de PRIORIDAD MAS BAJA. Nunca expulsa una urgente si queda
// alguna de prioridad menor. Devuelve el indice liberado o -1.
int  flexPhoneNotifEvict(FlexPhoneModel* m);
// Borrado por antiguedad (privacidad). Devuelve cuantas quito.
int  flexPhoneNotifExpire(FlexPhoneModel* m, uint32_t nowMs, uint32_t maxAgeMs);

// Que texto puede enseriarse de esta notificacion AHORA, teniendo
// en cuenta bloqueo y privacidad. Deja `out` siempre terminada.
// Devuelve false si no hay nada que mostrar (oculto o sensible).
bool flexPhoneNotifBody(const FlexPhoneModel* m, const FlexPhoneNotif* n,
                        bool onLockScreen, char* out, size_t outN);

// -------------------------------------------------------------
//  Conversaciones y borradores
// -------------------------------------------------------------
// Reconstruye la lista de conversaciones a partir de las
// notificaciones vivas que admiten respuesta.
void flexPhoneConvRebuild(FlexPhoneModel* m);
int  flexPhoneConvFind(const FlexPhoneModel* m, const char* pkg, const char* who);
int  flexPhoneDraftPut(FlexPhoneModel* m, const char* pkg, const char* who,
                       const char* text, uint32_t nowMs);
int  flexPhoneDraftFind(const FlexPhoneModel* m, const char* pkg, const char* who);
bool flexPhoneDraftRemove(FlexPhoneModel* m, int idx);

// -------------------------------------------------------------
//  Iconos: tabla local de paquetes conocidos
// -------------------------------------------------------------
// NO se envian iconos por BLE. Aqui hay una tabla de paquetes
// conocidos -> glifo del sistema; lo desconocido usa el generico.
enum {
  FLP_ICON_GENERIC = 0, FLP_ICON_CHAT, FLP_ICON_MAIL, FLP_ICON_CALL,
  FLP_ICON_SOCIAL, FLP_ICON_MUSIC, FLP_ICON_CALENDAR, FLP_ICON_ALARM,
  FLP_ICON_BANK, FLP_ICON_SHOP, FLP_ICON_N
};
uint8_t     flexPhoneIconFor(const char* pkg, uint8_t cat);
const char* flexPhoneIconName(uint8_t icon);

// -------------------------------------------------------------
//  Codecs de carga Flex Link
// -------------------------------------------------------------
// Cada uno escribe/lee la carga de UN tipo de mensaje. Devuelven
// bytes escritos (>0) / true, o <=0 / false si algo no cuadra.
// NINGUNO confia en las longitudes que vienen del aire.
int  flexPhoneEncNotif(uint8_t* out, size_t outN, const FlexPhoneNotif* n);
bool flexPhoneDecNotif(const uint8_t* in, size_t n, FlexPhoneNotif* out);
int  flexPhoneEncReply(uint8_t* out, size_t outN, uint32_t notifId,
                       uint8_t action, const char* text);
bool flexPhoneDecReplyResult(const uint8_t* in, size_t n,
                             uint32_t* notifId, uint8_t* errCode);
bool flexPhoneDecPhoneState(const uint8_t* in, size_t n, FlexPhoneState* out);
bool flexPhoneDecMedia(const uint8_t* in, size_t n, FlexPhoneMedia* out);
bool flexPhoneDecRelayInfo(const uint8_t* in, size_t n, FlexPhoneRelay* out);
int  flexPhoneEncMediaCmd(uint8_t* out, size_t outN, uint8_t cmd);

// Comprueba una respuesta ANTES de enviarla. Devuelve un codigo
// FLNK_E_* (FLNK_E_NONE = se puede enviar). Aqui es donde se
// impide ofrecer un "Enviar" que no hace nada.
uint8_t flexPhoneReplyCheck(const FlexPhoneModel* m, uint32_t notifId, const char* text);

// -------------------------------------------------------------
//  Persistencia (agrupada)
// -------------------------------------------------------------
// El modelo NO escribe en flash por cada evento: marca `dirty` y
// el llamador vuelca cuando toca. Estas dos funciones solo
// serializan/deserializan a un buffer; quien lo guarde decide
// donde (FlexOS_FS, NVS...).
#define FLP_BLOB_MAGIC 0x464C5031u   // "FLP1"
// Tope del volcado, calculado en COMPILACION a partir de los limites
// de arriba. El buffer del llamador se dimensiona con esto, asi que
// cambiar FLP_PERSIST_MAX o un tamano de campo no puede dejar una
// escritura fuera de rango escondida.
#define FLP_BLOB_PER_NOTIF  (2u + 4u + (1u + FLP_PKG_MAX) + (1u + FLP_APPNAME_MAX) \
                            + (1u + FLP_TITLE_MAX) + (1u + FLP_TEXT_MAX) + 4u + 5u \
                            + FLP_ACTION_MAX * (1u + FLP_ACTLABEL_MAX))
#define FLP_BLOB_PER_DRAFT  ((1u + FLP_PKG_MAX) + (1u + FLP_TITLE_MAX) \
                            + (1u + 255u) + 4u)
#define FLP_BLOB_CAP        (12u + FLP_PERSIST_MAX * FLP_BLOB_PER_NOTIF \
                            + 2u + FLP_DRAFT_MAX * FLP_BLOB_PER_DRAFT)
// Tamano del volcado en el PEOR caso (== FLP_BLOB_CAP; funcion para
// que el test lo compruebe contra la macro).
size_t flexPhoneBlobMaxSize(void);
// Devuelve bytes escritos o 0 si no cabe.
size_t flexPhoneSerialize(const FlexPhoneModel* m, uint8_t* out, size_t outN);
// true si el blob es coherente (magia, version y longitudes).
bool   flexPhoneDeserialize(FlexPhoneModel* m, const uint8_t* in, size_t n);
