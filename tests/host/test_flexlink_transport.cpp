// #############################################################
//  test_flexlink_transport.cpp  ·  maquina de estados del enlace
//  (FlexOS_FlexPhone_Link.cpp + FlexOS_FlexPhone_Transport.cpp)
// #############################################################
//
//  Lo que se comprueba aqui es sobre todo lo que el enlace se NIEGA
//  a hacer: declararse conectado sin autenticar, emparejar con una
//  sola confirmacion, aceptar tramas repetidas o de otra sesion,
//  aceptar notificaciones de quien no ha demostrado la clave, y
//  reintentar para siempre.
//
//  QUE APORTA EL TRANSPORTE DE LAZO
//  --------------------------------
//  Antes de esta version el enlace no tenia por donde hablar: la
//  cola de salida se vaciaba sin entregar nada a nadie. Aqui hay un
//  TELEFONO SIMULADO que habla el protocolo de verdad -- contesta
//  WELCOME, deriva la misma clave y devuelve su prueba --, asi que
//  el apreton de manos completo se ejecuta en el PC, byte a byte,
//  igual que lo hara sobre un socket.

#include "../../FlexOS_Ultra/FlexOS_FlexPhone_Link.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static FlexPhoneLink  L;
static FlexPhoneModel M;

// =============================================================
//  TRANSPORTE DE LAZO  +  TELEFONO SIMULADO
// =============================================================
// Dos colas: lo que Flex OS envia (toPhone) y lo que el telefono
// devuelve (toFlexOS). El "telefono" es una funcion que lee de la
// primera y escribe en la segunda.
struct Loop {
  FlexFrameRing toPhone;
  FlexFrameRing toFlexOS;
  uint8_t  st = FLP_TC_DOWN;
  bool     phoneAnswers = true;     // se puede apagar para simular un mudo
  // Estado del telefono simulado
  char     phoneId[FLP_PEERID_MAX] = "phone-sim";
  char     flexosId[FLXA_ID_MAX]   = {0};
  char     code[8]  = {0};          // el que el usuario "teclea"
  uint8_t  key[FLXA_KEY_SIZE]       = {0};
  bool     haveKey = false;
  uint8_t  nonce[FLXA_NONCE_SIZE]   = {0};
  uint32_t txCounter = 0;
  bool     gotHostProof = false;
  // REGISTRO DE LO QUE FLEX OS PUSO EN EL CANAL. El telefono
  // simulado vacia la cola al contestar, asi que sin esta copia no
  // se podria comprobar despues que el codigo no viajo.
  uint8_t  sniff[32][FLNK_MAX_FRAME];
  uint16_t sniffLen[32] = {0};
  int      sniffN = 0;
};
static Loop gLoop;

static bool loopStart(FlexPhoneTransport* t){
  Loop* L2 = (Loop*)t->ctx;
  flexRingInit(&L2->toPhone);
  flexRingInit(&L2->toFlexOS);
  L2->st = FLP_TC_OPEN;
  return true;
}
static void loopStop(FlexPhoneTransport* t){ ((Loop*)t->ctx)->st = FLP_TC_DOWN; }
static uint8_t loopState(FlexPhoneTransport* t){ return ((Loop*)t->ctx)->st; }
static uint16_t loopMtu(FlexPhoneTransport* t){ (void)t; return FLNK_MAX_FRAME; }
static int loopSend(FlexPhoneTransport* t, const uint8_t* f, size_t n){
  Loop* L2 = (Loop*)t->ctx;
  if(L2->st != FLP_TC_OPEN) return FLP_TR_ECLOSED;
  if(L2->sniffN < 32 && n <= FLNK_MAX_FRAME){
    std::memcpy(L2->sniff[L2->sniffN], f, n);
    L2->sniffLen[L2->sniffN] = (uint16_t)n;
    L2->sniffN++;
  }
  return flexRingPush(&L2->toPhone, f, n) ? FLP_TR_OK : FLP_TR_EAGAIN;
}
static int loopRecv(FlexPhoneTransport* t, uint8_t* out, size_t cap){
  Loop* L2 = (Loop*)t->ctx;
  if(L2->st != FLP_TC_OPEN) return FLP_TR_ECLOSED;
  const size_t n = flexRingPop(&L2->toFlexOS, out, cap);
  return (int)n;
}
static const char* loopStatus(FlexPhoneTransport* t){
  return ((Loop*)t->ctx)->st == FLP_TC_OPEN ? "lazo abierto" : "lazo cerrado";
}
static void loopPeer(FlexPhoneTransport* t, char* out, size_t outN){
  (void)t;
  std::snprintf(out, outN, "lazo:0");
}
static FlexPhoneTransport gTr = {
  FLP_TR_LOOP, loopStart, loopStop, loopState, loopMtu,
  loopSend, loopRecv, loopStatus, loopPeer, &gLoop,
};

// El telefono simulado escribe una trama de vuelta.
static void phoneSay(uint8_t type, const uint8_t* body, size_t n, uint16_t session){
  uint8_t frame[FLNK_MAX_FRAME];
  FlexLinkHeader h; std::memset(&h, 0, sizeof(h));
  h.version = FLNK_VERSION; h.type = type;
  h.session = session; h.packet = (uint16_t)(++gLoop.txCounter);
  h.frag = 0; h.fragCount = 1; h.counter = gLoop.txCounter;
  const int fn = flexLinkWriteFrame(frame, sizeof(frame), &h, body, n);
  if(fn > 0) flexRingPush(&gLoop.toFlexOS, frame, (size_t)fn);
}

// Una vuelta del telefono simulado: lee lo que Flex OS le mando y
// contesta lo que toque. Es el protocolo REAL, no un eco.
static void phoneStep(){
  uint8_t f[FLNK_MAX_FRAME];
  size_t n;
  while((n = flexRingPop(&gLoop.toPhone, f, sizeof(f))) > 0){
    if(!gLoop.phoneAnswers) continue;
    FlexLinkHeader h;
    const uint8_t* p = nullptr; size_t pn = 0;
    if(flexLinkReadFrame(f, n, &h, &p, &pn) != FLNK_OK) continue;
    switch(h.type){
      case FLNK_T_HELLO: {
        FlexLinkRd r; flexLinkRdInit(&r, p, pn);
        (void)flexLinkRdU8(&r);
        flexLinkRdStr(&r, gLoop.flexosId, sizeof(gLoop.flexosId));
        uint8_t body[1 + FLXA_ID_MAX + FLP_DEVNAME_MAX];
        FlexLinkWr w; flexLinkWrInit(&w, body, sizeof(body));
        flexLinkWrU8(&w, FLNK_VERSION);
        flexLinkWrStr(&w, gLoop.phoneId, FLXA_ID_MAX - 1);
        flexLinkWrStr(&w, "Galaxy A55 5G", FLP_DEVNAME_MAX - 1);
        phoneSay(FLNK_T_WELCOME, body, w.at, 0);
        break;
      }
      case FLNK_T_PAIR_CODE: {
        // Flex OS manda la sal. El telefono deriva la clave con el
        // codigo que el usuario ha tecleado -- NO con uno que venga
        // por la red, que es justo lo que no se manda.
        uint8_t salt[FLXA_SALT_SIZE];
        FlexLinkRd r; flexLinkRdInit(&r, p, pn);
        flexLinkRdBytes(&r, salt, sizeof(salt));
        flexLinkRdBytes(&r, gLoop.nonce, sizeof(gLoop.nonce));
        char hostId[FLXA_ID_MAX] = {0};
        flexLinkRdStr(&r, hostId, sizeof(hostId));
        if(!flexLinkRdOk(&r)) break;
        flexAuthDeriveKey(gLoop.code, salt, hostId, gLoop.phoneId, gLoop.key);
        gLoop.haveKey = true;
        uint8_t proof[FLXA_PROOF_SIZE];
        flexAuthProof(gLoop.key, FLXA_ROLE_PHONE, gLoop.nonce, 0, proof);
        phoneSay(FLNK_T_PAIR_CONFIRM, proof, sizeof(proof), 0);
        break;
      }
      case FLNK_T_AUTH_CHALLENGE: {
        FlexLinkRd r; flexLinkRdInit(&r, p, pn);
        flexLinkRdBytes(&r, gLoop.nonce, sizeof(gLoop.nonce));
        const uint16_t sess = flexLinkRdU16(&r);
        if(!flexLinkRdOk(&r) || !gLoop.haveKey) break;
        uint8_t proof[FLXA_PROOF_SIZE];
        flexAuthProof(gLoop.key, FLXA_ROLE_PHONE, gLoop.nonce, sess, proof);
        phoneSay(FLNK_T_AUTH_RESPONSE, proof, sizeof(proof), sess);
        break;
      }
      case FLNK_T_AUTH_OK: {
        // El telefono comprueba que quien le contesta ES Flex OS.
        uint8_t proof[FLXA_PROOF_SIZE];
        FlexLinkRd r; flexLinkRdInit(&r, p, pn);
        flexLinkRdBytes(&r, proof, sizeof(proof));
        if(flexLinkRdOk(&r) && gLoop.haveKey &&
           flexAuthVerify(gLoop.key, FLXA_ROLE_HOST, gLoop.nonce, h.session, proof))
          gLoop.gotHostProof = true;
        break;
      }
      case FLNK_T_PING:
        phoneSay(FLNK_T_PONG, nullptr, 0, h.session);
        break;
      default: break;
    }
  }
}

// Avanza el enlace y el telefono unas cuantas vueltas.
static void pump(uint32_t& t, int rounds = 6, uint32_t stepMs = 20){
  for(int i = 0; i < rounds; i++){
    flexPhoneLinkTick(&L, &M, t);
    phoneStep();
    t += stepMs;
  }
}

// Construye una trama valida y se la entrega al enlace directamente,
// saltandose el transporte (para probar tramas hostiles).
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

// Fuente de azar reproducible: las pruebas tienen que dar lo mismo
// en cada ejecucion.
static uint32_t gSeed = 0x12345678u;
static uint32_t testRand(){
  gSeed = gSeed * 1664525u + 1013904223u;
  return gSeed;
}

// Arranca el enlace con el transporte de lazo conectado.
static void bring(){
  gLoop = Loop();
  gTr.ctx = &gLoop;
  gSeed = 0x12345678u;
  flexPhoneLinkInit(&L);
  flexPhoneModelInit(&M);
  flexPhoneLinkSetIdentity(&L, "flexos-test");
  flexPhoneLinkSetTransport(&L, &gTr);
}

// Empareja de verdad: codigo, sal, prueba y confirmacion del usuario.
static void pairUp(uint32_t& t){
  bring();
  CHECK(flexPhoneLinkStart(&L), "no arranco con transporte disponible");
  pump(t, 3);
  CHECK(L.state == FLP_LS_PAIRING || L.state == FLP_LS_CONNECTING,
        "estado tras presentarse: %s", flexPhoneLinkStateName(L.state));
  flexPhoneLinkBeginPairing(&L, testRand, t);
  std::memcpy(gLoop.code, L.code, sizeof(gLoop.code) - 1);  // el usuario teclea el bueno
  pump(t, 4);
  flexPhoneLinkConfirm(&L, t);
  pump(t, 4);
}

// -------------------------------------------------------------
//  1) Honestidad sobre el transporte
// -------------------------------------------------------------
static void testCapability(){
  std::printf("[link] el transporte actual es Wi-Fi, y BLE se dice como lo que es\n");
  bring();
  // En Flex OS el transporte de Flex Phone es Wi-Fi. Se comprueba
  // que NO se anuncia BLE, porque el P4 no tiene radio Bluetooth.
  CHECK(flexPhoneLinkCap() == FLP_LINK_CAP_WIFI,
        "el transporte deberia ser Wi-Fi (cap=%u)", flexPhoneLinkCap());
  CHECK(flexPhoneLinkAvailable(), "el enlace deberia poder intentarse");
  CHECK(!flexPhoneBleReady(), "no hay radio BLE en este chip y se declaro que si");
  CHECK(std::strstr(flexPhoneBleReason(), "C6") != nullptr,
        "el motivo de BLE deberia mencionar el C6");
  CHECK(std::strstr(flexPhoneBleReason(), "Wi-Fi") != nullptr,
        "el motivo deberia decir que Flex Phone funciona por Wi-Fi");
  // Y lo mas importante: recien arrancado NUNCA se declara listo.
  CHECK(!flexPhoneLinkReady(&L), "se declaro CONECTADO sin sesion");

  // Sin transporte: se dice que no y no se cambia de estado a medias.
  flexPhoneLinkSetTransport(&L, nullptr);
  CHECK(!flexPhoneLinkStart(&L), "arranco sin transporte");
  CHECK(L.state == FLP_LS_ERROR, "estado tras fallar: %s", flexPhoneLinkStateName(L.state));
  CHECK(L.err[0] != 0, "no explico por que no pudo arrancar");
  CHECK(!flexPhoneLinkReady(&L), "se declaro conectado tras fallar");
}

// -------------------------------------------------------------
//  2) Emparejamiento: hacen falta LAS DOS partes
// -------------------------------------------------------------
static void testPairing(){
  std::printf("[link] emparejamiento: el codigo no viaja y hacen falta los dos lados\n");
  uint32_t t = 1000;
  bring();
  flexPhoneLinkStart(&L);
  pump(t, 3);
  flexPhoneLinkBeginPairing(&L, testRand, t);
  CHECK(L.state == FLP_LS_PAIRING, "no entro en emparejamiento");
  CHECK(std::strlen(L.code) == FLP_LINK_CODE_LEN, "codigo de %u digitos",
        (unsigned)std::strlen(L.code));
  for(int i = 0; i < FLP_LINK_CODE_LEN; i++)
    CHECK(L.code[i] >= '0' && L.code[i] <= '9', "el codigo tiene un caracter no numerico");

  // EL CODIGO NO PUEDE VIAJAR. Se recorre todo lo que Flex OS ha
  // puesto en el canal y se comprueba que la cadena no aparece.
  pump(t, 3);
  {
    bool leaked = false;
    for(int k = 0; k < gLoop.sniffN; k++){
      const size_t n = gLoop.sniffLen[k];
      for(size_t i = 0; i + FLP_LINK_CODE_LEN <= n; i++)
        if(std::memcmp(gLoop.sniff[k] + i, L.code, FLP_LINK_CODE_LEN) == 0) leaked = true;
    }
    CHECK(gLoop.sniffN > 0, "no se envio nada al telefono");
    CHECK(!leaked, "EL CODIGO DE EMPAREJAMIENTO VIAJO POR EL ENLACE");
  }

  // Solo el usuario: no basta.
  flexPhoneLinkConfirm(&L, t);
  CHECK(!flexPhoneLinkPairComplete(&L), "emparejo con una sola confirmacion");
  CHECK(L.state == FLP_LS_PAIRING, "salio de emparejamiento antes de tiempo");
  CHECK(!flexPhoneLinkReady(&L), "se declaro conectado a medio emparejar");

  // Un telefono que teclea MAL el codigo no empareja, por mucho que
  // conteste a tiempo y con una trama perfecta.
  std::memcpy(gLoop.code, "999999", 7);
  if(L.code[0] == '9' && std::strcmp(L.code, "999999") == 0) std::memcpy(gLoop.code, "111111", 7);
  pump(t, 4);
  CHECK(!flexPhoneLinkPairComplete(&L), "EMPAREJO CON UN CODIGO EQUIVOCADO");
  CHECK(L.nAuthFail > 0, "no conto el fallo de autenticacion");
  CHECK(!flexPhoneLinkReady(&L), "se declaro conectado con el codigo mal");

  // Con el codigo bueno, si.
  std::memcpy(gLoop.code, L.code, 7);
  gLoop.haveKey = false;
  flexPhoneLinkBeginPairing(&L, testRand, t);
  std::memcpy(gLoop.code, L.code, 7);
  pump(t, 4);
  flexPhoneLinkConfirm(&L, t);
  pump(t, 4);
  CHECK(flexPhoneLinkPairComplete(&L), "no completo con las dos partes");
  CHECK(L.state == FLP_LS_READY, "no quedo listo (%s)", flexPhoneLinkStateName(L.state));
  CHECK(flexPhoneLinkBonded(&L), "no marco el vinculo");
  CHECK(L.code[0] == 0, "dejo el codigo a la vista tras emparejar");
  CHECK(L.session != 0, "no abrio sesion");
  // Y el telefono comprobo que hablaba con Flex OS, no con un impostor.
  CHECK(gLoop.gotHostProof, "Flex OS no demostro SU identidad al telefono");

  // Caducidad: el codigo no se queda en pantalla para siempre.
  bring();
  flexPhoneLinkStart(&L);
  pump(t, 3);
  flexPhoneLinkBeginPairing(&L, testRand, t);
  t += FLP_LINK_PAIR_WINDOW_MS + 1000;
  flexPhoneLinkTick(&L, &M, t);
  CHECK(L.state != FLP_LS_PAIRING, "el emparejamiento no caduco");
  CHECK(L.code[0] == 0, "dejo el codigo caducado en pantalla");
}

// -------------------------------------------------------------
//  3) Reconexion con vinculo guardado: NO se vuelve a emparejar
// -------------------------------------------------------------
static void testResume(){
  std::printf("[link] con vinculo guardado se reconecta sin volver a emparejar\n");
  uint32_t t = 5000;
  pairUp(t);
  CHECK(L.state == FLP_LS_READY, "no quedo listo tras emparejar");
  FlexPhoneBond saved = L.bond;

  // Reinicio de Flex OS: el vinculo se carga de flash.
  uint8_t savedKey[FLXA_KEY_SIZE];
  std::memcpy(savedKey, gLoop.key, sizeof(savedKey));
  const bool phoneHadKey = gLoop.haveKey;
  gLoop = Loop();
  gTr.ctx = &gLoop;
  std::memcpy(gLoop.key, savedKey, sizeof(savedKey));
  gLoop.haveKey = phoneHadKey;
  flexPhoneLinkInit(&L);
  flexPhoneModelInit(&M);
  flexPhoneLinkSetIdentity(&L, "flexos-test");
  flexPhoneLinkSetTransport(&L, &gTr);
  flexPhoneLinkSetBond(&L, &saved);
  CHECK(flexPhoneLinkBonded(&L), "no recupero el vinculo guardado");
  flexPhoneLinkStart(&L);
  pump(t, 8);
  CHECK(L.state == FLP_LS_READY, "no reconecto (%s): %s",
        flexPhoneLinkStateName(L.state), L.err);
  CHECK(L.code[0] == 0, "pidio emparejar teniendo vinculo");

  // Un telefono que dice tener el mismo id pero NO la clave no entra.
  gLoop = Loop();
  gTr.ctx = &gLoop;
  flexAuthRandomBytes(testRand, gLoop.key, sizeof(gLoop.key));   // clave equivocada
  gLoop.haveKey = true;
  flexPhoneLinkInit(&L);
  flexPhoneModelInit(&M);
  flexPhoneLinkSetIdentity(&L, "flexos-test");
  flexPhoneLinkSetTransport(&L, &gTr);
  flexPhoneLinkSetBond(&L, &saved);
  flexPhoneLinkStart(&L);
  pump(t, 8);
  CHECK(L.state != FLP_LS_READY, "ACEPTO UN TELEFONO SIN LA CLAVE");
  CHECK(L.nAuthFail > 0, "no conto el fallo de autenticacion");

  // Olvidar borra la clave DE VERDAD, no solo la bandera.
  flexPhoneLinkSetBond(&L, &saved);
  flexPhoneLinkForget(&L);
  CHECK(!flexPhoneLinkBonded(&L), "siguio vinculado tras olvidar");
  bool zero = true;
  for(size_t i = 0; i < FLXA_KEY_SIZE; i++) if(L.bond.key[i]) zero = false;
  CHECK(zero, "LA CLAVE SIGUIO EN MEMORIA DESPUES DE OLVIDAR");
}

// -------------------------------------------------------------
//  4) Sin sesion no entra NADA
// -------------------------------------------------------------
static void testAccessControl(){
  std::printf("[link] sin sesion autenticada no se acepta ni una notificacion\n");
  uint32_t t = 9000;
  bring();
  flexPhoneLinkStart(&L);
  pump(t, 3);

  FlexPhoneNotif n; std::memset(&n, 0, sizeof(n));
  n.id = 77;
  std::snprintf(n.pkg,   sizeof(n.pkg),   "com.intruso");
  std::snprintf(n.app,   sizeof(n.app),   "Intruso");
  std::snprintf(n.title, sizeof(n.title), "hola");
  n.pri = FLP_PRI_DEFAULT;
  uint8_t body[FLP_LINK_TXBUF];
  const int bn = flexPhoneEncNotif(body, sizeof(body), &n);
  CHECK(bn > 0, "no se codifico la notificacion de prueba");

  const uint32_t dropped0 = L.nDropped;
  CHECK(!feed(FLNK_T_NOTIF_ADD, body, (size_t)bn, 900, t, 0),
        "ACEPTO UNA NOTIFICACION SIN SESION AUTENTICADA");
  CHECK(M.notifCount == 0, "guardo una notificacion de un extremo sin autenticar");
  CHECK(L.nDropped > dropped0, "no conto el descarte");
}

// -------------------------------------------------------------
//  5) Tramas hostiles con sesion abierta
// -------------------------------------------------------------
static void testHostileFrames(){
  std::printf("[link] repetidas, de otra sesion, corruptas y de version futura\n");
  uint32_t t = 12000;
  pairUp(t);
  CHECK(L.state == FLP_LS_READY, "no quedo listo para la prueba");
  const uint16_t sess = L.session;

  FlexPhoneNotif n; std::memset(&n, 0, sizeof(n));
  n.id = 42;
  std::snprintf(n.pkg,   sizeof(n.pkg),   "com.whatsapp");
  std::snprintf(n.app,   sizeof(n.app),   "WhatsApp");
  std::snprintf(n.title, sizeof(n.title), "Ana");
  std::snprintf(n.text,  sizeof(n.text),  "hola");
  n.pri = FLP_PRI_HIGH;
  uint8_t body[FLP_LINK_TXBUF];
  const int bn = flexPhoneEncNotif(body, sizeof(body), &n);
  CHECK(bn > 0, "no se codifico la notificacion de prueba");

  // La buena entra.
  const uint32_t c0 = 5000;
  CHECK(feed(FLNK_T_NOTIF_ADD, body, (size_t)bn, c0, t, sess), "no acepto una notificacion buena");
  CHECK(M.notifCount == 1, "no la guardo (%u)", M.notifCount);

  // La MISMA otra vez: repetida.
  const uint32_t dropped0 = L.nDropped;
  CHECK(!feed(FLNK_T_NOTIF_ADD, body, (size_t)bn, c0, t + 100, sess), "acepto una trama REPETIDA");
  CHECK(L.nDropped == dropped0 + 1, "no conto el descarte");
  CHECK(M.notifCount == 1, "la repetida duplico la notificacion");

  // De otra sesion: fuera.
  CHECK(!feed(FLNK_T_NOTIF_ADD, body, (size_t)bn, c0 + 1, t + 200, (uint16_t)(sess ^ 0x5A5A)),
        "acepto una trama de OTRA sesion");
  CHECK(M.notifCount == 1, "la trama ajena entro igualmente");

  // Corrupta: el CRC la caza.
  {
    uint8_t frame[FLNK_MAX_FRAME];
    FlexLinkHeader h; std::memset(&h, 0, sizeof(h));
    h.version = FLNK_VERSION; h.type = FLNK_T_NOTIF_ADD;
    h.session = sess; h.packet = 9; h.frag = 0; h.fragCount = 1; h.counter = c0 + 2;
    const int fn = flexLinkWriteFrame(frame, sizeof(frame), &h, body, (size_t)bn);
    CHECK(fn > 0, "no se pudo construir la trama");
    frame[FLNK_HDR_SIZE + 3] ^= 0xFF;                 // un byte de la carga
    const uint32_t bad0 = L.nBad;
    CHECK(!flexPhoneLinkOnFrame(&L, &M, frame, (size_t)fn, t + 300), "acepto una trama corrupta");
    CHECK(L.nBad == bad0 + 1, "no conto el paquete malo");
    CHECK(M.stats.badPacket > 0, "el modelo no se entero del paquete malo");
  }

  // Version futura: NO se interpreta la carga.
  {
    uint8_t frame[FLNK_MAX_FRAME];
    FlexLinkHeader h; std::memset(&h, 0, sizeof(h));
    h.version = FLNK_VERSION + 9; h.type = FLNK_T_NOTIF_ADD;
    h.session = sess; h.packet = 11; h.frag = 0; h.fragCount = 1; h.counter = c0 + 3;
    const int fn = flexLinkWriteFrame(frame, sizeof(frame), &h, body, (size_t)bn);
    CHECK(fn > 0, "no se pudo construir la trama de version futura");
    CHECK(!flexPhoneLinkOnFrame(&L, &M, frame, (size_t)fn, t + 400), "acepto una version futura");
    CHECK(M.notifCount == 1, "una version futura modifico el modelo");
  }

  // Tipo desconocido: se ignora, NO tumba el enlace.
  {
    uint32_t t2 = t + 500;
    pairUp(t2);
    CHECK(feed(0xEE, nullptr, 0, 8000, t2, L.session),
          "un tipo desconocido no deberia ser un fallo");
    CHECK(L.state == FLP_LS_READY, "un tipo desconocido tumbo el enlace");
  }
}

// -------------------------------------------------------------
//  6) Latencia REAL y latido
// -------------------------------------------------------------
static void testLatency(){
  std::printf("[link] la latencia se MIDE con ping/pong; sin medida se dice que no hay\n");
  uint32_t t = 20000;
  bring();
  flexPhoneLinkStart(&L);
  uint16_t ms = 0;
  CHECK(!flexPhoneLinkLatency(&L, &ms), "invento una latencia sin haber medido nada");

  pairUp(t);
  CHECK(L.state == FLP_LS_READY, "no quedo listo");
  CHECK(!flexPhoneLinkLatency(&L, &ms), "ya daba latencia antes del primer ping");

  // Se deja pasar el tiempo de latido: el enlace manda PING solo.
  t += FLP_LINK_IDLE_PING_MS + 100;
  flexPhoneLinkTick(&L, &M, t);      // encola el latido
  flexPhoneLinkTick(&L, &M, t);      // lo entrega: aqui arranca el reloj
  CHECK(L.pingSentMs != 0, "no salio el latido");
  phoneStep();                       // el telefono contesta PONG
  t += 13;                           // 13 ms de ida y vuelta
  flexPhoneLinkTick(&L, &M, t);
  CHECK(flexPhoneLinkLatency(&L, &ms), "no registro la latencia tras el PONG");
  CHECK(ms == 13, "latencia medida %u ms (esperaba 13)", ms);

  // Un PONG que no llega no deja la medida colgada para siempre.
  gLoop.phoneAnswers = false;
  t += FLP_LINK_IDLE_PING_MS + 100;
  flexPhoneLinkTick(&L, &M, t);
  flexPhoneLinkTick(&L, &M, t);
  CHECK(L.pingSentMs != 0, "no salio el segundo latido");
  t += FLP_LINK_ACK_TIMEOUT_MS + 100;
  flexPhoneLinkTick(&L, &M, t);
  CHECK(L.pingSentMs == 0, "el ping perdido dejo la medida colgada");
}

// -------------------------------------------------------------
//  7) Caida del enlace: se limpia, no se congela
// -------------------------------------------------------------
static void testDrop(){
  std::printf("[link] al caerse el telefono se limpia el estado y se reconecta con limite\n");
  uint32_t t = 30000;
  pairUp(t);
  CHECK(L.state == FLP_LS_READY, "no quedo listo");

  // Llega estado del telefono: bateria real.
  {
    uint8_t body[64];
    FlexLinkWr w; flexLinkWrInit(&w, body, sizeof(body));
    flexLinkWrStr(&w, "Galaxy A55 5G", FLP_DEVNAME_MAX - 1);
    flexLinkWrU8(&w, 78);
    flexLinkWrU8(&w, 0x01);
    flexLinkWrU8(&w, FLP_NET_WIFI);
    CHECK(feed(FLNK_T_PHONE_STATE, body, w.at, 9000, t, L.session), "no acepto el estado");
    CHECK(M.phone.valid && M.phone.battery == 78, "no guardo la bateria real");
  }

  // El telefono deja de contestar. Pasado el plazo se da por caido.
  gLoop.phoneAnswers = false;
  t += FLP_LINK_DEAD_MS + 1000;
  flexPhoneLinkTick(&L, &M, t);
  CHECK(L.state != FLP_LS_READY, "siguio diciendo CONECTADO con el telefono mudo");
  CHECK(!M.phone.valid, "CONSERVO LA BATERIA DE UN TELEFONO QUE YA NO ESTA");
  CHECK(L.err[0] != 0, "no dijo que habia pasado");

  // Ahora se cae el Wi-Fi de verdad: el transporte informa FALLO. Se
  // reintenta con espera progresiva, pero NO para siempre.
  gLoop.st = FLP_TC_FAILED;
  for(int i = 0; i < 40 && L.state != FLP_LS_ERROR; i++){
    t += FLNK_RETRY_CAP_MS + 1000;
    flexPhoneLinkTick(&L, &M, t);
  }
  CHECK(L.state == FLP_LS_ERROR, "reintento sin limite");
  CHECK(L.err[0] != 0, "se rindio sin decir por que");

  // Con el enlace en error no entra ni una trama mas.
  const uint32_t dropped0 = L.nDropped;
  CHECK(!feed(FLNK_T_PING, nullptr, 0, 50000, t, L.session), "acepto una trama con el enlace caido");
  CHECK(L.nDropped > dropped0, "no conto el descarte");
}

// -------------------------------------------------------------
//  8) Cola de tramas del transporte
// -------------------------------------------------------------
static void testRing(){
  std::printf("[transporte] la cola tira lo VIEJO al llenarse, y lo cuenta\n");
  FlexFrameRing r;
  flexRingInit(&r);
  CHECK(flexRingEmpty(&r), "recien creada no estaba vacia");

  uint8_t f[8];
  for(int i = 0; i < FLP_RING_SLOTS - 1; i++){
    std::memset(f, (uint8_t)i, sizeof(f));
    CHECK(flexRingPush(&r, f, sizeof(f)), "no acepto la trama %d", i);
  }
  CHECK(flexRingFull(&r), "no se declaro llena");

  // Una mas: se va la primera.
  std::memset(f, 0xEE, sizeof(f));
  CHECK(flexRingPush(&r, f, sizeof(f)), "no acepto la trama que desborda");
  CHECK(r.nDropped == 1, "no conto el descarte (%u)", r.nDropped);

  uint8_t out[FLNK_MAX_FRAME];
  const size_t n = flexRingPop(&r, out, sizeof(out));
  CHECK(n == sizeof(f), "saco %u bytes", (unsigned)n);
  CHECK(out[0] == 1, "no tiro la MAS ANTIGUA (saco %u)", out[0]);

  // Argumentos imposibles: se rechazan, no se copian.
  CHECK(!flexRingPush(&r, nullptr, 4), "acepto un puntero nulo");
  CHECK(!flexRingPush(&r, f, 0), "acepto una trama vacia");
  CHECK(!flexRingPush(&r, f, FLP_RING_FRAME + 1), "acepto una trama demasiado grande");

  // Un lector sin sitio no recibe media trama.
  flexRingInit(&r);
  uint8_t big[FLP_RING_FRAME];
  std::memset(big, 0xAB, sizeof(big));
  flexRingPush(&r, big, sizeof(big));
  uint8_t small[4];
  CHECK(flexRingPop(&r, small, sizeof(small)) == 0, "entrego media trama");
  CHECK(r.nDropped > 0, "no conto la trama que no cabia");
}

int main(){
  std::printf("\n=== FlexOS · maquina de estados del enlace de Flex Phone ===\n");
  testCapability();
  testPairing();
  testResume();
  testAccessControl();
  testHostileFrames();
  testLatency();
  testDrop();
  testRing();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
