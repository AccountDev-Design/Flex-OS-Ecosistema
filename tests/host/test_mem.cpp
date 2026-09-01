// #############################################################
//  test_mem.cpp  ·  pruebas de host de FlexOS_Mem.cpp
// #############################################################
//
//  El modulo bajo prueba es EXACTAMENTE el que va a la placa: las
//  reglas que deciden si Flex OS deja abrir una app pesada, cuando
//  se protege y que aviso saca. Son reglas que en placa solo se
//  verian ejecutando el caso justo (memoria a 5,4 MB, o 8 MB libres
//  en trozos de 300 KB), asi que se ejercitan aqui.
//
//  Que se comprueba:
//    1) Escala de presion y salud, con los cortes exactos.
//    2) Fragmentacion: la definicion y sus tres clases.
//    3) Veredicto de apertura por clase de peso, incluida la
//       distincion entre "todavia no" (soltar recursos) y "no".
//    4) Avisos: prioridad, enfriamiento por clase, separacion
//       global y que no se repiten en bucle.
//    5) Formato de cifras (lo que el usuario lee).

#include "../../FlexOS_Mem.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

#define MB(x) ((uint32_t)((x) * 1024u * 1024u))
#define KB(x) ((uint32_t)((x) * 1024u))

// Medida "sana" de referencia: 32 MB de PSRAM con 12 libres de una pieza.
static FlexMemSnap base(){
  FlexMemSnap s;
  memset(&s, 0, sizeof(s));
  s.psTotal = MB(32); s.psFree = MB(12); s.psLargest = MB(11); s.psPeakUsed = MB(22);
  s.inTotal = KB(400); s.inFree = KB(180); s.inMin = KB(120);
  s.fsTotal = MB(8);  s.fsUsed = MB(2);   s.fsValid = 1;
  return s;
}

// =============================================================
static void testLevels(){
  std::printf("-- niveles de presion --\n");
  FlexMemSnap s = base();
  CHECK(flexMemLevel(&s) == FLEXMEM_LV_OK, "12 MB libres deberian ser OK");
  CHECK(flexMemHealth(&s) == FLEXMEM_H_GOOD, "12 MB libres: salud buena");

  s.psFree = MB(8); s.psLargest = MB(7);
  CHECK(flexMemLevel(&s) == FLEXMEM_LV_NOTICE, "8 MB libres -> aviso discreto");
  CHECK(flexMemHealth(&s) == FLEXMEM_H_WATCH, "8 MB libres: atencion");

  s.psFree = MB(5) + KB(512); s.psLargest = MB(5);
  CHECK(flexMemLevel(&s) == FLEXMEM_LV_WARN, "5,5 MB libres -> advertencia");

  s.psFree = MB(4); s.psLargest = MB(3);
  CHECK(flexMemLevel(&s) == FLEXMEM_LV_CRITICAL, "4 MB libres -> critico");
  CHECK(flexMemHealth(&s) == FLEXMEM_H_CRIT, "4 MB libres: salud critica");

  // Bloque contiguo insuficiente CON memoria de sobra: es critico igual.
  s = base();
  s.psLargest = KB(700);
  CHECK(flexMemLevel(&s) == FLEXMEM_LV_CRITICAL,
        "12 MB libres pero sin bloque de 1 MB tiene que ser critico");

  // SRAM interna en zona peligrosa manda sobre la PSRAM.
  s = base();
  s.inFree = KB(20);
  CHECK(flexMemLevel(&s) == FLEXMEM_LV_CRITICAL, "SRAM interna a 20 KB -> critico");

  // Placa sin PSRAM: esta escala no aplica y no se inventa una.
  memset(&s, 0, sizeof(s));
  CHECK(flexMemLevel(&s) == FLEXMEM_LV_OK, "sin PSRAM medida no hay presion que reportar");
  CHECK(flexMemUsedPct(&s) == 0, "sin PSRAM el porcentaje es 0, no una division por cero");
}

// =============================================================
static void testUsage(){
  std::printf("-- uso y porcentajes --\n");
  FlexMemSnap s = base();
  CHECK(flexMemUsed(&s) == MB(20), "32 - 12 = 20 MB en uso");
  CHECK(flexMemUsedPct(&s) == 62, "20/32 = 62%%, salio %d", flexMemUsedPct(&s));

  // Libre > total (no deberia pasar, pero no puede desbordar).
  s.psFree = MB(40);
  CHECK(flexMemUsed(&s) == 0, "libre > total no puede dar un uso negativo enorme");

  s = base();
  CHECK(flexMemFlashPct(&s) == 25, "2/8 = 25%%");
  s.fsValid = 0;
  CHECK(flexMemFlashPct(&s) == -1, "sin medida de flash se devuelve -1 (no disponible)");
}

// =============================================================
static void testFrag(){
  std::printf("-- fragmentacion --\n");
  FlexMemSnap s = base();
  s.psFree = MB(8); s.psLargest = MB(8);
  CHECK(flexMemFragPct(&s) == 0, "todo el hueco libre en una pieza -> 0%%");
  CHECK(flexMemFragClass(&s) == FLEXMEM_FRAG_LOW, "0%% es fragmentacion baja");

  s.psLargest = MB(6);
  CHECK(flexMemFragPct(&s) == 25, "6 de 8 -> 25%%, salio %d", flexMemFragPct(&s));
  CHECK(flexMemFragClass(&s) == FLEXMEM_FRAG_LOW, "25%% sigue siendo baja");

  s.psLargest = MB(4);
  CHECK(flexMemFragClass(&s) == FLEXMEM_FRAG_MED, "50%% es media");

  s.psLargest = KB(512);
  CHECK(flexMemFragPct(&s) == 94, "512 KB de 8 MB -> 94%%, salio %d", flexMemFragPct(&s));
  CHECK(flexMemFragClass(&s) == FLEXMEM_FRAG_HIGH, "93%% es alta");

  s.psFree = 0; s.psLargest = 0;
  CHECK(flexMemFragPct(&s) == 0, "sin memoria libre no se divide por cero");
}

// =============================================================
static void testCanOpen(){
  std::printf("-- veredicto de apertura --\n");
  FlexMemSnap s = base();
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_LIGHT)  == FLEXMEM_OK, "ligera con 12 MB: si");
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_MEDIUM) == FLEXMEM_OK, "media con 12 MB: si");
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_HEAVY)  == FLEXMEM_OK, "pesada con 12 MB: si");

  // Banda 5-6 MB: la pesada exige soltar antes (reserva de 6 MB del sistema).
  s.psFree = MB(5) + KB(512); s.psLargest = MB(5);
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_HEAVY) == FLEXMEM_NEED_SHED,
        "pesada entre 5 y 6 MB: primero soltar recursos");
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_LIGHT) == FLEXMEM_OK,
        "una ligera NUNCA se bloquea: es lo que hace falta para arreglar el apuro");

  // Por debajo del suelo duro no se abre una pesada ni soltando.
  s.psFree = MB(4); s.psLargest = MB(3);
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_HEAVY) == FLEXMEM_DENY_PSRAM,
        "pesada con 4 MB libres: proteccion");
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_MEDIUM) == FLEXMEM_DENY_PSRAM,
        "media con 4 MB libres: proteccion");
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_LIGHT) == FLEXMEM_OK, "ligera con 4 MB: si");

  // Bloque contiguo: con memoria de sobra se pide soltar; sin ella, se niega.
  s = base();
  s.psFree = MB(12); s.psLargest = KB(600);
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_HEAVY) == FLEXMEM_NEED_SHED,
        "12 MB en trozos: soltar puede devolver un hueco grande");
  s.psFree = MB(6); s.psLargest = KB(600);
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_HEAVY) == FLEXMEM_DENY_BLOCK,
        "6 MB y ningun hueco de 1 MB: no hay nada que soltar que lo arregle");

  // SRAM interna
  s = base();
  s.inFree = KB(20);
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_HEAVY) == FLEXMEM_DENY_SRAM,
        "SRAM interna peligrosa: se niega y se dice que es la SRAM");
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_LIGHT) == FLEXMEM_OK,
        "una ligera sigue abriendose con la SRAM justa");

  // Sin PSRAM medida (placa sin PSRAM): no se inventa una politica.
  FlexMemSnap z; memset(&z, 0, sizeof(z));
  CHECK(flexMemCanOpen(&z, FLEXMEM_W_HEAVY) == FLEXMEM_OK,
        "sin PSRAM medida no se bloquea nada");

  // El colchon: 32 MB con 7 MB libres de una pieza. 3 MB de coste + 5 de
  // suelo = 8 > 7, asi que toca soltar primero.
  s = base();
  s.psFree = MB(7); s.psLargest = MB(7);
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_HEAVY) == FLEXMEM_NEED_SHED,
        "7 MB no cubren coste + suelo de una pesada");
  CHECK(flexMemCanOpen(&s, FLEXMEM_W_MEDIUM) == FLEXMEM_OK,
        "una media si cabe con 7 MB");
}

// =============================================================
static void testAlerts(){
  std::printf("-- avisos y enfriamiento --\n");
  FlexMemAlerts a;
  flexMemAlertsReset(&a);
  FlexMemSnap s = base();
  uint32_t t = 1000;

  // Sin nada que decir.
  CHECK(flexMemAlertPick(&s, 0, t, &a) == FLEXMEM_AL_NONE, "con 12 MB no hay nada que avisar");

  // 8 MB -> aviso discreto, una sola vez.
  s.psFree = MB(8); s.psLargest = MB(7);
  CHECK(flexMemAlertPick(&s, 0, t, &a) == FLEXMEM_AL_PS_NOTICE, "8 MB -> aviso discreto");
  t += 1000;
  CHECK(flexMemAlertPick(&s, 0, t, &a) == FLEXMEM_AL_NONE,
        "el mismo aviso no puede repetirse un segundo despues");
  t += FLEXMEM_CD_MEM;
  CHECK(flexMemAlertPick(&s, 0, t, &a) == FLEXMEM_AL_PS_NOTICE, "tras el enfriamiento vuelve a poder darse");

  // Escalada: el critico es otra clase, con su propio enfriamiento, pero la
  // separacion global lo retiene un momento.
  flexMemAlertsReset(&a);
  t = 100000;
  s.psFree = MB(8);
  CHECK(flexMemAlertPick(&s, 0, t, &a) == FLEXMEM_AL_PS_NOTICE, "primero el discreto");
  s.psFree = MB(4); s.psLargest = MB(3);
  CHECK(flexMemAlertPick(&s, 0, t + 1000, &a) == FLEXMEM_AL_NONE,
        "separacion global: no se encadenan dos tarjetas seguidas");
  CHECK(flexMemAlertPick(&s, 0, t + FLEXMEM_CD_GLOBAL + 1, &a) == FLEXMEM_AL_PS_CRIT,
        "pasada la separacion, el critico si sale (es otra clase)");

  // Fragmentacion: solo con memoria de sobra.
  flexMemAlertsReset(&a);
  s = base();
  s.psFree = MB(12); s.psLargest = KB(400);
  // Con ese bloque el nivel es CRITICO, asi que gana el aviso de memoria.
  CHECK(flexMemAlertPick(&s, 0, 500000, &a) == FLEXMEM_AL_PS_CRIT,
        "sin bloque contiguo manda el aviso critico, no el de fragmentacion");
  flexMemAlertsReset(&a);
  s.psLargest = MB(2);          // hay bloque de 1 MB: ya no es critico
  CHECK(flexMemFragClass(&s) == FLEXMEM_FRAG_HIGH, "12 MB con mayor hueco de 2 MB: fragmentacion alta");
  CHECK(flexMemAlertPick(&s, 0, 500000, &a) == FLEXMEM_AL_FRAG, "ahora si toca el de fragmentacion");

  // SRAM baja
  flexMemAlertsReset(&a);
  s = base();
  s.inFree = KB(50);            // por debajo de LOW pero por encima de MIN
  CHECK(flexMemAlertPick(&s, 0, 600000, &a) == FLEXMEM_AL_SRAM, "SRAM baja tiene su propio aviso");

  // Flash
  flexMemAlertsReset(&a);
  s = base();
  s.fsUsed = MB(7);             // 87%
  CHECK(flexMemAlertPick(&s, 0, 700000, &a) == FLEXMEM_AL_FLASH80, "flash al 87%% -> aviso de 80");
  flexMemAlertsReset(&a);
  s.fsUsed = MB(8) - KB(200);   // ~97%
  CHECK(flexMemAlertPick(&s, 0, 700000, &a) == FLEXMEM_AL_FLASH90, "flash al 97%% -> aviso de 90");
  flexMemAlertsReset(&a);
  s.fsValid = 0;
  CHECK(flexMemAlertPick(&s, 0, 700000, &a) == FLEXMEM_AL_NONE,
        "sin medida de flash no se avisa de flash");

  // Tarjeta con error: el estado se lo pasa el llamante, aqui no se sondea nada.
  flexMemAlertsReset(&a);
  s = base();
  CHECK(flexMemAlertPick(&s, 1, 800000, &a) == FLEXMEM_AL_SD_ERR, "tarjeta en error -> su aviso");
  CHECK(flexMemAlertPick(&s, 1, 800000 + FLEXMEM_CD_GLOBAL + 1, &a) == FLEXMEM_AL_NONE,
        "no se repite mientras dure su enfriamiento");
  CHECK(flexMemAlertPick(&s, 1, 800000 + FLEXMEM_CD_SD + 1, &a) == FLEXMEM_AL_SD_ERR,
        "pasado su enfriamiento puede volver a decirse");

  // El reloj da la vuelta (49,7 dias): la resta sin signo sigue siendo correcta.
  flexMemAlertsReset(&a);
  s = base(); s.psFree = MB(8); s.psLargest = MB(7);
  uint32_t near = 0xFFFFF000u;
  CHECK(flexMemAlertPick(&s, 0, near, &a) == FLEXMEM_AL_PS_NOTICE, "aviso justo antes de la vuelta");
  uint32_t after = near + FLEXMEM_CD_MEM;        // desborda a proposito
  CHECK(flexMemAlertPick(&s, 0, after, &a) == FLEXMEM_AL_PS_NOTICE,
        "el enfriamiento sigue midiendo bien despues de que millis() de la vuelta");
}

// =============================================================
static void testFmt(){
  std::printf("-- formato de cifras --\n");
  char b[32];
  flexMemFmt(0, b, sizeof(b));                 CHECK(!strcmp(b, "0 B"), "0 -> '%s'", b);
  flexMemFmt(900, b, sizeof(b));               CHECK(!strcmp(b, "900 B"), "900 -> '%s'", b);
  flexMemFmt(KB(642), b, sizeof(b));           CHECK(!strcmp(b, "642 KB"), "642 KB -> '%s'", b);
  flexMemFmt(MB(8) + KB(410), b, sizeof(b));   CHECK(!strcmp(b, "8.4 MB"), "8,4 MB -> '%s'", b);
  flexMemFmt(MB(32), b, sizeof(b));            CHECK(!strcmp(b, "32.0 MB"), "32 MB -> '%s'", b);
  flexMemFmt(1024ull * 1024ull * 1024ull * 2ull, b, sizeof(b));
  CHECK(!strcmp(b, "2.0 GB"), "2 GB -> '%s'", b);

  flexMemFmtPair(MB(7) + KB(640), MB(32), b, sizeof(b));
  CHECK(!strcmp(b, "7.6 MB / 32.0 MB"), "par -> '%s'", b);

  // Nunca escribe fuera del buffer que le dan.
  char tiny[4];
  memset(tiny, 'x', sizeof(tiny));
  flexMemFmt(MB(8), tiny, sizeof(tiny));
  CHECK(tiny[3] == 0, "el buffer corto queda terminado en 0");
}

// =============================================================
int main(){
  std::printf("\n=== FlexOS · presupuesto de memoria y avisos ===\n");
  testLevels();
  testUsage();
  testFrag();
  testCanOpen();
  testAlerts();
  testFmt();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
