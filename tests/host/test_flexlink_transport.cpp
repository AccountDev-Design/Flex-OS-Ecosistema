// #############################################################
//  test_flexlink_transport.cpp  ·  maquina de estados del enlace
//  (FlexOS_FlexPhone_Link.cpp)
// #############################################################
//
//  Lo que se comprueba aqui es sobre todo lo que el enlace se NIEGA
//  a hacer: declararse conectado sin sesion, emparejar con una sola
//  confirmacion, aceptar tramas repetidas o de otra sesion, y
//  reintentar para siempre.
//
//  En el PC la capacidad es FLP_LINK_CAP_NONE (no hay radio), igual
//  que en el P4 sin el C6 preparado. Para poder ejercitar la
//  maquina de estados se fuerza la capacidad a mano tras el init:
//  es EXACTAMENTE lo que hara una placa con BLE disponible.

#include "../../FlexOS_FlexPhone_Link.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static FlexPhoneLink  L;
static FlexPhoneModel M;

// Construye una trama valida y se la entrega al enlace.
static bool feed(uint8_t type, const uint8_t* body, size_t bodyN,
                 uint32_t counter, uint32_t nowMs, uint16_t session){
  uint8_t frame[FLNK_MAX_FRAME];
  FlexLinkHeader h; std::memset(&h, 0, sizeof(h));
  h.version = FLNK_VERSION; h.type = type;
  h.session = session; h.packet = (uint16_t)counter;
  h.frag = 0; h.fragCount = 1; h.counter = counter;
  const int n = flexLinkWriteFrame(frame, sizeof(frame), &h, body, bodyN);
  if(n <= 0) return false;
  return flexPhoneLinkOnFrame(&L, &M, frame, (size_t)n, nowMs);
}

// Arranca el enlace forzando la capacidad, como una placa con BLE.
static void bring(uint32_t nowMs){
  flexPhoneLinkInit(&L);
  flexPhoneModelInit(&M);
  L.cap = FLP_LINK_CAP_LOCAL;     // simula una placa CON radio
  L.state = FLP_LS_OFF;
  CHECK(flexPhoneLinkStart(&L), "no arranco con capacidad disponible");
  CHECK(L.state == FLP_LS_ADVERTISING, "estado tras arrancar: %s",
        flexPhoneLinkStateName(L.state));
  (void)nowMs;
}

// -------------------------------------------------------------
//  1) Honestidad sobre el hardware
// -------------------------------------------------------------
static void testCapability(){
  std::printf("[link] capacidad: en el PC/P4 sin C6 NO hay BLE, y se dice\n");
  flexPhoneLinkInit(&L);
  flexPhoneModelInit(&M);
  // El host no tiene SOC_BLE_SUPPORTED: misma situacion que el P4.
  CHECK(flexPhoneLinkCap() == FLP_LINK_CAP_NONE,
        "el host no deberia declarar BLE (cap=%u)", flexPhoneLinkCap());
  CHECK(L.state == FLP_LS_UNAVAILABLE, "estado inicial %s", flexPhoneLinkStateName(L.state));
  CHECK(!flexPhoneLinkAvailable(), "se declaro disponible sin radio");
  CHECK(!flexPhoneLinkStart(&L), "arranco un enlace imposible");
  CHECK(L.state == FLP_LS_UNAVAILABLE, "cambio de estado pese a no poder");
  CHECK(L.err[0] != 0, "no explico por que no hay BLE");
  CHECK(std::strstr(flexPhoneLinkCapReason(), "C6") != nullptr,
        "el motivo deberia mencionar el C6");
  // Y lo mas importante: NUNCA se declara listo.
  CHECK(!flexPhoneLinkReady(&L), "se declaro CONECTADO sin hardware");
  std::printf("   motivo: %.70s...\n", flexPhoneLinkCapReason());
}

// -------------------------------------------------------------
//  2) Emparejamiento: hacen falta LAS DOS confirmaciones
// -------------------------------------------------------------
static void testPairing(){
  std::printf("[link] emparejamiento: una sola confirmacion NO basta\n");
  bring(1000);

  flexPhoneLinkBeginPairing(&L, 123456789u, 1000);
  CHECK(L.state == FLP_LS_PAIRING, "no entro en emparejamiento");
  CHECK(std::strlen(L.code) == FLP_LINK_CODE_LEN, "codigo de %u digitos", (unsigned)std::strlen(L.code));
  for(int i = 0; i < FLP_LINK_CODE_LEN; i++)
    CHECK(L.code[i] >= '0' && L.code[i] <= '9', "el codigo tiene un caracter no numerico");

  // Solo el usuario de Flex OS: NO empareja.
  flexPhoneLinkConfirm(&L, 1100);
  CHECK(!flexPhoneLinkPairComplete(&L), "emparejo con una sola confirmacion");
  CHECK(L.state == FLP_LS_PAIRING, "salio de emparejamiento antes de tiempo");
  CHECK(!flexPhoneLinkReady(&L), "se declaro conectado a medio emparejar");

  // Ahora confirma el telefono: ya si.
  CHECK(feed(FLNK_T_PAIR_CONFIRM, nullptr, 0, 1, 1200, 0), "no acepto la confirmacion del telefono");
  CHECK(flexPhoneLinkPairComplete(&L), "no completo con las dos confirmaciones");
  CHECK(L.state == FLP_LS_READY, "no quedo listo (%s)", flexPhoneLinkStateName(L.state));
  CHECK(L.bonded, "no marco el vinculo");
  CHECK(L.code[0] == 0, "dejo el codigo a la vista tras emparejar");
  CHECK(L.session != 0, "no abrio sesion");

  // El codigo CADUCA si nadie confirma.
  bring(0);
  flexPhoneLinkBeginPairing(&L, 42u, 1000);
  flexPhoneLinkTick(&L, &M, 1000 + FLP_LINK_PAIR_WINDOW_MS + 1);
  CHECK(L.state == FLP_LS_ADVERTISING, "el emparejamiento no caduco");
  CHECK(L.code[0] == 0, "dejo el codigo caducado en pantalla");
}

// -------------------------------------------------------------
//  3) Tramas hostiles llegando al enlace
// -------------------------------------------------------------
static void testHostileFrames(){
  std::printf("[link] tramas hostiles: repetida, de otra sesion, corrupta y de version futura\n");
  bring(0);
  flexPhoneLinkBeginPairing(&L, 1u, 1000);
  flexPhoneLinkConfirm(&L, 1000);
  feed(FLNK_T_PAIR_CONFIRM, nullptr, 0, 1, 1000, 0);
  CHECK(L.state == FLP_LS_READY, "no quedo listo para la prueba");
  const uint16_t sess = L.session;

  // Notificacion buena.
  uint8_t body[256];
  FlexPhoneNotif nt; std::memset(&nt, 0, sizeof(nt));
  nt.id = 7; std::snprintf(nt.pkg, sizeof(nt.pkg), "com.whatsapp");
  std::snprintf(nt.title, sizeof(nt.title), "Ana");
  std::snprintf(nt.text, sizeof(nt.text), "hola");
  const int bn = flexPhoneEncNotif(body, sizeof(body), &nt);
  CHECK(bn > 0, "no se codifico la notificacion de prueba");

  CHECK(feed(FLNK_T_NOTIF_ADD, body, (size_t)bn, 10, 2000, sess), "no acepto una notificacion buena");
  CHECK(M.notifCount == 1, "no la guardo (%u)", M.notifCount);

  // -- REPETIDA: mismo contador -> descartada, sin duplicar --
  const uint32_t dropped0 = L.nDropped;
  CHECK(!feed(FLNK_T_NOTIF_ADD, body, (size_t)bn, 10, 2100, sess), "acepto una trama REPETIDA");
  CHECK(L.nDropped == dropped0 + 1, "no conto el descarte");
  CHECK(M.notifCount == 1, "la repetida duplico la notificacion");

  // -- DE OTRA SESION: no se acepta --
  CHECK(!feed(FLNK_T_NOTIF_ADD, body, (size_t)bn, 11, 2200, (uint16_t)(sess ^ 0x5A5A)),
        "acepto una trama de OTRA sesion");
  CHECK(M.notifCount == 1, "la trama ajena entro igualmente");

  // -- CORRUPTA: se cuenta como paquete malo --
  uint8_t frame[FLNK_MAX_FRAME];
  FlexLinkHeader h; std::memset(&h, 0, sizeof(h));
  h.version = FLNK_VERSION; h.type = FLNK_T_NOTIF_ADD; h.session = sess;
  h.packet = 20; h.fragCount = 1; h.counter = 20;
  int fn = flexLinkWriteFrame(frame, sizeof(frame), &h, body, (size_t)bn);
  frame[FLNK_HDR_SIZE + 1] ^= 0xFF;                  // rompe la carga
  const uint32_t bad0 = L.nBad;
  CHECK(!flexPhoneLinkOnFrame(&L, &M, frame, (size_t)fn, 2300), "acepto una trama corrupta");
  CHECK(L.nBad == bad0 + 1, "no conto el paquete malo");
  CHECK(M.stats.badPacket > 0, "el modelo no se entero del paquete malo");

  // -- VERSION FUTURA: se rechaza y se avisa al telefono --
  FlexLinkHeader hv; std::memset(&hv, 0, sizeof(hv));
  hv.version = FLNK_VERSION + 1; hv.type = FLNK_T_NOTIF_ADD; hv.session = sess;
  hv.packet = 30; hv.fragCount = 1; hv.counter = 30;
  fn = flexLinkWriteFrame(frame, sizeof(frame), &hv, nullptr, 0);
  CHECK(!flexPhoneLinkOnFrame(&L, &M, frame, (size_t)fn, 2400), "acepto una version futura");

  // -- TIPO DESCONOCIDO: se ignora sin romper (compatibilidad futura) --
  CHECK(feed(0xEE, nullptr, 0, 40, 2500, sess), "un tipo desconocido no deberia ser un fallo");
  CHECK(L.state == FLP_LS_READY, "un tipo desconocido tumbo el enlace");

  // -- Cualquier basura de cualquier longitud: ni cuelga ni desborda --
  uint8_t junk[FLNK_MAX_FRAME];
  for(size_t n = 0; n <= sizeof(junk); n++){
    for(size_t i = 0; i < n; i++) junk[i] = (uint8_t)(i * 31 + n);
    (void)flexPhoneLinkOnFrame(&L, &M, junk, n, 3000);
  }
  std::printf("   %u longitudes de basura procesadas sin desbordar\n", (unsigned)sizeof(junk) + 1);
  CHECK(L.state == FLP_LS_READY, "la basura tumbo el enlace");
}

// -------------------------------------------------------------
//  4) Desconexion y reconexion
// -------------------------------------------------------------
static void testDisconnect(){
  std::printf("[link] desconexion: el estado del telefono deja de darse por cierto\n");
  bring(0);
  flexPhoneLinkBeginPairing(&L, 1u, 1000);
  flexPhoneLinkConfirm(&L, 1000);
  feed(FLNK_T_PAIR_CONFIRM, nullptr, 0, 1, 1000, 0);
  const uint16_t sess = L.session;

  // Llega estado real del telefono.
  uint8_t body[64];
  FlexLinkWr w; flexLinkWrInit(&w, body, sizeof(body));
  flexLinkWrStr(&w, "Pixel 8", 24); flexLinkWrU8(&w, 77);
  flexLinkWrU8(&w, 0x01); flexLinkWrU8(&w, FLP_NET_WIFI);
  CHECK(feed(FLNK_T_PHONE_STATE, body, w.at, 50, 2000, sess), "no acepto el estado");
  CHECK(M.phone.valid && M.phone.battery == 77, "no guardo el estado real");

  // -- BYE: despedida limpia --
  CHECK(feed(FLNK_T_BYE, nullptr, 0, 51, 2100, sess), "no acepto la despedida");
  CHECK(L.state == FLP_LS_ADVERTISING, "tras BYE deberia volver a anunciar");
  CHECK(!flexPhoneLinkReady(&L), "sigue diciendo que hay telefono tras BYE");
  CHECK(!M.phone.valid, "conservo un estado de telefono que ya no es cierto");
  CHECK(M.phone.battery == 255, "dejo la bateria antigua en pantalla");

  // -- ENLACE MUERTO: silencio prolongado --
  bring(0);
  flexPhoneLinkBeginPairing(&L, 1u, 1000);
  flexPhoneLinkConfirm(&L, 1000);
  feed(FLNK_T_PAIR_CONFIRM, nullptr, 0, 1, 1000, 0);
  feed(FLNK_T_PHONE_STATE, body, w.at, 60, 2000, L.session);
  CHECK(M.phone.valid, "no guardo el estado antes de la caida");
  flexPhoneLinkTick(&L, &M, 2000 + FLP_LINK_DEAD_MS + 1);
  CHECK(L.state == FLP_LS_ADVERTISING, "no detecto el enlace muerto");
  CHECK(!M.phone.valid, "mantuvo el estado de un telefono que ya no responde");
  CHECK(M.stats.reconnects == 1, "no conto la reconexion");
  CHECK(L.err[0] != 0, "no explico la caida");

  // -- El envio se NIEGA sin sesion --
  CHECK(!flexPhoneLinkSend(&L, FLNK_T_MEDIA_CMD, nullptr, 0, false),
        "dejo enviar una orden sin telefono conectado");
}

// -------------------------------------------------------------
//  5) La cola de salida no crece ni bloquea
// -------------------------------------------------------------
static void testTxQueue(){
  std::printf("[link] cola de salida: acotada, no bloquea, reintentos con limite\n");
  bring(0);
  flexPhoneLinkBeginPairing(&L, 1u, 1000);
  flexPhoneLinkConfirm(&L, 1000);
  feed(FLNK_T_PAIR_CONFIRM, nullptr, 0, 1, 1000, 0);

  // Un tick primero: el PAIR_CONFIRM que encolo el emparejamiento ya
  // ha salido, asi que la cola arranca de verdad vacia.
  flexPhoneLinkTick(&L, &M, 1500);
  for(int i = 0; i < FLP_LINK_TXQ; i++)
    CHECK(!L.tx[i].used, "la cola no quedo vacia tras el primer tick");

  uint8_t cmd[1] = { FLP_MCMD_PLAY };
  int accepted = 0;
  for(int i = 0; i < FLP_LINK_TXQ * 3; i++)
    if(flexPhoneLinkSend(&L, FLNK_T_MEDIA_CMD, cmd, 1, true)) accepted++;
  CHECK(accepted == FLP_LINK_TXQ, "acepto %d mensajes con una cola de %d", accepted, FLP_LINK_TXQ);
  CHECK(L.nDropped > 0, "no conto los descartes por cola llena");

  // Una carga mayor que el buffer se rechaza, no se trunca en silencio.
  static uint8_t big[FLP_LINK_TXBUF + 64];
  std::memset(big, 'x', sizeof(big));
  CHECK(!flexPhoneLinkSend(&L, FLNK_T_REPLY_REQ, big, sizeof(big), true),
        "acepto una carga mayor que el buffer de salida");

  // Los reintentos se AGOTAN: nada se reintenta para siempre.
  uint32_t t = 2000;
  for(int i = 0; i < FLNK_RETRY_MAX + 4; i++){
    t += FLNK_RETRY_CAP_MS + 1000;
    flexPhoneLinkTick(&L, &M, t);
  }
  int stillQueued = 0;
  for(int i = 0; i < FLP_LINK_TXQ; i++) if(L.tx[i].used) stillQueued++;
  CHECK(stillQueued == 0, "quedan %d mensajes reintentandose para siempre", stillQueued);
  CHECK(L.nTimeouts > 0, "no conto los tiempos agotados");
}

// -------------------------------------------------------------
//  6) Desvincular borra el estado
// -------------------------------------------------------------
static void testForget(){
  std::printf("[link] desvincular: se olvida el vinculo y se corta la sesion\n");
  bring(0);
  flexPhoneLinkBeginPairing(&L, 1u, 1000);
  flexPhoneLinkConfirm(&L, 1000);
  feed(FLNK_T_PAIR_CONFIRM, nullptr, 0, 1, 1000, 0);
  CHECK(L.bonded && L.state == FLP_LS_READY, "no quedo vinculado para la prueba");

  flexPhoneLinkForget(&L);
  CHECK(!L.bonded, "sigue vinculado tras desvincular");
  CHECK(L.session == 0, "conservo la sesion");
  CHECK(!flexPhoneLinkReady(&L), "sigue diciendo que hay telefono");
  CHECK(L.txCounter == 0, "no reinicio el contador de salida");
  // Tras olvidar, una trama de la sesion vieja ya no vale.
  CHECK(!feed(FLNK_T_NOTIF_CLEAR, nullptr, 0, 99, 5000, 1234),
        "acepto trafico de la sesion olvidada");
}

int main(){
  std::printf("\n=== FlexOS · transporte del enlace Flex Phone ===\n");
  testCapability();
  testPairing();
  testHostileFrames();
  testDisconnect();
  testTxQueue();
  testForget();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
