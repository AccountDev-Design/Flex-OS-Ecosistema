// #############################################################
//  FLEX STORAGE · NUCLEO PORTABLE · implementacion
//  Ver FlexOS_StorageCore.h. Sin Arduino, sin red, sin NVS.
// #############################################################
#include "FlexOS_StorageCore.h"
#include "FlexOS_FlexAuth.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>

// ---------------------------------------------------------------------------
//  Backend de ECDH: mbedTLS en la placa (y en la prueba opcional del PC con
//  -DFST_FORCE_MBEDTLS), OpenSSL en el PC. El mismo criterio que el SHA-256
//  de FlexOS_CloudCore.
// ---------------------------------------------------------------------------
#if defined(FST_FORCE_MBEDTLS)
#  define FST_ECDH_MBEDTLS 1
#elif defined(FST_FORCE_OPENSSL)
#  define FST_ECDH_OPENSSL 1
#elif defined(__has_include)
#  if __has_include(<mbedtls/ecdh.h>)
#    define FST_ECDH_MBEDTLS 1
#  elif __has_include(<openssl/ec.h>)
#    define FST_ECDH_OPENSSL 1
#  endif
#endif
#if FST_ECDH_MBEDTLS
#  include "mbedtls/ecdh.h"
#  include "mbedtls/ecp.h"
#  include "mbedtls/bignum.h"
#elif FST_ECDH_OPENSSL
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  include <openssl/ec.h>
#  include <openssl/ecdh.h>
#  include <openssl/bn.h>
#  include <openssl/obj_mac.h>
#else
#  error "FlexOS_StorageCore: falta un backend de ECDH (mbedTLS u OpenSSL)"
#endif

static void wipe(void* p, size_t n){ volatile uint8_t* v = (volatile uint8_t*)p; while(n--) *v++ = 0; }

// =====================================================================
//  Texto
// =====================================================================
// Longitud de la secuencia UTF-8 que empieza en s (0 = no es valida: sin
// formas largas, sin sustitutos y nada por encima de U+10FFFF).
static size_t utf8Seq(const unsigned char* s, size_t avail){
  unsigned char c = s[0];
  size_t n; uint32_t cp;
  if(c < 0x80) return 1;
  if((c & 0xE0) == 0xC0){ n = 2; cp = c & 0x1F; }
  else if((c & 0xF0) == 0xE0){ n = 3; cp = c & 0x0F; }
  else if((c & 0xF8) == 0xF0){ n = 4; cp = c & 0x07; }
  else return 0;
  if(n > avail) return 0;
  for(size_t i = 1; i < n; i++){
    if((s[i] & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (s[i] & 0x3F);
  }
  if((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
     (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return n;
}

void fstCleanText(char* out, size_t cap, const char* in){
  if(!out || !cap) return;
  out[0] = 0;
  if(!in) return;
  const unsigned char* s = (const unsigned char*)in;
  size_t len = strlen(in), i = 0, o = 0;
  while(i < len){
    size_t k = utf8Seq(s + i, len - i);
    if(k == 0){                                        // secuencia rota: UN '?' en su lugar
      if(o + 1 >= cap) break;
      out[o++] = '?'; i++;
      while(i < len && (s[i] & 0xC0) == 0x80) i++;
      continue;
    }
    if(o + k >= cap) break;                            // sin partir un caracter
    if(k == 1) out[o++] = (s[i] < 0x20 || s[i] == 0x7f) ? ' ' : (char)s[i];
    else { memcpy(out + o, s + i, k); o += k; }
    i += k;
  }
  out[o] = 0;
  // Sin espacios a los lados.
  size_t a = 0; while(out[a] == ' ') a++;
  if(a) memmove(out, out + a, strlen(out + a) + 1);
  size_t L = strlen(out); while(L && out[L - 1] == ' ') out[--L] = 0;
}

static bool idOk(const char* s){
  if(!s) return false;
  size_t n = strlen(s);
  if(n == 0 || n >= FST_ID_MAX) return false;
  for(size_t i = 0; i < n; i++){
    char c = s[i];
    if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
  }
  return true;
}

// Puerto en decimal estricto: solo cifras (nada de espacios ni signos), 1..65535.
static bool portOk(const char* s, uint16_t* out){
  if(!s) return false;
  size_t n = strlen(s);
  if(n == 0 || n > 5) return false;
  unsigned long v = 0;
  for(size_t i = 0; i < n; i++){
    if(s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (unsigned long)(s[i] - '0');
  }
  if(v == 0 || v > 65535) return false;
  *out = (uint16_t)v;
  return true;
}

static bool hexLower(const char* s, size_t n){
  if(!s || strlen(s) != n) return false;
  for(size_t i = 0; i < n; i++) if(!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}

void fstHex(const uint8_t* b, size_t n, char* out){
  static const char H[] = "0123456789abcdef";
  for(size_t i = 0; i < n; i++){ out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
  out[2 * n] = 0;
}

bool fstUnhex(const char* s, uint8_t* out, size_t n){
  if(!s || strlen(s) != 2 * n) return false;
  for(size_t i = 0; i < n; i++){
    int v = 0;
    for(int k = 0; k < 2; k++){
      char c = s[2 * i + k];
      int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
      if(d < 0) return false;
      v = v * 16 + d;
    }
    out[i] = (uint8_t)v;
  }
  return true;
}

bool fstLocalIpv4(const char* ip){
  if(!ip) return false;
  unsigned o[4]; char tail;
  if(sscanf(ip, "%u.%u.%u.%u%c", &o[0], &o[1], &o[2], &o[3], &tail) != 4) return false;
  for(int i = 0; i < 4; i++) if(o[i] > 255) return false;
  // Sin ceros a la izquierda ni signos raros: se vuelve a escribir y se compara.
  char back[FST_IP_MAX];
  snprintf(back, sizeof(back), "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
  if(strcmp(back, ip)) return false;
  if(o[0] == 10 || o[0] == 127) return true;
  if(o[0] == 172 && o[1] >= 16 && o[1] <= 31) return true;
  if(o[0] == 192 && o[1] == 168) return true;
  if(o[0] == 169 && o[1] == 254) return true;
  if(o[0] == 100 && o[1] >= 64 && o[1] <= 127) return true;
  return false;
}

// =====================================================================
//  Registro del telefono (NVS)
// =====================================================================
static uint32_t crc32(const uint8_t* p, size_t n){
  uint32_t c = 0xFFFFFFFFu;
  for(size_t i = 0; i < n; i++){
    c ^= p[i];
    for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
  }
  return ~c;
}

// "FST1" | ver(1) | enabled(1) | len+id | len+name | len+model | key(32) | len+ip | port(2) | epoch(4) | crc(4)
size_t fstPhoneEncode(const FstPhone* p, uint8_t* buf, size_t cap){
  if(!p || !p->valid || !buf) return 0;
  size_t n = 0;
  auto put = [&](const void* d, size_t k) -> bool { if(n + k > cap) return false; memcpy(buf + n, d, k); n += k; return true; };
  auto putStr = [&](const char* s, size_t max) -> bool {
    size_t L = strnlen(s, max - 1);
    uint8_t l = (uint8_t)L;
    return put(&l, 1) && put(s, L);
  };
  const uint8_t ver = 1, en = p->enabled ? 1 : 0;
  uint8_t pb[2] = { (uint8_t)(p->port & 0xFF), (uint8_t)(p->port >> 8) };
  uint8_t eb[4] = { (uint8_t)p->pairedEpoch, (uint8_t)(p->pairedEpoch >> 8), (uint8_t)(p->pairedEpoch >> 16), (uint8_t)(p->pairedEpoch >> 24) };
  if(!(put("FST1", 4) && put(&ver, 1) && put(&en, 1) && putStr(p->id, sizeof(p->id)) && putStr(p->name, sizeof(p->name)) &&
       putStr(p->model, sizeof(p->model)) && put(p->key, FST_KEY_SIZE) && putStr(p->ip, sizeof(p->ip)) && put(pb, 2) && put(eb, 4))) return 0;
  uint32_t c = crc32(buf, n);
  uint8_t cb[4] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), (uint8_t)(c >> 24) };
  if(!put(cb, 4)) return 0;
  return n;
}

bool fstPhoneDecode(FstPhone* p, const uint8_t* buf, size_t len){
  if(!p) return false;
  memset(p, 0, sizeof(*p));
  if(!buf || len < 4 + 2 + 3 + FST_KEY_SIZE + 1 + 2 + 4 + 4 || len > FST_PHONE_BLOB_MAX) return false;
  uint32_t want = (uint32_t)buf[len - 4] | ((uint32_t)buf[len - 3] << 8) | ((uint32_t)buf[len - 2] << 16) | ((uint32_t)buf[len - 1] << 24);
  if(crc32(buf, len - 4) != want || memcmp(buf, "FST1", 4) || buf[4] != 1) return false;
  size_t n = 6, end = len - 4;
  auto getStr = [&](char* out, size_t cap) -> bool {
    if(n >= end) return false;
    size_t L = buf[n++];
    if(L >= cap || n + L > end) return false;
    memcpy(out, buf + n, L); out[L] = 0; n += L;
    return true;
  };
  FstPhone t; memset(&t, 0, sizeof(t));
  t.enabled = buf[5] ? 1 : 0;
  if(!getStr(t.id, sizeof(t.id)) || !getStr(t.name, sizeof(t.name)) || !getStr(t.model, sizeof(t.model))) return false;
  if(n + FST_KEY_SIZE > end) return false;
  memcpy(t.key, buf + n, FST_KEY_SIZE); n += FST_KEY_SIZE;
  if(!getStr(t.ip, sizeof(t.ip)) || n + 6 != end) return false;
  t.port = (uint16_t)(buf[n] | (buf[n + 1] << 8));
  t.pairedEpoch = (uint32_t)buf[n + 2] | ((uint32_t)buf[n + 3] << 8) | ((uint32_t)buf[n + 4] << 16) | ((uint32_t)buf[n + 5] << 24);
  if(!idOk(t.id) || !fstLocalIpv4(t.ip) || t.port == 0){ wipe(&t, sizeof(t)); return false; }
  t.valid = 1;
  *p = t;
  wipe(&t, sizeof(t));
  return true;
}

bool fstPhoneBaseUrl(const FstPhone* p, char* out, size_t cap){
  if(!p || !p->valid || !out || !cap || !fstLocalIpv4(p->ip) || !p->port) return false;
  int r = snprintf(out, cap, "http://%s:%u/api/cloud", p->ip, (unsigned)p->port);
  return r > 0 && (size_t)r < cap;
}

void fstPhoneWipe(FstPhone* p){ if(p) wipe(p, sizeof(*p)); }

// =====================================================================
//  HMAC con campos con su longitud delante (gemelo de StorageCrypto.kt)
// =====================================================================
// Cada mensaje es una etiqueta fija (< 32 bytes) y como mucho tres campos de
// <= 255 bytes: 32 + 3 * 256 = 800 bytes, asi que nunca se desborda. Las
// entradas reales son mucho mas cortas (todas se validan antes de llegar aqui).
// Un campo de mas de 255 bytes no existe en el protocolo (StorageCrypto.lp
// lanza): si llegara, se recorta. El HMAC sale distinto del del telefono
// (falla cerrado) y, como va con la clave, nadie puede adivinarlo: nunca hay
// un valor fijo (p. ej. ceros) que sirva de prueba.
struct Msg {
  uint8_t b[800];
  size_t  n;
};
static void mTag(Msg& m, const char* tag){
  size_t L = strlen(tag);
  if(L > 32) L = 32;
  memcpy(m.b + m.n, tag, L); m.n += L;
}
static void mLp(Msg& m, const char* s){
  size_t L = s ? strlen(s) : 0;
  if(L > 255) L = 255;
  if(m.n + 1 + L > sizeof(m.b)) L = sizeof(m.b) - m.n - 1;      // no pasa: 3 campos como mucho
  m.b[m.n++] = (uint8_t)L;
  if(L) memcpy(m.b + m.n, s, L);
  m.n += L;
}
static void mac(const uint8_t* key, size_t keyN, Msg& m, uint8_t out[32]){
  flexHmacSha256(key, keyN, m.b, m.n, out);
  wipe(m.b, m.n);
}

void fstDeriveKey(const uint8_t z[32], const char* offer, const char* p4Id, const char* phoneId, uint8_t out[FST_KEY_SIZE]){
  Msg m; m.n = 0;
  mTag(m, "flexstorage-v1-key"); mLp(m, offer); mLp(m, p4Id); mLp(m, phoneId);
  mac(z, 32, m, out);
}
void fstSas(const uint8_t k[FST_KEY_SIZE], char out[7]){
  Msg m; m.n = 0;
  mTag(m, "flexstorage-v1-sas");
  uint8_t h[32]; mac(k, FST_KEY_SIZE, m, h);
  uint32_t v = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
  snprintf(out, 7, "%06lu", (unsigned long)(v % 1000000u));
  wipe(h, sizeof(h));
}
static void tagged1(const uint8_t k[FST_KEY_SIZE], const char* tag, const char* a, uint8_t out[32]){
  Msg m; m.n = 0; mTag(m, tag); mLp(m, a); mac(k, FST_KEY_SIZE, m, out);
}
static void tagged2(const uint8_t k[FST_KEY_SIZE], const char* tag, const char* a, const char* b, uint8_t out[32]){
  Msg m; m.n = 0; mTag(m, tag); mLp(m, a); mLp(m, b); mac(k, FST_KEY_SIZE, m, out);
}
void fstPhoneProof(const uint8_t k[FST_KEY_SIZE], const char* pairId, uint8_t out[32]){ tagged1(k, "flexstorage-v1-phone-ok", pairId, out); }
void fstP4Proof(const uint8_t k[FST_KEY_SIZE], const char* pairId, uint8_t out[32]){ tagged1(k, "flexstorage-v1-p4-ok", pairId, out); }
void fstKnownProof(const uint8_t k[FST_KEY_SIZE], const char* offer, uint8_t out[32]){ tagged1(k, "flexstorage-v1-known", offer, out); }
void fstSessionMac(const uint8_t k[FST_KEY_SIZE], const char* nonce, const char* p4Id, uint8_t out[32]){ tagged2(k, "flexstorage-v1-sess", nonce, p4Id, out); }
void fstSessionOk(const uint8_t k[FST_KEY_SIZE], const char* nonce, const char* token, uint8_t out[32]){ tagged2(k, "flexstorage-v1-sess-ok", nonce, token, out); }

// =====================================================================
//  ECDH P-256
// =====================================================================
#if FST_ECDH_MBEDTLS
struct RngCtx { FstRandFn fn; void* ctx; const uint8_t* first; size_t firstLeft; uint32_t ctr; };
static int mbedRng(void* p, unsigned char* out, size_t n){
  RngCtx* r = (RngCtx*)p;
  size_t i = 0;
  // Para fstEcdhFromPrivate: los primeros bytes son la clave privada pedida;
  // lo que se pida despues (el cegado de la multiplicacion) es relleno.
  while(i < n && r->firstLeft){ out[i++] = *r->first++; r->firstLeft--; }
  if(i < n){
    if(r->fn) r->fn(r->ctx, out + i, n - i);
    else for(; i < n; i++){ r->ctr = r->ctr * 1103515245u + 12345u; out[i] = (unsigned char)(r->ctr >> 16); }
  }
  return 0;
}
const char* fstEcdhBackend(){ return "mbedtls"; }

static bool mbedGen(FstEcdh* e, RngCtx* rc){
  mbedtls_ecp_group grp; mbedtls_mpi d; mbedtls_ecp_point q;
  mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&d); mbedtls_ecp_point_init(&q);
  size_t olen = 0;
  bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
            mbedtls_ecdh_gen_public(&grp, &d, &q, mbedRng, rc) == 0 &&
            mbedtls_mpi_write_binary(&d, e->priv, 32) == 0 &&
            mbedtls_ecp_point_write_binary(&grp, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, e->pub, FST_POINT_SIZE) == 0 &&
            olen == FST_POINT_SIZE;
  mbedtls_ecp_point_free(&q); mbedtls_mpi_free(&d); mbedtls_ecp_group_free(&grp);
  if(!ok) fstEcdhWipe(e);
  return ok;
}
bool fstEcdhGenerate(FstEcdh* e, FstRandFn rnd, void* rctx){
  if(!e || !rnd) return false;
  RngCtx rc = { rnd, rctx, NULL, 0, 0 };
  return mbedGen(e, &rc);
}
bool fstEcdhFromPrivate(FstEcdh* e, const uint8_t priv[32]){
  if(!e || !priv) return false;
  RngCtx rc = { NULL, NULL, priv, 32, 0x2545F491u };
  return mbedGen(e, &rc) && !memcmp(e->priv, priv, 32);
}
bool fstEcdhShared(const FstEcdh* e, const uint8_t peer[FST_POINT_SIZE], uint8_t z[32], FstRandFn rnd, void* rctx){
  if(!e || !peer || !z || peer[0] != 0x04) return false;
  mbedtls_ecp_group grp; mbedtls_mpi d, s; mbedtls_ecp_point p;
  mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&d); mbedtls_mpi_init(&s); mbedtls_ecp_point_init(&p);
  RngCtx rc = { rnd, rctx, NULL, 0, 0x9E3779B9u };
  bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
            mbedtls_ecp_point_read_binary(&grp, &p, peer, FST_POINT_SIZE) == 0 &&
            mbedtls_ecp_check_pubkey(&grp, &p) == 0 &&                // en la curva y no el infinito
            mbedtls_mpi_read_binary(&d, e->priv, 32) == 0 &&
            mbedtls_ecdh_compute_shared(&grp, &s, &p, &d, mbedRng, &rc) == 0 &&
            mbedtls_mpi_write_binary(&s, z, 32) == 0;
  mbedtls_ecp_point_free(&p); mbedtls_mpi_free(&s); mbedtls_mpi_free(&d); mbedtls_ecp_group_free(&grp);
  if(!ok) wipe(z, 32);
  return ok;
}
#elif FST_ECDH_OPENSSL
const char* fstEcdhBackend(){ return "openssl"; }

static bool sslExport(EC_KEY* k, FstEcdh* e){
  const BIGNUM* d = EC_KEY_get0_private_key(k);
  const EC_POINT* q = EC_KEY_get0_public_key(k);
  const EC_GROUP* g = EC_KEY_get0_group(k);
  return d && q && g && BN_bn2binpad(d, e->priv, 32) == 32 &&
         EC_POINT_point2oct(g, q, POINT_CONVERSION_UNCOMPRESSED, e->pub, FST_POINT_SIZE, NULL) == FST_POINT_SIZE;
}
bool fstEcdhGenerate(FstEcdh* e, FstRandFn rnd, void* rctx){
  (void)rnd; (void)rctx;                         // en el PC, el azar es el de OpenSSL
  if(!e) return false;
  EC_KEY* k = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  bool ok = k && EC_KEY_generate_key(k) == 1 && sslExport(k, e);
  EC_KEY_free(k);
  if(!ok) fstEcdhWipe(e);
  return ok;
}
bool fstEcdhFromPrivate(FstEcdh* e, const uint8_t priv[32]){
  if(!e || !priv) return false;
  EC_KEY* k = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  BIGNUM* d = BN_bin2bn(priv, 32, NULL);
  EC_POINT* q = k ? EC_POINT_new(EC_KEY_get0_group(k)) : NULL;
  bool ok = k && d && q && EC_POINT_mul(EC_KEY_get0_group(k), q, d, NULL, NULL, NULL) == 1 &&
            EC_KEY_set_private_key(k, d) == 1 && EC_KEY_set_public_key(k, q) == 1 && sslExport(k, e);
  EC_POINT_free(q); BN_clear_free(d); EC_KEY_free(k);
  if(!ok) fstEcdhWipe(e);
  return ok;
}
bool fstEcdhShared(const FstEcdh* e, const uint8_t peer[FST_POINT_SIZE], uint8_t z[32], FstRandFn rnd, void* rctx){
  (void)rnd; (void)rctx;
  if(!e || !peer || !z || peer[0] != 0x04) return false;
  EC_KEY* k = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  const EC_GROUP* g = k ? EC_KEY_get0_group(k) : NULL;
  BIGNUM* d = BN_bin2bn(e->priv, 32, NULL);
  EC_POINT* p = g ? EC_POINT_new(g) : NULL;
  // oct2point rechaza un punto que no esta en la curva; check_key, el resto.
  bool ok = k && d && p && EC_KEY_set_private_key(k, d) == 1 &&
            EC_POINT_oct2point(g, p, peer, FST_POINT_SIZE, NULL) == 1 && EC_POINT_is_on_curve(g, p, NULL) == 1 &&
            !EC_POINT_is_at_infinity(g, p) && ECDH_compute_key(z, 32, p, k, NULL) == 32;
  EC_POINT_free(p); BN_clear_free(d); EC_KEY_free(k);
  if(!ok) wipe(z, 32);
  return ok;
}
#endif

void fstEcdhWipe(FstEcdh* e){ if(e) wipe(e, sizeof(*e)); }

// =====================================================================
//  Ofertas y emparejamiento
// =====================================================================
void fstInit(FstCore* c, const char* p4Id, const char* p4Name){
  if(!c) return;
  wipe(c, sizeof(*c));
  if(idOk(p4Id)) snprintf(c->p4Id, sizeof(c->p4Id), "%s", p4Id);
  else snprintf(c->p4Id, sizeof(c->p4Id), "flexos-p4");
  fstCleanText(c->p4Name, sizeof(c->p4Name), p4Name && p4Name[0] ? p4Name : "Flex OS Ultra");
}

static bool expired(uint32_t nowMs, uint32_t expMs){ return (int32_t)(nowMs - expMs) >= 0; }

bool fstOfferNew(FstCore* c, uint32_t nowMs, FstRandFn rnd, void* rctx, char out[FST_HEX32]){
  if(!c || !rnd || !out) return false;
  FstOffer* slot = NULL;
  for(int i = 0; i < FST_OFFERS && !slot; i++) if(!c->offers[i].live || expired(nowMs, c->offers[i].expMs)) slot = &c->offers[i];
  if(!slot){                                           // la mas vieja cede su sitio
    slot = &c->offers[0];
    for(int i = 1; i < FST_OFFERS; i++) if((int32_t)(c->offers[i].expMs - slot->expMs) < 0) slot = &c->offers[i];
  }
  uint8_t r[16];
  rnd(rctx, r, sizeof(r));
  fstHex(r, sizeof(r), slot->offer);
  wipe(r, sizeof(r));
  slot->expMs = nowMs + FST_OFFER_TTL_MS;
  slot->live = 1;
  memcpy(out, slot->offer, FST_HEX32);
  return true;
}

static bool eqCT(const char* a, const char* b, size_t n){
  uint8_t d = 0;
  for(size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

static void limFail(FstCore* c, uint32_t nowMs){
  if(c->fails < 250) c->fails++;
  if(c->fails >= FST_TRIES){
    int k = c->fails - FST_TRIES; if(k > 5) k = 5;
    c->blockUntilMs = nowMs + (30000u << k);           // 30 s, 1 min, 2 min... 16 min
    if(!c->blockUntilMs) c->blockUntilMs = 1;
  }
}
static uint32_t limWait(FstCore* c, uint32_t nowMs){
  if(!c->blockUntilMs) return 0;
  int32_t d = (int32_t)(c->blockUntilMs - nowMs);
  if(d <= 0){ c->blockUntilMs = 0; return 0; }
  return (uint32_t)d;
}

void fstPairCancel(FstCore* c){ if(c) wipe(&c->pair, sizeof(c->pair)); }

static void say(char* err, size_t cap, const char* msg){ if(err && cap) snprintf(err, cap, "%s", msg); }

int fstPairBegin(FstCore* c, uint32_t nowMs, const FstPairReq* q, const char* peerIp,
                 FstRandFn rnd, void* rctx, FstPairResp* resp, char* err, size_t errCap, uint32_t* waitS){
  if(waitS) *waitS = 0;
  if(!c || !q || !resp || !rnd){ say(err, errCap, "Petici\xC3\xB3n no v\xC3\xA1lida"); return 400; }
  memset(resp, 0, sizeof(*resp));
  uint8_t peer[FST_POINT_SIZE];
  uint16_t port = 0;
  if(!hexLower(q->offer, 32) || !idOk(q->pid) || !portOk(q->port, &port) ||
     !fstUnhex(q->pub, peer, FST_POINT_SIZE) || peer[0] != 0x04){
    say(err, errCap, "Petici\xC3\xB3n de emparejamiento incompleta");
    return 400;
  }
  if(!fstLocalIpv4(peerIp)){
    say(err, errCap, "El tel\xC3\xA9" "fono no est\xC3\xA1 en la red local");
    return 403;
  }
  // La oferta: viva, sin caducar y de UN solo uso.
  FstOffer* hit = NULL;
  for(int i = 0; i < FST_OFFERS; i++){
    FstOffer* o = &c->offers[i];
    if(o->live && !expired(nowMs, o->expMs) && eqCT(o->offer, q->offer, 32)) hit = o;
  }
  if(!hit){
    // El limitador solo frena a quien prueba ofertas: una oferta buena (de una
    // sesion web autenticada) pasa siempre, asi que nadie en la Wi-Fi puede
    // bloquear el emparejamiento del telefono de verdad mandando basura.
    uint32_t w = limWait(c, nowMs);
    if(w){
      if(waitS) *waitS = (w + 999) / 1000;
      say(err, errCap, "Demasiados intentos. Espera antes de volver a probar");
      return 429;
    }
    limFail(c, nowMs);
    say(err, errCap, "Este enlace ya no vale: vuelve a pulsar \xC2\xAB" "Activar Flex Cloud\xC2\xBB en la web");
    return 403;
  }
  char offer[FST_HEX32]; memcpy(offer, hit->offer, FST_HEX32);
  wipe(hit, sizeof(*hit));                             // gastada
  // Un emparejamiento anterior a medias se cancela: uno a la vez.
  fstPairCancel(c);
  FstEcdh mine; uint8_t z[32];
  if(!fstEcdhGenerate(&mine, rnd, rctx)){ say(err, errCap, "No se pudo generar la clave (memoria)"); return 500; }
  if(!fstEcdhShared(&mine, peer, z, rnd, rctx)){
    fstEcdhWipe(&mine);
    say(err, errCap, "La clave del tel\xC3\xA9" "fono no es v\xC3\xA1lida");
    return 400;
  }
  FstPairing* p = &c->pair;
  fstDeriveKey(z, offer, c->p4Id, q->pid, p->key);
  wipe(z, sizeof(z));
  fstHex(mine.pub, FST_POINT_SIZE, resp->pub);
  fstEcdhWipe(&mine);
  // ¿Un telefono que ya estaba emparejado y demuestra que conserva su clave?
  bool known = false;
  uint8_t kp[32];
  if(q->kp4 && q->known && c->phone.valid && !strcmp(q->kp4, c->p4Id) && !strcmp(q->pid, c->phone.id) && fstUnhex(q->known, kp, 32)){
    uint8_t want[32];
    fstKnownProof(c->phone.key, offer, want);
    known = flexAuthEqual(kp, want, 32);
    wipe(want, sizeof(want));
  }
  wipe(kp, sizeof(kp));
  uint8_t r[16]; rnd(rctx, r, sizeof(r)); fstHex(r, sizeof(r), p->pairId); wipe(r, sizeof(r));
  snprintf(p->phoneId, sizeof(p->phoneId), "%s", q->pid);
  fstCleanText(p->name, sizeof(p->name), q->name && q->name[0] ? q->name : "Tel\xC3\xA9" "fono");
  if(!p->name[0]) snprintf(p->name, sizeof(p->name), "Tel\xC3\xA9" "fono");
  fstCleanText(p->model, sizeof(p->model), q->model ? q->model : "");
  snprintf(p->ip, sizeof(p->ip), "%s", peerIp);
  p->port = port;
  fstSas(p->key, p->sas);
  p->needsApproval = known ? 0 : 1;
  p->state = known ? FSTP_APPROVED : FSTP_PENDING;
  p->fails = 0;
  p->startMs = nowMs;
  memcpy(resp->pairId, p->pairId, FST_HEX32);
  resp->approve = p->needsApproval;
  resp->expiresS = FST_PAIR_TTL_MS / 1000u;
  c->fails = 0; c->blockUntilMs = 0;                   // un intento valido limpia el limitador
  return 202;
}

int fstPairPoll(FstCore* c, uint32_t nowMs, const char* pairId, const char* proofHex,
                uint8_t* state, char proofOut[65], bool* persist, char* err, size_t errCap){
  if(persist) *persist = false;
  if(state) *state = FSTP_NONE;
  if(proofOut) proofOut[0] = 0;
  if(!c){ say(err, errCap, "No existe"); return 404; }
  FstPairing* p = &c->pair;
  if(p->state == FSTP_NONE || !pairId || strlen(pairId) != 32 || !eqCT(p->pairId, pairId, 32)){
    say(err, errCap, "Esta solicitud ya no existe en Flex OS");
    return 404;
  }
  if(nowMs - p->startMs > FST_PAIR_TTL_MS){
    fstPairCancel(c);
    say(err, errCap, "Se acab\xC3\xB3 el tiempo para aprobarlo en Flex OS");
    return 410;
  }
  uint8_t got[32], want[32];
  bool ok = fstUnhex(proofHex, got, 32);
  fstPhoneProof(p->key, p->pairId, want);
  ok = ok && flexAuthEqual(got, want, 32);
  wipe(want, sizeof(want));
  if(!ok){
    // Acotado por emparejamiento: a la tercera, se cancela (la clave de 256
    // bits no se adivina; esto solo corta a quien insiste).
    if(++p->fails >= FST_PROOF_FAILS) fstPairCancel(c);
    say(err, errCap, "La prueba del tel\xC3\xA9" "fono no es correcta");
    return 403;
  }
  if(p->state == FSTP_PENDING){ if(state) *state = FSTP_PENDING; return 200; }
  if(p->state == FSTP_DENIED){
    fstPairCancel(c);
    say(err, errCap, "Rechazado en la pantalla de Flex OS");
    return 403;
  }
  if(p->state == FSTP_APPROVED){
    // Aprobado (en pantalla o por ser un telefono conocido): queda emparejado.
    FstPhone* ph = &c->phone;
    fstPhoneWipe(ph);
    ph->valid = 1; ph->enabled = 1;
    snprintf(ph->id, sizeof(ph->id), "%s", p->phoneId);
    snprintf(ph->name, sizeof(ph->name), "%s", p->name);
    snprintf(ph->model, sizeof(ph->model), "%s", p->model);
    memcpy(ph->key, p->key, FST_KEY_SIZE);
    snprintf(ph->ip, sizeof(ph->ip), "%s", p->ip);
    ph->port = p->port;
    p->state = FSTP_DONE;
    if(persist) *persist = true;
  }
  // DONE: se repite la respuesta (la anterior pudo perderse por la red).
  uint8_t pr[32]; fstP4Proof(p->key, p->pairId, pr);
  if(proofOut) fstHex(pr, 32, proofOut);
  wipe(pr, sizeof(pr));
  if(state) *state = FSTP_DONE;
  return 200;
}

bool fstPairDecide(FstCore* c, bool allow){
  if(!c || c->pair.state != FSTP_PENDING) return false;
  c->pair.state = allow ? FSTP_APPROVED : FSTP_DENIED;
  return true;
}

bool fstPairWaiting(FstCore* c, uint32_t nowMs){
  if(!c || c->pair.state != FSTP_PENDING) return false;
  if(nowMs - c->pair.startMs > FST_PAIR_TTL_MS){ fstPairCancel(c); return false; }
  return true;
}

// =====================================================================
//  Sesion P4 -> telefono
// =====================================================================
static const char* jstr(const cJSON* o, const char* k){
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

bool fstParseChallenge(const char* body, size_t len, char nonce[FST_HEX32]){
  if(!body || !nonce) return false;
  nonce[0] = 0;
  cJSON* r = cJSON_ParseWithLength(body, len);
  const char* n = r ? jstr(r, "nonce") : NULL;
  bool ok = n && hexLower(n, 32);
  if(ok) memcpy(nonce, n, FST_HEX32);
  cJSON_Delete(r);
  return ok;
}

size_t fstSessionBody(const FstCore* c, const char* nonce, char* out, size_t cap){
  if(!c || !c->phone.valid || !hexLower(nonce, 32) || !out) return 0;
  uint8_t m[32]; char mh[65];
  fstSessionMac(c->phone.key, nonce, c->p4Id, m);
  fstHex(m, 32, mh);
  wipe(m, sizeof(m));
  int r = snprintf(out, cap, "{\"p4Id\":\"%s\",\"nonce\":\"%s\",\"mac\":\"%s\"}", c->p4Id, nonce, mh);
  return r > 0 && (size_t)r < cap ? (size_t)r : 0;
}

bool fstParseSession(const char* body, size_t len, const uint8_t key[FST_KEY_SIZE], const char* nonce,
                     char token[FST_TOKEN_MAX], uint32_t* expiresS, char* name, size_t nameCap){
  if(!body || !key || !token) return false;
  token[0] = 0;
  cJSON* r = cJSON_ParseWithLength(body, len);
  const char* tk = r ? jstr(r, "token") : NULL;
  const char* mh = r ? jstr(r, "mac") : NULL;
  uint8_t got[32], want[32];
  bool ok = tk && hexLower(tk, 48) && hexLower(nonce, 32) && fstUnhex(mh, got, 32);
  if(ok){
    // El telefono demuestra que TAMBIEN tiene la clave: sin esto, otro equipo
    // con su IP podria quedarse con lo que el P4 sube.
    fstSessionOk(key, nonce, tk, want);
    ok = flexAuthEqual(got, want, 32);
    wipe(want, sizeof(want));
  }
  if(ok){
    memcpy(token, tk, 49);
    const cJSON* e = cJSON_GetObjectItemCaseSensitive(r, "expiresIn");
    if(expiresS) *expiresS = cJSON_IsNumber(e) && e->valuedouble > 0 && e->valuedouble < 86400 ? (uint32_t)e->valuedouble : 1800;
    const char* nm = jstr(r, "name");
    if(name && nameCap) fstCleanText(name, nameCap, nm ? nm : "");
  }
  cJSON_Delete(r);
  return ok;
}

#if FST_ECDH_OPENSSL
#  pragma GCC diagnostic pop
#endif
