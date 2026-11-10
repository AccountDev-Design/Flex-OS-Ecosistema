// Flex Account (N3a): el nucleo puro del puerto frente al FlexOS_Account.cpp / FlexOS_CloudTLS.cpp de Arduino compilados tal cual (tests/host/ref/ref_account_main.cpp, contra el
// tests/host/netstub del propio Arduino). Sobres ES256 firmados con una clave efimera (la privada de produccion solo existe en Flex Developer Studio): validos, y rotos de
// todas las formas que importan.
//   tests/host/run.sh
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   // la API EC de OpenSSL 3 que usa tambien el nucleo en el PC
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_account.h"
#include "flex_tls.h"

static int g_fail, g_checks;
#define CHECK(c)                                                                                                                                             \
    do {                                                                                                                                                     \
        g_checks++;                                                                                                                                          \
        if (!(c)) {                                                                                                                                          \
            g_fail++;                                                                                                                                        \
            if (g_fail < 40) printf("account: FALLO %s:%d: %s\n", __FILE__, __LINE__, #c);                                                                   \
        }                                                                                                                                                    \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                                                       \
    do {                                                                                                                                                     \
        long long _a = (long long)(a), _b = (long long)(b);                                                                                                  \
        g_checks++;                                                                                                                                          \
        if (_a != _b) {                                                                                                                                      \
            g_fail++;                                                                                                                                        \
            if (g_fail < 40) printf("account: FALLO %s:%d: %s == %lld, esperado %lld\n", __FILE__, __LINE__, #a, _a, _b);                                    \
        }                                                                                                                                                    \
    } while (0)

// ---- Arduino (ref_account_main.cpp) ----
void ref_acct_set_random(const unsigned char *bytes, int n);
void ref_acct_set_key(const unsigned char *pub65);
int ref_acct_classify(int status, const char *code);
unsigned ref_acct_backoff(unsigned f);
unsigned long ref_acct_nvslen(unsigned long ret, const char *buf, unsigned long cap);
int ref_acct_json_escape(const char *in, char *out, unsigned long cap);
int ref_acct_b64_value(char c);
int ref_acct_b64_decode(const char *text, unsigned char *out, unsigned long cap, unsigned long max_out, unsigned long *out_len);
int ref_acct_valid_address(const char *a);
int ref_acct_verify(const unsigned char *payload, unsigned long len, const unsigned char *sig64);
int ref_acct_signed_payload(const unsigned char *env, unsigned long len, unsigned char *out, unsigned long cap, unsigned long *out_len);
int ref_acct_parse_code(const unsigned char *payload, unsigned long len, const char *hash, char *code, char *url);
int ref_acct_parse_status(const unsigned char *payload, unsigned long len, char *address, char *name);
int ref_acct_make_bearer(char *out, char *hash);
void ref_acct_hex32(const unsigned char *d, char *out);
int ref_acct_sha256(const unsigned char *d, unsigned long n, unsigned char *out);
const char *ref_acct_link_label(int l);
const char *ref_acct_verdict_detail(int v);
int ref_tls_room(unsigned long internal_free, unsigned long block);
const char *ref_cloud_root_ca(void);
const char *ref_tls_reason(int e, char *out, unsigned long cap);
const char *ref_tls_phase(int dns, unsigned el, unsigned hs, unsigned cn, char *out, unsigned long cap);
int ref_url_host(const char *url, char *out, unsigned long cap);

static uint32_t s_seed = 7;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 8;
}
static const char *pick(const char *const *a, int n) { return a[rnd() % (unsigned)n]; }

// ---- clave efimera y firma ----
static EC_KEY *s_key;
static unsigned char s_pub[65];
static void key_setup(void)
{
    s_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(s_key);
    EC_POINT_point2oct(EC_KEY_get0_group(s_key), EC_KEY_get0_public_key(s_key), POINT_CONVERSION_UNCOMPRESSED, s_pub, sizeof(s_pub), NULL);
    flex_account_test_set_key(s_pub);
    ref_acct_set_key(s_pub);
}
// firma r||s sobre SHA-256(payload); `high_s` la deja con S alto (OpenSSL la acepta; mbedTLS del puerto la normaliza)
static void sign(const unsigned char *payload, size_t len, unsigned char out[64], int high_s)
{
    unsigned char dg[32];
    SHA256(payload, len, dg);
    ECDSA_SIG *sig = ECDSA_do_sign(dg, 32, s_key);
    const BIGNUM *r, *s;
    ECDSA_SIG_get0(sig, &r, &s);
    BIGNUM *s2 = BN_dup(s);
    if (high_s) {
        const BIGNUM *order = EC_GROUP_get0_order(EC_KEY_get0_group(s_key));
        BIGNUM *tmp = BN_new();
        BN_sub(tmp, order, s);
        BN_free(s2);
        s2 = tmp;
    }
    memset(out, 0, 64);
    BN_bn2binpad(r, out, 32);
    BN_bn2binpad(s2, out + 32, 32);
    BN_free(s2);
    ECDSA_SIG_free(sig);
}
static void b64url_enc(const unsigned char *d, size_t n, char *out, int pad, int std_alphabet)
{
    static const char U[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    static const char S[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *t = std_alphabet ? S : U;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out[o++] = t[(v >> 18) & 63];
        out[o++] = t[(v >> 12) & 63];
        if (i + 1 < n) out[o++] = t[(v >> 6) & 63]; else if (pad) out[o++] = '=';
        if (i + 2 < n) out[o++] = t[v & 63]; else if (pad) out[o++] = '=';
    }
    out[o] = 0;
}

// ---- generadores de cargas ----
static const char *HASHES[] = {"aaaa", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "", "x"};
static void gen_code_payload(char *out, size_t cap, const char *hash)
{
    static const char *const CODES[] = {"ABCDE", "AB2CD3EF", "ABCD", "abcde", "ABCDEFGHI", "A1B2C3D4", "23456789", "AAAAA", "", "A B C D E"};
    static const char *const URLS[] = {FLEX_ACCOUNT_ACTIVATE_PREFIX "ABCDE", FLEX_ACCOUNT_ACTIVATE_PREFIX, "https://otro.example/activate?code=ABCDE", "", "http://flex-developer-studio.ralvarezsantos980.chatgpt.site/activate?code=ABCDE",
                                       FLEX_ACCOUNT_ACTIVATE_PREFIX "ABCDE&x=1"};
    const char *sch = rnd() % 8 ? "1" : (rnd() % 2 ? "2" : "\"1\"");
    const char *purp = rnd() % 8 ? "flex-device-code" : (rnd() % 2 ? "flex-device-status" : "");
    const char *hh = rnd() % 6 ? hash : "otro";
    const char *code = rnd() % 3 ? CODES[rnd() % 3] : pick(CODES, 10);
    const char *url = rnd() % 3 ? URLS[0] : pick(URLS, 6);
    int missing = (int)(rnd() % 12);
    char b[900];
    int n = snprintf(b, sizeof(b), "{\"schema\":%s,\"purpose\":\"%s\"", sch, purp);
    if (missing != 0) n += snprintf(b + n, sizeof(b) - (size_t)n, ",\"code\":\"%s\"", code);
    if (missing != 1) n += snprintf(b + n, sizeof(b) - (size_t)n, ",\"tokenHash\":\"%s\"", hh);
    if (missing != 2) n += snprintf(b + n, sizeof(b) - (size_t)n, ",\"activationUrl\":\"%s\"", url);
    snprintf(b + n, sizeof(b) - (size_t)n, "}");
    snprintf(out, cap, "%s", b);
}
static void gen_status_payload(char *out, size_t cap)
{
    static const char *const STATES[] = {"pending", "approved", "expired", "other", ""};
    static const char *const ADDR[] = {"ana@flex", "a@flex", "ana@flex.es", "@flex", "abcdef@flex", "x@flexx", "", "ana@FLEX", "maria.lopez@flex"};
    static const char *const NAMES[] = {"Ana", "", "Maria Lopez Hernandez", "\xC3\x91" "and\xC3\xBA"};
    const char *sch = rnd() % 8 ? "1" : "3";
    const char *purp = rnd() % 8 ? "flex-device-status" : "flex-device-code";
    const char *st = rnd() % 3 ? STATES[1] : pick(STATES, 5);
    int missing = (int)(rnd() % 10);
    char b[600];
    int n = snprintf(b, sizeof(b), "{\"schema\":%s,\"purpose\":\"%s\",\"state\":\"%s\"", sch, purp, st);
    if (missing != 0) n += snprintf(b + n, sizeof(b) - (size_t)n, ",\"flexAddress\":\"%s\"", pick(ADDR, 9));
    if (missing != 1) {
        if (rnd() % 6 == 0) n += snprintf(b + n, sizeof(b) - (size_t)n, ",\"displayName\":null");
        else n += snprintf(b + n, sizeof(b) - (size_t)n, ",\"displayName\":\"%s\"", pick(NAMES, 4));
    }
    if (rnd() % 7 == 0) n += snprintf(b + n, sizeof(b) - (size_t)n, ",\"extra\":{\"a\":1}");
    snprintf(b + n, sizeof(b) - (size_t)n, "}");
    snprintf(out, cap, "%s", b);
}

static void t_simple(void)
{
    // clasificar la sesion
    static const int ST[] = {-1000, -11, -1, 0, 100, 199, 200, 201, 204, 299, 300, 301, 400, 401, 402, 403, 404, 408, 429, 500, 502, 503, 599, 600};
    static const char *const CODES[] = {NULL, "", "token_expired", "device_revoked", "auth_required", "invalid_token", "device_unknown", "other", "TOKEN_EXPIRED", "token_expire", "token_expired "};
    for (unsigned i = 0; i < sizeof(ST) / sizeof(ST[0]); i++)
        for (unsigned j = 0; j < sizeof(CODES) / sizeof(CODES[0]); j++) CHECK_EQ(flex_account_classify(ST[i], CODES[j]), ref_acct_classify(ST[i], CODES[j]));
    // espera progresiva
    for (unsigned f = 0; f < 256; f++) CHECK_EQ(flex_account_backoff_ms((uint8_t)f), ref_acct_backoff(f));
    CHECK_EQ(flex_account_backoff_ms(1), 30000);
    CHECK_EQ(flex_account_backoff_ms(2), 60000);
    CHECK_EQ(flex_account_backoff_ms(5), 480000);
    CHECK_EQ(flex_account_backoff_ms(6), 900000);
    // lectura de cadenas de la NVS
    for (int it = 0; it < 6000; it++) {
        char buf[24];
        unsigned long cap = rnd() % 22;
        int n = (int)(rnd() % 20);
        for (int i = 0; i < 24; i++) buf[i] = (char)('a' + rnd() % 26);
        if (rnd() % 5) buf[n < 23 ? n : 23] = 0;
        unsigned long cands[] = {0, (unsigned long)n, (unsigned long)n + 1, (unsigned long)n + 2, rnd() % 30};
        unsigned long ret = cands[rnd() % 5];
        const char *bp = rnd() % 20 ? buf : NULL;
        CHECK_EQ(flex_nvs_str_len(ret, bp, cap), ref_acct_nvslen(ret, bp, cap));
    }
    // escapar JSON
    static const char *const JS[] = {"", "FlexOS Ultra", "a\"b", "a\\b", "l1\nl2\r\n", "tab\tx", "ctl\x01x", "\xC3\xB1" "andu", "\"\"\"\"", "\\\\\\", "x\x1F", "0123456789012345678901234567890123456789"};
    for (unsigned i = 0; i < sizeof(JS) / sizeof(JS[0]); i++) {
        for (unsigned long cap = 0; cap < 48; cap++) {
            char a[64], b[64];
            memset(a, 0x55, sizeof(a));
            memset(b, 0x55, sizeof(b));
            int ra = flex_account_json_escape(JS[i], a, cap), rb = ref_acct_json_escape(JS[i], b, cap);
            CHECK_EQ(ra, rb);
            if (ra && rb) CHECK(!strcmp(a, b));
        }
    }
    CHECK(!flex_account_json_escape(NULL, (char[4]){0}, 4) && !ref_acct_json_escape(NULL, (char[4]){0}, 4));
    for (int c = 0; c < 256; c++) CHECK_EQ(flex_account_b64_value((char)c), ref_acct_b64_value((char)c));
    // direcciones
    static const char *const AD[] = {"", "ana@flex", "a@flex", "@flex", "abcdef@flex", "ana@flexx", "ana@flex.com", "xx@flex@flex", "ana@FLEX", "ana@fle", "12345@flex", "@flex@flex", "ab@flex"};
    for (unsigned i = 0; i < sizeof(AD) / sizeof(AD[0]); i++) CHECK_EQ(flex_account_valid_address(AD[i]), ref_acct_valid_address(AD[i]));
    CHECK(!flex_account_valid_address(NULL) && !ref_acct_valid_address(NULL));
    // textos
    for (int l = 0; l < 8; l++) CHECK(!strcmp(flex_account_link_label((flex_account_link_t)l), ref_acct_link_label(l)));
    for (int v = 0; v < 5; v++) CHECK(!strcmp(flex_account_verdict_detail((flex_session_verdict_t)v), ref_acct_verdict_detail(v)));
}

static void t_b64(void)
{
    for (int it = 0; it < 12000; it++) {
        unsigned char raw[80];
        size_t n = rnd() % 70;
        for (size_t i = 0; i < n; i++) raw[i] = (unsigned char)rnd();
        char text[160];
        b64url_enc(raw, n, text, (int)(rnd() % 2), (int)(rnd() % 3 == 0));
        size_t tl = strlen(text);
        switch (rnd() % 12) {   // roturas
        case 0: if (tl) text[rnd() % tl] = "!@# .\n\x7f"[rnd() % 8]; break;
        case 1: if (tl) text[tl - 1] = '='; break;
        case 2: strcat(text, "="); break;
        case 3: strcat(text, "=="); break;
        case 4: if (tl > 2) text[tl / 2] = '='; break;
        case 5: if (tl) text[tl - 1] = 0; break;
        case 6: if (tl > 3) text[tl - 3] = 0; break;
        case 7: strcat(text, "A"); break;
        default: break;
        }
        unsigned long max_out = rnd() % 5 == 0 ? rnd() % 80 : 4096;
        uint8_t *mine = NULL;
        size_t mine_len = 0;
        unsigned char ref[256];
        unsigned long ref_len = 0;
        int a = flex_account_b64url_decode(text, &mine, &mine_len, max_out);
        int b = ref_acct_b64_decode(text, ref, sizeof(ref), max_out, &ref_len);
        CHECK_EQ(a, b);
        if (a && b) {
            CHECK(mine_len == ref_len && !memcmp(mine, ref, mine_len) && mine[mine_len] == 0);
        }
        free(mine);
    }
    uint8_t *p;
    size_t l;
    CHECK(!flex_account_b64url_decode(NULL, &p, &l, 10));
}

static void t_signed(void)
{
    char hash[65];
    for (int it = 0; it < 9000; it++) {
        const char *h = pick(HASHES, 4);
        snprintf(hash, sizeof(hash), "%s", h);
        char pj[1100];
        if (rnd() % 2) gen_code_payload(pj, sizeof(pj), hash); else gen_status_payload(pj, sizeof(pj));
        if (rnd() % 11 == 0) pj[rnd() % strlen(pj)] = 'Z';   // JSON roto
        size_t pl = strlen(pj);
        unsigned char sig[64];
        sign((const unsigned char *)pj, pl, sig, (int)(rnd() % 2));
        int bad = (int)(rnd() % 14);
        if (bad == 1) sig[rnd() % 64] ^= 1u << (rnd() % 8);
        char pb[1600], sb[200], env[2200];
        unsigned char pj2[1100];
        memcpy(pj2, pj, pl + 1);
        if (bad == 2) pj2[rnd() % pl] ^= 0x20;   // el payload cambia despues de firmar
        b64url_enc(pj2, pl, pb, (int)(rnd() % 2), 0);
        b64url_enc(sig, bad == 3 ? 63 : 64, sb, (int)(rnd() % 2), 0);
        const char *alg = bad == 4 ? "ES384" : "ES256";
        const char *kid = bad == 5 ? "0000" : FLEX_ACCOUNT_KEY_ID;
        const char *sch = bad == 6 ? "1" : "2";
        int n = snprintf(env, sizeof(env), "{\"schema\":%s,\"algorithm\":\"%s\",\"keyId\":\"%s\"", sch, alg, kid);
        if (bad != 7) n += snprintf(env + n, sizeof(env) - (size_t)n, ",\"payload\":\"%s\"", bad == 8 ? "" : pb);
        if (bad != 9) n += snprintf(env + n, sizeof(env) - (size_t)n, ",\"signature\":\"%s\"", sb);
        if (bad == 10) n += snprintf(env + n, sizeof(env) - (size_t)n, ",\"extra\":[1,2,3]");
        snprintf(env + n, sizeof(env) - (size_t)n, "}");
        if (bad == 11) env[strlen(env) - 1] = 0;   // sobre cortado
        size_t el = strlen(env);
        uint8_t *mine = NULL;
        size_t mine_len = 0;
        unsigned char ref[1200];
        unsigned long ref_len = 0;
        int a = flex_account_signed_payload((const uint8_t *)env, el, &mine, &mine_len);
        int b = ref_acct_signed_payload((const unsigned char *)env, el, ref, sizeof(ref), &ref_len);
        CHECK_EQ(a, b);
        if (a && b) {
            CHECK(mine_len == ref_len && !memcmp(mine, ref, mine_len));
            // y las dos cargas se leen igual
            char c1[9] = "", u1[192] = "", c2[9] = "", u2[192] = "";
            int pa = flex_account_parse_code(mine, mine_len, hash, c1, u1), pb2 = ref_acct_parse_code(ref, ref_len, hash, c2, u2);
            CHECK_EQ(pa, pb2);
            if (pa && pb2) CHECK(!strcmp(c1, c2) && !strcmp(u1, u2));
            char ad1[48] = "", nm1[64] = "", ad2[48] = "", nm2[64] = "";
            int sa = flex_account_parse_status(mine, mine_len, ad1, nm1), sb2 = ref_acct_parse_status(ref, ref_len, ad2, nm2);
            CHECK_EQ(sa, sb2);
            if (sa == FLEX_POLL_APPROVED && sb2 == FLEX_POLL_APPROVED) CHECK(!strcmp(ad1, ad2) && !strcmp(nm1, nm2));
        }
        free(mine);
        // firma suelta: lo mismo con la firma ya decodificada
        int va = flex_account_verify_signature((const uint8_t *)pj, pl, sig), vb = ref_acct_verify((const unsigned char *)pj, pl, sig);
        CHECK_EQ(va, vb);
    }
    // cargas sueltas sin sobre: JSON arbitrario
    for (int it = 0; it < 12000; it++) {
        char pj[1100];
        snprintf(hash, sizeof(hash), "%s", pick(HASHES, 4));
        if (rnd() % 2) gen_code_payload(pj, sizeof(pj), hash); else gen_status_payload(pj, sizeof(pj));
        if (rnd() % 10 == 0) pj[rnd() % strlen(pj)] = "{}[]\",:0"[rnd() % 8];
        size_t pl = strlen(pj);
        char c1[9], u1[192], c2[9], u2[192];
        memset(c1, 'x', sizeof(c1)); memset(u1, 'x', sizeof(u1)); memset(c2, 'x', sizeof(c2)); memset(u2, 'x', sizeof(u2));
        int pa = flex_account_parse_code((const uint8_t *)pj, pl, hash, c1, u1), pb2 = ref_acct_parse_code((const unsigned char *)pj, pl, hash, c2, u2);
        CHECK_EQ(pa, pb2);
        if (pa && pb2) CHECK(!strcmp(c1, c2) && !strcmp(u1, u2));
        char ad1[48], nm1[64], ad2[48], nm2[64];
        memset(ad1, 'x', sizeof(ad1)); memset(nm1, 'x', sizeof(nm1)); memset(ad2, 'x', sizeof(ad2)); memset(nm2, 'x', sizeof(nm2));
        int sa = flex_account_parse_status((const uint8_t *)pj, pl, ad1, nm1), sb2 = ref_acct_parse_status((const unsigned char *)pj, pl, ad2, nm2);
        CHECK_EQ(sa, sb2);
        if (sa == FLEX_POLL_APPROVED && sb2 == FLEX_POLL_APPROVED) CHECK(!strcmp(ad1, ad2) && !strcmp(nm1, nm2));
    }
}

static void t_bearer(void)
{
    for (int it = 0; it < 3000; it++) {
        unsigned char r[32];
        for (int i = 0; i < 32; i++) r[i] = (unsigned char)rnd();
        if (it % 50 == 0) memset(r, it % 100 ? 0xFF : 0x00, 32);
        char o1[48], h1[65], o2[48], h2[65];
        memset(o1, 'x', sizeof(o1)); memset(o2, 'x', sizeof(o2));
        ref_acct_set_random(r, 32);
        int a = flex_account_make_bearer(r, o1, h1), b = ref_acct_make_bearer(o2, h2);
        CHECK_EQ(a, b);
        CHECK(a && b && !strcmp(o1, o2) && !strcmp(h1, h2) && strlen(o1) == 43 && strlen(h1) == 64);
        unsigned char d1[32], d2[32];
        char x1[65], x2[65];
        CHECK_EQ(flex_account_sha256(r, 32, d1), ref_acct_sha256(r, 32, d2));
        CHECK(!memcmp(d1, d2, 32));
        flex_account_hex32(d1, x1);
        ref_acct_hex32(d2, x2);
        CHECK(!strcmp(x1, x2));
    }
    CHECK(!flex_account_sha256(NULL, 3, (uint8_t[32]){0}) && !ref_acct_sha256(NULL, 3, (unsigned char[32]){0}));
    // el SHA-256 es el de verdad
    unsigned char dg[32];
    flex_account_sha256((const uint8_t *)"abc", 3, dg);
    char hx[65];
    flex_account_hex32(dg, hx);
    CHECK(!strcmp(hx, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

static void t_tls(void)
{
    // motivos de fallo: cada modulo y cada causa de mbedTLS, y lo que no se conoce
    static const unsigned HI[] = {0, 0x2700, 0x7A00, 0x7F00, 0x2880, 0x3F80, 0x4D80, 0x5280, 0x6800, 0x7280, 0x7780, 0x7680, 0x2E80, 0x7900};
    static const unsigned LO[] = {0, 0x10, 0x50, 0x52, 0x42, 0x44, 0x01, 0x7F, 0x20};
    for (unsigned i = 0; i < sizeof(HI) / sizeof(HI[0]); i++) {
        for (unsigned j = 0; j < sizeof(LO) / sizeof(LO[0]); j++) {
            int err = -(int)(HI[i] | LO[j]);
            char a[64], b[64];
            flex_tls_reason(err, a, sizeof(a));
            ref_tls_reason(err, b, sizeof(b));
            CHECK(!strcmp(a, b));
        }
    }
    for (int err = -300; err <= 300; err++) {
        char a[64], b[64];
        flex_tls_reason(err, a, sizeof(a));
        ref_tls_reason(err, b, sizeof(b));
        CHECK(!strcmp(a, b));
    }
    static const int BIG[] = {-0x7F00, -0x2700, -0x7280, -0x6800, -0x12345, -0x7FFF, -0x8000, 100000, -2147483647};
    for (unsigned i = 0; i < sizeof(BIG) / sizeof(BIG[0]); i++) {
        char a[64], b[64];
        flex_tls_reason(BIG[i], a, sizeof(a));
        ref_tls_reason(BIG[i], b, sizeof(b));
        CHECK(!strcmp(a, b));
        char c[8], d[8];   // y con un buffer corto
        flex_tls_reason(BIG[i], c, sizeof(c));
        ref_tls_reason(BIG[i], d, sizeof(d));
        CHECK(!strcmp(c, d));
    }
    // la fase de un -1 (DNS, TCP o saludo TLS)
    for (int it = 0; it < 6000; it++) {
        int dns = (int)(rnd() % 2);
        unsigned el = rnd() % 20 == 0 ? 2999 + rnd() % 3 : rnd() % 30000;
        unsigned hs = rnd() % 3 ? 12000u : (rnd() % 2 ? 0u : rnd() % 20000);
        unsigned cn = rnd() % 3 ? 15000u : (rnd() % 2 ? 0u : 1000 + rnd() % 2);
        if (rnd() % 7 == 0) el = (hs ? hs : 12000) - 1500 + rnd() % 3;
        if (rnd() % 7 == 0) el = (cn ? cn : 15000) - 1000 + rnd() % 3;
        char a[80], b[80];
        flex_tls_phase(dns != 0, el, hs, cn, a, sizeof(a));
        ref_tls_phase(dns, el, hs, cn, b, sizeof(b));
        CHECK(!strcmp(a, b));
    }
    // el servidor de una URL
    static const char *const U[] = {"https://flex.example/api/x", "flex.example/ruta", "https://h:8443/x", "https://h?q=1", "https://", "", "http://a.b.c.d", "ftp://x/y", "https://muy-largo-" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.example/z", "//x/y", "https://[::1]:443/z"};
    for (unsigned i = 0; i < sizeof(U) / sizeof(U[0]); i++) {
        for (unsigned long cap = 0; cap < 24; cap += 3) {
            char a[64] = "", b[64] = "";
            int ra = flex_url_host(U[i], a, cap), rb = ref_url_host(U[i], b, cap);
            CHECK_EQ(ra, rb);
            if (cap) CHECK(!strcmp(a, b));
        }
    }
    CHECK(!flex_url_host(NULL, (char[8]){0}, 8) && !ref_url_host(NULL, (char[8]){0}, 8));
    // sin buffer o sin capacidad: no se escribe nada y se devuelve ""
    {
        char z[8];
        memset(z, 0x55, sizeof(z));
        CHECK(!strcmp(flex_tls_reason(-5, NULL, 8), "") && !strcmp(ref_tls_reason(-5, NULL, 8), ""));
        CHECK(!strcmp(flex_tls_reason(-5, z, 0), "") && !strcmp(ref_tls_reason(-5, z, 0), "") && (unsigned char)z[0] == 0x55);
        CHECK(!strcmp(flex_tls_phase(true, 5000, 12000, 15000, NULL, 8), "") && !strcmp(ref_tls_phase(1, 5000, 12000, 15000, NULL, 8), ""));
        CHECK(!strcmp(flex_tls_phase(true, 5000, 12000, 15000, z, 0), "") && !strcmp(ref_tls_phase(1, 5000, 12000, 15000, z, 0), "") && (unsigned char)z[0] == 0x55);
        CHECK(!flex_url_host("https://a/b", NULL, 8) && !ref_url_host("https://a/b", NULL, 8));
        CHECK(!flex_url_host("https://a/b", z, 0) && !ref_url_host("https://a/b", z, 0) && (unsigned char)z[0] == 0x55);
    }
    // sitio para TLS: los suelos (40 KB de interna y 16 KB de bloque), uno a uno
    static const unsigned long F[] = {0, 40 * 1024 - 1, 40 * 1024, 40 * 1024 + 1, 200000};
    static const unsigned long B[] = {0, 16 * 1024 - 1, 16 * 1024, 16 * 1024 + 1, 100000};
    for (unsigned i = 0; i < 5; i++) for (unsigned j = 0; j < 5; j++) CHECK_EQ(flex_tls_room_ok(F[i], B[j]), ref_tls_room(F[i], B[j]));
}


// Longitudes EXACTAS en el borde de cada buffer destino (47/48/49 del que copia la direccion, 63/64/65 del nombre, 8/9 del codigo, 191/192 de la URL): una cota mal puesta
// escribe un byte mas alla y AddressSanitizer lo ve porque los destinos miden justo eso
static void t_edges(void)
{
    char hash[65];
    memset(hash, 'a', 64);
    hash[64] = 0;
    for (int len = 40; len <= 60; len++) {   // direccion: relleno + "@flex"
        for (int nlen = 58; nlen <= 70; nlen++) {
            char pj[400], addr[80], name[100];
            memset(addr, 'b', sizeof(addr));
            memcpy(addr + len - 5, "@flex", 6);
            memset(name, 'n', (size_t)nlen);
            name[nlen] = 0;
            snprintf(pj, sizeof(pj), "{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"approved\",\"flexAddress\":\"%.*s\",\"displayName\":\"%s\"}", len, addr, name);
            char a1[48], n1[64], a2[48], n2[64];
            memset(a1, 'x', sizeof(a1)); memset(n1, 'x', sizeof(n1)); memset(a2, 'x', sizeof(a2)); memset(n2, 'x', sizeof(n2));
            int ra = flex_account_parse_status((const uint8_t *)pj, strlen(pj), a1, n1);
            int rb = ref_acct_parse_status((const unsigned char *)pj, strlen(pj), a2, n2);
            CHECK_EQ(ra, rb);
            if (ra == FLEX_POLL_APPROVED && rb == FLEX_POLL_APPROVED) CHECK(!strcmp(a1, a2) && !strcmp(n1, n2));
        }
    }
    for (int clen = 4; clen <= 12; clen++) {
        for (int ulen = 180; ulen <= 200; ulen++) {
            char code[20], url[260], pj[900];
            for (int i = 0; i < clen; i++) code[i] = (char)('A' + i % 8);
            code[clen] = 0;
            size_t pl = strlen(FLEX_ACCOUNT_ACTIVATE_PREFIX);
            memcpy(url, FLEX_ACCOUNT_ACTIVATE_PREFIX, pl);
            for (size_t i = pl; i < (size_t)ulen; i++) url[i] = 'u';
            url[ulen] = 0;
            snprintf(pj, sizeof(pj), "{\"schema\":1,\"purpose\":\"flex-device-code\",\"code\":\"%s\",\"tokenHash\":\"%s\",\"activationUrl\":\"%s\"}", code, hash, url);
            char c1[9], u1[192], c2[9], u2[192];
            memset(c1, 'x', sizeof(c1)); memset(u1, 'x', sizeof(u1)); memset(c2, 'x', sizeof(c2)); memset(u2, 'x', sizeof(u2));
            int ra = flex_account_parse_code((const uint8_t *)pj, strlen(pj), hash, c1, u1);
            int rb = ref_acct_parse_code((const unsigned char *)pj, strlen(pj), hash, c2, u2);
            CHECK_EQ(ra, rb);
            if (ra && rb) CHECK(!strcmp(c1, c2) && !strcmp(u1, u2));
        }
    }
    // flex_nvs_str_len con el buffer del tamano EXACTO en el montón: leer el byte `cap` es un desbordamiento
    for (unsigned long cap = 1; cap < 20; cap++) {
        for (int fill = 0; fill < 3; fill++) {   // 0: sin terminador, 1: terminador al final, 2: terminador antes
            char *buf = (char *)malloc(cap);
            memset(buf, 'k', cap);
            if (fill == 1) buf[cap - 1] = 0;
            if (fill == 2 && cap > 2) buf[cap / 2] = 0;
            for (unsigned long ret = 0; ret <= cap + 2; ret++) {
                CHECK_EQ(flex_nvs_str_len(ret, buf, cap), ref_acct_nvslen(ret, buf, cap));
            }
            free(buf);
        }
    }
}

// Las raices de confianza que embebe la placa (flex_net/certs/cloud_root_bundle.pem) son EXACTAMENTE las de Arduino (FlexOS_CloudTLS.cpp)
static void t_roots(void)
{
    FILE *f = fopen("../../components/flex_net/certs/cloud_root_bundle.pem", "rb");
    CHECK(f != NULL);
    if (!f) return;
    static char buf[40000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    const char *ref = ref_cloud_root_ca();
    CHECK(strlen(ref) == n && !memcmp(ref, buf, n));   // las raices de la placa son las de Arduino
    int certs = 0;
    for (const char *p = buf; (p = strstr(p, "BEGIN CERTIFICATE")); p++) certs++;
    CHECK(certs == 11);
}

int main(void)
{
    key_setup();
    t_roots();
    t_edges();
    t_simple();
    t_b64();
    t_signed();
    t_bearer();
    t_tls();
    if (g_fail) {
        printf("account: %d fallos de %d\n", g_fail, g_checks);
        return 1;
    }
    printf("OK: Flex Account, nucleo (%d comprobaciones: sesion, espera, NVS, base64url, firma ES256 y sobres rotos, cargas, credencial, textos TLS)\n", g_checks);
    return 0;
}
