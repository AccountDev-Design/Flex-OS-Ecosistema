// #############################################################
// ##  FLEX OS ULTRA  ·  RED  ·  arranque seguro de la radio y Wi-Fi
// ##  ----------------------------------------------------------
// ##  El arranque que NO toca la radio hasta que el transporte esta
// ##  validado, la pantalla de Wi-Fi, las credenciales guardadas en NVS y
// ##  la reconexion automatica. REGLA DE ORO: esp-hosted nunca se toca
// ##  desde loopTask; todo pasa por wifiScanTask/wifiConnTask/wifiOffTask.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente. FlexOS_Ultra.ino lo incluye en el
// ##  orden que fija la cadena de cabeceras (cada modulo incluye al
// ##  anterior), asi que todo el sistema sigue compilandose como UN
// ##  SOLO archivo, exactamente igual que antes de separarlo.
// ##
// ##  Consecuencias practicas, y son las que mantienen esto seguro:
// ##    · Las variables globales se DEFINEN una sola vez, aqui, en el
// ##      modulo al que pertenecen. No hace falta `extern` ni existe
// ##      el riesgo de una definicion duplicada en el enlazado.
// ##    · El ORDEN de definicion es el mismo que tenia el .ino: una
// ##      funcion `static` solo se puede llamar despues de definirse,
// ##      y esa relacion se conserva modulo a modulo.
// ##    · La cadena de includes es LINEAL (Types -> ... -> Recovery),
// ##      asi que no hay dependencias circulares posibles.
// ##    · No lo incluyas por tu cuenta desde otro sitio: el punto de
// ##      entrada del sistema es siempre FlexOS_Ultra.ino.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Power.h"   // eslabon anterior de la cadena

// #############################################################
// ##  setup / loop
// #############################################################
// -------------------------------------------------------------
//  Radio (WiFi via co-procesador ESP32-C6 / esp-hosted por SDIO)
//  EL ARRANQUE NO TOCA LA RADIO HASTA QUE EL TRANSPORTE HAYA SIDO
//  VALIDADO MANUALMENTE. Antes, bootInitRadioSafe()
//  lanzaba un intento de conexion automatico en cada boot (con SSID/
//  PASS fijos en el codigo) en cuanto FLEXOS_ENABLE_WIFI valia 1. Eso
//  es lo que producia el bucle de "PANIC (crash)": si el enlace SDIO
//  con el C6 fallaba (placa sin C6 operativo en esos pines, firmware
//  slave desactualizado, etc.), fallaba EN CADA arranque, de forma
//  determinista, antes de que pudieras hacer nada.
//
//  Ahora una red antigua no puede despertar la radio en bucle: primero
//  exige una conexion manual correcta. Desde ese momento se permite una
//  reconexion diferida y protegida por un marcador NVS; si se interrumpe
//  por reset, el siguiente arranque abre el fusible y no vuelve a probar.
//
//  1) El enlace P4<->C6 es SDIO (CLK/CMD/D0-D3 + reset), NO SPI. En
//     arduino-esp32 3.2.1 WiFi.setPins() permite fijar los siete pines
//     antes de inicializar esp-hosted. La 3.2.0 no sirve para esta placa.
//  2) WiFi.begin()/WiFi.scanNetworks() disparan por debajo el
//     transporte hosted (esp_wifi_remote); nunca se llama a
//     esp_hosted_init() a mano.
//  3) El C6 necesita firmware "slave" de esp-hosted flasheado aparte;
//     si por Serial ves version v0.0.0, no hay enlace real posible.
//
//  FLEXOS_ENABLE_WIFI y gNetOnline estan declarados arriba del todo
//  del archivo (junto a los PINES) porque Ajustes los necesita antes
//  de llegar aqui. Si tu placa NO tiene el C6 operativo, pon
//  FLEXOS_ENABLE_WIFI a 0: la pantalla de Wi-Fi lo respeta y deja de
//  ofrecer "Buscar redes" sin que toques nada mas.
// -------------------------------------------------------------
#define FLEXOS_WIFI_TIMEOUT_MS 15000
#define FLEXOS_WIFI_AUTOCONN_DELAY_MS 6000



// Definido mas abajo (necesita wifiSavedSSID y el resto del bloque Wi-Fi).
static bool wifiCredsLoad();

// Se llama UNA vez desde setup(), antes de cualquier API que pueda iniciar
// esp-hosted. No enciende la radio: solo sustituye la configuracion SDIO que
// en 3.2.0 estaba codificada dentro del SDK y no respetaba el variant.
static void wifiConfigureHostedTransport(){
#if FLEXOS_ENABLE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  gWifiHostedPinsOk = WiFi.setPins(18, 19, 14, 15, 16, 17, 54);
  Serial.printf("[C6] pines SDIO hosted: %s\n", gWifiHostedPinsOk ? "OK" : "ERROR");
#elif FLEXOS_ENABLE_WIFI
  gWifiHostedPinsOk = true;
#else
  gWifiHostedPinsOk = false;
  Serial.println(F("[C6] Wi-Fi bloqueado: instala arduino-esp32 3.2.1"));
#endif
}

// -------------------------------------------------------------
//  setup() NO INICIALIZA LA RADIO. NI UNA SOLA VEZ.
//  -------------------------------------------------------------
//  Esto es una regla del sistema, no una preferencia (ver el bloque
//  "EL ARRANQUE YA NUNCA TOCA LA RADIO" mas arriba): encender la pila
//  WiFi dentro de setup() es exactamente lo que producia el bucle de
//  "PANIC (crash)" en cada arranque cuando el enlace esp-hosted/SDIO
//  con el C6 no respondia. La version anterior de este fichero volvio
//  a meter ahi el intento de reconexion y reprodujo el problema, esta
//  vez en forma de inundacion del TWDT desde el primer boot.
//
//  Aqui solo se leen las credenciales y el fusible anti-bucle de NVS.
//  La reconexion temporizada se arma unicamente despues de que una conexion
//  manual haya demostrado que los pines, el core y el firmware del C6 sirven.
// -------------------------------------------------------------
static void bootInitRadioSafe(){
  bool saved = wifiCredsLoad();          // solo NVS: no despierta el C6
  gWifiAutoDone = !(saved && gWifiAutoTrusted && gWifiHostedPinsOk);
  if(gWifiAutoInterrupted)
    Serial.println(F("[C6] intento automatico interrumpido por reset -> fusible abierto"));
  else if(!gWifiHostedPinsOk)
    Serial.println(F("[C6] transporte no configurado -> Wi-Fi deshabilitado"));
  else if(saved && gWifiAutoTrusted)
    Serial.println(F("[C6] red y transporte validados; reconexion automatica diferida"));
  else if(saved)
    Serial.println(F("[C6] red guardada; requiere una conexion manual correcta antes del autoarranque"));
  else
    Serial.println(F("[C6] radio en modo bajo demanda -> se activa desde Ajustes > Wi-Fi"));
}


// ---- BLE opcional (tambien via C6, requiere firmware slave con BT) ----
// NOTA: el P4 NO tiene controlador Bluetooth propio, asi que aqui NO se
// llama a esp_bt_controller_mem_release(): esa API es para liberar RAM
// de Bluetooth Clasico en un chip con radio LOCAL (S3, C3...), y el P4
// no tiene radio local ni Bluetooth Clasico que liberar. El C6 ademas
// solo ofrece BLE (no Classic). Descomenta esto SOLO despues de
// confirmar que WiFi ya enlaza por el mismo C6.
#if 0
#include <NimBLEDevice.h>
static void bootInitBleSafe(){
  NimBLEDevice::init("FlexOS");
  // ... tu logica de advertising / GATT aqui
}
#endif

// #############################################################
// ##  AJUSTES -> RED E INTERNET -> WI-FI
// ##  Escaneo y conexion corren en su propia tarea del Core 1
// ##  (igual patron que arriba): loop() nunca se bloquea, y un
// ##  fallo del C6 se queda contenido en esta pantalla.
// #############################################################
#define WIFI_MAX_NETS 16
struct WifiNet { char ssid[33]; int8_t rssi; bool secure; };
static WifiNet      wifiNets[WIFI_MAX_NETS];
static volatile int wifiNetCount = 0;
static portMUX_TYPE wifiMux = portMUX_INITIALIZER_UNLOCKED;

enum { WUI_LIST = 0, WUI_SCANNING, WUI_PASS, WUI_CONNECTING, WUI_OK, WUI_FAIL };
static volatile int wifiUIState = WUI_LIST;
static int      wifiSel = -1;
static char     wifiPass[64] = "";
static uint32_t wifiKbAnim = 0;
static char     wifiConnSSID[33] = "";
static char     wifiConnPass[64] = "";
static char     wifiConnIP[24]   = "";

// #############################################################
// ##  CREDENCIALES GUARDADAS (NVS) + RECONEXION AUTOMATICA
// ##  ------------------------------------------------------
// ##  Antes, la red elegida a mano solo vivia en RAM: cada
// ##  apagado obligaba a repetir escaneo + seleccion + clave.
// ##  Ahora, la PRIMERA conexion correcta guarda SSID y clave en
// ##  NVS (namespace propio "flexos_wifi", separado de "flexos"
// ##  para que un borrado de ajustes no arrastre la red y al
// ##  reves), y el arranque las reutiliza en segundo plano.
// #############################################################
#define WIFI_NVS_NS    "flexos_wifi"
#define WIFI_NVS_SSID  "ssid"
#define WIFI_NVS_PASS  "pass"
#define WIFI_NVS_AUTOSAFE "autosafe"  // una conexion manual ya funciono con este transporte
#define WIFI_NVS_AUTOTRY  "autotry"   // se pone antes del autoarranque y se limpia al terminar

static char          wifiSavedSSID[33] = "";
static char          wifiSavedPass[64] = "";
static bool wifiCredsExist(){ return wifiSavedSSID[0] != 0; }
static const char* wifiActiveSSID(){
  return (gNetOnline && wifiSavedSSID[0]) ? wifiSavedSSID : "";
}

// Carga las credenciales de NVS a RAM. Se llama una vez en el arranque.
static bool wifiCredsLoad(){
  Preferences p;
  if(!p.begin(WIFI_NVS_NS, true)){
    wifiSavedSSID[0] = 0; wifiSavedPass[0] = 0;
    gWifiAutoTrusted = false; gWifiAutoInterrupted = false;
    return false;
  }
  String s = p.getString(WIFI_NVS_SSID, "");
  String w = p.getString(WIFI_NVS_PASS, "");
  bool trusted = p.getBool(WIFI_NVS_AUTOSAFE, false);
  bool pending = p.getBool(WIFI_NVS_AUTOTRY, false);
  p.end();
  s.toCharArray(wifiSavedSSID, sizeof(wifiSavedSSID));
  w.toCharArray(wifiSavedPass, sizeof(wifiSavedPass));

  // Si AUTOTRY seguia a 1, el P4 se reinicio antes de que la tarea pudiera
  // cerrarlo. Se invalida la confianza y se limpia el marcador para permitir
  // una prueba MANUAL, pero el arranque no vuelve a tocar Wi-Fi por si solo.
  gWifiAutoInterrupted = pending;
  gWifiAutoTrusted = trusted && !pending;
  if(pending && p.begin(WIFI_NVS_NS, false)){
    p.putBool(WIFI_NVS_AUTOTRY, false);
    p.putBool(WIFI_NVS_AUTOSAFE, false);
    p.end();
  }
  return wifiCredsExist();
}

static void wifiAutoGuardWrite(bool trusted, bool pending){
  Preferences p;
  if(p.begin(WIFI_NVS_NS, false)){
    p.putBool(WIFI_NVS_AUTOSAFE, trusted);
    p.putBool(WIFI_NVS_AUTOTRY, pending);
    p.end();
  }
  gWifiAutoTrusted = trusted;
  gWifiAutoInterrupted = false;
}

// Guarda (solo si algo cambio: escribir NVS sin necesidad desgasta la flash).
static void wifiCredsSave(const char* ssid, const char* pass){
  if(!ssid || !ssid[0]) return;
  if(!strcmp(wifiSavedSSID, ssid) && !strcmp(wifiSavedPass, pass ? pass : "")) return;
  Preferences p;
  if(!p.begin(WIFI_NVS_NS, false)) return;
  p.putString(WIFI_NVS_SSID, ssid);
  p.putString(WIFI_NVS_PASS, pass ? pass : "");
  p.end();
  strncpy(wifiSavedSSID, ssid, sizeof(wifiSavedSSID) - 1); wifiSavedSSID[sizeof(wifiSavedSSID) - 1] = 0;
  strncpy(wifiSavedPass, pass ? pass : "", sizeof(wifiSavedPass) - 1); wifiSavedPass[sizeof(wifiSavedPass) - 1] = 0;
  Serial.printf("[WiFi] red guardada: %s\n", wifiSavedSSID);
}

// Olvida la red (cambio de router). Borra NVS y la copia en RAM.
static void wifiCredsForget(){
  Preferences p;
  if(p.begin(WIFI_NVS_NS, false)){ p.clear(); p.end(); }
  wifiSavedSSID[0] = 0; wifiSavedPass[0] = 0;
  gWifiAutoTrusted = false; gWifiAutoInterrupted = false;
  gWifiAutoDone = true;                      // no reintentar en esta sesion
  Serial.println(F("[WiFi] red guardada borrada"));
}

// #############################################################
// ##  REGLA DE ORO: esp-hosted NUNCA se toca desde loopTask
// ##  ------------------------------------------------------
// ##  loopTask esta SUSCRITO AL TASK WATCHDOG (ver flexFeedWdt) y solo lo
// ##  alimenta una vez por vuelta. Cualquier llamada que despierte el
// ##  transporte hosted -- WiFi.getMode(), WiFi.mode(), WiFi.begin(),
// ##  WiFi.scanNetworks(), WiFi.disconnect() -- bloquea esa vuelta durante
// ##  todo el arranque del enlace SDIO con el C6: reset del co-procesador,
// ##  negociacion y handshake. Si el C6 no contesta rapido, la vuelta se
// ##  pasa del plazo del TWDT y el chip entra en PANIC y reinicia.
// ##
// ##  Eso es exactamente el sintoma "se reinicia al activar el Wi-Fi", y
// ##  el motivo por el que aqui hay DOS funciones y no una:
// ##
// ##    wifiTransportReady()  -> BARATA. Solo mira banderas propias y la
// ##                             SRAM interna. NO habla con el driver.
// ##                             Es la unica que puede llamar la interfaz.
// ##    wifiEnsureStaMode()   -> CARA. Toca esp-hosted. SOLO puede
// ##                             llamarse desde una tarea propia de Wi-Fi,
// ##                             que no esta suscrita al TWDT.
// ##
// ##  Toda la teleria de encendido y apagado pasa ahora por una tarea.
// #############################################################

// Suelo de SRAM INTERNA para empezar una operacion de radio. esp-hosted, el
// driver SDIO y la pila de la tarea salen de la SRAM interna, no de la PSRAM:
// con la interna en las ultimas, WiFi.begin() falla dentro del driver en un
// sitio donde ya no hay forma elegante de volver. Es una GUARDA -- evita
// entrar en una operacion condenada --, no un arreglo del fallo de fondo.
#define FLEXOS_WIFI_MIN_SRAM (48u * 1024u)

// Comprobacion BARATA, apta para el hilo de la interfaz: ni una llamada al
// driver. Devuelve false y deja dicho el motivo si no se puede intentar.
static bool wifiTransportReady(const char** why){
  if(!gWifiHostedPinsOk){
    if(why) *why = "transporte SDIO sin configurar";
    return false;
  }
  size_t inFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if(inFree < FLEXOS_WIFI_MIN_SRAM){
    if(why) *why = "memoria interna insuficiente para la radio";
    Serial.printf("[C6] radio rechazada: SRAM interna %u KB < %u KB\n",
                  (unsigned)(inFree / 1024u), (unsigned)(FLEXOS_WIFI_MIN_SRAM / 1024u));
    return false;
  }
  return true;
}

// GUARD DE MODO. SOLO DESDE UNA TAREA DE WI-FI (ver la regla de oro de arriba).
// El driver debe estar en STA antes de tocarlo. Llamar a WiFi.mode() cuando ya
// se esta en el modo pedido es innecesario y en el transporte hosted del C6
// reinicia la interfaz sin motivo, asi que solo se cambia si hace falta.
static bool wifiEnsureStaMode(){
  if(!wifiTransportReady(NULL)){
    Serial.println(F("[C6] operacion rechazada: transporte no disponible"));
    return false;
  }
  FLEXDIAG_WIFI("antes de WiFi.getMode()");
  gWifiDriverOn = true;                    // publicar ANTES de tocar esp-hosted
  if(WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  FLEXDIAG_WIFI("despues de WiFi.mode(STA)");
  return true;
}

// APAGADO DE LA RADIO, TAMBIEN EN UNA TAREA. WiFi.disconnect() y
// WiFi.mode(WIFI_OFF) hablan con el C6 igual que el encendido: en loopTask
// tienen el mismo riesgo de watchdog.
#if FLEXOS_ENABLE_WIFI
static volatile bool gWifiOffBusy = false;
static void wifiOffTask(void*){
  FLEXDIAG_WIFI("antes de apagar la radio");
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  gNetOnline = false;
  gWifiDriverOn = false;
  FLEXDIAG_WIFI("radio apagada");
  gWifiOffBusy = false;
  vTaskDelete(NULL);
}
#endif
// Entrada BARATA para la interfaz: pide el apagado y vuelve en el acto.
static void wifiRequestOff(){
#if FLEXOS_ENABLE_WIFI
  gNetOnline = false;                       // el estado logico cambia YA
  if(!gWifiDriverOn || gWifiOffBusy) return;
  gWifiOffBusy = true;
  if(xTaskCreatePinnedToCore(wifiOffTask, "wifiOff", 4096, NULL, 1, NULL, 1) != pdPASS){
    gWifiOffBusy = false;                   // sin tarea: se queda encendida, pero nadie se cuelga
    Serial.println(F("[WiFi] no se pudo crear la tarea de apagado"));
  }
#endif
}

#if FLEXOS_ENABLE_WIFI
static void wifiScanTask(void*){
  FLEXDIAG_WIFI("wifiScanTask: entrada");
  if(!wifiEnsureStaMode()){ wifiUIState = WUI_FAIL; vTaskDelete(NULL); return; }
  FLEXDIAG_WIFI("antes de WiFi.scanNetworks");
  int n = WiFi.scanNetworks();
  FLEXDIAG_WIFI("despues de WiFi.scanNetworks");                // bloqueante, pero en su PROPIA tarea: loop() sigue vivo
  // ANTI-CRASH: construir la lista FUERA de toda seccion critica. La version
  // anterior copiaba dentro de portENTER_CRITICAL(&wifiMux), pero WiFi.SSID()
  // devuelve un String (malloc) y ademas toca el driver WiFi. Hacer malloc con
  // las interrupciones deshabilitadas y un spinlock tomado puede (a) bloquear
  // el lock interno del heap -> deadlock, o (b) mantener las IRQ apagadas
  // demasiado tiempo -> disparar el watchdog de interrupciones (INT_WDT) y
  // reiniciar el ESP32. wifiNets[] SOLO lo escribe esta tarea; la UI lee unica-
  // mente indices [0, wifiNetCount). Por eso basta con publicar wifiNetCount AL
  // FINAL, bajo una seccion critica minima (barrera de memoria): la UI nunca ve
  // una entrada a medio escribir y no hay ninguna asignacion bajo el spinlock.
  int cnt = 0;
  if(n > 0){
    for(int i = 0; i < n && cnt < WIFI_MAX_NETS; i++){
      String ss = WiFi.SSID(i);
      if(ss.length() == 0) continue;                                // oculta redes sin nombre
      bool dup = false;
      for(int k = 0; k < cnt; k++) if(!strcmp(wifiNets[k].ssid, ss.c_str())){ dup = true; break; }
      if(dup) continue;                                             // mismo SSID visto en varios canales
      ss.toCharArray(wifiNets[cnt].ssid, sizeof(wifiNets[cnt].ssid));
      wifiNets[cnt].rssi   = (int8_t)WiFi.RSSI(i);
      wifiNets[cnt].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      cnt++;
    }
  }
  portENTER_CRITICAL(&wifiMux); wifiNetCount = cnt; portEXIT_CRITICAL(&wifiMux);  // publicacion atomica del contador
  WiFi.scanDelete();
  wifiUIState = WUI_LIST;                     // n<=0 -> lista vacia (mensaje "sin redes"), no es un error fatal
  vTaskDelete(NULL);
}
static void wifiConnTask(void*){
  if(!wifiEnsureStaMode()){ wifiUIState = WUI_FAIL; vTaskDelete(NULL); return; }
  FLEXDIAG_WIFI("antes de WiFi.begin (manual)");
  WiFi.begin(wifiConnSSID, wifiConnPass);
  uint32_t t0 = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - t0 < FLEXOS_WIFI_TIMEOUT_MS){
    vTaskDelay(pdMS_TO_TICKS(200));           // cede CPU -> no molesta a nadie, no dispara TWDT
  }
  if(WiFi.status() == WL_CONNECTED){
    gNetOnline = true;
    IPAddress ip = WiFi.localIP();
    String ips = ip.toString();
    ips.toCharArray(wifiConnIP, sizeof(wifiConnIP));
    // PERSISTENCIA: solo se guarda lo que YA se ha comprobado que
    // funciona. Guardar antes de confirmar dejaria una clave erronea
    // fija en NVS y el equipo reintentaria con ella en cada arranque.
    wifiCredsSave(wifiConnSSID, wifiConnPass);
    wifiAutoGuardWrite(true, false);       // ya se comprobo manualmente este transporte
    ntpOnWifiUp();                          // hay red: la hora real ya se puede pedir
    wifiUIState = WUI_OK;
  } else {
    gNetOnline = false;
    WiFi.disconnect(true, true);              // libera el intento fallido, no deja el C6 a medias
    WiFi.mode(WIFI_OFF);
    gWifiDriverOn = false;
    wifiUIState = WUI_FAIL;
  }
  vTaskDelete(NULL);
}

// ---- CONEXION MANUAL A LA RED GUARDADA ----------------------------
// La llama el interruptor de Wi-Fi del panel, nunca el arranque. Corre en
// su propia tarea y no toca wifiUIState porque esa variable pertenece a la
// pantalla detallada de Wi-Fi.
static void wifiAutoConnTask(void*){
  Serial.printf("[WiFi] conexion solicitada a \"%s\"...\n", wifiSavedSSID);
  if(!wifiEnsureStaMode()){
    if(gWifiBootAttempt) wifiAutoGuardWrite(false, false);
    gWifiBootAttempt = false; gWifiAutoBusy = false; gWifiAutoDone = true;
    vTaskDelete(NULL); return;
  }
  FLEXDIAG_WIFI("antes de WiFi.begin (auto)");
  WiFi.begin(wifiSavedSSID, wifiSavedPass);
  uint32_t t0 = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - t0 < FLEXOS_WIFI_TIMEOUT_MS){
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if(WiFi.status() == WL_CONNECTED){
    gNetOnline = true;
    IPAddress ip = WiFi.localIP();
    String ips = ip.toString();
    ips.toCharArray(wifiConnIP, sizeof(wifiConnIP));
    strncpy(wifiConnSSID, wifiSavedSSID, sizeof(wifiConnSSID) - 1);
    wifiConnSSID[sizeof(wifiConnSSID) - 1] = 0;
    ntpOnWifiUp();                          // hay red: la hora real ya se puede pedir
    wifiAutoGuardWrite(true, false);        // manual o automatico: termino limpiamente
    Serial.printf("[WiFi] conectado. IP %s\n", wifiConnIP);
  } else {
    // Fallo (router apagado, clave cambiada, fuera de alcance): se suelta
    // la radio y NO se borra nada. Las credenciales siguen guardadas para
    // el proximo arranque; el usuario puede entrar a Ajustes > Wi-Fi y
    // configurar a mano, que es el camino de siempre.
    gNetOnline = false;
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    gWifiDriverOn = false;
    // Un router apagado no invalida el hardware. Solo se limpia AUTOTRY para
    // que el siguiente arranque no interprete este fallo normal como PANIC.
    if(gWifiBootAttempt) wifiAutoGuardWrite(gWifiAutoTrusted, false);
    Serial.println(F("[WiFi] conexion fallida -> queda la configuracion manual"));
  }
  gWifiBootAttempt = false;
  gWifiAutoBusy = false;
  gWifiAutoDone = true;
  vTaskDelete(NULL);
}

// Lanza la conexion pedida por el usuario si hay una red guardada.
static void wifiTryAutoConnect(bool bootAttempt){
  if(gWifiAutoBusy || gWifiAutoDone) return;
  if(!gWifiHostedPinsOk || !wifiCredsExist()){
    gWifiAutoDone = true;
    Serial.println(F("[WiFi] transporte o red no disponible -> configuracion manual"));
    return;
  }
  if(bootAttempt && !gWifiAutoTrusted){
    gWifiAutoDone = true;
    Serial.println(F("[WiFi] autoarranque no validado -> se requiere conexion manual"));
    return;
  }
  gWifiBootAttempt = bootAttempt;
  if(bootAttempt) wifiAutoGuardWrite(true, true); // queda a 1 si ocurre PANIC/reset
  gWifiAutoBusy = true;
  BaseType_t made = xTaskCreatePinnedToCore(wifiAutoConnTask, "wifiAuto", 8192, NULL, 1, NULL, 1);
  if(made != pdPASS){
    if(bootAttempt) wifiAutoGuardWrite(gWifiAutoTrusted, false);
    gWifiBootAttempt = false; gWifiAutoBusy = false; gWifiAutoDone = true;
    gWifiDriverOn = false;                 // el interruptor no puede quedarse encendido en falso
    Serial.println(F("[WiFi] no se pudo crear la tarea de conexion"));
  }
}
#endif   // FLEXOS_ENABLE_WIFI

// Reconecta una sola vez, ya con Home/Bloqueo operativo. Solo queda armada
// despues de una conexion manual correcta; el intento lleva ademas el fusible
// persistente definido arriba para impedir un bucle de PANIC.
static void wifiAutoReconnectTick(){
#if FLEXOS_ENABLE_WIFI
  if(gWifiAutoDone || gWifiAutoBusy) return;
  if(gState != ST_HOME && gState != ST_LOCK) return;
  if(millis() < FLEXOS_WIFI_AUTOCONN_DELAY_MS) return;
  if(!wifiCredsExist()){ gWifiAutoDone = true; return; }
  wifiTryAutoConnect(true);
#endif
}

// ¿Hay una operacion de radio EN VUELO? Se compone de banderas que YA existian
// -- no se anade estado nuevo ni un mutex global. La usa el gestor de memoria
// para no ponerse a liberar buffers en el mismo instante en que esp-hosted
// esta levantando el enlace SDIO con el C6: no es que una cosa corrompa a la
// otra (la radio vive en SRAM interna y lo que se suelta es PSRAM), es que el
// handshake es sensible al tiempo y el alivio automatico puede esperar unos
// segundos sin que nadie lo note.
static bool wifiRadioBusy(){
#if FLEXOS_ENABLE_WIFI
  return gWifiAutoBusy || gWifiOffBusy ||
         wifiUIState == WUI_SCANNING || wifiUIState == WUI_CONNECTING;
#else
  return false;
#endif
}

static void wifiStartScan(){
#if FLEXOS_ENABLE_WIFI
  // Comprobacion BARATA. Antes aqui se llamaba a wifiEnsureStaMode(), que
  // levanta esp-hosted DESDE loopTask -- y la tarea que se crea justo debajo
  // vuelve a hacerlo de todas formas en su primera linea. Era trabajo repetido
  // y, sobre todo, era el que podia pasarse del Task Watchdog (ver la regla de
  // oro). La radio la despierta la tarea, que no esta suscrita al TWDT.
  const char* why = NULL;
  if(!wifiTransportReady(&why)){
    Serial.printf("[C6] escaneo rechazado: %s\n", why ? why : "");
    wifiUIState = WUI_FAIL; return;
  }
  wifiUIState = WUI_SCANNING;
  portENTER_CRITICAL(&wifiMux); wifiNetCount = 0; portEXIT_CRITICAL(&wifiMux);
  // 8 KB: scanNetworks() + los String de los SSID necesitan mas que 6 KB.
  // LIMITE HONESTO: esta cifra viene de la placa, no de una prueba de host --
  // aqui no hay radio ni pila de driver que consuman pila de verdad.
  if(xTaskCreatePinnedToCore(wifiScanTask, "wifiScan", 8192, NULL, 1, NULL, 1) != pdPASS){
    wifiUIState = WUI_FAIL;
    gWifiDriverOn = false;
    Serial.println(F("[WiFi] no se pudo crear la tarea de escaneo"));
  }
#endif
}
static void wifiStartConnect(){
#if FLEXOS_ENABLE_WIFI
  if(wifiSel < 0 || wifiSel >= WIFI_MAX_NETS){ wifiUIState = WUI_LIST; return; }  // indice invalido -> nunca leer wifiNets[] fuera de rango
  const char* why = NULL;                    // barata: la radio la despierta wifiConnTask
  if(!wifiTransportReady(&why)){
    Serial.printf("[C6] conexion rechazada: %s\n", why ? why : "");
    wifiUIState = WUI_FAIL; return;
  }
  strncpy(wifiConnSSID, wifiNets[wifiSel].ssid, sizeof(wifiConnSSID) - 1); wifiConnSSID[sizeof(wifiConnSSID) - 1] = 0;
  strncpy(wifiConnPass, wifiPass, sizeof(wifiConnPass) - 1); wifiConnPass[sizeof(wifiConnPass) - 1] = 0;
  wifiUIState = WUI_CONNECTING;
  xTaskCreatePinnedToCore(wifiConnTask, "wifiConn", 8192, NULL, 1, NULL, 1);   // 8KB: WiFi.begin() + pila del driver necesitan mas que 6KB
#endif
}
static void wifiPassAppend(const char* s){ int L = strlen(wifiPass), sl = strlen(s); if(L + sl < (int)sizeof(wifiPass) - 1){ memcpy(wifiPass + L, s, sl); wifiPass[L + sl] = 0; } }
static void wifiPassBackspace(){ int L = strlen(wifiPass); if(L > 0){ int q = L - 1; while(q > 0 && (wifiPass[q] & 0xC0) == 0x80) q--; wifiPass[q] = 0; } }
static void wifiBack(){ strokeSegAA(30, 26, 18, 18, 2.4f, TH_NAV); strokeSegAA(18, 18, 30, 10, 2.4f, TH_NAV); }

static int wifiRowY(int i){ return 150 + i * 66; }

// Geometria de los botones del pie. UNA sola fuente para el dibujo y para
// el tap: si el "Olvidar red" aparece o desaparece segun haya red guardada,
// las dos rutas cambian a la vez y la zona pulsable no puede desalinearse
// de lo pintado (mismo criterio que setRowCard/setRowY0 en Ajustes).
static void wifiBtnRects(int& by, int& sx, int& sw, int& fx, int& fw){
  by = SCR_H - 90;
  if(wifiCredsExist()){                       // dos botones lado a lado
    int w = (SCR_W - 48 - 12) / 2;
    sx = 24;              sw = w;
    fx = 24 + w + 12;     fw = w;
  } else {                                    // solo "Buscar redes", centrado
    sx = SCR_W / 2 - 110; sw = 220;
    fx = 0;               fw = 0;
  }
}

static void wifiRenderList(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  wifiBack();
  drawTextC(SCR_W / 2, 60, "Wi-Fi", 4, TH_TXT);
  // Estado de la red recordada, justo bajo el titulo.
  if(wifiCredsExist()){
    char sv[64];
    bool on = gNetOnline;
    snprintf(sv, sizeof(sv), "%s %s", on ? "Conectado a" : "Red guardada:", wifiSavedSSID);
    drawTextC(SCR_W / 2, 112, sv, 1, on ? TH_OK : TH_TXT2);   // "conectado" = estado de exito (se conserva en los dos temas)
  } else if(gWifiAutoBusy){
    drawTextC(SCR_W / 2, 112, "Reconectando...", 1, TH_TXT2);
  }
  int cnt; portENTER_CRITICAL(&wifiMux); cnt = wifiNetCount; portEXIT_CRITICAL(&wifiMux);
  if(wifiUIState == WUI_SCANNING){
    drawTextC(SCR_W / 2, 300, "Buscando redes...", 2, TH_TXT2);
  } else if(cnt == 0){
    drawTextC(SCR_W / 2, 300, "No se encontraron redes", 2, TH_TXT2);
  } else {
    for(int i = 0; i < cnt; i++){
      int y = wifiRowY(i); if(y > SCR_H - 60) break;
      if(uiGlass) drawGlassCardFlat(24, y, SCR_W - 48, 56, 14, TH_GLASS, TH_PAGE);
      else        fillRoundRect(24, y, SCR_W - 48, 56, 14, TH_SURF);
      drawWifi(56, y + 28, 13, TH_TXT);
      drawTextClip(88, y + 12, wifiNets[i].ssid, 2, TH_TXT, SCR_W - 100);
      if(wifiNets[i].secure){
        fillRoundRect(SCR_W - 80, y + 20, 16, 14, 3, TH_TXT2);
        arcStroke(SCR_W - 72, y + 20, 6, 180, 360, 2, TH_TXT2);
      }
    }
  }
  int by, sx, sw, fx, fw;
  wifiBtnRects(by, sx, sw, fx, fw);
  fillRoundRect(sx, by, sw, 56, 16, TH_PRIM);                   // "Buscar redes" (reescanear)
  drawTextC(sx + sw / 2, by + 18, wifiUIState == WUI_SCANNING ? "Buscando..." : "Buscar redes", 2, TH_ONACC);
  if(fw > 0){                                                   // "Olvidar red" (cambio de router): accion destructiva
    fillRoundRect(fx, by, fw, 56, 16, TH_SURF2);
    drawTextC(fx + fw / 2, by + 18, "Olvidar red", 2, TH_DANGER);
  }
  flxFlushAll();
}
static void wifiRenderPass(int yoff){
  setBuf(bbuf);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  wifiBack();
  char title[48]; snprintf(title, sizeof(title), "Contrase\xC3\xB1" "a de %s", wifiNets[wifiSel].ssid);
  drawTextC(SCR_W / 2, 50, title, 2, TH_TXT);
  int cnt = utf8Count(wifiPass);
  for(int i = 0; i < cnt && i < 18; i++) fillCircle(30 + i * 24, 120, 7, TH_PRIM);
  int ky = KB_Y + yoff;
  if(uiGlass){ drawLiquidGlassPanel(0, ky - 4, SCR_W, SCR_H - (ky - 4), 0, kbColPanel()); }
  else fillRect(0, ky - 4, SCR_W, SCR_H - (ky - 4), kbColPanel());
  int fs = kbFontSize();
  for(int r = 0; r < KB_ROWS; r++) for(int c = 0; c < KB_COLS; c++){
    int x = KB_X + c * (KB_KW + KB_GAP), y = ky + r * (KB_KH + KB_GAP);
    char u[6];
    const char* k = kbResolveKey(mapaActivo[r][c], u, false);
    kbPaintKey(x, y, KB_KW, KB_KH, k, fs, kbColKey(), kbColKeyTxt(), false);
  }
  int fy = ky + 3 * (KB_KH + KB_GAP);
  const char* lb[KB_FKEYS] = { "shift", kbLayerLabel(), kbLangEs ? "ES" : "EN", "espacio", "<-", "Conectar" };
  for(int i = 0; i < KB_FKEYS; i++) kbFKey(kbFKeyX(i), fy, kbFKeyW(i), lb[i], (i == 0) && kbShift);
  present(0, SCR_H - 1);
}
static void wifiRenderStatus(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  wifiBack();
  if(wifiUIState == WUI_OK){
    // El circulo y el tick son ESTADO (exito): conservan su significado en las
    // dos apariencias, solo se toma el verde de la paleta activa.
    drawCircle(SCR_W / 2, 280, 46, TH_OK); drawCircle(SCR_W / 2, 280, 45, TH_OK);
    strokeSegAA(SCR_W / 2 - 20, 280, SCR_W / 2 - 4, 300, 4.0f, TH_OK);
    strokeSegAA(SCR_W / 2 - 4, 300, SCR_W / 2 + 26, 260, 4.0f, TH_OK);
    drawTextC(SCR_W / 2, 350, "Conectado", 3, TH_TXT);
    char ipl[40]; snprintf(ipl, sizeof(ipl), "IP: %s", wifiConnIP);
    drawTextC(SCR_W / 2, 390, ipl, 2, TH_TXT2);
    fillRoundRect(SCR_W / 2 - 100, SCR_H - 120, 200, 56, 16, TH_PRIM);
    drawTextC(SCR_W / 2, SCR_H - 102, "Listo", 2, TH_ONACC);
  } else if(wifiUIState == WUI_CONNECTING){
    drawTextC(SCR_W / 2, 280, "Conectando...", 3, TH_TXT);
    char sub[48]; snprintf(sub, sizeof(sub), "a %s", wifiConnSSID);
    drawTextC(SCR_W / 2, 320, sub, 2, TH_TXT2);
  } else {                                     // WUI_FAIL
    drawCircle(SCR_W / 2, 280, 46, TH_ERR); drawCircle(SCR_W / 2, 280, 45, TH_ERR);
    strokeSegAA(SCR_W / 2 - 14, 264, SCR_W / 2 + 14, 296, 4.0f, TH_ERR);
    strokeSegAA(SCR_W / 2 + 14, 264, SCR_W / 2 - 14, 296, 4.0f, TH_ERR);
    drawTextC(SCR_W / 2, 350, "No se pudo conectar", 3, TH_TXT);
    drawTextC(SCR_W / 2, 388, "Contrase\xC3\xB1" "a incorrecta o red fuera de rango", 1, TH_TXT2);
    fillRoundRect(SCR_W / 2 - 210, SCR_H - 120, 200, 56, 16, TH_SURF2);
    drawTextC(SCR_W / 2 - 110, SCR_H - 102, "Cancelar", 2, TH_TXT);
    fillRoundRect(SCR_W / 2 + 10, SCR_H - 120, 200, 56, 16, TH_PRIM);
    drawTextC(SCR_W / 2 + 110, SCR_H - 102, "Reintentar", 2, TH_ONACC);
  }
  flxFlushAll();
}
static void wifiRenderUnavail(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  wifiBack();
  drawTextC(SCR_W / 2, 60, "Wi-Fi", 4, TH_TXT);
#if FLEXOS_WIFI_CORE_UNSAFE
  drawTextC(SCR_W / 2, 286, "Core ESP32 incompatible", 2, TH_ERR);
  drawTextC(SCR_W / 2, 326, "Instala arduino-esp32 3.2.1", 2, TH_TXT2);
  drawTextC(SCR_W / 2, 356, "La version 3.2.0 puede reiniciar el P4/C6", 1, TH_MUTE);
#else
  drawTextC(SCR_W / 2, 300, "Wi-Fi desactivado en este build", 2, TH_TXT2);
  drawTextC(SCR_W / 2, 334, "(FLEXOS_ENABLE_WIFI = 0 en el .ino)", 1, TH_MUTE);
#endif
  flxFlushAll();
}
// Repinta la pantalla de Wi-Fi que este a la vista. Lo usa themeChanged() para
// que un cambio de tema hecho desde el Modo PC (o desde cualquier otra ruta que
// deje Wi-Fi abierto) se vea al momento, sin tocar la maquina de estados.
static void wifiSettingsRepaint(){
#if FLEXOS_ENABLE_WIFI
  if(wifiUIState == WUI_PASS)                                          wifiRenderPass(0);
  else if(wifiUIState == WUI_LIST || wifiUIState == WUI_SCANNING)       wifiRenderList();
  else                                                                  wifiRenderStatus();
#else
  wifiRenderUnavail();
#endif
}
static int wifiReturnState = ST_APP;
static void wifiExit(){
  // Si solo se abrio el escaner y no se establecio conexion, se suelta
  // esp-hosted para no retener la radio innecesariamente.
  if(gWifiDriverOn && !gNetOnline) wifiRequestOff();   // en tarea: nunca en loopTask
  // accountResumeEnter (y no accountOobeEnter): la pantalla de Cuenta vuelve
  // con el MISMO destino de salida que tenia antes de venir a configurar la
  // red. Con accountOobeEnter, entrar aqui desde Flex Store o desde Ajustes
  // convertia la pantalla en primer arranque y su boton acababa en el bloqueo.
  if(wifiReturnState == ST_OOBE_ACCOUNT){ accountResumeEnter(); return; }
  gState = ST_APP; settingsRender();
}

static void wifiSettingsEnter(){
  wifiReturnState = ST_APP;
  gState = ST_WIFI;
#if FLEXOS_ENABLE_WIFI
  wifiSel = -1; wifiPass[0] = 0;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbExtrasOn = false; kbBotReserve = 0; kbApplySize(); kbMtSurfaceReset();   // el teclado de Wi-Fi solo hereda el TAMANO (Fase A)
  wifiStartScan();
  if(wifiUIState == WUI_FAIL) wifiRenderStatus(); else wifiRenderList();
#else
  wifiRenderUnavail();
#endif
}
// Primera configuracion reutiliza el Wi-Fi real y vuelve a Flex Account.
static void wifiOobeEnter(){
  wifiReturnState = ST_OOBE_ACCOUNT;
  gState = ST_WIFI;
#if FLEXOS_ENABLE_WIFI
  wifiSel = -1; wifiPass[0] = 0;
  mapaActivo = LAYOUT_ES; kbLangEs = true; kbShift = false;
  kbExtrasOn = false; kbApplySize(); kbMtSurfaceReset();
  wifiStartScan();
  if(wifiUIState == WUI_FAIL) wifiRenderStatus(); else wifiRenderList();
#else
  wifiRenderUnavail();
#endif
}
static void wifiTick(){
#if !FLEXOS_ENABLE_WIFI
  if(T.tap && T.x < 48 && T.y < 48) wifiExit();
  return;
#else
  // Repinta UNA vez cuando el estado cambia por causas externas (la
  // tarea de escaneo/conexion en Core 1 termino). Las transiciones que
  // dispara el propio tap ya pintan de inmediato mas abajo.
  static int wifiUIStateShown = -1;
  if(wifiUIState != wifiUIStateShown){
    wifiUIStateShown = wifiUIState;
    if(wifiUIState == WUI_LIST || wifiUIState == WUI_SCANNING) wifiRenderList();
    else if(wifiUIState == WUI_CONNECTING || wifiUIState == WUI_OK || wifiUIState == WUI_FAIL) wifiRenderStatus();
  }
  switch(wifiUIState){
    case WUI_LIST: {
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiExit(); return; }
        int by, sx, sw, fx, fw;
        wifiBtnRects(by, sx, sw, fx, fw);
        if(T.y >= by && T.y <= by + 56){
          if(T.x >= sx && T.x <= sx + sw){ wifiStartScan(); return; }
          if(fw > 0 && T.x >= fx && T.x <= fx + fw){       // Olvidar red guardada
            wifiCredsForget();
            wifiRequestOff();                              // suelta la sesion en curso, en tarea
            wifiRenderList();                              // el boton desaparece al no haber red
            return;
          }
        }
        int cnt; portENTER_CRITICAL(&wifiMux); cnt = wifiNetCount; portEXIT_CRITICAL(&wifiMux);
        for(int i = 0; i < cnt; i++){
          int y = wifiRowY(i); if(y > SCR_H - 60) break;
          if(T.y >= y && T.y <= y + 56 && T.x >= 24 && T.x <= SCR_W - 24){
            wifiSel = i;
            if(!wifiNets[i].secure) wifiStartConnect();                          // red abierta: conecta directo
            else { wifiPass[0] = 0; wifiUIState = WUI_PASS; wifiKbAnim = millis(); }
            return;
          }
        }
      }
      break;
    }
    case WUI_SCANNING: break;    // pantalla estatica "Buscando..."; el repintado de arriba cambia a LIST solo
    case WUI_PASS: {
      if(wifiKbAnim){
        float p = (millis() - wifiKbAnim) / 300.0f; if(p >= 1){ p = 1; wifiKbAnim = 0; }
        wifiRenderPass((int)((1.0f - p) * (SCR_H - KB_Y)));
        return;
      }
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiUIState = WUI_LIST; wifiRenderList(); return; }
        int fi = kbFRowHit(T.x, T.y);
        if(fi >= 0){
          if(fi == 0) kbShift = !kbShift;
          else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
          else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
          else if(fi == 3) wifiPassAppend(" ");
          else if(fi == 4) wifiPassBackspace();
          else if(strlen(wifiPass) > 0){ wifiStartConnect(); return; }
          wifiRenderPass(0); return;
        }
        int cell = kbCellAt(T.x, T.y);
        if(cell >= 0){
          char u[6];
          const char* k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, true);
          wifiPassAppend(k);
          wifiRenderPass(0);
        }
      }
      break;
    }
    case WUI_CONNECTING: break;  // pantalla estatica "Conectando..."; el repintado de arriba cambia a OK/FAIL solo
    case WUI_OK: {
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiExit(); return; }
        if(T.y >= SCR_H - 120 && T.y <= SCR_H - 64 && T.x >= SCR_W/2 - 100 && T.x <= SCR_W/2 + 100){ wifiExit(); return; }
      }
      break;
    }
    case WUI_FAIL: {
      if(T.tap){
        if(T.x < 48 && T.y < 48){ wifiUIState = WUI_LIST; wifiRenderList(); return; }
        if(T.y >= SCR_H - 120 && T.y <= SCR_H - 64){
          if(T.x >= SCR_W/2 - 210 && T.x <= SCR_W/2 - 10){ wifiUIState = WUI_LIST; wifiRenderList(); return; }             // cancelar
          if(T.x >= SCR_W/2 + 10 && T.x <= SCR_W/2 + 210){ wifiPass[0] = 0; wifiUIState = WUI_PASS; wifiKbAnim = millis(); return; }  // reintentar
        }
      }
      break;
    }
  }
#endif
}

// #############################################################
