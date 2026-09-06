// #############################################################
// ##  FlexOS_Weather.h  ·  MOTOR METEOROLOGICO DE FLEX OS
// ##  ------------------------------------------------------
// ##  UNA SOLA FUENTE DE VERDAD para todo el clima del sistema:
// ##
// ##      Open-Meteo  ->  tarea de red (Core 1)  ->  parser
// ##                  ->  WeatherState (doble buffer)
// ##                  ->  App Clima / Widget Home / Bloqueo
// ##
// ##  NADIE MAS habla con Internet. La app, el widget del
// ##  escritorio y el widget de la pantalla de bloqueo LEEN el
// ##  mismo WeatherState, asi que es IMPOSIBLE que muestren
// ##  numeros distintos.
// ##
// ##  REGLAS DEL MODULO (las mismas que FlexOS_OTA)
// ##    1. NADA de red en el hilo grafico. HTTPClient y
// ##       WiFiClientSecure solo se tocan dentro de la tarea
// ##       flexWxTask, en el Core 1.
// ##    2. NADA de delay(). Solo vTaskDelay(), que cede la CPU.
// ##    3. El renderer NUNCA lee una estructura a medio escribir:
// ##       la tarea rellena el buffer INACTIVO y solo al final
// ##       publica el indice (swap atomico de una palabra).
// ##    4. Ni un solo dato meteorologico inventado. Si no hay
// ##       descarga valida, flexWeatherData() devuelve NULL y la
// ##       interfaz tiene que pintar su estado vacio.
// ##
// ##  Este fichero es una UNIDAD DE TRADUCCION APARTE (igual que
// ##  FlexOS_OTA.cpp): no puede llamar a las primitivas de dibujo
// ##  del .ino, que son static. Por eso aqui NO hay ni una linea
// ##  de interfaz: solo datos, red, parseo y persistencia. Todo
// ##  lo que se dibuja vive en el .ino, que es quien tiene el
// ##  motor grafico.
// #############################################################

#ifndef FLEXOS_WEATHER_H
#define FLEXOS_WEATHER_H

#include <stdint.h>
#include <stddef.h>

// Interruptor maestro (mismo patron que FLEXOS_OTA_ON / KIOSK_ON).
// A 0 el modulo compila a casi nada: la app muestra su estado vacio y
// no se crea ninguna tarea de red.
#ifndef FLEXWX_ON
#define FLEXWX_ON 1
#endif

// -------------------------------------------------------------
//  Tamanos FIJOS. Ni un malloc por muestra: todo el pronostico
//  vive en dos structs estaticos del tamano exacto declarado aqui.
// -------------------------------------------------------------
#define FLEXWX_HOURS      48     // horas de pronostico horario (requisito: >= 48)
#define FLEXWX_DAYS        7     // dias de pronostico diario
#define FLEXWX_LOCS        5     // ubicaciones guardadas como maximo
#define FLEXWX_RESULTS     6     // resultados de busqueda de ciudad
#define FLEXWX_NAME       36     // nombre de ciudad
#define FLEXWX_REGION     36     // "Region, Pais"
#define FLEXWX_QUERY      48     // texto de busqueda

// Presupuesto de la respuesta HTTP. Con timeformat=unixtime y solo las
// variables que se usan, una respuesta de 48 h + 7 dias ronda los 5 KB;
// 16 KB deja margen de sobra sin desperdiciar PSRAM.
#define FLEXWX_HTTP_BUF   16384
#define FLEXWX_GEO_BUF     6144

// -------------------------------------------------------------
//  TLS VERIFICADO POR DEFECTO
//  ------------------------------------------------------------
//  Misma regla que FlexOS_OTA y que el Centro de noticias de esta
//  rama: un binario publicable NO acepta cualquier certificado.
//  Aqui no hace falta configurar nada porque el servidor es fijo y
//  conocido: api.open-meteo.com y geocoding-api.open-meteo.com
//  presentan cadenas de Let's Encrypt, asi que se fijan sus DOS
//  raices (ISRG Root X1 y X2, tal cual vienen del almacen de CA de
//  Mozilla). Si algun dia Open-Meteo cambia de emisor, la descarga
//  falla con WXE_TLS y se dice en pantalla -- nunca se degrada en
//  silencio a una conexion sin verificar.
//
//    · FLEXWX_ROOT_CA         PEM(es) de la(s) raiz(ces). Vacio =
//                             sin verificacion posible.
//    · FLEXWX_ALLOW_INSECURE  0 por defecto. A 1 (y solo desde la
//                             linea de compilacion, en una placa de
//                             pruebas) se acepta cualquier
//                             certificado si no hay CA.
// -------------------------------------------------------------
#ifndef FLEXWX_ALLOW_INSECURE
#define FLEXWX_ALLOW_INSECURE 0
#endif
#ifndef FLEXWX_ROOT_CA
#define FLEXWX_ROOT_CA \
  "-----BEGIN CERTIFICATE-----\n" \
  "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\n" \
  "-----END CERTIFICATE-----\n" \
  "-----BEGIN CERTIFICATE-----\n" \
  "MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n" \
  "CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n" \
  "R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n" \
  "MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n" \
  "ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n" \
  "EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n" \
  "+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n" \
  "ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n" \
  "AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n" \
  "zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n" \
  "tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n" \
  "/q4AaOeMSQ+2b1tbFfLn\n" \
  "-----END CERTIFICATE-----\n"
#endif

#define FLEXWX_HTTP_TIMEOUT_MS   12000   // conexion + lectura
#define FLEXWX_STALL_MS           6000   // sin un solo byte nuevo -> timeout
#define FLEXWX_MIN_HEAP          56000   // TLS necesita margen: por debajo, ni se intenta

// Cadencia de refresco automatico y reintentos tras error.
#define FLEXWX_REFRESH_MS      (25UL * 60UL * 1000UL)   // ~25 min
#define FLEXWX_RETRY_MS        ( 2UL * 60UL * 1000UL)   // reintento tras fallo
#define FLEXWX_RETRY_LONG_MS   (15UL * 60UL * 1000UL)   // tras varios fallos seguidos
#define FLEXWX_RETRY_FAILS     3                        // fallos para pasar al reintento largo

// -------------------------------------------------------------
//  ESTADO VISUAL. Conversor UNICO de weather_code de Open-Meteo:
//  la app, el widget del Home y el del bloqueo llaman todos a
//  flexWeatherVisual(), asi que un mismo codigo produce SIEMPRE
//  el mismo icono y la misma escena. Cero logica duplicada.
// -------------------------------------------------------------
enum {
  WXV_CLEAR = 0,      // 0
  WXV_MOSTLY_CLEAR,   // 1
  WXV_PARTLY,         // 2
  WXV_CLOUDY,         // 3
  WXV_FOG,            // 45,48
  WXV_DRIZZLE,        // 51,53,55,56,57
  WXV_RAIN,           // 61,63,80,81
  WXV_HEAVY_RAIN,     // 65,82
  WXV_SLEET,          // 66,67
  WXV_SNOW,           // 71,73,75,77,85,86
  WXV_THUNDER,        // 95
  WXV_HAIL,           // 96,99
  WXV_COUNT
};

// Estado del motor (requisito: nunca un bool ambiguo).
enum {
  WXS_NEVER = 0,   // jamas hubo una descarga valida
  WXS_LOADING,     // primera descarga en curso
  WXS_OK,          // datos validos de esta sesion
  WXS_STALE,       // validos pero de la cache (sin conexion / sin refrescar)
  WXS_UPDATING,    // hay datos y ademas se esta refrescando
  WXS_ERROR        // sin datos validos y el ultimo intento fallo
};

// Errores de red / datos.
enum {
  WXE_NONE = 0,
  WXE_NOWIFI,      // Wi-Fi desconectado
  WXE_CONNECT,     // DNS / TCP / TLS
  WXE_TIMEOUT,     // sin respuesta a tiempo
  WXE_HTTP,        // codigo != 200
  WXE_JSON,        // respuesta incompleta o no parseable
  WXE_MEM,         // sin memoria para el buffer / heap bajo
  WXE_NOLOC,       // no hay ubicacion seleccionada
  WXE_NORESULT,    // busqueda de ciudad sin resultados
  WXE_TLS          // certificado no verificable (ver FLEXWX_ROOT_CA)
};

// Estado de la busqueda de ubicaciones (geocoding).
enum { WXQ_IDLE = 0, WXQ_BUSY, WXQ_OK, WXQ_EMPTY, WXQ_ERROR };

// -------------------------------------------------------------
//  ESTRUCTURAS. Enteros de ancho fijo donde se puede: 48 horas en
//  float por variable serian 1,5 KB solo de temperaturas. En
//  decimas de unidad (int16) el error es 0,05 grados -- invisible
//  en pantalla -- y ocupa la mitad.
// -------------------------------------------------------------
struct FlexWxHour {
  int32_t  t;          // unix UTC de la hora
  int16_t  temp10;     // temperatura       (decimas de grado)
  int16_t  feels10;    // sensacion termica (decimas de grado)
  int16_t  wind10;     // viento            (decimas de km/h)
  uint16_t vis100;     // visibilidad       (centenas de metro; 0xFFFF = no disponible)
  uint16_t precip100;  // precipitacion     (centesimas de mm)
  uint8_t  code;       // weather_code
  uint8_t  pop;        // probabilidad de precipitacion 0..100 (255 = no disponible)
  uint8_t  rh;         // humedad relativa % (255 = no disponible)
  uint8_t  uv10;       // indice UV en decimas, saturado a 25.5 (255 = no disponible)
};

struct FlexWxDay {
  int32_t date;        // unix UTC del inicio del dia local
  int32_t sunrise;     // unix UTC
  int32_t sunset;      // unix UTC
  int16_t max10;
  int16_t min10;
  uint8_t code;
  uint8_t pop;         // 255 = no disponible
  uint8_t uv10;        // 255 = no disponible
};

struct FlexWxLoc {
  char  name[FLEXWX_NAME];
  char  region[FLEXWX_REGION];
  float lat, lon;
};

// Bandera por campo: un dato que Open-Meteo devuelve como null NO se
// dibuja como 0, se dibuja como "No disponible".
#define WXF_FEELS     0x0001
#define WXF_HUMIDITY  0x0002
#define WXF_PRECIP    0x0004
#define WXF_WIND      0x0008
#define WXF_GUST      0x0010
#define WXF_PRESSURE  0x0020
#define WXF_CLOUD     0x0040
#define WXF_UV        0x0080
#define WXF_VIS       0x0100
#define WXF_POP       0x0200
#define WXF_MINMAX    0x0400
#define WXF_SUN       0x0800
#define WXF_WINDDIR   0x1000

// Sentinelas de "sin valor". Un dato que Open-Meteo devuelve como null
// NO se dibuja como 0: se dibuja como "No disponible". Viven en la
// cabecera porque la interfaz (el .ino) tambien los consulta.
#define WX_NOVAL_I16  ((int16_t)-32768)
#define WX_NOVAL_U8   ((uint8_t)255)
#define WX_NOVAL_U16  ((uint16_t)0xFFFF)

#define FLEXWX_MAGIC   0x57583031u   // 'WX01'
#define FLEXWX_VER     1

struct FlexWeather {
  uint32_t  magic;
  uint16_t  ver;
  uint16_t  have;        // mascara WXF_*

  FlexWxLoc loc;         // ubicacion a la que corresponden ESTOS datos

  int32_t   obsTime;     // current.time  (unix UTC)
  int32_t   utcOffset;   // segundos respecto a UTC de la ubicacion

  float     temp;        // C
  float     feels;       // C
  float     humidity;    // %
  float     precip;      // mm
  float     windSpeed;   // km/h
  float     windGust;    // km/h
  float     pressure;    // hPa
  float     cloud;       // %
  float     uv;          // indice
  float     visibility;  // metros
  int16_t   windDir;     // grados
  uint8_t   pop;         // % de la hora en curso
  uint8_t   code;        // weather_code actual
  uint8_t   isDay;       // 1 = de dia
  uint8_t   hourCount;
  uint8_t   dayCount;
  uint8_t   pad_;

  float     tmax, tmin;  // maxima / minima de HOY
  int32_t   sunrise, sunset;

  FlexWxHour hours[FLEXWX_HOURS];
  FlexWxDay  days[FLEXWX_DAYS];
};

#ifdef __cplusplus
extern "C" {
#endif

// ---- Ciclo de vida -------------------------------------------------
// Begin: lee ubicaciones y cache de NVS y crea la tarea de red. NO toca
// la radio (igual que bootInitRadioSafe: en setup() la radio no se toca).
void     flexWeatherBegin(void);
// Tick desde loop(). netOnline se lo pasa el .ino (gNetOnline): asi este
// modulo no tiene que sondear el driver Wi-Fi por su cuenta.
void     flexWeatherTick(bool netOnline);
// Refresco manual (pull-to-refresh). force = ignorar la cadencia minima.
void     flexWeatherRefresh(bool force);

// ---- Lectura del estado (hilo grafico) ------------------------------
// NULL mientras no exista NI UNA descarga valida (ni en cache).
const FlexWeather* flexWeatherData(void);
uint32_t flexWeatherGen(void);        // sube en cada publicacion: sirve de "dirty"
uint8_t  flexWeatherStatus(void);     // WXS_*
uint8_t  flexWeatherError(void);      // WXE_* del ultimo intento
bool     flexWeatherBusy(void);       // hay descarga en curso
// Segundos desde la ultima descarga correcta, o -1 si no se sabe (datos de
// cache cargados en el arranque: sin RTC no hay forma honesta de calcularlo).
int32_t  flexWeatherAgeSec(void);
// Hora UTC real del sistema. La pone el .ino desde clkSetEpoch() -- el
// UNICO punto por el que pasan la semilla, NVS y NTP -- igual que hace con
// flexVaultSetClock(). Con ella, "actualizado hace N min" es exacto incluso
// tras un reinicio (la cache de NVS trae su hora de observacion). Sin ella,
// el motor sigue funcionando con la hora que trae la propia respuesta.
void     flexWeatherSetClock(uint32_t utcNow);

// Hora LOCAL de la ubicacion como unix+offset, extrapolada con millis()
// desde la ultima respuesta. 0 si no hay datos. NO usa el reloj del
// sistema (que se siembra a una fecha fija y no tiene NTP).
int32_t  flexWeatherNowLocal(void);

// ---- Ubicaciones ----------------------------------------------------
uint8_t  flexWeatherLocCount(void);
const FlexWxLoc* flexWeatherLocAt(uint8_t i);
int8_t   flexWeatherLocSel(void);
void     flexWeatherLocSelect(uint8_t i);   // + descarga inmediata
bool     flexWeatherLocAdd(const FlexWxLoc* l);
void     flexWeatherLocRemove(uint8_t i);

// ---- Busqueda de ciudad (Open-Meteo Geocoding) ----------------------
void     flexWeatherSearch(const char* q, int lang);
uint8_t  flexWeatherSearchState(void);      // WXQ_*
uint8_t  flexWeatherSearchCount(void);
const FlexWxLoc* flexWeatherSearchAt(uint8_t i);
void     flexWeatherSearchClear(void);

// ---- Conversores DETERMINISTAS (sin red, sin IA) --------------------
uint8_t     flexWeatherVisual(int code);                    // weather_code -> WXV_*
const char* flexWeatherCondName(int code, int lang);        // "Parcialmente nublado"
const char* flexWeatherErrorText(uint8_t err, int lang);
// Frase compuesta a partir de datos REALES (codigo + viento + lluvia +
// minima). Ej: "Cielo parcialmente cubierto y viento. Minima de 17 C."
void        flexWeatherDescribe(char* out, size_t n, int lang);

#ifdef __cplusplus
}
#endif

#endif // FLEXOS_WEATHER_H
