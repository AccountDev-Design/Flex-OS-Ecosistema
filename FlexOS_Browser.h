// #############################################################
// ##  FlexOS_Browser.h  ·  NAVEGADOR DE FLEX OS ULTRA
// ##  Nucleo comun a las tres placas: ESP32-P4 (Ultra), ESP32-S3
// ##  (Ultra S3) y ESP32 clasico (Pro).
// #############################################################
//
//  QUE ES ESTO
//  -----------
//  La app "Navegador" del escritorio era hasta ahora una maqueta:
//  navEnter() pintaba una barra de direcciones falsa, tres pestanas
//  decorativas y un globo. Este modulo la convierte en un navegador
//  de verdad.
//
//  POR QUE UNA ARQUITECTURA HIBRIDA (y no "un navegador dentro del chip")
//  ---------------------------------------------------------------------
//  Un motor web moderno (HTML5 + CSS3 + JavaScript + TLS + fuentes +
//  vídeo) son cientos de MB de codigo y necesita cientos de MB de RAM.
//  El ESP32-P4 tiene 32 MB de PSRAM en el mejor caso y el ESP32 clasico
//  ~300 KB de SRAM. No es una cuestion de optimizar: no cabe, y decir
//  lo contrario seria mentir. Asi que el reparto es:
//
//    · EL DISPOSITIVO pone lo que sabe hacer bien y con baja latencia:
//      la interfaz nativa, el tactil, el teclado, las pestanas, el
//      historial, los favoritos, la cache, las paginas internas y el
//      reproductor multimedia.
//    · UN SERVICIO REMOTO aislado (Node.js + Playwright, en server/)
//      ejecuta Chromium de verdad y devuelve la pagina YA RASTERIZADA
//      como JPEG del tamano exacto de la pantalla.
//    · El transporte es HTTPS para el control y WebSocket (wss) para
//      la sesion interactiva.
//
//  El usuario ve texto e imagenes reales de sitios modernos, pulsa
//  enlaces, se desplaza y escribe. Lo que viaja son pixeles hacia
//  abajo y eventos hacia arriba.
//
//  ESTE FICHERO NO DIBUJA NADA. Igual que FlexOS_OTA.cpp, es una
//  unidad de traduccion aparte que no ve las primitivas `static` de
//  los .ino: el dibujo entra por FlexOS_Browser_Bridge.h.
//
//  REPARTO EN FICHEROS
//  -------------------
//    FlexOS_Browser.h          este: API, tipos, protocolo, capacidades
//    FlexOS_Browser.cpp        nucleo PURO y portable (se prueba en PC):
//                              omnibox, validacion de URL/SSRF, paginas
//                              internas, historial, favoritos, ajustes,
//                              codec del protocolo, telemetria
//    FlexOS_BrowserNet.cpp     transporte real (solo ARDUINO): cliente
//                              WebSocket sobre TLS + tarea de red
//    FlexOS_BrowserUI.cpp      interfaz y gestos (solo ARDUINO)
//    FlexOS_Browser_Bridge.h   puente con las primitivas del .ino
//
//  El nucleo se compila TAMBIEN en el PC (tests/host) para poder
//  probar de verdad el analizador del omnibox y el protocolo.

#ifndef FLEXOS_BROWSER_H
#define FLEXOS_BROWSER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// sdkconfig.h ANTES que nada, por el mismo motivo que en FlexOS_OTA.h:
// de ahi salen las macros CONFIG_IDF_TARGET_* con las que se decide la
// plataforma. En el PC no existe, y entonces se compila el perfil de
// pruebas de host.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  #include "sdkconfig.h"
  #define FLEXBR_ON_DEVICE 1
#else
  #define FLEXBR_ON_DEVICE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================
//  1) INTERRUPTORES MAESTROS
//  -------------------------------------------------------------
//  Mismo patron que GLASS_*_ON / KIOSK_ON / FLEXOS_OTA_ON: cada
//  bloque se puede apagar solo, sin tocar los demas, y a 0 el codigo
//  compila a casi nada. Sirven para aislar un problema en la placa
//  sin desmontar la app entera.
// =============================================================
#ifndef FLEXBR_ON
#define FLEXBR_ON            1   // interruptor general de la app
#endif
#ifndef FLEXBR_REMOTE_ON
#define FLEXBR_REMOTE_ON     1   // sesion remota (WebSocket + frames)
#endif
#ifndef FLEXBR_TILES_ON
#define FLEXBR_TILES_ON      1   // regiones sucias ademas de frames completos
#endif
#ifndef FLEXBR_MEDIA_ON
#define FLEXBR_MEDIA_ON      1   // deteccion multimedia + reproductor propio
#endif
#ifndef FLEXBR_TELEMETRY_ON
#define FLEXBR_TELEMETRY_ON  1   // panel local de depuracion (flex://about)
#endif
// TLS. A 1 el transporte exige wss://. Se puede bajar a 0 SOLO para
// pruebas en una red de laboratorio; el codigo avisa en la interfaz.
#ifndef FLEXBR_REQUIRE_TLS
#define FLEXBR_REQUIRE_TLS   1
#endif

// =============================================================
//  2) PLATAFORMA Y PRESUPUESTOS REALES
//  -------------------------------------------------------------
//  No hay una lista de modelos escrita a mano: la plataforma sale de
//  CONFIG_IDF_TARGET_*, igual que en el modulo OTA. Los presupuestos
//  SI son por placa, porque la memoria disponible es radicalmente
//  distinta y fingir lo contrario acaba en un reinicio por falta de
//  heap a mitad de una pagina.
// =============================================================
#if !FLEXBR_ON_DEVICE
  #define FLEXBR_PLAT_HOST      1
  #define FLEXBR_PLAT_NAME      "Host"
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  #define FLEXBR_PLAT_ULTRA     1
  #define FLEXBR_PLAT_NAME      "Flex OS Ultra"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  #define FLEXBR_PLAT_ULTRA_S3  1
  #define FLEXBR_PLAT_NAME      "Flex OS Ultra S3"
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #define FLEXBR_PLAT_PRO       1
  #define FLEXBR_PLAT_NAME      "Flex OS Pro"
#else
  #error "FlexOS Browser: plataforma no soportada"
#endif

#ifndef FLEXBR_PLAT_ULTRA
#define FLEXBR_PLAT_ULTRA    0
#endif
#ifndef FLEXBR_PLAT_ULTRA_S3
#define FLEXBR_PLAT_ULTRA_S3 0
#endif
#ifndef FLEXBR_PLAT_PRO
#define FLEXBR_PLAT_PRO      0
#endif
#ifndef FLEXBR_PLAT_HOST
#define FLEXBR_PLAT_HOST     0
#endif

// -------------------------------------------------------------
//  Presupuesto de memoria por placa.
//
//  DECISION CLAVE: la "cache de pagina" es el JPEG COMPRIMIDO, no el
//  mapa de bits descomprimido. Un frame de 480x800 en RGB565 son
//  768 KB; el mismo frame en JPEG de calidad util ronda 20-60 KB. Al
//  guardar el comprimido y re-decodificar cuando hay que repintar
//  (cerrar un menu, volver del reproductor) se ahorran esos 768 KB
//  enteros, que en el Pro es la diferencia entre funcionar y no
//  arrancar. El coste es CPU en un repintado ocasional, no por frame.
// -------------------------------------------------------------
#if FLEXBR_PLAT_PRO
  #define FLEXBR_KEYFRAME_MAX   28672    // 28 KB  (RAM interna: no hay PSRAM)
  #define FLEXBR_TILE_MAX       0        // sin regiones sucias en esta placa
  #define FLEXBR_MAX_TABS       1
  #define FLEXBR_HIST_MAX       24
  #define FLEXBR_BM_MAX         12
  #define FLEXBR_NET_STACK      6144
  // Suelo de heap interno libre por debajo del cual NI SE INTENTA la
  // sesion remota: TLS (mbedTLS) mas el buffer de recepcion no caben y
  // el resultado seria un reinicio. Se comprueba EN EJECUCION.
  #define FLEXBR_MIN_FREE_HEAP  92000
#elif FLEXBR_PLAT_ULTRA_S3
  #define FLEXBR_KEYFRAME_MAX   131072   // 128 KB en PSRAM
  #define FLEXBR_TILE_MAX       49152    // 48 KB
  #define FLEXBR_MAX_TABS       4
  #define FLEXBR_HIST_MAX       80
  #define FLEXBR_BM_MAX         40
  #define FLEXBR_NET_STACK      8192
  #define FLEXBR_MIN_FREE_HEAP  48000
#else  /* ULTRA (P4) y HOST */
  #define FLEXBR_KEYFRAME_MAX   196608   // 192 KB en PSRAM
  #define FLEXBR_TILE_MAX       65536    // 64 KB
  #define FLEXBR_MAX_TABS       6
  #define FLEXBR_HIST_MAX       120
  #define FLEXBR_BM_MAX         60
  #define FLEXBR_NET_STACK      10240
  #define FLEXBR_MIN_FREE_HEAP  48000
#endif

// -------------------------------------------------------------
//  Capacidades. Se resuelven EN EJECUCION (memoria medida, PSRAM
//  presente, ajustes del usuario) y la interfaz explica por que algo
//  esta desactivado en vez de limitarse a no funcionar.
// -------------------------------------------------------------
enum {
  FLEXBR_CAP_REMOTE   = 1u << 0,   // sesion remota con frames
  FLEXBR_CAP_TILES    = 1u << 1,   // regiones sucias
  FLEXBR_CAP_MEDIA    = 1u << 2,   // reproductor multimedia propio
  FLEXBR_CAP_TABS     = 1u << 3,   // mas de una pestana
  FLEXBR_CAP_TLS      = 1u << 4,   // TLS disponible
  FLEXBR_CAP_PSRAM    = 1u << 5,   // hay PSRAM utilizable
  FLEXBR_CAP_FS       = 1u << 6    // LittleFS montado (historial/favoritos)
};

// =============================================================
//  3) LIMITES DE TEXTO
//  -------------------------------------------------------------
//  Todo son buffers fijos a proposito: ni un String dinamico en las
//  rutas que se ejecutan por frame o por evento tactil. Es la misma
//  regla que sigue FlexOS_FS.h y por el mismo motivo (fragmentacion
//  del heap = tirones en pantalla).
// =============================================================
#define FLEXBR_URL_MAX     512    // URL resuelta
#define FLEXBR_INPUT_MAX   256    // lo que el usuario puede teclear
#define FLEXBR_TITLE_MAX   80
#define FLEXBR_HOST_MAX    128
#define FLEXBR_ERRMSG_MAX  120
#define FLEXBR_TOKEN_MAX   96     // credencial del dispositivo
#define FLEXBR_SERVER_MAX  160    // wss://host:puerto/ruta

// =============================================================
//  4) PROTOCOLO  ·  FBP/1  (Flex Browser Protocol, version 1)
//  -------------------------------------------------------------
//  Binario, pequeno y versionado. Todos los enteros van en
//  LITTLE-ENDIAN (es el orden nativo del ESP32: cero conversiones en
//  el lado con menos CPU). Las cadenas van SIEMPRE con longitud por
//  delante (uint16) y NO llevan terminador: no hay forma de que una
//  cadena sin cerrar arrastre la lectura del resto del mensaje.
//
//  Cabecera de 12 bytes:
//     0  uint8   'F'
//     1  uint8   'B'
//     2  uint8   version (1)
//     3  uint8   tipo
//     4  uint8   flags
//     5  uint8   canal   (0 = sesion, 1..N = pestana)
//     6  uint16  seq
//     8  uint32  longitud de la carga
//    12  carga
//
//  SEPARACION CONTROL / IMAGEN: son tipos distintos sobre la misma
//  conexion, y el receptor puede descartar un FRAME viejo sin perder
//  ni un mensaje de control. El numero de secuencia es lo que permite
//  tirar frames atrasados en vez de dibujarlos tarde.
// =============================================================
#define FBP_MAGIC0        'F'
#define FBP_MAGIC1        'B'
#define FBP_VERSION       1
#define FBP_HDR_SIZE      12
// Tope duro de carga util. Un servidor comprometido no puede pedir al
// dispositivo que reserve mas que esto: se cierra la sesion.
#define FBP_PAYLOAD_MAX   (FLEXBR_KEYFRAME_MAX + 4096)

// ---- Dispositivo -> servicio ----
enum {
  FBP_C_HELLO      = 0x01,
  FBP_C_PING       = 0x02,
  FBP_C_ACK        = 0x03,   // uint16 lastSeq, uint8 pendientes
  FBP_C_NAVIGATE   = 0x10,   // str url
  FBP_C_BACK       = 0x11,
  FBP_C_FORWARD    = 0x12,
  FBP_C_RELOAD     = 0x13,
  FBP_C_STOP       = 0x14,
  FBP_C_POINTER    = 0x20,   // u8 accion, u16 x, u16 y, u8 boton
  FBP_C_SCROLL     = 0x21,   // i16 dx, i16 dy
  FBP_C_KEY        = 0x22,   // u8 accion, u8 codigo, u8 mods, str texto
  FBP_C_VIEWPORT   = 0x23,   // u16 w, u16 h, u8 calidad, u8 perfil, u8 escala%
  FBP_C_TAB_NEW    = 0x30,
  FBP_C_TAB_CLOSE  = 0x31,   // u8 id
  FBP_C_TAB_SELECT = 0x32,   // u8 id
  FBP_C_MEDIA      = 0x40,   // u8 orden, u32 argumento
  FBP_C_REQ_FRAME  = 0x41    // pide un frame COMPLETO (tras un fallo de decodificacion)
};

// ---- Servicio -> dispositivo ----
enum {
  FBP_S_WELCOME    = 0x81,
  FBP_S_STATE      = 0x82,
  FBP_S_FRAME      = 0x83,
  FBP_S_MEDIA      = 0x84,
  FBP_S_ERROR      = 0x85,
  FBP_S_PONG       = 0x86,
  FBP_S_NAVIGATED  = 0x87,
  FBP_S_TABS       = 0x88,
  FBP_S_DOWNLOAD   = 0x89
};

// Acciones de puntero
enum { FBP_PTR_DOWN = 0, FBP_PTR_UP = 1, FBP_PTR_MOVE = 2, FBP_PTR_TAP = 3, FBP_PTR_CANCEL = 4 };
// Acciones de teclado
enum { FBP_KEY_TEXT = 0, FBP_KEY_DOWN = 1, FBP_KEY_UP = 2 };
// Teclas especiales (codigo != 0 con FBP_KEY_DOWN)
enum { FBP_VK_NONE = 0, FBP_VK_ENTER = 1, FBP_VK_BACKSPACE = 2, FBP_VK_TAB = 3,
       FBP_VK_ESC = 4, FBP_VK_UP = 5, FBP_VK_DOWN = 6, FBP_VK_LEFT = 7, FBP_VK_RIGHT = 8 };
// Ordenes al reproductor
enum { FBP_MED_PLAY = 0, FBP_MED_PAUSE = 1, FBP_MED_SEEK = 2, FBP_MED_STOP = 3,
       FBP_MED_VOLUME = 4, FBP_MED_OPEN = 5 };

// Flags de STATE
enum {
  FBP_ST_LOADING  = 1u << 0,
  FBP_ST_CAN_BACK = 1u << 1,
  FBP_ST_CAN_FWD  = 1u << 2,
  FBP_ST_SECURE   = 1u << 3,   // https con certificado valido
  FBP_ST_INSECURE = 1u << 4,   // http plano o certificado con avisos
  FBP_ST_MEDIA    = 1u << 5,   // la pagina tiene multimedia reproducible
  FBP_ST_EDITING  = 1u << 6    // la pagina tiene el foco en un campo de texto
};
// Canal reservado para el flujo del reproductor. Los canales 1..N son
// pestanas; este va aparte para que el video no se confunda nunca con
// un frame de pagina ni compita por el mismo hueco.
#define FBP_CH_MEDIA  200
// Flags de FRAME
enum {
  FBP_FR_KEYFRAME = 1u << 0,   // frame COMPLETO (region = viewport entero)
  FBP_FR_LAST     = 1u << 1    // ultima region de esta actualizacion
};
// Formatos de imagen que el dispositivo declara saber decodificar.
enum { FBP_IMG_JPEG = 1 };

// Codigos de error del servicio (FBP_S_ERROR). Se traducen a texto en
// el dispositivo para que el mensaje siga el idioma del sistema.
enum {
  FBP_ERR_NONE = 0,
  FBP_ERR_AUTH = 1,          // credencial del dispositivo rechazada
  FBP_ERR_BLOCKED = 2,       // destino prohibido (SSRF, esquema, lista negra)
  FBP_ERR_NAV = 3,           // la navegacion fallo (DNS, conexion, 5xx)
  FBP_ERR_TIMEOUT = 4,
  FBP_ERR_LIMIT = 5,         // limite de sesiones/pestanas/ancho de banda
  FBP_ERR_INTERNAL = 6,
  FBP_ERR_CERT = 7,          // certificado del sitio invalido
  FBP_ERR_PROTOCOL = 8,      // mensaje mal formado
  FBP_ERR_EXPIRED = 9        // sesion caducada por inactividad
};

// =============================================================
//  5) ESTADO DE LA APP
// =============================================================
// Vistas del navegador.
enum {
  BRV_PAGE = 0,      // pagina remota (o el hueco de error / offline)
  BRV_INTERNAL,      // pagina interna flex://
  BRV_OMNIBOX,       // omnibox en edicion, con teclado
  BRV_MENU,          // menu propio de Flex OS
  BRV_TABS,          // selector de pestanas
  BRV_MEDIA          // reproductor a pantalla completa
};

// Paginas internas. El orden importa: es el que usa flexBrPageName().
enum {
  BRP_NONE = -1,
  BRP_NEWTAB = 0,
  BRP_HISTORY,
  BRP_BOOKMARKS,
  BRP_SETTINGS,
  BRP_DOWNLOADS,
  BRP_OFFLINE,
  BRP_ABOUT,
  BRP_N
};

// Resultado de resolver lo que el usuario escribio en el omnibox.
enum {
  BRI_EMPTY = 0,     // no habia nada: no se navega
  BRI_INTERNAL,      // pagina interna, NO se envia al servidor
  BRI_URL,           // http(s) validado
  BRI_SEARCH,        // consulta al buscador configurado
  BRI_BLOCKED        // bloqueado por seguridad
};

// Motivos de bloqueo (para poder explicarlo, no solo negarlo).
enum {
  BRB_NONE = 0,
  BRB_SCHEME,        // file://, javascript:, data:, ftp:, ...
  BRB_LOCALHOST,     // localhost, *.local, .internal
  BRB_PRIVATE_IP,    // 10/8, 192.168/16, 127/8, 169.254/16, fc00::/7, ...
  BRB_USERINFO,      // http://usuario:clave@sitio  (riesgo de suplantacion)
  BRB_TOOLONG,       // supera FLEXBR_URL_MAX
  BRB_BADCHARS,      // caracteres de control o codificacion invalida
  BRB_NOHOST         // no hay host reconocible
};

typedef struct {
  int  kind;                       // BRI_*
  int  page;                       // BRP_* si kind == BRI_INTERNAL
  int  block;                      // BRB_* si kind == BRI_BLOCKED
  char url[FLEXBR_URL_MAX];        // resultado final, ya codificado
} BrResolved;

// Perfiles de rendimiento. NO prometen una tasa de frames concreta:
// fijan calidad, escala y cuantos frames se dejan en vuelo. Lo que se
// mide de verdad esta en la telemetria (flex://about).
enum { BRQ_LOWLAT = 0, BRQ_BALANCED = 1, BRQ_DATASAVER = 2, BRQ_N };

typedef struct {
  char     server[FLEXBR_SERVER_MAX];   // wss://host:puerto/v1/session
  char     token[FLEXBR_TOKEN_MAX];     // credencial del dispositivo
  uint8_t  searchEngine;                // 0 DuckDuckGo · 1 Google · 2 Bing · 3 personalizado
  char     searchCustom[FLEXBR_SERVER_MAX];  // plantilla con %s
  uint8_t  profile;                     // BRQ_*
  uint8_t  quality;                     // 20..90 (calidad JPEG que se pide)
  uint8_t  scalePct;                    // 50..100 (escala del viewport remoto)
  bool     allowLocal;                  // permitir IP/localhost (SOLO desarrollo)
  bool     allowInsecure;               // permitir ws:// sin TLS (SOLO desarrollo)
  bool     saveHistory;
  bool     telemetry;
  bool     mediaNative;                 // usar el reproductor propio al detectar video
} BrSettings;

// Una entrada de historial. `visits` y `lastMs` permiten ordenar sin
// guardar un fichero de indice aparte.
typedef struct {
  char     url[FLEXBR_URL_MAX];
  char     title[FLEXBR_TITLE_MAX];
  uint32_t lastMs;
  uint16_t visits;
} BrHistEntry;

typedef struct {
  char url[FLEXBR_URL_MAX];
  char title[FLEXBR_TITLE_MAX];
} BrBookmark;

// Estado de UNA pestana en el dispositivo. El contenido real vive en el
// servicio; aqui solo se guarda lo que la interfaz necesita pintar.
typedef struct {
  bool     used;
  uint8_t  remoteId;                 // id de la sesion remota (canal FBP)
  int      view;                     // BRV_PAGE / BRV_INTERNAL
  int      page;                     // BRP_* cuando view == BRV_INTERNAL
  char     url[FLEXBR_URL_MAX];
  char     title[FLEXBR_TITLE_MAX];
  uint8_t  stateFlags;               // FBP_ST_*
  uint8_t  progress;                 // 0..100
  bool     hasFrame;                 // hay un keyframe valido para repintar
  uint32_t lastUseMs;                // para suspender la mas antigua
} BrTab;

// Telemetria local. Se puede apagar entera (FLEXBR_TELEMETRY_ON = 0 o
// el ajuste del usuario) y no se envia a ningun sitio: solo se muestra
// en flex://about.
typedef struct {
  uint32_t framesOk, framesDropped, framesBad;
  uint32_t bytesIn, bytesOut;
  uint32_t reconnects, decodeMsLast, decodeMsMax;
  uint32_t rttMs;
  uint32_t freeHeap, freePsram, minFreeHeap;
  uint32_t navCount, errCount;
} BrStats;

// Estado del transporte.
enum {
  BRN_OFF = 0,        // no configurado / app cerrada
  BRN_NOWIFI,         // sin Wi-Fi
  BRN_CONNECTING,
  BRN_HANDSHAKE,
  BRN_READY,
  BRN_ERROR
};

// =============================================================
//  6) API DEL NUCLEO PURO  (portable, se prueba en el PC)
// =============================================================

// Deja `st` con los valores por defecto (servidor vacio, DuckDuckGo,
// perfil equilibrado, sin permisos de desarrollo).
void  flexBrSettingsDefaults(BrSettings* st);

// Resuelve lo que el usuario confirmo en el omnibox. Es la funcion mas
// sensible del modulo -- ver el orden exacto en FlexOS_Browser.cpp.
// Nunca concatena texto sin codificar y nunca devuelve una URL mas
// larga de FLEXBR_URL_MAX.
int   flexBrResolveInput(const char* text, const BrSettings* st, BrResolved* out);

// Comprueba una URL ya formada (por ejemplo, la que anuncia el servidor
// tras una redireccion). Devuelve BRB_NONE si es aceptable.
int   flexBrCheckUrl(const char* url, const BrSettings* st);

// Reconoce `flex://<pagina>` y devuelve BRP_* (o BRP_NONE).
int   flexBrInternalPage(const char* url);
const char* flexBrPageName(int page);      // "newtab", "history", ...
const char* flexBrPageTitle(int page);     // titulo legible en espanol

// Codificacion porcentual de un componente de consulta (RFC 3986).
// Devuelve los bytes escritos sin contar el 0 final, o -1 si no cabe.
int   flexBrPercentEncode(const char* in, char* out, size_t outN);

// Extrae el host de una URL http(s) a `out`. false si no hay host.
bool  flexBrUrlHost(const char* url, char* out, size_t outN);

// Etiqueta corta para la barra: host sin "www." y sin esquema.
void  flexBrUrlLabel(const char* url, char* out, size_t outN);

// Texto del motivo de bloqueo, en espanol, listo para pintar.
const char* flexBrBlockReason(int block);
// Texto de un error del servicio.
const char* flexBrErrorText(int code);

// ---- Codec del protocolo (sin red: solo bytes) ----
typedef struct {
  uint8_t  type, flags, channel;
  uint16_t seq;
  uint32_t len;
} FbpHeader;

// Escribe una cabecera en `buf` (>= FBP_HDR_SIZE). Devuelve FBP_HDR_SIZE.
int  fbpWriteHeader(uint8_t* buf, uint8_t type, uint8_t flags, uint8_t channel,
                    uint16_t seq, uint32_t len);
// Lee una cabecera. false si la magia o la version no cuadran, o si la
// longitud declarada supera FBP_PAYLOAD_MAX.
bool fbpReadHeader(const uint8_t* buf, size_t n, FbpHeader* out);

// Constructores de mensajes del cliente. Todos devuelven el total de
// bytes escritos (cabecera incluida) o -1 si no caben en `buf`.
int fbpBuildHello(uint8_t* buf, size_t n, uint16_t seq, const char* token,
                  const char* deviceId, uint16_t w, uint16_t h,
                  uint8_t quality, uint8_t profile, uint32_t caps);
int fbpBuildNavigate(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, const char* url);
int fbpBuildSimple(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, uint8_t type);
int fbpBuildPointer(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                    uint8_t action, uint16_t x, uint16_t y, uint8_t button);
int fbpBuildScroll(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, int16_t dx, int16_t dy);
int fbpBuildKey(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                uint8_t action, uint8_t code, uint8_t mods, const char* text);
int fbpBuildViewport(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch,
                     uint16_t w, uint16_t h, uint8_t quality, uint8_t profile, uint8_t scalePct);
int fbpBuildAck(uint8_t* buf, size_t n, uint16_t seq, uint16_t lastSeq, uint8_t pending);
int fbpBuildMedia(uint8_t* buf, size_t n, uint16_t seq, uint8_t ch, uint8_t cmd, uint32_t arg);

// Lectores de los mensajes del servidor. Devuelven false si la carga
// esta mal formada (y entonces NO se toca la salida).
typedef struct {
  uint8_t  protoVer;
  uint16_t viewW, viewH;
  uint32_t caps;
  uint16_t maxTabs;
  uint32_t maxFrameBytes;
  char     sessionId[40];
} FbpWelcome;
bool fbpParseWelcome(const uint8_t* p, uint32_t n, FbpWelcome* out);

typedef struct {
  uint8_t  flags, progress;
  char     title[FLEXBR_TITLE_MAX];
  char     url[FLEXBR_URL_MAX];
} FbpState;
bool fbpParseState(const uint8_t* p, uint32_t n, FbpState* out);

typedef struct {
  uint16_t x, y, w, h;
  uint8_t  format, flags;
  uint32_t frameId;
  const uint8_t* img;      // apunta DENTRO de la carga: no se copia
  uint32_t imgLen;
} FbpFrame;
bool fbpParseFrame(const uint8_t* p, uint32_t n, FbpFrame* out);

typedef struct {
  uint8_t  kind;           // 0 ninguno · 1 video · 2 audio
  uint32_t durationMs;
  uint16_t w, h;
  char     mime[40];
  char     url[FLEXBR_URL_MAX];
  char     title[FLEXBR_TITLE_MAX];
} FbpMedia;
bool fbpParseMedia(const uint8_t* p, uint32_t n, FbpMedia* out);

typedef struct { uint16_t code; char msg[FLEXBR_ERRMSG_MAX]; } FbpError;
bool fbpParseError(const uint8_t* p, uint32_t n, FbpError* out);

// ---- Utilidades del apreton de manos WebSocket (RFC 6455) ----
//  Viven en el nucleo PORTABLE y no en el transporte para poder
//  probarlas contra los vectores del propio RFC: si el calculo de
//  Sec-WebSocket-Accept estuviera mal, el sintoma en la placa seria
//  "no conecta" sin mas pista, y eso es carisimo de depurar a ciegas.
void flexBrSha1(const uint8_t* data, size_t n, uint8_t out[20]);
int  flexBrBase64(const uint8_t* in, size_t n, char* out, size_t outN);
// Calcula el valor que el servidor DEBE devolver en Sec-WebSocket-Accept
// para la clave `clientKeyB64`. false si no cabe.
bool flexBrWsAccept(const char* clientKeyB64, char* out, size_t outN);
// Trocea "wss://host:puerto/ruta". `tls` sale a true con wss://.
bool flexBrParseWsUrl(const char* url, bool* tls, char* host, size_t hostN,
                      uint16_t* port, char* path, size_t pathN);

// =============================================================
//  7) API DE LA APP  (la que llama el .ino)
//  -------------------------------------------------------------
//  Ciclo de vida completo y simetrico: Begin una vez en setup(),
//  Enter/Exit al abrir y cerrar la app, Tick por frame. Exit LIBERA
//  todo (socket, tarea de red, buffers): es lo que garantiza que
//  cerrar el navegador devuelva la memoria al sistema.
// =============================================================
void flexBrowserBegin();     // setup(): carga ajustes, historial y favoritos
void flexBrowserEnter();     // el usuario abrio la app
void flexBrowserTick();      // una vez por frame mientras la app esta viva
void flexBrowserExit();      // el usuario cerro la app: libera TODO
bool flexBrowserWantsClose();// la app pide cerrarse (el .ino llama a appClose)

// Entrada de texto desde el teclado del sistema (la gestiona el puente).
void flexBrowserKeyText(const char* utf8);
void flexBrowserKeyBackspace();
void flexBrowserKeyEnter();
void flexBrowserKeyCancel();
// true mientras el omnibox esta en edicion y necesita el teclado.
bool flexBrowserKeyboardOpen();
// Texto actual del omnibox (para que el puente lo pinte).
const char* flexBrowserEditText();
// Etiqueta de lo que se esta editando ("Dirección", "Servidor"...).
const char* flexBrowserEditLabel();
// Boton "atras" del sistema. Devuelve true si el navegador lo consumio
// (retrocedio en el historial o cerro una capa); false = cierra la app.
bool flexBrowserHandleSystemBack();

// Consulta de estado para la interfaz y para flex://about.
const BrStats*    flexBrowserStats();
uint32_t          flexBrowserCaps();
int               flexBrowserNetState();
const BrSettings* flexBrowserSettings();
// Motivo legible de una capacidad desactivada ("sin PSRAM", "heap
// insuficiente", "sin servidor configurado"...). NULL si esta activa.
const char*       flexBrowserCapReason(uint32_t cap);

// Guardado diferido: marca los datos como sucios y el modulo decide
// cuando escribir (nunca por frame, nunca por pulsacion).
void flexBrowserFlushPersist(bool force);
// Borra historial, favoritos y cache segun la mascara.
enum { BRCLR_HISTORY = 1, BRCLR_BOOKMARKS = 2, BRCLR_CACHE = 4, BRCLR_ALL = 7 };
void flexBrowserClearData(uint32_t mask);

// =============================================================
//  8) PUENTE  ·  lo que el .ino tiene que definir
//  -------------------------------------------------------------
//  Igual que otaHost*: funciones LIBRES (no punteros a funcion) para
//  que el enlazador las resuelva en compilacion. Las implementa
//  FlexOS_Browser_Bridge.h en una linea cada una.
// =============================================================
#if FLEXBR_ON_DEVICE
// -- contexto --
int      brHostScrW();
int      brHostScrH();
void     brHostContentRect(int* x, int* y, int* w, int* h);  // area util de la app
bool     brHostDark();
bool     brHostGlass();
bool     brHostHosted();          // dentro de una ventana de Modo PC/DeX
bool     brHostOnline();          // hay Wi-Fi asociada
const char* brHostDeviceName();
uint32_t brHostMillis();
// Y del borde superior del panel del teclado, o brHostScrH() si esta
// cerrado. El navegador maqueta contra esto para que el omnibox en
// edicion nunca quede tapado por las teclas.
int      brHostKeyboardTop();

// -- tactil --
// Copia del estado tactil de alto nivel del sistema (struct Touch del
// .ino). El navegador NUNCA escribe en el original: cuando consume un
// gesto llama a brHostConsumeTouch(), que es lo que hace el resto del
// sistema (isla de notificaciones, OTA) para no corromper la maquina
// de estados del tactil.
typedef struct {
  bool down, pressed, released, tap, moved;
  bool swipeUp, swipeDown, swipeLeft, swipeRight;
  int  x, y, startX, startY, dx, dy;
  uint32_t downMs;
} BrTouch;
void brHostGetTouch(BrTouch* t);
void brHostConsumeTouch();

// -- colores del tema (indices BRC_*) --
enum { BRC_PAGE = 0, BRC_WIN, BRC_SURF, BRC_SURF2, BRC_TXT, BRC_TXT2, BRC_MUTE,
       BRC_PRIM, BRC_ONACC, BRC_BORDER, BRC_DIV, BRC_OK, BRC_WARN, BRC_ERR,
       BRC_DANGER, BRC_SEL, BRC_TRACK, BRC_NAV, BRC_N };
uint16_t brHostColor(int idx);
uint16_t brHostRGB(uint8_t r, uint8_t g, uint8_t b);

// -- dibujo --
void brHostFillRect(int x, int y, int w, int h, uint16_t c);
void brHostFillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void brHostDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void brHostFillCircle(int cx, int cy, int r, uint16_t c);
void brHostDrawCircle(int cx, int cy, int r, uint16_t c);
void brHostTriangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c);
void brHostStroke(float x0, float y0, float x1, float y1, float w, uint16_t c);
void brHostText(int x, int y, const char* s, int size, uint16_t c);
void brHostTextC(int cx, int y, const char* s, int size, uint16_t c);
void brHostTextR(int rx, int y, const char* s, int size, uint16_t c);
void brHostTextClip(int x, int y, const char* s, int size, uint16_t c, int maxRight);
int  brHostTextW(const char* s, int size);
void brHostClip(int y0, int y1);
void brHostFlush(int y0, int y1);
// Vuelca una fila de pixeles RGB565 ya decodificados. Es el unico
// camino por el que la pagina remota llega a la pantalla, y recorta
// contra el area de contenido: una pagina externa NO puede pintar
// sobre la barra del sistema ni sobre la interfaz del navegador.
void brHostBlitRow(int x, int y, int w, const uint16_t* px);

// -- memoria --
void* brHostAlloc(size_t n, bool preferPsram);
void  brHostFree(void* p);
size_t brHostFreeHeap();
size_t brHostFreePsram();

// -- persistencia (NVS + LittleFS, ya montados por el sistema) --
bool brHostPrefsGetStr(const char* key, char* out, size_t n, const char* def);
void brHostPrefsPutStr(const char* key, const char* val);
int  brHostPrefsGetInt(const char* key, int def);
void brHostPrefsPutInt(const char* key, int v);
bool brHostFsReady();
int  brHostFileRead(const char* path, void* buf, size_t n);
bool brHostFileWrite(const char* path, const void* buf, size_t n);
bool brHostFileDelete(const char* path);
#endif // FLEXBR_ON_DEVICE

#ifdef __cplusplus
}
#endif

#endif // FLEXOS_BROWSER_H
