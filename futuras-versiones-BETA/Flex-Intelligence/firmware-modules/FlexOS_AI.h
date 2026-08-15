// #############################################################
// ##  FlexOS_AI.h  ·  FLEX INTELLIGENCE  ·  MOTOR Y CLIENTE
// ##  ------------------------------------------------------
// ##  QUE ES
// ##  ------
// ##  El nucleo de Flex Intelligence: la configuracion del
// ##  servidor, el cliente HTTPS, el formato de peticion y
// ##  respuesta, y la LISTA BLANCA de acciones que el servidor
// ##  puede pedir. La cola de trabajos vive en FlexOS_Cowork y
// ##  la interfaz en el .ino; aqui no se dibuja nada.
// ##
// ##  NINGUNA CLAVE VIVE EN ESTE CODIGO
// ##  ---------------------------------
// ##  Mismo criterio que el servicio de Noticias: el firmware
// ##  sale de fabrica SIN servidor configurado y lo dice en
// ##  pantalla. La URL, el identificador de dispositivo y el
// ##  token los escribe el usuario en Ajustes y se guardan en
// ##  NVS (namespace propio "flexos_ai"). El token es revocable
// ##  desde el servidor y borrable desde el dispositivo.
// ##
// ##  TLS DE VERDAD
// ##  -------------
// ##  Sin CA raiz configurada NO SE CONECTA. No hay setInsecure()
// ##  ni un interruptor para saltarse la validacion: por este
// ##  socket viaja texto del usuario y su token, y aceptar
// ##  cualquier certificado convertiria eso en un regalo para
// ##  quien controle la red. Si falta la CA, el estado real es
// ##  AI_ERR_TLS y se dice tal cual.
// ##
// ##  LA IA NUNCA EJECUTA FUNCIONES ARBITRARIAS
// ##  -----------------------------------------
// ##  El servidor puede devolver acciones estructuradas, pero
// ##  solo las de AI_ACT_*, y las destructivas o que salgan del
// ##  dispositivo exigen confirmacion visible del usuario. Una
// ##  accion que no este en la lista se descarta en el parser --
// ##  no llega a la UI siquiera.
// ##
// ##  SOLO ESP32-P4 (Flex OS Ultra)
// ##  -----------------------------
// ##  Este modulo se compila a nada en S3 y en el ESP32 clasico
// ##  (ver FLEXOS_AI_ENABLED abajo): Flex Intelligence es una
// ##  funcion de Flex OS Ultra y no cambia ni un byte del
// ##  firmware de las otras dos placas.
// #############################################################

#ifndef FLEXOS_AI_H
#define FLEXOS_AI_H

#include <stdint.h>
#include <stddef.h>

#include "sdkconfig.h"

// -------------------------------------------------------------
//  PLATAFORMA
//  -------------------------------------------------------------
//  Se decide EN COMPILACION, igual que hace FlexOS_OTA.h. En una
//  placa que no sea el P4 todo el modulo queda vacio y el enlazador
//  no arrastra ni el cliente TLS ni los buffers.
// -------------------------------------------------------------
#ifndef FLEXOS_AI_ENABLED
  #if defined(CONFIG_IDF_TARGET_ESP32P4)
    #define FLEXOS_AI_ENABLED 1
  #else
    #define FLEXOS_AI_ENABLED 0
  #endif
#endif

// La parte de RED se puede apagar sola, sin tocar el resto. Sirve
// para las pruebas de host (no hay WiFiClientSecure en el PC) y para
// una compilacion de diagnostico en la que se quiera el motor y la
// interfaz pero ninguna salida a internet.
#ifndef FLEXOS_AI_NET
  #if defined(ARDUINO) && FLEXOS_AI_ENABLED
    #define FLEXOS_AI_NET 1
  #else
    #define FLEXOS_AI_NET 0
  #endif
#endif

// -------------------------------------------------------------
//  LIMITES DE CONFIGURACION
// -------------------------------------------------------------
#define AI_URL_MAX     160
#define AI_DEVID_MAX    32
#define AI_TOKEN_MAX    96
#define AI_CA_MAX     2048     // CA raiz en PEM (se guarda en NVS)

// Tamanos de trabajo del cliente. Van a PSRAM y solo mientras dura
// la consulta: el modulo no se queda con un buffer de 8 KB reservado
// toda la sesion para usarlo dos veces al dia.
#define AI_REQ_MAX    2048
#define AI_RESP_MAX   4096
#define AI_HTTP_TIMEOUT_MS 20000

// -------------------------------------------------------------
//  ESTADO / ERRORES  (los "estados honestos" del enunciado)
// -------------------------------------------------------------
enum {
  AI_ERR_NONE = 0,
  AI_ERR_NOCFG,     // sin configurar: no hay URL o no hay token
  AI_ERR_NONET,     // sin conexion (o modo avion)
  AI_ERR_TLS,       // sin CA raiz, o el certificado no valida
  AI_ERR_HTTP,      // el servidor respondio mal
  AI_ERR_AUTH,      // token rechazado o revocado
  AI_ERR_PARSE,     // respuesta ilegible
  AI_ERR_TIMEOUT,
  AI_ERR_TOOBIG,
  AI_ERR_MEM,
  AI_ERR_DENIED,    // el permiso necesario esta desactivado
  AI_ERR_DISABLED   // Flex Intelligence apagada en Ajustes
};

// -------------------------------------------------------------
//  PERMISOS  (bits independientes, todos apagados de fabrica)
//  -------------------------------------------------------------
//  Estos permisos son del USUARIO, no del servidor: dicen que puede
//  SALIR del dispositivo. Sin el bit correspondiente, la peticion ni
//  se construye -- se falla con AI_ERR_DENIED antes de tocar la radio.
// -------------------------------------------------------------
#define AI_PERM_TEXT   0x01    // enviar texto (notas, seleccion, errores)
#define AI_PERM_IMAGE  0x02    // enviar imagenes
#define AI_PERM_FILES  0x04    // enviar archivos del almacenamiento
// Flex Vault NO tiene bit aqui a proposito. El contenido protegido no
// se desbloquea con una casilla de Ajustes: exige la boveda abierta,
// el archivo elegido a mano y una confirmacion, cada vez. Ver
// aiVaultConsent* mas abajo.

// -------------------------------------------------------------
//  ACCIONES ESTRUCTURADAS  ·  LISTA BLANCA
//  -------------------------------------------------------------
//  Todo lo que el servidor puede PEDIR que haga el dispositivo. Lo
//  que no este aqui se descarta al leer la respuesta. Las marcadas
//  como "confirma" no se ejecutan sin un toque del usuario.
// -------------------------------------------------------------
enum {
  AI_ACT_NONE = 0,
  AI_ACT_SHOW_TEXT,      // mostrar el resultado (nunca necesita confirmar)
  AI_ACT_CREATE_NOTE,    // crear una nota con el resultado      -> confirma
  AI_ACT_REPLACE_TEXT,   // sustituir el texto de origen         -> confirma
  AI_ACT_APPEND_NOTE,    // anadir al final de una nota          -> confirma
  AI_ACT_OPEN_APP,       // abrir una app del sistema            -> confirma
  AI_ACT_N
};

// true si esa accion exige confirmacion visible del usuario.
bool aiActionNeedsConfirm(uint8_t act);
// Nombre de la accion tal como se muestra en el boton de la tarjeta.
const char* aiActionName(uint8_t act);
// Convierte el nombre que manda el servidor a un codigo de la lista
// blanca. Devuelve AI_ACT_NONE para cualquier cosa no reconocida --
// que es el caso normal y seguro.
uint8_t aiActionFromName(const char* s, size_t n);

// -------------------------------------------------------------
//  CONFIGURACION
// -------------------------------------------------------------
struct AiConfig {
  char    url  [AI_URL_MAX];     // https://... obligatoriamente
  char    devId[AI_DEVID_MAX];
  char    token[AI_TOKEN_MAX];
  uint8_t perms;                 // AI_PERM_*
  bool    enabled;               // Flex Intelligence activada
  bool    autoCorrect;           // el teclado puede autocorregir solo
  bool    localOnly;             // nada sale del dispositivo, pase lo que pase
  uint8_t openMode;              // 0 = panel rapido, 1 = app completa
};

#define AI_OPEN_PANEL 0
#define AI_OPEN_APP   1

// Lee/escribe la configuracion en memoria. La persistencia en NVS la
// hace el .ino (es quien tiene Preferences abierto con su namespace),
// igual que el servicio de Noticias.
AiConfig* aiConfig();
void      aiConfigDefaults();

// true si hay lo minimo para poder consultar: URL https, dispositivo
// y token. Es lo que separa "sin configurar" de "sin conexion".
bool aiConfigured();

// true si la CA raiz esta puesta. Sin ella no se conecta: ver la
// cabecera del fichero.
bool aiHasRootCa();
void aiSetRootCa(const char* pem);
const char* aiRootCa();
size_t aiRootCaLen();

// Valida una URL de servidor antes de guardarla: tiene que empezar
// por https:// y tener host. Devuelve false y no guarda nada si no.
bool aiValidUrl(const char* url);

// Texto del estado actual, para la app y para Ajustes. Nunca inventa:
// si no hay servidor configurado, lo dice.
const char* aiErrorText(uint8_t err);

// -------------------------------------------------------------
//  CONSENTIMIENTO DE FLEX VAULT
//  -------------------------------------------------------------
//  Un permiso de UN SOLO USO. Se concede para un item concreto, con
//  la boveda abierta, tras enseñar al usuario que se va a enviar. Se
//  consume al construir la peticion y CADUCA: al bloquear la
//  pantalla, al suspender o al cerrar Vault, el .ino llama a
//  aiVaultConsentClear() y el permiso desaparece.
// -------------------------------------------------------------
void aiVaultConsentGrant(uint16_t itemId);
void aiVaultConsentClear();
bool aiVaultConsentHas(uint16_t itemId);
uint16_t aiVaultConsentItem();

// -------------------------------------------------------------
//  PETICION
// -------------------------------------------------------------

// Construye el cuerpo JSON de una peticion. NO toca la red: se puede
// probar entero en el PC, que es justo lo que se hace.
//   kind    -> CW_KIND_* de FlexOS_Cowork.h
//   text    -> la entrada (ya es un snapshot, ver Cowork)
//   extra   -> idioma destino, tono... o NULL
// Devuelve los bytes escritos, o 0 si no cabe o si falta un permiso.
// El TOKEN NO VA EN EL CUERPO: viaja en la cabecera Authorization,
// para que no acabe en un registro de peticiones del servidor.
size_t aiBuildRequest(char* out, size_t cap, uint8_t kind,
                      const char* text, size_t textLen, const char* extra);

// Escapa una cadena a JSON dentro de out. Devuelve los bytes escritos
// (sin comillas). Es publica porque la usan las pruebas.
size_t aiJsonEscape(char* out, size_t cap, const char* s, size_t n);

// -------------------------------------------------------------
//  RESPUESTA
// -------------------------------------------------------------
#define AI_RES_TEXT_MAX 1024
#define AI_RES_ARG_MAX   64

struct AiResult {
  uint8_t action;                    // de la lista blanca; AI_ACT_NONE si no la hay
  bool    needsConfirm;
  char    text[AI_RES_TEXT_MAX];     // el texto que se muestra o se aplica
  size_t  textLen;
  char    arg [AI_RES_ARG_MAX];      // destino de la accion (ruta de nota, id de app)
  uint8_t err;
  char    detail[64];                // mensaje del servidor, si lo mando
};

// Lee la respuesta del servidor. Es un lector de JSON acotado y
// tolerante: solo busca las claves que conoce ("text", "action",
// "arg", "error") y no monta un arbol. Devuelve false si no se pudo
// entender nada; en ese caso res->err queda en AI_ERR_PARSE.
//
// TODA accion que no este en la lista blanca se convierte en
// AI_ACT_SHOW_TEXT: el resultado se enseña, no se ejecuta.
bool aiParseResponse(const char* body, size_t n, AiResult* res);

// -------------------------------------------------------------
//  CLIENTE  (solo con FLEXOS_AI_NET; en el PC no existe)
// -------------------------------------------------------------

// Hace la consulta COMPLETA y bloquea hasta terminar. Se llama
// EXCLUSIVAMENTE desde la tarea de fondo de Cowork, nunca desde
// loop() ni desde nada que dibuje. Devuelve AI_ERR_*.
uint8_t aiQuery(uint8_t kind, const char* text, size_t textLen,
                const char* extra, AiResult* res);

// Diagnostico para Ajustes -> "Probar conexion". Misma ruta que
// aiQuery pero con una carga minima.
uint8_t aiSelfTest();

#endif // FLEXOS_AI_H
