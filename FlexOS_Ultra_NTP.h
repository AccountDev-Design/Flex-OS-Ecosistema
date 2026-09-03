// #############################################################
// ##  FLEX OS ULTRA  ·  HORA REAL POR NTP
// ##  ----------------------------------------------------------
// ##  Cliente UDP en su propia tarea. Publica la epoca con clkSetEpoch().
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
#include "FlexOS_Ultra_Network.h"   // eslabon anterior de la cadena

// ##  HORA REAL POR NTP  ·  Lima (UTC-5, sin horario de verano)
// ##  ------------------------------------------------------
// ##  COMO FUNCIONA
// ##    · Cliente SNTP propio sobre UDP (48 bytes, RFC 4330). No se
// ##      usa configTime()/lwIP-SNTP a proposito: ese arranca un
// ##      re-sondeo periodico propio que no se puede acotar desde
// ##      aqui, y el requisito es sincronizar cada 6 h, no de forma
// ##      continua. Con el socket propio, el ritmo lo marca este
// ##      fichero y el socket se cierra en cuanto termina.
// ##    · Todo ocurre en una tarea de fondo (Core 1, prioridad 1).
// ##      loop() NUNCA espera a la red: ntpTick() solo mira banderas
// ##      y lanza la tarea, y la tarea escribe el resultado en
// ##      variables volatiles. El renderizado no se entera.
// ##    · Al conectar el Wi-Fi se pide una sincronizacion. Despues,
// ##      una cada NTP_PERIOD_MS (6 h).
// ##    · Si falla, espera progresiva (30 s, 60 s, 2 min... hasta
// ##      30 min) en vez de martillear el servidor.
// ##    · Perder el Wi-Fi NO para el reloj: la hora sigue saliendo
// ##      del ancla (epoca + millis), que es independiente de la red.
// ##    · La ultima hora buena se guarda en NVS. Tras un reinicio sin
// ##      internet, el sistema arranca con esa hora APROXIMADA (nunca
// ##      salta a 1970) y lo dice en Ajustes hasta volver a sincronizar.
// #############################################################
// (WiFiUdp.h ya se incluye arriba del todo, junto a WiFi.h: la guarda del
//  propio encabezado hacia que este segundo #include no anadiera nada.)

#define NTP_PERIOD_MS     (6UL * 3600UL * 1000UL)   // resincronizacion: 6 h
#define NTP_FIRST_DELAY   1500                      // margen tras conectar antes del primer intento
#define NTP_TIMEOUT_MS    4000                      // espera maxima por respuesta de UN servidor
#define NTP_PORT          123
#define NTP_PKT_SIZE      48
#define NTP_EPOCH_DELTA   2208988800UL              // segundos entre 1900-01-01 y 1970-01-01
#define NTP_BACKOFF_MIN   30000UL                   // 30 s tras el primer fallo
#define NTP_BACKOFF_MAX   1800000UL                 // techo: 30 min

// Tres servidores publicos. Se prueban en orden y el primero que conteste
// gana; asi un servidor caido no deja al equipo sin hora.
static const char* NTP_SERVERS[3] = { "pool.ntp.org", "time.google.com", "time.cloudflare.com" };

enum { NTPS_NEVER = 0, NTPS_OK, NTPS_FAIL, NTPS_BUSY };
static volatile uint8_t  gNtpState   = NTPS_NEVER;
static volatile bool     gNtpBusy    = false;
static volatile uint32_t gNtpDoneMs  = 0;      // millis() del ultimo intento terminado
static uint32_t gNtpLastSyncUtc = 0;           // epoca UTC de la ultima sincronizacion correcta
static bool     gNtpFromNvs     = false;       // la hora actual viene de NVS, no de la red
static uint8_t  gNtpFails       = 0;           // fallos seguidos (espera progresiva)
static uint32_t gNtpNextMs      = 0;           // millis() del proximo intento permitido
static bool     gNtpUserAsked   = false;       // el intento en curso lo pidio el usuario

#define TIME_NVS_NS    "flexos_time"
#define TIME_NVS_EPOCH "epoch"
#define TIME_NVS_SYNC  "lastsync"

// ---- Persistencia de la hora --------------------------------------
// Se guarda la epoca UTC actual (no la local): la conversion a Lima se hace
// siempre al leer, asi que un cambio futuro de zona no invalida lo guardado.
static void clkSaveNvs(){
  if(!clkAnchored) return;
  uint32_t now = clkNowUtc();
  if(now < FLEXOS_CLK_MIN_EPOCH) return;          // nunca persistir una hora que ya sabemos mala
  Preferences p;
  if(!p.begin(TIME_NVS_NS, false)){ gTimeNvsOk = false; return; }
  gTimeNvsOk = true;
  p.putULong64(TIME_NVS_EPOCH, (uint64_t)now);
  p.putULong64(TIME_NVS_SYNC,  (uint64_t)gNtpLastSyncUtc);
  p.end();
}
// Arranque: recupera la ultima hora conocida. Es una hora APROXIMADA (el
// equipo ha estado apagado un rato sin contar), pero es infinitamente mejor
// que arrancar en 1970 o en la semilla de fabrica.
static void clkLoadNvs(){
  Preferences p;
  // PRIMER ARRANQUE. begin(..., true) abre en SOLO LECTURA y devuelve false si
  // el namespace todavia no existe -- que es justo lo que pasa en un equipo
  // recien flasheado. Tomar eso por "NVS averiado" pintaria un error en
  // Ajustes en cada equipo nuevo. Por eso se reintenta en lectura/escritura,
  // que ADEMAS crea el namespace: solo si eso tambien falla hay un problema
  // real de almacenamiento.
  if(!p.begin(TIME_NVS_NS, true)){
    if(!p.begin(TIME_NVS_NS, false)){
      gTimeNvsOk = false;
      Serial.println(F("[TIME] NVS no disponible -> hora de fabrica"));
      return;
    }
  }
  gTimeNvsOk = true;
  uint64_t e = p.getULong64(TIME_NVS_EPOCH, 0);
  uint64_t l = p.getULong64(TIME_NVS_SYNC,  0);
  p.end();
  if(e >= FLEXOS_CLK_MIN_EPOCH){
    clkSetEpoch((uint32_t)e);
    gNtpLastSyncUtc = (uint32_t)l;
    gNtpFromNvs = true;
    Serial.println(F("[TIME] hora recuperada de NVS (aproximada hasta sincronizar)"));
  }
}

// ---- Cliente SNTP ---------------------------------------------------
// Consulta UN servidor. Devuelve la epoca UTC o 0 si no contesta.
// El socket se abre y se CIERRA aqui dentro pase lo que pase: ni un
// descriptor queda vivo entre intentos.
static uint32_t ntpQuery(const char* host){
  WiFiUDP udp;
  if(!udp.begin(0)) return 0;                     // puerto local efimero
  uint8_t pkt[NTP_PKT_SIZE];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0xE3;      // LI = 3 (sin aviso), VN = 4, Mode = 3 (cliente)
  pkt[1] = 0;         // stratum
  pkt[2] = 6;         // polling interval
  pkt[3] = 0xEC;      // precision
  pkt[12] = 0x31; pkt[13] = 0x4E; pkt[14] = 0x31; pkt[15] = 0x34;   // reference id
  uint32_t got = 0;
  if(udp.beginPacket(host, NTP_PORT)){
    udp.write(pkt, NTP_PKT_SIZE);
    if(udp.endPacket()){
      uint32_t t0 = millis();
      while(millis() - t0 < NTP_TIMEOUT_MS){
        int n = udp.parsePacket();
        if(n >= NTP_PKT_SIZE){
          if(udp.read(pkt, NTP_PKT_SIZE) == NTP_PKT_SIZE){
            // Bytes 40..43: marca de tiempo de transmision, segundos desde 1900.
            uint32_t secs1900 = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                                ((uint32_t)pkt[42] << 8)  |  (uint32_t)pkt[43];
            if(secs1900 > NTP_EPOCH_DELTA){
              uint32_t utc = secs1900 - NTP_EPOCH_DELTA;
              if(utc >= FLEXOS_CLK_MIN_EPOCH) got = utc;   // filtro de cordura
            }
          }
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));            // cede CPU: no hay espera activa
      }
    }
  }
  udp.stop();                                     // cierre incondicional del socket
  return got;
}

static void ntpTask(void*){
  uint32_t utc = 0;
  for(int i = 0; i < 3 && utc == 0; i++){
    if(WiFi.status() != WL_CONNECTED) break;      // el Wi-Fi cayo a mitad: no insistir
    utc = ntpQuery(NTP_SERVERS[i]);
  }
  if(utc){
    clkSetEpoch(utc);
    gNtpLastSyncUtc = utc;
    gNtpFromNvs = false;
    gNtpFails   = 0;
    gNtpState   = NTPS_OK;
    clkSaveNvs();
    Serial.println(F("[NTP] hora sincronizada"));
  } else {
    if(gNtpFails < 8) gNtpFails++;
    gNtpState = NTPS_FAIL;
    Serial.println(F("[NTP] sin respuesta -> se reintentara mas tarde"));
  }
  gNtpDoneMs = millis();
  gNtpBusy   = false;
  vTaskDelete(NULL);
}

// El Wi-Fi acaba de enlazar: adelanta la proxima sincronizacion. Se llama
// desde las dos tareas de conexion (manual y automatica), que son los UNICOS
// puntos donde se confirma un enlace correcto.
static void ntpOnWifiUp(){
  gNtpFails  = 0;
  gNtpNextMs = millis() + NTP_FIRST_DELAY;
  if(!gNtpNextMs) gNtpNextMs = 1;         // 0 esta reservado para "nunca programada"
}

// Pide una sincronizacion. NO bloquea: solo lanza la tarea si procede.
// userAsked = true viene del boton "Sincronizar ahora" de Ajustes y salta la
// espera progresiva (el usuario ha pedido el intento a proposito).
static void ntpRequestSync(bool userAsked){
#if FLEXOS_ENABLE_WIFI
  if(gNtpBusy) return;
  if(gAirplane) return;
  if(!gNetOnline) return;
  if(!userAsked && gNtpNextMs && (int32_t)(millis() - gNtpNextMs) < 0) return;
  gNtpBusy = true; gNtpState = NTPS_BUSY; gNtpUserAsked = userAsked;
  if(xTaskCreatePinnedToCore(ntpTask, "ntp", 4096, NULL, 1, NULL, 1) != pdPASS){
    gNtpBusy = false; gNtpState = NTPS_FAIL;
  }
#else
  (void)userAsked;
#endif
}

// Ritmo del reloj: se llama desde loop(), cuesta unas comparaciones.
// Aqui NO hay red: solo decide CUANDO hay que pedirla.
static void ntpTick(){
#if FLEXOS_ENABLE_WIFI
  uint32_t now = millis();
  if(gNtpBusy) return;
  // Un intento acaba de terminar: programa el siguiente. Correcto -> 6 h.
  // Fallido -> espera progresiva 30 s, 1 min, 2 min... con techo de 30 min.
  if(gNtpDoneMs){
    uint32_t done = gNtpDoneMs; gNtpDoneMs = 0;
    if(gNtpState == NTPS_OK) gNtpNextMs = done + NTP_PERIOD_MS;
    else {
      uint32_t wait = NTP_BACKOFF_MIN << (gNtpFails > 0 ? (gNtpFails - 1) : 0);
      if(wait > NTP_BACKOFF_MAX || wait < NTP_BACKOFF_MIN) wait = NTP_BACKOFF_MAX;
      gNtpNextMs = done + wait;
    }
    return;
  }
  if(gAirplane || !gNetOnline) return;
  // Primera sincronizacion de la sesion: en cuanto haya red y el sistema
  // este operativo (no a mitad del arranque).
  if(gNtpNextMs == 0){
    if(gState != ST_HOME && gState != ST_LOCK) return;
    if(now < NTP_FIRST_DELAY) return;
    ntpRequestSync(false);
    if(gNtpBusy) gNtpNextMs = now + NTP_PERIOD_MS;   // se reajusta al terminar
    return;
  }
  if((int32_t)(now - gNtpNextMs) >= 0) ntpRequestSync(false);
#endif
}

// Guardado periodico de la hora (una vez por hora). Sin esto, un corte de
// corriente perderia hasta 6 h de reloj; con esto, como mucho una.
static void clkPersistTick(){
  static uint32_t lastSave = 0;
  if(!clkAnchored) return;
  if(lastSave && millis() - lastSave < 3600000UL) return;
  lastSave = millis(); if(!lastSave) lastSave = 1;
  clkSaveNvs();
}

// ---- Textos para Ajustes -------------------------------------------
static bool ntpIsBusy(){ return gNtpBusy; }
static void ntpStateText(char* out, size_t n){
#if !FLEXOS_ENABLE_WIFI
  #if FLEXOS_WIFI_CORE_UNSAFE
    snprintf(out, n, "Requiere core ESP32 3.2.1");
  #else
    snprintf(out, n, "Wi-Fi desactivado en este build");
  #endif
#else
  if(!gTimeNvsOk){ snprintf(out, n, "Error: almacenamiento NVS no disponible"); return; }
  if(gNtpBusy){ snprintf(out, n, "Sincronizando..."); return; }
  switch(gNtpState){
    case NTPS_OK:   snprintf(out, n, "Sincronizado - UTC-5 (Lima)"); break;
    case NTPS_FAIL: snprintf(out, n, gAirplane ? "Modo avi\xC3\xB3n activo"
                                    : (gNetOnline ? "Sin respuesta del servidor" : "Sin conexi\xC3\xB3n Wi-Fi")); break;
    default:
      if(gNtpFromNvs)                     snprintf(out, n, "Hora aproximada (sin sincronizar)");
      else if(gAirplane)                  snprintf(out, n, "Modo avi\xC3\xB3n activo");
      else if(!gNetOnline)                   snprintf(out, n, "Esperando Wi-Fi");
      else                                snprintf(out, n, "Pendiente");
      break;
  }
#endif
}
static void ntpLastSyncText(char* out, size_t n){
  if(gNtpLastSyncUtc < FLEXOS_CLK_MIN_EPOCH){ snprintf(out, n, "Nunca"); return; }
  long local = (long)gNtpLastSyncUtc + FLEXOS_TZ_OFFSET_SEC;
  long days  = local / 86400L, rem = local % 86400L;
  if(rem < 0){ rem += 86400L; days--; }
  int y, mo, d; clkCivilFromDays(days, y, mo, d);
  long nowLocal = (long)clkNowUtc() + FLEXOS_TZ_OFFSET_SEC;
  long nowDays  = nowLocal / 86400L; if(nowLocal % 86400L < 0) nowDays--;
  int hh = (int)(rem / 3600L), mm = (int)((rem % 3600L) / 60L);
  if(days == nowDays)          snprintf(out, n, "Hoy %02d:%02d", hh, mm);
  else if(days == nowDays - 1) snprintf(out, n, "Ayer %02d:%02d", hh, mm);
  else                         snprintf(out, n, "%02d/%02d/%04d %02d:%02d", d, mo, y, hh, mm);
}
