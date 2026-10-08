// #############################################################
// ##  FLEX AUTH -- implementacion
// ##  Ver FlexOS_FlexAuth.h para el contrato y, sobre todo, para
// ##  lo que esta capa NO protege.
// #############################################################
#include "FlexOS_FlexAuth.h"
#include <string.h>

// =============================================================
//  SHA-256  (FIPS 180-4)
// =============================================================
static const uint32_t K256[64] = {
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};

static inline uint32_t rotr32(uint32_t x, int n){ return (x >> n) | (x << (32 - n)); }

static void sha256Block(uint32_t st[8], const uint8_t b[64]){
  uint32_t w[64];
  for(int i = 0; i < 16; i++){
    w[i] = ((uint32_t)b[i*4] << 24) | ((uint32_t)b[i*4+1] << 16) |
           ((uint32_t)b[i*4+2] << 8) | (uint32_t)b[i*4+3];
  }
  for(int i = 16; i < 64; i++){
    const uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
    const uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  uint32_t a = st[0], bb = st[1], c = st[2], d = st[3];
  uint32_t e = st[4], f = st[5], g = st[6], h = st[7];
  for(int i = 0; i < 64; i++){
    const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t t1 = h + S1 + ch + K256[i] + w[i];
    const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
    const uint32_t mj = (a & bb) ^ (a & c) ^ (bb & c);
    const uint32_t t2 = S0 + mj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = bb; bb = a; a = t1 + t2;
  }
  st[0] += a; st[1] += bb; st[2] += c; st[3] += d;
  st[4] += e; st[5] += f;  st[6] += g; st[7] += h;
}

void flexSha256Init(FlexSha256* c){
  if(!c) return;
  c->state[0] = 0x6a09e667u; c->state[1] = 0xbb67ae85u;
  c->state[2] = 0x3c6ef372u; c->state[3] = 0xa54ff53au;
  c->state[4] = 0x510e527fu; c->state[5] = 0x9b05688cu;
  c->state[6] = 0x1f83d9abu; c->state[7] = 0x5be0cd19u;
  c->bits = 0;
  c->at   = 0;
  memset(c->buf, 0, sizeof(c->buf));
}

void flexSha256Update(FlexSha256* c, const void* data, size_t n){
  if(!c || (n && !data)) return;
  const uint8_t* p = (const uint8_t*)data;
  c->bits += (uint64_t)n * 8u;
  while(n){
    const size_t room = FLXA_BLOCK_SIZE - c->at;
    const size_t take = n < room ? n : room;
    memcpy(c->buf + c->at, p, take);
    c->at += take; p += take; n -= take;
    if(c->at == FLXA_BLOCK_SIZE){ sha256Block(c->state, c->buf); c->at = 0; }
  }
}

void flexSha256Final(FlexSha256* c, uint8_t out[FLXA_SHA256_SIZE]){
  if(!c || !out) return;
  const uint64_t bits = c->bits;
  c->buf[c->at++] = 0x80;
  if(c->at > FLXA_BLOCK_SIZE - 8){
    memset(c->buf + c->at, 0, FLXA_BLOCK_SIZE - c->at);
    sha256Block(c->state, c->buf);
    c->at = 0;
  }
  memset(c->buf + c->at, 0, FLXA_BLOCK_SIZE - 8 - c->at);
  for(int i = 0; i < 8; i++) c->buf[FLXA_BLOCK_SIZE - 1 - i] = (uint8_t)(bits >> (8 * i));
  sha256Block(c->state, c->buf);
  for(int i = 0; i < 8; i++){
    out[i*4]   = (uint8_t)(c->state[i] >> 24);
    out[i*4+1] = (uint8_t)(c->state[i] >> 16);
    out[i*4+2] = (uint8_t)(c->state[i] >> 8);
    out[i*4+3] = (uint8_t)(c->state[i]);
  }
  // El contexto lleva material derivado de la clave: no se deja en
  // memoria para que lo encuentre lo siguiente que use esa pila.
  memset(c, 0, sizeof(*c));
}

void flexSha256(const void* data, size_t n, uint8_t out[FLXA_SHA256_SIZE]){
  FlexSha256 c;
  flexSha256Init(&c);
  flexSha256Update(&c, data, n);
  flexSha256Final(&c, out);
}

// =============================================================
//  HMAC-SHA256
// =============================================================
void flexHmacSha256(const uint8_t* key, size_t keyN,
                    const void* msg, size_t msgN,
                    uint8_t out[FLXA_SHA256_SIZE]){
  if(!out) return;
  uint8_t k[FLXA_BLOCK_SIZE];
  memset(k, 0, sizeof(k));
  if(keyN > FLXA_BLOCK_SIZE){
    flexSha256(key, keyN, k);            // clave larga: se resume primero
  } else if(keyN && key){
    memcpy(k, key, keyN);
  }
  uint8_t ipad[FLXA_BLOCK_SIZE], opad[FLXA_BLOCK_SIZE];
  for(size_t i = 0; i < FLXA_BLOCK_SIZE; i++){
    ipad[i] = (uint8_t)(k[i] ^ 0x36);
    opad[i] = (uint8_t)(k[i] ^ 0x5C);
  }
  uint8_t inner[FLXA_SHA256_SIZE];
  FlexSha256 c;
  flexSha256Init(&c);
  flexSha256Update(&c, ipad, sizeof(ipad));
  flexSha256Update(&c, msg, msgN);
  flexSha256Final(&c, inner);

  flexSha256Init(&c);
  flexSha256Update(&c, opad, sizeof(opad));
  flexSha256Update(&c, inner, sizeof(inner));
  flexSha256Final(&c, out);

  memset(k, 0, sizeof(k));
  memset(ipad, 0, sizeof(ipad));
  memset(opad, 0, sizeof(opad));
  memset(inner, 0, sizeof(inner));
}

bool flexAuthEqual(const uint8_t* a, const uint8_t* b, size_t n){
  if(!a || !b) return false;
  uint8_t diff = 0;
  // Sin salida anticipada A PROPOSITO: el tiempo no debe depender de
  // cuantos bytes coinciden.
  for(size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

// =============================================================
//  Derivacion del vinculo
// =============================================================
// Texto de dominio. Cambiarlo invalida todos los vinculos, que es
// exactamente lo que debe pasar si el esquema cambia.
static const char FLXA_PAIR_TAG[] = "flexphone-pair-v2";
static const char FLXA_PEER_TAG[] = "flexphone-peer-v2";
static const char FLXA_HOST_TAG[] = "flexphone-host-v2";

// Longitud acotada: un id que venga del aire puede no estar
// terminado, asi que nunca se recorre mas alla de FLXA_ID_MAX.
static size_t idLen(const char* s){
  if(!s) return 0;
  size_t n = 0;
  while(n < FLXA_ID_MAX && s[n]) n++;
  return n;
}

void flexAuthDeriveKey(const char* code,
                       const uint8_t salt[FLXA_SALT_SIZE],
                       const char* flexosId, const char* phoneId,
                       uint8_t out[FLXA_KEY_SIZE]){
  if(!out) return;
  memset(out, 0, FLXA_KEY_SIZE);
  if(!code || !salt) return;

  // El mensaje lleva la longitud de cada id delante. Sin eso,
  // ("ab","c") y ("a","bc") darian la MISMA clave, y eso permite
  // que dos emparejamientos distintos colisionen.
  uint8_t msg[sizeof(FLXA_PAIR_TAG) + FLXA_SALT_SIZE + 2 + 2 * FLXA_ID_MAX];
  size_t at = 0;
  memcpy(msg + at, FLXA_PAIR_TAG, sizeof(FLXA_PAIR_TAG) - 1); at += sizeof(FLXA_PAIR_TAG) - 1;
  memcpy(msg + at, salt, FLXA_SALT_SIZE); at += FLXA_SALT_SIZE;
  const size_t la = idLen(flexosId), lb = idLen(phoneId);
  msg[at++] = (uint8_t)la;
  if(la) memcpy(msg + at, flexosId, la);
  at += la;
  msg[at++] = (uint8_t)lb;
  if(lb) memcpy(msg + at, phoneId, lb);
  at += lb;

  const size_t codeN = idLen(code);
  flexHmacSha256((const uint8_t*)code, codeN, msg, at, out);
  memset(msg, 0, sizeof(msg));
}

void flexAuthProof(const uint8_t key[FLXA_KEY_SIZE], uint8_t role,
                   const uint8_t nonce[FLXA_NONCE_SIZE], uint16_t session,
                   uint8_t out[FLXA_PROOF_SIZE]){
  if(!out) return;
  memset(out, 0, FLXA_PROOF_SIZE);
  if(!key || !nonce) return;
  const char* tag = (role == FLXA_ROLE_HOST) ? FLXA_HOST_TAG : FLXA_PEER_TAG;
  const size_t tagN = (role == FLXA_ROLE_HOST) ? sizeof(FLXA_HOST_TAG) - 1
                                               : sizeof(FLXA_PEER_TAG) - 1;
  uint8_t msg[sizeof(FLXA_PEER_TAG) + FLXA_NONCE_SIZE + 2];
  size_t at = 0;
  memcpy(msg + at, tag, tagN); at += tagN;
  memcpy(msg + at, nonce, FLXA_NONCE_SIZE); at += FLXA_NONCE_SIZE;
  msg[at++] = (uint8_t)(session & 0xFF);
  msg[at++] = (uint8_t)(session >> 8);
  flexHmacSha256(key, FLXA_KEY_SIZE, msg, at, out);
  memset(msg, 0, sizeof(msg));
}

bool flexAuthVerify(const uint8_t key[FLXA_KEY_SIZE], uint8_t role,
                    const uint8_t nonce[FLXA_NONCE_SIZE], uint16_t session,
                    const uint8_t got[FLXA_PROOF_SIZE]){
  if(!got) return false;
  uint8_t want[FLXA_PROOF_SIZE];
  flexAuthProof(key, role, nonce, session, want);
  const bool ok = flexAuthEqual(want, got, FLXA_PROOF_SIZE);
  memset(want, 0, sizeof(want));
  return ok;
}

void flexAuthFormatCode(uint32_t rnd, char out[7]){
  if(!out) return;
  uint32_t v = rnd % 1000000u;
  for(int i = 5; i >= 0; i--){ out[i] = (char)('0' + (v % 10u)); v /= 10u; }
  out[6] = 0;
}

void flexAuthRandomBytes(FlexAuthRandFn rnd, uint8_t* out, size_t n){
  if(!out) return;
  if(!rnd){ memset(out, 0, n); return; }
  size_t at = 0;
  while(at < n){
    const uint32_t v = rnd();
    for(int i = 0; i < 4 && at < n; i++) out[at++] = (uint8_t)(v >> (8 * i));
  }
}
