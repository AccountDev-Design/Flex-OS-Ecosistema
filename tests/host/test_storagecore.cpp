// #############################################################
//  FLEX STORAGE · NUCLEO PORTABLE DEL P4 (FlexOS_StorageCore.cpp)
//  ------------------------------------------------------------
//  El emparejamiento con el telefono (ECDH P-256 + codigo de 6 cifras +
//  aprobacion en pantalla), las ofertas de un solo uso, el registro que se
//  guarda en la NVS y el reto-respuesta de las sesiones.
//
//  Los vectores dorados son LOS MISMOS que fija CoreTest.kt en el modulo
//  :storage de Android (ECDH: RFC 5903, seccion 8.1). Si un lado cambia lo
//  que entra en un HMAC y el otro no, las dos baterias lo dicen antes de
//  que nadie empareje nada.
//
//  Con sanitizers: todo lo que analiza llega por la Wi-Fi de cualquiera.
// #############################################################
#include "FlexOS_StorageCore.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static int gChecks = 0, gFails = 0;
#define CHECK(c, m) do { gChecks++; if(!(c)){ gFails++; printf("  FALLO: %s (%s:%d)\n", m, __FILE__, __LINE__); } } while(0)

// Azar determinista para la prueba (xorshift64).
struct TRng { uint64_t s; };
static void trand(void* ctx, uint8_t* out, size_t n){
  TRng* r = (TRng*)ctx;
  for(size_t i = 0; i < n; i++){
    r->s ^= r->s << 13; r->s ^= r->s >> 7; r->s ^= r->s << 17;
    out[i] = (uint8_t)(r->s >> 29);
  }
}

static std::string hex(const uint8_t* b, size_t n){
  std::string s(2 * n, '0');
  std::vector<char> t(2 * n + 1);
  fstHex(b, n, t.data());
  s.assign(t.data());
  return s;
}
static std::vector<uint8_t> unhex(const char* s){
  std::vector<uint8_t> v(strlen(s) / 2);
  if(!fstUnhex(s, v.data(), v.size())) v.clear();
  return v;
}

static bool validUtf8(const char* s){
  const unsigned char* p = (const unsigned char*)s;
  while(*p){
    int n = *p < 0x80 ? 0 : (*p >> 5) == 6 ? 1 : (*p >> 4) == 14 ? 2 : (*p >> 3) == 30 ? 3 : -1;
    if(n < 0) return false;
    p++;
    for(int i = 0; i < n; i++, p++) if((*p & 0xC0) != 0x80) return false;
  }
  return true;
}

// ---------------------------------------------------------------- vectores
static const char* Z_RFC   = "d6840f6b42f6edafd13116e0e12565202fef8e9ece7dce03812464d04b9442de";
static const char* OFFER   = "00112233445566778899aabbccddeeff";
static const char* P4ID    = "flexos-a1b2c3d4e5f6";
static const char* PHID    = "a55-0f1e2d3c4b5a6978";
static const char* PAIRID  = "0123456789abcdef0123456789abcdef";
static const char* NONCE   = "fedcba9876543210fedcba9876543210";
static const char* TOKEN   = "00112233445566778899aabbccddeeff0011223344556677";
static const char* K_GOLD  = "d80ca9748fade0c3dcc6a8df78aa9acb8a4e6aec13b3277716fd5c957aed097c";

static void testVectors(){
  printf("-- vectores dorados (los mismos que CoreTest.kt) --\n");
  std::vector<uint8_t> z = unhex(Z_RFC);
  CHECK(z.size() == 32, "Z del RFC 5903");
  uint8_t k[32], o[32];
  fstDeriveKey(z.data(), OFFER, P4ID, PHID, k);
  CHECK(hex(k, 32) == K_GOLD, "clave del emparejamiento");
  char sas[7]; fstSas(k, sas);
  CHECK(!strcmp(sas, "119168"), "codigo de verificacion de 6 cifras");
  fstPhoneProof(k, PAIRID, o);
  CHECK(hex(o, 32) == "20d687744e9c94e2d562be6f4c94840058a04a7ad93753dddcfe8e50f2586dc6", "prueba del telefono");
  fstP4Proof(k, PAIRID, o);
  CHECK(hex(o, 32) == "6e6fef9fbe2bfb39364bca62080214c59bbacbb935e8060b4de11cdf623e65e1", "prueba del P4");
  fstKnownProof(k, OFFER, o);
  CHECK(hex(o, 32) == "b08f4049448d95087446408936384c22d0cc8178b2f8b8f15095527a26fef563", "telefono conocido");
  fstSessionMac(k, NONCE, P4ID, o);
  CHECK(hex(o, 32) == "1516b0c29c91791b1415367353843c1cf9f43c19dc7dd07a034c66fd011bbb2b", "MAC de la sesion");
  fstSessionOk(k, NONCE, TOKEN, o);
  CHECK(hex(o, 32) == "9be1ad4b63fe1e602ce8c0ce9030bd77f68c4ac316931e94e1c047f28e70bdb1", "prueba de la sesion del telefono");

  // Los campos llevan la longitud delante: ("ab","c") y ("a","bc") no chocan.
  uint8_t a[32], b[32];
  fstDeriveKey(z.data(), "ab", "c", "d", a);
  fstDeriveKey(z.data(), "a", "bc", "d", b);
  CHECK(memcmp(a, b, 32) != 0, "longitud delante de cada campo");
  // Etiquetas distintas por rol: la prueba de un lado no sirve al otro.
  uint8_t p1[32], p2[32];
  fstPhoneProof(k, PAIRID, p1); fstP4Proof(k, PAIRID, p2);
  CHECK(memcmp(p1, p2, 32) != 0, "pruebas del telefono y del P4 distintas");

  // Un campo imposible (> 255 bytes) no da un valor fijo: depende de la clave.
  std::string big(400, 'x');
  uint8_t k2[32]; memcpy(k2, k, 32); k2[0] ^= 1;
  uint8_t c1[32], c2[32], zero[32] = {0};
  fstPhoneProof(k, big.c_str(), c1); fstPhoneProof(k2, big.c_str(), c2);
  CHECK(memcmp(c1, zero, 32) && memcmp(c1, c2, 32), "campo demasiado largo: nunca un valor adivinable");
}

static void testHex(){
  printf("-- hexadecimal --\n");
  uint8_t b[4];
  CHECK(fstUnhex("00ff7Fa0", b, 4) && b[0] == 0 && b[1] == 0xff && b[2] == 0x7f && b[3] == 0xa0, "mayusculas y minusculas");
  CHECK(!fstUnhex("00ff7fa", b, 4), "longitud impar");
  CHECK(!fstUnhex("00ff7fa0aa", b, 4), "longitud de mas");
  CHECK(!fstUnhex("zz", b, 1), "cifras que no son hexadecimales");
  CHECK(!fstUnhex(NULL, b, 1), "nulo");
  CHECK(!fstUnhex("0 ", b, 1), "espacio");
  char out[9]; fstHex(b, 4, out);
  CHECK(!strcmp(out, "00ff7fa0"), "ida y vuelta en minusculas");
}

static void testText(){
  printf("-- textos que llegan de la red --\n");
  char o[16];
  fstCleanText(o, sizeof(o), "  Galaxy\tA55\n ");
  CHECK(!strcmp(o, "Galaxy A55"), "controles por espacios y sin espacios a los lados");
  fstCleanText(o, sizeof(o), "Teléfono de Ña");               // 16 bytes en UTF-8
  CHECK(validUtf8(o) && strlen(o) < sizeof(o), "recorta sin partir un caracter");
  CHECK(!strcmp(o, "Teléfono de Ñ") || !strcmp(o, "Teléfono de "), "lo que cabe");
  fstCleanText(o, sizeof(o), "\xC3\x28\xF0\x9F\x98");         // secuencias rotas
  CHECK(validUtf8(o) && !strcmp(o, "?(?"), "cada secuencia rota, un solo ? (tambien la cortada al final)");
  fstCleanText(o, sizeof(o), "\xC0\xAF\xED\xA0\x80");         // forma larga y sustituto
  CHECK(validUtf8(o) && !strchr(o, (char)0xC0) && !strchr(o, (char)0xED), "sin formas largas ni sustitutos");
  fstCleanText(o, sizeof(o), "\xF0\x9F\x93\xB1 A55");         // emoji de 4 bytes
  CHECK(!strcmp(o, "\xF0\x9F\x93\xB1 A55"), "emoji valido intacto");
  char t[4];
  fstCleanText(t, sizeof(t), "\xF0\x9F\x93\xB1");
  CHECK(t[0] == 0, "un emoji que no cabe no entra a medias");
  fstCleanText(o, 1, "abc");
  CHECK(o[0] == 0, "capacidad 1");
  fstCleanText(o, sizeof(o), NULL);
  CHECK(o[0] == 0, "nulo");
}

static void testIp(){
  printf("-- IPs de la red local --\n");
  const char* good[] = { "10.0.0.1", "192.168.1.50", "172.16.0.9", "172.31.255.254", "169.254.3.4", "100.64.0.1", "127.0.0.1" };
  for(const char* g : good) CHECK(fstLocalIpv4(g), g);
  const char* bad[] = { "8.8.8.8", "172.32.0.1", "172.15.0.1", "100.128.0.1", "192.169.0.1", "010.0.0.1", " 10.0.0.1",
                        "10.0.0.1 ", "10.0.0", "10.0.0.256", "10.0.0.1.5", "+10.0.0.1", "10.-1.0.1", "", "fe80::1",
                        "flexos.local", "4294967306.0.0.1" };
  for(const char* b : bad) CHECK(!fstLocalIpv4(b), b);
  CHECK(!fstLocalIpv4(NULL), "nulo");
}

static void testEcdh(){
  printf("-- ECDH P-256 (%s) --\n", fstEcdhBackend());
  std::vector<uint8_t> di = unhex("c88f01f510d9ac3f70a292daa2316de544e9aab8afe84049c62a9c57862d1433");
  std::vector<uint8_t> dr = unhex("c6ef9c5d78ae012a011164acb397ce2088685d8f06bf9be0b283ab46476bee53");
  FstEcdh a, b;
  CHECK(fstEcdhFromPrivate(&a, di.data()) && fstEcdhFromPrivate(&b, dr.data()), "claves privadas del RFC 5903");
  CHECK(hex(a.pub, 65) == "04dad0b65394221cf9b051e1feca5787d098dfe637fc90b9ef945d0c37725811805271a0461cdb8252d61f1c456fa3e59ab1f45b33accf5f58389e0577b8990bb3", "gi");
  CHECK(hex(b.pub, 65) == "04d12dfb5289c8d4f81208b70270398c342296970a0bccb74c736fc7554494bf6356fbf3ca366cc23e8157854c13c58d6aac23f046ada30f8353e74f33039872ab", "gr");
  TRng r = { 0x1234567887654321ull };
  uint8_t z1[32], z2[32];
  CHECK(fstEcdhShared(&a, b.pub, z1, trand, &r) && hex(z1, 32) == Z_RFC, "Z desde el iniciador");
  CHECK(fstEcdhShared(&b, a.pub, z2, trand, &r) && hex(z2, 32) == Z_RFC, "Z desde el respondedor");

  // Claves nuevas: los dos lados llegan al mismo secreto.
  FstEcdh x, y;
  CHECK(fstEcdhGenerate(&x, trand, &r) && fstEcdhGenerate(&y, trand, &r), "generar");
  CHECK(x.pub[0] == 0x04 && memcmp(x.pub, y.pub, 65), "puntos sin comprimir y distintos");
  CHECK(fstEcdhShared(&x, y.pub, z1, trand, &r) && fstEcdhShared(&y, x.pub, z2, trand, &r) && !memcmp(z1, z2, 32), "mismo secreto");

  // Ataque de curva invalida: un punto fuera de P-256 se rechaza, no se usa.
  uint8_t bad[65]; memcpy(bad, b.pub, 65); bad[64] ^= 1;
  memset(z1, 0xAA, 32);
  CHECK(!fstEcdhShared(&a, bad, z1, trand, &r), "punto fuera de la curva");
  uint8_t zero[32] = {0};
  CHECK(!memcmp(z1, zero, 32), "sin secreto a medias si falla");
  memcpy(bad, b.pub, 65); bad[0] = 0x02;
  CHECK(!fstEcdhShared(&a, bad, z1, trand, &r), "punto comprimido en un hueco de 65 bytes");
  memset(bad, 0, 65); bad[0] = 0x04;
  CHECK(!fstEcdhShared(&a, bad, z1, trand, &r), "(0,0)");
  memset(bad, 0xFF, 65); bad[0] = 0x04;
  CHECK(!fstEcdhShared(&a, bad, z1, trand, &r), "coordenadas >= p");
  CHECK(!fstEcdhShared(NULL, b.pub, z1, trand, &r) && !fstEcdhShared(&a, NULL, z1, trand, &r), "nulos");
  fstEcdhWipe(&a);
  bool clean = true; for(uint8_t v : a.priv) if(v) clean = false;
  CHECK(clean, "la clave privada se borra");
}

static void testRecord(){
  printf("-- registro del telefono (NVS) --\n");
  FstPhone p; memset(&p, 0, sizeof(p));
  p.valid = 1; p.enabled = 1;
  snprintf(p.id, sizeof(p.id), "%s", PHID);
  snprintf(p.name, sizeof(p.name), "Galaxy A55 de Ana \xF0\x9F\x93\xB1");
  snprintf(p.model, sizeof(p.model), "SM-A556B");
  std::vector<uint8_t> k = unhex(K_GOLD); memcpy(p.key, k.data(), 32);
  snprintf(p.ip, sizeof(p.ip), "192.168.1.50");
  p.port = 47830; p.pairedEpoch = 1790000000u;
  uint8_t buf[FST_PHONE_BLOB_MAX];
  size_t n = fstPhoneEncode(&p, buf, sizeof(buf));
  CHECK(n > 0 && n <= FST_PHONE_BLOB_MAX, "cabe en el blob");
  FstPhone q;
  CHECK(fstPhoneDecode(&q, buf, n), "se vuelve a leer");
  CHECK(q.valid && q.enabled && !strcmp(q.id, p.id) && !strcmp(q.name, p.name) && !strcmp(q.model, p.model) &&
        !memcmp(q.key, p.key, 32) && !strcmp(q.ip, p.ip) && q.port == 47830 && q.pairedEpoch == 1790000000u, "identico");
  char url[64];
  CHECK(fstPhoneBaseUrl(&q, url, sizeof(url)) && !strcmp(url, "http://192.168.1.50:47830/api/cloud"), "URL base de Flex Cloud");
  CHECK(!fstPhoneBaseUrl(&q, url, 20), "URL que no cabe");

  // Desconectado (sin olvidarlo).
  p.enabled = 0; n = fstPhoneEncode(&p, buf, sizeof(buf));
  CHECK(fstPhoneDecode(&q, buf, n) && q.valid && !q.enabled, "desactivado se conserva");

  // Cualquier byte danado: se descarta entero.
  int okFlips = 0;
  for(size_t i = 0; i < n; i++) for(int bit = 0; bit < 8; bit++){
    buf[i] ^= (uint8_t)(1 << bit);
    if(fstPhoneDecode(&q, buf, n)) okFlips++;
    buf[i] ^= (uint8_t)(1 << bit);
  }
  CHECK(okFlips == 0, "ningun bit cambiado pasa el CRC");
  for(size_t L = 0; L < n; L++) CHECK(!fstPhoneDecode(&q, buf, L), "truncado");
  CHECK(!q.valid, "un fallo deja el registro vacio");
  CHECK(fstPhoneDecode(&q, buf, n), "el original sigue valiendo");

  // Un registro con CRC bueno pero contenido malo (otra version, IP publica, id raro).
  auto reseal = [&](std::vector<uint8_t>& v){
    uint32_t c = 0xFFFFFFFFu;
    for(size_t i = 0; i + 4 < v.size(); i++){ c ^= v[i]; for(int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
    c = ~c; size_t e = v.size() - 4;
    v[e] = (uint8_t)c; v[e + 1] = (uint8_t)(c >> 8); v[e + 2] = (uint8_t)(c >> 16); v[e + 3] = (uint8_t)(c >> 24);
  };
  std::vector<uint8_t> v(buf, buf + n);
  v[4] = 2; reseal(v);
  CHECK(!fstPhoneDecode(&q, v.data(), v.size()), "version desconocida");
  FstPhone pub = p; snprintf(pub.ip, sizeof(pub.ip), "8.8.8.8"); pub.enabled = 1;
  n = fstPhoneEncode(&pub, buf, sizeof(buf));
  CHECK(n && !fstPhoneDecode(&q, buf, n), "IP publica: no se acepta");
  FstPhone weird = p; snprintf(weird.id, sizeof(weird.id), "../a");
  n = fstPhoneEncode(&weird, buf, sizeof(buf));
  CHECK(n && !fstPhoneDecode(&q, buf, n), "id con caracteres raros");
  FstPhone noport = p; noport.port = 0;
  n = fstPhoneEncode(&noport, buf, sizeof(buf));
  CHECK(n && !fstPhoneDecode(&q, buf, n), "puerto 0");
  CHECK(!fstPhoneEncode(&p, buf, 40), "no escribe fuera de un hueco pequeno");
  FstPhone inval; memset(&inval, 0, sizeof(inval));
  CHECK(!fstPhoneEncode(&inval, buf, sizeof(buf)), "registro vacio: nada que guardar");

  // Basura al azar: nunca se acepta ni se sale del bufer.
  TRng r = { 0xC0FFEEull };
  for(int it = 0; it < 4000; it++){
    uint8_t g[FST_PHONE_BLOB_MAX + 8]; uint8_t len; trand(&r, &len, 1);
    trand(&r, g, sizeof(g));
    if(it % 2) memcpy(g, "FST1\x01", 5);
    CHECK(!fstPhoneDecode(&q, g, len % sizeof(g)), "basura");
  }
  fstPhoneWipe(&q);
  bool clean = true; for(uint8_t b : q.key) if(b) clean = false;
  CHECK(clean && !q.valid, "borrar el registro borra la clave");
}

// ---------------------------------------------------------------- emparejamiento
// Lo que hace la app (AttachClient.kt) con lo que contesta el P4.
struct Phone {
  const char* id = PHID;
  FstEcdh e;
  char pub[131];
  uint8_t key[32];
  char proof[65];
  char sas[7];
};

static FstPairReq request(Phone& ph, const char* offer, TRng* r){
  fstEcdhGenerate(&ph.e, trand, r);
  fstHex(ph.e.pub, 65, ph.pub);
  FstPairReq q; memset(&q, 0, sizeof(q));
  q.offer = offer; q.pid = ph.id; q.name = "Galaxy A55 de Ana"; q.model = "SM-A556B"; q.port = "47830"; q.pub = ph.pub;
  return q;
}
// El telefono termina su mitad con la respuesta del P4.
static bool finish(Phone& ph, const FstCore& c, const FstPairResp& resp, const char* offer, TRng* r){
  uint8_t peer[65], z[32];
  if(!fstUnhex(resp.pub, peer, 65) || !fstEcdhShared(&ph.e, peer, z, trand, r)) return false;
  fstDeriveKey(z, offer, c.p4Id, ph.id, ph.key);
  fstSas(ph.key, ph.sas);
  uint8_t pr[32]; fstPhoneProof(ph.key, resp.pairId, pr); fstHex(pr, 32, ph.proof);
  return true;
}
static bool p4ProofOk(const Phone& ph, const char* pairId, const char* proofHex){
  uint8_t want[32]; fstP4Proof(ph.key, pairId, want);
  return hex(want, 32) == proofHex;
}

static void testPairing(){
  printf("-- emparejamiento --\n");
  TRng r = { 0x9E3779B97F4A7C15ull };
  FstCore c; fstInit(&c, P4ID, "Flex OS Ultra de Ana");
  CHECK(!strcmp(c.p4Id, P4ID) && !strcmp(c.p4Name, "Flex OS Ultra de Ana"), "identidad del P4");
  uint32_t now = 1000;
  char offer[FST_HEX32];
  CHECK(fstOfferNew(&c, now, trand, &r, offer) && strlen(offer) == 32, "oferta de 128 bits");

  Phone ph; FstPairReq q = request(ph, offer, &r);
  FstPairResp resp; char err[160]; uint32_t wait = 0;
  int st = fstPairBegin(&c, now, &q, "192.168.1.50", trand, &r, &resp, err, sizeof(err), &wait);
  CHECK(st == 202 && resp.approve == 1 && resp.expiresS == 120 && strlen(resp.pairId) == 32 && strlen(resp.pub) == 130, "202 y hay que aprobarlo");
  CHECK(finish(ph, c, resp, offer, &r), "el telefono completa el ECDH");
  CHECK(!strcmp(ph.sas, c.pair.sas), "MISMO codigo de 6 cifras en los dos lados");
  CHECK(fstPairWaiting(&c, now + 10), "la pantalla tiene algo que preguntar");
  CHECK(!strcmp(c.pair.name, "Galaxy A55 de Ana") && !strcmp(c.pair.model, "SM-A556B") && c.pair.port == 47830 &&
        !strcmp(c.pair.ip, "192.168.1.50"), "datos del telefono");

  // La oferta era de UN solo uso.
  Phone other; FstPairReq q2 = request(other, offer, &r);
  FstPairResp resp2;
  CHECK(fstPairBegin(&c, now, &q2, "192.168.1.51", trand, &r, &resp2, err, sizeof(err), &wait) == 403, "la oferta gastada no vale dos veces");
  CHECK(fstPairWaiting(&c, now + 10), "y no tumba el emparejamiento en curso");

  uint8_t state; char proof[65]; bool persist;
  st = fstPairPoll(&c, now + 1500, resp.pairId, ph.proof, &state, proof, &persist, err, sizeof(err));
  CHECK(st == 200 && state == FSTP_PENDING && !persist && !proof[0], "pendiente mientras nadie decide");
  CHECK(!c.phone.valid, "nada guardado antes de aprobarlo");

  CHECK(fstPairDecide(&c, true), "se aprueba en pantalla");
  CHECK(!fstPairDecide(&c, false), "no se decide dos veces");
  st = fstPairPoll(&c, now + 3000, resp.pairId, ph.proof, &state, proof, &persist, err, sizeof(err));
  CHECK(st == 200 && state == FSTP_DONE && persist, "emparejado: hay que guardarlo UNA vez");
  CHECK(p4ProofOk(ph, resp.pairId, proof), "el P4 demuestra que tiene la misma clave");
  CHECK(c.phone.valid && c.phone.enabled && !memcmp(c.phone.key, ph.key, 32) && !strcmp(c.phone.id, PHID) &&
        !strcmp(c.phone.ip, "192.168.1.50") && c.phone.port == 47830, "telefono emparejado");
  st = fstPairPoll(&c, now + 4500, resp.pairId, ph.proof, &state, proof, &persist, err, sizeof(err));
  CHECK(st == 200 && state == FSTP_DONE && !persist && p4ProofOk(ph, resp.pairId, proof), "se repite (la respuesta pudo perderse) sin volver a guardar");
  CHECK(!fstPairWaiting(&c, now + 4500), "ya no hay nada que preguntar");
  st = fstPairPoll(&c, now + 4500 + FST_PAIR_TTL_MS, resp.pairId, ph.proof, &state, proof, &persist, err, sizeof(err));
  CHECK(st == 410, "el emparejamiento terminado caduca");
  CHECK(c.phone.valid, "y el telefono sigue emparejado");
  FstPhone saved = c.phone;

  // ---- El mismo telefono vuelve (otra IP): demuestra su clave y no se pregunta.
  now += 600000;
  CHECK(fstOfferNew(&c, now, trand, &r, offer), "oferta nueva");
  Phone again; again.id = PHID;
  FstPairReq q3 = request(again, offer, &r);
  uint8_t kp[32]; char kph[65];
  fstKnownProof(saved.key, offer, kp); fstHex(kp, 32, kph);
  q3.kp4 = P4ID; q3.known = kph;
  st = fstPairBegin(&c, now, &q3, "192.168.1.77", trand, &r, &resp, err, sizeof(err), &wait);
  CHECK(st == 202 && resp.approve == 0, "telefono conocido: sin preguntar");
  CHECK(!fstPairWaiting(&c, now), "nada en la pantalla");
  CHECK(finish(again, c, resp, offer, &r), "ECDH");
  st = fstPairPoll(&c, now + 100, resp.pairId, again.proof, &state, proof, &persist, err, sizeof(err));
  CHECK(st == 200 && state == FSTP_DONE && persist && p4ProofOk(again, resp.pairId, proof), "listo al primer sondeo");
  CHECK(!strcmp(c.phone.ip, "192.168.1.77") && !memcmp(c.phone.key, again.key, 32) && memcmp(c.phone.key, saved.key, 32), "IP y clave nuevas");

  // ---- Prueba de "conocido" falsa, otro P4 u otro telefono: hay que preguntar.
  struct { const char* kp4; const char* pid; bool corrupt; const char* what; } fake[] = {
    { P4ID, PHID, true, "prueba de conocido falsa" },
    { "flexos-otro", PHID, false, "decia venir de otro P4" },
    { P4ID, "a55-otro", false, "otro telefono con la prueba del primero" },
  };
  for(auto& f : fake){
    now += 1000;
    fstOfferNew(&c, now, trand, &r, offer);
    Phone x; x.id = f.pid;
    FstPairReq qx = request(x, offer, &r);
    fstKnownProof(c.phone.key, offer, kp); if(f.corrupt) kp[5] ^= 0x40; fstHex(kp, 32, kph);
    qx.kp4 = f.kp4; qx.known = kph;
    st = fstPairBegin(&c, now, &qx, "192.168.1.78", trand, &r, &resp, err, sizeof(err), &wait);
    CHECK(st == 202 && resp.approve == 1, f.what);
  }
  fstPairCancel(&c);

  // ---- Rechazado en pantalla.
  now += 1000;
  fstOfferNew(&c, now, trand, &r, offer);
  Phone d; FstPairReq qd = request(d, offer, &r);
  CHECK(fstPairBegin(&c, now, &qd, "10.0.0.8", trand, &r, &resp, err, sizeof(err), &wait) == 202, "otro emparejamiento");
  finish(d, c, resp, offer, &r);
  FstPhone before = c.phone;
  CHECK(fstPairDecide(&c, false), "rechazar");
  st = fstPairPoll(&c, now + 100, resp.pairId, d.proof, &state, proof, &persist, err, sizeof(err));
  CHECK(st == 403 && !persist && strstr(err, "Rechazado"), "403 rechazado");
  CHECK(!memcmp(&before, &c.phone, sizeof(before)), "el telefono anterior sigue igual");
  CHECK(fstPairPoll(&c, now + 200, resp.pairId, d.proof, &state, proof, &persist, err, sizeof(err)) == 404, "y la solicitud ya no existe");

  // ---- Pruebas falsas: a la tercera se cancela.
  now += 1000;
  fstOfferNew(&c, now, trand, &r, offer);
  Phone m; FstPairReq qm = request(m, offer, &r);
  fstPairBegin(&c, now, &qm, "10.0.0.9", trand, &r, &resp, err, sizeof(err), &wait);
  finish(m, c, resp, offer, &r);
  char badProof[65]; memcpy(badProof, m.proof, 65); badProof[0] = badProof[0] == 'a' ? 'b' : 'a';
  CHECK(fstPairPoll(&c, now, resp.pairId, badProof, &state, proof, &persist, err, sizeof(err)) == 403, "prueba falsa 1");
  CHECK(fstPairPoll(&c, now, resp.pairId, "nohex", &state, proof, &persist, err, sizeof(err)) == 403, "prueba que no es hexadecimal");
  CHECK(fstPairPoll(&c, now, resp.pairId, m.proof, &state, proof, &persist, err, sizeof(err)) == 200, "la buena aun vale");
  CHECK(fstPairPoll(&c, now, resp.pairId, NULL, &state, proof, &persist, err, sizeof(err)) == 403, "prueba falsa 3");
  CHECK(fstPairPoll(&c, now, resp.pairId, m.proof, &state, proof, &persist, err, sizeof(err)) == 404, "cancelado tras 3 pruebas falsas");
  CHECK(!fstPairDecide(&c, true), "nada que aprobar");

  // ---- Se acaba el tiempo de aprobarlo.
  now += 1000;
  fstOfferNew(&c, now, trand, &r, offer);
  Phone t; FstPairReq qt = request(t, offer, &r);
  fstPairBegin(&c, now, &qt, "10.0.0.10", trand, &r, &resp, err, sizeof(err), &wait);
  finish(t, c, resp, offer, &r);
  CHECK(fstPairWaiting(&c, now + FST_PAIR_TTL_MS), "justo en el limite aun espera");
  CHECK(!fstPairWaiting(&c, now + FST_PAIR_TTL_MS + 1), "pasado el limite, la pantalla lo olvida");
  CHECK(fstPairPoll(&c, now + FST_PAIR_TTL_MS + 2, resp.pairId, t.proof, &state, proof, &persist, err, sizeof(err)) == 404, "y el telefono recibe 404");
  fstOfferNew(&c, now, trand, &r, offer);
  qt = request(t, offer, &r);
  fstPairBegin(&c, now, &qt, "10.0.0.10", trand, &r, &resp, err, sizeof(err), &wait);
  finish(t, c, resp, offer, &r);
  CHECK(fstPairPoll(&c, now + FST_PAIR_TTL_MS + 5, resp.pairId, t.proof, &state, proof, &persist, err, sizeof(err)) == 410, "sondeo tardio: 410");

  // ---- Id de emparejamiento que no existe.
  CHECK(fstPairPoll(&c, now, PAIRID, t.proof, &state, proof, &persist, err, sizeof(err)) == 404, "id desconocido");
  CHECK(fstPairPoll(&c, now, "abc", t.proof, &state, proof, &persist, err, sizeof(err)) == 404, "id corto");
  CHECK(fstPairPoll(&c, now, NULL, t.proof, &state, proof, &persist, err, sizeof(err)) == 404, "id nulo");

  // ---- Un emparejamiento nuevo cancela el que estaba a medias.
  now += 1000;
  char o1[FST_HEX32], o2[FST_HEX32];
  fstOfferNew(&c, now, trand, &r, o1); fstOfferNew(&c, now, trand, &r, o2);
  Phone p1, p2; FstPairReq a1 = request(p1, o1, &r), a2 = request(p2, o2, &r);
  FstPairResp r1, r2;
  fstPairBegin(&c, now, &a1, "10.0.0.11", trand, &r, &r1, err, sizeof(err), &wait);
  fstPairBegin(&c, now, &a2, "10.0.0.12", trand, &r, &r2, err, sizeof(err), &wait);
  finish(p1, c, r1, o1, &r); finish(p2, c, r2, o2, &r);
  CHECK(fstPairPoll(&c, now, r1.pairId, p1.proof, &state, proof, &persist, err, sizeof(err)) == 404, "el primero se cancelo");
  CHECK(fstPairPoll(&c, now, r2.pairId, p2.proof, &state, proof, &persist, err, sizeof(err)) == 200, "el segundo sigue");
  CHECK(!strcmp(c.pair.ip, "10.0.0.12"), "es el del segundo telefono");
  fstPairCancel(&c);
  CHECK(!fstPairWaiting(&c, now), "cancelado (la hoja se cerro)");
}

static void testOffers(){
  printf("-- ofertas y limitador --\n");
  TRng r = { 42 };
  FstCore c; fstInit(&c, P4ID, NULL);
  CHECK(!strcmp(c.p4Name, "Flex OS Ultra"), "nombre por defecto");
  FstCore bad; fstInit(&bad, "../x", "  ");
  CHECK(!strcmp(bad.p4Id, "flexos-p4") && !strcmp(bad.p4Name, ""), "id no valido: uno fijo");
  uint32_t now = 0xFFFFF000u;                         // millis() a punto de dar la vuelta
  char a[FST_HEX32], b[FST_HEX32], d[FST_HEX32];
  fstOfferNew(&c, now, trand, &r, a);
  fstOfferNew(&c, now + 10, trand, &r, b);
  fstOfferNew(&c, now + 20, trand, &r, d);            // la mas vieja (a) cede su sitio
  CHECK(strcmp(a, b) && strcmp(b, d), "ofertas distintas");
  char err[160]; uint32_t wait; FstPairResp resp;
  Phone ph; FstPairReq q = request(ph, a, &r);
  CHECK(fstPairBegin(&c, now + 30, &q, "192.168.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 403, "la oferta desplazada ya no vale");
  q = request(ph, b, &r);
  CHECK(fstPairBegin(&c, now + FST_OFFER_TTL_MS + 10, &q, "192.168.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 403,
        "caducada (aunque millis() haya dado la vuelta)");
  q = request(ph, d, &r);
  CHECK(fstPairBegin(&c, now + FST_OFFER_TTL_MS + 19, &q, "192.168.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 202,
        "justo antes de caducar, vale");

  // Ofertas falsas: a la quinta, espera creciente... pero una buena pasa SIEMPRE.
  FstCore l; fstInit(&l, P4ID, "P4");
  uint32_t t0 = 5000;
  char good[FST_HEX32]; fstOfferNew(&l, t0, trand, &r, good);
  int codes[7];
  for(int i = 0; i < 7; i++){
    FstPairReq f = request(ph, "ffffffffffffffffffffffffffffffff", &r);
    codes[i] = fstPairBegin(&l, t0 + i, &f, "192.168.0.66", trand, &r, &resp, err, sizeof(err), &wait);
  }
  CHECK(codes[0] == 403 && codes[4] == 403 && codes[5] == 429 && codes[6] == 429, "403 x5 y luego 429");
  CHECK(wait == 30, "dice cuanto esperar (30 s)");
  FstPairReq g = request(ph, good, &r);
  CHECK(fstPairBegin(&l, t0 + 10, &g, "192.168.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 202,
        "el telefono de verdad no queda bloqueado por la basura de otro");
  FstPairReq f = request(ph, "ffffffffffffffffffffffffffffffff", &r);
  CHECK(fstPairBegin(&l, t0 + 11, &f, "192.168.0.66", trand, &r, &resp, err, sizeof(err), &wait) == 403,
        "un intento bueno limpia el limitador");
  // Espera que crece y no pasa de 16 min.
  FstCore e; fstInit(&e, P4ID, "P4");
  uint32_t tt = 100;
  for(int i = 0; i < 40; i++){
    FstPairReq x = request(ph, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", &r);
    int s = fstPairBegin(&e, tt, &x, "192.168.0.66", trand, &r, &resp, err, sizeof(err), &wait);
    if(s == 429) tt += wait * 1000; else tt += 1;
  }
  CHECK(wait <= 16 * 60, "espera maxima de 16 min");
}

static void testHostileBegin(){
  printf("-- peticiones de emparejamiento hostiles --\n");
  TRng r = { 7 };
  FstCore c; fstInit(&c, P4ID, "P4");
  char offer[FST_HEX32]; char err[160]; uint32_t wait; FstPairResp resp;
  Phone ph;
  auto fresh = [&](){ fstOfferNew(&c, 1000, trand, &r, offer); return request(ph, offer, &r); };

  FstPairReq q = fresh(); std::string up(offer); for(char& ch : up) ch = (char)toupper(ch); q.offer = up.c_str();
  CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "oferta en mayusculas");
  const char* pids[] = { "", "a b", "../../x", "a/b", "0123456789012345678901234567890123456789" };
  for(const char* p : pids){ q = fresh(); q.pid = p; CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "id de telefono"); }
  const char* ports[] = { "", "0", "65536", "12a", "-1", " 80", "99999999999999999999" };
  for(const char* p : ports){ q = fresh(); q.port = p; CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "puerto"); }
  q = fresh(); q.port = NULL; CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "sin puerto");
  q = fresh(); std::string shortPub(ph.pub, 128); q.pub = shortPub.c_str();
  CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "clave publica corta");
  q = fresh(); std::string comp(ph.pub); comp[1] = '2'; q.pub = comp.c_str();
  CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "clave publica que no empieza por 04");
  q = fresh(); std::string off(ph.pub); off[129] = off[129] == '0' ? '1' : '0'; q.pub = off.c_str();
  CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "punto fuera de la curva");
  CHECK(!fstPairWaiting(&c, 1000), "nada a medias tras una clave mala");
  q = fresh();
  CHECK(fstPairBegin(&c, 1000, &q, "8.8.8.8", trand, &r, &resp, err, sizeof(err), &wait) == 403, "desde una IP publica");
  CHECK(fstPairBegin(&c, 1000, &q, NULL, trand, &r, &resp, err, sizeof(err), &wait) == 403, "sin IP");
  CHECK(fstPairBegin(&c, 1000, NULL, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "sin peticion");
  q = fresh(); q.offer = NULL;
  CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 400, "sin oferta");

  // Nombres hostiles: se limpian antes de llegar a la pantalla.
  q = fresh();
  std::string evil = "\x1b[2J\xC3\x28 Galaxy\x7f";
  evil += std::string(200, 'A');
  q.name = evil.c_str(); q.model = "\n\n";
  CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 202, "nombre hostil");
  CHECK(validUtf8(c.pair.name) && strlen(c.pair.name) < FST_NAME_MAX && !strchr(c.pair.name, 0x1b) && !strchr(c.pair.name, 0x7f), "nombre limpio");
  CHECK(c.pair.model[0] == 0, "modelo vacio");
  q = fresh(); q.name = "   ";
  CHECK(fstPairBegin(&c, 1000, &q, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait) == 202 && c.pair.name[0], "nombre en blanco: uno por defecto");

  // Peticiones al azar: ninguna rompe nada.
  for(int it = 0; it < 3000; it++){
    uint8_t raw[200]; trand(&r, raw, sizeof(raw));
    std::string s[6];
    for(int i = 0; i < 6; i++){
      size_t L = raw[i] % 140;
      for(size_t k = 0; k < L; k++){ char ch = (char)raw[(i * 31 + k) % sizeof(raw)]; s[i] += ch ? ch : 'x'; }
    }
    if(it % 3 == 0){ fstOfferNew(&c, 1000, trand, &r, offer); s[0] = offer; }
    if(it % 5 == 0) s[5] = ph.pub;
    FstPairReq x = { s[0].c_str(), s[1].c_str(), s[2].c_str(), s[3].c_str(), s[4].c_str(), s[5].c_str(), NULL, NULL };
    int st = fstPairBegin(&c, 1000 + it, &x, "10.0.0.2", trand, &r, &resp, err, sizeof(err), &wait);
    CHECK(st == 202 || st == 400 || st == 403 || st == 429, "estado conocido");
    CHECK(validUtf8(c.pair.name) && strlen(c.pair.name) < FST_NAME_MAX, "nombre siempre limpio");
  }
}

static void testSession(){
  printf("-- sesion P4 -> telefono --\n");
  char nonce[FST_HEX32];
  std::string ch = std::string("{\"nonce\":\"") + NONCE + "\",\"expiresIn\":60,\"phoneId\":\"" + PHID + "\"}";
  CHECK(fstParseChallenge(ch.data(), ch.size(), nonce) && !strcmp(nonce, NONCE), "reto");
  const char* badCh[] = { "{}", "{\"nonce\":5}", "{\"nonce\":\"FEDCBA9876543210FEDCBA9876543210\"}", "{\"nonce\":\"abc\"}", "[", "", "null" };
  for(const char* b : badCh) CHECK(!fstParseChallenge(b, strlen(b), nonce) && !nonce[0], "reto que no vale");
  for(size_t cut = 0; cut < ch.size(); cut++) CHECK(!fstParseChallenge(ch.data(), cut, nonce), "reto truncado");

  FstCore c; fstInit(&c, P4ID, "P4");
  char body[256];
  CHECK(fstSessionBody(&c, NONCE, body, sizeof(body)) == 0, "sin telefono emparejado no hay sesion");
  c.phone.valid = 1; std::vector<uint8_t> k = unhex(K_GOLD); memcpy(c.phone.key, k.data(), 32);
  size_t n = fstSessionBody(&c, NONCE, body, sizeof(body));
  CHECK(n > 0 && n == strlen(body), "cuerpo de la sesion");
  CHECK(strstr(body, "\"p4Id\":\"flexos-a1b2c3d4e5f6\"") && strstr(body, NONCE) &&
        strstr(body, "\"mac\":\"1516b0c29c91791b1415367353843c1cf9f43c19dc7dd07a034c66fd011bbb2b\""), "con el MAC dorado");
  CHECK(fstSessionBody(&c, NONCE, body, 40) == 0, "cuerpo que no cabe");
  CHECK(fstSessionBody(&c, "zz", body, sizeof(body)) == 0, "reto mal formado");

  std::string ok = std::string("{\"token\":\"") + TOKEN + "\",\"expiresIn\":1800,\"mac\":\"9be1ad4b63fe1e602ce8c0ce9030bd77f68c4ac316931e94e1c047f28e70bdb1\","
                   "\"phoneId\":\"" + PHID + "\",\"name\":\"Galaxy A55 de Ana\\u0007\"}";
  char token[FST_TOKEN_MAX]; uint32_t exp = 0; char name[FST_NAME_MAX];
  CHECK(fstParseSession(ok.data(), ok.size(), c.phone.key, NONCE, token, &exp, name, sizeof(name)), "sesion abierta");
  CHECK(!strcmp(token, TOKEN) && exp == 1800 && !strcmp(name, "Galaxy A55 de Ana"), "token, plazo y nombre limpio");

  // El telefono tiene que demostrar que tambien tiene la clave.
  std::string tam = ok; tam[tam.find("9be1") + 3] = 'f';
  CHECK(!fstParseSession(tam.data(), tam.size(), c.phone.key, NONCE, token, &exp, name, sizeof(name)) && !token[0], "MAC del telefono falso");
  std::string tok2 = ok; tok2[tok2.find(TOKEN) + 2] = 'f';
  CHECK(!fstParseSession(tok2.data(), tok2.size(), c.phone.key, NONCE, token, &exp, name, sizeof(name)), "token cambiado por el camino");
  CHECK(!fstParseSession(ok.data(), ok.size(), c.phone.key, "fedcba9876543210fedcba9876543211", token, &exp, name, sizeof(name)), "otro reto");
  uint8_t k2[32]; memcpy(k2, c.phone.key, 32); k2[31] ^= 1;
  CHECK(!fstParseSession(ok.data(), ok.size(), k2, NONCE, token, &exp, name, sizeof(name)), "otra clave");
  for(size_t cut = 0; cut < ok.size(); cut++)
    CHECK(!fstParseSession(ok.data(), cut, c.phone.key, NONCE, token, &exp, name, sizeof(name)), "respuesta truncada");
  std::string noexp = std::string("{\"token\":\"") + TOKEN + "\",\"mac\":\"9be1ad4b63fe1e602ce8c0ce9030bd77f68c4ac316931e94e1c047f28e70bdb1\"}";
  CHECK(fstParseSession(noexp.data(), noexp.size(), c.phone.key, NONCE, token, &exp, NULL, 0) && exp == 1800, "sin plazo: 30 min");
  std::string huge = std::string("{\"token\":\"") + TOKEN + "\",\"expiresIn\":1e30,\"mac\":\"9be1ad4b63fe1e602ce8c0ce9030bd77f68c4ac316931e94e1c047f28e70bdb1\"}";
  CHECK(fstParseSession(huge.data(), huge.size(), c.phone.key, NONCE, token, &exp, NULL, 0) && exp == 1800, "plazo absurdo: 30 min");
  std::string upper = ok; for(size_t i = ok.find(TOKEN), e = i + 48; i < e; i++) upper[i] = (char)toupper(upper[i]);
  CHECK(!fstParseSession(upper.data(), upper.size(), c.phone.key, NONCE, token, &exp, name, sizeof(name)), "token en mayusculas");

  // Basura al azar.
  TRng r = { 99 };
  for(int it = 0; it < 3000; it++){
    char g[96]; trand(&r, (uint8_t*)g, sizeof(g));
    size_t L = (uint8_t)g[0] % sizeof(g);
    CHECK(!fstParseSession(g, L, c.phone.key, NONCE, token, &exp, name, sizeof(name)), "basura (sesion)");
    CHECK(!fstParseChallenge(g, L, nonce), "basura (reto)");
  }
}

static void testHttpJson(){
  printf("-- respuestas HTTP del emparejamiento (contrato de AttachClient.kt) --\n");
  TRng r = { 0xABCDEFull };
  FstCore c; fstInit(&c, P4ID, "Flex \"OS\" \\ Ultra");
  char offer[FST_HEX32]; fstOfferNew(&c, 1000, trand, &r, offer);
  Phone ph; FstPairReq q = request(ph, offer, &r);
  char json[600]; uint32_t retry = 99;
  int st = fstPairBeginHttp(&c, 1000, &q, "192.168.1.5", trand, &r, json, sizeof(json), &retry);
  CHECK(st == 202 && retry == 0, "202");
  CHECK(strstr(json, "\"p4id\":\"flexos-a1b2c3d4e5f6\"") && strstr(json, "\"approve\":1") && strstr(json, "\"expiresIn\":120") &&
        strstr(json, "\"p4name\":\"Flex \\\"OS\\\" \\\\ Ultra\""), "nombre del P4 escapado como JSON");
  char pairId[33] = "";
  const char* pi = strstr(json, "\"pairId\":\"");
  if(pi) memcpy(pairId, pi + 10, 32);
  bool persist = true;
  st = fstPairPollHttp(&c, 1100, pairId, "00", json, sizeof(json), &persist);
  CHECK(st == 403 && strstr(json, "{\"error\":\"") == json && !persist, "prueba falsa: error JSON");
  fstOfferNew(&c, 1200, trand, &r, offer);
  FstPairReq bad = q; bad.offer = "ffffffffffffffffffffffffffffffff";
  for(int i = 0; i < 6; i++) st = fstPairBeginHttp(&c, 1200, &bad, "192.168.1.5", trand, &r, json, sizeof(json), &retry);
  CHECK(st == 429 && retry == 30 && strstr(json, "\"error\""), "429 con su espera");
  char small[40];
  fstErrorJson(small, sizeof(small), "un mensaje muy largo que no cabe en un buffer pequeno");
  CHECK(strlen(small) < sizeof(small), "error recortado sin salirse del buffer");
  char ctl[100];
  fstErrorJson(ctl, sizeof(ctl), "a\nb\x01" "c");
  CHECK(!strcmp(ctl, "{\"error\":\"a\\u000ab\\u0001c\"}"), "controles escapados");
}

int main(){
  printf("=== FlexOS · Flex Storage: nucleo portable del P4 ===\n");
  testVectors();
  testHex();
  testText();
  testIp();
  testEcdh();
  testRecord();
  testPairing();
  testOffers();
  testHostileBegin();
  testSession();
  testHttpJson();
  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
