// #############################################################
//  test_flexlink.cpp  ·  pruebas de host del protocolo Flex Link
//  (FlexOS_FlexLink.cpp: enmarcado, CRC, fragmentacion,
//   anti-repeticion y truncado UTF-8)
// #############################################################
//
//  Se compila el MISMO fichero que va a la placa. Todo lo que se
//  prueba aqui recibe bytes que NO son de fiar: vienen de un
//  telefono por el aire. Se compila con AddressSanitizer, asi que
//  una lectura fuera de rango en el parser no puede quedarse en
//  "parece que funciona": aborta la prueba.

#include "../../FlexOS_FlexLink.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static void hdrInit(FlexLinkHeader* h, uint8_t type){
  std::memset(h, 0, sizeof(*h));
  h->version = FLNK_VERSION; h->type = type;
  h->session = 0x1234; h->packet = 7; h->frag = 0; h->fragCount = 1; h->counter = 1;
}

// -------------------------------------------------------------
//  1) Ida y vuelta del enmarcado
// -------------------------------------------------------------
static void testRoundTrip(){
  std::printf("[flexlink] enmarcado: ida y vuelta\n");
  uint8_t frame[FLNK_MAX_FRAME];
  const char* body = "hola flex";
  FlexLinkHeader h; hdrInit(&h, FLNK_T_NOTIF_ADD);

  int n = flexLinkWriteFrame(frame, sizeof(frame), &h, (const uint8_t*)body, std::strlen(body));
  CHECK(n == (int)(FLNK_HDR_SIZE + std::strlen(body)), "escritos %d", n);

  FlexLinkHeader g; const uint8_t* pl = nullptr; size_t pn = 0;
  int r = flexLinkReadFrame(frame, (size_t)n, &g, &pl, &pn);
  CHECK(r == FLNK_OK, "lectura devolvio %d", r);
  CHECK(g.type == FLNK_T_NOTIF_ADD, "tipo %u", g.type);
  CHECK(g.session == 0x1234, "sesion %u", g.session);
  CHECK(g.packet == 7, "paquete %u", g.packet);
  CHECK(g.counter == 1, "contador %u", (unsigned)g.counter);
  CHECK(pn == std::strlen(body) && pl && std::memcmp(pl, body, pn) == 0, "carga distinta");

  // Carga vacia: sigue siendo una trama valida.
  FlexLinkHeader h2; hdrInit(&h2, FLNK_T_PING);
  n = flexLinkWriteFrame(frame, sizeof(frame), &h2, nullptr, 0);
  CHECK(n == FLNK_HDR_SIZE, "ping mide %d", n);
  r = flexLinkReadFrame(frame, (size_t)n, &g, &pl, &pn);
  CHECK(r == FLNK_OK && pn == 0, "ping no vuelve");
}

// -------------------------------------------------------------
//  2) Tramas que hay que RECHAZAR
//     Es la prueba que de verdad importa: son los bytes que un
//     telefono averiado -o un atacante- puede meter por el aire.
// -------------------------------------------------------------
static void testRejects(){
  std::printf("[flexlink] tramas invalidas: corrupta, corta, mentirosa y gigante\n");
  uint8_t frame[FLNK_MAX_FRAME];
  FlexLinkHeader h; hdrInit(&h, FLNK_T_NOTIF_ADD);
  const char* body = "carga de prueba";
  int n = flexLinkWriteFrame(frame, sizeof(frame), &h, (const uint8_t*)body, std::strlen(body));
  CHECK(n > 0, "no se pudo construir");

  FlexLinkHeader g; const uint8_t* pl; size_t pn;

  // -- corrupta: un bit cambiado en la carga --
  uint8_t bad[FLNK_MAX_FRAME]; std::memcpy(bad, frame, (size_t)n);
  bad[FLNK_HDR_SIZE + 2] ^= 0x01;
  CHECK(flexLinkReadFrame(bad, (size_t)n, &g, &pl, &pn) == FLNK_ERR_CRC, "CRC no detecto el bit cambiado");

  // -- corrupta: un bit cambiado en la CABECERA --
  std::memcpy(bad, frame, (size_t)n); bad[6] ^= 0x08;
  CHECK(flexLinkReadFrame(bad, (size_t)n, &g, &pl, &pn) == FLNK_ERR_CRC, "CRC no cubre la cabecera");

  // -- incompleta: se corta a la mitad --
  CHECK(flexLinkReadFrame(frame, FLNK_HDR_SIZE - 1, &g, &pl, &pn) == FLNK_ERR_SHORT, "acepto una trama corta");
  // Truncada por debajo de lo declarado: la longitud ya no cuadra.
  CHECK(flexLinkReadFrame(frame, (size_t)n - 3, &g, &pl, &pn) == FLNK_ERR_LEN, "acepto una trama truncada");

  // -- mentirosa: declara mas carga de la que trae --
  std::memcpy(bad, frame, (size_t)n);
  bad[10] = 0xFF; bad[11] = 0x00;
  CHECK(flexLinkReadFrame(bad, (size_t)n, &g, &pl, &pn) == FLNK_ERR_LEN, "acepto una longitud imposible");

  // -- magia equivocada: ruido de otra caracteristica GATT --
  std::memcpy(bad, frame, (size_t)n); bad[0] = 0x00;
  CHECK(flexLinkReadFrame(bad, (size_t)n, &g, &pl, &pn) == FLNK_ERR_MAGIC, "acepto magia equivocada");

  // -- version futura: NO se interpreta --
  FlexLinkHeader hv; hdrInit(&hv, FLNK_T_HELLO); hv.version = FLNK_VERSION + 1;
  n = flexLinkWriteFrame(frame, sizeof(frame), &hv, nullptr, 0);
  CHECK(n > 0, "no se construyo la trama de version futura");
  CHECK(flexLinkReadFrame(frame, (size_t)n, &g, &pl, &pn) == FLNK_ERR_VERSION,
        "una version futura no puede interpretarse");

  // -- demasiado grande: por encima del maximo de trama --
  FlexLinkHeader hb; hdrInit(&hb, FLNK_T_NOTIF_ADD);
  static uint8_t huge[FLNK_MAX_PAYLOAD + 32];
  std::memset(huge, 'A', sizeof(huge));
  CHECK(flexLinkWriteFrame(frame, sizeof(frame), &hb, huge, sizeof(huge)) < 0,
        "dejo escribir una carga mayor que el maximo");

  // -- fragmento imposible: indice >= total --
  FlexLinkHeader hf; hdrInit(&hf, FLNK_T_NOTIF_ADD); hf.frag = 3; hf.fragCount = 2;
  CHECK(flexLinkWriteFrame(frame, sizeof(frame), &hf, nullptr, 0) == FLNK_ERR_FRAG,
        "dejo escribir un fragmento fuera de rango");
}

// -------------------------------------------------------------
//  3) Fragmentacion y reensamblaje
// -------------------------------------------------------------
static void feedAll(FlexLinkReasm* r, const uint8_t* msg, size_t msgLen,
                    uint16_t packet, size_t mtu, int* lastResult,
                    bool reverse = false, bool duplicateFirst = false){
  const int k = flexLinkFragCount(msgLen, mtu);
  const size_t per = (mtu > FLNK_MAX_FRAME ? FLNK_MAX_FRAME : mtu) - FLNK_HDR_SIZE;
  uint8_t frame[FLNK_MAX_FRAME];
  for(int step = 0; step < k; step++){
    const int i = reverse ? (k - 1 - step) : step;
    FlexLinkHeader h; hdrInit(&h, FLNK_T_NOTIF_ADD);
    h.packet = packet; h.frag = (uint8_t)i; h.fragCount = (uint8_t)k;
    h.counter = (uint32_t)(100 + i);
    const size_t off  = (size_t)i * per;
    const size_t take = (msgLen - off) < per ? (msgLen - off) : per;
    int n = flexLinkWriteFrame(frame, sizeof(frame), &h, msg + off, take);
    if(n <= 0){ *lastResult = FLNK_R_DROP; return; }
    FlexLinkHeader g; const uint8_t* pl; size_t pn;
    if(flexLinkReadFrame(frame, (size_t)n, &g, &pl, &pn) != FLNK_OK){ *lastResult = FLNK_R_DROP; return; }
    *lastResult = flexLinkReasmFeed(r, &g, pl, pn, 1000);
    if(duplicateFirst && step == 0)   // el MISMO fragmento otra vez
      *lastResult = flexLinkReasmFeed(r, &g, pl, pn, 1000);
  }
}

static void testFragments(){
  std::printf("[flexlink] fragmentacion: completo, repetido, fuera de orden y caducado\n");
  // Mensaje que NO cabe en una trama: obliga a trocear de verdad.
  static uint8_t msg[700];
  for(size_t i = 0; i < sizeof(msg); i++) msg[i] = (uint8_t)(i * 7 + 3);

  const size_t mtu = FLNK_MAX_FRAME;
  const int k = flexLinkFragCount(sizeof(msg), mtu);
  CHECK(k == 4, "700 B con MTU 244 -> %d fragmentos", k);

  FlexLinkReasm r; flexLinkReasmInit(&r);
  int last = FLNK_R_DROP;
  feedAll(&r, msg, sizeof(msg), 11, mtu, &last);
  CHECK(last == FLNK_R_DONE, "no completo el mensaje (%d)", last);
  CHECK(r.len == sizeof(msg), "reensamblo %u bytes", (unsigned)r.len);
  CHECK(std::memcmp(r.buf, msg, sizeof(msg)) == 0, "el contenido reensamblado no coincide");

  // -- REPETIDO: el mismo fragmento dos veces no rompe ni duplica --
  flexLinkReasmInit(&r); last = FLNK_R_DROP;
  feedAll(&r, msg, sizeof(msg), 12, mtu, &last, false, true);
  CHECK(last == FLNK_R_DONE, "el repetido impidio completar (%d)", last);
  CHECK(r.nDup == 1, "no conto el fragmento repetido (nDup=%u)", (unsigned)r.nDup);
  CHECK(std::memcmp(r.buf, msg, sizeof(msg)) == 0, "el repetido corrompio el mensaje");

  // -- FUERA DE ORDEN: empezar por el ultimo fragmento se descarta --
  flexLinkReasmInit(&r); last = FLNK_R_DONE;
  feedAll(&r, msg, sizeof(msg), 13, mtu, &last, true);
  CHECK(r.nOutOfOrder > 0, "no conto los fragmentos fuera de orden");
  CHECK(last != FLNK_R_DONE, "completo un mensaje que empezo por el final");

  // -- INCOMPLETO + CADUCIDAD: falta un fragmento y el parcial se abandona --
  flexLinkReasmInit(&r);
  uint8_t frame[FLNK_MAX_FRAME];
  FlexLinkHeader h; hdrInit(&h, FLNK_T_NOTIF_ADD);
  h.packet = 14; h.frag = 0; h.fragCount = 3; h.counter = 200;
  int n = flexLinkWriteFrame(frame, sizeof(frame), &h, msg, FLNK_MAX_PAYLOAD);
  FlexLinkHeader g; const uint8_t* pl; size_t pn;
  CHECK(flexLinkReadFrame(frame, (size_t)n, &g, &pl, &pn) == FLNK_OK, "no se pudo releer");
  CHECK(flexLinkReasmFeed(&r, &g, pl, pn, 1000) == FLNK_R_NEED_MORE, "deberia pedir mas");
  CHECK(!flexLinkReasmExpire(&r, 2000, 5000), "caduco antes de tiempo");
  CHECK(flexLinkReasmExpire(&r, 9000, 5000), "no caduco el parcial");
  CHECK(r.nAbandoned == 1, "no conto el abandono");

  // -- Un packet id NUEVO abandona el parcial anterior, no lo mezcla --
  flexLinkReasmInit(&r);
  CHECK(flexLinkReadFrame(frame, (size_t)n, &g, &pl, &pn) == FLNK_OK, "relectura");
  flexLinkReasmFeed(&r, &g, pl, pn, 1000);
  last = FLNK_R_DROP;
  feedAll(&r, msg, sizeof(msg), 15, mtu, &last);   // otro mensaje entero
  CHECK(last == FLNK_R_DONE, "el mensaje nuevo no completo (%d)", last);
  CHECK(r.nAbandoned == 1, "no abandono el parcial anterior");
  CHECK(std::memcmp(r.buf, msg, sizeof(msg)) == 0, "se mezclaron dos mensajes");

  // -- MTU pequeno: mas fragmentos, mismo resultado --
  flexLinkReasmInit(&r); last = FLNK_R_DROP;
  const int k2 = flexLinkFragCount(sizeof(msg), 64);
  CHECK(k2 > 4 && k2 <= FLNK_MAX_FRAGS, "MTU 64 -> %d fragmentos", k2);
  feedAll(&r, msg, sizeof(msg), 16, 64, &last);
  // Con MTU 64 los fragmentos intermedios no van llenos, asi que el
  // reensamblador los rechaza por diseno: el emisor DEBE trocear a
  // FLNK_MAX_PAYLOAD. Lo que se comprueba es que no se cuelga ni
  // corrompe, no que lo acepte.
  CHECK(last == FLNK_R_DROP || last == FLNK_R_DONE, "estado inesperado (%d)", last);

  // -- Mensaje que no cabe ni troceado --
  CHECK(flexLinkFragCount(FLNK_MAX_MESSAGE + 1, mtu) == 0, "acepto un mensaje mayor que el maximo");
  CHECK(flexLinkFragCount(100, FLNK_MIN_MTU - 1) == 0, "acepto un MTU inservible");
}

// -------------------------------------------------------------
//  4) Anti-repeticion
// -------------------------------------------------------------
static void testAntiReplay(){
  std::printf("[flexlink] anti-repeticion: repetido, reordenado y salto grande\n");
  FlexLinkAntiReplay a; flexLinkAntiReplayInit(&a);
  CHECK(flexLinkAntiReplayCheck(&a, 100), "el primero deberia entrar");
  CHECK(!flexLinkAntiReplayCheck(&a, 100), "acepto el MISMO contador dos veces");
  CHECK(flexLinkAntiReplayCheck(&a, 101), "no acepto el siguiente");
  CHECK(flexLinkAntiReplayCheck(&a, 103), "no acepto un hueco hacia delante");
  CHECK(flexLinkAntiReplayCheck(&a, 102), "no acepto un reordenado dentro de la ventana");
  CHECK(!flexLinkAntiReplayCheck(&a, 102), "acepto un reordenado REPETIDO");
  CHECK(!flexLinkAntiReplayCheck(&a, 101), "acepto un repetido antiguo");
  CHECK(!flexLinkAntiReplayCheck(&a, 1), "acepto un contador demasiado viejo");
  CHECK(flexLinkAntiReplayCheck(&a, 100000), "no acepto un salto grande");
  CHECK(!flexLinkAntiReplayCheck(&a, 103), "tras el salto, lo viejo sigue rechazado");
}

// -------------------------------------------------------------
//  5) Escritor/lector de campos y truncado UTF-8
// -------------------------------------------------------------
static void testFields(){
  std::printf("[flexlink] campos: desbordamiento controlado y UTF-8 sin partir\n");
  uint8_t buf[64];
  FlexLinkWr w; flexLinkWrInit(&w, buf, sizeof(buf));
  flexLinkWrU8(&w, 0xAB);
  flexLinkWrU16(&w, 0x1234);
  flexLinkWrU32(&w, 0xDEADBEEF);
  flexLinkWrStr(&w, "WhatsApp", 32);
  CHECK(flexLinkWrOk(&w), "el escritor se desbordo sin motivo");

  FlexLinkRd r; flexLinkRdInit(&r, buf, w.at);
  CHECK(flexLinkRdU8(&r) == 0xAB, "u8 no vuelve");
  CHECK(flexLinkRdU16(&r) == 0x1234, "u16 no vuelve");
  CHECK(flexLinkRdU32(&r) == 0xDEADBEEF, "u32 no vuelve");
  char app[32]; flexLinkRdStr(&r, app, sizeof(app));
  CHECK(std::strcmp(app, "WhatsApp") == 0, "cadena \"%s\"", app);
  CHECK(flexLinkRdOk(&r), "el lector se desbordo");

  // -- Desbordamiento: se marca, no se escribe fuera --
  uint8_t tiny[4];
  FlexLinkWr w2; flexLinkWrInit(&w2, tiny, sizeof(tiny));
  flexLinkWrU32(&w2, 1);
  flexLinkWrU32(&w2, 2);                 // ya no cabe
  CHECK(!flexLinkWrOk(&w2), "no marco el desbordamiento");
  CHECK(w2.at == 4, "escribio fuera del buffer (at=%u)", (unsigned)w2.at);

  // -- Lectura de una cadena cuya longitud no cuadra --
  uint8_t liar[3] = { 0xFF, 'a', 'b' };   // dice 255 bytes, trae 2
  FlexLinkRd r2; flexLinkRdInit(&r2, liar, sizeof(liar));
  char out[16]; flexLinkRdStr(&r2, out, sizeof(out));
  CHECK(!flexLinkRdOk(&r2), "no marco la longitud mentirosa");
  CHECK(out[0] == 0, "dejo basura en el destino");

  // -- UTF-8: no se parte un caracter multibyte --
  // "café" = 63 61 66 C3 A9. Cortar a 4 bytes partiria la 'é'.
  const char* cafe = "caf\xC3\xA9";
  CHECK(flexLinkUtf8Trunc(cafe, 5) == 5, "no cabe entero con 5 bytes");
  CHECK(flexLinkUtf8Trunc(cafe, 4) == 3, "partio la 'e' acentuada");
  char small[5];
  size_t got = flexLinkUtf8Copy(small, sizeof(small), cafe);
  CHECK(got == 3 && std::strcmp(small, "caf") == 0, "copia UTF-8 dejo \"%s\"", small);
  // Emoji de 4 bytes en un buffer que solo deja 3.
  const char* emo = "\xF0\x9F\x93\xB1";       // telefono movil
  char tiny2[4];
  CHECK(flexLinkUtf8Copy(tiny2, sizeof(tiny2), emo) == 0, "partio un emoji de 4 bytes");
  CHECK(tiny2[0] == 0, "no dejo la cadena vacia y terminada");
  // Secuencia invalida: se corta ahi, no se propaga.
  const char* broken = "ok\xC3";
  CHECK(flexLinkUtf8Trunc(broken, 8) == 2, "propago UTF-8 roto");
}

// -------------------------------------------------------------
//  6) Reintentos con espera progresiva
// -------------------------------------------------------------
static void testRetry(){
  std::printf("[flexlink] reintentos: progresivos, con tope y con rendicion\n");
  uint32_t prev = 0;
  for(uint8_t i = 0; i < FLNK_RETRY_MAX; i++){
    uint32_t d = flexLinkRetryDelayMs(i);
    CHECK(d > 0, "intento %u sin espera", i);
    CHECK(d >= prev, "la espera no crece en el intento %u", i);
    CHECK(d <= FLNK_RETRY_CAP_MS, "la espera se paso del tope en %u", i);
    prev = d;
  }
  CHECK(flexLinkRetryDelayMs(FLNK_RETRY_MAX) == 0, "no se rinde tras el ultimo intento");
  CHECK(flexLinkRetryDelayMs(200) == 0, "no se rinde con un intento absurdo");
}

int main(){
  std::printf("\n=== FlexOS · protocolo Flex Link (P4 <-> Flex Phone) ===\n");
  testRoundTrip();
  testRejects();
  testFragments();
  testAntiReplay();
  testFields();
  testRetry();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
