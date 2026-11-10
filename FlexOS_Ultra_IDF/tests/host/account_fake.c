// Mundo en memoria para Flex Account: ver account_fake.h.
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account_fake.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static atomic_uint g_now = 1000;
static atomic_bool g_wifi = true;
static atomic_int g_session;
static atomic_int g_polls_left = 2;
static atomic_bool g_mem_ok = true;
static atomic_int g_post_fail, g_post_status;
static atomic_int g_cnt[4];
static atomic_uint g_latency = 5;
static pthread_mutex_t g_cfg_mu = PTHREAD_MUTEX_INITIALIZER;   // direccion, nombre, huellas
static char g_address[48] = "ana.p4@flex", g_display[64] = "Ana";
static char g_pending_hash[72], g_registered_hash[72];
static EC_KEY *g_key;
static unsigned char g_pub[65];
static bool (*g_ensure)(void);
static void (*g_delay_hook)(uint32_t);
static pthread_mutex_t g_big = PTHREAD_MUTEX_INITIALIZER;   // el cerrojo "del sistema" que da la placa (xSemaphore)

// ---- NVS en memoria (los accesos reales a flash tambien estan serializados por el sistema) ----
static pthread_mutex_t g_nvs_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    char token[64], address[64], display[80];
    bool has_token, has_address, has_display, linked, has_linked;
    uint8_t authst;
    bool has_authst;
    uint32_t install_id;
    bool has_install;
} g_nvs;
static atomic_uint g_rng = 12345;

static uint32_t o_millis(void) { return atomic_load(&g_now); }
static void o_delay(uint32_t ms)
{
    atomic_fetch_add(&g_now, ms);
    if (g_delay_hook) {
        g_delay_hook(ms);
    }
}
static bool o_wifi(void) { return atomic_load(&g_wifi); }
static void o_random(void *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v = atomic_load(&g_rng);
        unsigned nx;
        do {
            nx = v * 1664525u + 1013904223u;
        } while (!atomic_compare_exchange_weak(&g_rng, &v, nx));
        ((unsigned char *)b)[i] = (unsigned char)(nx >> 24);
    }
}
static uint64_t o_mac(void) { return 0xA1B2C3D4E5F6ull; }
static bool o_nvs_begin(void)
{
    pthread_mutex_lock(&g_nvs_mu);
    return true;
}
static void o_nvs_end(void) { pthread_mutex_unlock(&g_nvs_mu); }
static uint32_t o_get_u32(const char *k, uint32_t d) { return !strcmp(k, "installId") && g_nvs.has_install ? g_nvs.install_id : d; }
static size_t o_put_u32(const char *k, uint32_t v)
{
    if (!strcmp(k, "installId")) {
        g_nvs.install_id = v;
        g_nvs.has_install = true;
    }
    atomic_fetch_add(&g_cnt[AF_NVS_WRITES], 1);
    return 4;
}
static size_t o_get_str(const char *k, char *out, size_t cap)
{
    const char *src = NULL;
    if (!strcmp(k, "token") && g_nvs.has_token) src = g_nvs.token;
    else if (!strcmp(k, "address") && g_nvs.has_address) src = g_nvs.address;
    else if (!strcmp(k, "display") && g_nvs.has_display) src = g_nvs.display;
    if (!src || strlen(src) + 1 > cap) return 0;
    memcpy(out, src, strlen(src) + 1);
    return strlen(src) + 1;
}
static size_t o_put_str(const char *k, const char *v)
{
    if (!strcmp(k, "token")) { snprintf(g_nvs.token, sizeof(g_nvs.token), "%s", v); g_nvs.has_token = true; }
    else if (!strcmp(k, "address")) { snprintf(g_nvs.address, sizeof(g_nvs.address), "%s", v); g_nvs.has_address = true; }
    else if (!strcmp(k, "display")) { snprintf(g_nvs.display, sizeof(g_nvs.display), "%s", v); g_nvs.has_display = true; }
    atomic_fetch_add(&g_cnt[AF_NVS_WRITES], 1);
    return strlen(v);
}
static bool o_get_bool(const char *k, bool d) { return !strcmp(k, "linked") && g_nvs.has_linked ? g_nvs.linked : d; }
static bool o_put_bool(const char *k, bool v)
{
    if (!strcmp(k, "linked")) { g_nvs.linked = v; g_nvs.has_linked = true; }
    atomic_fetch_add(&g_cnt[AF_NVS_WRITES], 1);
    return true;
}
static uint8_t o_get_u8(const char *k, uint8_t d) { return !strcmp(k, "authst") && g_nvs.has_authst ? g_nvs.authst : d; }
static size_t o_put_u8(const char *k, uint8_t v)
{
    if (!strcmp(k, "authst")) { g_nvs.authst = v; g_nvs.has_authst = true; }
    atomic_fetch_add(&g_cnt[AF_NVS_WRITES], 1);
    return 1;
}
static bool o_is_key(const char *k)
{
    return (!strcmp(k, "linked") && g_nvs.has_linked) || (!strcmp(k, "token") && g_nvs.has_token) || (!strcmp(k, "address") && g_nvs.has_address) ||
           (!strcmp(k, "display") && g_nvs.has_display) || (!strcmp(k, "authst") && g_nvs.has_authst);
}
static void o_clear(void)
{
    g_nvs.has_token = g_nvs.has_address = g_nvs.has_display = g_nvs.has_linked = g_nvs.has_authst = false;
    g_nvs.linked = false;
    atomic_fetch_add(&g_cnt[AF_NVS_WRITES], 1);
}
static bool o_tls_room(size_t *f, size_t *b)
{
    bool ok = atomic_load(&g_mem_ok);
    if (f) *f = ok ? 200u << 10 : 24u << 10;
    if (b) *b = ok ? 100u << 10 : 10u << 10;
    return ok;
}
static bool o_dns(const char *h) { (void)h; return true; }

// ---- servidor ----
static void b64url(const unsigned char *d, size_t n, char *out)
{
    static const char U[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out[o++] = U[(v >> 18) & 63];
        out[o++] = U[(v >> 12) & 63];
        if (i + 1 < n) out[o++] = U[(v >> 6) & 63];
        if (i + 2 < n) out[o++] = U[v & 63];
    }
    out[o] = 0;
}
static void hex_sha256(const char *s, char out[72])
{
    unsigned char d[32];
    SHA256((const unsigned char *)s, strlen(s), d);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", d[i]);
}
static void make_envelope(const char *payload, char *out, size_t cap)
{
    unsigned char dg[32], sig64[64] = {0};
    SHA256((const unsigned char *)payload, strlen(payload), dg);
    ECDSA_SIG *sig = ECDSA_do_sign(dg, 32, g_key);
    const BIGNUM *r, *s;
    ECDSA_SIG_get0(sig, &r, &s);
    BN_bn2binpad(r, sig64, 32);
    BN_bn2binpad(s, sig64 + 32, 32);
    ECDSA_SIG_free(sig);
    char p64[1024], s64[128];
    b64url((const unsigned char *)payload, strlen(payload), p64);
    b64url(sig64, 64, s64);
    snprintf(out, cap, "{\"schema\":2,\"algorithm\":\"ES256\",\"keyId\":\"%s\",\"payload\":\"%s\",\"signature\":\"%s\"}", FLEX_ACCOUNT_KEY_ID, p64, s64);
}

static struct {   // la peticion en curso (solo la toca la tarea de fondo)
    char method[8], url[400], auth[80], body[600];
    char reply[4096];
    size_t reply_len, pos;
} R;

static bool o_http_begin(const flex_account_req_t *r)
{
    snprintf(R.method, sizeof(R.method), "%s", r->method);
    snprintf(R.url, sizeof(R.url), "%s", r->url);
    snprintf(R.auth, sizeof(R.auth), "%s", r->auth ? r->auth : "");
    R.body[0] = 0;
    if (r->body) {
        snprintf(R.body, sizeof(R.body), "%.*s", (int)r->body_len, (const char *)r->body);
    }
    R.reply_len = R.pos = 0;
    return true;
}
static void reply_text(const char *t)
{
    R.reply_len = strlen(t);
    memcpy(R.reply, t, R.reply_len);
}
static void reply_into(char *out, size_t cap, const char *t) { snprintf(out, cap, "%s", t); }
static int serve_into(const char *method, const char *url, const char *auth, const char *body, char *out, size_t outcap)
{
    if (!strncmp(url, FLEX_ACCOUNT_CODE_URL, strlen(FLEX_ACCOUNT_CODE_URL))) {
        if (!strcmp(method, "POST")) {
            atomic_fetch_add(&g_cnt[AF_POSTS], 1);
            int fails = atomic_load(&g_post_fail);
            if (fails > 0) {
                atomic_store(&g_post_fail, fails - 1);
                return -1;
            }
            int st = atomic_load(&g_post_status);
            if (st) return st;
            const char *h = strstr(body, "\"tokenHash\":\"");
            if (!h) return 400;
            pthread_mutex_lock(&g_cfg_mu);
            snprintf(g_pending_hash, sizeof(g_pending_hash), "%.64s", h + 13);
            char payload[600], env[2048];
            snprintf(payload, sizeof(payload), "{\"schema\":1,\"purpose\":\"flex-device-code\",\"code\":\"FLX7Q2\",\"tokenHash\":\"%s\",\"activationUrl\":\"%sFLX7Q2\"}", g_pending_hash,
                     FLEX_ACCOUNT_ACTIVATE_PREFIX);
            pthread_mutex_unlock(&g_cfg_mu);
            make_envelope(payload, env, sizeof(env));
            reply_into(out, outcap, env);
            return 201;
        }
        atomic_fetch_add(&g_cnt[AF_POLLS], 1);
        int left = atomic_load(&g_polls_left);
        char payload[400], env[2048];
        if (left > 0) {
            atomic_store(&g_polls_left, left - 1);
            snprintf(payload, sizeof(payload), "{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"pending\"}");
        } else {
            pthread_mutex_lock(&g_cfg_mu);
            snprintf(g_registered_hash, sizeof(g_registered_hash), "%s", g_pending_hash);
            snprintf(payload, sizeof(payload), "{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"approved\",\"flexAddress\":\"%s\",\"displayName\":\"%s\"}", g_address, g_display);
            pthread_mutex_unlock(&g_cfg_mu);
        }
        make_envelope(payload, env, sizeof(env));
        reply_into(out, outcap, env);
        return 200;
    }
    atomic_fetch_add(&g_cnt[AF_SESSIONS], 1);
    switch (atomic_load(&g_session)) {
    case AF_SESS_DOWN: return -1;
    case AF_SESS_REVOKED: reply_into(out, outcap, "{\"ok\":false,\"error\":{\"code\":\"device_revoked\",\"message\":\"x\"}}"); return 401;
    case AF_SESS_EXPIRED: reply_into(out, outcap, "{\"ok\":false,\"error\":{\"code\":\"token_expired\",\"message\":\"x\"}}"); return 401;
    default: break;
    }
    char hash[72];
    hex_sha256(strlen(auth) > 7 ? auth + 7 : "", hash);
    pthread_mutex_lock(&g_cfg_mu);
    bool known = !strcmp(hash, g_registered_hash);
    char okbody[400];
    snprintf(okbody, sizeof(okbody), "{\"ok\":true,\"account\":{\"id\":\"acc_1\",\"flexAddress\":\"%s\",\"displayName\":\"%s\"}}", g_address, g_display);
    pthread_mutex_unlock(&g_cfg_mu);
    if (!known) {
        reply_into(out, outcap, "{\"ok\":false,\"error\":{\"code\":\"device_revoked\",\"message\":\"x\"}}");
        return 401;
    }
    reply_into(out, outcap, okbody);
    return 200;
}

// El servidor sin transporte: contesta una peticion y deja el cuerpo en `reply` (para dobles de otros transportes, como el de esp_http_client del puerto)
int account_fake_serve(const char *method, const char *url, const char *auth, const char *body, char *reply, size_t cap)
{
    char tmp[4096];
    int status = serve_into(method, url, auth ? auth : "", body ? body : "", tmp, sizeof(tmp));
    snprintf(reply, cap, "%s", tmp);
    return status;
}
static int o_http_send(void)
{
    o_delay(atomic_load(&g_latency));   // la red tarda un poco: da pie a que otro hilo se meta en medio
    if (!atomic_load(&g_wifi)) return -1;
    char tmp[4096] = "";
    int status = serve_into(R.method, R.url, R.auth, R.body, tmp, sizeof(tmp));
    if (status > 0) {
        reply_text(tmp);
    }
    return status;
}
static int o_http_tls_error(void) { return -1; }
static int o_http_size(void) { return (int)R.reply_len; }
static bool o_http_has_stream(void) { return true; }
static int o_http_available(void) { return (int)(R.reply_len - R.pos); }
static int o_http_read(uint8_t *b, size_t n)
{
    size_t k = R.reply_len - R.pos < n ? R.reply_len - R.pos : n;
    memcpy(b, R.reply + R.pos, k);
    R.pos += k;
    return (int)k;
}
static bool o_http_connected(void) { return R.pos < R.reply_len; }
static void o_http_end(void) {}
static bool o_ensure_task(void) { return g_ensure ? g_ensure() : true; }
static void o_lock(void) { pthread_mutex_lock(&g_big); }
static void o_unlock(void) { pthread_mutex_unlock(&g_big); }

static const flex_account_ops_t OPS = {
    o_millis, o_delay, o_wifi, o_random, o_mac, o_nvs_begin, o_nvs_end, o_get_u32, o_put_u32, o_get_str, o_put_str, o_get_bool, o_put_bool, o_get_u8, o_put_u8, o_is_key, o_clear,
    o_tls_room, o_dns, o_http_begin, o_http_send, o_http_tls_error, o_http_size, o_http_has_stream, o_http_available, o_http_read, o_http_connected, o_http_end, o_ensure_task,
    o_lock, o_unlock, NULL,
};

// ---- control desde la prueba ----
void account_fake_reset(void)
{
    if (!g_key) {
        g_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        EC_KEY_generate_key(g_key);
        EC_POINT_point2oct(EC_KEY_get0_group(g_key), EC_KEY_get0_public_key(g_key), POINT_CONVERSION_UNCOMPRESSED, g_pub, sizeof(g_pub), NULL);
    }
    flex_account_test_set_key(g_pub);
    pthread_mutex_lock(&g_nvs_mu);
    memset(&g_nvs, 0, sizeof(g_nvs));
    pthread_mutex_unlock(&g_nvs_mu);
    pthread_mutex_lock(&g_cfg_mu);
    g_pending_hash[0] = g_registered_hash[0] = 0;
    snprintf(g_address, sizeof(g_address), "ana.p4@flex");
    snprintf(g_display, sizeof(g_display), "Ana");
    pthread_mutex_unlock(&g_cfg_mu);
    atomic_store(&g_now, 1000);
    atomic_store(&g_wifi, true);
    atomic_store(&g_session, AF_SESS_OK);
    atomic_store(&g_polls_left, 2);
    atomic_store(&g_mem_ok, true);
    atomic_store(&g_latency, 5);
    atomic_store(&g_post_fail, 0);
    atomic_store(&g_post_status, 0);
    for (int i = 0; i < 4; i++) atomic_store(&g_cnt[i], 0);
}
const flex_account_ops_t *account_fake_ops(void) { return &OPS; }
void account_fake_set_wifi(bool on) { atomic_store(&g_wifi, on); }
void account_fake_set_session(int mode) { atomic_store(&g_session, mode); }
void account_fake_set_polls(int n) { atomic_store(&g_polls_left, n); }
void account_fake_set_account(const char *address, const char *display)
{
    pthread_mutex_lock(&g_cfg_mu);
    snprintf(g_address, sizeof(g_address), "%s", address);
    snprintf(g_display, sizeof(g_display), "%s", display);
    pthread_mutex_unlock(&g_cfg_mu);
}
void account_fake_set_latency(uint32_t ms) { atomic_store(&g_latency, ms); }
void account_fake_set_mem(bool ok) { atomic_store(&g_mem_ok, ok); }
void account_fake_set_post_fail(int n) { atomic_store(&g_post_fail, n); }
void account_fake_set_post_status(int status) { atomic_store(&g_post_status, status); }
void account_fake_set_ensure_task(bool (*fn)(void)) { g_ensure = fn; }
void account_fake_set_delay_hook(void (*fn)(uint32_t)) { g_delay_hook = fn; }
void account_fake_advance(uint32_t ms) { atomic_fetch_add(&g_now, ms); }
uint32_t account_fake_now(void) { return atomic_load(&g_now); }
int account_fake_count(int what) { return atomic_load(&g_cnt[what]); }
bool account_fake_nvs_linked(void)
{
    pthread_mutex_lock(&g_nvs_mu);
    bool r = g_nvs.has_linked && g_nvs.linked && g_nvs.has_token && g_nvs.has_address;
    pthread_mutex_unlock(&g_nvs_mu);
    return r;
}
void account_fake_nvs_save(const char *token43, const char *address, const char *display, int authst)
{
    pthread_mutex_lock(&g_nvs_mu);
    snprintf(g_nvs.token, sizeof(g_nvs.token), "%s", token43);
    g_nvs.has_token = true;
    snprintf(g_nvs.address, sizeof(g_nvs.address), "%s", address);
    g_nvs.has_address = true;
    snprintf(g_nvs.display, sizeof(g_nvs.display), "%s", display);
    g_nvs.has_display = true;
    g_nvs.linked = g_nvs.has_linked = true;
    g_nvs.authst = (uint8_t)authst;
    g_nvs.has_authst = authst != 0;
    pthread_mutex_unlock(&g_nvs_mu);
}
void account_fake_server_register(const char *token43)
{
    pthread_mutex_lock(&g_cfg_mu);
    hex_sha256(token43, g_registered_hash);
    pthread_mutex_unlock(&g_cfg_mu);
}
