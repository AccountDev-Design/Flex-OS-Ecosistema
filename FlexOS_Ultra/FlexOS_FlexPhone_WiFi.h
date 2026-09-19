// #############################################################
// ##  FLEX PHONE · TRANSPORTE Wi-Fi  (ESP32-P4)
// ##  ---------------------------------------------------------
// ##  Implementa FlexPhoneTransport sobre un socket TCP de la red
// ##  local. Es el camino que Flex Phone usa HOY: el ESP32-P4 no
// ##  tiene radio Bluetooth, y el Wi-Fi que ya da el C6 para todo
// ##  el sistema sirve igual para esto.
// ##
// ##  QUIEN ES SERVIDOR Y QUIEN CLIENTE
// ##  ------------------------------------------------------------
// ##  El TELEFONO escucha y Flex OS se conecta. No es arbitrario:
// ##  el telefono esta encendido siempre y tiene un servicio en
// ##  primer plano que lo mantiene vivo; el reloj se suspende. Al
// ##  reves, cada suspension del P4 cortaria el enlace.
// ##
// ##  TODO LO DE RED VIVE EN SU PROPIA TAREA
// ##  ------------------------------------------------------------
// ##  Es la misma regla que ya cumple el navegador: el hilo grafico
// ##  NUNCA llama a connect(), read() ni write(). Entre la tarea y
// ##  el bucle principal solo pasan tramas completas por dos colas
// ##  de tamano fijo, con un mutex corto. Asi un telefono lento no
// ##  puede bajar los FPS del sistema.
// ##
// ##  DESCUBRIMIENTO
// ##  ------------------------------------------------------------
// ##  Una sonda UDP a la difusion de la red; el telefono contesta
// ##  con su puerto y su nombre. Si la red bloquea la difusion --
// ##  pasa en bastantes routers con aislamiento de clientes -- se
// ##  puede fijar la IP a mano, y la interfaz lo ofrece en vez de
// ##  quedarse buscando para siempre.
// ##
// ##  ESTE FICHERO ES UN MODULO DEL SKETCH, no una unidad de
// ##  traduccion aparte: necesita WiFi, FreeRTOS y el estado del
// ##  sistema. La LOGICA no esta aqui -- esta en
// ##  FlexOS_FlexPhone_Link.cpp, que se prueba en el PC.
// #############################################################
#pragma once
#include "FlexOS_FlexPhone_Transport.h"
// Los bytes del descubrimiento viven aparte, sin Arduino, para poder
// compararlos con los de Android en una bateria de PC. Ver
// tests/host/test_flexphone_discovery.cpp.
#include "FlexOS_FlexPhone_Discovery.h"
#include <WiFi.h>
#include <WiFiUdp.h>

// Cada cuanto se vuelve a preguntar mientras no hay telefono. No es
// sondeo agresivo: son unas decenas de bytes cada dos segundos, y
// solo mientras el usuario tiene el enlace encendido y todavia no hay
// nadie al otro lado.
#define FLPW_PROBE_EVERY_MS  2000
#define FLPW_CONNECT_MS      4000    // tope para abrir el socket
#define FLPW_TASK_STACK      4096
#define FLPW_RXBUF           (FLNK_MAX_FRAME * 3)

// #############################################################
// ##  DIAGNOSTICO DEL DESCUBRIMIENTO
// ##  ------------------------------------------------------
// ##  Mismo patron -- y mismo motivo -- que FLEXOS_DIAG_WIFI: en
// ##  funcionamiento normal el puerto serie tiene que estar LIMPIO,
// ##  asi que esto se compila a nada salvo que se encienda a mano.
// ##
// ##  Cuando el emparejamiento no encuentra nada, el problema esta
// ##  en uno de estos cuatro pasos, y aqui se ven los dos que le
// ##  tocan al reloj:
// ##
// ##      (a) el ESP32 manda la sonda        <- se ve aqui
// ##      (b) el telefono la recibe          <- logcat de Android
// ##      (c) el telefono contesta           <- logcat de Android
// ##      (d) el ESP32 recibe la respuesta   <- se ve aqui
// ##
// ##  Si se ve (a) y no (d), o el telefono no esta escuchando o la
// ##  red no deja pasar la difusion (aislamiento de clientes). El
// ##  logcat del telefono distingue las dos cosas.
// #############################################################
// #############################################################
// ##  A 1 MIENTRAS SE PERSIGUE EL PROBLEMA DE ESTABILIDAD
// ##  ------------------------------------------------------
// ##  Con esto encendido, el puerto serie recibe la secuencia
// ##  entera del enlace: cada conexion con su numero, cada cierre
// ##  con su MOTIVO y cuanto duro. Es lo que hace falta para
// ##  saber quien cuelga primero.
// ##
// ##  VUELVE A 0 antes de publicar: en produccion no se dejan
// ##  lineas de enlace en el registro.
// #############################################################
#define FLEXOS_DIAG_FLEXPHONE 1

#if FLEXOS_DIAG_FLEXPHONE
  #define FPW_DIAG(...) do { Serial.printf("[FLEXPHONE %8lu] ", (unsigned long)millis()); \
                             Serial.printf(__VA_ARGS__); Serial.println(); } while(0)
#else
  #define FPW_DIAG(...) ((void)0)
#endif

// =============================================================
//  Estado del transporte
// =============================================================
typedef struct {
  // -- compartido entre la tarea de red y el bucle principal --
  FlexFrameRing     rx;          // del telefono hacia Flex OS
  FlexFrameRing     tx;          // de Flex OS hacia el telefono
  SemaphoreHandle_t mux;
  volatile uint8_t  state;       // FLP_TC_*
  volatile bool     stop;
  TaskHandle_t      task;
  char              status[72];  // motivo legible; lo escribe la tarea
  char              peer[FLP_LINK_PEER_MAX];
  // -- destino --
  uint8_t           ip[4];
  uint16_t          port;
  bool              fixed;       // el usuario fijo la IP a mano
  char              foundName[FLP_DEVNAME_MAX];
  // Lo que este reloj contesta cuando el telefono pregunta. Lo pone
  // el puente (que es quien conoce el enlace); aqui solo se copia.
  char              selfId[FLXA_ID_MAX];
  char              selfName[FLP_DEVNAME_MAX];
  volatile bool     pairing;     // ¿hay un emparejamiento en curso?
  // -- diagnostico del descubrimiento --
  uint32_t          nAsked;      // sondas del telefono contestadas
  // -- diagnostico (nunca contenido) --
  uint32_t          nConnects, nDrops, nDesync;
} FlexPhoneWifiCtx;

// #############################################################
// ##  DIAGNOSTICO: NO RECONECTAR
// ##  ------------------------------------------------------
// ##  Con esto a 1, el reloj NO vuelve a conectar despues de la
// ##  primera caida. Sirve para aislar el PRIMER cierre: mientras
// ##  se reconecta sin parar, cada ciclo tapa al anterior y es
// ##  imposible saber cual fue el de verdad.
// ##
// ##  Se enciende A MANO, para una prueba, y se vuelve a 0. En
// ##  produccion va a 0 y el enlace se recupera solo.
// #############################################################
#ifndef FLEXOS_FLEXPHONE_NO_RECONNECT
  #define FLEXOS_FLEXPHONE_NO_RECONNECT 0
#endif

// Espera maxima entre reintentos de conexion, y cuanto tiene que
// aguantar un socket abierto para que esa espera vuelva a cero.
#define FLPW_RECONNECT_MAX_MS  15000
#define FLPW_RECONNECT_OK_MS   10000
// Cuanto se insiste con una trama que no avanza ni un byte antes de
// dar el socket por caido. Es un plazo PROPIO y explicito: no se
// hereda del que traiga por dentro la pila de red.
#define FLPW_WRITE_DEADLINE_MS 8000

static FlexPhoneWifiCtx fpwCtx;
// #############################################################
// ##  DIAGNOSTICO DE CIERRES
// ##  ------------------------------------------------------
// ##  Cada socket lleva un numero, y CADA cierre dice por que.
// ##  Sin esto, "se desconecta" es todo lo que se sabe; con
// ##  esto se puede reconstruir la secuencia entera y ver quien
// ##  cuelga primero.
// ##
// ##  Con FLEXOS_DIAG_FLEXPHONE a 0 (produccion) no se imprime
// ##  nada: son dos variables y una comparacion.
// #############################################################
static uint32_t    fpwConnId     = 0;      // numero del socket actual
static const char* fpwCloseReason = "";    // por que se cerro el ultimo
// Cuantas veces seguidas fallo la conexion. Solo lo toca la tarea de
// red, asi que no necesita el mutex.
static uint8_t fpwBackoff = 0;

static inline void fpwLock(){ if(fpwCtx.mux) xSemaphoreTake(fpwCtx.mux, portMAX_DELAY); }
static inline void fpwUnlock(){ if(fpwCtx.mux) xSemaphoreGive(fpwCtx.mux); }

// El texto de estado lo lee el bucle principal mientras la tarea
// puede estar escribiendolo. Se copia bajo el mismo mutex que las
// colas: son dos lineas, y una cadena a medias en pantalla es un
// fallo que solo aparece de vez en cuando y nadie sabe reproducir.
static void fpwSetStatus(const char* s){
  fpwLock();
  flexLinkUtf8Copy(fpwCtx.status, sizeof(fpwCtx.status), s ? s : "");
  fpwUnlock();
}

// -------------------------------------------------------------
//  Lo que este reloj contesta cuando el telefono pregunta
// -------------------------------------------------------------
// Lo fija el puente, que es quien conoce el enlace. Se copia bajo el
// mutex porque lo lee la tarea de red.
static void flexPhoneWifiSetIdentity(const char* id, const char* name, bool pairing){
  fpwLock();
  if(id)   flexLinkUtf8Copy(fpwCtx.selfId,   sizeof(fpwCtx.selfId),   id);
  if(name) flexLinkUtf8Copy(fpwCtx.selfName, sizeof(fpwCtx.selfName), name);
  fpwCtx.pairing = pairing;
  fpwUnlock();
}

// -------------------------------------------------------------
//  Una sonda del TELEFONO: se contesta y se aprende su direccion
// -------------------------------------------------------------
// Esta es la direccion FIABLE del descubrimiento. El telefono emite
// (emitir nunca se filtra) y aqui se aprende todo lo necesario: la IP
// del origen del paquete y el puerto de la carga. La respuesta vuelve
// en UNIDIFUSION, que Android tampoco filtra.
static void fpwHandleAsk(WiFiUDP& udp, const uint8_t* buf, int got, const IPAddress& from){
  uint8_t  ver  = 0;
  uint16_t port = 0;
  char     name[FLP_DEVNAME_MAX];
  if(!fpdParseAsk(buf, got, &ver, &port, name, sizeof(name))) return;
  if(ver < FLNK_VERSION_MIN){
    FPW_DIAG("(b) sonda del telefono con protocolo v%u; se necesita v%u",
             ver, (unsigned)FLNK_VERSION_MIN);
    return;
  }

  // ---- 1) Contestar SIEMPRE ----
  // Aunque ya tengamos telefono: el usuario puede estar mirando la
  // lista de relojes en otro movil, y no aparecer ahi es justo el
  // fallo que hace imposible emparejar.
  fpwLock();
  const bool pairing = fpwCtx.pairing;
  char sid[FLXA_ID_MAX], sname[FLP_DEVNAME_MAX];
  memcpy(sid,   fpwCtx.selfId,   sizeof(sid));
  memcpy(sname, fpwCtx.selfName, sizeof(sname));
  fpwUnlock();
  sid[sizeof(sid) - 1] = 0;
  sname[sizeof(sname) - 1] = 0;

  uint8_t ans[FLPW_ANS_MAX];
  const int at = fpdBuildAnswer(ans, sizeof(ans), FLNK_VERSION,
                                pairing ? FLPW_FLAG_PAIRING : 0, sid, sname);
  if(at > 0 && udp.beginPacket(from, FLPW_UDP_PORT)){
    udp.write(ans, (size_t)at);
    udp.endPacket();
  }
  fpwCtx.nAsked++;
  FPW_DIAG("(b) sonda de %u.%u.%u.%u:%u (\"%s\") -> contestado%s",
           from[0], from[1], from[2], from[3], (unsigned)port, name,
           pairing ? " [emparejando]" : "");

  // ---- 2) Aprender su direccion, si no teniamos ----
  // No se pisa un destino que el usuario haya fijado a mano, ni uno
  // que ya este en uso: el telefono con el que se habla lo decide el
  // vinculo, no quien grite mas alto en la red.
  if(!port) return;
  fpwLock();
  const bool takeIt = (fpwCtx.port == 0) && !fpwCtx.fixed;
  if(takeIt){
    fpwCtx.ip[0] = from[0]; fpwCtx.ip[1] = from[1];
    fpwCtx.ip[2] = from[2]; fpwCtx.ip[3] = from[3];
    fpwCtx.port  = port;
    flexLinkUtf8Copy(fpwCtx.foundName, sizeof(fpwCtx.foundName), name);
    snprintf(fpwCtx.peer, sizeof(fpwCtx.peer), "%u.%u.%u.%u:%u",
             from[0], from[1], from[2], from[3], (unsigned)port);
  }
  fpwUnlock();
  if(takeIt)
    FPW_DIAG("(b) telefono aprendido de su propia sonda: %u.%u.%u.%u:%u",
             from[0], from[1], from[2], from[3], (unsigned)port);
}

// -------------------------------------------------------------
//  Vaciar el socket UDP
// -------------------------------------------------------------
// #############################################################
// ##  SE LLAMA EN CADA VUELTA, TENGAMOS TELEFONO O NO
// ##  ------------------------------------------------------
// ##  Antes el socket solo se leia dentro del descubrimiento, o sea
// ##  400 ms de cada 2 s y SOLO mientras no habia telefono. El
// ##  resto del tiempo lo que llegaba se quedaba en el buffer de
// ##  lwIP hasta desbordarlo.
// ##
// ##  Con el descubrimiento en los dos sentidos eso ya no vale: la
// ##  sonda del telefono puede llegar en cualquier momento, y si no
// ##  se contesta, el reloj no aparece en su lista.
// #############################################################
static void fpwPumpUdp(WiFiUDP& udp){
  for(int guard = 0; guard < 8; guard++){       // tope por vuelta
    const int n = udp.parsePacket();
    if(n <= 0) return;
    uint8_t buf[96];
    const int got = udp.read(buf, sizeof(buf));
    if(got <= 0) return;
    const IPAddress from = udp.remoteIP();

    // ¿Una sonda del telefono? Se contesta.
    if(fpdIsAsk(buf, got)){
      fpwHandleAsk(udp, buf, got, from);
      continue;
    }
    // ¿La respuesta a NUESTRA sonda?
    uint8_t  ver  = 0;
    uint16_t port = 0;
    char     name[FLP_DEVNAME_MAX];
    if(fpdParseReply(buf, got, &ver, &port, name, sizeof(name))){
      if(ver < FLNK_VERSION_MIN){
        fpwSetStatus("la app del telefono es de una version anterior");
        continue;
      }
      if(!port) continue;
      // Solo se aprende un destino cuando NO hay ninguno. Si ya hay
      // sesion -- o el usuario fijo la IP a mano --, la respuesta de
      // otro telefono de la red no puede robar el enlace a media
      // conversacion: eso seria exactamente la sesion duplicada que
      // hay que evitar.
      fpwLock();
      const bool takeIt = (fpwCtx.port == 0) && !fpwCtx.fixed;
      if(takeIt){
        fpwCtx.ip[0] = from[0]; fpwCtx.ip[1] = from[1];
        fpwCtx.ip[2] = from[2]; fpwCtx.ip[3] = from[3];
        fpwCtx.port  = port;
        flexLinkUtf8Copy(fpwCtx.foundName, sizeof(fpwCtx.foundName), name);
        snprintf(fpwCtx.peer, sizeof(fpwCtx.peer), "%u.%u.%u.%u:%u",
                 from[0], from[1], from[2], from[3], (unsigned)port);
      }
      fpwUnlock();
      if(!takeIt) continue;
      FPW_DIAG("(d) telefono encontrado en %u.%u.%u.%u:%u (protocolo v%u)",
               from[0], from[1], from[2], from[3], (unsigned)port, ver);
      continue;
    }
    // Lo normal aqui es oir la PROPIA sonda de vuelta: la difusion
    // limitada se entrega tambien al que la emitio. No es un fallo.
  }
}

// -------------------------------------------------------------
//  Emitir la sonda del reloj
// -------------------------------------------------------------
// Solo EMITE. La respuesta la recoge fpwPumpUdp, que se llama en
// cada vuelta de la tarea: antes la recepcion vivia aqui dentro y
// por eso el socket solo se leia 400 ms de cada 2 s.
//
// Esta es la direccion FRAGIL del descubrimiento -- el telefono
// tiene que RECIBIR una difusion --, y se mantiene porque cuando
// funciona ahorra tener la app en primer plano. La fiable es la
// contraria, la que atiende fpwHandleAsk.
static bool fpwSendProbe(WiFiUDP& udp){
  if(WiFi.status() != WL_CONNECTED) return false;
  const IPAddress ip   = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  // Difusion de ESTA subred, no 255.255.255.255: la dirigida llega a
  // donde tiene que llegar y no molesta a redes vecinas.
  IPAddress bcast(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1],
                  ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);

  uint8_t probe[FLPW_PROBE_LEN + 1];
  const int probeN = fpdBuildProbe(probe, sizeof(probe), FLNK_VERSION);
  if(probeN <= 0) return false;

  FPW_DIAG("(a) sonda: yo %u.%u.%u.%u  mascara %u.%u.%u.%u  difusion %u.%u.%u.%u:%u",
           ip[0], ip[1], ip[2], ip[3], mask[0], mask[1], mask[2], mask[3],
           bcast[0], bcast[1], bcast[2], bcast[3], (unsigned)FLPW_UDP_PORT);

  // DOS destinos a proposito. La difusion DIRIGIDA de la subred es la
  // correcta y la que menos molesta, pero hay pilas y puntos de acceso
  // que la filtran y en cambio dejan pasar la limitada. Son 34 bytes
  // cada dos segundos y solo mientras no hay telefono; el coste es
  // irrelevante al lado de quedarse sin encontrarlo nunca.
  bool sent = false;
  if(udp.beginPacket(bcast, FLPW_UDP_PORT)){
    udp.write(probe, (size_t)probeN);
    sent = udp.endPacket() != 0;
  }
  const IPAddress limited(255, 255, 255, 255);
  if(udp.beginPacket(limited, FLPW_UDP_PORT)){
    udp.write(probe, (size_t)probeN);
    if(udp.endPacket() != 0) sent = true;
  }
  if(!sent) FPW_DIAG("(a) FALLO: no se pudo emitir la sonda");
  return sent;
}

// #############################################################
// ##  ESCRIBIR UNA TRAMA ENTERA
// ##  ------------------------------------------------------
// ##  AQUI ESTABA EL CICLO DE 10-13 SEGUNDOS.
// ##
// ##  Antes esto era una linea:
// ##
// ##      if(cli.write(frame, n) != n) goto closed;
// ##
// ##  y esa comparacion da por MUERTO un socket que solo estaba
// ##  ocupado. NetworkClient::write() del core 3.x no promete
// ##  escribirlo todo: su bucle interno hace select() con un
// ##  plazo de 1 s y se rinde tras 10 intentos
// ##  (WIFI_CLIENT_MAX_WRITE_RETRY x WIFI_CLIENT_SELECT_TIMEOUT_US),
// ##  o sea que puede tenerse la tarea de red DIEZ SEGUNDOS
// ##  dentro y devolver despues una escritura corta -- con la
// ##  conexion perfectamente viva.
// ##
// ##  Diez segundos dentro de write(), mas el apreton de manos y
// ##  el grano del tick, es justo la ventana de 10-13 s que se
// ##  observaba: el enlace se veia CONECTADO todo ese rato porque
// ##  el estado del transporte seguia en OPEN, y al volver de
// ##  write() se cerraba y se reconectaba un segundo despues.
// ##
// ##  Dos cosas, y las dos importan:
// ##
// ##    · una escritura corta se TERMINA, no se abandona. Dejar
// ##      media trama en el flujo descoloca al otro extremo, que
// ##      es un fallo peor y mas dificil de leer;
// ##    · solo se da por muerto el socket cuando de verdad lo
// ##      esta (connected() == false) o cuando se agota un plazo
// ##      PROPIO y explicito.
// #############################################################
static bool fpwWriteAll(WiFiClient& cli, const uint8_t* p, size_t n){
  size_t at = 0;
  const uint32_t t0 = millis();
  while(at < n){
    if(!cli.connected()) return false;          // muerto de verdad
    const size_t w = cli.write(p + at, n - at);
    if(w > 0){ at += w; continue; }
    // Ni un byte: el socket esta ocupado, no muerto. Se cede la CPU y
    // se reintenta hasta el plazo propio.
    if((uint32_t)(millis() - t0) > FLPW_WRITE_DEADLINE_MS){
      FPW_DIAG("(w) escritura sin avanzar en %u ms: se da por caida",
               (unsigned)FLPW_WRITE_DEADLINE_MS);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return true;
}

// -------------------------------------------------------------
//  Lectura de tramas del flujo TCP
// -------------------------------------------------------------
// TCP es un FLUJO: no respeta los limites de las tramas. Aqui se
// reconstruyen usando la propia cabecera de Flex Link, que ya dice
// su longitud. No hace falta inventar un segundo enmarcado.
//
// Si la marca no cuadra, el flujo esta descolocado y NO se intenta
// adivinar: se corta y se reconecta. Buscar la marca a ciegas en un
// flujo corrupto es como se acaba interpretando basura como si
// fueran mensajes.
static bool fpwPumpRead(WiFiClient& cli, uint8_t* acc, size_t& accN){
  while(cli.available() > 0 && accN < FLPW_RXBUF){
    const size_t room = FLPW_RXBUF - accN;
    const int got = cli.read(acc + accN, room);
    if(got <= 0) break;
    accN += (size_t)got;
  }
  // Extraer todas las tramas completas que haya.
  size_t at = 0;
  while(accN - at >= FLNK_HDR_SIZE){
    const uint8_t* h = acc + at;
    if(h[0] != 0xF1 || h[1] != 0x58){
      fpwCtx.nDesync++;
      fpwSetStatus("el flujo del telefono llego descolocado");
      return false;
    }
    const uint16_t len = (uint16_t)(h[10] | ((uint16_t)h[11] << 8));
    if(len > FLNK_MAX_PAYLOAD){
      fpwCtx.nDesync++;
      fpwSetStatus("el telefono anuncio una trama imposible");
      return false;
    }
    const size_t total = (size_t)FLNK_HDR_SIZE + len;
    if(accN - at < total) break;                 // falta cola: se espera
    fpwLock();
    flexRingPush(&fpwCtx.rx, acc + at, total);
    fpwUnlock();
    at += total;
  }
  if(at){
    accN -= at;
    if(accN) memmove(acc, acc + at, accN);
  }
  // El acumulador lleno sin una trama completa significa que el otro
  // extremo no habla este protocolo.
  if(accN >= FLPW_RXBUF){
    fpwCtx.nDesync++;
    fpwSetStatus("el otro extremo no habla Flex Link");
    return false;
  }
  return true;
}

// -------------------------------------------------------------
//  La tarea de red
// -------------------------------------------------------------
static void fpwTask(void*){
  WiFiUDP udp;
  bool udpUp = false;
  uint8_t acc[FLPW_RXBUF];
  size_t  accN = 0;
  uint32_t lastProbe = 0;

  while(!fpwCtx.stop){
    // ---- 1) Sin Wi-Fi no hay nada que hacer ----
    if(WiFi.status() != WL_CONNECTED){
      fpwCtx.state = FLP_TC_SEARCHING;
      fpwSetStatus("esperando a que haya Wi-Fi");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if(!udpUp){ udpUp = udp.begin(FLPW_UDP_PORT) != 0; }

    // ---- 2) Atender el socket UDP, haya telefono o no ----
    // Contestar la sonda del telefono es lo que hace que este reloj
    // APAREZCA en su lista. Si solo se atendiera mientras buscamos,
    // un reloj ya emparejado seria invisible para el movil que
    // intenta encontrarlo, que es justo el caso que fallaba.
    if(udpUp) fpwPumpUdp(udp);

    // ---- 3) Buscar el telefono ----
    bool have;
    fpwLock(); have = fpwCtx.port != 0; fpwUnlock();
    if(!have){
      fpwCtx.state = FLP_TC_SEARCHING;
      fpwSetStatus("buscando Flex Phone en la red");
      if(udpUp && millis() - lastProbe >= FLPW_PROBE_EVERY_MS){
        lastProbe = millis();
        fpwSendProbe(udp);
      }
      // 50 ms: la sonda del telefono se contesta casi al instante sin
      // que la tarea gire en vacio.
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // ---- 4) Abrir el socket ----
    uint8_t ip[4]; uint16_t port;
    fpwLock();
    memcpy(ip, fpwCtx.ip, 4); port = fpwCtx.port;
    fpwUnlock();

    WiFiClient cli;
    // NO se usa setTimeout(): en arduino-esp32 ese es el plazo de
    // Stream (para readBytes y compania) y NO toca el del socket, asi
    // que el plazo de conexion que se creia puesto aqui no se aplicaba
    // nunca. El que si vale es el tercer argumento de connect(), que
    // la API declara en MILISEGUNDOS.
    fpwCtx.state = FLP_TC_OPENING;
    fpwSetStatus("abriendo el enlace con el telefono");
    char host[16];
    snprintf(host, sizeof(host), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    if(!cli.connect(host, port, FLPW_CONNECT_MS)){
      fpwCtx.nDrops++;
      fpwCtx.state = FLP_TC_FAILED;
      fpwSetStatus("el telefono no acepto la conexion");
      // La direccion descubierta puede haber caducado (el router dio
      // otra IP). Se olvida para volver a buscar, salvo que la haya
      // fijado el usuario: ahi lo util es decir que esa no responde.
      fpwLock();
      if(!fpwCtx.fixed){ fpwCtx.port = 0; fpwCtx.peer[0] = 0; }
      fpwUnlock();
      vTaskDelay(pdMS_TO_TICKS(1500));
      continue;
    }

    fpwCtx.nConnects++;
    // Conexion abierta: la espera progresiva se reinicia solo cuando
    // el enlace llegue a AGUANTAR (ver mas abajo), no por el hecho de
    // abrir el socket -- abrir y que te lo cierren en el acto es
    // justo el caso que no puede reiniciar nada.
    const uint32_t openedAtMs = millis();
    accN = 0;
    fpwLock();
    flexRingInit(&fpwCtx.rx);
    flexRingInit(&fpwCtx.tx);
    snprintf(fpwCtx.peer, sizeof(fpwCtx.peer), "%s:%u", host, (unsigned)port);
    fpwUnlock();
    fpwCtx.state = FLP_TC_OPEN;
    fpwConnId++;
    fpwCloseReason = "";      // lo rellena quien cierre; ver mas abajo
    fpwSetStatus("enlace abierto");
    FPW_DIAG("(s) CONEXION #%u ABIERTA con %s:%u",
             (unsigned)fpwConnId, host, (unsigned)port);

    // ---- 5) Bombeo mientras el socket viva ----
    while(!fpwCtx.stop && cli.connected() && WiFi.status() == WL_CONNECTED){
      bool worked = false;

      // Tambien aqui: estando conectado se sigue contestando a quien
      // pregunte. Es lo que permite que el telefono vuelva a verlo
      // tras reinstalar la app o cambiar de movil.
      if(udpUp) fpwPumpUdp(udp);

      // Salida: lo que el enlace haya dejado en la cola.
      uint8_t frame[FLNK_MAX_FRAME];
      for(int i = 0; i < 8; i++){
        fpwLock();
        const size_t n = flexRingPop(&fpwCtx.tx, frame, sizeof(frame));
        fpwUnlock();
        if(!n) break;
        // Se escribe la trama ENTERA. Una escritura corta es el socket
        // ocupado, no el enlace muerto: ver fpwWriteAll.
        if(!fpwWriteAll(cli, frame, n)){
          fpwSetStatus("se corto al enviar");
          fpwCloseReason = "escritura fallida";
          goto closed;
        }
        worked = true;
      }

      // Entrada.
      if(cli.available() > 0){
        if(!fpwPumpRead(cli, acc, accN)){
          fpwCloseReason = "flujo ilegible";
          goto closed;
        }
        worked = true;
      }

      // Sin trabajo, se cede la CPU. Sin esta espera la tarea giraria
      // en vacio y le quitaria tiempo al hilo grafico.
      if(!worked) vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Si el socket AGUANTO un rato, el camino es bueno: la proxima
    // caida vuelve a reintentar deprisa.
    if(millis() - openedAtMs >= FLPW_RECONNECT_OK_MS) fpwBackoff = 0;

closed:
    // Por que salio del bucle, de verdad.
    //
    // SOLO si nadie lo dijo ya: un `goto` desde dentro del bucle trae
    // el motivo PRECISO (una escritura que no avanza, un flujo
    // ilegible), y rellenarlo aqui encima lo borraria -- tras una
    // escritura fallida el socket suele estar ya cerrado, asi que se
    // leeria "el telefono cerro" cuando no fue eso.
    if(!fpwCloseReason[0]){
      if(!cli.connected())                   fpwCloseReason = "el telefono cerro el socket";
      else if(WiFi.status() != WL_CONNECTED) fpwCloseReason = "se perdio el Wi-Fi";
      else if(fpwCtx.stop)                   fpwCloseReason = "enlace apagado";
      else                                   fpwCloseReason = "el bucle termino sin motivo";
    }
    FPW_DIAG("(s) CONEXION #%u CERRADA tras %u ms  motivo=%s",
             (unsigned)fpwConnId, (unsigned)(millis() - openedAtMs), fpwCloseReason);
    cli.stop();
    fpwCtx.nDrops++;
    if(!fpwCtx.stop){
      fpwCtx.state = FLP_TC_FAILED;
      if(WiFi.status() != WL_CONNECTED) fpwSetStatus("se perdio el Wi-Fi");
      else if(fpwCtx.status[0] == 0)    fpwSetStatus("el telefono cerro el enlace");
      // Al reconectar se vuelve a descubrir: el telefono puede haber
      // cambiado de IP mientras tanto.
      fpwLock();
      if(!fpwCtx.fixed){ fpwCtx.port = 0; fpwCtx.peer[0] = 0; }
      fpwUnlock();
      // #########################################################
      // ##  ESPERA PROGRESIVA, NO UN SEGUNDO FIJO
      // ##  --------------------------------------------------
      // ##  Esto NO es lo que arregla el ciclo de conecta/
      // ##  desconecta -- eso se cura en el telefono, dejando de
      // ##  guardar el hueco de la sesion para un socket muerto.
      // ##  Es lo que evita que, cuando de verdad no se puede
      // ##  conectar, el reloj martillee el puerto una vez por
      // ##  segundo para siempre: gasta bateria y radio, y llena
      // ##  el telefono de conexiones que solo sirven para
      // ##  cerrarse.
      // ##
      // ##  Una conexion que AGUANTA reinicia la cuenta, asi que
      // ##  un corte suelto se sigue recuperando en un segundo.
      // #########################################################
#if FLEXOS_FLEXPHONE_NO_RECONNECT
      // Prueba de diagnostico: se para aqui, con el motivo ya impreso.
      // La tarea SIGUE VIVA (solo duerme): matarla dejaria el estado
      // del transporte en DOWN y se perderia lo que se queria mirar.
      FPW_DIAG("(s) NO_RECONNECT activo: el enlace se queda parado");
      fpwSetStatus("diagnostico: sin reconexion automatica");
      fpwCtx.state = FLP_TC_FAILED;
      while(!fpwCtx.stop) vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
#endif
      uint32_t wait = flexLinkRetryDelayMs(fpwBackoff);
      if(wait == 0) wait = FLPW_RECONNECT_MAX_MS;   // ya se agoto: se reintenta despacio
      if(wait > FLPW_RECONNECT_MAX_MS) wait = FLPW_RECONNECT_MAX_MS;
      if(fpwBackoff < 255) fpwBackoff++;
      vTaskDelay(pdMS_TO_TICKS(wait));
    }
  }

  if(udpUp) udp.stop();
  fpwCtx.state = FLP_TC_DOWN;
  fpwCtx.task = NULL;
  vTaskDelete(NULL);
}

// =============================================================
//  La interfaz FlexPhoneTransport
// =============================================================
static bool fpwStart(FlexPhoneTransport* t){
  (void)t;
  if(fpwCtx.task) return true;
  if(!FLEXOS_ENABLE_WIFI || gAirplane){
    fpwSetStatus(gAirplane ? "el modo avion esta activado"
                           : "el Wi-Fi esta desactivado en este sistema");
    fpwCtx.state = FLP_TC_FAILED;
    return false;
  }
  if(!fpwCtx.mux) fpwCtx.mux = xSemaphoreCreateMutex();
  if(!fpwCtx.mux){
    fpwSetStatus("sin memoria para arrancar el enlace");
    fpwCtx.state = FLP_TC_FAILED;
    return false;
  }
  flexRingInit(&fpwCtx.rx);
  flexRingInit(&fpwCtx.tx);
  fpwCtx.stop  = false;
  fpwCtx.state = FLP_TC_SEARCHING;
  fpwSetStatus("buscando Flex Phone en la red");
  // Nucleo 1, prioridad 1: las mismas que el transporte del
  // navegador. El hilo grafico vive en el 0 y no comparte tiempo con
  // esto.
  if(xTaskCreatePinnedToCore(fpwTask, "flexphWifi", FLPW_TASK_STACK,
                             NULL, 1, &fpwCtx.task, 1) != pdPASS){
    fpwCtx.task = NULL;
    fpwSetStatus("no se pudo crear la tarea de red");
    fpwCtx.state = FLP_TC_FAILED;
    return false;
  }
  return true;
}

static void fpwStop(FlexPhoneTransport* t){
  (void)t;
  if(!fpwCtx.task){ fpwCtx.state = FLP_TC_DOWN; return; }
  fpwCtx.stop = true;
  // Se espera a que la tarea salga DE VERDAD antes de dar por
  // cerrado el transporte: soltar aqui y que la tarea siguiera
  // escribiendo en las colas es una corrupcion silenciosa.
  for(int i = 0; i < 200 && fpwCtx.task; i++) vTaskDelay(pdMS_TO_TICKS(10));
  fpwCtx.state = FLP_TC_DOWN;
  fpwLock();
  flexRingInit(&fpwCtx.rx);
  flexRingInit(&fpwCtx.tx);
  fpwUnlock();
  fpwSetStatus("enlace parado");
}

static uint8_t fpwState(FlexPhoneTransport* t){ (void)t; return fpwCtx.state; }

static uint16_t fpwMtu(FlexPhoneTransport* t){
  (void)t;
  // Por TCP cabria mucho mas, pero el tope del protocolo es el mismo
  // en los dos transportes a proposito: ver FlexOS_FlexLink.h.
  return FLNK_MAX_FRAME;
}

static int fpwSend(FlexPhoneTransport* t, const uint8_t* frame, size_t n){
  (void)t;
  if(fpwCtx.state != FLP_TC_OPEN) return FLP_TR_ECLOSED;
  fpwLock();
  const bool ok = flexRingPush(&fpwCtx.tx, frame, n);
  fpwUnlock();
  return ok ? FLP_TR_OK : FLP_TR_EAGAIN;
}

static int fpwRecv(FlexPhoneTransport* t, uint8_t* out, size_t cap){
  (void)t;
  if(fpwCtx.state != FLP_TC_OPEN) return FLP_TR_OK;
  fpwLock();
  const size_t n = flexRingPop(&fpwCtx.rx, out, cap);
  fpwUnlock();
  return (int)n;
}

static const char* fpwStatus(FlexPhoneTransport* t){
  (void)t;
  // Copia estable: el llamador puede guardar el puntero un rato y la
  // tarea puede reescribir fpwCtx.status en cualquier momento.
  static char snap[sizeof(fpwCtx.status)];
  fpwLock();
  memcpy(snap, fpwCtx.status, sizeof(snap));
  fpwUnlock();
  snap[sizeof(snap) - 1] = 0;
  return snap;
}

static void fpwPeer(FlexPhoneTransport* t, char* out, size_t outN){
  (void)t;
  if(!out || !outN) return;
  fpwLock();
  flexLinkUtf8Copy(out, outN, fpwCtx.peer);
  fpwUnlock();
}

static FlexPhoneTransport fpwTransport = {
  FLP_TR_WIFI, fpwStart, fpwStop, fpwState, fpwMtu,
  fpwSend, fpwRecv, fpwStatus, fpwPeer, &fpwCtx,
};

// -------------------------------------------------------------
//  API para la interfaz
// -------------------------------------------------------------
static FlexPhoneTransport* flexPhoneWifiTransport(){ return &fpwTransport; }

// Fija la direccion del telefono a mano. Existe porque bastantes
// routers domesticos aislan a los clientes entre si y la difusion no
// llega: sin esto, el usuario se quedaria en "buscando" para siempre
// sin saber por que.
static void flexPhoneWifiSetHost(const uint8_t ip[4], uint16_t port){
  fpwLock();
  if(ip){ memcpy(fpwCtx.ip, ip, 4); fpwCtx.port = port ? port : FLPW_TCP_PORT; fpwCtx.fixed = true; }
  else  { fpwCtx.port = 0; fpwCtx.fixed = false; fpwCtx.peer[0] = 0; }
  fpwUnlock();
}

static bool flexPhoneWifiHostFixed(){ return fpwCtx.fixed; }

// Nombre que anuncio el telefono al contestar la sonda. Vacio si
// todavia no ha contestado nadie: NO se inventa un "Galaxy" por
// defecto.
static void flexPhoneWifiFoundName(char* out, size_t outN){
  if(!out || !outN) return;
  fpwLock();
  flexLinkUtf8Copy(out, outN, fpwCtx.foundName);
  fpwUnlock();
}
