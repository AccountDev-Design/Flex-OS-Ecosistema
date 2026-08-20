// #############################################################
// ##  FlexOS_Weather.cpp  ·  MOTOR METEOROLOGICO DE FLEX OS
// ##  ------------------------------------------------------
// ##  Ver la cabecera FlexOS_Weather.h para la arquitectura.
// ##  Aqui viven, y SOLO aqui:
// ##
// ##    · la tarea de red (Core 1, prioridad 1)
// ##    · el armado de URLs de Open-Meteo
// ##    · el parseo JSON (escaner propio, sin ArduinoJson)
// ##    · la persistencia en NVS (ubicaciones + cache)
// ##    · los conversores deterministas weather_code -> texto/icono
// ##
// ##  NO hay ni una linea de dibujo: este .cpp es una unidad de
// ##  traduccion aparte y las primitivas graficas del .ino son
// ##  static. La interfaz vive en FlexOS_Ultra.ino y lee de aqui.
// #############################################################

#include "FlexOS_Weather.h"

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if FLEXWX_ON
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #include <HTTPClient.h>
  #include <Preferences.h>
  #include "esp_heap_caps.h"
  #include "esp_system.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
#endif

// -------------------------------------------------------------
//  ORIGEN DE LOS DATOS. Open-Meteo, sin clave de API, sin cuenta
//  y sin coste. Se piden EXACTAMENTE las variables que se pintan
//  -- ni una mas -- para que la respuesta quepa holgada en el
//  buffer y el parseo sea corto.
// -------------------------------------------------------------
#define WX_HOST_FORECAST  "https://api.open-meteo.com/v1/forecast"
#define WX_HOST_GEO       "https://geocoding-api.open-meteo.com/v1/search"

#define WX_CUR_VARS  "temperature_2m,apparent_temperature,relative_humidity_2m,is_day," \
                     "precipitation,weather_code,cloud_cover,pressure_msl," \
                     "wind_speed_10m,wind_direction_10m,wind_gusts_10m"
#define WX_HR_VARS   "temperature_2m,apparent_temperature,relative_humidity_2m,precipitation," \
                     "precipitation_probability,weather_code,wind_speed_10m,uv_index,visibility"
#define WX_DY_VARS   "weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset," \
                     "uv_index_max,precipitation_probability_max"

#define WX_NVS_NS    "flexwx"

// #############################################################
// ##  1) CONVERSORES DETERMINISTAS  (compilan siempre, aunque
// ##     FLEXWX_ON valga 0: la interfaz los usa igual)
// #############################################################

uint8_t flexWeatherVisual(int code){
  switch(code){
    case 0:  return WXV_CLEAR;
    case 1:  return WXV_MOSTLY_CLEAR;
    case 2:  return WXV_PARTLY;
    case 3:  return WXV_CLOUDY;
    case 45: case 48: return WXV_FOG;
    case 51: case 53: case 55: return WXV_DRIZZLE;
    case 56: case 57: return WXV_SLEET;
    case 61: case 63: return WXV_RAIN;
    case 65: return WXV_HEAVY_RAIN;
    case 66: case 67: return WXV_SLEET;
    case 71: case 73: case 75: case 77: return WXV_SNOW;
    case 80: case 81: return WXV_RAIN;
    case 82: return WXV_HEAVY_RAIN;
    case 85: case 86: return WXV_SNOW;
    case 95: return WXV_THUNDER;
    case 96: case 99: return WXV_HAIL;
    default: break;
  }
  // Codigo desconocido: no se inventa nada, se trata como nublado
  // (es lo que menos afirma sobre el cielo).
  return WXV_CLOUDY;
}

// Indice de idioma. El .ino maneja 6 idiomas (ES EN FR PT IT ZH);
// ZH no tiene glifos en la fuente y en todo el sistema cae a EN.
static inline int wxLang(int lang){
  if(lang < 0 || lang > 4) return 1;   // ZH y cualquier valor raro -> EN
  return lang;
}

// Nombre corto de la condicion. UNA sola tabla para app, widget y bloqueo.
// Los acentos van escapados en UTF-8 (mismo estilo que el resto del
// proyecto) para no depender de la codificacion del editor.
const char* flexWeatherCondName(int code, int lang){
  int L = wxLang(lang);
  switch(flexWeatherVisual(code)){
    case WXV_CLEAR: {
      static const char* s[5] = { "Despejado", "Clear", "D\xC3\xA9gag\xC3\xA9", "Limpo", "Sereno" };
      return s[L]; }
    case WXV_MOSTLY_CLEAR: {
      static const char* s[5] = { "Mayormente despejado", "Mostly clear", "Plut\xC3\xB4t d\xC3\xA9gag\xC3\xA9",
                                  "Predominantemente limpo", "Prevalentemente sereno" };
      return s[L]; }
    case WXV_PARTLY: {
      static const char* s[5] = { "Parcialmente nublado", "Partly cloudy", "Partiellement nuageux",
                                  "Parcialmente nublado", "Parzialmente nuvoloso" };
      return s[L]; }
    case WXV_CLOUDY: {
      static const char* s[5] = { "Nublado", "Cloudy", "Nuageux", "Nublado", "Nuvoloso" };
      return s[L]; }
    case WXV_FOG: {
      static const char* s[5] = { "Niebla", "Fog", "Brouillard", "Nevoeiro", "Nebbia" };
      return s[L]; }
    case WXV_DRIZZLE: {
      static const char* s[5] = { "Llovizna", "Drizzle", "Bruine", "Chuvisco", "Pioviggine" };
      return s[L]; }
    case WXV_RAIN: {
      static const char* s[5] = { "Lluvia", "Rain", "Pluie", "Chuva", "Pioggia" };
      return s[L]; }
    case WXV_HEAVY_RAIN: {
      static const char* s[5] = { "Lluvia intensa", "Heavy rain", "Pluie forte", "Chuva forte", "Pioggia intensa" };
      return s[L]; }
    case WXV_SLEET: {
      static const char* s[5] = { "Aguanieve", "Sleet", "Gr\xC3\xA9sil", "Chuva gelada", "Nevischio" };
      return s[L]; }
    case WXV_SNOW: {
      static const char* s[5] = { "Nieve", "Snow", "Neige", "Neve", "Neve" };
      return s[L]; }
    case WXV_THUNDER: {
      static const char* s[5] = { "Tormenta", "Thunderstorm", "Orage", "Tempestade", "Temporale" };
      return s[L]; }
    case WXV_HAIL: {
      static const char* s[5] = { "Tormenta con granizo", "Thunderstorm with hail", "Orage et gr\xC3\xAAle",
                                  "Tempestade com granizo", "Temporale con grandine" };
      return s[L]; }
    default: break;
  }
  static const char* s[5] = { "Nublado", "Cloudy", "Nuageux", "Nublado", "Nuvoloso" };
  return s[L];
}

const char* flexWeatherErrorText(uint8_t err, int lang){
  int L = wxLang(lang);
  switch(err){
    case WXE_NOWIFI: {
      static const char* s[5] = { "Sin conexi\xC3\xB3n Wi-Fi", "No Wi-Fi connection", "Pas de Wi-Fi",
                                  "Sem liga\xC3\xA7\xC3\xA3o Wi-Fi", "Nessuna connessione Wi-Fi" };
      return s[L]; }
    case WXE_CONNECT: {
      static const char* s[5] = { "No se pudo conectar al servidor", "Could not reach the server",
                                  "Serveur injoignable", "N\xC3\xA3o foi poss\xC3\xADvel ligar", "Server irraggiungibile" };
      return s[L]; }
    case WXE_TIMEOUT: {
      static const char* s[5] = { "El servidor tard\xC3\xB3 demasiado", "The server took too long",
                                  "D\xC3\xA9lai d\xC3\xA9pass\xC3\xA9", "O servidor demorou demais", "Tempo scaduto" };
      return s[L]; }
    case WXE_HTTP: {
      static const char* s[5] = { "Respuesta no v\xC3\xA1lida del servidor", "Invalid server response",
                                  "R\xC3\xA9ponse non valide", "Resposta inv\xC3\xA1lida", "Risposta non valida" };
      return s[L]; }
    case WXE_JSON: {
      static const char* s[5] = { "Datos incompletos", "Incomplete data", "Donn\xC3\xA9" "es incompl\xC3\xA8tes",
                                  "Dados incompletos", "Dati incompleti" };
      return s[L]; }
    case WXE_MEM: {
      static const char* s[5] = { "Memoria insuficiente", "Not enough memory", "M\xC3\xA9moire insuffisante",
                                  "Mem\xC3\xB3ria insuficiente", "Memoria insufficiente" };
      return s[L]; }
    case WXE_NOLOC: {
      static const char* s[5] = { "Sin ubicaci\xC3\xB3n", "No location", "Aucun lieu", "Sem localiza\xC3\xA7\xC3\xA3o", "Nessuna posizione" };
      return s[L]; }
    case WXE_NORESULT: {
      static const char* s[5] = { "Sin resultados", "No results", "Aucun r\xC3\xA9sultat", "Sem resultados", "Nessun risultato" };
      return s[L]; }
    case WXE_TLS: {
      static const char* s[5] = { "Certificado del servidor no v\xC3\xA1lido", "Invalid server certificate",
                                  "Certificat non valide", "Certificado inv\xC3\xA1lido", "Certificato non valido" };
      return s[L]; }
    default: break;
  }
  static const char* s[5] = { "", "", "", "", "" };
  return s[L];
}

// #############################################################
// ##  2) ESTADO GLOBAL DEL MOTOR
// #############################################################

// DOBLE BUFFER. La tarea de red escribe SIEMPRE en el que no esta
// publicado y solo al final mueve el indice: el renderer nunca puede
// leer una estructura a medio escribir (requisito 18).
static FlexWeather      gWx[2];
static volatile uint8_t gWxCur      = 0;
static volatile bool    gWxHave     = false;   // hay datos validos publicados
static volatile uint32_t gWxGen     = 0;       // sube en cada publicacion
static volatile bool    gWxBusy     = false;   // descarga de pronostico en curso
static volatile uint8_t gWxErr      = WXE_NONE;
static volatile bool    gWxFresh    = false;   // los datos son de ESTA sesion (no de cache)
static volatile uint32_t gWxOkMs    = 0;       // millis() de la ultima descarga correcta
static volatile bool    gWxOkValid  = false;   // gWxOkMs tiene sentido
static volatile uint8_t gWxFails    = 0;       // fallos consecutivos
static volatile uint32_t gWxAttempts = 0;      // intentos TERMINADOS (exito o fallo)

// Ubicaciones guardadas.
static FlexWxLoc        gLocs[FLEXWX_LOCS];
static uint8_t          gLocN   = 0;
static int8_t           gLocSel = -1;

// Resultados de busqueda.
static FlexWxLoc        gRes[FLEXWX_RESULTS];
static volatile uint8_t gResN   = 0;
static volatile uint8_t gResSt  = WXQ_IDLE;

// Peticiones pendientes. Dos banderas independientes en vez de una sola
// orden: asi pedir una busqueda no descarta un refresco pendiente (ni al
// reves), que es lo que dejaba el buscador girando sin fin.
static volatile bool    gReqFetch  = false;
static volatile bool    gReqSearch = false;
static char             gQuery[FLEXWX_QUERY] = "";
static volatile int     gQueryLang = 0;
// Ubicacion de la orden de descarga: se copia ANTES de publicar la peticion,
// asi la tarea nunca lee una ubicacion a medio cambiar.
static FlexWxLoc        gCmdLoc;

static uint32_t         gNextAutoMs = 0;       // millis() del proximo intento automatico
static bool             gBeganOk    = false;
// Ancla del reloj del SISTEMA (NTP / NVS / semilla). La pasa el .ino desde
// clkSetEpoch(); mientras no llegue, el motor se apana con la hora que trae
// la propia respuesta de la API.
static volatile uint32_t gSysEpoch  = 0;
static volatile uint32_t gSysEpochMs = 0;
static volatile bool     gSysClockOk = false;

#if FLEXWX_ON
static char*            gNetBuf  = NULL;       // buffer de respuesta (PSRAM)
static size_t           gNetCap  = 0;
static TaskHandle_t     gTask    = NULL;
#endif

// #############################################################
// ##  3) ESCANER JSON  ·  minimo, sin asignaciones
// #############################################################
// No se construye ningun arbol: se busca la clave dentro de un rango
// y se lee el valor en sitio. Coste de memoria: cero.

static inline const char* jSkipWs(const char* p, const char* e){
  while(p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  return p;
}

// Salta un valor completo (objeto, array, cadena, numero o literal).
static const char* jSkipVal(const char* p, const char* e){
  p = jSkipWs(p, e);
  if(p >= e) return e;
  if(*p == '{' || *p == '['){
    char open = *p, close = (open == '{') ? '}' : ']';
    int depth = 0; bool str = false;
    while(p < e){
      char c = *p;
      if(str){
        if(c == '\\'){ p += 2; continue; }
        if(c == '"') str = false;
      } else if(c == '"') str = true;
      else if(c == open) depth++;
      else if(c == close){ depth--; if(depth == 0){ return p + 1; } }
      p++;
    }
    return e;
  }
  if(*p == '"'){
    p++;
    while(p < e){
      if(*p == '\\'){ p += 2; continue; }
      if(*p == '"') return p + 1;
      p++;
    }
    return e;
  }
  while(p < e && *p != ',' && *p != '}' && *p != ']') p++;
  return p;
}

// Busca la clave "key" en el nivel del rango [s,e) y devuelve un puntero
// al primer caracter de su VALOR. Compara la clave ENTRECOMILLADA
// completa, asi que "current" no puede casar con "current_units".
static const char* jFind(const char* s, const char* e, const char* key){
  size_t kl = strlen(key);
  if(!s || !e || e <= s) return NULL;
  for(const char* p = s; p + kl + 2 < e; p++){
    if(*p != '"') continue;
    if((size_t)(e - p) < kl + 2) break;
    if(memcmp(p + 1, key, kl) != 0) continue;
    if(p[1 + kl] != '"') continue;
    const char* q = jSkipWs(p + 2 + kl, e);
    if(q >= e || *q != ':') continue;
    return jSkipWs(q + 1, e);
  }
  return NULL;
}

// Rango [os,oe) del objeto/array asociado a "key" (sin las llaves).
static bool jBlock(const char* s, const char* e, const char* key, const char** os, const char** oe){
  const char* v = jFind(s, e, key);
  if(!v || v >= e) return false;
  if(*v != '{' && *v != '[') return false;
  const char* end = jSkipVal(v, e);
  if(end <= v + 1) return false;
  *os = v + 1;
  *oe = end - 1;
  return true;
}

// Numero simple. ok=false si el valor es null (Open-Meteo lo usa para
// "variable no disponible en esta hora") -- que NO es lo mismo que 0.
static bool jNum(const char* p, const char* e, float* out){
  if(!p || p >= e) return false;
  if(*p == 'n' || *p == 'N') return false;                 // null
  char* endp = NULL;
  float v = strtof(p, &endp);
  if(endp == p) return false;
  *out = v;
  return true;
}

static bool jInt(const char* p, const char* e, int32_t* out){
  if(!p || p >= e) return false;
  if(*p == 'n' || *p == 'N') return false;
  char* endp = NULL;
  long long v = strtoll(p, &endp, 10);
  if(endp == p) return false;
  *out = (int32_t)v;
  return true;
}

// Cadena JSON -> UTF-8 (decodifica \" \\ \/ \n \t y \uXXXX).
static bool jStr(const char* p, const char* e, char* out, size_t n){
  if(!p || p >= e || *p != '"' || n == 0) return false;
  p++;
  size_t o = 0;
  while(p < e && *p != '"'){
    uint32_t cp;
    if(*p == '\\'){
      p++;
      if(p >= e) break;
      switch(*p){
        case 'n': cp = '\n'; p++; break;
        case 't': cp = '\t'; p++; break;
        case 'r': cp = '\r'; p++; break;
        case 'b': cp = ' ';  p++; break;
        case 'f': cp = ' ';  p++; break;
        case 'u': {
          if(p + 4 >= e) return false;
          cp = 0;
          for(int i = 1; i <= 4; i++){
            char c = p[i]; cp <<= 4;
            if(c >= '0' && c <= '9') cp |= (uint32_t)(c - '0');
            else if(c >= 'a' && c <= 'f') cp |= (uint32_t)(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F') cp |= (uint32_t)(c - 'A' + 10);
            else return false;
          }
          p += 5;
        } break;
        default: cp = (uint8_t)*p; p++; break;
      }
    } else {
      cp = (uint8_t)*p; p++;
      if(cp >= 0x80){                       // ya viene en UTF-8: copiar tal cual
        if(o + 1 >= n) break;
        out[o++] = (char)cp;
        continue;
      }
    }
    if(cp < 0x80){ if(o + 1 >= n) break; out[o++] = (char)cp; }
    else if(cp < 0x800){ if(o + 2 >= n) break; out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
    else { if(o + 3 >= n) break; out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
  }
  out[o] = 0;
  return true;
}

// ---- Arrays. Se escriben DIRECTAMENTE dentro de los registros del
// pronostico (base + i*stride) con un setter minusculo por variable:
// asi no hace falta ni un array temporal de 48 floats en la pila.
typedef void (*WxSet)(void* rec, double v, bool ok);

// Numero de un array, en DOBLE precision. Es importante: los tiempos vienen
// como unix (1.75e9) y un float solo tiene 24 bits de mantisa -> 1755558600
// se guardaba como 1755558016 y las horas salian desplazadas.
static bool jNumD(const char* p, const char* e, double* out){
  if(!p || p >= e) return false;
  if(*p == 'n' || *p == 'N') return false;                 // null
  char* endp = NULL;
  double v = strtod(p, &endp);
  if(endp == p) return false;
  *out = v;
  return true;
}

static int jArrRec(const char* s, const char* e, const char* key,
                   void* base, size_t stride, int maxn, WxSet set){
  const char* as = NULL; const char* ae = NULL;
  if(!jBlock(s, e, key, &as, &ae)) return 0;
  const char* p = jSkipWs(as, ae);
  int i = 0;
  while(p < ae && i < maxn){
    double v = 0; bool ok = jNumD(p, ae, &v);
    set((uint8_t*)base + (size_t)i * stride, v, ok);
    i++;
    // avanzar a la siguiente coma
    while(p < ae && *p != ',') p++;
    if(p < ae) p++;
    p = jSkipWs(p, ae);
  }
  return i;
}

// #############################################################
// ##  4) FRASE DEL TIEMPO  (deterministica, sin IA ni servicios)
// #############################################################

void flexWeatherDescribe(char* out, size_t n, int lang){
  if(!out || n == 0) return;
  out[0] = 0;
  const FlexWeather* w = flexWeatherData();
  if(!w) return;
  int L = wxLang(lang);

  // 1) Cielo: sale del weather_code REAL.
  const char* sky;
  switch(flexWeatherVisual(w->code)){
    case WXV_CLEAR: case WXV_MOSTLY_CLEAR: {
      static const char* s[5] = { "Cielo despejado", "Clear sky", "Ciel d\xC3\xA9gag\xC3\xA9", "C\xC3\xA9u limpo", "Cielo sereno" };
      sky = s[L]; } break;
    case WXV_PARTLY: {
      static const char* s[5] = { "Cielo parcialmente cubierto", "Partly cloudy skies", "Ciel partiellement couvert",
                                  "C\xC3\xA9u parcialmente encoberto", "Cielo parzialmente coperto" };
      sky = s[L]; } break;
    case WXV_CLOUDY: {
      static const char* s[5] = { "Cielo cubierto", "Overcast skies", "Ciel couvert", "C\xC3\xA9u encoberto", "Cielo coperto" };
      sky = s[L]; } break;
    case WXV_FOG: {
      static const char* s[5] = { "Niebla", "Fog", "Brouillard", "Nevoeiro", "Nebbia" };
      sky = s[L]; } break;
    default: sky = flexWeatherCondName(w->code, lang); break;
  }

  // 2) Complemento: viento o rachas REALES (nada de adornos fijos).
  const char* extra = NULL;
  if((w->have & WXF_WIND) && w->windSpeed >= 25.0f){
    static const char* s[5] = { " y viento", " and windy", " et venteux", " e vento", " e ventoso" };
    extra = s[L];
  } else if((w->have & WXF_GUST) && w->windGust >= 45.0f){
    static const char* s[5] = { " con rachas fuertes", " with strong gusts", " avec rafales",
                                " com rajadas fortes", " con raffiche forti" };
    extra = s[L];
  }

  char tail[64]; tail[0] = 0;
  // 3) Cola: minima por la tarde/noche, maxima por la manana. La hora
  //    sale del reloj de la API (unix + offset), NO del reloj interno
  //    del sistema, que no tiene NTP.
  if(w->have & WXF_MINMAX){
    int32_t nowL = flexWeatherNowLocal();
    int hour = nowL ? (int)(((nowL % 86400) + 86400) % 86400) / 3600 : 12;
    bool evening = (hour >= 15 || hour < 6);
    int v = (int)lroundf(evening ? w->tmin : w->tmax);
    if(evening){
      static const char* s[5] = { " M\xC3\xADnima de %d C.", " Low of %d C.", " Minimale de %d C.",
                                  " M\xC3\xADnima de %d C.", " Minima di %d C." };
      snprintf(tail, sizeof(tail), s[L], v);
    } else {
      static const char* s[5] = { " M\xC3\xA1xima de %d C.", " High of %d C.", " Maximale de %d C.",
                                  " M\xC3\xA1xima de %d C.", " Massima di %d C." };
      snprintf(tail, sizeof(tail), s[L], v);
    }
  }

  // 4) Aviso de lluvia solo si la probabilidad REAL lo justifica.
  char rain[64]; rain[0] = 0;
  if((w->have & WXF_POP) && w->pop >= 40){
    static const char* s[5] = { " Probabilidad de lluvia del %d %%.", " %d %% chance of rain.",
                                " %d %% de risque de pluie.", " %d %% de probabilidade de chuva.",
                                " Probabilit\xC3\xA0 di pioggia del %d %%." };
    snprintf(rain, sizeof(rain), s[L], (int)w->pop);
  }

  snprintf(out, n, "%s%s.%s%s", sky, extra ? extra : "", tail, rain);
}

// #############################################################
// ##  5) LECTURA DEL ESTADO (la llama el hilo grafico)
// #############################################################

const FlexWeather* flexWeatherData(void){
  if(!gWxHave) return NULL;
  return &gWx[gWxCur];          // lectura de una palabra: nunca a medias
}
uint32_t flexWeatherGen(void){ return gWxGen; }

void flexWeatherSetClock(uint32_t utcNow){
  if(utcNow < 1600000000u) return;          // antes de 2020: es la semilla de fabrica, no una hora real
  gSysEpoch = utcNow; gSysEpochMs = millis(); gSysClockOk = true;
}
// Segundos UTC ahora segun el reloj del sistema, o 0 si nunca se ancló.
static uint32_t wxSysNow(){
  if(!gSysClockOk) return 0;
  return gSysEpoch + (uint32_t)((millis() - gSysEpochMs) / 1000UL);
}
bool     flexWeatherBusy(void){ return gWxBusy; }
uint8_t  flexWeatherError(void){ return gWxErr; }

uint8_t flexWeatherStatus(void){
  if(gWxBusy) return gWxHave ? WXS_UPDATING : WXS_LOADING;
  if(!gWxHave) return (gWxErr != WXE_NONE) ? WXS_ERROR : WXS_NEVER;
  if(!gWxFresh || gWxErr != WXE_NONE) return WXS_STALE;
  return WXS_OK;
}

int32_t flexWeatherAgeSec(void){
  if(!gWxHave) return -1;
  // 1) Descarga de ESTA sesion: millis() es exacto y no depende de nadie.
  if(gWxOkValid) return (int32_t)((millis() - gWxOkMs) / 1000UL);
  // 2) Cache traida de NVS en el arranque: con el reloj del sistema anclado
  //    (NTP o la hora guardada) la antiguedad REAL se puede calcular, que es
  //    justo lo que antes no se podia y obligaba a decir solo la hora de
  //    observacion.
  uint32_t now = wxSysNow();
  const FlexWeather* w = &gWx[gWxCur];
  if(now && w->obsTime > 0 && now > (uint32_t)w->obsTime)
    return (int32_t)(now - (uint32_t)w->obsTime);
  return -1;
}

int32_t flexWeatherNowLocal(void){
  if(!gWxHave) return 0;
  const FlexWeather* w = &gWx[gWxCur];
  if(w->obsTime == 0) return 0;
  // 1) Con el reloj del sistema anclado (NTP), la hora local de la ubicacion
  //    es exacta: hora UTC real + el desfase que trajo la API.
  uint32_t sys = wxSysNow();
  if(sys) return (int32_t)sys + w->utcOffset;
  // 2) Sin reloj anclado: la unica referencia honesta es la hora que trajo la
  //    API mas lo que ha corrido millis() desde entonces. Si la cache viene
  //    del arranque, se devuelve la hora de observacion tal cual -- un dato
  //    real, aunque congelado, y la interfaz lo etiqueta como tal.
  uint32_t elapsed = gWxOkValid ? ((millis() - gWxOkMs) / 1000UL) : 0;
  return w->obsTime + w->utcOffset + (int32_t)elapsed;
}

uint8_t flexWeatherLocCount(void){ return gLocN; }
const FlexWxLoc* flexWeatherLocAt(uint8_t i){ return (i < gLocN) ? &gLocs[i] : NULL; }
int8_t  flexWeatherLocSel(void){ return gLocSel; }

uint8_t flexWeatherSearchState(void){ return gResSt; }
uint8_t flexWeatherSearchCount(void){ return gResN; }
const FlexWxLoc* flexWeatherSearchAt(uint8_t i){ return (i < gResN) ? &gRes[i] : NULL; }
void flexWeatherSearchClear(void){ gResN = 0; gResSt = WXQ_IDLE; }

#if !FLEXWX_ON
// ---- Modulo desactivado: la API existe pero no hace nada ----
void flexWeatherBegin(void){}
void flexWeatherTick(bool){}
void flexWeatherRefresh(bool){}
void flexWeatherLocSelect(uint8_t){}
bool flexWeatherLocAdd(const FlexWxLoc*){ return false; }
void flexWeatherLocRemove(uint8_t){}
void flexWeatherSearch(const char*, int){}
#else

// #############################################################
// ##  6) PERSISTENCIA (NVS)
// ##  Namespace propio "flexwx": borrar los ajustes del sistema no
// ##  se lleva por delante las ubicaciones, ni al reves.
// #############################################################

static void wxSaveLocs(){
  Preferences p;
  if(!p.begin(WX_NVS_NS, false)) return;
  p.putBytes("locs", gLocs, sizeof(FlexWxLoc) * FLEXWX_LOCS);
  p.putUChar("nloc", gLocN);
  p.putChar("sel", gLocSel);
  p.end();
}

static void wxLoadLocs(){
  Preferences p;
  gLocN = 0; gLocSel = -1;
  memset(gLocs, 0, sizeof(gLocs));
  if(!p.begin(WX_NVS_NS, true)) return;
  size_t n = p.getBytes("locs", gLocs, sizeof(FlexWxLoc) * FLEXWX_LOCS);
  uint8_t cnt = p.getUChar("nloc", 0);
  int8_t  sel = p.getChar("sel", -1);
  p.end();
  if(n != sizeof(FlexWxLoc) * FLEXWX_LOCS){ memset(gLocs, 0, sizeof(gLocs)); return; }
  if(cnt > FLEXWX_LOCS) cnt = 0;
  gLocN = cnt;
  // Saneado: nombres siempre terminados y coordenadas en rango. Unas
  // prefs corruptas no pueden dejar la app apuntando a la nada.
  for(uint8_t i = 0; i < gLocN; i++){
    gLocs[i].name[FLEXWX_NAME - 1] = 0;
    gLocs[i].region[FLEXWX_REGION - 1] = 0;
    if(!(gLocs[i].lat >= -90.0f && gLocs[i].lat <= 90.0f) ||
       !(gLocs[i].lon >= -180.0f && gLocs[i].lon <= 180.0f) || gLocs[i].name[0] == 0){
      gLocN = 0; gLocSel = -1; return;
    }
  }
  if(sel >= (int8_t)gLocN) sel = gLocN ? 0 : -1;
  gLocSel = sel;
}

static void wxSaveCache(){
  const FlexWeather* w = &gWx[gWxCur];
  Preferences p;
  if(!p.begin(WX_NVS_NS, false)) return;
  p.putBytes("cache", w, sizeof(FlexWeather));
  p.end();
}

static void wxLoadCache(){
  Preferences p;
  if(!p.begin(WX_NVS_NS, true)) return;
  FlexWeather* dst = &gWx[1 - gWxCur];
  size_t n = p.getBytes("cache", dst, sizeof(FlexWeather));
  p.end();
  if(n != sizeof(FlexWeather)) return;
  if(dst->magic != FLEXWX_MAGIC || dst->ver != FLEXWX_VER) return;
  if(dst->hourCount > FLEXWX_HOURS || dst->dayCount > FLEXWX_DAYS) return;
  dst->loc.name[FLEXWX_NAME - 1] = 0;
  dst->loc.region[FLEXWX_REGION - 1] = 0;
  // Publicacion: primero el contenido, despues el indice.
  gWxCur   = 1 - gWxCur;
  gWxHave  = true;
  gWxFresh = false;              // es cache: la interfaz lo dira
  // Y NO es una descarga de esta sesion: sin esto, la antiguedad se calcularia
  // con el millis() de una descarga anterior y diria "hace 0 min" sobre unos
  // datos que pueden ser de ayer. Con la marca a false, flexWeatherAgeSec()
  // usa el reloj del sistema (NTP/NVS), que es la unica referencia valida.
  gWxOkValid = false;
  gWxGen = gWxGen + 1;
}

// #############################################################
// ##  7) SETTERS DE ARRAY (uno por variable; sin temporales)
// #############################################################

static inline int16_t wxT10(double v, bool ok){
  if(!ok) return WX_NOVAL_I16;
  double x = v * 10.0;
  if(x > 32000.0) x = 32000.0;
  if(x < -32000.0) x = -32000.0;
  return (int16_t)lround(x);
}
static inline uint8_t wxPct(double v, bool ok){
  if(!ok) return WX_NOVAL_U8;
  if(v < 0) v = 0;
  if(v > 100) v = 100;
  return (uint8_t)lround(v);
}

static void setHTime (void* r, double v, bool ok){ ((FlexWxHour*)r)->t       = ok ? (int32_t)v : 0; }
static void setHTemp (void* r, double v, bool ok){ ((FlexWxHour*)r)->temp10  = wxT10(v, ok); }
static void setHFeels(void* r, double v, bool ok){ ((FlexWxHour*)r)->feels10 = wxT10(v, ok); }
static void setHWind (void* r, double v, bool ok){ ((FlexWxHour*)r)->wind10  = ok ? (int16_t)lround(v * 10.0) : WX_NOVAL_I16; }
static void setHRh   (void* r, double v, bool ok){ ((FlexWxHour*)r)->rh      = wxPct(v, ok); }
static void setHPop  (void* r, double v, bool ok){ ((FlexWxHour*)r)->pop     = wxPct(v, ok); }
static void setHCode (void* r, double v, bool ok){ ((FlexWxHour*)r)->code    = ok ? (uint8_t)((v < 0 || v > 254) ? 3 : (int)v) : 3; }
static void setHPrec (void* r, double v, bool ok){
  if(!ok){ ((FlexWxHour*)r)->precip100 = WX_NOVAL_U16; return; }
  if(v < 0) v = 0;
  double x = v * 100.0; if(x > 65000.0) x = 65000.0;
  ((FlexWxHour*)r)->precip100 = (uint16_t)lround(x);
}
static void setHUv   (void* r, double v, bool ok){
  if(!ok){ ((FlexWxHour*)r)->uv10 = WX_NOVAL_U8; return; }
  if(v < 0) v = 0;
  double x = v * 10.0; if(x > 250.0) x = 250.0;
  ((FlexWxHour*)r)->uv10 = (uint8_t)lround(x);
}
static void setHVis  (void* r, double v, bool ok){
  if(!ok){ ((FlexWxHour*)r)->vis100 = WX_NOVAL_U16; return; }
  if(v < 0) v = 0;
  double x = v / 100.0;                       // metros -> centenas de metro
  if(x > 65000.0) x = 65000.0;
  ((FlexWxHour*)r)->vis100 = (uint16_t)lround(x);
}

static void setDDate (void* r, double v, bool ok){ ((FlexWxDay*)r)->date    = ok ? (int32_t)v : 0; }
static void setDRise (void* r, double v, bool ok){ ((FlexWxDay*)r)->sunrise = ok ? (int32_t)v : 0; }
static void setDSet  (void* r, double v, bool ok){ ((FlexWxDay*)r)->sunset  = ok ? (int32_t)v : 0; }
static void setDMax  (void* r, double v, bool ok){ ((FlexWxDay*)r)->max10   = wxT10(v, ok); }
static void setDMin  (void* r, double v, bool ok){ ((FlexWxDay*)r)->min10   = wxT10(v, ok); }
static void setDCode (void* r, double v, bool ok){ ((FlexWxDay*)r)->code    = ok ? (uint8_t)((v < 0 || v > 254) ? 3 : (int)v) : 3; }
static void setDPop  (void* r, double v, bool ok){ ((FlexWxDay*)r)->pop     = wxPct(v, ok); }
static void setDUv   (void* r, double v, bool ok){
  if(!ok){ ((FlexWxDay*)r)->uv10 = WX_NOVAL_U8; return; }
  if(v < 0) v = 0;
  double x = v * 10.0; if(x > 250.0) x = 250.0;
  ((FlexWxDay*)r)->uv10 = (uint8_t)lround(x);
}

// #############################################################
// ##  8) RED  ·  SOLO desde la tarea flexWx (Core 1)
// #############################################################

static bool wxFail(uint8_t err){
  gWxErr = err;
  if(gWxFails < 250) gWxFails = (uint8_t)(gWxFails + 1);
  return false;
}

// Reserva el buffer de respuesta UNA vez. Preferencia PSRAM; si no hay,
// heap interno; si tampoco, error claro (nunca un cuelgue silencioso).
static bool wxEnsureBuf(size_t need){
  if(gNetBuf && gNetCap >= need) return true;
  if(gNetBuf){ heap_caps_free(gNetBuf); gNetBuf = NULL; gNetCap = 0; }
  char* b = (char*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!b) b = (char*)malloc(need);
  if(!b) return false;
  gNetBuf = b; gNetCap = need;
  return true;
}

// Descarga una URL entera al buffer. Devuelve la longitud o 0 y deja el
// error en gWxErr. Es la UNICA funcion que habla con la red.
static size_t wxHttpGet(const char* url, size_t cap){
  if(WiFi.status() != WL_CONNECTED){ wxFail(WXE_NOWIFI); return 0; }
  if(esp_get_free_heap_size() < FLEXWX_MIN_HEAP){ wxFail(WXE_MEM); return 0; }
  if(!wxEnsureBuf(cap)){ wxFail(WXE_MEM); return 0; }

  WiFiClientSecure sec;
  HTTPClient http;
  // TLS VERIFICADO. Que el dato sea publico no quiere decir que valga
  // cualquiera: sin verificar, quien controle la red decide que tiempo hace
  // -- y con ello la escena, los avisos de lluvia y la ubicacion que se
  // ensena en el bloqueo. Misma regla que el OTA y las noticias de esta
  // version: sin CA no se conecta, salvo permiso explicito de compilacion.
  {
    const char* ca = FLEXWX_ROOT_CA;
    bool verified = (ca && ca[0]);
    if(verified) sec.setCACert(ca);
    else if(FLEXWX_ALLOW_INSECURE) sec.setInsecure();     // solo placa de pruebas
    else { wxFail(WXE_TLS); return 0; }
  }
  sec.setTimeout(FLEXWX_HTTP_TIMEOUT_MS / 1000);
  http.setTimeout(FLEXWX_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(FLEXWX_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.useHTTP10(true);                 // sin chunked

  if(!http.begin(sec, url)){ wxFail(WXE_CONNECT); return 0; }
  int code = http.GET();
  if(code < 0){
    http.end();
    // Con CA fijada, un fallo de transporte es casi siempre el handshake:
    // se distingue del "no hay ruta" para que el mensaje sea util.
    if(code == HTTPC_ERROR_READ_TIMEOUT || code == HTTPC_ERROR_CONNECTION_LOST) wxFail(WXE_TIMEOUT);
    else if(FLEXWX_ROOT_CA[0])                                                  wxFail(WXE_TLS);
    else                                                                        wxFail(WXE_CONNECT);
    return 0;
  }
  if(code != HTTP_CODE_OK){ http.end(); wxFail(WXE_HTTP); return 0; }

  WiFiClient* st = http.getStreamPtr();
  if(!st){ http.end(); wxFail(WXE_CONNECT); return 0; }

  size_t n = 0; uint32_t last = millis();
  while(n < cap - 1){
    if(st->available()){
      int r = st->readBytes((uint8_t*)gNetBuf + n, cap - 1 - n);
      if(r > 0){ n += (size_t)r; last = millis(); continue; }
    }
    if(!st->connected() && !st->available()) break;        // fin normal
    if(millis() - last > FLEXWX_STALL_MS){ http.end(); wxFail(WXE_TIMEOUT); return 0; }
    vTaskDelay(pdMS_TO_TICKS(5));                          // cede CPU: la UI sigue viva
  }
  gNetBuf[n] = 0;
  http.end();
  if(n == 0){ wxFail(WXE_JSON); return 0; }
  return n;
}

// ---- Parseo del pronostico -> buffer INACTIVO -------------------------
static bool wxParseForecast(const char* s, size_t len, const FlexWxLoc* loc){
  const char* e = s + len;
  // Integridad barata: una respuesta completa termina en '}'. Si la conexion
  // se corta a mitad, esto lo caza antes de publicar un pronostico cojo (3
  // horas en vez de 48) como si fuera bueno.
  {
    const char* z = e;
    while(z > s && (z[-1] == '\n' || z[-1] == '\r' || z[-1] == ' ' || z[-1] == '\t')) z--;
    if(z == s || z[-1] != '}') return wxFail(WXE_JSON);
  }
  FlexWeather* w = &gWx[1 - gWxCur];       // SIEMPRE el que no esta publicado
  memset(w, 0, sizeof(FlexWeather));
  w->magic = FLEXWX_MAGIC;
  w->ver   = FLEXWX_VER;
  w->loc   = *loc;

  float f; int32_t iv;
  const char* p = jFind(s, e, "utc_offset_seconds");
  if(p && jInt(p, e, &iv)) w->utcOffset = iv;

  // ---- current ----
  const char *cs = NULL, *ce = NULL;
  if(!jBlock(s, e, "current", &cs, &ce)) return wxFail(WXE_JSON);
  p = jFind(cs, ce, "time");
  if(!p || !jInt(p, ce, &w->obsTime)) return wxFail(WXE_JSON);
  p = jFind(cs, ce, "temperature_2m");
  if(!p || !jNum(p, ce, &w->temp)) return wxFail(WXE_JSON);     // sin temperatura no hay nada que mostrar
  p = jFind(cs, ce, "weather_code");
  if(p && jNum(p, ce, &f)) w->code = (uint8_t)((f < 0 || f > 254) ? 3 : (int)f); else w->code = 3;
  p = jFind(cs, ce, "is_day");
  w->isDay = (p && jNum(p, ce, &f)) ? (f > 0.5f ? 1 : 0) : 1;
  p = jFind(cs, ce, "apparent_temperature");
  if(p && jNum(p, ce, &w->feels))    w->have |= WXF_FEELS;
  p = jFind(cs, ce, "relative_humidity_2m");
  if(p && jNum(p, ce, &w->humidity)) w->have |= WXF_HUMIDITY;
  p = jFind(cs, ce, "precipitation");
  if(p && jNum(p, ce, &w->precip))   w->have |= WXF_PRECIP;
  p = jFind(cs, ce, "cloud_cover");
  if(p && jNum(p, ce, &w->cloud))    w->have |= WXF_CLOUD;
  p = jFind(cs, ce, "pressure_msl");
  if(p && jNum(p, ce, &w->pressure)) w->have |= WXF_PRESSURE;
  p = jFind(cs, ce, "wind_speed_10m");
  if(p && jNum(p, ce, &w->windSpeed)) w->have |= WXF_WIND;
  p = jFind(cs, ce, "wind_gusts_10m");
  if(p && jNum(p, ce, &w->windGust))  w->have |= WXF_GUST;
  p = jFind(cs, ce, "wind_direction_10m");
  if(p && jNum(p, ce, &f)){ w->windDir = (int16_t)lroundf(f); w->have |= WXF_WINDDIR; }

  // ---- hourly ----
  const char *hs = NULL, *he = NULL;
  if(jBlock(s, e, "hourly", &hs, &he)){
    FlexWxHour* h = w->hours;
    size_t st = sizeof(FlexWxHour);
    int n = jArrRec(hs, he, "time", h, st, FLEXWX_HOURS, setHTime);
    jArrRec(hs, he, "temperature_2m",            h, st, FLEXWX_HOURS, setHTemp);
    jArrRec(hs, he, "apparent_temperature",      h, st, FLEXWX_HOURS, setHFeels);
    jArrRec(hs, he, "relative_humidity_2m",      h, st, FLEXWX_HOURS, setHRh);
    jArrRec(hs, he, "precipitation",             h, st, FLEXWX_HOURS, setHPrec);
    jArrRec(hs, he, "precipitation_probability", h, st, FLEXWX_HOURS, setHPop);
    jArrRec(hs, he, "weather_code",              h, st, FLEXWX_HOURS, setHCode);
    jArrRec(hs, he, "wind_speed_10m",            h, st, FLEXWX_HOURS, setHWind);
    jArrRec(hs, he, "uv_index",                  h, st, FLEXWX_HOURS, setHUv);
    jArrRec(hs, he, "visibility",                h, st, FLEXWX_HOURS, setHVis);
    if(n < 0) n = 0;
    if(n > FLEXWX_HOURS) n = FLEXWX_HOURS;

    // Alineado a la hora en curso. No se da por hecho que la API empiece
    // justo ahora: se busca el bloque horario que CONTIENE obsTime y se
    // desplaza. Si la API ya venia alineada, el desplazamiento es 0.
    int i0 = 0;
    for(int i = 0; i < n; i++){
      if(w->hours[i].t + 3600 > w->obsTime){ i0 = i; break; }
      i0 = i + 1;
    }
    if(i0 > 0 && i0 < n){
      memmove(w->hours, w->hours + i0, (size_t)(n - i0) * sizeof(FlexWxHour));
      n -= i0;
    } else if(i0 >= n){
      n = 0;                       // todas las horas quedaron en el pasado
    }
    w->hourCount = (uint8_t)n;

    if(n > 0){
      if(w->hours[0].uv10 != WX_NOVAL_U8){ w->uv = w->hours[0].uv10 / 10.0f; w->have |= WXF_UV; }
      if(w->hours[0].vis100 != WX_NOVAL_U16){ w->visibility = w->hours[0].vis100 * 100.0f; w->have |= WXF_VIS; }
      if(w->hours[0].pop != WX_NOVAL_U8){ w->pop = w->hours[0].pop; w->have |= WXF_POP; }
    }
  }

  // ---- daily ----
  const char *ds = NULL, *de = NULL;
  if(jBlock(s, e, "daily", &ds, &de)){
    FlexWxDay* d = w->days;
    size_t st = sizeof(FlexWxDay);
    int n = jArrRec(ds, de, "time", d, st, FLEXWX_DAYS, setDDate);
    jArrRec(ds, de, "weather_code",                   d, st, FLEXWX_DAYS, setDCode);
    jArrRec(ds, de, "temperature_2m_max",             d, st, FLEXWX_DAYS, setDMax);
    jArrRec(ds, de, "temperature_2m_min",             d, st, FLEXWX_DAYS, setDMin);
    jArrRec(ds, de, "sunrise",                        d, st, FLEXWX_DAYS, setDRise);
    jArrRec(ds, de, "sunset",                         d, st, FLEXWX_DAYS, setDSet);
    jArrRec(ds, de, "uv_index_max",                   d, st, FLEXWX_DAYS, setDUv);
    jArrRec(ds, de, "precipitation_probability_max",  d, st, FLEXWX_DAYS, setDPop);
    if(n < 0) n = 0;
    if(n > FLEXWX_DAYS) n = FLEXWX_DAYS;
    w->dayCount = (uint8_t)n;
    if(n > 0){
      if(w->days[0].max10 != WX_NOVAL_I16 && w->days[0].min10 != WX_NOVAL_I16){
        w->tmax = w->days[0].max10 / 10.0f;
        w->tmin = w->days[0].min10 / 10.0f;
        w->have |= WXF_MINMAX;
      }
      if(w->days[0].sunrise && w->days[0].sunset){
        w->sunrise = w->days[0].sunrise;
        w->sunset  = w->days[0].sunset;
        w->have |= WXF_SUN;
      }
      if(!(w->have & WXF_UV) && w->days[0].uv10 != WX_NOVAL_U8){
        w->uv = w->days[0].uv10 / 10.0f; w->have |= WXF_UV;
      }
    }
  }

  // Publicacion ATOMICA: el contenido ya esta entero antes de mover el
  // indice, asi que el renderer no puede ver una estructura a medias.
  gWxCur  = 1 - gWxCur;
  gWxHave = true;
  gWxFresh = true;
  gWxErr   = WXE_NONE;
  gWxFails = 0;
  gWxOkMs  = millis();
  gWxOkValid = true;
  gWxGen = gWxGen + 1;
  return true;
}

static bool wxDoFetch(){
  // Copia estable de la ubicacion: si la interfaz cambia la seleccion a mitad
  // de la descarga, estos datos siguen siendo los de la ciudad que se pidio y
  // se etiquetan con ella (nunca datos de una ciudad con el nombre de otra).
  FlexWxLoc loc = gCmdLoc;
  if(loc.name[0] == 0) return wxFail(WXE_NOLOC);
  static char url[760];
  snprintf(url, sizeof(url),
           WX_HOST_FORECAST
           "?latitude=%.4f&longitude=%.4f&timezone=auto&timeformat=unixtime&wind_speed_unit=kmh"
           "&forecast_days=%d&forecast_hours=%d"
           "&current=" WX_CUR_VARS
           "&hourly=" WX_HR_VARS
           "&daily=" WX_DY_VARS,
           (double)loc.lat, (double)loc.lon, FLEXWX_DAYS, FLEXWX_HOURS);

  size_t n = wxHttpGet(url, FLEXWX_HTTP_BUF);
  if(n == 0) return false;                       // gWxErr ya puesto
  if(!wxParseForecast(gNetBuf, n, &loc)) return false;
  wxSaveCache();
  return true;
}

// ---- Geocoding -------------------------------------------------------
static void wxUrlEncode(const char* in, char* out, size_t n){
  size_t o = 0;
  for(const unsigned char* p = (const unsigned char*)in; *p && o + 4 < n; p++){
    unsigned char c = *p;
    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~'){
      out[o++] = (char)c;
    } else if(c == ' '){
      out[o++] = '+';
    } else {
      static const char hex[] = "0123456789ABCDEF";
      out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
}

static bool wxDoSearch(){
  if(gQuery[0] == 0){ gResSt = WXQ_EMPTY; gResN = 0; return false; }
  char enc[FLEXWX_QUERY * 3 + 4];
  wxUrlEncode(gQuery, enc, sizeof(enc));
  static const char* GEO_LANG[5] = { "es", "en", "fr", "pt", "it" };
  static char url[300];
  snprintf(url, sizeof(url), WX_HOST_GEO "?name=%s&count=%d&language=%s&format=json",
           enc, FLEXWX_RESULTS, GEO_LANG[wxLang(gQueryLang)]);

  size_t n = wxHttpGet(url, FLEXWX_GEO_BUF);
  if(n == 0){ gResN = 0; gResSt = WXQ_ERROR; return false; }

  const char* s = gNetBuf; const char* e = gNetBuf + n;
  const char *rs = NULL, *re = NULL;
  if(!jBlock(s, e, "results", &rs, &re)){ gResN = 0; gResSt = WXQ_EMPTY; return true; }

  uint8_t cnt = 0;
  const char* p = jSkipWs(rs, re);
  while(p < re && cnt < FLEXWX_RESULTS){
    if(*p != '{'){ p++; continue; }
    const char* oe = jSkipVal(p, re);          // objeto completo del resultado
    const char* os = p + 1;
    const char* ie = (oe > os) ? oe - 1 : os;

    FlexWxLoc L; memset(&L, 0, sizeof(L));
    const char* q = jFind(os, ie, "name");
    bool okName = q && jStr(q, ie, L.name, sizeof(L.name));
    float lat = 0, lon = 0;
    q = jFind(os, ie, "latitude");  bool okLat = q && jNum(q, ie, &lat);
    q = jFind(os, ie, "longitude"); bool okLon = q && jNum(q, ie, &lon);
    if(okName && okLat && okLon && L.name[0] &&
       lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f){
      L.lat = lat; L.lon = lon;
      char a1[FLEXWX_REGION] = ""; char co[FLEXWX_REGION] = "";
      q = jFind(os, ie, "admin1");  if(q) jStr(q, ie, a1, sizeof(a1));
      q = jFind(os, ie, "country"); if(q) jStr(q, ie, co, sizeof(co));
      // "Region, Pais" se compone aparte y se copia recortado: asi el recorte
      // es explicito (y no un truncado accidental de snprintf).
      char reg[FLEXWX_REGION * 2 + 4] = "";
      if(a1[0] && co[0])      snprintf(reg, sizeof(reg), "%s, %s", a1, co);
      else if(co[0])          snprintf(reg, sizeof(reg), "%s", co);
      else if(a1[0])          snprintf(reg, sizeof(reg), "%s", a1);
      strncpy(L.region, reg, sizeof(L.region) - 1);
      L.region[sizeof(L.region) - 1] = 0;
      gRes[cnt] = L;
      cnt++;
    }
    p = jSkipWs(oe, re);
    while(p < re && *p == ',') p = jSkipWs(p + 1, re);
  }
  gResN  = cnt;
  gResSt = cnt ? WXQ_OK : WXQ_EMPTY;
  if(!cnt) gWxErr = WXE_NORESULT;
  return cnt > 0;
}

// #############################################################
// ##  9) TAREA DE RED
// #############################################################
static void flexWxTask(void*){
  for(;;){
    if(gReqSearch){                 // la busqueda va primero: hay alguien mirando
      gReqSearch = false;
      gResSt = WXQ_BUSY;
      wxDoSearch();                 // nunca lanza: deja el estado en gResSt
    } else if(gReqFetch){
      gReqFetch = false;
      gWxBusy = true;
      wxDoFetch();                  // el error queda en gWxErr; nunca lanza
      gWxAttempts = gWxAttempts + 1;                // lo lee flexWeatherTick para elegir la cadencia
      gWxBusy = false;
    } else {
      vTaskDelay(pdMS_TO_TICKS(120));
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

// #############################################################
// ##  10) API PUBLICA
// #############################################################

void flexWeatherBegin(void){
  if(gBeganOk) return;
  memset(gWx, 0, sizeof(gWx));
  memset(&gCmdLoc, 0, sizeof(gCmdLoc));
  wxLoadLocs();      // solo NVS (flash), nunca radio: setup() no toca el C6
  wxLoadCache();
  gNextAutoMs = millis() + 4000;    // primer intento cuando el sistema ya vive
  gBeganOk = true;
  // 10 KB de pila: TLS (mbedTLS) necesita margen, igual que la tarea del OTA.
  if(xTaskCreatePinnedToCore(flexWxTask, "flexWx", 10240, NULL, 1, &gTask, 1) != pdPASS){
    gTask = NULL;
    gWxErr = WXE_MEM;      // sin tarea no habra descargas: que se vea, no un "cargando" eterno
  }
}

// Encola una descarga con la ubicacion seleccionada. Copia lat/lon ANTES
// de publicar la orden: la tarea nunca ve una ubicacion a medio cambiar.
static void wxQueueFetch(){
  if(gLocSel < 0 || gLocSel >= (int8_t)gLocN){ gWxErr = WXE_NOLOC; return; }
  gCmdLoc   = gLocs[gLocSel];
  gReqFetch = true;
}

void flexWeatherTick(bool netOnline){
  if(!gBeganOk) return;
  uint32_t now = millis();

  // 1) Reprogramar en cuanto TERMINA un intento, con su resultado ya en la
  //    mano: exito -> cadencia normal; fallo aislado -> reintento corto;
  //    varios fallos seguidos -> reintento largo (nada de insistir cada
  //    segundo contra un servidor que no responde).
  static uint32_t seenAttempts = 0;
  if(gWxAttempts != seenAttempts){
    seenAttempts = gWxAttempts;
    uint32_t next = FLEXWX_REFRESH_MS;
    if(gWxErr != WXE_NONE)
      next = (gWxFails >= FLEXWX_RETRY_FAILS) ? FLEXWX_RETRY_LONG_MS : FLEXWX_RETRY_MS;
    gNextAutoMs = now + next;
  }

  if(gWxBusy || gReqFetch) return;
  if(gLocSel < 0) return;
  if(!netOnline) return;                       // sin red no se molesta a nadie
  if((int32_t)(now - gNextAutoMs) < 0) return;

  // 2) Con datos frescos y dentro de la ventana no se descarga nada: solo se
  //    vuelve a mirar dentro de un minuto.
  if(gWxHave && gWxFresh && gWxOkValid && (now - gWxOkMs) < FLEXWX_REFRESH_MS){
    gNextAutoMs = now + 60000UL;
    return;
  }
  gNextAutoMs = now + FLEXWX_REFRESH_MS;       // lo ajustara el bloque 1 al terminar
  wxQueueFetch();
}

void flexWeatherRefresh(bool force){
  if(!gBeganOk) return;
  if(gWxBusy || gReqFetch) return;
  if(gLocSel < 0){ gWxErr = WXE_NOLOC; return; }
  if(!force && gWxOkValid && (millis() - gWxOkMs) < 300000UL) return;  // anti-machaque: al abrir la app no se descarga si hay algo de hace <5 min
  gNextAutoMs = millis() + FLEXWX_REFRESH_MS;
  wxQueueFetch();
}

void flexWeatherLocSelect(uint8_t i){
  if(i >= gLocN) return;
  if(gLocSel == (int8_t)i && gWxHave && gWxFresh) return;
  gLocSel = (int8_t)i;
  wxSaveLocs();
  // Cambio de ubicacion: los datos que hay ya no valen para la nueva
  // ciudad. Se conservan en pantalla SOLO si son de esa misma ciudad.
  const FlexWeather* w = flexWeatherData();
  if(w && strncmp(w->loc.name, gLocs[i].name, FLEXWX_NAME) != 0){
    gWxHave = false; gWxFresh = false; gWxOkValid = false; gWxGen = gWxGen + 1;
  }
  gWxErr = WXE_NONE; gWxFails = 0;
  gNextAutoMs = millis() + FLEXWX_REFRESH_MS;
  wxQueueFetch();
}

bool flexWeatherLocAdd(const FlexWxLoc* l){
  if(!l || l->name[0] == 0) return false;
  if(gLocN >= FLEXWX_LOCS) return false;
  for(uint8_t i = 0; i < gLocN; i++)                       // ya guardada -> solo seleccionarla
    if(strncmp(gLocs[i].name, l->name, FLEXWX_NAME) == 0 &&
       fabsf(gLocs[i].lat - l->lat) < 0.01f && fabsf(gLocs[i].lon - l->lon) < 0.01f){
      flexWeatherLocSelect(i);
      return true;
    }
  gLocs[gLocN] = *l;
  gLocs[gLocN].name[FLEXWX_NAME - 1] = 0;
  gLocs[gLocN].region[FLEXWX_REGION - 1] = 0;
  gLocN++;
  wxSaveLocs();
  flexWeatherLocSelect((uint8_t)(gLocN - 1));
  return true;
}

void flexWeatherLocRemove(uint8_t i){
  if(i >= gLocN) return;
  for(uint8_t k = i; k + 1 < gLocN; k++) gLocs[k] = gLocs[k + 1];
  gLocN--;
  memset(&gLocs[gLocN], 0, sizeof(FlexWxLoc));
  if(gLocN == 0){
    gLocSel = -1;
    gWxHave = false; gWxFresh = false; gWxOkValid = false; gWxGen = gWxGen + 1;   // sin ubicacion no hay clima que ensenar
  } else if(gLocSel >= (int8_t)gLocN){
    gLocSel = (int8_t)(gLocN - 1);
  }
  wxSaveLocs();
  if(gLocSel >= 0){
    const FlexWeather* w = flexWeatherData();
    if(w && strncmp(w->loc.name, gLocs[gLocSel].name, FLEXWX_NAME) != 0){
      gWxHave = false; gWxFresh = false; gWxOkValid = false; gWxGen = gWxGen + 1;
      wxQueueFetch();
    }
  }
}

void flexWeatherSearch(const char* q, int lang){
  if(!gBeganOk || !q) return;
  if(gReqSearch) return;                    // ya hay una en cola
  snprintf(gQuery, sizeof(gQuery), "%s", q);
  gQueryLang = lang;
  gResN = 0;
  gResSt = WXQ_BUSY;
  gReqSearch = true;                        // puede convivir con una descarga en curso
}

#endif // FLEXWX_ON
