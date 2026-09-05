// #############################################################
// ##  FLEX OS ULTRA  ·  CONECTIVIDAD  ·  Wi-Fi / BLE / Modo avion  (ST_CONN)
// ##  ----------------------------------------------------------
// ##  Los tres interruptores encienden y apagan radio DE VERDAD; ninguno es
// ##  un booleano que solo cambia de color.
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
#include "FlexOS_Ultra_NTP.h"   // eslabon anterior de la cadena

// #############################################################
// ##  CONECTIVIDAD  ·  Wi-Fi / BLE / Modo avion   (ST_CONN)
// ##  ------------------------------------------------------
// ##  Los tres interruptores encienden y apagan radio DE
// ##  VERDAD. Ninguno es un booleano que solo cambia de color:
// ##
// ##   · Wi-Fi     -> WiFi.mode(WIFI_STA) + WiFi.begin() con la
// ##                  red guardada, o WiFi.scanNetworks() (en su
// ##                  tarea) si aun no hay ninguna. Al apagar,
// ##                  WiFi.disconnect(true,true) + WIFI_OFF.
// ##                  El subtitulo es WiFi.SSID(): el nombre
// ##                  REAL de la red a la que se esta conectado.
// ##   · BLE       -> BLEDevice::init() + advertising real (el
// ##                  equipo aparece como "FlexOS" en cualquier
// ##                  movil) y BLEDevice::deinit(true) al
// ##                  apagar. En una placa SIN radio Bluetooth
// ##                  el interruptor queda deshabilitado con la
// ##                  etiqueta "No disponible", y eso se decide
// ##                  en COMPILACION mirando SOC_BLE_SUPPORTED
// ##                  del propio SDK -- no esta escrito a mano
// ##                  para un chip concreto.
// ##   · Modo avion-> apaga las dos radios llamando a las
// ##                  funciones reales de apagado y deja los
// ##                  otros dos interruptores bloqueados
// ##                  mientras este activo. Se guarda en NVS y,
// ##                  si estaba activo, Wi-Fi permanece apagado.
// #############################################################
#define CONN_CARD_X   14
#define CONN_CARD_W   (SCR_W - 28)
#define CONN_ROW_H    112
#define CONN_CARD1_Y  110
#define CONN_CARD2_Y  (CONN_CARD1_Y + 2 * CONN_ROW_H + 26)

// (gAirplane y gBleOn se declaran arriba del todo, junto a gNetOnline: la
//  pantalla de Ajustes -- que esta ANTES en el archivo -- necesita leerlos.)

// ---- BLE real -------------------------------------------------
// FLEXOS_BLE_HW lo decide el SDK (soc_caps.h) segun el chip que se
// esta compilando. FLEXOS_ENABLE_BLE permite ademas apagarlo a mano
// cuando la placa SI tiene radio pero no sobra flash para la pila BLE
// (ver la cabecera del sketch de la placa correspondiente).
static bool flexBleStart(){
#if FLEXOS_ENABLE_BLE
  if(gBleOn) return true;
  BLEDevice::init("FlexOS");
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  if(!adv){ BLEDevice::deinit(true); return false; }
  adv->setScanResponse(true);
  adv->start();                      // desde aqui el equipo es visible de verdad
  gBleOn = true;
  Serial.println(F("[BLE] advertising activo como \"FlexOS\""));
  return true;
#else
  return false;
#endif
}

static void flexBleStop(){
#if FLEXOS_ENABLE_BLE
  if(!gBleOn) return;
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  if(adv) adv->stop();
  BLEDevice::deinit(true);           // libera el controlador, no solo la capa alta
  gBleOn = false;
  Serial.println(F("[BLE] apagado"));
#endif
}

// ---- Wi-Fi real -----------------------------------------------
static bool connWifiOn(){
#if FLEXOS_ENABLE_WIFI
  // Estado propio: WiFi.getMode() tambien toca esp-hosted en el P4.
  return gWifiDriverOn;
#else
  return false;
#endif
}

static void connWifiSet(bool on){
#if FLEXOS_ENABLE_WIFI
  if(on){
    if(gAirplane) return;                       // bloqueo real, no visual
    // BARATA, y en el hilo de la interfaz eso es obligatorio: el interruptor
    // del panel rapido corre dentro de loopTask. Quien despierta la radio es
    // la tarea que crean wifiTryAutoConnect() o wifiStartScan().
    const char* why = NULL;
    if(!wifiTransportReady(&why)){
      Serial.printf("[C6] encendido rechazado: %s\n", why ? why : "");
      return;
    }
    // El interruptor refleja la INTENCION en el acto. gWifiDriverOn es una
    // bandera de Flex OS, no una consulta al driver, asi que ponerla aqui no
    // toca esp-hosted; quien la baja si el intento fracasa son las propias
    // tareas. Sin esto el interruptor se quedaria "apagado" hasta que la
    // tarea llegara a ejecutarse, y el usuario volveria a pulsarlo.
    gWifiDriverOn = true;
    if(wifiCredsExist()){
      // Hay red guardada -> WiFi.begin() real contra ella, en su tarea
      // (loop() no se bloquea; misma ruta que la reconexion de arranque).
      gWifiAutoDone = false; gWifiAutoBusy = false;
      wifiTryAutoConnect(false);
    } else {
      wifiStartScan();                          // sin red guardada -> escaneo real
    }
  } else {
    // El apagado tambien habla con el C6: va a su propia tarea (wifiOffTask).
    // Hacerlo aqui bloqueaba loopTask durante todo el cierre del enlace.
    gWifiAutoDone = true;                       // que no vuelva a encenderse sola
    wifiRequestOff();
  }
#else
  (void)on;
#endif
}

// Subtitulo del Wi-Fi: SSID REAL, nunca un nombre de ejemplo.
static void connWifiSub(char* out, size_t n){
#if !FLEXOS_ENABLE_WIFI
  snprintf(out, n, "(No disponible)");
#else
  if(gAirplane){ snprintf(out, n, "(Modo avi\xC3\xB3n)"); return; }
  if(!connWifiOn()){ snprintf(out, n, "(Desactivado)"); return; }
  if(gNetOnline){
    const char* s = wifiActiveSSID();
    if(s[0]) snprintf(out, n, "(%s)", s);
    else     snprintf(out, n, "(Conectado)");
    return;
  }
  if(gWifiAutoBusy)               { snprintf(out, n, "(Conectando...)"); return; }
  if(wifiUIState == WUI_SCANNING) { snprintf(out, n, "(Buscando redes...)"); return; }
  snprintf(out, n, "(No conectado)");
#endif
}

static void connBleSub(char* out, size_t n){
#if !FLEXOS_BLE_HW
  // El chip que se esta compilando no tiene radio Bluetooth (caso del
  // ESP32-P4, que delega el Wi-Fi en un C6 pero no expone BLE).
  snprintf(out, n, "(No disponible)");
#elif !FLEXOS_ENABLE_BLE
  snprintf(out, n, "(Desactivado al compilar)");
#else
  if(gAirplane)   snprintf(out, n, "(Modo avi\xC3\xB3n)");
  else if(gBleOn) snprintf(out, n, "(Visible como \"FlexOS\")");
  else            snprintf(out, n, "(Desactivado)");
#endif
}

static void connSaveState(){
  prefs.begin("flexos", false);
  prefs.putBool("airpl", gAirplane);
  prefs.end();
}

// Se llama desde setup(). Solo lee NVS (flash, no radio): sigue en pie la
// regla de que el arranque NO toca la radio.
static void connBootRestore(){
  prefs.begin("flexos", true);
  gAirplane = prefs.getBool("airpl", false);
  prefs.end();
#if FLEXOS_ENABLE_WIFI
  // Si el equipo se apago en modo avion, el arranque no debe reconectar
  // solo: seria encender una radio que el usuario dejo apagada.
  if(gAirplane) gWifiAutoDone = true;
#endif
  if(gAirplane) Serial.println(F("[RADIO] modo avion activo desde el arranque"));
}

static void connAirplaneSet(bool on){
  gAirplane = on;
  if(on){
    flexBleStop();          // apagado real de las dos radios
    connWifiSet(false);
  }
  connSaveState();
}

// ---- Interruptor (pastilla) -----------------------------------
static void connPill(int x, int y, int w, int h, bool on, bool enabled){
  uint16_t track = !enabled ? TH_DIS : (on ? TH_OK : TH_TRACK);
  fillRoundRect(x, y, w, h, h / 2, track);
  int r = h / 2 - 4;
  int cx = on ? (x + w - h / 2) : (x + h / 2);
  fillCircle(cx, y + h / 2, r, TH_ONACC);
  if(!enabled){                                  // aspa tenue: no se puede tocar
    strokeSegAA(cx - 6, y + h / 2 - 6, cx + 6, y + h / 2 + 6, 1.6f, TH_MUTE);
    strokeSegAA(cx + 6, y + h / 2 - 6, cx - 6, y + h / 2 + 6, 1.6f, TH_MUTE);
  }
}

// Geometria de las tres filas. UNA sola fuente para dibujo y para el
// tap: no puede desalinearse lo pintado de lo pulsable.
static void connRowRect(int i, int &x, int &y, int &w, int &h){
  x = CONN_CARD_X; w = CONN_CARD_W; h = CONN_ROW_H;
  if(i == 0)      y = CONN_CARD1_Y;
  else if(i == 1) y = CONN_CARD1_Y + CONN_ROW_H;
  else            y = CONN_CARD2_Y;
}

static bool connRowEnabled(int i){
  if(i == 2) return true;                        // modo avion siempre se puede tocar
  if(gAirplane) return false;                    // con el avion puesto, los otros dos no
  if(i == 0) return FLEXOS_ENABLE_WIFI ? true : false;
  return FLEXOS_ENABLE_BLE ? true : false;
}

static bool connRowOn(int i){
  if(i == 0) return connWifiOn();
  if(i == 1) return gBleOn;
  return gAirplane;
}

// Solo el CONTENIDO de la fila (titulo, subtitulo y pastilla). El fondo lo
// pinta connPaintCards con esquinas redondeadas: si cada fila se rellenara a si
// misma con un rectangulo recto, se comeria las esquinas de la tarjeta.
static void connDrawRow(int i){
  int x, y, w, h; connRowRect(i, x, y, w, h);
  uint16_t tx = TH_TXT, sb = TH_TXT2;
  const char* title = (i == 0) ? "Wifi" : (i == 1) ? "BLE" : "Modo avi\xC3\xB3n";
  char sub[64]; sub[0] = 0;
  if(i == 0) connWifiSub(sub, sizeof(sub));
  else if(i == 1) connBleSub(sub, sizeof(sub));
  bool en = connRowEnabled(i);
  drawText(x + 22, y + (sub[0] ? 16 : 36), title, 5, en ? tx : TH_DIS);
  if(sub[0]) drawTextClip(x + 22, y + 62, sub, 3, en ? sb : TH_MUTE, x + w - 130);
  connPill(x + w - 116, y + h / 2 - 22, 100, 44, connRowOn(i), en);
}

// Las dos tarjetas, enteras. Se pinta primero el fondo redondeado y despues
// el contenido de cada fila: asi el repintado parcial (connRefresh) deja
// exactamente el mismo resultado que el completo.
static void connPaintCards(){
  if(uiGlass) drawLiquidGlassPanel(CONN_CARD_X, CONN_CARD1_Y, CONN_CARD_W, 2 * CONN_ROW_H, 24, TH_GLASS);
  else fillRoundRect(CONN_CARD_X, CONN_CARD1_Y, CONN_CARD_W, 2 * CONN_ROW_H, 24, TH_SURF);
  connDrawRow(0);
  connDrawRow(1);
  fillRect(CONN_CARD_X + 8, CONN_CARD1_Y + CONN_ROW_H - 1, CONN_CARD_W - 16, 2, TH_DIV);
  if(uiGlass) drawLiquidGlassPanel(CONN_CARD_X, CONN_CARD2_Y, CONN_CARD_W, CONN_ROW_H, 24, TH_GLASS);
  else fillRoundRect(CONN_CARD_X, CONN_CARD2_Y, CONN_CARD_W, CONN_ROW_H, 24, TH_SURF);
  connDrawRow(2);
}

static void connRender(){
  setBuf(fb);
  fillRect(0, 0, SCR_W, SCR_H, TH_PAGE);
  strokeSegAA(30, 26, 18, 18, 2.4f, TH_NAV);
  strokeSegAA(18, 18, 30, 10, 2.4f, TH_NAV);
  drawTextC(SCR_W / 2, 30, "Conectividad", 3, TH_TXT);
  connPaintCards();
  if(gAirplane)
    drawTextC(SCR_W / 2, CONN_CARD2_Y + CONN_ROW_H + 24,
              "Con el modo avi\xC3\xB3n activo, Wifi y BLE quedan apagados", 1, TH_MUTE);
  flxFlushAll();
}

// Repinta SOLO la banda de las tarjetas. Se usa cuando el estado cambia por
// causas externas (la tarea de conexion termino y ya hay SSID real): no se
// repinta la pantalla entera por un cambio de subtitulo.
static void connRefresh(){
  setBuf(fb);
  connPaintCards();
  flxFlush(CONN_CARD1_Y - 2, CONN_CARD2_Y + CONN_ROW_H + 2);
}

static char     connLastSub[64] = "";
static uint32_t connPollMs = 0;

static void connEnter(){
  gState = ST_CONN;
  connLastSub[0] = 0;
  connRender();
}

static void connExit(){ gState = ST_APP; settingsRender(); }

static void connTick(){
  // Refresco por cambio REAL de estado: se compara el subtitulo que tocaria
  // pintar con el ultimo pintado. Mientras no cambie nada, no se toca la
  // pantalla (ni un solo flxFlush de balde).
  //
  // La comprobacion va limitada a 4 veces por segundo A PROPOSITO: WiFi.SSID()
  // devuelve un String (malloc + free) y hacerlo en cada vuelta de loop() -que
  // corre cada ~5 ms- seria fragmentar el heap para nada. El SSID no cambia
  // 200 veces por segundo.
  if(millis() - connPollMs > 250){
    connPollMs = millis();
    char now[64]; connWifiSub(now, sizeof(now));
    if(strcmp(now, connLastSub)){
      strncpy(connLastSub, now, sizeof(connLastSub) - 1);
      connLastSub[sizeof(connLastSub) - 1] = 0;
      connRefresh();
    }
  }

  if(!T.tap) return;
  if(T.x < 60 && T.y < 60){ connExit(); return; }
  for(int i = 0; i < 3; i++){
    int x, y, w, h; connRowRect(i, x, y, w, h);
    if(T.x < x || T.x > x + w || T.y < y || T.y > y + h) continue;
    if(!connRowEnabled(i)) return;                       // deshabilitado: no hace nada
    if(i == 0)      connWifiSet(!connWifiOn());
    else if(i == 1){ if(gBleOn) flexBleStop(); else flexBleStart(); }
    else            connAirplaneSet(!gAirplane);
    connLastSub[0] = 0;                                  // fuerza el refresco de la banda
    connRender();
    return;
  }
}

// Icono del modulo: reutiliza los iconos de app existentes (mapeo simple)
static void drawModuleIcon(ModuleType type, int x, int y, int S){
  int id = IC_AJUSTES;
  switch(type){
    case MOD_ULTRASONIC:  id = IC_NAV;     break;
    case MOD_BME280:      id = IC_BIEN;    break;
    case MOD_MPU6050:     id = IC_JUEGOS;  break;
    case MOD_LED:         id = IC_CALC;    break;
    case MOD_BUTTON:      id = IC_NOTAS;   break;
    case MOD_SERVO:       id = IC_MODOPC;  break;
    case MOD_I2C_GENERIC: id = IC_ALMACEN; break;
    // Avisos del sistema llevan el icono de la app correspondiente.
    case MOD_MEDIA:       id = IC_MULTIMEDIA; break;
    default:              id = IC_AJUSTES; break;
  }
  drawAppIcon(id, x, y, S);
}
