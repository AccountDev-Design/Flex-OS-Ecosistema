// #############################################################
// ##  FlexOS Ultra  ·  ESP32-P4  ·  GUITION JC4880P443C_I_W
// ##  MIPI-DSI 480x800 (ST7701)  ·  GT911 tactil  ·  NATIVO
// #############################################################
//
//  QUE ES ESTE ARCHIVO
//  -------------------
//  Sistema operativo NUEVO, escrito DESDE CERO, a resolucion
//  NATIVA 480x800 (sin el modo puente 320x480 de ArduOS).
//
//  Lo UNICO que se reutiliza de ArduOS Z Ultra Pro v3.45-P4 es
//  la CAPA DE HARDWARE, porque son datos del fabricante que no
//  tiene sentido "reinventar" (y que ya estan probados en tu
//  placa). En concreto:
//
//    1) ENCENDER LA PANTALLA  -> flexPanelInit()
//         LDO canal 3 @2.5V (PHY MIPI) + bus DSI 2 lanes @500Mbps
//         + panel DPI 480x800 @34MHz + tabla DCS del ST7701.
//    2) MOSTRAR COLORES       -> flush MIPI-DSI sincronizado:
//         framebuffer en PSRAM + DMA2D, esperando el callback de fin
//         antes de reutilizar memoria (una sola resolucion, sin escalado).
//    3) HACER FUNCIONAR EL TACTIL -> gt* + flexTouchInit()
//         GT911 por I2C (SDA=7, SCL=8, RST=3), coords 0..479 x
//         0..799 ya calibradas de fabrica -> se usan DIRECTAS.
//
//  TODO LO DEMAS (motor grafico de alto nivel, fuentes, iconos,
//  gestos, arranque, OOBE, bloqueo, home, apps, ajustes...) es
//  original de FlexOS Ultra y NO proviene de ArduOS.
//
//  ENTORNO (verificado contra los ejemplos del fabricante, no lo cambies):
//    Arduino IDE 2.3.10 · core arduino-esp32 v3.2.1 EXACTO
//    Board: ESP32P4 Dev Module · 360MHz · Flash 80MHz/QIO/16MB
//    PSRAM: Enabled · USB Mode: USB-OTG (TinyUSB)
//    Particion: cualquiera con zona de datos. FlexOS_FS monta
//               LittleFS sobre ella probando las etiquetas
//               habituales (spiffs / littlefs / ffat / storage),
//               asi que sirve el esquema que ya tengas elegido.
//               Ahi viven los dibujos de Paint y las notas.
//
//  DEPURACION SIN PC (trabajas solo desde el movil):
//    Si algo peta antes de dibujar, el motivo del ultimo reinicio
//    se muestra en una BANDA FORENSE en pantalla al bootear (abajo
//    del todo). Ver showBootBanner().
//
//  ESTADO: Milestone 1 (arranque + OOBE + bloqueo + escritorio).
//  Las apps y Ajustes llegan en los siguientes milestones. Ver el
//  bloque "HOJA DE RUTA" al final del archivo.
// #############################################################

// #############################################################
// ##  MAPA DEL SKETCH  ·  este .ino es el ORQUESTADOR
// ##  ----------------------------------------------------------
// ##  Aqui quedan solo cuatro cosas: las librerias que usa todo el
// ##  sistema, la lista de MODULOS en el orden en que se incluyen,
// ##  los puentes de los subsistemas externos y, al final, setup()
// ##  y loop(). Todo lo demas -- interfaces, apps y utilidades --
// ##  vive en los archivos FlexOS_Ultra_*.h de esta misma carpeta.
// ##
// ##  POR QUE CABECERAS Y NO .cpp
// ##  El sistema comparte cientos de variables y funciones `static`
// ##  de ambito de fichero. Repartirlo en unidades de traduccion
// ##  separadas obligaria a declarar cada una con `extern`, a
// ##  duplicar prototipos y a renunciar a `static`: mucha superficie
// ##  para un fallo de enlazado y ningun beneficio real en una placa
// ##  que compila el sketch entero de una vez. Con cabeceras el
// ##  resultado es UNA sola unidad de traduccion, byte a byte el
// ##  mismo codigo que antes de separarlo, y el IDE de Arduino lo
// ##  compila sin ningun ajuste. Es ademas el patron que este
// ##  proyecto ya usaba (FlexOS_Jumper.h, FlexOS_*_Bridge.h).
// ##
// ##  REGLAS DE LOS MODULOS
// ##    1. Cada modulo lleva `#pragma once` e incluye AL ANTERIOR de
// ##       la lista: la cadena es lineal, asi que el orden queda
// ##       garantizado y no puede haber dependencias circulares.
// ##    2. Cada global se DEFINE una sola vez, en su modulo. No hay
// ##       `extern` ni definiciones repetidas.
// ##    3. El orden de esta lista es el orden en que el codigo estaba
// ##       en el .ino: una funcion `static` sigue definiendose antes
// ##       de usarse, que es lo que hace que esto compile igual.
// ##    4. Anadir un modulo = crearlo, encadenarlo al anterior y
// ##       ponerlo en esta lista, en su sitio. tests/host lo verifica
// ##       (check_wiring.py comprueba que no quede ninguno suelto).
// #############################################################

#include <Wire.h>
#include <Preferences.h>
#if __has_include("esp_arduino_version.h")
  #include "esp_arduino_version.h"
#else
  // Solo lo usa el compilador de pruebas del PC. En una compilacion Arduino
  // real la cabecera existe y aporta la version verdadera del core.
  #define ESP_ARDUINO_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | patch)
  #define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(3, 2, 1)
#endif

// Modulos propios que el sketch usa desde el principio. Los dos son
// unidades de traduccion aparte por el motivo de siempre en este
// proyecto: el .ino DIBUJA, los modulos mueven los bytes.
//   FlexOS_Media -> que es cada fichero, como se demultiplexa un AVI
//                   MJPEG y como se construye el indice por lotes.
//                   Es portable y tiene pruebas de host propias.
#include "FlexOS_Media.h"
#include "FlexOS_Audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_system.h"          // esp_reset_reason() para la banda forense
#include <WiFi.h>                // pila WiFi (transporte hosted C6 por debajo, AUTO)
#include <WiFiUdp.h>             // socket UDP del cliente NTP (ver "HORA REAL POR NTP")
#include "esp_task_wdt.h"        // TWDT: esp_task_wdt_reset() en loop()
#include "esp_sleep.h"           // deep sleep real + ext1/timer como fuente de despertar (ESP32-P4)
#include "driver/gpio.h"         // gpio_hold_en / gpio_deep_sleep_hold_en (mantener RST del GT911 en sleep)

// SISTEMA GLOBAL DE ACTUALIZACIONES OTA. Solo la API publica: toda la
// logica (red, JSON, descarga por streaming, instalacion e interfaz One
// UI) vive en FlexOS_OTA.cpp, que es comun a las tres placas. La version
// de firmware y la URL del manifiesto se definen DENTRO de esa cabecera
// -- no aqui -- porque el .cpp es una unidad de traduccion aparte y un
// #define hecho en este .ino no llegaria hasta ella.
#include "FlexOS_OTA.h"

// FLEX PACKAGE / FLEX STORE. El instalador valida FLXP v1, hashes SHA-256 y
// firmas ECDSA P-256 antes de activar una app. El runtime ejecuta unicamente
// interfaces declarativas flex-ui-1, nunca codigo nativo de terceros.
#include "FlexOS_Package.h"
#include "FlexOS_Runtime.h"
#include "FlexOS_Store.h"
#include "FlexOS_Account.h"

// SISTEMA DE ARCHIVOS REAL (LittleFS). Toda la logica de ficheros vive en
// FlexOS_FS.cpp -- comun a las tres placas -- para no tener tres copias de
// lo mismo que se desincronicen. Aqui solo se dibuja.
#include "FlexOS_FS.h"
// FLEX VAULT (Carpeta segura). Trae ademas el hash con sal del bloqueo
// del sistema: el PIN y la contrasena de la pantalla de bloqueo ya no se
// guardan en texto legible. Ver FlexOS_Vault.h.
#include "FlexOS_Vault.h"
// Decodificador JPEG (ya en el proyecto, lo usa el navegador). Flex Vault lo
// necesita para poder ENSENAR una foto privada sin escribirla en claro: se
// decodifica desde el buffer descifrado que vive en RAM.
#include "FlexOS_JPEG.h"

// NAVEGADOR REAL. La app 7 deja de ser una maqueta: la interfaz, las
// pestanas, el omnibox, el historial, los favoritos, la cache y el
// reproductor viven en FlexOS_BrowserApp.cpp (comun a las tres placas)
// y el contenido de los sitios lo rasteriza un servicio remoto con
// Chromium (ver server/). Aqui solo se declara la API; el puente con
// las primitivas de dibujo se incluye abajo, antes de setup().
#include "FlexOS_Browser.h"

// MOTOR METEOROLOGICO. Igual que el OTA, el sistema de archivos o la boveda:
// aqui solo entra la API publica. La red (Open-Meteo), el parseo JSON, el
// geocoding y la persistencia viven en FlexOS_Weather.cpp, que es una unidad
// de traduccion aparte. La app Clima, el widget del escritorio y el del
// bloqueo LEEN el mismo WeatherState, asi que no pueden contradecirse.
#include "FlexOS_Weather.h"

// FLEX PHONE. Igual que el clima o la boveda: aqui solo entra la API
// publica. El protocolo (FlexOS_FlexLink.cpp), el modelo
// (FlexOS_FlexPhone.cpp) y el transporte (FlexOS_FlexPhone_Link.cpp)
// son unidades de traduccion aparte y se prueban enteros en el PC.
// La interfaz vive en FlexOS_FlexPhone_Bridge.h, incluido abajo con
// los demas puentes.
#include "FlexOS_FlexPhone.h"

// PRESUPUESTO DE MEMORIA Y MULTITAREA. Mismo criterio que el clima o los
// medios: aqui solo entra la API. Las REGLAS -- cortes de 10/6/5 MB, bloque
// contiguo minimo, fragmentacion, veredicto de "cabe o no cabe" y el
// enfriamiento de los avisos -- viven en FlexOS_Mem.cpp, que es logica pura
// y se ejercita entera en el PC (tests/host/test_mem.cpp). Aqui solo se MIDE
// (heap_caps_*) y se ACTUA (soltar buffers); decidir es de alli.
#include "FlexOS_Mem.h"

// ---- DISPONIBILIDAD REAL DE BLE ------------------------------------------
// No se escribe a mano "el P4 no tiene BLE": se le pregunta al SDK. soc_caps.h
// define SOC_BLE_SUPPORTED solo en los chips que llevan radio Bluetooth, asi
// que el interruptor de BLE queda deshabilitado con "No disponible" en las
// placas que no pueden hacerlo, y funcional en las que si -- decidido en
// tiempo de COMPILACION, sin listas de modelos que mantener.
#include "soc/soc_caps.h"
#if defined(SOC_BLE_SUPPORTED) && SOC_BLE_SUPPORTED
  #define FLEXOS_BLE_HW 1
#else
  #define FLEXOS_BLE_HW 0
#endif
#ifndef FLEXOS_ENABLE_BLE
  #define FLEXOS_ENABLE_BLE FLEXOS_BLE_HW
#endif
#if FLEXOS_ENABLE_BLE
  #include <BLEDevice.h>
#endif


// ---------------- MODULOS DE FLEX OS ULTRA ----------------
// El orden IMPORTA (ver regla 3). Cada uno incluye al anterior,
// asi que basta con respetar esta lista.
#include "FlexOS_Ultra_Types.h"              // tipos de firma, interruptores maestros y estado temprano
#include "FlexOS_Ultra_HAL.h"                // panel MIPI-DSI (ST7701) y tactil GT911  -- capa de hardware
#include "FlexOS_Ultra_Gfx.h"                // motor grafico 480x800: framebuffers PSRAM, DMA2D y primitivas
#include "FlexOS_Ultra_Wallpaper.h"          // catalogo de fondos, fondo desde imagen real y paleta
#include "FlexOS_Ultra_Theme.h"              // tema semantico, claro/oscuro, Liquid Glass y superficies
#include "FlexOS_Ultra_Text.h"               // tipografia base, acentos, reloj vectorial y triangulos
#include "FlexOS_Ultra_Font.h"               // fuente Outfit 4bpp (tablas + rasterizador)
#include "FlexOS_Ultra_Icons.h"              // iconos vectoriales del sistema y enum IC_* de apps
#include "FlexOS_Ultra_Touch.h"              // gestos de alto nivel y suspension de pantalla
#include "FlexOS_Ultra_Prefs.h"              // preferencias en NVS, idiomas y ajustes del teclado
#include "FlexOS_Ultra_Session.h"            // sesiones en LittleFS, modo seguro y restablecimiento
#include "FlexOS_Ultra_Clock.h"              // reloj del sistema (epoca UTC) y API de NTP
#include "FlexOS_Ultra_Shell.h"              // enum ST_*, cadenas, splash y OOBE  -- maquina de estados
#include "FlexOS_Ultra_Home.h"               // escritorio por paginas, deslizamiento y modo edicion
#include "FlexOS_Ultra_Widgets.h"            // widgets del escritorio y su refresco de datos
#include "FlexOS_Ultra_HomeCfg.h"            // modo personalizacion del inicio y gesto de pellizco
#include "FlexOS_Ultra_AppFramework.h"       // marco de app, transiciones, nav inferior y ciclo de vida
#include "FlexOS_Ultra_Core.h"               // memoria, multitarea, los tres botones y rendimiento
#include "FlexOS_Ultra_AppSettings.h"        // app Ajustes
#include "FlexOS_Ultra_AppsBasic.h"          // Calculadora, Calendario, Bienestar y marco de Galeria
#include "FlexOS_Ultra_DeX.h"                // Modo PC / DeX: modelo y estado
#include "FlexOS_Ultra_DeXDraw.h"            // Modo PC / DeX: dibujo
#include "FlexOS_Ultra_DeXInput.h"           // Modo PC / DeX: entrada, APP_REG y ciclo de vida
#include "FlexOS_Ultra_QuickPanel.h"         // panel rapido: catalogo de controles y render
#include "FlexOS_Ultra_QuickPanelGlass.h"    // panel rapido: material Liquid Glass cacheado
#include "FlexOS_Ultra_QuickPanelEdit.h"     // panel rapido: modo edicion
#include "FlexOS_Ultra_Media.h"              // nucleo de medios: LittleFS, clasificacion e indice
#include "FlexOS_Ultra_AppMultimedia.h"      // app Multimedia (reproductor real)
#include "FlexOS_Ultra_AppCamera.h"          // app Camara
#include "FlexOS_Ultra_Keyboard.h"           // teclado de 4 capas y maquetacion de texto
#include "FlexOS_Ultra_FileKit.h"            // kit de archivos: menu, nombre, confirmacion y papelera
#include "FlexOS_Ultra_AppNotes.h"           // app Notas
#include "FlexOS_Ultra_KeyboardSettings.h"   // ajustes del teclado (pantalla propia)
#include "FlexOS_Ultra_AppStorage.h"         // app Almacenamiento y detalles de memoria
#include "FlexOS_Ultra_AppFiles.h"           // explorador de archivos
#include "FlexOS_Ultra_AppGames.h"           // app Juegos (incluye FlexOS_Jumper.h)
#include "FlexOS_Ultra_AppCodeIDE.h"         // Code IDE: asistente de hardware
#include "FlexOS_Ultra_AppPaint.h"           // app Paint
#include "FlexOS_Ultra_WeatherKit.h"         // clima: iconografia y escenas procedurales
#include "FlexOS_Ultra_AppWeather.h"         // app Clima y sus dos widgets
#include "FlexOS_Ultra_AppSwitcher.h"        // App Switcher / Recientes
#include "FlexOS_Ultra_Lock.h"               // bloqueo de seguridad y modo kiosco
#include "FlexOS_Ultra_AppDrawer.h"          // menu contextual del escritorio y caja de aplicaciones
#include "FlexOS_Ultra_Power.h"              // desbloqueo, suspension y apagado completo
#include "FlexOS_Ultra_Network.h"            // arranque seguro de la radio y Wi-Fi
#include "FlexOS_Ultra_NTP.h"                // cliente NTP en su propia tarea
#include "FlexOS_Ultra_Conn.h"               // conectividad: Wi-Fi / BLE / modo avion
#include "FlexOS_Ultra_Notif.h"              // isla dinamica: notificaciones
#include "FlexOS_Ultra_AppChrono.h"          // cronometro: app, capsula y tarjeta
#include "FlexOS_Ultra_System.h"             // I2C, soltar caches, Optimizar Flex OS y cambio de tema
#include "FlexOS_Ultra_AppGallery.h"         // Galeria
#include "FlexOS_Ultra_Vault.h"              // Flex Vault: interfaz de la Carpeta segura
#include "FlexOS_Ultra_Recovery.h"           // restablecer datos de fabrica y modo seguro
// ------------- FIN DE LOS MODULOS -------------------------

// Puente del modulo OTA. Va AQUI, y no arriba, a proposito: implementa
// las funciones otaHost* llamando a las primitivas graficas del sistema
// (fillRect, drawText, present, setBuf...), que son `static` y por tanto
// solo existen a partir del punto del fichero en que se definieron.
#include "FlexOS_OTA_Bridge.h"
// Puente del NAVEGADOR. Va aqui, y no arriba, por la misma razon que el
// del OTA: necesita que las primitivas de dibujo Y el teclado (kb*) ya
// esten definidos. Define navEnter()/navTick().
#include "FlexOS_Browser_Bridge.h"
// Puente visual de Flex Store. Se incluye aqui para reutilizar las primitivas
// estaticas del framebuffer y el sistema tactil de FlexOS Ultra.
#include "FlexOS_Store_Bridge.h"
// Pantalla real de Flex Account: OOBE, enlace posterior desde Flex Store y
// retorno al configurador de Wi-Fi.
#include "FlexOS_Account_Bridge.h"

// FLEX PHONE. Va DESPUES del puente del navegador: la seccion
// "Navegador" de Flex Phone lee flexBrowserSettings() y resuelve la
// fuente con la misma funcion que usa el propio navegador.
#include "FlexOS_FlexPhone_Bridge.h"

// Adaptadores de ciclo de vida para modulos cuyo estado interno ya es propio.
// Suspender no destruye pestañas ni descargas; cerrar la tarjeta si libera todo.
// NAVEGADOR. Suspender ya no es "salir y volver a entrar": la sesion (pestana,
// direccion, desplazamiento e historial de sesion) sigue viva, y lo unico que
// se suelta es la CACHE DE FOTOGRAMAS -- las imagenes remotas decodificadas,
// que es justo lo que no tiene sentido conservar con la app en segundo plano.
// Al volver, el navegador repinta y pide un fotograma nuevo al servicio.
static void navSuspendLife(){ navSuspend(); }
static void navResumeLife(){  navResume(); }
static size_t navShedLife(){  return flexBrowserReleaseVisualCache(); }
static void navCloseLife(){ flexBrowserExit(); }
static void storeResumeLife(){ storeEnter(); }
static void storeCloseLife(){ storeExit(); }

void setup(){
  Serial.begin(115200);
  delay(60);
  Serial.println(F("\n=== FlexOS Ultra (ESP32-P4) arrancando ==="));

  // (Ya NO se toca el TWDT aqui.) La version anterior llamaba a
  // esp_task_wdt_reconfigure() en cada arranque para dar margen al
  // bring-up del panel. Era innecesario -nada en setup() bloquea mas
  // de una fraccion de segundo, y la radio ya no corre aqui- y es
  // codigo nuevo no verificado contra el estado real del TWDT en esta
  // placa, asi que se retira: menos superficie para un crash en cada
  // boot. Sigue en pie esp_task_wdt_reset() en loop() (ver mas abajo).

  // FILTRO DE ENCENDIDO desde apagado completo. Va ANTES de flexPanelInit() a
  // proposito: si el toque no se sostiene 3 s, el chip se vuelve a dormir sin
  // haber encendido nunca ni el panel ni el backlight, asi que un roce
  // accidental en el bolsillo no produce ni un destello. Ver poffWakeGate().
  poffWakeGate();

  // Panel: reintento acotado. Si de verdad no enciende (cableado DSI),
  // parpadeo del backlight como SOS -> la placa sigue viva, no muerta.
  bool panelOk = flexPanelInit();
  if(!panelOk){ delay(150); panelOk = flexPanelInit(); }
  if(!panelOk){
    Serial.println(F("[FATAL] el panel DSI no responde (revisa cableado)"));
    pinMode(PIN_LCD_BL, OUTPUT);
    for(;;){ digitalWrite(PIN_LCD_BL, HIGH); delay(150); digitalWrite(PIN_LCD_BL, LOW); delay(150); }
  }
  if(!flxGfxInit()){
    Serial.println(F("[FATAL] sin PSRAM (activa 'PSRAM: Enabled' en el IDE)"));
    for(;;) delay(1000);
  }

  flexTouchInit();       // GT911: fallo suave (si no aparece, se sigue sin tactil)
  safeBootEval();        // antes de iniciar tareas pesadas: decide el modo de recuperacion
  frLoadState();         // marcador transaccional del restablecimiento

  // Solo configura CLK/CMD/D0-D3/RESET del enlace P4-C6. WiFi.setPins() no
  // levanta el driver ni habla con el C6, por lo que sigue siendo seguro en
  // setup(). Debe ocurrir antes de cualquier posible WiFi.mode()/begin().
  FLEXDIAG_WIFI("setup: antes de setPins");
  wifiConfigureHostedTransport();
  FLEXDIAG_WIFI("setup: despues de setPins");

  if(!gFrPending && !gSafeMode){
    bootInitRadioSafe(); // WiFi/C6: BYPASS -> nunca bloquea el arranque
    flexBrowserBegin();  // carga estado; no abre red
    flexOtaBegin();      // crea la tarea de fondo; no descarga aqui
  }

  // SEGURIDAD DEL BLOQUEO: migracion del PIN/contrasena guardados en
  // TEXTO LEGIBLE por las versiones anteriores a hash con sal. Va ANTES de
  // cfgLoad() para que lo que lea de "locktype" ya sea el estado
  // definitivo. Si no hay nada que migrar no escribe en NVS.
  //
  // Es una migracion en tres pasos (escribir el hash, comprobar que valida
  // la misma clave, y solo entonces borrar el texto legible), asi que un
  // fallo a mitad deja el telefono abriendose con la clave de siempre en
  // vez de dejar al usuario fuera. Ver flexLockMigrate en FlexOS_Vault.h.
  if(!gFrPending){
    int mg = flexLockMigrate();
    if(mg > 0)      Serial.println(F("[SEG] clave del bloqueo migrada a hash con sal"));
    else if(mg < 0) Serial.println(F("[SEG] no se pudo migrar la clave: se conserva la anterior"));
  }
  cfgLoad();

  // SISTEMA DE ARCHIVOS. Es flash, no radio: montarlo aqui NO viola la regla
  // de "setup() no toca la radio". Si falla, las pantallas que dependen de el
  // lo dicen en pantalla (fkNoFsScreen) en vez de ensenar datos inventados.
  bool fsOk = flexFsBegin();
  if(!fsOk) Serial.printf("[FS] no montado: %s\n", flexFsError());
  else {
    Serial.printf("[FS] LittleFS: %lu / %lu bytes usados\n",
                  (unsigned long)flexFsUsedBytes(), (unsigned long)flexFsTotalBytes());
    flexFsMkdir(FS_DIR_SESS);
    flexFsMkdir(FS_DIR_CACHE);
    if(!gFrPending && !gSafeMode) flexPkgBegin();
  }

  // AUDIO. Va DESPUES del tactil a proposito: el ES8311 cuelga del
  // MISMO bus I2C que el GT911 (GPIO7/8), asi que se aprovecha el
  // Wire que ya esta inicializado en vez de reconfigurar el bus por
  // debajo del panel. Si el codec no responde, el sistema sigue
  // igual y ninguna pantalla ensena controles de sonido.
  if(!flexAudioBegin())
    Serial.printf("[AUDIO] no disponible: %s\n", flexAudioError());

  // Una recuperacion interrumpida solo necesita pantalla, tactil, NVS y FS.
  // No se cargan cuenta, boveda, tienda, navegador ni red antes de terminar.
  if(gFrPending){ setBacklight(gBright); frResumeAfterBoot(); return; }

  if(!gSafeMode){
    flexStoreBegin();       // crea la tarea de fondo; no abre WiFi ni descarga en setup()
    flexAccountBegin();     // carga la cuenta local y crea su tarea; no toca la radio
  }

  // FLEX VAULT. Solo carga el sobre de la clave y los contadores de NVS:
  // la boveda arranca SIEMPRE cerrada y no se descifra nada aqui. Necesita
  // el sistema de archivos montado, por eso va justo despues.
  if(!gSafeMode){
    flexVaultBegin();
    connBootRestore();            // modo avion guardado (solo lee NVS, no toca radio)
    flexWeatherBegin();           // ubicaciones y cache + tarea de red
    // FLEX PHONE: solo carga el historial guardado y deja el enlace
    // APAGADO. Misma regla que la radio -- encender BLE en setup() es
    // lo que convertiria un fallo del C6 en un cuelgue de arranque.
    flexPhoneBegin();
  }
  setBacklight(gBright);          // aplica el brillo guardado
  homeOrderLoad();                // orden de iconos del Home
  // ASPECTO DEL INICIO: fondo de inicio y de bloqueo, imagen elegida, encuadre,
  // paleta y tema. Va DESPUES de flexFsBegin() porque si el fondo es una imagen
  // hay que leerla y decodificarla del almacenamiento, y ANTES del primer
  // renderHome() (que ocurre al terminar el splash). Si el JPEG ya no existe,
  // wallEnsureImage() vuelve al fondo integrado por defecto sin reiniciar.
  homeCfgLoad();
  // TECLADO (Fases A-D): geometria del tamano guardado, ranuras fijadas del
  // portapapeles y una comprobacion barata de que ese tamano cabe en pantalla.
  kbApplySize();
  kbMtSurfaceReset();
  clipLoadPinned();
  Serial.printf("[KB] tamano=%d (%dx%d gap=%d x=%d) cabe=%s\n",
                gKbSize, KB_KW, KB_KH, KB_GAP, KB_X, kbSizeCheck() ? "si" : "NO");

  clkBootMs = millis();
  seedMinOfDay = FLEXOS_CLK_SEED_MIN;   // semilla de fabrica: sab 4 jul 2026, 13:23
  clkSeedFactory();
  // Ultima hora conocida (NVS). Si existe, pisa la semilla: tras un reinicio
  // sin internet el equipo arranca con una hora APROXIMADA en vez de con la
  // fecha de fabrica. Si el almacenamiento no responde, gTimeNvsOk queda a
  // false y Ajustes lo dice con un error real, no en silencio.
  clkLoadNvs();
  clkUpdate();

  // Pantalla de diagnostico SOLO si el reinicio fue ANORMAL
  // (crash / watchdog / brownout). En encendido normal, arranque limpio.
  esp_reset_reason_t rr = esp_reset_reason();
  // Despertar de un apagado completo es un arranque NORMAL, no un crash: sin
  // esta rama la banda forense saldria en cada encendido desde deep sleep.
  // gBootCleanOff distingue "el usuario apago a proposito" de un deep sleep que
  // no salio de aqui; la bandera se consume (se borra) para que solo valga para
  // este arranque.
  bool fromDeep = (rr == ESP_RST_DEEPSLEEP);
  if(fromDeep){
    prefs.begin("flexos", false);
    gBootCleanOff = prefs.getBool("cleanoff", false);
    if(gBootCleanOff) prefs.putBool("cleanoff", false);
    prefs.end();
    Serial.printf("[PWR] arranque desde deep sleep (apagado limpio: %s)\n", gBootCleanOff ? "si" : "no");
  }
  bool abnormal = !(rr == ESP_RST_POWERON || rr == ESP_RST_SW || fromDeep);
  if(abnormal) showBootBanner();

  if(gSafeMode){ safeEnter(); return; }

  // Fondo NEGRO ABSOLUTO para el splash (como un movil comercial)
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, rgb565(0,0,0));
  flxFlushAll();

  splashStart = millis();
  gState = ST_SPLASH;
}

// Bucle de animacion continuo de la UI — corre pase lo que pase, no solo
// cuando hay un tap. Compone off-screen (anti-flicker) via cada render.
static unsigned long uiAnimMs = 0;
static void uiTick(){
  // El ripple del icono es una animacion puramente TEMPORAL (su posicion es
  // funcion de millis(), no un paso fijo por frame) y, tras el repintado
  // parcial de animateIconRipple, cuesta muy poco por frame. Por eso ESA ruta
  // se refresca a ~60 fps: restaura exactamente la franja que dibuja, asi que
  // es correcta por construccion y muy barata. El resto (edicion, cortina)
  // conserva su cadencia de ~26 fps -- algunas llevan pasos por-frame (p.ej.
  // el resorte de iconos en Modo Edicion) y acelerarlas cambiaria su
  // VELOCIDAD, no solo su suavidad; por eso no se tocan.
  // LA CORTINA SI PASA A LA VIA RAPIDA. Ya no tiene ningun paso por-cuadro: su
  // posicion sale del dedo (con suavizado por constante de TIEMPO) o del reloj
  // (qsAnimTo), y el destello de sus botones tambien es funcion de millis().
  // Subir su cadencia a ~60 fps solo la hace mas suave, no mas rapida -- y a 26
  // fps un gesto rapido avanzaba tanto entre cuadro y cuadro que el movimiento
  // se veia a saltos.
  if(appTrOwnsScreen()) return;    // la transicion posee la pantalla: nadie mas compone bandas
  bool qsVisible = (qsPanelY > 0 || qsAnimOn);
  bool fastPath = qsVisible || (gState == ST_HOME && !editMode && gRippleActive);
  unsigned long interval = fastPath ? 16 : 38;
  if(millis() - uiAnimMs < interval) return;
  uiAnimMs = millis();
  // LA CORTINA VA PRIMERO Y EN CUALQUIER ESTADO. Ya no es una pantalla del
  // escritorio: puede estar encima de una app, y su animacion de apertura y
  // cierre (antes un bucle bloqueante) se avanza aqui, un paso por cuadro.
  if(qsVisible){
    // qsTick() avanza TODO lo que se mueve en la cortina con el tiempo
    // transcurrido: apertura/cierre, "snap" del asa de la tarjeta, inercia
    // del scroll y destellos; y publica solo la banda sucia del cuadro.
    qsTick();
    return;
  }
  if(gState == ST_HOME){
    // Con un gesto de pagina en curso, la rejilla NO esta donde cree el
    // destello: sus coordenadas son las de la pagina quieta, asi que
    // restauraria un trozo de escritorio sin desplazar en medio del
    // contenido que se mueve. Mientras dura el gesto, esta animacion
    // calla; hpTick es quien manda en esa banda.
    if(hpDragging || hpSettling) return;
    if(editMode) edRender();                    // jiggle continuo
    else if(gRippleActive) animateIconRipple(); // destello del icono tocado (Vidrio), ~0.5s
  }
}

void loop(){
  flexFeedWdt();          // alimenta el TWDT solo si loopTask sigue suscrito (ver arriba)
  loopRateTick();         // ritmo real del sistema (vueltas/s), un entero por vuelta
  flexPollTouch();        // (aqui dentro corre tambien el detector de doble-tap de la suspension)

  // -----------------------------------------------------------
  //  RESTABLECIMIENTO DE FABRICA EN CURSO: PANTALLA EN EXCLUSIVA
  //  ---------------------------------------------------------
  //  Mientras el borrado corre, NADIE mas dibuja ni recibe toques: ni
  //  el escritorio, ni la isla de notificaciones, ni el panel rapido,
  //  ni la capa del OTA. Un solo propietario de la pantalla, igual que
  //  ya hacia el OTA con su overlay a pantalla completa.
  //
  //  El tactil, el TWDT y el reloj SIGUEN corriendo: lo unico que se
  //  corta es todo lo demas. Y como frTick ejecuta UNA etapa por
  //  vuelta, el watchdog se alimenta entre etapa y etapa sin ocultar
  //  ningun bloqueo real.
  // -----------------------------------------------------------
  if(gFrPending || (gState == ST_FACTORY)){
    clkUpdate();
    frTick();
    delay(5);
    return;
  }

  safeStableTick();
  sessAutosaveTick();
  safeToastTick();
  memTick();              // medida de memoria por TIEMPO (nunca dentro del dibujo)
  memAlertTick();         // avisos reales, con enfriamiento (FlexOS_Mem.cpp decide)

  suspFadeTick();         // SUSPENSION/APAGADO: un paso del fundido de backlight (no bloqueante)
  autoLockTick();         // FASE 1: bloqueo por inactividad (lee T sin filtrar, antes de que nadie consuma el toque)
  cronoOverlayTouch();    // CRONOMETRO: la capsula y su tarjeta se quedan el toque antes que la isla
  notifHandleTouch();     // la isla intercepta toques dentro de sus tarjetas (Fase 1)
  flexOtaTouchBridge();   // OTA: si hay overlay visible, se queda el toque antes que nadie
  hwDetectTick();         // deteccion I2C incremental, mismo contexto que el tactil (Fase 2)
  mediaIndexTick();       // indice LittleFS: un lote corto cuando esta activo
  if(!gSafeMode){
    wifiAutoReconnectTick();// reconexion diferida, una vez por arranque
    ntpTick();              // la red corre en su tarea, nunca aqui
  }
  clkPersistTick();       // guarda la hora en NVS una vez por hora (arranque sin internet)
  bool minChanged = clkUpdate();
  gMinChanged = minChanged;

  // -----------------------------------------------------------
  //  PANTALLA EN EXCLUSIVA PARA EL OTA
  //  ---------------------------------------------------------
  //  Con una capa OTA a pantalla completa (changelog, progreso o
  //  ajustes de actualizacion), NINGUN otro subsistema dibuja.
  //
  //  Por que: el overlay no cambia gState -- durante una descarga
  //  seguimos en ST_HOME. Sin este corte, en cada vuelta se
  //  ejecutaban igualmente homeTick(), kioskTick(), uiTick() y
  //  notifTick(), y todos ellos componen en el MISMO bbuf y
  //  publican su banda con present(). Entre dos repintados del OTA
  //  se colaba una banda con el fondo del escritorio -- un degradado
  //  azul/verde -- justo encima de la pantalla de progreso: ese era
  //  el "parpadeo cian" en cada 1%. No era una carrera entre nucleos:
  //  era que nadie tenia la propiedad exclusiva de la pantalla.
  //
  //  El tactil, el TWDT, la deteccion I2C y la radio SIGUEN
  //  corriendo arriba: aqui solo se corta el DIBUJO.
  // -----------------------------------------------------------
  if(flexOtaOwnsScreen()){
    // La OTA manda siempre. Si el modo de personalizacion estaba abierto se
    // cierra AQUI, guardando y liberando sus buffers (miniatura y
    // previsualizaciones), en vez de dejarlos reservados durante toda la
    // descarga compitiendo por la PSRAM.
    if(hcActive){ hcClose(true); gState = ST_HOME; gHomeDirty = true; }
    flexOtaRender();
    delay(5);
    return;
  }

  // -----------------------------------------------------------
  //  PANTALLA EN EXCLUSIVA PARA "OPTIMIZAR FLEX OS"
  //  ---------------------------------------------------------
  //  Mismo patron -- y mismo motivo -- que el OTA y la tarjeta del
  //  cronometro: mientras el panel esta a la vista, NADIE mas compone
  //  bandas. Sin este corte, el tick de Almacenamiento o el del selector
  //  seguirian publicando su banda entre dos etapas y se veria un trozo
  //  de la pantalla de debajo encima del panel.
  //
  //  Sin delay al final, como en la transicion de apps: aqui hay una
  //  animacion en marcha y el bucle no debe frenarse. El tactil, el TWDT
  //  y el reloj ya corrieron arriba.
  // -----------------------------------------------------------
  if(optActive()){
    optTick();
    flexOtaRender();
    return;
  }

  if(!gSafeMode) flexWeatherTick(gNetOnline);
  // FLEX PHONE: con el enlace apagado o no disponible sale en su
  // primera linea, asi que una placa sin telefono emparejado no paga
  // nada por que esta app exista.
  if(!gSafeMode) flexPhoneTick();

  // -----------------------------------------------------------
  //  PANEL RAPIDO GLOBAL
  //  ---------------------------------------------------------
  //  Va ANTES del switch de estado a proposito. Si el gesto entra
  //  por el borde superior, la cortina se queda con el toque
  //  ENTERO y la pantalla de debajo -- escritorio o app -- no
  //  llega a verlo. Mientras esta a la vista devuelve true en cada
  //  vuelta, asi que el tick de la app no corre: ni sus gestos ni
  //  sus botones responden por debajo del panel.
  //
  //  En Modo PC / DeX horizontal qsCanOpen() devuelve false y esto
  //  no hace nada (ver la cabecera del panel).
  // -----------------------------------------------------------
  if(qsGlobalHandle()){
    kioskTick();
    uiTick();               // anima la cortina (apertura, cierre y destello)
    notifTick();            // no dibuja (la isla se pausa con la cortina), pero SI
                            // contabiliza la pausa: sin esta llamada, al cerrar la
                            // cortina todas las tarjetas caducarian de golpe
    flexOtaRender();
    delay(5);
    return;
  }

  // -----------------------------------------------------------
  //  TARJETA EXPANDIDA DEL CRONOMETRO
  //  ---------------------------------------------------------
  //  Overlay MODAL propio (no es una notificacion: no vive en
  //  gNotifs[] ni caduca a los 5 s). Va aqui, con el mismo patron
  //  que la cortina: mientras esta a la vista se queda con la
  //  pantalla y la de debajo no hace tick.
  //
  //  Por que importa: la tarjeta se abre capturando la banda REAL
  //  de fb -- lo que hubiera debajo, sea el escritorio o una app.
  //  Si la pantalla de debajo siguiera repintando, esa captura se
  //  quedaria vieja y al cerrar la tarjeta volveria un fotograma
  //  caducado. Cediendole la pantalla, la banda capturada sigue
  //  siendo valida y el cierre es pixel a pixel, sin parpadeo.
  // -----------------------------------------------------------
  if(cronoCardVisible()){
    if(minChanged) gHomeDirty = true;   // el escritorio se rehara al cerrar la tarjeta
    cronoCardTick();
    flexOtaRender();
    delay(5);
    return;
  }

  switch(gState){
    case ST_SPLASH:    splashTick(); break;
    case ST_OOBE_LANG: oobeLangTick(); break;
    case ST_OOBE_NAME: oobeNameTick(); break;
    case ST_OOBE_ACCOUNT: accountOobeTick(); break;
    case ST_LOCK:
      if(minChanged){ renderLock(); if(lockOff == 0) showLock(); }
      lockTick();
      break;
    case ST_HOME:
      // El cambio de minuto rehace homeBuf y lo vuelca ENTERO. En medio
      // de un gesto de pagina eso borraria el frame desplazado y pondria
      // la pagina de origen sin mover: un salto a mitad del arrastre.
      // Se aplaza -- gHomeDirty hace que homeTick lo rehaga en cuanto el
      // gesto termina -- porque un reloj un segundo tarde se perdona y un
      // tiron en el deslizamiento no.
      if(minChanged && qsPanelY == 0 && !qsAnimOn && !editMode){
        // Con una transicion de app a la vista pasa lo mismo que con un gesto de
        // pagina: volcar el escritorio entero borraria la tarjeta que esta
        // animandose. Se aplaza -- homeTick lo rehace en cuanto termina.
        if(hpDragging || hpSettling || appTrOwnsScreen()) gHomeDirty = true;
        else { renderHome();             // refresca el cache homeBuf (offscreen: setBuf(homeBuf)...setBuf(fb), sin tocar pantalla)
               showHome(); }
      }
      homeTick();     // la cortina ya se atendio arriba (qsGlobalHandle)
      break;
    case ST_APP:       appTick(); break;
    case ST_SWITCHER:  swTick(); break;
    case ST_LOCKSETUP: lsuTick(); break;
    case ST_WIFI:      wifiTick(); break;
    case ST_CTX:       ctxTick(); break;        // FASE 2: menu contextual de long-press
    case ST_KIOSKSET:  kioskSetTick(); break;   // FASE 4: definir el area excluida
    case ST_POWEROFF_CONFIRM: poffTick(); break;    // APAGADO: slider "desliza para apagar"
    case ST_POWEROFF_ANIM:    poffAnimTick(); break;// APAGADO: animacion final (no vuelve)
    case ST_KBSET:            kbsTick(); break;     // FASE E: Ajustes del teclado
    case ST_CONN:             connTick(); break;    // Conectividad: Wifi / BLE / Modo avion
    case ST_FILES:            filesTick(); break;   // Explorador de archivos real
    case ST_VAULT:            vaultTick(); break;          // Flex Vault (Carpeta segura)
    case ST_DRAWER:           drawerTick(); break;         // Caja de aplicaciones (One UI)
    case ST_HOMECFG:          hcTick(); break;             // Personalizar inicio
    case ST_SAFE:             safeTick(); break;
    case ST_FACTORY:          frTick(); break;
  }
  kioskTick();            // FASE 4: refresca el candado y escucha el gesto de salida
  wgDataTick();           // widgets del Home: refresco de DATOS (nunca dentro del dibujo)
  // -----------------------------------------------------------
  //  TRANSICION DE APP: UN SOLO DUENO DE LA PANTALLA
  //  ---------------------------------------------------------
  //  Mismo patron que el OTA y la tarjeta del cronometro, y por el
  //  mismo motivo: la transicion compone su banda sobre homeBuf y
  //  la publica con present(). Si los widgets, la isla o la capsula
  //  del cronometro publicaran su propia banda entre medias, se
  //  colaria un trozo de escritorio SIN la tarjeta encima -- un
  //  parpadeo en mitad de la animacion.
  //
  //  Lo que NO se corta es el TACTIL ni el estado logico: eso ya
  //  corrio arriba (flexPollTouch y el switch de gState), que es
  //  precisamente lo que permite aceptar la app siguiente mientras
  //  la anterior todavia se ve encogiendo.
  //
  //  Sin delay(5) al final: mientras hay animacion, cada milisegundo
  //  del presupuesto de cuadro cuenta. En reposo se conserva.
  // -----------------------------------------------------------
  if(appTrOwnsScreen()){
    // La capa solo tiene sentido sobre Inicio o sobre una app. Si la navegacion
    // se fue a otro sitio (Recientes, bloqueo, Personalizar inicio, apagado...),
    // la transicion se ABORTA en vez de seguir pintando encima de una pantalla
    // que no es la suya. Esa pantalla ya se dibujo entera al entrar.
    if(gState != ST_HOME && gState != ST_APP) appTrCancel();
    else { appTrTick(); flexOtaRender(); return; }
  }
  if(wgDirty && gState == ST_HOME && !editMode && qsPanelY == 0 && !qsAnimOn &&
     !hpDragging && !hpSettling){
    wgDirty = false;      // solo se repintan las filas de los widgets, ni una mas
    wgRepaint();
  }
  uiTick();               // animacion continua del vidrio
  notifTick();            // isla dinamica: anima y compone sobre la pantalla activa (Fase 1)
  cronoCapsuleTick();     // CRONOMETRO: capsula de la barra (solo repinta al cambiar el segundo)
  flexOtaRender();        // OTA: ULTIMA capa del pipeline grafico (nunca toca el fb de una app)
  delay(5);
}

// #############################################################
// ##  HOJA DE RUTA  (lo que llega despues del Milestone 1)
// #############################################################
//
//  Milestone 1 (ESTE archivo) — COMPLETO:
//    · Capa HW nativa 480x800 (panel ST7701 + GT911) reusada de ArduOS
//    · Motor grafico propio (framebuffers PSRAM + DMA2D sincronizada)
//    · Fuente 5x7 con acentos UTF-8 (es/fr/pt/it) + reloj vectorial
//    · Splash con fundido · OOBE (6 idiomas + teclado QWERTY)
//    · Bloqueo con reloj gigante y desbloqueo con fisica (swipe-up)
//    · Escritorio: barra de estado, 2 widgets, rejilla 4x3, dock, nav
//    · Banda forense de reinicio (depuracion sin PC)
//
//  Milestone 2 — Framework de apps + apps reales:
//    [HECHO] Sistema de ventanas: apertura/cierre animado desde el icono,
//            marco estandar (estado + cabecera "atras" + nav), registro
//            APP_REG enchufable, gestos de cierre. App de referencia: Reloj.
//    [PENDIENTE] Rellenar el resto (reemplazar entradas de APP_REG):
//      Galeria, Multimedia, Almacenamiento, Modo PC, Notas, Educacion,
//      Navegador, Code IDE, Bienestar, Paint, Juegos, Calculadora,
//      Calendario, Camara.
//
//  Milestone 3 — Ajustes (imagen 3): [HECHO] dos paneles (barra lateral
//    de 12 categorias + panel de detalle con scroll). General y Acerca de
//    con datos reales del dispositivo; resto con filas representativas.
//    Motor: se anadio recorte vertical (clip) para listas con scroll.
//
//  Milestone 4 — Modo PC estilo Windows 11 (imagen 4): barra de
//    tareas, ventanas flotantes, escritorio horizontal.
//
//  Pendientes de plataforma (cuando toque):
//    · Fuente CJK (archivo de fuente en SPIFFS) para chino real.
//    · WiFi/NTP: hoy OFF por la inestabilidad del co-procesador C6
//      (esp-hosted). Reactivar tras actualizar su firmware/core.
//    · Bateria real por ADC y brillo por PWM del backlight.
//    · [HECHO] Almacenamiento REAL en LittleFS (FlexOS_FS.h/.cpp):
//      carpetas /Paint, /Notas, /System, /Documentos y /Papelera,
//      con Paint, Notas, Almacenamiento y el Explorador de archivos
//      operando sobre ficheros de verdad.
//    · [HECHO] MEDIOS REALES (FlexOS_Media.h/.cpp + Galeria +
//      Multimedia): indice incremental de LittleFS, JPEG
//      baseline, video AVI/MJPEG con reproduccion por bloques y
//      orientacion Auto/Vertical/Horizontal sobre el motor gLand que
//      ya existia.
//    · [PARCIAL] AUDIO (FlexOS_Audio.h/.cpp): ES8311 por I2S, con
//      volumen que escribe el registro del codec. Solo se activa si
//      el chip contesta su identificacion en el bus; NO esta
//      verificado con altavoz en placa.
//    · Pendiente: fondos de pantalla cargados desde fichero.
// #############################################################
