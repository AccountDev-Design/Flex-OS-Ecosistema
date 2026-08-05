// #############################################################
// ##  FlexOS_OTA.h  ·  SISTEMA GLOBAL DE ACTUALIZACIONES OTA
// ##  ------------------------------------------------------
// ##  UNA sola implementacion para todo el ecosistema FlexOS:
// ##
// ##    · Flex OS ULTRA     -> ESP32-P4  · MIPI-DSI 480x800 · PSRAM
// ##    · Flex OS ULTRA S3  -> ESP32-S3  · TFT SPI          · PSRAM
// ##    · Flex OS PRO       -> ESP32     · TFT SPI          · SIN PSRAM
// ##
// ##  La plataforma se detecta SOLA (CONFIG_IDF_TARGET_*): no hay
// ##  que tocar ni un flag al cambiar de placa. Lo unico que se
// ##  adapta por #ifdef son medidas, buffers y memoria; TODA la
// ##  logica (red, JSON, descarga, instalacion, UI) es comun.
// ##
// ##  COMO SE INTEGRA (3 hooks, nada mas — ver FlexOS_OTA_Bridge.h)
// ##  ------------------------------------------------------------
// ##    setup():            flexOtaBegin();
// ##    loop() (temprano):  flexOtaHandleTouch(&ot);   // intercepta
// ##    loop() (al final):  flexOtaRender();           // dibuja
// ##
// ##  POR QUE EXISTE FlexOS_OTA_Bridge.h
// ##  ----------------------------------
// ##  Las primitivas de dibujo de FlexOS (fillRect, drawText,
// ##  present...) son `static` DENTRO de cada .ino, asi que una
// ##  unidad de traduccion aparte -este modulo- no puede llamarlas.
// ##  El puente declara aqui un juego minimo de funciones libres
// ##  (otaHost*) que el .ino define en UNA linea cada una. Se usan
// ##  funciones libres y NO punteros a funcion a proposito: el
// ##  enlazador las resuelve en tiempo de compilacion, asi que no
// ##  hay indireccion en tiempo de ejecucion ni tabla que inicializar.
// ##  El bloque es IDENTICO en las tres placas -> vive en un solo
// ##  fichero incluido una vez por .ino: cero codigo duplicado.
// #############################################################

#ifndef FLEXOS_OTA_H
#define FLEXOS_OTA_H

#include <stdint.h>
#include <stddef.h>

// sdkconfig.h ANTES que nada: de ahi salen las macros CONFIG_IDF_TARGET_*
// con las que este fichero decide la plataforma (bloque 2). Esta cabecera
// la incluye tambien el .ino ANTES de <Arduino.h>, asi que no se puede dar
// por hecho que alguien ya las haya definido: sin este include, la
// deteccion caeria siempre en el #error del final del bloque 2.
#include "sdkconfig.h"

// =============================================================
//  1) VERSION DE FIRMWARE Y ORIGEN DEL MANIFIESTO
//  -------------------------------------------------------------
//  OJO: estas macros van AQUI y no en el .ino. FlexOS_OTA.cpp es
//  una unidad de traduccion INDEPENDIENTE: un #define hecho en el
//  .ino no llega hasta ella. Definirlas en la cabecera es lo unico
//  que garantiza que .ino y .cpp compilan contra el MISMO valor.
//  Todas admiten override por flags de compilacion (-D...).
// =============================================================

// Version instalada. SUBELA EN CADA RELEASE (formato MAJOR.MINOR.PATCH).
#ifndef FLEXOS_FW_VERSION
#define FLEXOS_FW_VERSION   "1.0.0"
#endif

// Manifiesto remoto. Debe ser HTTPS.
#ifndef FLEXOS_OTA_MANIFEST_URL
#define FLEXOS_OTA_MANIFEST_URL  "https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Ecosistema/main/ota/manifest.json"
#endif

// Certificado raiz del servidor (PEM). Si se deja vacio se acepta
// cualquier certificado (setInsecure): comodo para probar, NO para
// produccion. Con un CA definido, un certificado que no valide da
// OTA_ERR_TLS -- que es justo el estado "certificado invalido".
#ifndef FLEXOS_OTA_ROOT_CA
#define FLEXOS_OTA_ROOT_CA  ""
#endif

// Interruptor maestro. A 0, el modulo compila a casi nada (los hooks
// quedan vacios) -- util en Flex OS PRO si el sketch no cabe en el
// esquema de particion con OTA. Mismo patron que GLASS_*_ON o KIOSK_ON.
#ifndef FLEXOS_OTA_ON
#define FLEXOS_OTA_ON       1
#endif

// Comprobacion automatica al arrancar (una sola vez, ~20 s despues
// del boot y solo si hay red). A 0, solo se busca a peticion del usuario.
#ifndef FLEXOS_OTA_AUTOCHECK
#define FLEXOS_OTA_AUTOCHECK  1
#endif

// =============================================================
//  2) DETECCION AUTOMATICA DE PLATAFORMA
//  -------------------------------------------------------------
//  De aqui salen (a) la clave que se lee del manifiesto y (b) el
//  nombre comercial que muestra la interfaz.
// =============================================================
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  #define FLEXOS_OTA_PLAT_ULTRA     1
  #define FLEXOS_OTA_KEY            "flex_os_ultra"
  #define FLEXOS_OTA_PLAT_NAME      "Flex OS Ultra"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  #define FLEXOS_OTA_PLAT_ULTRA_S3  1
  #define FLEXOS_OTA_KEY            "flex_os_ultra_s3"
  #define FLEXOS_OTA_PLAT_NAME      "Flex OS Ultra S3"
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #define FLEXOS_OTA_PLAT_PRO       1
  #define FLEXOS_OTA_KEY            "flex_os_pro"
  #define FLEXOS_OTA_PLAT_NAME      "Flex OS Pro"
#else
  #error "FlexOS OTA: plataforma no soportada (se esperaba ESP32-P4, ESP32-S3 o ESP32 clasico)"
#endif

// -------------------------------------------------------------
//  Presupuesto de memoria por plataforma.
//  El ESP32 clasico NO tiene PSRAM: ~300 KB de SRAM para todo, y
//  150 KB ya se los lleva el framebuffer. Por eso alli los buffers
//  son deliberadamente pequenos. NINGUNA ruta del OTA depende de
//  PSRAM: los buffers son estaticos (BSS) y viven en RAM interna en
//  las tres placas, asi que descargar e instalar funciona igual.
// -------------------------------------------------------------
#if FLEXOS_OTA_PLAT_PRO
  #define FLEXOS_OTA_CHUNK        1024   // bloque de descarga por streaming
  #define FLEXOS_OTA_MANIFEST_MAX 2048   // manifiesto JSON completo
  #define FLEXOS_OTA_TASK_STACK   10240  // pila de la tarea (TLS necesita margen)
  #define FLEXOS_OTA_CHANGELOG_MAX 192
  // Heap libre minimo para intentar el handshake TLS. mbedTLS reserva
  // sus buffers de registro (~36 KB) al conectar; sin este filtro, en la
  // placa sin PSRAM el intento podia morir por falta de memoria. Con el,
  // se reporta OTA_ERR_LOWMEM y no se toca nada.
  #define FLEXOS_OTA_MIN_HEAP     48000
#else
  #define FLEXOS_OTA_CHUNK        4096
  // 16 KB de pila. Antes eran 12 KB y la tarea se quedaba corta de
  // margen: el handshake TLS de mbedTLS y HTTPClient anidan bastantes
  // marcos, y un desbordamiento de pila aqui aparece como un reinicio
  // aleatorio a mitad de la descarga -- exactamente el sintoma que se
  // vio en placa. La marca de agua real se registra por Serial cada
  // 10% (ver otaLogProgress) para poder ajustarlo con datos.
  #define FLEXOS_OTA_TASK_STACK   16384
  #define FLEXOS_OTA_MANIFEST_MAX 4096
  #define FLEXOS_OTA_CHANGELOG_MAX 320
  #define FLEXOS_OTA_MIN_HEAP     56000
#endif

// Tiempos de red (ms). Cualquiera que se agote -> OTA_ERR_TIMEOUT.
#define FLEXOS_OTA_HTTP_TIMEOUT   12000   // conexion + cabeceras
#define FLEXOS_OTA_STALL_TIMEOUT  15000   // sin recibir un solo byte

// Tamano de firmware aceptable (filtro de cordura del manifiesto)
#define FLEXOS_OTA_MIN_BIN        (64u * 1024u)
#define FLEXOS_OTA_MAX_BIN        (8u * 1024u * 1024u)

// =============================================================
//  3) MAQUINA DE ESTADOS
// =============================================================

// Estado del motor OTA. Lo publica la tarea de fondo; lo lee la UI.
enum FlexOtaState : uint8_t {
  OTA_IDLE = 0,      // en reposo, sin informacion
  OTA_CHECKING,      // descargando y leyendo el manifiesto
  OTA_UP_TO_DATE,    // el sistema ya esta al dia
  OTA_AVAILABLE,     // hay version nueva esperando confirmacion
  OTA_CONNECTING,    // abriendo la descarga del firmware
  OTA_DOWNLOADING,   // streaming -> particion OTA
  OTA_VERIFYING,     // validando tamano/integridad (MD5)
  OTA_INSTALLING,    // cerrando la escritura y fijando la particion
  OTA_FINALIZING,    // guardando estado, listo para reiniciar
  OTA_REBOOTING,     // cuenta atras visible antes de esp_restart()
  OTA_ERROR          // fallo: ver flexOtaError()
};

// Causa exacta del fallo. CADA fallo posible tiene su propio codigo:
// nunca se colapsan en un generico, para que la UI diga que ha pasado.
enum FlexOtaError : uint8_t {
  OTA_ERR_NONE = 0,
  OTA_ERR_NO_WIFI,        // sin conexion
  OTA_ERR_LOWMEM,         // no hay heap para el handshake TLS
  OTA_ERR_HTTPS,          // no se pudo abrir la conexion HTTPS
  OTA_ERR_TLS,            // certificado invalido / no valida contra el CA
  OTA_ERR_HTTP_STATUS,    // respuesta != 200 (404: URL inexistente, etc.)
  OTA_ERR_TIMEOUT,        // agotado el tiempo de espera
  OTA_ERR_JSON,           // manifiesto corrupto o ilegible
  OTA_ERR_NO_BLOCK,       // el manifiesto no trae bloque para esta placa
  OTA_ERR_BAD_VERSION,    // version no parseable (no es MAJOR.MINOR.PATCH)
  OTA_ERR_BAD_URL,        // url ausente o no HTTPS
  OTA_ERR_BAD_SIZE,       // tamano fuera de rango / servidor sin Content-Length
  OTA_ERR_NO_PARTITION,   // esquema de particion SIN OTA (falta app1)
  OTA_ERR_BEGIN,          // Update.begin() rechazo la operacion
  OTA_ERR_WRITE,          // error escribiendo en la particion
  OTA_ERR_TRUNCATED,      // la descarga acabo antes de tiempo
  OTA_ERR_CORRUPT,        // Update.end(): MD5/magic invalido
  OTA_ERR_CANCELLED       // el usuario cancelo
};

// Capa de overlay activa. El render OTA es SIEMPRE lo ultimo del
// pipeline grafico y NUNCA escribe en el framebuffer de una app.
enum OverlayState : uint8_t {
  OVERLAY_HIDDEN = 0,
  OVERLAY_NOTIFICATION,   // tarjeta flotante "Nueva actualizacion disponible"
  OVERLAY_CHANGELOG,      // modal con changelog + Cancelar / Actualizar ahora
  OVERLAY_DOWNLOADING,    // pantalla de progreso
  OVERLAY_SETTINGS        // Ajustes -> Sistema -> Actualizaciones
};

// -------------------------------------------------------------
//  Estado tactil que el .ino presta al modulo. Es una copia POD de
//  struct Touch: el modulo LIMPIA los flags que consume y el puente
//  los devuelve a T, de modo que la app de debajo no los ve. Mismo
//  patron de interceptacion que usa hoy notifHandleTouch().
// -------------------------------------------------------------
struct FlexOtaTouch {
  bool down, pressed, released, tap, moved;
  bool swipeUp, swipeDown, swipeLeft, swipeRight;
  int  x, y, startX, startY, dx, dy;
};

// =============================================================
//  4) API PUBLICA  (lo unico que el .ino necesita llamar)
// =============================================================

// Arranca el modulo y crea la tarea FreeRTOS de fondo. Llamar UNA vez
// en setup(), despues de que el grafico y la radio esten listos.
void flexOtaBegin();

// Intercepta el tactil. Devuelve true si el toque era para el OTA (y ya
// ha limpiado en *t los flags consumidos). Llamar TEMPRANO en loop(),
// junto a notifHandleTouch().
bool flexOtaHandleTouch(FlexOtaTouch* t);

// Avanza animaciones y dibuja el overlay activo. Llamar al FINAL de
// loop(), despues de notifTick(): asi el OTA es la ultima capa.
void flexOtaRender();

// --- Acciones de la interfaz -------------------------------------
void flexOtaRequestCheck();      // boton "Buscar actualizacion"
void flexOtaRequestInstall();    // boton "Actualizar ahora"
void flexOtaCancel();            // boton "Cancelar"
void flexOtaOpenSettings();      // Ajustes -> Sistema -> Actualizaciones
void flexOtaCloseOverlay();      // cierra la capa visible

// --- Consultas (no bloqueantes, seguras desde el hilo grafico) ----
FlexOtaState flexOtaState();
FlexOtaError flexOtaError();
OverlayState flexOtaOverlay();
bool         flexOtaOverlayActive();   // hay capa OTA visible
bool         flexOtaBusy();            // hay trabajo de red en curso
const char*  flexOtaLocalVersion();    // "1.0.0"
const char*  flexOtaRemoteVersion();   // "1.2.0" ("" si aun no se sabe)
const char*  flexOtaChangelog();
uint32_t     flexOtaSizeBytes();
uint8_t      flexOtaPercent();
// Texto corto de estado para la fila de Ajustes ("Buscando...", "Al dia",
// "Flex OS 1.2.0 disponible", "Sin conexion"...).
const char*  flexOtaStatusText();

// =============================================================
//  5) PUENTE CON EL ANFITRION  (lo implementa FlexOS_OTA_Bridge.h)
//  -------------------------------------------------------------
//  Son las UNICAS funciones del sistema que el modulo necesita.
//  Todas se definen en una linea dentro del .ino.
// =============================================================

// --- Contexto -----------------------------------------------------
int  otaHostScrW();          // ancho logico  (480 en las tres placas)
int  otaHostScrH();          // alto  logico  (800 Ultra/S3 · 640 Pro)
bool otaHostIsHome();        // el escritorio esta visible y libre
bool otaHostDark();          // gDark   (modo oscuro / claro)
bool otaHostGlass();         // uiGlass (Liquid Glass activado)

// --- Exclusion PANEL <-> FLASH  (arregla el parpadeo cian) ---------
//  Toman el MISMO mutex que el presenter grafico usa alrededor de
//  esp_lcd_panel_draw_bitmap() + espera de fin de DMA.
//
//  Por que hace falta: escribir en la flash SPI obliga al IDF a
//  deshabilitar la cache de los DOS nucleos mientras dura la
//  operacion. Si en ese instante el presenter (Core 0) esta subiendo
//  el framebuffer al panel MIPI-DSI, su DMA se queda sin poder leer y
//  el FIFO del DSI se vacia: el panel pinta un frame basura -- el
//  destello cian de milisegundos que aparecia en cada 1%.
//
//  Tomando este candado alrededor de Update.write(), la escritura en
//  flash espera a que la subida en curso termine, y el presenter no
//  puede arrancar una nueva mientras se escribe. Nunca se solapan.
void otaHostLockPanel();
void otaHostUnlockPanel();

// --- Composicion (NUNCA se dibuja sobre el fb de una app) ----------
void otaHostBeginCompose();              // setBuf(bbuf) + recorte completo
void otaHostEndCompose(int y0, int y1);  // present(y0,y1)
void otaHostRestoreBand(int y0, int y1); // repinta limpio ese tramo del Home
void otaHostRepaint();                   // repintado completo de la pantalla actual

// --- Primitivas de dibujo ------------------------------------------
uint16_t otaHostRGB(uint8_t r, uint8_t g, uint8_t b);
void otaHostFillRect(int x, int y, int w, int h, uint16_t c);
void otaHostFillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void otaHostFillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a);
void otaHostDrawRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void otaHostFillCircle(int cx, int cy, int r, uint16_t c);
void otaHostStroke(float x0, float y0, float x1, float y1, float w, uint16_t c);
void otaHostGlassPanel(int x, int y, int w, int h, int rad, uint16_t tint);
void otaHostText(int x, int y, const char* s, int size, uint16_t c);
void otaHostTextC(int cx, int y, const char* s, int size, uint16_t c);
void otaHostTextR(int rx, int y, const char* s, int size, uint16_t c);
void otaHostTextClip(int x, int y, const char* s, int size, uint16_t c, int maxRight);
int  otaHostTextW(const char* s, int size);
void otaHostClip(int y0, int y1);        // banda de recorte vertical

#endif // FLEXOS_OTA_H
