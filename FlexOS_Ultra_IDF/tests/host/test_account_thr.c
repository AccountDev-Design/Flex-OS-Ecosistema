// Flex Account con HILOS de verdad: la tarea de fondo (core 1 en la placa) da vueltas a flex_account_step() mientras la interfaz, Flex Cloud y el usuario (core 0) piden
// enlazar, cancelar, validar, desvincular, copiar la credencial y leer la foto. Con ThreadSanitizer detecta carreras; con ASan, usos de memoria ya liberada. Ademas exige
// invariantes: la foto nunca es "a medias" (cadenas terminadas, enlazada <=> hay credencial copiable), la credencial copiada siempre tiene 43 caracteres validos, y al
// terminar el servicio sigue en un estado coherente.
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
#include <unistd.h>
#include "flex_account_svc.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static atomic_int g_fails;
#define CHECK(c, ...) do { if (!(c)) { atomic_fetch_add(&g_fails, 1); fprintf(stderr, "  FALLO: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "  (%s:%d)\n", __FILE__, __LINE__); } } while (0)

// ---- reloj virtual y mundo ----
static atomic_uint g_now = 1000;
static atomic_bool g_wifi = true;
static atomic_int g_session_mode;   // 0 ok, 1 caido, 2 revocada, 3 caducada
static atomic_int g_polls_left = 2;
static atomic_bool g_stop;
static atomic_int g_posts, g_polls, g_sessions, g_linked_seen, g_transient;
static EC_KEY *g_key;
static unsigned char g_pub[65];
static pthread_mutex_t g_big = PTHREAD_MUTEX_INITIALIZER;   // el cerrojo "del sistema" que da la placa (xSemaphore)

// NVS en memoria (los accesos reales a flash tambien estan serializados por el sistema)
static pthread_mutex_t g_nvs_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    char token[64], address[64], display[80];
    bool has_token, has_address, has_display, linked, has_linked;
    uint8_t authst;
    bool has_authst;
    uint32_t install_id;
    bool has_install;
} g_nvs;

static uint32_t o_millis(void) { return atomic_load(&g_now); }
static void o_delay(uint32_t ms)
{
    atomic_fetch_add(&g_now, ms);
    usleep(30);
}
static bool o_wifi(void) { return atomic_load(&g_wifi); }
static uint32_t g_rng = 12345;
static pthread_mutex_t g_rng_mu = PTHREAD_MUTEX_INITIALIZER;
static void o_random(void *b, size_t n)
{
    pthread_mutex_lock(&g_rng_mu);
    for (size_t i = 0; i < n; i++) {
        g_rng = g_rng * 1664525u + 1013904223u;
        ((unsigned char *)b)[i] = (unsigned char)(g_rng >> 24);
    }
    pthread_mutex_unlock(&g_rng_mu);
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
    return strlen(v);
}
static bool o_get_bool(const char *k, bool d) { return !strcmp(k, "linked") && g_nvs.has_linked ? g_nvs.linked : d; }
static bool o_put_bool(const char *k, bool v)
{
    if (!strcmp(k, "linked")) { g_nvs.linked = v; g_nvs.has_linked = true; }
    return true;
}
static uint8_t o_get_u8(const char *k, uint8_t d) { return !strcmp(k, "authst") && g_nvs.has_authst ? g_nvs.authst : d; }
static size_t o_put_u8(const char *k, uint8_t v)
{
    if (!strcmp(k, "authst")) { g_nvs.authst = v; g_nvs.has_authst = true; }
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
}
static bool o_tls_room(size_t *f, size_t *b)
{
    if (f) *f = 200u << 10;
    if (b) *b = 100u << 10;
    return true;
}
static bool o_dns(const char *h) { (void)h; return true; }

// ---- servidor: respuestas servidas desde un buffer, sin estado compartido (la tarea de fondo es la unica que habla) ----
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
    int status;
} R;
static char g_pending_hash[80];
static char g_approved_hash[80];

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
static int o_http_send(void)
{
    o_delay(5);   // la red tarda un poco: da pie a que otro hilo se meta en medio
    if (!atomic_load(&g_wifi)) return -1;
    if (!strncmp(R.url, FLEX_ACCOUNT_CODE_URL, strlen(FLEX_ACCOUNT_CODE_URL))) {
        if (!strcmp(R.method, "POST")) {
            atomic_fetch_add(&g_posts, 1);
            const char *h = strstr(R.body, "\"tokenHash\":\"");
            if (!h) return 400;
            snprintf(g_pending_hash, sizeof(g_pending_hash), "%.64s", h + 13);
            char payload[600], env[2048];
            snprintf(payload, sizeof(payload), "{\"schema\":1,\"purpose\":\"flex-device-code\",\"code\":\"FLX7Q2\",\"tokenHash\":\"%s\",\"activationUrl\":\"%sFLX7Q2\"}",
                     g_pending_hash, FLEX_ACCOUNT_ACTIVATE_PREFIX);
            make_envelope(payload, env, sizeof(env));
            reply_text(env);
            return 201;
        }
        atomic_fetch_add(&g_polls, 1);
        int left = atomic_load(&g_polls_left);
        char payload[400], env[2048];
        if (left > 0) {
            atomic_store(&g_polls_left, left - 1);
            snprintf(payload, sizeof(payload), "{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"pending\"}");
        } else {
            snprintf(g_approved_hash, sizeof(g_approved_hash), "%s", g_pending_hash);
            snprintf(payload, sizeof(payload), "{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"approved\",\"flexAddress\":\"ana.p4@flex\",\"displayName\":\"Ana\"}");
        }
        make_envelope(payload, env, sizeof(env));
        reply_text(env);
        return 200;
    }
    atomic_fetch_add(&g_sessions, 1);
    switch (atomic_load(&g_session_mode)) {
    case 1: return -1;
    case 2: reply_text("{\"ok\":false,\"error\":{\"code\":\"device_revoked\",\"message\":\"x\"}}"); return 401;
    case 3: reply_text("{\"ok\":false,\"error\":{\"code\":\"token_expired\",\"message\":\"x\"}}"); return 401;
    default: break;
    }
    reply_text("{\"ok\":true,\"account\":{\"id\":\"acc_1\",\"flexAddress\":\"ana.p4@flex\",\"displayName\":\"Ana\"}}");
    return 200;
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

static atomic_bool g_task_started;
static void *task_main(void *);
static pthread_t g_task_thread;
static bool o_ensure_task(void)
{
    bool expected = false;
    if (atomic_compare_exchange_strong(&g_task_started, &expected, true)) {
        pthread_create(&g_task_thread, NULL, task_main, NULL);
    }
    return true;
}
static void o_lock(void) { pthread_mutex_lock(&g_big); }
static void o_unlock(void) { pthread_mutex_unlock(&g_big); }

static const flex_account_ops_t OPS = {
    o_millis, o_delay, o_wifi, o_random, o_mac, o_nvs_begin, o_nvs_end, o_get_u32, o_put_u32, o_get_str, o_put_str, o_get_bool, o_put_bool, o_get_u8, o_put_u8, o_is_key, o_clear,
    o_tls_room, o_dns, o_http_begin, o_http_send, o_http_tls_error, o_http_size, o_http_has_stream, o_http_available, o_http_read, o_http_connected, o_http_end, o_ensure_task,
    o_lock, o_unlock, NULL,
};

static void *task_main(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_stop)) {
        flex_account_step();
        atomic_fetch_add(&g_now, 100);
        usleep(50);
    }
    return NULL;
}

// ---- hilos de la interfaz y de la nube ----
static unsigned xs(unsigned *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}
static void check_snapshot(const flex_account_snapshot_t *s)
{
    CHECK(memchr(s->stage, 0, sizeof(s->stage)) && memchr(s->error, 0, sizeof(s->error)) && memchr(s->code, 0, sizeof(s->code)) && memchr(s->activation_url, 0, sizeof(s->activation_url)) &&
              memchr(s->flex_address, 0, sizeof(s->flex_address)) && memchr(s->display_name, 0, sizeof(s->display_name)) && memchr(s->link_detail, 0, sizeof(s->link_detail)),
          "foto con una cadena sin terminar");
    CHECK(s->state <= FLEX_ACCOUNT_ERROR, "estado fuera de rango: %d", (int)s->state);
    CHECK((unsigned)s->link <= FLEX_LINK_ERROR, "vinculo fuera de rango: %d", (int)s->link);
    if (s->state == FLEX_ACCOUNT_LINKED) {
        atomic_fetch_add(&g_linked_seen, 1);
    }
    // Desvincular borra la foto mientras el flujo de enlace sigue vivo y puede escribir un ultimo CODE_READY antes de ver que lo cancelaron: un fotograma sin codigo.
    // Es el comportamiento de Arduino (misma estructura) y lo absorbe la pantalla (no dibuja el codigo si esta vacio); aqui solo se cuenta.
    if (s->state == FLEX_ACCOUNT_CODE_READY && !s->code[0]) {
        atomic_fetch_add(&g_transient, 1);
    }
    if (s->link == FLEX_LINK_LINKED || s->link == FLEX_LINK_LINKED_OFFLINE) {
        CHECK(s->flex_address[0] != 0, "vinculada sin direccion");
    }
}
static void check_bearer(void)
{
    char b[64];
    if (flex_account_copy_bearer(b, sizeof(b))) {
        size_t n = strlen(b);
        CHECK(n == 43, "credencial de %zu caracteres", n);
        for (size_t i = 0; i < n; i++) {
            char c = b[i];
            CHECK((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_', "caracter raro en la credencial");
        }
    }
}
static void *ui_main(void *arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg;
    for (int i = 0; i < 5000 && !atomic_load(&g_stop); i++) {
        unsigned r = xs(&seed) % 200;
        if (r < 14) {
            flex_account_request_code("Hilo");
        } else if (r < 16) {
            flex_account_cancel();
        } else if (r < 17) {
            flex_account_forget_local();
        } else if (r < 30) {
            flex_account_request_validation();
        } else if (r < 100) {
            flex_account_snapshot_t s;
            flex_account_snapshot(&s);
            check_snapshot(&s);
        } else if (r < 120) {
            check_bearer();
        } else if (r < 140) {
            (void)flex_account_linked();
            (void)flex_account_usable();
            (void)flex_account_state();
            (void)flex_account_link_state();
        } else if (r < 146) {
            atomic_store(&g_wifi, xs(&seed) % 5 != 0);
        } else if (r < 152) {
            atomic_store(&g_session_mode, (int)(xs(&seed) % 4 == 0 ? xs(&seed) % 4 : 0));
        } else if (r < 158) {
            atomic_store(&g_polls_left, (int)(xs(&seed) % 4));
        }
        usleep(120);
    }
    return NULL;
}
static void *cloud_main(void *arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg;
    for (int i = 0; i < 5000 && !atomic_load(&g_stop); i++) {
        usleep(60);
        switch (xs(&seed) % 4) {
        case 0: flex_account_report_rejected(); break;
        case 1: check_bearer(); break;
        case 2: (void)flex_account_usable(); break;
        default: usleep(10); break;
        }
    }
    return NULL;
}

int main(void)
{
    g_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(g_key);
    EC_POINT_point2oct(EC_KEY_get0_group(g_key), EC_KEY_get0_public_key(g_key), POINT_CONVERSION_UNCOMPRESSED, g_pub, sizeof(g_pub), NULL);
    flex_account_test_set_key(g_pub);
    static const flex_account_cfg_t cfg = {"1.0.0"};
    flex_account_set(&cfg, &OPS);
    flex_account_begin();

    pthread_t ui[3], cloud;
    for (int i = 0; i < 3; i++) pthread_create(&ui[i], NULL, ui_main, (void *)(uintptr_t)(0x1234567u + (unsigned)i * 7919u));
    pthread_create(&cloud, NULL, cloud_main, (void *)(uintptr_t)0xBEEFu);
    for (int i = 0; i < 3; i++) pthread_join(ui[i], NULL);
    pthread_join(cloud, NULL);
    atomic_store(&g_stop, true);
    pthread_join(g_task_thread, NULL);

    // al final, con todo quieto, el servicio esta coherente
    flex_account_snapshot_t s;
    flex_account_snapshot(&s);
    check_snapshot(&s);
    CHECK(flex_account_linked() == (s.linked != 0), "linked() incoherente con la foto");
    EC_KEY_free(g_key);
    printf("  (%d codigos pedidos, %d consultas, %d validaciones, %d fotos con la cuenta vinculada, %d fotogramas CODE_READY sin codigo)\n", atomic_load(&g_posts), atomic_load(&g_polls),
           atomic_load(&g_sessions), atomic_load(&g_linked_seen), atomic_load(&g_transient));
    int f = atomic_load(&g_fails);
    printf(f ? "FALLOS: %d (Flex Account con hilos)\n" : "OK: Flex Account con hilos\n", f);
    return f ? 1 : 0;
}
