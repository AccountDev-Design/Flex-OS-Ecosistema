// #############################################################
//  PRUEBAS DE HOST DEL MOTOR METEOROLOGICO (Flex Weather)
//  ------------------------------------------------------------
//  Compila y ejecuta FlexOS_Weather.cpp EN EL PC -- el mismo
//  fichero que va a la placa, no una copia -- con:
//    · el entorno Arduino simulado de stub/,
//    · una NVS en memoria (wxstub/Preferences.h),
//    · sin red: lo que se prueba es el PARSEO y la LOGICA, que es
//      la parte escrita aqui y la que puede estar mal.
//
//  QUE SE COMPRUEBA DE VERDAD AQUI
//    1. Que una respuesta REAL de Open-Meteo (timeformat=unixtime)
//       se lee entera y con los valores exactos: temperatura,
//       viento, rachas, presion, amanecer/atardecer, 48 h y dias.
//    2. Que un null de la API NO se convierte en 0: queda marcado
//       como "no disponible" para que la interfaz lo diga.
//    3. Que los tiempos unix se leen en DOBLE precision. Con float,
//       1755558600 se guardaba como 1755558016: el panel horario y
//       el arco del sol salian desplazados hasta dos minutos. Esta
//       comprobacion existe porque el fallo era real.
//    4. Que una respuesta vacia, truncada, con basura o con el JSON
//       de error de la API NO publica nada ni corrompe los datos
//       buenos que ya habia.
//    5. Que el panel horario se alinea a la hora EN CURSO aunque la
//       API mande horas ya pasadas, y que 96 h o 16 dias se recortan
//       sin escribir fuera de los arrays.
//    6. Que la cache de NVS vuelve intacta tras un "reinicio" y que
//       una cache corrupta se rechaza en vez de darse por buena.
//    7. Que las ubicaciones persisten, se deduplican, respetan el
//       tope y que unas preferencias corruptas no dejan coordenadas
//       imposibles.
//    8. Que la frase del tiempo es DETERMINISTA y sale de los datos
//       (con 27 km/h dice "y viento"), y que sin datos no dice nada.
//    9. Que los estados (nunca cargado / cargando / valido / cache /
//       error / actualizando) son los que dicta cada situacion.
//
//  LIMITE HONESTO: aqui no hay TLS ni sockets. La descarga se prueba
//  contra el servidor de verdad en la placa; lo que estas pruebas
//  cubren es todo lo que ocurre DESPUES de recibir los bytes.
// #############################################################
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Arduino.h"
#include "WiFi.h"

// Reloj y consola del arnes (los declara stub/Arduino.h; cada prueba los
// define, igual que test_vault o test_app).
static unsigned long gMs = 1000;
unsigned long millis(){ return gMs; }
void delay(unsigned long ms){ gMs += ms; }
__FlexSerial Serial;
__FlexWiFi   WiFi;

// El modulo real, tal cual va a la placa.
#include "FlexOS_Weather.cpp"

static int gFails = 0, gChecks = 0;
static void chk(bool ok, const char* what){
  gChecks++;
  if(!ok){ printf("  FALLO: %s\n", what); gFails++; }
}

// Respuesta con el formato EXACTO de Open-Meteo con timeformat=unixtime
// (recortada a 6 horas y 3 dias para que quepa aqui; la estructura es la
// misma que la de 48 h y 7 dias que pide el firmware).
static const char* SAMPLE =
"{\"latitude\":-11.99,\"longitude\":-77.0,\"generationtime_ms\":0.42,\"utc_offset_seconds\":-18000,"
"\"timezone\":\"America/Lima\",\"timezone_abbreviation\":\"-05\",\"elevation\":161.0,"
"\"current_units\":{\"time\":\"unixtime\",\"interval\":\"seconds\",\"temperature_2m\":\"C\",\"weather_code\":\"wmo code\"},"
"\"current\":{\"time\":1755558600,\"interval\":900,\"temperature_2m\":21.3,\"apparent_temperature\":21.0,"
"\"relative_humidity_2m\":78,\"is_day\":1,\"precipitation\":0.00,\"weather_code\":2,\"cloud_cover\":62,"
"\"pressure_msl\":1013.4,\"wind_speed_10m\":27.4,\"wind_direction_10m\":200,\"wind_gusts_10m\":41.0},"
"\"hourly_units\":{\"time\":\"unixtime\",\"temperature_2m\":\"C\"},"
"\"hourly\":{\"time\":[1755558000,1755561600,1755565200,1755568800,1755572400,1755576000],"
"\"temperature_2m\":[21.4,20.6,19.9,19.5,19.2,19.0],"
"\"apparent_temperature\":[21.1,20.3,19.6,19.2,18.9,18.7],"
"\"relative_humidity_2m\":[78,80,82,83,84,85],"
"\"precipitation\":[0.00,0.00,0.10,0.00,0.00,0.00],"
"\"precipitation_probability\":[2,2,3,null,4,5],"
"\"weather_code\":[2,2,3,3,3,2],"
"\"wind_speed_10m\":[27.4,25.1,22.0,20.4,19.8,18.5],"
"\"uv_index\":[0.40,0.10,0.00,0.00,0.00,0.00],"
"\"visibility\":[24140.0,24140.0,20000.0,null,18000.0,17000.0]},"
"\"daily_units\":{\"time\":\"unixtime\"},"
"\"daily\":{\"time\":[1755493200,1755579600,1755666000],"
"\"weather_code\":[3,2,61],"
"\"temperature_2m_max\":[25.1,26.3,24.0],"
"\"temperature_2m_min\":[17.2,17.8,18.1],"
"\"sunrise\":[1755516720,1755603120,1755689520],"
"\"sunset\":[1755559440,1755645840,1755732240],"
"\"uv_index_max\":[6.10,5.80,null],"
"\"precipitation_probability_max\":[3,9,48]}}";

// Respuesta real del geocoding, con un acento escapado como ú.
static const char* GEO =
"{\"results\":[{\"id\":3934905,\"name\":\"San Juan de Lurigancho\",\"latitude\":-11.99167,"
"\"longitude\":-77.00694,\"elevation\":190.0,\"feature_code\":\"PPLA3\",\"country_code\":\"PE\","
"\"admin1\":\"Lima region\",\"admin2\":\"Lima\",\"timezone\":\"America/Lima\",\"population\":1091303,"
"\"country\":\"Per\\u00fa\",\"admin1_id\":3936451},"
"{\"id\":3936456,\"name\":\"Lima\",\"latitude\":-12.04318,\"longitude\":-77.02824,"
"\"country_code\":\"PE\",\"admin1\":\"Lima\",\"timezone\":\"America/Lima\",\"country\":\"Per\\u00fa\"}],"
"\"generationtime_ms\":0.9}";

// -------------------------------------------------------------
//  1. Parseo completo
// -------------------------------------------------------------
static void testPronostico(){
  printf("Parseo del pronostico\n");
  FlexWxLoc loc; memset(&loc, 0, sizeof(loc));
  snprintf(loc.name, sizeof(loc.name), "San Juan de Lurigancho");
  loc.lat = -11.99f; loc.lon = -77.0f;
  chk(wxParseForecast(SAMPLE, strlen(SAMPLE), &loc), "la respuesta se acepta");
  const FlexWeather* w = flexWeatherData();
  chk(w != NULL, "y se publica");
  if(!w) return;
  chk(w->utcOffset == -18000, "desfase horario de la ubicacion");
  chk(w->obsTime == 1755558600, "hora de observacion EXACTA (sin perdida de precision)");
  chk(w->temp > 21.2f && w->temp < 21.4f, "temperatura actual");
  chk(w->code == 2 && w->isDay == 1, "codigo de tiempo y dia/noche");
  chk((w->have & WXF_HUMIDITY) && (int)w->humidity == 78, "humedad");
  chk((w->have & WXF_WIND) && w->windSpeed > 27.3f, "viento");
  chk((w->have & WXF_GUST) && w->windGust > 40.9f, "rachas");
  chk((w->have & WXF_PRESSURE) && w->pressure > 1013.0f, "presion");
  chk((w->have & WXF_WINDDIR) && w->windDir == 200, "direccion del viento");
  chk(w->hourCount == 6, "las seis horas del ejemplo");
  chk(w->hours[0].t == 1755558000, "primera hora EXACTA");
  chk(w->hours[0].temp10 == 214 && w->hours[1].temp10 == 206, "temperaturas horarias en decimas");
  chk(w->hours[2].precip100 == 10, "precipitacion horaria en centesimas de mm");
  chk(w->hours[2].vis100 == 200, "visibilidad horaria (20 km)");
  chk((w->have & WXF_UV) && w->uv > 0.39f && w->uv < 0.41f, "UV de la hora en curso");
  chk((w->have & WXF_POP) && w->pop == 2, "probabilidad de precipitacion actual");
  chk(w->dayCount == 3, "los tres dias del ejemplo");
  chk(w->days[0].max10 == 251 && w->days[0].min10 == 172, "maxima y minima de hoy");
  chk(w->days[2].code == 61, "codigo del tercer dia");
  chk((w->have & WXF_MINMAX) && (int)w->tmax == 25, "maxima publicada");
  chk((w->have & WXF_SUN) && w->sunrise == 1755516720 && w->sunset == 1755559440,
      "amanecer y atardecer EXACTOS");
  chk(strcmp(w->loc.name, "San Juan de Lurigancho") == 0, "los datos llevan SU ubicacion");
}

// -------------------------------------------------------------
//  2. null != 0
// -------------------------------------------------------------
static void testNulos(){
  printf("Un null de la API no es un cero\n");
  const FlexWeather* w = flexWeatherData();
  if(!w){ chk(false, "no hay datos que revisar"); return; }
  chk(w->hours[3].pop    == WX_NOVAL_U8,  "probabilidad nula marcada como no disponible");
  chk(w->hours[3].vis100 == WX_NOVAL_U16, "visibilidad nula marcada como no disponible");
  chk(w->days[2].uv10    == WX_NOVAL_U8,  "UV nulo del tercer dia marcado como no disponible");
}

// -------------------------------------------------------------
//  3. Respuestas rotas
// -------------------------------------------------------------
static void testRotas(){
  printf("Respuestas rotas: ni se publican ni corrompen lo que habia\n");
  uint32_t genAntes = flexWeatherGen();
  const FlexWeather* antes = flexWeatherData();
  float tempAntes = antes ? antes->temp : -999.0f;

  static const char* casos[] = {
    "",                                                            // vacia
    "{",                                                           // truncada
    "no soy json",                                                 // basura
    "{\"error\":true,\"reason\":\"Latitude must be in range\"}",   // error real de la API
    "{\"current\":{\"time\":123}}",                                // sin temperatura
    "{\"current\":{\"time\":1,\"temperature_2m\":",                // cortada a mitad
  };
  for(unsigned i = 0; i < sizeof(casos) / sizeof(casos[0]); i++){
    FlexWxLoc l; memset(&l, 0, sizeof(l)); snprintf(l.name, sizeof(l.name), "X");
    chk(!wxParseForecast(casos[i], strlen(casos[i]), &l), "se rechaza una respuesta invalida");
  }
  // Y una cortada justo dentro de 'hourly': el caso de la conexion que cae.
  char media[2048];
  size_t n = strlen(SAMPLE) * 2 / 3;
  if(n > sizeof(media) - 1) n = sizeof(media) - 1;
  memcpy(media, SAMPLE, n); media[n] = 0;
  FlexWxLoc l2; memset(&l2, 0, sizeof(l2)); snprintf(l2.name, sizeof(l2.name), "Y");
  chk(!wxParseForecast(media, n, &l2), "se rechaza una respuesta cortada a la mitad");

  const FlexWeather* w = flexWeatherData();
  chk(flexWeatherGen() == genAntes, "la generacion no cambia con datos invalidos");
  chk(w && w->temp == tempAntes, "los datos buenos siguen ahi");
  chk(w && strcmp(w->loc.name, "San Juan de Lurigancho") == 0, "y con su ubicacion intacta");
}

// -------------------------------------------------------------
//  4. Alineado horario y desbordes
// -------------------------------------------------------------
static void construir(char* out, size_t cap, int nHours, int firstOffsetH, int nDays){
  long obs = 1755558600;
  long h0 = obs - (obs % 3600) - (long)firstOffsetH * 3600;
  size_t o = 0;
  o += snprintf(out + o, cap - o, "{\"utc_offset_seconds\":-18000,\"current\":{\"time\":%ld,"
                "\"temperature_2m\":21.3,\"weather_code\":2,\"is_day\":1},\"hourly\":{\"time\":[", obs);
  for(int i = 0; i < nHours; i++) o += snprintf(out + o, cap - o, "%s%ld", i ? "," : "", h0 + (long)i * 3600);
  o += snprintf(out + o, cap - o, "],\"temperature_2m\":[");
  for(int i = 0; i < nHours; i++) o += snprintf(out + o, cap - o, "%s%d.5", i ? "," : "", 10 + (i % 20));
  o += snprintf(out + o, cap - o, "],\"weather_code\":[");
  for(int i = 0; i < nHours; i++) o += snprintf(out + o, cap - o, "%s2", i ? "," : "");
  o += snprintf(out + o, cap - o, "]},\"daily\":{\"time\":[");
  for(int i = 0; i < nDays; i++) o += snprintf(out + o, cap - o, "%s%ld", i ? "," : "", h0 + (long)i * 86400);
  o += snprintf(out + o, cap - o, "],\"temperature_2m_max\":[");
  for(int i = 0; i < nDays; i++) o += snprintf(out + o, cap - o, "%s%d.0", i ? "," : "", 20 + i);
  o += snprintf(out + o, cap - o, "],\"temperature_2m_min\":[");
  for(int i = 0; i < nDays; i++) o += snprintf(out + o, cap - o, "%s%d.0", i ? "," : "", 10 + i);
  o += snprintf(out + o, cap - o, "],\"weather_code\":[");
  for(int i = 0; i < nDays; i++) o += snprintf(out + o, cap - o, "%s3", i ? "," : "");
  snprintf(out + o, cap - o, "]}}");
}

static void testAlineado(){
  printf("Alineado horario y desbordes\n");
  static char j[16384];
  FlexWxLoc l; memset(&l, 0, sizeof(l)); snprintf(l.name, sizeof(l.name), "Z");
  long obs = 1755558600, cur = obs - (obs % 3600);

  construir(j, sizeof(j), 24, 6, 7);
  chk(wxParseForecast(j, strlen(j), &l), "respuesta con horas ya pasadas");
  const FlexWeather* w = flexWeatherData();
  chk(w->hours[0].t == cur, "el panel empieza en la hora EN CURSO, no en la primera del array");
  chk(w->hourCount == 24 - 6, "y las pasadas no ocupan sitio");

  construir(j, sizeof(j), 96, 0, 16);
  chk(wxParseForecast(j, strlen(j), &l), "respuesta mas larga de lo que cabe");
  w = flexWeatherData();
  chk(w->hourCount == FLEXWX_HOURS, "las horas se recortan al tope");
  chk(w->dayCount  == FLEXWX_DAYS,  "y los dias tambien");

  construir(j, sizeof(j), 4, 10, 3);
  chk(wxParseForecast(j, strlen(j), &l), "respuesta con TODAS las horas pasadas");
  w = flexWeatherData();
  chk(w->hourCount == 0, "no se publica un panel de horas viejas");

  const char* solo = "{\"utc_offset_seconds\":0,\"current\":{\"time\":1755558600,"
                     "\"temperature_2m\":9.5,\"weather_code\":61,\"is_day\":0}}";
  chk(wxParseForecast(solo, strlen(solo), &l), "respuesta con solo el tiempo actual");
  w = flexWeatherData();
  chk(w->hourCount == 0 && w->dayCount == 0, "sin horario ni diario");
  chk(w->temp > 9.4f && w->temp < 9.6f, "pero con la temperatura de verdad");
  chk(!(w->have & WXF_MINMAX), "sin bloque diario no se inventa una maxima");
  chk(!(w->have & WXF_SUN),    "ni un amanecer");
}

// -------------------------------------------------------------
//  5. Geocoding
// -------------------------------------------------------------
static void testGeocoding(){
  printf("Busqueda de ciudades\n");
  const char* s = GEO; const char* e = GEO + strlen(GEO);
  const char *rs = NULL, *re = NULL;
  chk(jBlock(s, e, "results", &rs, &re), "se encuentra el array de resultados");
  char nm[FLEXWX_NAME] = "", co[FLEXWX_REGION] = "";
  const char* p = jSkipWs(rs, re);
  const char* oe = jSkipVal(p, re);
  const char* is = p + 1; const char* ie = oe - 1;
  const char* q = jFind(is, ie, "name");
  chk(q && jStr(q, ie, nm, sizeof(nm)) && strcmp(nm, "San Juan de Lurigancho") == 0, "nombre del primer resultado");
  q = jFind(is, ie, "country");
  chk(q && jStr(q, ie, co, sizeof(co)) && strcmp(co, "Per\xC3\xBA") == 0, "el pais llega con su acento (\\u00fa a UTF-8)");
  float lat = 0;
  q = jFind(is, ie, "latitude");
  chk(q && jNum(q, ie, &lat) && lat < -11.9f && lat > -12.0f, "latitud");
  p = jSkipWs(oe, re);
  while(p < re && *p == ',') p = jSkipWs(p + 1, re);
  const char* oe2 = jSkipVal(p, re);
  q = jFind(p + 1, oe2 - 1, "name");
  chk(q && jStr(q, oe2 - 1, nm, sizeof(nm)) && strcmp(nm, "Lima") == 0, "y el segundo resultado tambien se lee");
  const char* vacio = "{\"generationtime_ms\":0.9}";
  const char *a = NULL, *b = NULL;
  chk(!jBlock(vacio, vacio + strlen(vacio), "results", &a, &b), "una busqueda sin resultados no inventa ninguno");
}

// -------------------------------------------------------------
//  6. Cache en NVS
// -------------------------------------------------------------
static void testCache(){
  printf("Cache en NVS\n");
  FlexWxLoc loc; memset(&loc, 0, sizeof(loc));
  snprintf(loc.name, sizeof(loc.name), "San Juan de Lurigancho");
  wxParseForecast(SAMPLE, strlen(SAMPLE), &loc);
  wxSaveCache();
  const FlexWeather* w = flexWeatherData();
  float t = w->temp; int32_t obs = w->obsTime;

  // "Reinicio": se borra el estado vivo y se recarga de NVS.
  memset(gWx, 0, sizeof(gWx));
  gWxHave = false; gWxCur = 0; gWxFresh = true; gWxOkValid = true;   // como si hubiera habido descarga antes
  wxLoadCache();
  const FlexWeather* r = flexWeatherData();
  chk(r != NULL, "la cache vuelve tras el reinicio");
  if(r){
    chk(r->temp == t && r->obsTime == obs, "con los mismos valores");
    chk(r->hourCount == 6, "y con su pronostico horario");
    chk(strcmp(r->loc.name, "San Juan de Lurigancho") == 0, "y su ubicacion");
  }
  chk(flexWeatherStatus() == WXS_STALE, "se marca como cache, no como dato fresco");

  // Con el reloj del sistema anclado, la antiguedad REAL se puede calcular.
  flexWeatherSetClock((uint32_t)obs + 34 * 60);
  int32_t age = flexWeatherAgeSec();
  chk(age >= 34 * 60 - 2 && age <= 34 * 60 + 2, "y su antiguedad sale del reloj del sistema");

  // Una cache corrupta no puede darse por buena.
  { Preferences p; p.begin("flexwx", false);
    char basura[1300]; memset(basura, 'Z', sizeof(basura));
    p.putBytes("cache", basura, sizeof(basura)); p.end(); }
  gWxHave = false;
  wxLoadCache();
  chk(flexWeatherData() == NULL, "una cache corrupta se rechaza");
}

// -------------------------------------------------------------
//  7. Ubicaciones
// -------------------------------------------------------------
static void testUbicaciones(){
  printf("Ubicaciones\n");
  prefsTestClear();
  gLocN = 0; gLocSel = -1; memset(gLocs, 0, sizeof(gLocs));

  FlexWxLoc a; memset(&a, 0, sizeof(a));
  snprintf(a.name, sizeof(a.name), "Cusco"); a.lat = -13.5f; a.lon = -71.97f;
  chk(flexWeatherLocAdd(&a), "se anade una ubicacion");
  chk(flexWeatherLocCount() == 1 && flexWeatherLocSel() == 0, "y queda seleccionada");
  chk(flexWeatherLocAdd(&a) && flexWeatherLocCount() == 1, "anadir la misma no la duplica");

  FlexWxLoc b; memset(&b, 0, sizeof(b));
  snprintf(b.name, sizeof(b.name), "Arequipa"); b.lat = -16.4f; b.lon = -71.5f;
  flexWeatherLocAdd(&b);
  chk(flexWeatherLocCount() == 2, "y una distinta si entra");
  for(int i = 0; i < 6; i++){
    FlexWxLoc c; memset(&c, 0, sizeof(c));
    snprintf(c.name, sizeof(c.name), "Ciudad%d", i); c.lat = (float)i; c.lon = (float)i;
    flexWeatherLocAdd(&c);
  }
  chk(flexWeatherLocCount() == FLEXWX_LOCS, "el tope de ubicaciones se respeta");

  uint8_t n = gLocN; int8_t sel = gLocSel;
  gLocN = 0; gLocSel = -1; memset(gLocs, 0, sizeof(gLocs));
  wxLoadLocs();
  chk(gLocN == n && gLocSel == sel, "sobreviven a un reinicio");
  chk(strcmp(gLocs[0].name, "Cusco") == 0, "y en el mismo orden");

  flexWeatherLocRemove(0);
  chk(gLocN == n - 1 && strcmp(gLocs[0].name, "Arequipa") == 0, "borrar una recoloca el resto");
  flexWeatherLocRemove(99);
  chk(gLocN == n - 1, "un indice invalido no toca la lista");
  while(gLocN) flexWeatherLocRemove(0);
  chk(flexWeatherLocSel() == -1, "sin ubicaciones no hay seleccion");

  // Preferencias corruptas: coordenadas imposibles.
  prefsTestClear();
  { FlexWxLoc bad[FLEXWX_LOCS]; memset(bad, 0, sizeof(bad));
    snprintf(bad[0].name, sizeof(bad[0].name), "Mala"); bad[0].lat = 999; bad[0].lon = 999;
    Preferences p; p.begin("flexwx", false);
    p.putBytes("locs", bad, sizeof(bad));
    p.putUChar("nloc", 1);
    p.putChar("sel", 0);
    p.end(); }
  wxLoadLocs();
  chk(gLocN == 0, "unas coordenadas imposibles no se dan por buenas");
}

// -------------------------------------------------------------
//  8. Textos deterministas
// -------------------------------------------------------------
static void testTextos(){
  printf("Conversores y frase del tiempo\n");
  chk(flexWeatherVisual(0) == WXV_CLEAR,       "codigo 0 = despejado");
  chk(flexWeatherVisual(2) == WXV_PARTLY,      "codigo 2 = parcialmente nublado");
  chk(flexWeatherVisual(65) == WXV_HEAVY_RAIN, "codigo 65 = lluvia intensa");
  chk(flexWeatherVisual(95) == WXV_THUNDER,    "codigo 95 = tormenta");
  chk(flexWeatherVisual(199) == WXV_CLOUDY,    "un codigo desconocido no inventa un cielo");
  bool rango = true, nulos = false;
  for(int c = 0; c < 100; c++){
    if(flexWeatherVisual(c) >= WXV_COUNT) rango = false;
    for(int l = 0; l < 6; l++) if(!flexWeatherCondName(c, l)) nulos = true;
  }
  chk(rango, "todos los codigos caen en un estado visual valido");
  chk(!nulos, "y tienen nombre en los seis idiomas");

  FlexWxLoc l; memset(&l, 0, sizeof(l)); snprintf(l.name, sizeof(l.name), "Test");
  wxParseForecast(SAMPLE, strlen(SAMPLE), &l);
  char d1[160], d2[160], d3[160];
  flexWeatherDescribe(d1, sizeof(d1), 0);
  flexWeatherDescribe(d2, sizeof(d2), 1);
  flexWeatherDescribe(d3, sizeof(d3), 0);
  printf("   ES: %s\n   EN: %s\n", d1, d2);
  chk(strstr(d1, "viento") != NULL, "con 27 km/h la frase menciona el viento");
  chk(strstr(d2, "windy")  != NULL, "y en ingles tambien");
  chk(strcmp(d1, d3) == 0, "la frase es determinista");
  bool had = gWxHave; gWxHave = false;
  char d4[160]; flexWeatherDescribe(d4, sizeof(d4), 0);
  chk(d4[0] == 0, "sin datos no se genera ninguna frase");
  gWxHave = had;
}

// -------------------------------------------------------------
//  9. Estados
// -------------------------------------------------------------
static void testEstados(){
  printf("Estados del motor\n");
  gWxHave = false; gWxErr = WXE_NONE; gWxBusy = false; gWxFresh = false;
  chk(flexWeatherStatus() == WXS_NEVER, "sin nada: nunca cargado");
  gWxBusy = true;
  chk(flexWeatherStatus() == WXS_LOADING, "descargando sin datos: cargando");
  gWxHave = true;
  chk(flexWeatherStatus() == WXS_UPDATING, "descargando con datos: actualizando");
  gWxBusy = false; gWxFresh = true; gWxErr = WXE_NONE;
  chk(flexWeatherStatus() == WXS_OK, "datos frescos: valido");
  gWxErr = WXE_TIMEOUT;
  chk(flexWeatherStatus() == WXS_STALE, "datos + ultimo intento fallido: cache");
  gWxHave = false;
  chk(flexWeatherStatus() == WXS_ERROR, "sin datos + fallo: error");
}

// -------------------------------------------------------------
//  10. TLS: la politica de esta version
// -------------------------------------------------------------
static void testTls(){
  printf("Politica de TLS\n");
  chk(FLEXWX_ALLOW_INSECURE == 0, "por defecto NO se acepta un certificado sin verificar");
  const char* ca = FLEXWX_ROOT_CA;
  chk(ca && ca[0], "hay una CA raiz fijada de fabrica");
  chk(strstr(ca, "-----BEGIN CERTIFICATE-----") != NULL, "y es un PEM");
  int n = 0;
  for(const char* p = ca; (p = strstr(p, "-----BEGIN CERTIFICATE-----")) != NULL; p += 10) n++;
  chk(n == 2, "con las DOS raices de Let's Encrypt (ISRG X1 y X2)");
}

int main(){
  printf("\n=== FlexOS · motor meteorologico (Open-Meteo) ===\n");
  prefsTestClear();
  testPronostico();
  testNulos();
  testRotas();
  testAlineado();
  testGeocoding();
  testCache();
  testUbicaciones();
  testTextos();
  testEstados();
  testTls();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
