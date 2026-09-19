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
#include <WiFi.h>
#include <WiFiUdp.h>

// =============================================================
//  CONSTANTES DEL PROTOCOLO DE RED
//  Estos numeros van al aire: la app Android tiene los MISMOS en
//  WifiLinkServer.kt. Cambiar uno solo aqui deja el telefono y el
//  reloj sin encontrarse, y sin ningun error que lo explique.
// =============================================================
#define FLPW_TCP_PORT        47820   // Flex Link sobre TCP
#define FLPW_UDP_PORT        47821   // descubrimiento
#define FLPW_PROBE           "FLEXPHONE?"
#define FLPW_REPLY           "FLEXPHONE!"
#define FLPW_PROBE_LEN       10
// Cada cuanto se vuelve a preguntar mientras no hay telefono. No es
// sondeo agresivo: son 34 bytes cada dos segundos, y solo mientras
// el usuario tiene el enlace encendido y todavia no hay nadie.
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
#define FLEXOS_DIAG_FLEXPHONE 0

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
  // -- diagnostico (nunca contenido) --
  uint32_t          nConnects, nDrops, nDesync;
} FlexPhoneWifiCtx;

static FlexPhoneWifiCtx fpwCtx;

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
//  Descubrimiento
// -------------------------------------------------------------
// Manda la sonda a la difusion de la subred y espera una respuesta
// corta. Devuelve true si encontro un telefono. NO bloquea mas de
// lo que indique `waitMs`.
static bool fpwDiscover(WiFiUDP& udp, uint32_t waitMs){
  if(WiFi.status() != WL_CONNECTED) return false;
  const IPAddress ip   = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  // Difusion de ESTA subred, no 255.255.255.255: la dirigida llega a
  // donde tiene que llegar y no molesta a redes vecinas.
  IPAddress bcast(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1],
                  ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);

  uint8_t probe[FLPW_PROBE_LEN + 1];
  memcpy(probe, FLPW_PROBE, FLPW_PROBE_LEN);
  probe[FLPW_PROBE_LEN] = FLNK_VERSION;

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
    udp.write(probe, sizeof(probe));
    sent = udp.endPacket() != 0;
  }
  const IPAddress limited(255, 255, 255, 255);
  if(udp.beginPacket(limited, FLPW_UDP_PORT)){
    udp.write(probe, sizeof(probe));
    if(udp.endPacket() != 0) sent = true;
  }
  if(!sent){
    FPW_DIAG("(a) FALLO: no se pudo emitir la sonda");
    return false;
  }

  const uint32_t t0 = millis();
  while(millis() - t0 < waitMs){
    const int n = udp.parsePacket();
    if(n <= 0){ vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    uint8_t buf[80];
    const int got = udp.read(buf, sizeof(buf));
    // Minimo: marca + version + puerto. Menos que eso no se
    // interpreta: leer campos de un paquete corto es como se cuelan
    // los desbordamientos.
    if(got < FLPW_PROBE_LEN + 3){
      FPW_DIAG("(d) paquete de %d B descartado: demasiado corto", got);
      continue;
    }
    if(memcmp(buf, FLPW_REPLY, FLPW_PROBE_LEN) != 0){
      // Lo normal aqui es oir la PROPIA sonda de vuelta: la difusion
      // limitada se entrega tambien al que la emitio. No es un fallo.
      FPW_DIAG("(d) paquete de %d B descartado: no es una respuesta Flex Phone", got);
      continue;
    }
    const uint8_t ver = buf[FLPW_PROBE_LEN];
    if(ver < FLNK_VERSION_MIN){
      fpwSetStatus("la app del telefono es de una version anterior");
      continue;
    }
    const uint16_t port = (uint16_t)(buf[FLPW_PROBE_LEN + 1] |
                                     ((uint16_t)buf[FLPW_PROBE_LEN + 2] << 8));
    if(!port) continue;
    const IPAddress from = udp.remoteIP();
    fpwLock();
    fpwCtx.ip[0] = from[0]; fpwCtx.ip[1] = from[1];
    fpwCtx.ip[2] = from[2]; fpwCtx.ip[3] = from[3];
    fpwCtx.port  = port;
    // Nombre opcional: se copia solo lo que de verdad venga.
    fpwCtx.foundName[0] = 0;
    if(got > FLPW_PROBE_LEN + 3){
      int nl = buf[FLPW_PROBE_LEN + 3];
      const int avail = got - (FLPW_PROBE_LEN + 4);
      if(nl > avail) nl = avail;
      if(nl > (int)sizeof(fpwCtx.foundName) - 1) nl = (int)sizeof(fpwCtx.foundName) - 1;
      if(nl > 0){
        memcpy(fpwCtx.foundName, buf + FLPW_PROBE_LEN + 4, (size_t)nl);
        fpwCtx.foundName[nl] = 0;
      }
    }
    snprintf(fpwCtx.peer, sizeof(fpwCtx.peer), "%u.%u.%u.%u:%u",
             from[0], from[1], from[2], from[3], (unsigned)port);
    fpwUnlock();
    FPW_DIAG("(d) telefono encontrado en %u.%u.%u.%u:%u (protocolo v%u)",
             from[0], from[1], from[2], from[3], (unsigned)port, ver);
    return true;
  }
  return false;
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

    // ---- 2) Buscar el telefono ----
    bool have;
    fpwLock(); have = fpwCtx.port != 0; fpwUnlock();
    if(!have){
      fpwCtx.state = FLP_TC_SEARCHING;
      fpwSetStatus("buscando Flex Phone en la red");
      if(millis() - lastProbe >= FLPW_PROBE_EVERY_MS){
        lastProbe = millis();
        fpwDiscover(udp, 400);
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      continue;
    }

    // ---- 3) Abrir el socket ----
    uint8_t ip[4]; uint16_t port;
    fpwLock();
    memcpy(ip, fpwCtx.ip, 4); port = fpwCtx.port;
    fpwUnlock();

    WiFiClient cli;
    cli.setTimeout(FLPW_CONNECT_MS / 1000 ? FLPW_CONNECT_MS / 1000 : 1);
    fpwCtx.state = FLP_TC_OPENING;
    fpwSetStatus("abriendo el enlace con el telefono");
    char host[16];
    snprintf(host, sizeof(host), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    if(!cli.connect(host, port)){
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
    accN = 0;
    fpwLock();
    flexRingInit(&fpwCtx.rx);
    flexRingInit(&fpwCtx.tx);
    snprintf(fpwCtx.peer, sizeof(fpwCtx.peer), "%s:%u", host, (unsigned)port);
    fpwUnlock();
    fpwCtx.state = FLP_TC_OPEN;
    fpwSetStatus("enlace abierto");

    // ---- 4) Bombeo mientras el socket viva ----
    while(!fpwCtx.stop && cli.connected() && WiFi.status() == WL_CONNECTED){
      bool worked = false;

      // Salida: lo que el enlace haya dejado en la cola.
      uint8_t frame[FLNK_MAX_FRAME];
      for(int i = 0; i < 8; i++){
        fpwLock();
        const size_t n = flexRingPop(&fpwCtx.tx, frame, sizeof(frame));
        fpwUnlock();
        if(!n) break;
        // Una escritura corta deja la trama a medias en el flujo, y
        // el otro extremo ya no puede reensamblar nada: se corta.
        if(cli.write(frame, n) != n){
          fpwSetStatus("se corto al enviar");
          goto closed;
        }
        worked = true;
      }

      // Entrada.
      if(cli.available() > 0){
        if(!fpwPumpRead(cli, acc, accN)) goto closed;
        worked = true;
      }

      // Sin trabajo, se cede la CPU. Sin esta espera la tarea giraria
      // en vacio y le quitaria tiempo al hilo grafico.
      if(!worked) vTaskDelay(pdMS_TO_TICKS(10));
    }

closed:
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
      vTaskDelay(pdMS_TO_TICKS(1000));
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
