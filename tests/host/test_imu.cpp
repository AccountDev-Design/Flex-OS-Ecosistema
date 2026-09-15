// #############################################################
//  FLEX OS  ·  PRUEBA DEL DRIVER DEL GY-BNO085 CONTRA UN BUS SIMULADO
//  ------------------------------------------------------------
//  Esta prueba existe por un fallo concreto y grave: con el modulo IMU
//  conectado y el sistema en marcha, RETIRARLO fisicamente congelaba
//  Flex OS Ultra -- el tactil dejaba de responder y la interfaz con el.
//
//  La causa no estaba en la interfaz. El BNO085 cuelga del MISMO bus I2C
//  que el GT911 tactil y se lee desde el MISMO hilo que el bucle del
//  sistema. Cuando el modulo desaparece, cada transaccion contra ese bus
//  se va al plazo de espera del driver, y flexBnoTick() seguia leyendo en
//  CADA vuelta -- tambien despues de dar el sensor por perdido. El bucle
//  se quedaba sin vueltas utiles: eso es lo que se veia como "congelado".
//
//  Aqui se compila el driver DE VERDAD (FlexOS_BNO085.cpp) contra un bus
//  simulado que sabe estar sano, vacio, trabado o devolviendo basura, y se
//  comprueba lo unico que de verdad importa: que el driver NUNCA gaste mas
//  de su presupuesto en una vuelta, y que con el sensor perdido no toque
//  el bus en absoluto -- que es lo que deja el tactil vivo.
// #############################################################
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "bnostub/Arduino.h"
#include "bnostub/Wire.h"
#include "../../FlexOS_Ultra/FlexOS_BNO085.h"

// ---- Entorno simulado ----
uint32_t gFakeMs       = 1000;
int      gWireMode     = WIRE_PRESENT;
uint32_t gWireTx       = 0;
uint32_t gWireMsCost   = 0;
uint16_t gWireTimeoutMs = 50;      // el valor por defecto de Arduino, hasta que el sistema lo baje
uint8_t  gBnoI2cAddr   = 0x4A;
TwoWire  Wire;

static int gChecks = 0, gFails = 0;
static void ok(const char* what, bool cond){
  gChecks++;
  if(!cond){ gFails++; printf("  FALLO: %s\n", what); }
}

// #############################################################
//  EL SENSOR SIMULADO  ·  SHTP de verdad
// #############################################################
#define SH2_RSP_PRODUCT_ID  0xF1
#define SH2_CMD_PRODUCT_ID  0xF9
#define SH2_CMD_SET_FEATURE 0xFD

static uint8_t  fbPkt[320];       // paquete pendiente de entregar (con su cabecera)
static size_t   fbPktLen = 0, fbPktPos = 0;
static uint8_t  fbFeature[0x20];  // periodo != 0 => informe activado
static bool     fbIdent  = false; // el maestro ya pidio el Product ID

void fakeBnoReset(){
  fbPktLen = fbPktPos = 0;
  memset(fbFeature, 0, sizeof(fbFeature));
  fbIdent = false;
}

// Encola un paquete SHTP completo (cabecera + carga)
static void fbQueue(uint8_t ch, const uint8_t* body, size_t n){
  if(fbPktLen > fbPktPos) return;            // ya hay uno a medio entregar
  size_t total = n + 4;
  if(total > sizeof(fbPkt)) return;
  fbPkt[0] = (uint8_t)(total & 0xFF);
  fbPkt[1] = (uint8_t)((total >> 8) & 0x7F);
  fbPkt[2] = ch;
  fbPkt[3] = 0;
  memcpy(fbPkt + 4, body, n);
  fbPktLen = total; fbPktPos = 0;
}

void fakeBnoOnWrite(const uint8_t* p, size_t n){
  if(n < 5) return;                          // cabecera + al menos un byte
  uint8_t ch = p[2];
  const uint8_t* pay = p + 4;
  size_t payN = n - 4;
  if(ch == 2 && payN >= 1 && pay[0] == SH2_CMD_PRODUCT_ID){
    uint8_t rsp[16]; memset(rsp, 0, sizeof(rsp));
    rsp[0] = SH2_RSP_PRODUCT_ID; rsp[2] = 3; rsp[3] = 2;   // version 3.2
    fbQueue(2, rsp, sizeof(rsp));
    fbIdent = true;
    return;
  }
  if(ch == 2 && payN >= 9 && pay[0] == SH2_CMD_SET_FEATURE){
    uint8_t rpt = pay[1];
    uint32_t us = (uint32_t)pay[5] | ((uint32_t)pay[6] << 8) |
                  ((uint32_t)pay[7] << 16) | ((uint32_t)pay[8] << 24);
    if(rpt < sizeof(fbFeature)) fbFeature[rpt] = us ? 1 : 0;
    return;
  }
}

// Un informe de entrada por cada sensor activado, en un solo paquete del
// canal 3, con su cabecera de tiempo delante -- igual que el sensor real.
bool fakeBnoQueueReports(){
  uint8_t b[64]; size_t n = 0;
  b[n++] = 0xFB; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0;   // base timestamp
  const uint8_t ids[4] = { 0x01, 0x02, 0x03, 0x05 };
  const uint8_t lens[4] = { 10, 10, 10, 14 };
  bool any = false;
  for(int i = 0; i < 4; i++){
    if(!fbFeature[ids[i]]) continue;
    if(n + lens[i] > sizeof(b)) break;
    memset(b + n, 0, lens[i]);
    b[n] = ids[i];                 // id
    b[n + 2] = 3;                  // estado: precision alta
    if(ids[i] == 0x05) b[n + 10] = 0x40;   // real = 1.0 en Q14
    else               b[n + 8] = 0x00;
    n += lens[i];
    any = true;
  }
  if(!any) return false;
  fbQueue(3, b, n);
  return true;
}

size_t fakeBnoRead(uint8_t* out, size_t want){
  // Sin paquete pendiente: el sensor contesta una cabecera de longitud cero.
  // Esto NO es un fallo de bus y el driver tiene que distinguirlo.
  if(fbPktPos >= fbPktLen){
    memset(out, 0, want);
    return want;
  }
  // Cada trozo repite la cabecera de 4 bytes; el driver la descarta.
  size_t i = 0;
  if(fbPktPos == 0){
    while(i < want && fbPktPos < fbPktLen) out[i++] = fbPkt[fbPktPos++];
  } else {
    for(int k = 0; k < 4 && i < want; k++) out[i++] = fbPkt[k];
    while(i < want && fbPktPos < fbPktLen) out[i++] = fbPkt[fbPktPos++];
  }
  while(i < want) out[i++] = 0;
  return want;
}

// ---- Utilidades de la prueba ----
static void busReset(){ gWireTx = 0; gWireMsCost = 0; }
// Una vuelta de loop(): mueve el reloj, publica informes a su cadencia y
// llama al tick del driver, exactamente como hace imuServiceTick().
static void loopOnce(uint32_t stepMs, bool feed){
  gFakeMs += stepMs;
  if(feed && gWireMode == WIRE_PRESENT) fakeBnoQueueReports();
  flexBnoTick(gFakeMs);
}

int main(){
  printf("=== FlexOS - GY-BNO085: el modulo se puede retirar en caliente ===\n");
  gWireTimeoutMs = 8;              // el que fija flexTouchInit() en la placa
  fakeBnoReset();

  // ---------------------------------------------------------------
  //  TEST B - ARRANQUE CON EL MODULO CONECTADO
  // ---------------------------------------------------------------
  printf("[B] arranque con el BNO085 conectado\n");
  gWireMode = WIRE_PRESENT;
  ok("flexBnoBegin encuentra el modulo", flexBnoBegin());
  ok("y queda sondeando, no listo de mentira", flexBnoState() == FLEXBNO_ST_PROBING);
  for(int i = 0; i < 200 && flexBnoState() != FLEXBNO_ST_READY; i++) loopOnce(10, true);
  ok("el dialogo SHTP llega a READY", flexBnoState() == FLEXBNO_ST_READY);
  ok("con el Product ID contestado", (flexBnoChecks() & FLEXBNO_CHK_I2C) != 0);
  ok("y los cuatro informes comprobados", (flexBnoChecks() & FLEXBNO_CHK_ALL) == FLEXBNO_CHK_ALL);
  ok("flexBnoAvailable() dice la verdad", flexBnoAvailable());
  { float q[4]; ok("hay cuaternion real", flexBnoQuat(q)); }

  // Un sensor SANO no puede activar el freno por "no hay informe todavia":
  // el bucle corre mas rapido que la cadencia del sensor y eso es lo normal.
  printf("[B] un sensor sano nunca se frena por vueltas sin informe\n");
  busReset();
  for(int i = 0; i < 60; i++) loopOnce(2, false);          // 120 ms sin publicar nada
  ok("sigue READY tras decenas de vueltas sin informe", flexBnoState() == FLEXBNO_ST_READY);
  loopOnce(2, true);
  uint32_t before = flexBnoReportCount();
  loopOnce(2, true);
  ok("y el primer informe siguiente se lee sin esperar",
     flexBnoReportCount() > before);

  // ---------------------------------------------------------------
  //  TEST C - DESCONEXION EN CALIENTE  (el test mas importante)
  // ---------------------------------------------------------------
  printf("[C] desconexion en caliente: el bus se traba y el driver se aparta\n");
  gWireMode = WIRE_WEDGED;                 // el modulo se va y deja SDA a masa
  busReset();
  uint32_t peor = 0;
  int vueltas = 0;
  for(; vueltas < 400 && flexBnoState() != FLEXBNO_ST_LOST; vueltas++){
    uint32_t t0 = gWireMsCost;
    loopOnce(10, false);
    uint32_t gasto = gWireMsCost - t0;
    if(gasto > peor) peor = gasto;
  }
  ok("el driver da el sensor por perdido", flexBnoState() == FLEXBNO_ST_LOST);
  ok("y lo hace en menos de 2 s de reloj", vueltas * 10 < 2000);
  // ESTE es el numero del fallo: lo que UNA vuelta del bucle podia irse en el
  // bus. El presupuesto del driver son 4 ms; con margen para la transaccion
  // que estuviera en marcha al agotarlo, el tope real es 4 ms + un plazo.
  ok("ninguna vuelta gasta mas que el presupuesto del driver mas una espera",
     peor <= (uint32_t)(4 + gWireTimeoutMs));
  printf("    peor vuelta durante la perdida: %u ms de bus\n", (unsigned)peor);

  // Y AHORA LO QUE ARREGLA EL CONGELAMIENTO: perdido el sensor, el driver
  // deja de tocar el bus. Cero transacciones = el tactil tiene el bus entero.
  busReset();
  for(int i = 0; i < 300; i++) loopOnce(10, false);        // 3 s de sistema en marcha
  ok("con el sensor perdido, el driver NO hace ni una transaccion I2C", gWireTx == 0);
  ok("ni gasta un solo ms de bus", gWireMsCost == 0);
  ok("y sigue diciendo que esta perdido", flexBnoState() == FLEXBNO_ST_LOST);

  // Soltar el servicio con el sensor perdido tampoco puede escribir en el bus:
  // apagarle los informes a un modulo que ya no esta son cuatro esperas.
  busReset();
  flexBnoStop();
  ok("flexBnoStop() sobre un sensor perdido no escribe en el bus", gWireTx == 0);

  // ---------------------------------------------------------------
  //  TEST A - ARRANQUE SIN MODULO
  // ---------------------------------------------------------------
  printf("[A] arranque sin modulo: barato y sin bloquear\n");
  gWireMode = WIRE_ABSENT;
  fakeBnoReset();
  busReset();
  ok("flexBnoBegin dice que no hay modulo", !flexBnoBegin());
  ok("el estado es 'no hay ninguno'", flexBnoState() == FLEXBNO_ST_ABSENT);
  ok("sondear el bus vacio cuesta dos transacciones", gWireTx == 2);
  busReset();
  for(int i = 0; i < 200; i++) loopOnce(10, false);
  ok("y sin modulo el tick no toca el bus jamas", gWireTx == 0);

  // ---------------------------------------------------------------
  //  BUS SUCIO: cabeceras imposibles no pueden parar el sistema
  // ---------------------------------------------------------------
  printf("[*] bus devolviendo 0xFF: una cabecera inventada no para el bucle\n");
  gWireMode = WIRE_PRESENT;
  fakeBnoReset();
  flexBnoRescan();
  for(int i = 0; i < 200 && flexBnoState() != FLEXBNO_ST_READY; i++) loopOnce(10, true);
  ok("el modulo vuelve a estar listo antes de ensuciar el bus",
     flexBnoState() == FLEXBNO_ST_READY);
  gWireMode = WIRE_GARBAGE;            // 0xFF => la cabecera SHTP dice 32763 bytes
  busReset();
  uint32_t peorTx = 0;
  for(int i = 0; i < 50; i++){
    uint32_t t0 = gWireTx;
    loopOnce(10, false);
    uint32_t n = gWireTx - t0;
    if(n > peorTx) peorTx = n;
  }
  // Sin el tope de longitud, UNA cabecera de 0xFF eran mas de mil
  // transacciones seguidas dentro de una sola llamada.
  ok("una vuelta nunca dispara mas de un punado de transacciones", peorTx <= 8);
  printf("    peor vuelta con el bus sucio: %u transacciones\n", (unsigned)peorTx);
  for(int i = 0; i < 400 && flexBnoState() != FLEXBNO_ST_LOST; i++) loopOnce(10, false);
  ok("y el bus sucio acaba en 'sensor perdido', no en un cuelgue",
     flexBnoState() == FLEXBNO_ST_LOST);

  // ---------------------------------------------------------------
  //  EL PEOR CASO: cabecera con canal VALIDO y longitud absurda
  //  ------------------------------------------------------------
  //  Un canal imposible lo descarta cualquier comprobacion. Lo que de verdad
  //  puede parar el sistema es una cabecera que PARECE valida -- canal 3,
  //  informes de entrada -- y anuncia 32763 bytes: vaciarla son mas de mil
  //  transacciones I2C dentro de UNA llamada, en el hilo del bucle.
  // ---------------------------------------------------------------
  printf("[*] cabecera de canal valido que anuncia 32763 bytes\n");
  gWireMode = WIRE_PRESENT;
  fakeBnoReset();
  flexBnoRescan();
  for(int i = 0; i < 200 && flexBnoState() != FLEXBNO_ST_READY; i++) loopOnce(10, true);
  ok("el modulo esta listo antes del paquete imposible", flexBnoState() == FLEXBNO_ST_READY);
  gWireMode = WIRE_BADLEN;
  busReset();
  peorTx = 0;
  for(int i = 0; i < 50; i++){
    uint32_t t0 = gWireTx;
    loopOnce(10, false);
    uint32_t n = gWireTx - t0;
    if(n > peorTx) peorTx = n;
  }
  ok("el tope de longitud corta el vaciado antes de empezarlo", peorTx <= 8);
  printf("    peor vuelta con la longitud imposible: %u transacciones\n", (unsigned)peorTx);
  for(int i = 0; i < 400 && flexBnoState() != FLEXBNO_ST_LOST; i++) loopOnce(10, false);
  ok("y tambien acaba en 'sensor perdido'", flexBnoState() == FLEXBNO_ST_LOST);

  // ---------------------------------------------------------------
  //  TEST D - RECONEXION
  // ---------------------------------------------------------------
  printf("[D] se vuelve a enchufar el modulo\n");
  gWireMode = WIRE_PRESENT;
  fakeBnoReset();
  busReset();
  flexBnoRescan();
  ok("el re-sondeo lo encuentra otra vez", flexBnoState() == FLEXBNO_ST_PROBING);
  for(int i = 0; i < 200 && flexBnoState() != FLEXBNO_ST_READY; i++) loopOnce(10, true);
  ok("vuelve a READY sin reiniciar nada mas", flexBnoState() == FLEXBNO_ST_READY);
  ok("y vuelve a haber orientacion", (flexBnoChecks() & FLEXBNO_CHK_FUSION) != 0);
  { float q[4]; ok("con cuaternion utilizable", flexBnoQuat(q)); }

  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
