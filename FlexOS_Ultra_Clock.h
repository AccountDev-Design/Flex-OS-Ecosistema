// #############################################################
// ##  FLEX OS ULTRA  ·  RELOJ DEL SISTEMA  ·  epoca UTC + zona de Lima
// ##  ----------------------------------------------------------
// ##  La epoca UTC en segundos como unica fuente de verdad de la hora, su
// ##  conversion a hora local y la API de NTP que consume Ajustes. La
// ##  implementacion de red del cliente NTP vive en FlexOS_Ultra_NTP.h.
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
#include "FlexOS_Ultra_Session.h"   // eslabon anterior de la cadena

// #############################################################
// ##  RELOJ DEL SISTEMA  ·  epoca UTC + zona fija de Lima
// ##  ------------------------------------------------------
// ##  Antes el reloj era un contador de MINUTOS desde el arranque
// ##  sembrado a mano (sab 4 jul 2026, 13:23) que avanzaba la fecha
// ##  dia a dia con un bucle. No habia forma de meterle una hora
// ##  real: no existia ningun instante absoluto que fijar.
// ##
// ##  Ahora la fuente de verdad es UNA epoca UTC en segundos
// ##  (clkEpochRef) anclada a un millis() (clkRefMs). De ahi salen
// ##  rtcY/rtcMo/rtcD/rtcWd/rtcH/rtcMin exactamente igual que
// ##  antes, asi que NINGUN llamante cambia: la pantalla de
// ##  bloqueo, el widget, la barra de estado y Ajustes siguen
// ##  leyendo las mismas variables.
// ##
// ##  ZONA HORARIA: Peru (America/Lima) es UTC-5 TODO el ano --
// ##  no aplica horario de verano desde 1994. Por eso el desfase
// ##  es una constante y no hay tabla de reglas que mantener.
// ##
// ##  SIN NTP el reloj sigue funcionando igual que siempre: con la
// ##  ultima hora guardada en NVS, o con la semilla de fabrica si
// ##  el equipo nunca se ha sincronizado.
// #############################################################

// Desfase fijo de Lima. Negativo = al oeste de Greenwich.
#define FLEXOS_TZ_OFFSET_SEC   (-5 * 3600)
// Semilla de fabrica (la de siempre): sabado 4 de julio de 2026, 13:23 LOCAL.
#define FLEXOS_CLK_SEED_Y      2026
#define FLEXOS_CLK_SEED_MO     7
#define FLEXOS_CLK_SEED_D      4
#define FLEXOS_CLK_SEED_MIN    (13 * 60 + 23)
// Suelo de cordura: 1 ene 2026 00:00 UTC. Cualquier epoca por debajo es
// basura (un servidor que responde ceros, un NVS a medio escribir) y se
// rechaza en vez de dejar el reloj en 1970.
#define FLEXOS_CLK_MIN_EPOCH   1767225600UL

static int rtcY = 2026, rtcMo = 7, rtcD = 4, rtcWd = 6, rtcH = 13, rtcMin = 23;
static unsigned long clkBootMs = 0;
static long          clkLastMin = -1;
// Semilla en minutos del dia. Se conserva por compatibilidad con setup(),
// que la fija antes del primer clkUpdate().
static long          seedMinOfDay = FLEXOS_CLK_SEED_MIN;

// ---- Ancla absoluta del reloj ----
static uint32_t      clkEpochRef = 0;      // segundos UTC del instante anclado
static unsigned long clkRefMs    = 0;      // millis() de ese mismo instante
static bool          clkAnchored = false;  // ya hay ancla (semilla, NVS o NTP)

static int daysInMonth(int y, int m){
  static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[m - 1];
}

// Dias civiles <-> dias desde 1970-01-01. Algoritmo de calendario
// proleptico gregoriano de Howard Hinnant: aritmetica entera pura, sin
// bucles y sin tablas, valido para cualquier ano. Sustituye al bucle
// "avanza un dia N veces" de antes, que con una fecha real (miles de
// dias desde la semilla) habria costado miles de iteraciones POR CUADRO.
static long clkDaysFromCivil(int y, int m, int d){
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);                       // [0, 399]
  unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
  return era * 146097L + (long)doe - 719468L;
}
static void clkCivilFromDays(long z, int &y, int &m, int &d){
  z += 719468L;
  long era = (z >= 0 ? z : z - 146096L) / 146097L;
  unsigned doe = (unsigned)(z - era * 146097L);                   // [0, 146096]
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long yy = (long)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);         // [0, 365]
  unsigned mp = (5 * doy + 2) / 153;                              // [0, 11]
  d = (int)(doy - (153 * mp + 2) / 5 + 1);                        // [1, 31]
  m = (int)(mp < 10 ? mp + 3 : mp - 9);                           // [1, 12]
  y = (int)(yy + (m <= 2));
}

// Segundos UTC AHORA. La resta de millis() se hace en aritmetica sin signo,
// asi que el desbordamiento de millis() (~49,7 dias) da el delta correcto;
// ademas clkUpdate() re-ancla cada hora, con lo que el delta nunca se acerca
// siquiera a ese limite.
static uint32_t clkNowUtc(){
  if(!clkAnchored) return 0;
  return clkEpochRef + (uint32_t)((millis() - clkRefMs) / 1000UL);
}
// Fija el ancla. Se usa igual para la semilla, para NVS y para NTP.
static void clkSetEpoch(uint32_t utc){
  clkEpochRef = utc; clkRefMs = millis(); clkAnchored = true;
  clkLastMin = -1;                    // fuerza el repintado del minuto
  // FLEX VAULT: el modulo de la boveda no sabe la hora (no toca la red ni
  // el reloj). Se la damos aqui, en el UNICO punto por el que pasan la
  // semilla, NVS y NTP, para que el registro de seguridad y los "ultimo
  // acceso" lleven una hora de verdad. Si el reloj nunca se ancla, la
  // boveda ensena "sin hora" en vez de inventarse una fecha.
  flexVaultSetClock(utc);
  // Lo mismo para el motor del clima: con una hora real puede decir "hace 34
  // min" incluso con una cache traida de NVS tras un reinicio, en vez de
  // limitarse a la hora de observacion.
  flexWeatherSetClock(utc);
}
// Ancla de fabrica: la misma fecha y hora que sembraba la version anterior.
static void clkSeedFactory(){
  long days = clkDaysFromCivil(FLEXOS_CLK_SEED_Y, FLEXOS_CLK_SEED_MO, FLEXOS_CLK_SEED_D);
  clkSetEpoch((uint32_t)(days * 86400L + seedMinOfDay * 60L - FLEXOS_TZ_OFFSET_SEC));
}
// Compat: la version anterior exportaba clkSetDate(addDays) para mover la
// fecha. Se conserva la firma porque es barata y deja el reloj coherente si
// algun dia hace falta desplazarlo a mano.
static void clkSetDate(long addDays){
  if(!clkAnchored) clkSeedFactory();
  clkSetEpoch(clkNowUtc() + (uint32_t)(addDays * 86400L));
}

// devuelve true si cambio el minuto (para repintar el reloj)
static bool clkUpdate(){
  if(!clkAnchored) clkSeedFactory();
  // Re-anclaje horario: dobla el tiempo transcurrido dentro de la epoca y
  // reinicia el contador de millis(). Mantiene el delta siempre pequeno (por
  // debajo del desbordamiento de millis) sin mover ni un segundo el reloj.
  if(millis() - clkRefMs > 3600000UL) clkSetEpoch(clkNowUtc());

  long local = (long)clkNowUtc() + FLEXOS_TZ_OFFSET_SEC;
  long days  = local / 86400L; long rem = local % 86400L;
  if(rem < 0){ rem += 86400L; days--; }            // division hacia -inf (fechas antes de 1970)
  long mins = days * 1440L + rem / 60L;
  if(mins == clkLastMin) return false;
  clkLastMin = mins;
  rtcH   = (int)(rem / 3600L);
  rtcMin = (int)((rem % 3600L) / 60L);
  clkCivilFromDays(days, rtcY, rtcMo, rtcD);
  long w = (days + 4) % 7; if(w < 0) w += 7;       // 1970-01-01 fue jueves
  rtcWd = (int)w;
  return true;
}

// #############################################################
// ##  NTP  ·  API que consume la pantalla de Ajustes
// ##  ------------------------------------------------------
// ##  La implementacion vive junto al resto de la red (mucho mas
// ##  abajo, tras la pantalla de Wi-Fi) porque necesita el socket
// ##  y la tarea de fondo. Aqui solo se declara lo que Ajustes
// ##  -- que esta ANTES en el archivo -- necesita llamar.
// #############################################################
static void ntpRequestSync(bool userAsked);      // pide una sincronizacion (no bloquea)
static void ntpOnWifiUp();                       // el Wi-Fi acaba de enlazar
static void ntpStateText(char* out, size_t n);   // "Sincronizado", "Sin conexion", ...
static void ntpLastSyncText(char* out, size_t n);// "hoy 13:24" / "nunca"
static bool ntpIsBusy();                         // hay una consulta en vuelo
static void clkSaveNvs();                        // vuelca la hora actual a NVS (lo usa tambien el apagado)
static bool gTimeNvsOk = true;                   // false = NVS no disponible (error real en Ajustes)

// Reloj "H:MM" pelado. Lo usa el RELOJ GIGANTE vectorial (bloqueo y app Reloj),
// que solo sabe dibujar digitos y ':' -> aqui NO puede entrar el AM/PM.
static void clkStr12(char* out, size_t n){
  if(g24h){ snprintf(out, n, "%d:%02d", rtcH, rtcMin); return; }
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d", h12, rtcMin);
}
// Reloj para las BARRAS DE ESTADO (fuente normal, sí admite letras).
// Antes todas las barras llamaban a clkStr12, y como en 12 h no se anadia
// AM/PM, el ajuste "Formato de hora" no cambiaba NADA visible entre las 00:00
// y las 12:59, y a partir de esa hora "1:23" podia ser la una de la tarde o de
// la madrugada. Ahora 24 h -> "13:23" y 12 h -> "1:23 PM".
// Reserva al menos 12 bytes: "12:34 PM" son 9 con el terminador.
static void clkStrBar(char* out, size_t n){
  if(g24h){ snprintf(out, n, "%d:%02d", rtcH, rtcMin); return; }
  int h12 = rtcH % 12; if(h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d %s", h12, rtcMin, (rtcH < 12) ? "AM" : "PM");
}
