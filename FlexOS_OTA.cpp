// #############################################################
// ##  FlexOS_OTA.cpp  ·  MOTOR OTA + INTERFAZ ONE UI
// ##  ------------------------------------------------------
// ##  Una sola implementacion para Flex OS ULTRA (ESP32-P4),
// ##  ULTRA S3 (ESP32-S3) y PRO (ESP32 clasico, SIN PSRAM).
// ##
// ##  REGLAS DE ORO DE ESTE FICHERO (no se rompen nunca):
// ##
// ##   1. NADA de red en el hilo grafico. HTTPClient,
// ##      WiFiClientSecure y Update SOLO se tocan dentro de
// ##      flexOtaTask(). El loop() jamas se bloquea.
// ##   2. NADA de delay(). Solo vTaskDelay(), que cede la CPU.
// ##   3. La UI y la tarea se comunican UNICAMENTE por estado
// ##      compartido volatil. Ni un puntero cruzado, ni un
// ##      callback desde la tarea hacia el render.
// ##   4. La descarga es POR STREAMING: se escribe en la
// ##      particion OTA en bloques de FLEXOS_OTA_CHUNK bytes.
// ##      El firmware completo NUNCA esta en RAM.
// ##   5. Los buffers grandes son ESTATICOS (BSS). Cero malloc
// ##      en la ruta caliente -> cero fragmentacion, cero fugas.
// ##   6. Solo se reinicia si Update.end() confirma que la
// ##      imagen esta entera y es valida. Ante CUALQUIER fallo
// ##      se llama a Update.abort() y la particion activa se
// ##      queda intacta: es imposible dejar el equipo a medias.
// #############################################################

#include "FlexOS_OTA.h"

#include <string.h>
#include <stdio.h>
#include <Arduino.h>

#if FLEXOS_OTA_ON
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #include <HTTPClient.h>
  #include <Update.h>
  #include <Preferences.h>
  #include "esp_ota_ops.h"
  #include "esp_system.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
#endif

#if FLEXOS_OTA_ON

// =============================================================
//  MEDIDAS DE LA INTERFAZ  (lo UNICO que cambia por plataforma)
//  -------------------------------------------------------------
//  El ancho logico es 480 en las TRES placas; solo cambia el alto
//  (800 en Ultra/Ultra S3, 640 en Pro) y, con el, los tamanos de
//  fuente y los espaciados. Todo lo que centra o mide se calcula
//  con otaHostScrW()/otaHostScrH() en tiempo de ejecucion, asi que
//  la interfaz se readapta sola si un dia cambia el panel.
// =============================================================
#if FLEXOS_OTA_PLAT_PRO
  #define OTA_M            16     // margen lateral
  #define OTA_RAD          22     // radio One UI
  #define OTA_TOAST_H      84
  #define OTA_TOAST_GAP    60     // separacion desde el borde inferior
  #define OTA_SZ_BIG        5     // porcentaje gigante
  #define OTA_SZ_TITLE      3
  #define OTA_SZ_HEAD       2
  #define OTA_SZ_BODY       2
  #define OTA_SZ_SMALL      1
  #define OTA_SZ_LOG        1
  #define OTA_LOG_LH       16     // interlineado del changelog
  #define OTA_MODAL_H     430
  #define OTA_BTN_H        48
  #define OTA_ICON        36
#else
  #define OTA_M            16
  #define OTA_RAD          28
  #define OTA_TOAST_H      96
  #define OTA_TOAST_GAP    80
  #define OTA_SZ_BIG        6
  #define OTA_SZ_TITLE      4
  #define OTA_SZ_HEAD       3
  #define OTA_SZ_BODY       2
  #define OTA_SZ_SMALL      1
  #define OTA_SZ_LOG        2
  #define OTA_LOG_LH       24
  #define OTA_MODAL_H     540
  #define OTA_BTN_H        56
  #define OTA_ICON        44
#endif

// Ritmo de repintado del overlay (~30 fps). El sistema grafico corre
// a 26-60 fps segun la ruta; el OTA se conforma con 30 y solo repinta
// cuando de verdad ha cambiado algo (ver gOvDirty).
#define OTA_FRAME_MS      33

// =============================================================
//  ESTADO COMPARTIDO  (tarea de fondo  <->  hilo grafico)
//  -------------------------------------------------------------
//  Escalares de 32 bits alineados: en Xtensa y en RISC-V su
//  lectura/escritura es atomica por hardware, asi que `volatile`
//  basta y evita el coste de std::atomic. Las CADENAS se publican
//  con el patron "escribir primero, publicar despues": la tarea
//  rellena gRemoteVer/gChangelog/gUrl y SOLO ENTONCES pasa el estado
//  a OTA_AVAILABLE. La UI unicamente las lee cuando ve ese estado,
//  asi que nunca puede observar una cadena a medio escribir. Es el
//  mismo patron que ya usa wifiScanTask() con wifiNetCount.
// =============================================================
static volatile FlexOtaState gState    = OTA_IDLE;
static volatile FlexOtaError gError    = OTA_ERR_NONE;
static volatile uint8_t      gPercent  = 0;
static volatile uint32_t     gDone     = 0;    // bytes escritos
static volatile uint32_t     gTotal    = 0;    // bytes totales
static volatile uint32_t     gBps      = 0;    // velocidad suavizada (B/s)
static volatile bool         gCancel   = false;
static volatile bool         gHasUpd   = false; // hay version nueva confirmada

// Candado de los campos de progreso. Cada campo por separado ya seria
// atomico (32 bits alineados), pero la UI los pinta JUNTOS: sin esto
// podia dibujar el porcentaje nuevo al lado del contador de bytes viejo
// -- una cifra incoherente durante un cuadro. La seccion critica es de
// cuatro asignaciones, asi que no cuesta nada medible.
static portMUX_TYPE gProgMux = portMUX_INITIALIZER_UNLOCKED;

// Instantanea coherente de todo el progreso, para que el render trabaje
// sobre valores de un mismo momento.
struct OtaProgSnap { uint8_t pct; uint32_t done, total, bps; FlexOtaState st; };
static void otaProgSnapshot(OtaProgSnap* s){
  portENTER_CRITICAL(&gProgMux);
  s->pct   = gPercent;
  s->done  = gDone;
  s->total = gTotal;
  s->bps   = gBps;
  portEXIT_CRITICAL(&gProgMux);
  s->st    = gState;
}

// Ordenes de la UI hacia la tarea (una sola palabra, sin cola: la
// ultima orden gana, que es exactamente lo que quiere el usuario).
enum : uint8_t { CMD_NONE = 0, CMD_CHECK, CMD_INSTALL };
static volatile uint8_t gCmd = CMD_NONE;

// Datos del manifiesto (los escribe SOLO la tarea)
static char gRemoteVer[16] = "";
static char gUrl[192]      = "";
static char gMd5[36]       = "";
static char gChangelog[FLEXOS_OTA_CHANGELOG_MAX] = "";

// Buffers grandes: ESTATICOS. No se pide ni un byte al heap en toda
// la ruta de descarga -> el heap no se fragmenta y no hay nada que
// liberar (imposible que haya fugas).
static uint8_t gChunk[FLEXOS_OTA_CHUNK];
static char    gManifest[FLEXOS_OTA_MANIFEST_MAX];

static TaskHandle_t gTask = NULL;
static Preferences  gPrefs;
static bool         gJustUpdated = false;   // venimos de una OTA correcta

// =============================================================
//  MICRO-PARSER JSON  (sin dependencias, sin asignaciones)
//  -------------------------------------------------------------
//  Se escribe a mano a proposito en vez de tirar de ArduinoJson:
//  el manifiesto tiene una forma FIJA y conocida, asi que un lector
//  de ~80 lineas que trabaja sobre el buffer ya descargado cuesta
//  unos cientos de bytes de flash y CERO de heap. ArduinoJson
//  habria anadido una dependencia externa y un documento en RAM
//  que la placa sin PSRAM agradece no tener.
// =============================================================

static inline bool jIsSpace(char c){ return c==' '||c=='\t'||c=='\n'||c=='\r'; }

// Busca la clave "key" y devuelve el puntero al PRIMER caracter de su
// valor. Exige comilla-clave-comilla-dos puntos, asi que no confunde
// una clave con un valor que resulte tener el mismo texto.
static const char* jFind(const char* p, const char* end, const char* key){
  size_t kl = strlen(key);
  while(p < end){
    const char* q = (const char*)memchr(p, '"', (size_t)(end - p));
    if(!q) return NULL;
    if((size_t)(end - q) > kl + 2 && memcmp(q + 1, key, kl) == 0 && q[1 + kl] == '"'){
      const char* r = q + 2 + kl;
      while(r < end && jIsSpace(*r)) r++;
      if(r < end && *r == ':'){ r++; while(r < end && jIsSpace(*r)) r++; return r; }
    }
    p = q + 1;
  }
  return NULL;
}

// Acota el objeto { ... } de una clave, contando llaves y respetando
// las que aparezcan dentro de una cadena. Gracias a esto, "version"
// se busca SOLO dentro del bloque de esta placa.
//
// Nota fina: la clave se busca con sus comillas incluidas, asi que
// "flex_os_ultra" NO casa con "flex_os_ultra_s3" (el caracter
// siguiente tendria que ser una comilla y es una '_').
static bool jBlock(const char* buf, const char* key, const char** bs, const char** be){
  const char* end = buf + strlen(buf);
  const char* p = jFind(buf, end, key);
  if(!p || p >= end || *p != '{') return false;
  int depth = 0; bool instr = false;
  for(const char* q = p; q < end; q++){
    if(instr){
      if(*q == '\\' && q + 1 < end) q++;
      else if(*q == '"') instr = false;
    } else if(*q == '"') instr = true;
    else if(*q == '{')  depth++;
    else if(*q == '}'){ depth--; if(depth == 0){ *bs = p + 1; *be = q; return true; } }
  }
  return false;   // llaves desbalanceadas -> JSON corrupto
}

// Extrae una cadena JSON (con escapes) a un buffer acotado.
static bool jStr(const char* p, const char* end, char* out, size_t cap){
  if(!p || p >= end || *p != '"' || cap == 0) return false;
  p++; size_t n = 0;
  while(p < end && *p != '"'){
    char c = *p++;
    if(c == '\\' && p < end){
      char e = *p++;
      switch(e){
        case 'n': c = '\n'; break;
        case 'r': continue;                 // se ignora: \n ya salta linea
        case 't': c = ' ';  break;
        case 'u': for(int i = 0; i < 4 && p < end; i++) p++; c = '?'; break;  // sin tabla Unicode
        default:  c = e;    break;          // \" \\ \/ ...
      }
    }
    if(n + 1 < cap) out[n++] = c;
  }
  out[n] = 0;
  return (p < end && *p == '"');            // cadena sin cerrar -> corrupto
}

static uint32_t jNum(const char* p, const char* end){
  uint32_t v = 0;
  while(p < end && *p >= '0' && *p <= '9'){ v = v * 10u + (uint32_t)(*p - '0'); p++; }
  return v;
}

// -------------------------------------------------------------
//  Versiones MAJOR.MINOR.PATCH. Estricto a proposito: cualquier
//  cosa que no sean exactamente tres numeros separados por puntos
//  se rechaza -> OTA_ERR_BAD_VERSION (el estado "version invalida").
// -------------------------------------------------------------
static bool verParse(const char* s, uint16_t v[3]){
  v[0] = v[1] = v[2] = 0;
  if(!s || !*s) return false;
  int i = 0;
  while(*s){
    if(*s < '0' || *s > '9') return false;
    uint32_t n = 0;
    while(*s >= '0' && *s <= '9'){ n = n * 10u + (uint32_t)(*s - '0'); if(n > 65535u) return false; s++; }
    if(i > 2) return false;
    v[i++] = (uint16_t)n;
    if(*s == '.'){ s++; if(!*s) return false; }
    else if(*s) return false;
  }
  return i == 3;
}
static int verCmp(const uint16_t a[3], const uint16_t b[3]){
  for(int i = 0; i < 3; i++){ if(a[i] != b[i]) return a[i] < b[i] ? -1 : 1; }
  return 0;
}

// =============================================================
//  RED  (SOLO se ejecuta dentro de flexOtaTask)
// =============================================================

static inline void otaSet(FlexOtaState s){ gState = s; }
static bool otaFail(FlexOtaError e){
  gError = e;
  gState = OTA_ERROR;
  Serial.printf("[OTA] fallo, codigo=%u\n", (unsigned)e);
  return false;
}

// Abre una peticion HTTPS y deja la respuesta lista para leer.
// Traduce cada modo de fallo a SU codigo: sin conexion, HTTPS caido,
// certificado invalido, 404, timeout... nunca a un generico.
static bool otaOpen(HTTPClient& http, WiFiClientSecure& sec, const char* url){
  if(WiFi.status() != WL_CONNECTED) return otaFail(OTA_ERR_NO_WIFI);
  if(strncmp(url, "https://", 8) != 0) return otaFail(OTA_ERR_BAD_URL);

  // Filtro de memoria ANTES de tocar TLS: mbedTLS reserva sus buffers
  // de registro al conectar y en la placa sin PSRAM eso puede no caber.
  // Mejor un estado claro que un fallo de asignacion a media conexion.
  if(esp_get_free_heap_size() < FLEXOS_OTA_MIN_HEAP) return otaFail(OTA_ERR_LOWMEM);

  const char* ca = FLEXOS_OTA_ROOT_CA;
  bool pinned = (ca && ca[0]);
  if(pinned) sec.setCACert(ca);      // validacion real del certificado
  else       sec.setInsecure();      // solo para pruebas (ver FlexOS_OTA.h)
  sec.setTimeout(FLEXOS_OTA_HTTP_TIMEOUT / 1000);

  http.setTimeout(FLEXOS_OTA_HTTP_TIMEOUT);
  http.setConnectTimeout(FLEXOS_OTA_HTTP_TIMEOUT);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.useHTTP10(true);              // sin chunked: getSize() es fiable

  if(!http.begin(sec, url)) return otaFail(OTA_ERR_HTTPS);

  int code = http.GET();
  if(code < 0){
    // Codigos negativos = fallo de transporte. Con un CA fijado, lo mas
    // probable con diferencia es que el certificado no valide.
    if(code == HTTPC_ERROR_READ_TIMEOUT || code == HTTPC_ERROR_CONNECTION_LOST)
      return otaFail(OTA_ERR_TIMEOUT);
    return otaFail(pinned ? OTA_ERR_TLS : OTA_ERR_HTTPS);
  }
  if(code != HTTP_CODE_OK) return otaFail(OTA_ERR_HTTP_STATUS);   // 404 incluido
  return true;
}

// ---- Paso 1: manifiesto -------------------------------------------
static bool otaCheck(){
  otaSet(OTA_CHECKING);
  gError = OTA_ERR_NONE;

  WiFiClientSecure sec;
  HTTPClient http;
  if(!otaOpen(http, sec, FLEXOS_OTA_MANIFEST_URL)){ http.end(); return false; }

  WiFiClient* st = http.getStreamPtr();
  if(!st){ http.end(); return otaFail(OTA_ERR_HTTPS); }     // sin flujo no hay nada que leer
  size_t n = 0; uint32_t last = millis();
  while(n < sizeof(gManifest) - 1){
    if(st->available()){
      int r = st->readBytes((uint8_t*)gManifest + n, sizeof(gManifest) - 1 - n);
      if(r > 0){ n += (size_t)r; last = millis(); continue; }
    }
    if(!st->connected() && !st->available()) break;          // servidor cerro: fin normal
    if(millis() - last > FLEXOS_OTA_STALL_TIMEOUT){ http.end(); return otaFail(OTA_ERR_TIMEOUT); }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  gManifest[n] = 0;
  http.end();
  if(n == 0) return otaFail(OTA_ERR_JSON);

  // --- Bloque de ESTA placa (FLEXOS_OTA_KEY sale del #ifdef de la
  //     cabecera). Cada plataforma lee unicamente lo suyo. ---
  const char *bs = NULL, *be = NULL;
  if(!jBlock(gManifest, FLEXOS_OTA_KEY, &bs, &be)) return otaFail(OTA_ERR_NO_BLOCK);

  char ver[16] = "";
  const char* pv = jFind(bs, be, "version");
  if(!pv || !jStr(pv, be, ver, sizeof(ver))) return otaFail(OTA_ERR_JSON);

  uint16_t rv[3], lv[3];
  if(!verParse(ver, rv))                return otaFail(OTA_ERR_BAD_VERSION);
  if(!verParse(FLEXOS_FW_VERSION, lv))  return otaFail(OTA_ERR_BAD_VERSION);

  if(verCmp(rv, lv) <= 0){              // igual o mas antigua: nada que hacer
    gHasUpd = false; gRemoteVer[0] = 0;
    otaSet(OTA_UP_TO_DATE);
    return true;
  }

  char url[sizeof(gUrl)] = "";
  const char* pu = jFind(bs, be, "url");
  if(!pu || !jStr(pu, be, url, sizeof(url)))   return otaFail(OTA_ERR_JSON);
  if(strncmp(url, "https://", 8) != 0)         return otaFail(OTA_ERR_BAD_URL);

  char log[FLEXOS_OTA_CHANGELOG_MAX] = "";
  const char* pc = jFind(bs, be, "changelog");
  if(pc) jStr(pc, be, log, sizeof(log));       // opcional

  char md5[sizeof(gMd5)] = "";
  const char* pm = jFind(bs, be, "md5");
  if(pm) jStr(pm, be, md5, sizeof(md5));       // opcional pero MUY recomendable

  uint32_t size = 0;
  const char* ps = jFind(bs, be, "size");
  if(ps) size = jNum(ps, be);                  // opcional (informativo)

  // Publicacion: primero los datos, el estado DESPUES (ver cabecera
  // de "ESTADO COMPARTIDO"). Asi la UI nunca lee cadenas a medias.
  strncpy(gRemoteVer, ver, sizeof(gRemoteVer) - 1); gRemoteVer[sizeof(gRemoteVer) - 1] = 0;
  strncpy(gUrl,       url, sizeof(gUrl) - 1);       gUrl[sizeof(gUrl) - 1] = 0;
  strncpy(gMd5,       md5, sizeof(gMd5) - 1);       gMd5[sizeof(gMd5) - 1] = 0;
  strncpy(gChangelog, log, sizeof(gChangelog) - 1); gChangelog[sizeof(gChangelog) - 1] = 0;
  gTotal   = size;
  gDone    = 0;
  gPercent = 0;

  // BARRERA DE MEMORIA antes de publicar. `volatile` solo ordena entre
  // si los accesos volatiles: sin esta barrera, el compilador o el
  // buffer de escritura del procesador podrian dejar que el OTRO nucleo
  // (la UI corre en el core del loopTask, esta tarea en el suyo) viera
  // ya el estado OTA_AVAILABLE mientras gRemoteVer/gChangelog aun van a
  // medio escribir. Con ella, quien observe el estado nuevo ve tambien
  // todas las cadenas completas.
  __sync_synchronize();

  gHasUpd  = true;
  otaSet(OTA_AVAILABLE);
  Serial.printf("[OTA] disponible %s (local %s)\n", gRemoteVer, FLEXOS_FW_VERSION);
  return true;
}

// -------------------------------------------------------------
//  ¿Es este MD5 utilizable de verdad?
//  Exige 32 digitos hexadecimales Y que no sean todo ceros. Lo
//  segundo es lo importante: "00000000000000000000000000000000" es
//  el valor de relleno que llevan los manifiestos de ejemplo, mide
//  32 caracteres y por tanto pasaba cualquier filtro de longitud.
//  Pasarselo a Update.setMD5() condena la actualizacion: el MD5 real
//  de la imagen nunca sera 32 ceros, asi que Update.end() falla
//  siempre y la descarga termina en "Firmware corrupto".
// -------------------------------------------------------------
static bool otaMd5Usable(const char* s){
  if(!s || strlen(s) != 32) return false;
  bool allZero = true;
  for(int i = 0; i < 32; i++){
    char c = s[i];
    bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if(!hex) return false;               // no es hexadecimal -> inservible
    if(c != '0') allZero = false;
  }
  return !allZero;                        // 32 ceros = relleno, no un hash
}

// -------------------------------------------------------------
//  Traza de diagnostico. Se emite al empezar, cada 10% y al acabar.
//  Lleva lo necesario para explicar un reinicio aleatorio sin tener
//  que reproducirlo con un depurador conectado:
//    core   -> en que nucleo corre de verdad la tarea OTA
//    heap   -> memoria libre (y el minimo historico)
//    pila   -> bytes que le SOBRAN a la tarea en su peor momento;
//              si esto se acerca a 0, el reinicio es desbordamiento
//              de pila y hay que subir FLEXOS_OTA_TASK_STACK
//    bytes  -> escritos vs esperados
// -------------------------------------------------------------
static void otaLogProgress(const char* tag, uint32_t written, uint32_t total){
  Serial.printf("[OTA] %-13s core=%d  pct=%3u  bytes=%u/%u  heap=%u (min %u)  pila_libre=%u\n",
                tag,
                (int)xPortGetCoreID(),
                (unsigned)(total ? (uint32_t)((uint64_t)written * 100u / total) : 0u),
                (unsigned)written, (unsigned)total,
                (unsigned)esp_get_free_heap_size(),
                (unsigned)esp_get_minimum_free_heap_size(),
                (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
}

// ---- Paso 2: descarga por streaming + instalacion -----------------
static bool otaInstall(){
  if(!gHasUpd || !gUrl[0]) return otaFail(OTA_ERR_BAD_URL);

  gCancel  = false;
  gDone    = 0;
  gPercent = 0;
  gBps     = 0;
  gError   = OTA_ERR_NONE;
  otaSet(OTA_CONNECTING);

  // Sin segunda particion de app no hay OTA posible. Se comprueba ANTES
  // de descargar un solo byte: en Flex OS PRO con el esquema "Huge APP
  // (No OTA)" esto es exactamente lo que pasa, y asi el usuario ve un
  // mensaje claro en vez de un fallo raro al final de la descarga.
  if(esp_ota_get_next_update_partition(NULL) == NULL) return otaFail(OTA_ERR_NO_PARTITION);

  WiFiClientSecure sec;
  HTTPClient http;
  if(!otaOpen(http, sec, gUrl)){ http.end(); return false; }

  int len = http.getSize();
  if(len <= 0){ http.end(); return otaFail(OTA_ERR_BAD_SIZE); }          // sin Content-Length
  uint32_t total = (uint32_t)len;
  if(total < FLEXOS_OTA_MIN_BIN || total > FLEXOS_OTA_MAX_BIN){
    http.end(); return otaFail(OTA_ERR_BAD_SIZE);
  }
  gTotal = total;

  if(!Update.begin(total, U_FLASH)){                                      // no cabe en la particion
    http.end(); return otaFail(OTA_ERR_BEGIN);
  }
  // Integridad REAL: si el manifiesto trae un MD5 DE VERDAD, Update lo
  // verifica al cerrar y rechaza la imagen si no cuadra.
  //
  // El filtro otaMd5Usable() no es cosmetico: el manifiesto de ejemplo
  // se publico con "00000000000000000000000000000000" como relleno. Esa
  // cadena tiene 32 caracteres, asi que la comprobacion anterior
  // (strlen == 32) la daba por buena y se la pasaba a Update.setMD5().
  // Al cerrar, el MD5 real de la imagen jamas coincide con 32 ceros ->
  // Update.end() fallaba SIEMPRE -> "Firmware corrupto" al final de cada
  // descarga completa. Ahora un relleno se ignora (con aviso por Serial)
  // en vez de condenar la actualizacion.
  if(otaMd5Usable(gMd5)){
    Update.setMD5(gMd5);
    Serial.printf("[OTA] verificacion MD5 activa: %s\n", gMd5);
  } else if(gMd5[0]){
    Serial.printf("[OTA] AVISO: md5 del manifiesto no utilizable (\"%s\"); se omite la verificacion MD5.\n"
                  "[OTA]        La imagen se sigue validando por cabecera y tamano.\n", gMd5);
  } else {
    Serial.println(F("[OTA] AVISO: el manifiesto no trae md5; se omite la verificacion MD5."));
  }

  otaSet(OTA_DOWNLOADING);
  WiFiClient* st = http.getStreamPtr();
  if(!st){ Update.abort(); http.end(); return otaFail(OTA_ERR_HTTPS); }   // deja la particion intacta

  uint32_t written = 0, last = millis();
  uint32_t spdBytes = 0, spdT0 = millis();
  uint8_t  logStep = 0;                    // ultimo tramo de 10% registrado

  otaLogProgress("inicio", 0, total);

  while(written < total){
    // --- Cancelacion: se atiende en cada vuelta ---
    if(gCancel){ Update.abort(); http.end(); return otaFail(OTA_ERR_CANCELLED); }

    size_t avail = (size_t)st->available();
    if(avail){
      size_t want = avail > sizeof(gChunk) ? sizeof(gChunk) : avail;
      if(want > total - written) want = total - written;
      int r = st->readBytes(gChunk, want);
      if(r <= 0){ vTaskDelay(pdMS_TO_TICKS(2)); continue; }

      // --- ESCRITURA EN FLASH, SERIALIZADA CON LA SUBIDA AL PANEL ---
      // Escribir en la flash SPI apaga la cache de los dos nucleos. Si el
      // presenter (Core 0) estuviera en mitad de su DMA hacia el MIPI-DSI,
      // el FIFO del panel se vaciaria y saldria un frame basura: ese era
      // el destello cian que aparecia en cada 1%. Con el candado del
      // panel tomado, la escritura espera a que la subida en vuelo
      // termine y el presenter no puede empezar otra mientras tanto.
      // El bloque es de 4 KB (~1-3 ms), muy por debajo del presupuesto
      // de 33 ms por cuadro de la UI.
      otaHostLockPanel();
      size_t w = Update.write(gChunk, (size_t)r);
      otaHostUnlockPanel();

      if(w != (size_t)r){                                               // fallo de escritura en flash
        Serial.printf("[OTA] ERROR de escritura: pedidos %d, escritos %u\n", r, (unsigned)w);
        Update.abort(); http.end(); return otaFail(OTA_ERR_WRITE);
      }
      written  += (uint32_t)r;
      spdBytes += (uint32_t)r;
      last      = millis();

      // Publicacion del progreso bajo seccion critica: los cuatro campos
      // se actualizan juntos, asi que la UI nunca puede leer un
      // porcentaje nuevo con un contador de bytes viejo (o al reves).
      uint8_t pct = (uint8_t)((uint64_t)written * 100u / total);
      portENTER_CRITICAL(&gProgMux);
      gDone    = written;
      gPercent = pct;
      portEXIT_CRITICAL(&gProgMux);

      uint32_t dt = millis() - spdT0;
      if(dt >= 500){                       // velocidad suavizada cada 0,5 s
        uint32_t inst = (uint32_t)((uint64_t)spdBytes * 1000u / dt);
        portENTER_CRITICAL(&gProgMux);
        gBps = gBps ? (uint32_t)(((uint64_t)gBps * 3u + inst) / 4u) : inst;
        portEXIT_CRITICAL(&gProgMux);
        spdBytes = 0; spdT0 = millis();
      }

      // Diagnostico cada 10%: si vuelve a aparecer un reinicio aleatorio,
      // el ultimo tramo registrado dice donde y con cuanta memoria/pila.
      if(pct / 10 > logStep){
        logStep = pct / 10;
        otaLogProgress("descarga", written, total);
      }

      // Cede la CPU entre bloques. La tarea OTA y el loop() grafico
      // comparten el Core 1 con la MISMA prioridad (1), asi que sin esta
      // cesion explicita el reparto dependeria solo del corte por tick:
      // la UI se veria a tirones y el TWDT del loopTask se acercaria
      // demasiado a su limite.
      vTaskDelay(1);
    } else {
      if(!st->connected() && !st->available()) break;                    // conexion cerrada antes de tiempo
      if(millis() - last > FLEXOS_OTA_STALL_TIMEOUT){                    // ni un byte en 15 s
        Serial.printf("[OTA] TIMEOUT sin datos en %u bytes de %u\n", (unsigned)written, (unsigned)total);
        Update.abort(); http.end(); return otaFail(OTA_ERR_TIMEOUT);
      }
      vTaskDelay(pdMS_TO_TICKS(4));        // cede CPU: el hilo grafico sigue a 30-60 fps
    }
  }
  http.end();
  otaLogProgress("fin descarga", written, total);

  if(written != total){ Update.abort(); return otaFail(OTA_ERR_TRUNCATED); }

  // --- Verificacion + cierre -------------------------------------
  otaSet(OTA_VERIFYING);
  vTaskDelay(pdMS_TO_TICKS(120));          // deja ver el estado en pantalla

  otaSet(OTA_INSTALLING);
  // Update.end() vuelca el ultimo bloque y sella la particion: tambien
  // escribe en flash, asi que va bajo el mismo candado que el resto de
  // escrituras (misma razon: no solapar con la DMA del panel).
  otaHostLockPanel();
  bool endOk = Update.end(true);           // valida MD5 + cabecera de imagen
  otaHostUnlockPanel();

  if(!endOk){
    Serial.printf("[OTA] Update.end() fallo, codigo=%u: %s\n",
                  (unsigned)Update.getError(), Update.errorString());
    Update.abort();
    return otaFail(OTA_ERR_CORRUPT);
  }
  if(!Update.isFinished()){
    Serial.println(F("[OTA] Update.isFinished() == false tras end()"));
    Update.abort();
    return otaFail(OTA_ERR_CORRUPT);
  }
  otaLogProgress("instalada", written, total);

  // --- Finalizacion ----------------------------------------------
  otaSet(OTA_FINALIZING);
  gPrefs.begin("flexota", false);
  gPrefs.putString("pend", gRemoteVer);    // para saludar tras el reinicio
  gPrefs.end();
  vTaskDelay(pdMS_TO_TICKS(400));

  otaSet(OTA_REBOOTING);
  Serial.printf("[OTA] instalada %s -> reiniciando\n", gRemoteVer);
  for(int i = 0; i < 16; i++) vTaskDelay(pdMS_TO_TICKS(100));   // 1,6 s de cortesia visual
  esp_restart();
  return true;                             // inalcanzable
}

// ---- Tarea de fondo -----------------------------------------------
//  Prioridad 1 (baja, la misma que el loopTask de Arduino) y anclada
//  al core 1, igual que wifiScanTask/wifiConnTask del sistema: asi no
//  compite con el presenter grafico, que vive en el core 0.
static void flexOtaTask(void*){
  for(;;){
    uint8_t cmd = gCmd;
    if(cmd == CMD_NONE){ vTaskDelay(pdMS_TO_TICKS(120)); continue; }
    gCmd = CMD_NONE;

    if(cmd == CMD_CHECK)        otaCheck();
    else if(cmd == CMD_INSTALL) otaInstall();

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =============================================================
//  PALETA Y AYUDAS DE DIBUJO  (One UI, clara y oscura)
// =============================================================
static inline uint16_t cBg()     { return otaHostDark() ? otaHostRGB(16,18,26)   : otaHostRGB(244,247,251); }
static inline uint16_t cCard()   { return otaHostDark() ? otaHostRGB(34,38,50)   : otaHostRGB(255,255,255); }
static inline uint16_t cGlass()  { return otaHostDark() ? otaHostRGB(48,54,72)   : otaHostRGB(238,242,248); }
static inline uint16_t cTxtHi()  { return otaHostDark() ? otaHostRGB(240,242,248): otaHostRGB(20,22,30);    }
static inline uint16_t cTxtLo()  { return otaHostDark() ? otaHostRGB(160,166,182): otaHostRGB(120,126,140); }
static inline uint16_t cAccent() { return otaHostRGB(60,140,235); }
static inline uint16_t cOk()     { return otaHostRGB(60,190,130); }
static inline uint16_t cErr()    { return otaHostRGB(230,90,90);  }
static inline uint16_t cLine()   { return otaHostDark() ? otaHostRGB(58,64,82)   : otaHostRGB(224,229,238); }

static inline float otaEaseOut(float p){ float q = 1.0f - p; return 1.0f - q * q * q; }

// Sombra suave One UI: varios rectangulos redondeados muy transparentes,
// cada uno un poco mas grande. Es barato (son filas con alpha) y da el
// difuminado sin necesidad de un blur de verdad.
static void otaShadow(int x, int y, int w, int h, int rad){
  for(int i = 5; i >= 1; i--)
    otaHostFillRoundRectA(x - i, y - i + 3, w + 2 * i, h + 2 * i, rad + i, otaHostRGB(0,0,0), 10);
}

// Icono de actualizacion: circulo + flecha hacia abajo (descarga).
// Se dibuja con trazos antialias en vez de un bitmap: escala a
// cualquier tamano y no gasta ni un byte de flash en datos.
static void otaGlyphUpdate(int x, int y, int s, uint16_t col, uint16_t fg){
  int cx = x + s / 2, cy = y + s / 2;
  otaHostFillCircle(cx, cy, s / 2, col);
  float t = s * 0.055f; if(t < 1.6f) t = 1.6f;
  otaHostStroke(cx, cy - s * 0.24f, cx, cy + s * 0.16f, t, fg);
  otaHostStroke(cx - s * 0.16f, cy - s * 0.02f, cx, cy + s * 0.17f, t, fg);
  otaHostStroke(cx + s * 0.16f, cy - s * 0.02f, cx, cy + s * 0.17f, t, fg);
  otaHostStroke(cx - s * 0.20f, cy + s * 0.28f, cx + s * 0.20f, cy + s * 0.28f, t, fg);
}

// Marca de verificado (sistema al dia)
static void otaGlyphOk(int x, int y, int s, uint16_t col, uint16_t fg){
  int cx = x + s / 2, cy = y + s / 2;
  otaHostFillCircle(cx, cy, s / 2, col);
  float t = s * 0.075f; if(t < 1.8f) t = 1.8f;
  otaHostStroke(cx - s * 0.20f, cy + s * 0.02f, cx - s * 0.04f, cy + s * 0.17f, t, fg);
  otaHostStroke(cx - s * 0.04f, cy + s * 0.17f, cx + s * 0.21f, cy - s * 0.16f, t, fg);
}

// Aspa (error)
static void otaGlyphErr(int x, int y, int s, uint16_t col, uint16_t fg){
  int cx = x + s / 2, cy = y + s / 2;
  otaHostFillCircle(cx, cy, s / 2, col);
  float t = s * 0.075f; if(t < 1.8f) t = 1.8f;
  float d = s * 0.17f;
  otaHostStroke(cx - d, cy - d, cx + d, cy + d, t, fg);
  otaHostStroke(cx - d, cy + d, cx + d, cy - d, t, fg);
}

static void otaFmtBytes(uint32_t b, char* out, size_t cap){
  if(b >= 1048576u) snprintf(out, cap, "%u,%01u MB", (unsigned)(b / 1048576u), (unsigned)((b % 1048576u) * 10u / 1048576u));
  else if(b >= 1024u) snprintf(out, cap, "%u KB", (unsigned)(b / 1024u));
  else snprintf(out, cap, "%u B", (unsigned)b);
}

// Texto con salto de linea por palabras. Devuelve el TOTAL de lineas
// que ocupa el texto (aunque no se dibujen todas), que es justo lo que
// necesita el scroll del changelog para conocer su recorrido.
static int otaWrap(const char* s, int x, int y, int maxW, int size, uint16_t col,
                   int lineH, int skip, int maxLines){
  char line[128]; int ll = 0;
  int idx = 0, drawn = 0;
  if(!s) return 0;
  while(*s){
    // Palabra siguiente (o salto explicito)
    if(*s == '\n'){
      s++;
      if(idx >= skip && drawn < maxLines){ line[ll] = 0; otaHostText(x, y + drawn * lineH, line, size, col); drawn++; }
      else if(idx >= skip) drawn++;
      idx++; ll = 0;
      continue;
    }
    const char* w0 = s;
    while(*s && *s != ' ' && *s != '\n') s++;
    int wl = (int)(s - w0);
    if(wl > (int)sizeof(line) - 1) wl = (int)sizeof(line) - 1;

    char cand[128];
    int cl = 0;
    if(ll){ memcpy(cand, line, ll); cl = ll; cand[cl++] = ' '; }
    if(cl + wl > (int)sizeof(cand) - 1) wl = (int)sizeof(cand) - 1 - cl;
    memcpy(cand + cl, w0, wl); cl += wl; cand[cl] = 0;

    if(ll && otaHostTextW(cand, size) > maxW){
      // No cabe: cierra la linea actual y empieza otra con la palabra
      line[ll] = 0;
      if(idx >= skip && drawn < maxLines){ otaHostText(x, y + drawn * lineH, line, size, col); drawn++; }
      else if(idx >= skip) drawn++;
      idx++;
      ll = 0;
      memcpy(line, w0, wl); ll = wl;
    } else {
      memcpy(line, cand, cl); ll = cl;
    }
    while(*s == ' ') s++;
  }
  if(ll){
    line[ll] = 0;
    if(idx >= skip && drawn < maxLines) otaHostText(x, y + drawn * lineH, line, size, col);
    idx++;
  }
  return idx;
}

// =============================================================
//  ADMINISTRADOR DE OVERLAYS
//  -------------------------------------------------------------
//  El OTA NUNCA dibuja sobre el framebuffer de una app: compone en
//  el back buffer del sistema (otaHostBeginCompose -> setBuf(bbuf))
//  y publica la banda ya terminada (otaHostEndCompose -> present()).
//  Se llama al FINAL del pipeline, despues de notifTick(), asi que
//  siempre queda por encima de todo lo demas.
// =============================================================
static OverlayState gOv       = OVERLAY_HIDDEN;
static uint32_t     gOvT0     = 0;       // inicio de la animacion de entrada
static bool         gOvClosing= false;
static uint32_t     gOvCloseT0= 0;
static bool         gOvDirty  = true;    // hay que repintar
static bool         gNeedClear= false;   // queda un frame de limpieza pendiente
static uint32_t     gLastFrame= 0;
static int          gLogScroll= 0;       // desplazamiento del changelog (lineas)
static int          gLogLines = 0;       // lineas totales del changelog
static bool         gLogDrag  = false;
static int          gLogDragY0= 0, gLogScroll0 = 0;

// Estado ya mostrado, para repintar solo cuando cambia algo de verdad
static FlexOtaState gShownState = OTA_IDLE;
static uint8_t      gShownPct   = 255;

#define OTA_ANIM_IN   300
#define OTA_ANIM_OUT  240
#define OTA_TOAST_MS 9000

// --- Registro de botones -------------------------------------------
//  Se apunta el rectangulo REAL de cada boton al dibujarlo y el tactil
//  consulta ese registro. Mismo patron que setRowCard()/setRowY0 en
//  Ajustes: la zona pulsable no puede desincronizarse de lo pintado.
enum : uint8_t { BT_NONE = 0, BT_CHECK, BT_INSTALL, BT_CANCEL, BT_CLOSE, BT_BACK, BT_TOAST };
#define OTA_BTN_MAX 5
static int     gBX0[OTA_BTN_MAX], gBY0[OTA_BTN_MAX], gBX1[OTA_BTN_MAX], gBY1[OTA_BTN_MAX];
static uint8_t gBId[OTA_BTN_MAX];
static int     gBN = 0;

static void btnReset(){ gBN = 0; }
static void btnAdd(uint8_t id, int x, int y, int w, int h){
  if(gBN >= OTA_BTN_MAX) return;
  gBX0[gBN] = x; gBY0[gBN] = y; gBX1[gBN] = x + w; gBY1[gBN] = y + h; gBId[gBN] = id; gBN++;
}
static uint8_t btnHit(int px, int py){
  for(int i = 0; i < gBN; i++)
    if(px >= gBX0[i] && px <= gBX1[i] && py >= gBY0[i] && py <= gBY1[i]) return gBId[i];
  return BT_NONE;
}

// Boton One UI (pildora). filled = accion principal.
static void otaButton(uint8_t id, int x, int y, int w, int h, const char* label, bool filled){
  int rad = h / 2;
  if(filled){
    otaHostFillRoundRect(x, y, w, h, rad, cAccent());
    otaHostTextC(x + w / 2, y + (h - (OTA_SZ_BODY * 8)) / 2, label, OTA_SZ_BODY, otaHostRGB(255,255,255));
  } else {
    otaHostFillRoundRect(x, y, w, h, rad, otaHostDark() ? otaHostRGB(52,58,76) : otaHostRGB(232,236,244));
    otaHostTextC(x + w / 2, y + (h - (OTA_SZ_BODY * 8)) / 2, label, OTA_SZ_BODY, cTxtHi());
  }
  btnAdd(id, x, y, w, h);
}

// Barra de progreso One UI (pista + relleno redondeado)
static void otaProgressBar(int x, int y, int w, int h, uint8_t pct){
  otaHostFillRoundRect(x, y, w, h, h / 2, otaHostDark() ? otaHostRGB(52,58,76) : otaHostRGB(224,229,238));
  int fw = (int)((uint32_t)w * pct / 100u);
  if(fw < h) fw = (pct > 0) ? h : 0;               // que se vea aunque sea 1%
  if(fw > 0) otaHostFillRoundRect(x, y, fw, h, h / 2, cAccent());
}

// -------------------------------------------------------------
//  Textos de estado (una sola fuente de verdad para toda la UI)
// -------------------------------------------------------------
static const char* otaErrText(FlexOtaError e){
  switch(e){
    case OTA_ERR_NO_WIFI:      return "Sin conexion a internet";
    case OTA_ERR_LOWMEM:       return "Memoria insuficiente";
    case OTA_ERR_HTTPS:        return "No se pudo conectar (HTTPS)";
    case OTA_ERR_TLS:          return "Certificado no valido";
    case OTA_ERR_HTTP_STATUS:  return "Archivo no encontrado";
    case OTA_ERR_TIMEOUT:      return "Se agoto el tiempo de espera";
    case OTA_ERR_JSON:         return "Manifiesto corrupto";
    case OTA_ERR_NO_BLOCK:     return "Sin version para este equipo";
    case OTA_ERR_BAD_VERSION:  return "Version no valida";
    case OTA_ERR_BAD_URL:      return "Direccion de descarga no valida";
    case OTA_ERR_BAD_SIZE:     return "Tamano de firmware no valido";
    case OTA_ERR_NO_PARTITION: return "Particion sin soporte OTA";
    case OTA_ERR_BEGIN:        return "No hay espacio para la imagen";
    case OTA_ERR_WRITE:        return "Error de escritura";
    case OTA_ERR_TRUNCATED:    return "Descarga incompleta";
    case OTA_ERR_CORRUPT:      return "Firmware corrupto";
    case OTA_ERR_CANCELLED:    return "Actualizacion cancelada";
    default:                   return "Error desconocido";
  }
}

// Frase de la pantalla de instalacion, por estado.
static const char* otaPhaseText(FlexOtaState s){
  switch(s){
    case OTA_CONNECTING:  return "Conectando...";
    case OTA_DOWNLOADING: return "Descargando...";
    case OTA_VERIFYING:   return "Verificando...";
    case OTA_INSTALLING:  return "Instalando...";
    case OTA_FINALIZING:  return "Finalizando...";
    case OTA_REBOOTING:   return "Reiniciando...";
    default:              return "Preparando...";
  }
}

const char* flexOtaStatusText(){
  static char buf[48];
  switch(gState){
    case OTA_CHECKING:   return "Buscando actualizaciones...";
    case OTA_UP_TO_DATE: return "El sistema esta al dia";
    case OTA_AVAILABLE:  snprintf(buf, sizeof(buf), "%s disponible", gRemoteVer); return buf;
    case OTA_ERROR:      return otaErrText(gError);
    case OTA_IDLE:       return "Buscar actualizacion";
    default:             return otaPhaseText(gState);
  }
}

// =============================================================
//  CAPA 1 · NOTIFICACION FLOTANTE
//  -------------------------------------------------------------
//  Se coloca ABAJO a proposito: la isla dinamica del sistema ocupa
//  la banda superior (NOTIF_BAND_TOP..NOTIF_BAND_BOT). Asi las dos
//  pueden convivir en pantalla sin pelearse por las mismas filas.
// =============================================================
static void otaToastBand(int& y0, int& y1){
  int H = otaHostScrH();
  y0 = H - OTA_TOAST_GAP - OTA_TOAST_H - 14;
  y1 = H - 1;
  if(y0 < 0) y0 = 0;
}

static void otaDrawToast(){
  int W = otaHostScrW(), H = otaHostScrH();
  int x = OTA_M, w = W - 2 * OTA_M;
  int yEnd = H - OTA_TOAST_GAP - OTA_TOAST_H;

  float p;
  if(gOvClosing) p = 1.0f - (float)(millis() - gOvCloseT0) / (float)OTA_ANIM_OUT;
  else           p = (float)(millis() - gOvT0) / (float)OTA_ANIM_IN;
  if(p < 0) p = 0;
  if(p > 1) p = 1;
  float e = otaEaseOut(p);
  int y = yEnd + (int)((1.0f - e) * (OTA_TOAST_H + OTA_TOAST_GAP));   // entra deslizando desde abajo

  int b0, b1; otaToastBand(b0, b1);
  otaHostBeginCompose();
  otaHostClip(b0, b1);
  otaHostRestoreBand(b0, b1);

  otaShadow(x, y, w, OTA_TOAST_H, OTA_RAD);
  // Liquid Glass si el usuario lo tiene activo; si lo apago en Ajustes,
  // tarjeta plana. La capa OTA respeta el ajuste del sistema como
  // cualquier otra superficie de FlexOS.
  if(otaHostGlass()) otaHostGlassPanel(x, y, w, OTA_TOAST_H, OTA_RAD, cGlass());
  else               otaHostFillRoundRect(x, y, w, OTA_TOAST_H, OTA_RAD, cCard());
  otaHostDrawRoundRect(x, y, w, OTA_TOAST_H, OTA_RAD, otaHostDark() ? otaHostRGB(96,104,128) : otaHostRGB(206,214,228));

  int is = OTA_ICON;
  otaGlyphUpdate(x + 18, y + (OTA_TOAST_H - is) / 2, is, cAccent(), otaHostRGB(255,255,255));

  int tx = x + 18 + is + 16;
  otaHostTextClip(tx, y + 20, "Nueva actualizacion disponible", OTA_SZ_BODY, cTxtHi(), x + w - 20);
  char l2[56];
  snprintf(l2, sizeof(l2), "%s %s", FLEXOS_OTA_PLAT_NAME, gRemoteVer);
  otaHostTextClip(tx, y + 20 + OTA_SZ_BODY * 10 + 6, l2, OTA_SZ_SMALL, cTxtLo(), x + w - 20);

  btnReset();
  btnAdd(BT_TOAST, x, y, w, OTA_TOAST_H);

  otaHostClip(0, H - 1);
  otaHostEndCompose(b0, b1);
}

// =============================================================
//  CAPA 2 · MODAL DE CHANGELOG
// =============================================================
static void otaDrawModal(){
  int W = otaHostScrW(), H = otaHostScrH();
  int cw = W - 2 * OTA_M * 2;               // tarjeta algo mas estrecha que el toast
  if(cw < 260) cw = W - 2 * OTA_M;
  int cx = (W - cw) / 2;
  int ch = OTA_MODAL_H; if(ch > H - 80) ch = H - 80;
  int cy = (H - ch) / 2;

  float p = (float)(millis() - gOvT0) / (float)OTA_ANIM_IN;
  if(p < 0) p = 0;
  if(p > 1) p = 1;
  float e = otaEaseOut(p);
  int off = (int)((1.0f - e) * 40);         // sube 40 px al aparecer
  cy += off;

  otaHostBeginCompose();

  // VELO OPACO, no un oscurecido del contenido de debajo. Es deliberado:
  // en Flex OS PRO bbuf ES fb (no hay RAM para un back buffer), asi que
  // un fillRectA translucido se acumularia cuadro a cuadro hasta dejar la
  // pantalla negra. Pintar un fondo propio y opaco es idempotente -- el
  // mismo cuadro dibujado dos veces da el mismo resultado -- y ademas
  // cuesta menos. El escalon de tono da profundidad sin translucidez.
  uint16_t scrim = otaHostDark() ? otaHostRGB(8,10,16) : otaHostRGB(228,232,240);
  otaHostFillRect(0, 0, W, H, scrim);

  otaShadow(cx, cy, cw, ch, OTA_RAD);
  otaHostFillRoundRect(cx, cy, cw, ch, OTA_RAD, cCard());

  int y = cy + 26;
  int is = OTA_ICON + 8;
  otaGlyphUpdate(cx + (cw - is) / 2, y, is, cAccent(), otaHostRGB(255,255,255));
  y += is + 16;

  otaHostTextC(cx + cw / 2, y, "Actualizacion de software", OTA_SZ_HEAD, cTxtHi());
  y += OTA_SZ_HEAD * 10 + 14;

  // Version actual -> nueva
  char row[72];
  snprintf(row, sizeof(row), "%s  %s", FLEXOS_OTA_PLAT_NAME, gRemoteVer);
  otaHostTextC(cx + cw / 2, y, row, OTA_SZ_BODY, cAccent());
  y += OTA_SZ_BODY * 10 + 8;
  snprintf(row, sizeof(row), "Version actual: %s", FLEXOS_FW_VERSION);
  otaHostTextC(cx + cw / 2, y, row, OTA_SZ_SMALL, cTxtLo());
  y += OTA_SZ_SMALL * 10 + 6;

  if(gTotal > 0){
    char sz[24]; otaFmtBytes(gTotal, sz, sizeof(sz));
    snprintf(row, sizeof(row), "Tamano de la descarga: %s", sz);
    otaHostTextC(cx + cw / 2, y, row, OTA_SZ_SMALL, cTxtLo());
  }
  y += OTA_SZ_SMALL * 10 + 12;

  otaHostFillRect(cx + 20, y, cw - 40, 1, cLine());
  y += 14;

  // --- Changelog desplazable -------------------------------------
  int btnY = cy + ch - OTA_BTN_H - 20;
  int logTop = y, logBot = btnY - 14;
  int logH   = logBot - logTop;
  int lines  = logH / OTA_LOG_LH; if(lines < 1) lines = 1;

  otaHostClip(logTop, logBot);
  gLogLines = otaWrap(gChangelog[0] ? gChangelog : "Sin notas de la version.",
                      cx + 20, logTop, cw - 40, OTA_SZ_LOG, cTxtLo(),
                      OTA_LOG_LH, gLogScroll, lines);
  otaHostClip(0, H - 1);

  // Indicador de scroll (solo si hay mas texto del que cabe)
  if(gLogLines > lines){
    int trH = logH - 8;
    int knob = trH * lines / gLogLines; if(knob < 18) knob = 18;
    int maxS = gLogLines - lines;
    int kY = logTop + 4 + (maxS > 0 ? (trH - knob) * gLogScroll / maxS : 0);
    otaHostFillRoundRect(cx + cw - 12, logTop + 4, 3, trH, 2, cLine());
    otaHostFillRoundRect(cx + cw - 12, kY, 3, knob, 2, cTxtLo());
  }

  // --- Botones ----------------------------------------------------
  btnReset();
  int bw = (cw - 20 * 2 - 12) / 2;
  otaButton(BT_CANCEL,  cx + 20,           btnY, bw, OTA_BTN_H, "Cancelar", false);
  otaButton(BT_INSTALL, cx + 20 + bw + 12, btnY, bw, OTA_BTN_H, "Actualizar ahora", true);

  otaHostEndCompose(0, H - 1);
}

// =============================================================
//  CAPA 3 · PANTALLA DE INSTALACION
// =============================================================
// Extremos de la BANDA DINAMICA (porcentaje, barra y cifras): lo unico
// que cambia mientras baja el firmware.
static int gProgDynTop = 0, gProgDynBot = 0;

// full = true  -> pinta la pantalla entera y publica 0..H-1
// full = false -> repinta SOLO la banda dinamica y publica esa franja
//
// POR QUE IMPORTA: la version anterior hacia siempre lo primero, asi que
// cada 1% copiaba el framebuffer completo (480x800 = 768 KB) y lanzaba
// una subida DMA de pantalla entera al MIPI-DSI. Eso, mas de 100 veces
// durante una descarga y compitiendo con el trafico SDIO del C6 y con
// las escrituras en flash, es lo que llenaba la ventana de riesgo del
// destello cian. Ahora un tick de progreso mueve ~190 filas en vez de
// 800: cerca de un 76% menos de trafico hacia el panel.
//
// El aspecto NO cambia: mismas coordenadas, mismos textos, mismo estilo.
static void otaDrawProgress(bool full){
  int W = otaHostScrW(), H = otaHostScrH();
  OtaProgSnap p; otaProgSnapshot(&p);          // lectura coherente del progreso
  bool bad = (p.st == OTA_ERROR);
  if(bad) full = true;                          // el error tiene otra composicion

  // --- Geometria, calculada igual en las dos rutas ---
  int yTop = H / 2 - (OTA_MODAL_H / 2);
  if(yTop < 60) yTop = 60;
  int is    = OTA_ICON + 20;
  int yHead = yTop + is + 24;
  int yVer  = yHead + OTA_SZ_HEAD * 10 + 10;
  int yDyn  = yVer + OTA_SZ_SMALL * 10 + 30;   // aqui empieza lo que cambia
  int dynH  = OTA_SZ_BIG * 10 + 22 + 10 + 26
            + OTA_SZ_BODY * 10 + 12
            + (OTA_SZ_SMALL * 10 + 6) * 2 + 8;
  if(yDyn + dynH > H - 1) dynH = H - 1 - yDyn;
  gProgDynTop = yDyn; gProgDynBot = yDyn + dynH - 1;

  char l[72];
  otaHostBeginCompose();

  if(full){
    otaHostFillRect(0, 0, W, H, cBg());
    if(bad) otaGlyphErr((W - is) / 2, yTop, is, cErr(), otaHostRGB(255,255,255));
    else    otaGlyphUpdate((W - is) / 2, yTop, is, cAccent(), otaHostRGB(255,255,255));
    otaHostTextC(W / 2, yHead, bad ? "No se pudo actualizar" : "Actualizando FlexOS",
                 OTA_SZ_HEAD, cTxtHi());
    snprintf(l, sizeof(l), "%s %s", FLEXOS_OTA_PLAT_NAME, gRemoteVer);
    otaHostTextC(W / 2, yVer, l, OTA_SZ_SMALL, cTxtLo());
  } else {
    // Solo la franja que cambia. El recorte impide que un texto largo se
    // salga de la banda publicada y deje restos fuera de ella.
    otaHostClip(yDyn, yDyn + dynH - 1);
    otaHostFillRect(0, yDyn, W, dynH, cBg());
  }

  // --- Rama de error (siempre pintado completo) ---
  if(bad){
    int y = yDyn;
    otaHostTextC(W / 2, y, otaErrText(gError), OTA_SZ_BODY, cErr());
    y += OTA_SZ_BODY * 10 + 16;
    otaHostTextC(W / 2, y, "El equipo no se ha modificado.", OTA_SZ_SMALL, cTxtLo());
    y += OTA_SZ_SMALL * 10 + 34;
    btnReset();
    int bw = W - 2 * (OTA_M + 40);
    otaButton(BT_CLOSE, (W - bw) / 2, y, bw, OTA_BTN_H, "Entendido", true);
    otaHostClip(0, H - 1);
    otaHostEndCompose(0, H - 1);
    return;
  }

  // --- Banda dinamica ---
  int y = yDyn;
  snprintf(l, sizeof(l), "%u%%", (unsigned)p.pct);          // porcentaje gigante
  otaHostTextC(W / 2, y, l, OTA_SZ_BIG, cTxtHi());
  y += OTA_SZ_BIG * 10 + 22;

  otaProgressBar(OTA_M + 24, y, W - 2 * (OTA_M + 24), 10, p.pct);   // barra
  y += 26;

  otaHostTextC(W / 2, y, otaPhaseText(p.st), OTA_SZ_BODY, cAccent());
  y += OTA_SZ_BODY * 10 + 12;

  if(p.total > 0){
    char a[24], b[24];
    otaFmtBytes(p.done,  a, sizeof(a));
    otaFmtBytes(p.total, b, sizeof(b));
    snprintf(l, sizeof(l), "%s / %s", a, b);
    otaHostTextC(W / 2, y, l, OTA_SZ_SMALL, cTxtLo());
    y += OTA_SZ_SMALL * 10 + 6;
  }
  if(p.bps > 0 && p.st == OTA_DOWNLOADING){
    char v[24]; otaFmtBytes(p.bps, v, sizeof(v));
    snprintf(l, sizeof(l), "%s/s", v);
    otaHostTextC(W / 2, y, l, OTA_SZ_SMALL, cTxtLo());
  }

  // --- Pie: solo en pintado completo ---
  //  btnReset()/otaButton NO pueden ejecutarse en el repintado parcial:
  //  vaciarian el registro de zonas pulsables en cada 1% y "Cancelar"
  //  dejaria de responder entre tick y tick.
  if(full){
    int yBtn = yDyn + dynH + 16;
    btnReset();
    // Cancelar SOLO mientras se puede abortar sin dejar nada a medias.
    // A partir de OTA_INSTALLING ya no se ofrece: la imagen se esta
    // cerrando y cortar ahi no aportaria nada bueno.
    if(p.st == OTA_CONNECTING || p.st == OTA_DOWNLOADING){
      int bw = W - 2 * (OTA_M + 40);
      otaButton(BT_CANCEL, (W - bw) / 2, yBtn, bw, OTA_BTN_H, "Cancelar", false);
    } else if(p.st == OTA_REBOOTING){
      otaHostTextC(W / 2, yBtn + 8, "El equipo se reiniciara solo", OTA_SZ_SMALL, cTxtLo());
    }
    otaHostEndCompose(0, H - 1);
  } else {
    otaHostClip(0, H - 1);
    otaHostEndCompose(gProgDynTop, gProgDynBot);   // solo la franja
  }
}

// =============================================================
//  CAPA 4 · AJUSTES -> SISTEMA -> ACTUALIZACIONES
// =============================================================
static void otaDrawSettings(){
  int W = otaHostScrW(), H = otaHostScrH();
  FlexOtaState s = gState;

  otaHostBeginCompose();
  otaHostFillRect(0, 0, W, H, cBg());

  // Cabecera con flecha atras
  otaHostStroke(34, 42, 22, 32, 2.4f, cTxtHi());
  otaHostStroke(22, 32, 34, 22, 2.4f, cTxtHi());
  btnReset();
  btnAdd(BT_BACK, 4, 6, 60, 56);
  otaHostTextC(W / 2, 26, "Actualizacion de software", OTA_SZ_BODY, cTxtHi());

  int y = 92;

  // --- Tarjeta principal ------------------------------------------
  int cardH = OTA_ICON + 116;
  otaHostFillRoundRect(OTA_M, y, W - 2 * OTA_M, cardH, OTA_RAD, cCard());

  int is = OTA_ICON + 12;
  bool upd = (s == OTA_AVAILABLE) || gHasUpd;
  if(s == OTA_ERROR)      otaGlyphErr((W - is) / 2, y + 20, is, cErr(), otaHostRGB(255,255,255));
  else if(upd)            otaGlyphUpdate((W - is) / 2, y + 20, is, cAccent(), otaHostRGB(255,255,255));
  else                    otaGlyphOk((W - is) / 2, y + 20, is, cOk(), otaHostRGB(255,255,255));

  int ty = y + 20 + is + 16;
  const char* head = upd ? "Actualizacion disponible"
                         : (s == OTA_ERROR ? "No se pudo comprobar"
                                           : (s == OTA_CHECKING ? "Buscando..." : "El sistema esta al dia"));
  otaHostTextC(W / 2, ty, head, OTA_SZ_BODY, cTxtHi());
  ty += OTA_SZ_BODY * 10 + 10;

  char l[72];
  snprintf(l, sizeof(l), "%s %s", FLEXOS_OTA_PLAT_NAME, FLEXOS_FW_VERSION);
  otaHostTextC(W / 2, ty, l, OTA_SZ_SMALL, cTxtLo());

  y += cardH + 14;

  // --- Detalle de la version nueva --------------------------------
  if(upd && gRemoteVer[0]){
    int dh = 78;
    otaHostFillRoundRect(OTA_M, y, W - 2 * OTA_M, dh, OTA_RAD, cCard());
    otaHostText(OTA_M + 20, y + 16, "Version disponible", OTA_SZ_SMALL, cTxtLo());
    otaHostTextR(W - OTA_M - 20, y + 16, gRemoteVer, OTA_SZ_SMALL, cAccent());
    if(gTotal > 0){
      char sz[24]; otaFmtBytes(gTotal, sz, sizeof(sz));
      otaHostText(OTA_M + 20, y + 44, "Tamano", OTA_SZ_SMALL, cTxtLo());
      otaHostTextR(W - OTA_M - 20, y + 44, sz, OTA_SZ_SMALL, cTxtHi());
    }
    y += dh + 14;
  } else if(s == OTA_ERROR){
    int dh = 56;
    otaHostFillRoundRect(OTA_M, y, W - 2 * OTA_M, dh, OTA_RAD, cCard());
    otaHostTextC(W / 2, y + 20, otaErrText(gError), OTA_SZ_SMALL, cErr());
    y += dh + 14;
  }

  // --- Changelog ---------------------------------------------------
  int btnY = H - OTA_BTN_H - 40;
  if(upd && gChangelog[0]){
    int logTop = y + 30, logBot = btnY - 18;
    if(logBot > logTop + OTA_LOG_LH){
      otaHostFillRoundRect(OTA_M, y, W - 2 * OTA_M, logBot - y + 8, OTA_RAD, cCard());
      otaHostText(OTA_M + 20, y + 14, "Novedades", OTA_SZ_SMALL, cTxtHi());
      int lines = (logBot - logTop) / OTA_LOG_LH; if(lines < 1) lines = 1;
      otaHostClip(logTop, logBot);
      gLogLines = otaWrap(gChangelog, OTA_M + 20, logTop, W - 2 * OTA_M - 40,
                          OTA_SZ_LOG, cTxtLo(), OTA_LOG_LH, gLogScroll, lines);
      otaHostClip(0, H - 1);
    }
  }

  // --- Botones ------------------------------------------------------
  bool busy = flexOtaBusy();
  if(upd && !busy){
    int bw = (W - 2 * OTA_M - 12) / 2;
    otaButton(BT_CHECK,   OTA_M,            btnY, bw, OTA_BTN_H, "Buscar", false);
    otaButton(BT_INSTALL, OTA_M + bw + 12,  btnY, bw, OTA_BTN_H, "Actualizar ahora", true);
  } else {
    int bw = W - 2 * OTA_M;
    if(busy) otaHostTextC(W / 2, btnY + (OTA_BTN_H - OTA_SZ_BODY * 8) / 2,
                          flexOtaStatusText(), OTA_SZ_BODY, cTxtLo());
    else     otaButton(BT_CHECK, OTA_M, btnY, bw, OTA_BTN_H, "Buscar actualizacion", true);
  }

  otaHostEndCompose(0, H - 1);
}

// =============================================================
//  CONTROL DE CAPAS
// =============================================================
static void otaShow(OverlayState s){
  gOv = s; gOvT0 = millis(); gOvClosing = false;
  gLogScroll = 0; gLogDrag = false;
  gOvDirty = true;
  gShownPct = 255; gShownState = OTA_IDLE;
  btnReset();
}

void flexOtaCloseOverlay(){
  if(gOv == OVERLAY_HIDDEN) return;
  bool full = (gOv != OVERLAY_NOTIFICATION);
  gOv = OVERLAY_HIDDEN; gOvClosing = false; btnReset();
  if(full) otaHostRepaint();      // devuelve la pantalla que habia debajo
  else     gNeedClear = true;     // un ultimo frame que limpia la banda del toast
}

// =============================================================
//  API PUBLICA
// =============================================================
void flexOtaBegin(){
  // Saludo posterior a una actualizacion correcta: si la version que
  // dejamos apuntada coincide con la que ahora corre, la OTA funciono.
  gPrefs.begin("flexota", false);
  String pend = gPrefs.getString("pend", "");
  if(pend.length()){
    if(pend == FLEXOS_FW_VERSION) gJustUpdated = true;
    gPrefs.remove("pend");
  }
  gPrefs.end();

  if(!gTask){
    // Prioridad 1 = baja (la del loopTask de Arduino). Core 1, igual que
    // wifiScanTask/wifiConnTask: el core 0 es del presenter grafico.
    xTaskCreatePinnedToCore(flexOtaTask, "flexOTA", FLEXOS_OTA_TASK_STACK, NULL, 1, &gTask, 1);
  }
  Serial.printf("[OTA] %s v%s · clave de manifiesto '%s'\n",
                FLEXOS_OTA_PLAT_NAME, FLEXOS_FW_VERSION, FLEXOS_OTA_KEY);
}

void flexOtaRequestCheck(){
  if(flexOtaBusy()) return;
  if(WiFi.status() != WL_CONNECTED){ gError = OTA_ERR_NO_WIFI; gState = OTA_ERROR; gOvDirty = true; return; }
  gError = OTA_ERR_NONE;
  gState = OTA_CHECKING;
  gCmd   = CMD_CHECK;
  gOvDirty = true;
}

void flexOtaRequestInstall(){
  if(flexOtaBusy() || !gHasUpd) return;
  if(WiFi.status() != WL_CONNECTED){ gError = OTA_ERR_NO_WIFI; gState = OTA_ERROR; gOvDirty = true; return; }
  gCancel = false;
  gState  = OTA_CONNECTING;
  gCmd    = CMD_INSTALL;
  otaShow(OVERLAY_DOWNLOADING);
}

void flexOtaCancel(){
  gCancel = true;                       // la tarea lo ve en su siguiente vuelta
}

void flexOtaOpenSettings(){ otaShow(OVERLAY_SETTINGS); }

FlexOtaState flexOtaState()        { return gState; }
FlexOtaError flexOtaError()        { return gError; }
OverlayState flexOtaOverlay()      { return gOv; }
bool         flexOtaOverlayActive(){ return gOv != OVERLAY_HIDDEN; }
const char*  flexOtaLocalVersion() { return FLEXOS_FW_VERSION; }
const char*  flexOtaRemoteVersion(){ return gRemoteVer; }
const char*  flexOtaChangelog()    { return gChangelog; }
uint32_t     flexOtaSizeBytes()    { return gTotal; }
uint8_t      flexOtaPercent()      { return gPercent; }

bool flexOtaBusy(){
  FlexOtaState s = gState;
  return s == OTA_CHECKING || s == OTA_CONNECTING || s == OTA_DOWNLOADING ||
         s == OTA_VERIFYING || s == OTA_INSTALLING || s == OTA_FINALIZING || s == OTA_REBOOTING;
}

// La tarjeta flotante NO cuenta: es pequeña, vive sobre el escritorio y
// necesita que el escritorio se siga dibujando debajo. Las otras tres
// capas ocupan la pantalla entera y deben tenerla en exclusiva.
bool flexOtaOwnsScreen(){
  return gOv == OVERLAY_CHANGELOG || gOv == OVERLAY_DOWNLOADING || gOv == OVERLAY_SETTINGS;
}

// =============================================================
//  TACTIL  (se llama TEMPRANO en loop, junto a notifHandleTouch)
//  -------------------------------------------------------------
//  Con una capa OTA a pantalla completa, el toque se CONSUME entero:
//  se limpian los flags en *t y el puente los devuelve a T, asi que
//  la app de debajo no llega a verlos. Es el mismo patron de
//  interceptacion que ya usa la isla de notificaciones.
// =============================================================
static void otaEat(FlexOtaTouch* t){
  t->tap = false; t->pressed = false; t->released = false;
  t->swipeUp = false; t->swipeDown = false; t->swipeLeft = false; t->swipeRight = false;
}

bool flexOtaHandleTouch(FlexOtaTouch* t){
  if(!t || gOv == OVERLAY_HIDDEN) return false;
  bool full = (gOv != OVERLAY_NOTIFICATION);

  // --- Scroll del changelog (modal y ajustes) ---
  if(gOv == OVERLAY_CHANGELOG || gOv == OVERLAY_SETTINGS){
    if(t->down && !gLogDrag && gLogLines > 0){ gLogDrag = true; gLogDragY0 = t->y; gLogScroll0 = gLogScroll; }
    if(gLogDrag){
      if(t->down){
        int d = (gLogDragY0 - t->y) / OTA_LOG_LH;
        int ns = gLogScroll0 + d;
        if(ns < 0) ns = 0;
        if(ns > gLogLines - 1) ns = gLogLines - 1;
        if(ns != gLogScroll){ gLogScroll = ns; gOvDirty = true; }
        if(abs(t->y - gLogDragY0) > 8) t->tap = false;    // arrastre, no pulsacion
      } else {
        gLogDrag = false;
      }
    }
  }

  if(t->tap){
    uint8_t b = btnHit(t->x, t->y);
    switch(b){
      case BT_TOAST:                       // pulsar la notificacion abre el modal
        otaShow(OVERLAY_CHANGELOG);
        otaEat(t);
        return true;
      case BT_INSTALL:
        flexOtaRequestInstall();
        otaEat(t);
        return true;
      case BT_CHECK:
        flexOtaRequestCheck();
        gOvDirty = true;
        otaEat(t);
        return true;
      case BT_CANCEL:
        if(gOv == OVERLAY_DOWNLOADING) flexOtaCancel();
        else                           flexOtaCloseOverlay();
        gOvDirty = true;
        otaEat(t);
        return true;
      case BT_CLOSE:
      case BT_BACK:
        flexOtaCloseOverlay();
        otaEat(t);
        return true;
      default: break;
    }
  }

  // Gesto "atras" del sistema (deslizar hacia la derecha) en las capas de
  // las que tiene sentido salir. La pantalla de instalacion se queda
  // fuera a proposito: mientras se escribe en la particion, el usuario
  // debe ver lo que esta pasando y no perder la pantalla por un roce.
  if(t->swipeRight && (gOv == OVERLAY_CHANGELOG || gOv == OVERLAY_SETTINGS)){
    flexOtaCloseOverlay();
    otaEat(t);
    return true;
  }

  // Una capa a pantalla completa se queda TODO lo que le llegue, aunque
  // no haya caido sobre un boton: nada debe filtrarse a la app de abajo.
  if(full){ otaEat(t); return true; }

  // El toast solo se queda lo que cae dentro de su tarjeta (arriba ya se
  // ha resuelto); un deslizamiento hacia abajo sobre el lo descarta.
  if(t->swipeDown && btnHit(t->x, t->y) == BT_TOAST){
    gOvClosing = true; gOvCloseT0 = millis();
    otaEat(t);
    return true;
  }
  return false;
}

// =============================================================
//  RENDER  (se llama al FINAL de loop, despues de notifTick)
// =============================================================

// Comprobacion automatica: una sola vez, ~20 s despues del arranque y
// solo si hay red. No se reintenta sola: si falla, el usuario decide.
static void otaAutoCheckTick(){
#if FLEXOS_OTA_AUTOCHECK
  static bool done = false;
  if(done) return;
  if(millis() < 20000) return;
  done = true;
  if(WiFi.status() == WL_CONNECTED && gState == OTA_IDLE) gCmd = CMD_CHECK;
#endif
}

void flexOtaRender(){
  otaAutoCheckTick();

  FlexOtaState s = gState;

  // --- Aparicion automatica del toast al detectarse version nueva ---
  //  Solo en el escritorio: es donde la isla del sistema tambien se
  //  permite dibujar, y es la unica pantalla cuyo fondo se puede
  //  restaurar banda a banda sin pelearse con nadie.
  if(gOv == OVERLAY_HIDDEN && !gNeedClear && s == OTA_AVAILABLE && gHasUpd && otaHostIsHome()){
    static char shown[16] = "";
    if(strcmp(shown, gRemoteVer) != 0){
      strncpy(shown, gRemoteVer, sizeof(shown) - 1); shown[sizeof(shown) - 1] = 0;
      otaShow(OVERLAY_NOTIFICATION);
    }
  }

  // --- Aviso de "actualizado correctamente" tras el reinicio ---
  if(gJustUpdated && gOv == OVERLAY_HIDDEN && otaHostIsHome() && millis() > 3000){
    gJustUpdated = false;                 // se muestra una sola vez
    Serial.printf("[OTA] actualizacion completada: %s\n", FLEXOS_FW_VERSION);
  }

  // --- Un ultimo frame para limpiar la banda del toast ---
  if(gNeedClear){
    gNeedClear = false;
    int b0, b1; otaToastBand(b0, b1);
    if(otaHostIsHome()){
      otaHostBeginCompose();
      otaHostClip(b0, b1);
      otaHostRestoreBand(b0, b1);
      otaHostClip(0, otaHostScrH() - 1);
      otaHostEndCompose(b0, b1);
    } else {
      otaHostRepaint();
    }
  }

  if(gOv == OVERLAY_HIDDEN) return;

  // --- Ritmo de repintado ---
  if(millis() - gLastFrame < OTA_FRAME_MS) return;
  gLastFrame = millis();

  // El toast se cierra solo, y tambien si el usuario sale del escritorio
  // (ahi ya no podemos restaurar su banda con garantias).
  if(gOv == OVERLAY_NOTIFICATION){
    if(!otaHostIsHome()){ gOv = OVERLAY_HIDDEN; btnReset(); return; }
    if(!gOvClosing && millis() - gOvT0 > OTA_TOAST_MS){ gOvClosing = true; gOvCloseT0 = millis(); }
    if(gOvClosing && millis() - gOvCloseT0 > OTA_ANIM_OUT){ flexOtaCloseOverlay(); return; }
    otaDrawToast();
    return;
  }

  // La pantalla de instalacion sigue al estado del motor.
  if(gOv == OVERLAY_DOWNLOADING){
    uint8_t pct = gPercent;
    bool anim = (millis() - gOvT0 < OTA_ANIM_IN);
    // Pintado COMPLETO solo cuando cambia algo del marco fijo (fase de la
    // instalacion, primera aparicion). Un simple avance de porcentaje
    // repinta unicamente la banda dinamica -- que es lo que evita mandar
    // 768 KB al panel mas de cien veces durante una descarga.
    bool needFull = (gOvDirty || anim || s != gShownState);
    if(needFull || pct != gShownPct){
      gShownState = s; gShownPct = pct; gOvDirty = false;
      otaDrawProgress(needFull);
    }
    return;
  }

  if(gOv == OVERLAY_CHANGELOG){
    bool anim = (millis() - gOvT0 < OTA_ANIM_IN);
    if(gOvDirty || anim || s != gShownState){
      gShownState = s; gOvDirty = false;
      otaDrawModal();
    }
    return;
  }

  if(gOv == OVERLAY_SETTINGS){
    if(gOvDirty || s != gShownState){
      gShownState = s; gOvDirty = false;
      otaDrawSettings();
    }
    return;
  }
}

#else   // ---------- FLEXOS_OTA_ON == 0 : el modulo se apaga entero ----------

void flexOtaBegin(){}
bool flexOtaHandleTouch(FlexOtaTouch*){ return false; }
void flexOtaRender(){}
void flexOtaRequestCheck(){}
void flexOtaRequestInstall(){}
void flexOtaCancel(){}
void flexOtaOpenSettings(){}
void flexOtaCloseOverlay(){}
FlexOtaState flexOtaState()        { return OTA_IDLE; }
FlexOtaError flexOtaError()        { return OTA_ERR_NONE; }
OverlayState flexOtaOverlay()      { return OVERLAY_HIDDEN; }
bool         flexOtaOverlayActive(){ return false; }
bool         flexOtaBusy()         { return false; }
bool         flexOtaOwnsScreen()   { return false; }
const char*  flexOtaLocalVersion() { return FLEXOS_FW_VERSION; }
const char*  flexOtaRemoteVersion(){ return ""; }
const char*  flexOtaChangelog()    { return ""; }
uint32_t     flexOtaSizeBytes()    { return 0; }
uint8_t      flexOtaPercent()      { return 0; }
const char*  flexOtaStatusText()   { return "No disponible"; }

#endif  // FLEXOS_OTA_ON
