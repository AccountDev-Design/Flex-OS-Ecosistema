// #############################################################
// ##  FLEX OS ULTRA  ·  SERVICIO DE COMPARTIR POR WI-FI  ·  servidor HTTP
// ##  ----------------------------------------------------------
// ##  La mitad de PLACA del servidor HTTP local: el socket de escucha,
// ##  la tarea propia y el pegamento con el estado de red. El
// ##  protocolo -- analizar la peticion, componer la respuesta -- vive
// ##  en FlexOS_HttpShare.cpp, que es codigo portable con pruebas de
// ##  host y sanitizers.
// ##
// ##  ES UN SERVICIO DEL SISTEMA. Flex Vector Pro es el primero que lo
// ##  usa, pero aqui no hay ni una linea de vectores: se publica un
// ##  BLOQUE DE BYTES con un nombre. Notas, Galeria o Almacenamiento
// ##  pueden compartir por el mismo camino sin tocar este archivo.
// ##
// ##  LA REGLA DE ORO DEL P4, Y ES LA RAZON DE QUE HAYA UNA TAREA.
// ##  El Wi-Fi de esta placa no esta en el P4: esta en el C6, al otro
// ##  lado de un enlace SDIO (esp-hosted). Cualquier llamada de red
// ##  puede bloquear decenas de milisegundos esperando al otro chip.
// ##  loopTask alimenta el Task Watchdog una vez por vuelta, asi que
// ##  aceptar una conexion desde ahi es pedir un PANIC. Por eso todo lo
// ##  que toca sockets corre en flexShareTask, exactamente igual que
// ##  wifiScanTask, wifiConnTask y wifiAutoConnTask.
// ##
// ##  COMO ENCAJA ESTE ARCHIVO
// ##  ----------------------------------------------------------
// ##  Es una PARTE del sketch FlexOS_Ultra.ino, no una unidad de
// ##  traduccion independiente, y se incluye en la cadena lineal de
// ##  modulos justo detras de FlexOS_Ultra_Network.h -- que es donde
// ##  nacen gNetOnline y wifiConnIP, los dos datos que necesita.
// #############################################################
#pragma once
#include "FlexOS_Ultra_Network.h"   // eslabon anterior de la cadena

#include "FlexOS_HttpShare.h"

// Puerto. 8080 y no 80 a proposito: por debajo de 1024 algunos moviles
// y navegadores aplican politicas distintas, y 8080 se escribe igual de
// bien en la barra de direcciones.
#define FLEXSHARE_PORT        8080
#define FLEXSHARE_STACK       6144
#define FLEXSHARE_IDLE_MS     20      // espera entre sondeos sin cliente
#define FLEXSHARE_READ_MS    3000     // techo para leer una peticion entera
#define FLEXSHARE_BUF        4096     // peticion
#define FLEXSHARE_PAGE       4096     // pagina o cabecera de respuesta

// Motivos por los que compartir puede no arrancar. Se ensenan tal cual:
// "no se pudo" no le dice nada a nadie.
enum {
  FLEXSHARE_OK = 0,
  FLEXSHARE_E_OFFLINE,    // no hay Wi-Fi conectado
  FLEXSHARE_E_TASK,       // FreeRTOS no pudo crear la tarea
  FLEXSHARE_E_MEM,        // sin PSRAM para los buffers del servidor
  FLEXSHARE_E_BIND,       // el puerto no se pudo abrir
  FLEXSHARE_E_BUSY        // ya hay algo compartiendose
};

static volatile bool  gShRun     = false;   // la tarea esta viva
static volatile bool  gShStop    = false;   // se le ha pedido parar
static volatile bool  gShReady   = false;   // el socket escucha de verdad
static volatile int   gShErr     = FLEXSHARE_OK;
static volatile uint32_t gShHits = 0;       // descargas servidas
static const uint8_t* gShData    = NULL;    // NO es nuestro: lo presta la app
static volatile size_t gShLen    = 0;
static char  gShName[FLEXHTTP_NAME_MAX] = "";
static char  gShToken[FLEXHTTP_TOKEN_LEN + 1] = "";
static char  gShUrl[128] = "";
static uint8_t* gShBuf   = NULL;            // FLEXSHARE_BUF + FLEXSHARE_PAGE, en PSRAM
static TaskHandle_t gShTask = NULL;

static const char* flexShareErrText(int e){
  switch(e){
    case FLEXSHARE_OK:         return "Listo";
    case FLEXSHARE_E_OFFLINE:  return "Conectate a una red Wi-Fi";
    case FLEXSHARE_E_TASK:     return "El sistema no pudo abrir el servidor";
    case FLEXSHARE_E_MEM:      return "Memoria insuficiente";
    case FLEXSHARE_E_BIND:     return "El puerto 8080 esta ocupado";
    case FLEXSHARE_E_BUSY:     return "Ya hay un archivo compartiendose";
  }
  return "Error";
}

static bool flexShareActive(){ return gShRun && gShReady; }
static const char* flexShareUrl(){ return gShUrl; }
static int  flexShareError(){ return gShErr; }
static uint32_t flexShareHits(){ return gShHits; }

// -------------------------------------------------------------
//  ATENCION A UN CLIENTE
//  ------------------------------------------------------------
//  Se lee hasta el fin de cabeceras con un TECHO de tiempo y otro de
//  tamano. Sin los dos, una conexion que abre y no manda nada -- o que
//  manda cabeceras sin parar -- deja la tarea colgada y el usuario ve
//  "compartiendo" para siempre sin que nada funcione.
// -------------------------------------------------------------
static void flexShareServe(WiFiClient& cli){
  char* req  = (char*)gShBuf;
  char* page = (char*)gShBuf + FLEXSHARE_BUF;
  size_t got = 0;
  uint32_t t0 = millis();
  FlexHttpReq r;
  int st = 0;
  while(millis() - t0 < FLEXSHARE_READ_MS){
    int avail = cli.available();
    if(avail > 0){
      size_t room = FLEXSHARE_BUF - 1 - got;
      if(room == 0){ st = -1; break; }
      int n = cli.read((uint8_t*)req + got, room < (size_t)avail ? room : (size_t)avail);
      if(n <= 0) break;
      got += (size_t)n;
      req[got] = 0;
      st = flexHttpParse(req, got, &r);
      if(st != 0) break;
    } else {
      if(!cli.connected()) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  size_t hlen = 0, blen = 0;
  const uint8_t* body = NULL;
  if(st != 1){
    blen = flexHttpErrorPage(page, FLEXSHARE_PAGE / 2, 400, "Peticion no valida");
    hlen = flexHttpHeader(page + FLEXSHARE_PAGE / 2, FLEXSHARE_PAGE / 2, 400,
                          "text/html; charset=utf-8", blen, NULL, 0);
    if(hlen) cli.write((const uint8_t*)page + FLEXSHARE_PAGE / 2, hlen);
    if(blen) cli.write((const uint8_t*)page, blen);
    return;
  }
  if(r.method == FLEXHTTP_M_UNKNOWN){
    blen = flexHttpErrorPage(page, FLEXSHARE_PAGE / 2, 405, "Solo se admite GET");
    hlen = flexHttpHeader(page + FLEXSHARE_PAGE / 2, FLEXSHARE_PAGE / 2, 405,
                          "text/html; charset=utf-8", blen, NULL, 0);
    if(hlen) cli.write((const uint8_t*)page + FLEXSHARE_PAGE / 2, hlen);
    if(blen) cli.write((const uint8_t*)page, blen);
    return;
  }
  const uint8_t* data = gShData;
  size_t len = gShLen;
  if(flexHttpMatchShare(r.path, gShToken, gShName) && data && len){
    // EL ARCHIVO. Se envia desde PSRAM tal cual: no se copia a un buffer
    // intermedio (serian otros 96 KB) ni se lee de disco (ya esta en
    // memoria, que es donde lo dejo la exportacion).
    hlen = flexHttpHeader(page, FLEXSHARE_PAGE, 200, flexHttpMime(gShName),
                          len, gShName, 0);
    if(hlen) cli.write((const uint8_t*)page, hlen);
    if(r.method == FLEXHTTP_M_GET){
      size_t sent = 0;
      while(sent < len && cli.connected()){
        size_t chunk = len - sent;
        if(chunk > 1460) chunk = 1460;      // una MTU: ni fragmentar de mas ni bloquear de mas
        int n = cli.write(data + sent, chunk);
        if(n <= 0) break;
        sent += (size_t)n;
      }
      if(sent >= len) gShHits++;
    }
    return;
  }
  if(!strcmp(r.path, "/") || !strcmp(r.path, "/index.html")){
    blen = flexHttpIndexPage(page, FLEXSHARE_PAGE / 2, gShToken, gShName, len);
    hlen = flexHttpHeader(page + FLEXSHARE_PAGE / 2, FLEXSHARE_PAGE / 2, 200,
                          "text/html; charset=utf-8", blen, NULL, 0);
    if(hlen) cli.write((const uint8_t*)page + FLEXSHARE_PAGE / 2, hlen);
    if(blen) cli.write((const uint8_t*)page, blen);
    return;
  }
  blen = flexHttpErrorPage(page, FLEXSHARE_PAGE / 2, 404, "Aqui no hay nada");
  hlen = flexHttpHeader(page + FLEXSHARE_PAGE / 2, FLEXSHARE_PAGE / 2, 404,
                        "text/html; charset=utf-8", blen, NULL, 0);
  if(hlen) cli.write((const uint8_t*)page + FLEXSHARE_PAGE / 2, hlen);
  if(blen) cli.write((const uint8_t*)page, blen);
}

static void flexShareTask(void* arg){
  (void)arg;
  WiFiServer server(FLEXSHARE_PORT);
  server.begin();
  gShReady = true;
  while(!gShStop){
    WiFiClient cli = server.accept();
    if(!cli){ vTaskDelay(pdMS_TO_TICKS(FLEXSHARE_IDLE_MS)); continue; }
    cli.setTimeout(2);
    flexShareServe(cli);
    cli.stop();
  }
  server.end();
  gShReady = false;
  gShRun   = false;
  gShTask  = NULL;
  vTaskDelete(NULL);
}

// Publica 'data' (que sigue siendo del llamante y NO puede liberarse ni
// moverse mientras esto este activo) bajo el nombre 'name'.
static int flexShareStart(const char* name, const uint8_t* data, size_t len){
  if(gShRun) return FLEXSHARE_E_BUSY;
  if(!name || !data || len == 0) return FLEXSHARE_E_MEM;
  if(!gNetOnline || !wifiConnIP[0]) return FLEXSHARE_E_OFFLINE;
  if(!gShBuf){
    gShBuf = (uint8_t*)heap_caps_malloc(FLEXSHARE_BUF + FLEXSHARE_PAGE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!gShBuf) gShBuf = (uint8_t*)malloc(FLEXSHARE_BUF + FLEXSHARE_PAGE);
    if(!gShBuf){ gShErr = FLEXSHARE_E_MEM; return FLEXSHARE_E_MEM; }
  }
  snprintf(gShName, sizeof(gShName), "%s", name);
  // TESTIGO DE SESION. La ruta no es adivinable y solo vale mientras la
  // app este abierta: quien no tenga el QR (o el enlace) no llega al
  // archivo aunque este en la misma Wi-Fi y sepa la IP.
  uint64_t seed = ((uint64_t)esp_random() << 32) ^ (uint64_t)esp_random();
  flexHttpToken(seed, gShToken, sizeof(gShToken));
  gShData = data; gShLen = len; gShHits = 0;
  snprintf(gShUrl, sizeof(gShUrl), "http://%s:%d/%s/%s",
           wifiConnIP, (int)FLEXSHARE_PORT, gShToken, gShName);
  gShStop = false; gShReady = false; gShErr = FLEXSHARE_OK;
  gShRun = true;
  if(xTaskCreatePinnedToCore(flexShareTask, "flexShare", FLEXSHARE_STACK,
                             NULL, 1, &gShTask, 1) != pdPASS){
    gShRun = false; gShData = NULL; gShLen = 0; gShUrl[0] = 0;
    gShErr = FLEXSHARE_E_TASK;
    return FLEXSHARE_E_TASK;
  }
  return FLEXSHARE_OK;
}

// Deja de compartir. VUELVE cuando el servidor ya no puede tocar los
// datos: la app libera su buffer justo despues, y devolver el control
// antes seria entregarle al servidor un puntero muerto.
static void flexShareStop(){
  if(!gShRun){ gShData = NULL; gShLen = 0; gShUrl[0] = 0; return; }
  gShStop = true;
  for(int i = 0; i < 200 && gShRun; i++) delay(10);   // 2 s de margen
  gShRun = false; gShReady = false;
  gShData = NULL; gShLen = 0; gShUrl[0] = 0; gShToken[0] = 0;
}

// Suelta los buffers del servicio. La llama memShedSystem cuando el
// sistema necesita PSRAM y aqui no se esta compartiendo nada.
static size_t flexShareShed(){
  if(gShRun || !gShBuf) return 0;
  free(gShBuf); gShBuf = NULL;
  return FLEXSHARE_BUF + FLEXSHARE_PAGE;
}

// Vigilancia desde loop(). NO toca el driver de Wi-Fi: lee gNetOnline,
// que es el estado ya publicado por las tareas de red. Es la misma regla
// que check_wiring.py impone a wgDataTick y ntpTick, y por el mismo
// motivo: un acceso periodico a esp-hosted desde loopTask.
static void flexShareTick(){
  if(gShRun && !gNetOnline) flexShareStop();
}
