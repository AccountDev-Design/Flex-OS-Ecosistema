// OTA (N5c): flex_ota_port.c COMPILADO EN EL HOST contra dobles de ESP-IDF (tests/host/ota_port_stub): esp_http_client, esp_ota, mbedtls MD5, NVS, temporizador y
// tareas guionizados. No sustituye a la placa (la flash, el TLS y el bootloader de verdad siguen sin probarse), pero comprueba la parte que decide la seguridad:
//   * la configuracion (certificado fijado, nada de TLS inseguro, version del firmware),
//   * HTTP: redirecciones, codigos, tiempos, lo que lee y lo que dice que le queda,
//   * flash: el MD5 de lo ESCRITO se compara antes de cerrar, el proyecto de la imagen tiene que ser el que corre, y la particion de arranque solo cambia al final,
//   * el motor real (flex_ota.c) por encima de todo eso: instalacion completa, MD5 malo, imagen de otra aplicacion, cancelar, atasco, truncada, sin memoria,
//   * el saludo posterior al reinicio y la confirmacion de arranque (rollback).
//   tests/host/run.sh
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "flex_ota.h"
#include "flex_ota_port.h"
#include "flex_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/md5.h"

static int g_fail, g_checks;
#define CHECK(c)                                                        \
    do {                                                                \
        g_checks++;                                                     \
        if (!(c)) {                                                     \
            g_fail++;                                                   \
            printf("ota_port: FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                               \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                               \
    do {                                                                                                                             \
        long long _a = (long long)(a), _b = (long long)(b);                                                                          \
        g_checks++;                                                                                                                  \
        if (_a != _b) {                                                                                                              \
            g_fail++;                                                                                                                \
            printf("ota_port: FALLO %s:%d: %s == %lld, esperado %lld\n", __FILE__, __LINE__, #a, _a, _b);                            \
        }                                                                                                                            \
    } while (0)

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// MD5 real (RFC 1321): lo que el puerto compara con el manifiesto tiene que ser el MD5 de verdad
static unsigned K5[64];
static const int S5[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                           4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
static void md5_block(unsigned st[4], const unsigned char *p)
{
    unsigned M[16];
    for (int i = 0; i < 16; i++) {
        M[i] = (unsigned)p[4 * i] | (unsigned)p[4 * i + 1] << 8 | (unsigned)p[4 * i + 2] << 16 | (unsigned)p[4 * i + 3] << 24;
    }
    unsigned a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; i++) {
        unsigned f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        f = f + a + K5[i] + M[g];
        a = d; d = c; c = b;
        b = b + ((f << S5[i]) | (f >> (32 - S5[i])));
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}
void mbedtls_md5_init(mbedtls_md5_context *c) { memset(c, 0, sizeof(*c)); }
int mbedtls_md5_starts(mbedtls_md5_context *c)
{
    for (int i = 0; i < 64; i++) K5[i] = (unsigned)(fabs(sin((double)(i + 1))) * 4294967296.0);
    c->state[0] = 0x67452301u; c->state[1] = 0xefcdab89u; c->state[2] = 0x98badcfeu; c->state[3] = 0x10325476u;
    c->count = 0;
    return 0;
}
int mbedtls_md5_update(mbedtls_md5_context *c, const unsigned char *in, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        c->buf[c->count % 64] = in[i];
        c->count++;
        if (c->count % 64 == 0) md5_block(c->state, c->buf);
    }
    return 0;
}
int mbedtls_md5_finish(mbedtls_md5_context *c, unsigned char out[16])
{
    unsigned long long bits = c->count * 8;
    unsigned char pad = 0x80;
    mbedtls_md5_update(c, &pad, 1);
    pad = 0;
    while (c->count % 64 != 56) mbedtls_md5_update(c, &pad, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (8 * i));
    mbedtls_md5_update(c, len, 8);
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out[4 * i + j] = (unsigned char)(c->state[i] >> (8 * j));
    return 0;
}
void mbedtls_md5_free(mbedtls_md5_context *c) { memset(c, 0, sizeof(*c)); }
static void md5_hex(const unsigned char *d, size_t n, char out[33])
{
    mbedtls_md5_context c;
    unsigned char dg[16];
    mbedtls_md5_init(&c);
    mbedtls_md5_starts(&c);
    mbedtls_md5_update(&c, d, n);
    mbedtls_md5_finish(&c, dg);
    for (int i = 0; i < 16; i++) snprintf(out + 2 * i, 3, "%02x", dg[i]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// el mundo guionizado
#define BODY_MAX 400000
typedef struct { int open_err; long long hdr; int status; int redir_err; } Hop;
static struct {
    uint64_t now_us;
    size_t heap;
    bool net, safe, reboot_seen;
    esp_app_desc_t run_desc, new_desc;
    esp_partition_t slot[2];
    int no_next, state_err;
    esp_ota_img_states_t run_state;
    // OTA
    int begin_err, end_err, set_boot_err, desc_err, mark_err, write_fail_at;
    int begins, writes, ends, aborts, set_boots, marks, reboots;
    size_t begin_size, written;
    esp_ota_handle_t handle, open_h;
    unsigned char flash[BODY_MAX];
    char seq[4096];
    int nseq;
    // HTTP
    Hop hop[8];
    int init_fail, inits, opens, closes, cleanups, redirs, timeout_set;
    esp_http_client_config_t cfg;
    char cfg_cert[64], enc_hdr[32];
    const unsigned char *body;
    size_t blen, bpos;
    long long eagain_at, err_at, hook_at;
    void (*hook)(void);
    // temporizador y tareas
    void (*tcb)(void *);
    uint64_t tperiod;
    int tstops, tcreated;
    int task_created, task_fail, task_stack, task_prio, task_core;
    char task_name[32];
    // locks y log
    int takes, gives, errlog;
    char pend[64];
    int pend_writes;
} P;

static void seq(char c)
{
    if (P.nseq < (int)sizeof(P.seq) - 1) {
        P.seq[P.nseq++] = c;
        P.seq[P.nseq] = 0;
    }
}

void ota_stub_log(int level, const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
    if (level >= 3) {
        P.errlog++;
    }
}
const char *esp_err_to_name(int e)
{
    (void)e;
    return "ERR";
}
const esp_app_desc_t *esp_app_get_description(void) { return &P.run_desc; }
size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return P.heap;
}
int64_t esp_timer_get_time(void) { return (int64_t)P.now_us; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *out)
{
    P.tcb = a->callback;
    P.tcreated++;
    *out = (esp_timer_handle_t)&P;
    return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t us)
{
    (void)t;
    P.tperiod = us;
    return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t t)
{
    (void)t;
    P.tstops++;
    return ESP_OK;
}
struct ota_stub_mutex { int x; };
struct ota_stub_task { int x; };
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static struct ota_stub_mutex m;
    return &m;
}
int xSemaphoreTake(SemaphoreHandle_t m, TickType_t t)
{
    (void)m;
    (void)t;
    P.takes++;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t m)
{
    (void)m;
    P.gives++;
    return 1;
}
void vTaskDelay(TickType_t t) { P.now_us += (uint64_t)t * 1000u; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *name, uint32_t stack, void *arg, unsigned prio, TaskHandle_t *out, int core)
{
    (void)f;
    (void)arg;
    if (P.task_fail) {
        return 0;
    }
    static struct ota_stub_task t;
    P.task_created++;
    P.task_stack = (int)stack;
    P.task_prio = (int)prio;
    P.task_core = core;
    snprintf(P.task_name, sizeof(P.task_name), "%s", name);
    *out = &t;
    return pdPASS;
}
bool flex_net_online(void) { return P.net; }
bool flex_safe_mode(void) { return P.safe; }
void flex_reset_reboot(void)
{
    P.reboots++;
    seq('R');
}
esp_err_t flex_kvs_set_str(const char *ns, const char *key, const char *v)
{
    CHECK(!strcmp(ns, "flexota") && !strcmp(key, "pend"));
    snprintf(P.pend, sizeof(P.pend), "%s", v);
    P.pend_writes++;
    return ESP_OK;
}
void flex_kvs_get_str(const char *ns, const char *key, char *out, size_t cap, const char *def)
{
    CHECK(!strcmp(ns, "flexota") && !strcmp(key, "pend"));
    snprintf(out, cap, "%s", P.pend[0] ? P.pend : def);
}

// ---- esp_ota ----
const esp_partition_t *esp_ota_get_running_partition(void) { return &P.slot[0]; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *s)
{
    (void)s;
    return P.no_next ? NULL : &P.slot[1];
}
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *out)
{
    CHECK(p == &P.slot[1]);   // nunca la ranura que corre
    P.begins++;
    P.begin_size = size;
    if (P.begin_err) return P.begin_err;
    *out = P.open_h = ++P.handle;
    P.written = 0;
    seq('b');
    return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *d, size_t n)
{
    CHECK(h == P.open_h && P.open_h != 0);
    if (P.write_fail_at >= 0 && P.writes >= P.write_fail_at) return ESP_FAIL;
    if (P.written + n <= BODY_MAX) memcpy(P.flash + P.written, d, n);
    P.written += n;
    P.writes++;
    seq('W');
    return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t h)
{
    CHECK(h == P.open_h);
    P.ends++;
    seq('E');
    return P.end_err;
}
esp_err_t esp_ota_abort(esp_ota_handle_t h)
{
    CHECK(h == P.open_h);
    P.aborts++;
    P.open_h = 0;
    seq('A');
    return ESP_OK;
}
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *d)
{
    CHECK(p == &P.slot[1]);
    if (P.desc_err) return P.desc_err;
    *d = P.new_desc;
    return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{
    CHECK(p == &P.slot[1]);
    P.set_boots++;
    seq('B');
    return P.set_boot_err;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *s)
{
    CHECK(p == &P.slot[0]);
    if (P.state_err) return P.state_err;
    *s = P.run_state;
    return ESP_OK;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    P.marks++;
    return P.mark_err;
}

// ---- esp_http_client ----
static struct esp_http_client { int x; } s_cli;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    P.inits++;
    if (P.init_fail) return NULL;
    P.cfg = *c;
    snprintf(P.cfg_cert, sizeof(P.cfg_cert), "%s", c->cert_pem ? c->cert_pem : "");
    P.cfg.cert_pem = P.cfg_cert;
    P.cfg.url = NULL;
    return &s_cli;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v)
{
    CHECK(c == &s_cli);
    if (!strcmp(k, "Accept-Encoding")) snprintf(P.enc_hdr, sizeof(P.enc_hdr), "%s", v);
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int w)
{
    CHECK(c == &s_cli && w == 0);
    int i = P.opens++;
    CHECK(i < 8);
    return i < 8 ? P.hop[i].open_err : ESP_FAIL;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    (void)c;
    return P.hop[P.opens - 1].hdr;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c)
{
    (void)c;
    return P.hop[P.opens - 1].status;
}
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t c)
{
    (void)c;
    P.redirs++;
    return P.hop[P.opens - 1].redir_err;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t c)
{
    (void)c;
    P.closes++;
    return ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{
    (void)c;
    P.cleanups++;
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c, int ms)
{
    (void)c;
    P.timeout_set = ms;
    return ESP_OK;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{
    (void)c;
    return (long long)P.bpos >= (long long)P.blen && P.eagain_at < 0 && P.err_at < 0;
}
int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len)
{
    (void)c;
    if (P.err_at >= 0 && (long long)P.bpos >= P.err_at) return -1;
    if (P.eagain_at >= 0 && (long long)P.bpos >= P.eagain_at) {
        P.now_us += 1000u * 1000u;   // la rebanada de 1 s se agota
        return -ESP_ERR_HTTP_EAGAIN;
    }
    size_t n = P.blen - P.bpos;
    if (n > (size_t)len) n = (size_t)len;
    if (P.eagain_at >= 0 && P.bpos + n > (size_t)P.eagain_at) n = (size_t)P.eagain_at - P.bpos;
    if (P.err_at >= 0 && P.bpos + n > (size_t)P.err_at) n = (size_t)P.err_at - P.bpos;
    memcpy(buf, P.body + P.bpos, n);
    P.bpos += n;
    P.now_us += n * 2u;   // ~500 KB/s
    if (P.hook && P.hook_at >= 0 && (long long)P.bpos >= P.hook_at) {
        void (*h)(void) = P.hook;
        P.hook = NULL;
        h();
    }
    return (int)n;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
const char ota_root_ca_pem_def[] asm("_binary_ota_root_ca_pem_start") = "-----BEGIN FAKE CA-----";

static unsigned char s_body[BODY_MAX];
#define N_IMG 100000
static void world_reset(void)
{
    unsigned char *keep = s_body;
    (void)keep;
    memset(&P, 0, sizeof(P));
    P.heap = 200000;
    P.net = true;
    P.write_fail_at = -1;
    P.eagain_at = P.err_at = P.hook_at = -1;
    snprintf(P.run_desc.version, sizeof(P.run_desc.version), "1.0.0");
    snprintf(P.run_desc.project_name, sizeof(P.run_desc.project_name), "flexos_ultra_idf");
    P.new_desc = P.run_desc;
    snprintf(P.new_desc.version, sizeof(P.new_desc.version), "1.2.0");
    P.slot[0].id = 0;
    P.slot[1].id = 1;
    P.run_state = ESP_OTA_IMG_VALID;
    for (int i = 0; i < 8; i++) P.hop[i] = (Hop){0, N_IMG, 200, 0};
    P.body = s_body;
    P.blen = N_IMG;
    for (int i = 0; i < N_IMG; i++) s_body[i] = (unsigned char)(i * 7 + 3 + (i >> 8));
}

static const flex_ota_ops_t *OPS;
static int count_char(const char *s, char c)
{
    int n = 0;
    for (; *s; s++) n += *s == c;
    return n;
}

// ---- configuracion y operaciones sueltas ----
static void t_cfg(void)
{
    world_reset();
    const flex_ota_cfg_t *c = flex_ota_port_cfg();
    CHECK(c != NULL);
    CHECK(!strcmp(c->local_version, "1.0.0"));   // la del firmware (esp_app_desc)
    CHECK(!strcmp(c->key, FLEX_OTA_KEY) && !strcmp(c->key, "flex_os_ultra_idf"));
    CHECK(!strcmp(c->manifest_url, "https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Ecosistema/main/ota/manifest.json"));
    CHECK(!strcmp(c->root_ca, "-----BEGIN FAKE CA-----"));   // el certificado embebido
    CHECK(!c->allow_insecure && !c->require_signature && !c->antirollback);   // nunca TLS inseguro en la placa
    CHECK_EQ(c->min_heap, FLEX_OTA_MIN_HEAP);
    OPS = flex_ota_port_ops();
    CHECK(OPS != NULL);
    CHECK(OPS->millis && OPS->delay_ms && OPS->net_ready && OPS->free_heap && OPS->next_partition && OPS->http_begin && OPS->http_get && OPS->http_size &&
          OPS->http_available && OPS->http_read && OPS->http_connected && OPS->http_stream && OPS->http_end && OPS->upd_begin && OPS->upd_set_md5 && OPS->upd_write &&
          OPS->upd_end && OPS->upd_finished && OPS->upd_abort && OPS->save_pending && OPS->load_pending && OPS->clear_pending && OPS->restart && OPS->lock && OPS->unlock);
    CHECK(!OPS->panel_lock && !OPS->panel_unlock);   // el controlador de flash de IDF gestiona la cache; el panel DSI lee de la PSRAM
    // tiempo, red y memoria
    P.now_us = 12345678;
    CHECK_EQ(OPS->millis(), 12345);
    uint32_t t0 = OPS->millis();
    OPS->delay_ms(0);
    CHECK_EQ(OPS->millis() - t0, 1);   // nunca cede 0 ticks: como poco 1 ms
    t0 = OPS->millis();
    OPS->delay_ms(120);
    CHECK_EQ(OPS->millis() - t0, 120);
    P.net = false;
    CHECK(!OPS->net_ready());
    P.net = true;
    CHECK(OPS->net_ready());
    P.heap = 4242;
    CHECK_EQ(OPS->free_heap(), 4242);
    CHECK(OPS->next_partition());
    P.no_next = 1;
    CHECK(!OPS->next_partition());
}

static void t_md5_stub(void)
{
    char h[33];
    md5_hex((const unsigned char *)"", 0, h);
    CHECK(!strcmp(h, "d41d8cd98f00b204e9800998ecf8427e"));
    md5_hex((const unsigned char *)"abc", 3, h);
    CHECK(!strcmp(h, "900150983cd24fb0d6963f7d28e17f72"));
    unsigned char big[1000];
    memset(big, 'a', sizeof(big));
    md5_hex(big, sizeof(big), h);
    CHECK(!strcmp(h, "cabe45dcc9ae5b66ba86600cca6b8ba8"));
}

static void t_http(void)
{
    world_reset();
    // sin certificado o inseguro: nada se abre
    CHECK(!OPS->http_begin("https://x/y", NULL, false, 12000));
    CHECK(!OPS->http_begin("https://x/y", "", false, 12000));
    CHECK(!OPS->http_begin("https://x/y", "PEM", true, 12000));
    CHECK_EQ(P.inits, 0);
    P.init_fail = 1;
    CHECK(!OPS->http_begin("https://x/y", "PEM", false, 12000));
    P.init_fail = 0;
    CHECK(OPS->http_begin("https://x/y", "PEM-X", false, 12000));
    CHECK(!strcmp(P.cfg_cert, "PEM-X") && P.cfg.timeout_ms == 12000 && P.cfg.max_redirection_count == 5 && !P.cfg.keep_alive_enable && !P.cfg.skip_cert_common_name_check);
    CHECK(P.cfg.buffer_size == 2048 && P.cfg.buffer_size_tx == 1024);
    CHECK(!strcmp(P.enc_hdr, "identity"));   // sin gzip: el Content-Length es el del binario
    CHECK(!OPS->http_stream() && !OPS->http_connected());   // abierto el cliente, pero aun sin peticion: no hay flujo
    // 200 directo
    CHECK_EQ(OPS->http_get(), 200);
    CHECK_EQ(P.opens, 1);
    CHECK_EQ(P.timeout_set, 1000);   // el cuerpo se lee a rebanadas de 1 s
    CHECK_EQ(OPS->http_size(), N_IMG);
    CHECK(OPS->http_stream());
    OPS->http_end();
    CHECK(!OPS->http_stream());   // y no queda nada abierto
    CHECK_EQ(P.cleanups, 1);
    CHECK(P.closes >= 1);
    // 404 se devuelve tal cual (el motor lo traduce)
    world_reset();
    P.hop[0].status = 404;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 404);
    OPS->http_end();
    // 302 -> 200: sigue la redireccion UNA vez, cerrando antes de volver a abrir
    world_reset();
    P.hop[0] = (Hop){0, 0, 302, 0};
    P.hop[1] = (Hop){0, N_IMG, 200, 0};
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 200);
    CHECK(P.opens == 2 && P.redirs == 1 && P.closes >= 1);
    CHECK_EQ(OPS->http_size(), N_IMG);
    OPS->http_end();
    // cada codigo de redireccion
    static const int REDIR[] = {301, 302, 303, 307, 308};
    for (unsigned k = 0; k < 5; k++) {
        world_reset();
        P.hop[0] = (Hop){0, 0, REDIR[k], 0};
        CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
        CHECK_EQ(OPS->http_get(), 200);
        CHECK_EQ(P.redirs, 1);
        OPS->http_end();
    }
    world_reset();   // 300 y 304 no son redirecciones que se sigan
    P.hop[0] = (Hop){0, 0, 304, 0};
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 304);   // no es una redireccion: el motor lo dice como estado HTTP
    OPS->http_end();
    // la redireccion que esp_http_client rechaza (bajada a http://) es un fallo de transporte
    world_reset();
    P.hop[0] = (Hop){0, 0, 302, ESP_FAIL};
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), FLEX_OTA_HTTPE_OTHER);
    OPS->http_end();
    // redirecciones sin fin: como mucho 5
    world_reset();
    for (int i = 0; i < 8; i++) P.hop[i] = (Hop){0, 0, 302, 0};
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), FLEX_OTA_HTTPE_OTHER);
    CHECK_EQ(P.opens, 6);
    OPS->http_end();
    // errores de transporte
    world_reset();
    P.hop[0].open_err = ESP_ERR_TIMEOUT;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), FLEX_OTA_HTTPE_TIMEOUT);
    OPS->http_end();
    world_reset();
    P.hop[0].open_err = ESP_ERR_HTTP_CONNECT;   // el certificado no valida, DNS, rechazo...
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), FLEX_OTA_HTTPE_OTHER);
    OPS->http_end();
    world_reset();
    P.hop[0].hdr = -ESP_ERR_HTTP_EAGAIN;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), FLEX_OTA_HTTPE_TIMEOUT);
    OPS->http_end();
    world_reset();
    P.hop[0].hdr = -1;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), FLEX_OTA_HTTPE_OTHER);
    OPS->http_end();
    // sin cliente
    CHECK_EQ(OPS->http_get(), FLEX_OTA_HTTPE_OTHER);
    CHECK_EQ(OPS->http_size(), 0);
    CHECK(!OPS->http_stream() && !OPS->http_connected() && OPS->http_available() == 0);
    // tamano enorme: se acota a lo que cabe en un int; Content-Length 0 (chunked) = 0
    world_reset();
    P.hop[0].hdr = 3000000000LL;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 200);
    CHECK_EQ(OPS->http_size(), 0x7fffffff);
    OPS->http_end();
    world_reset();
    P.hop[0].hdr = 0;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 200);
    CHECK_EQ(OPS->http_size(), 0);
    OPS->http_end();
    // volver a empezar cierra lo anterior
    world_reset();
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK(OPS->http_begin("https://x/z", "PEM", false, 12000));
    CHECK_EQ(P.cleanups, 1);
    OPS->http_end();
    CHECK_EQ(P.cleanups, 2);
}

static void t_read(void)
{
    world_reset();
    P.blen = 10000;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 200);
    uint8_t buf[4096];
    CHECK(OPS->http_connected() && OPS->http_available() == FLEX_OTA_CHUNK);   // mientras falte, "puede haber datos"
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 4096);
    CHECK(!memcmp(buf, s_body, 4096));
    CHECK_EQ(OPS->http_read(buf, 100), 100);
    CHECK(!memcmp(buf, s_body + 4096, 100));
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 4096);
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 10000 - 4096 - 100 - 4096);
    CHECK(!OPS->http_connected() && OPS->http_available() == 0);   // todo recibido
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 0);   // fin de los datos
    OPS->http_end();
    // una rebanada sin datos no es un error ni cierra la conexion
    world_reset();
    P.eagain_at = 5000;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 200);
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 4096);
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 904);
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 0);
    CHECK(OPS->http_connected() && OPS->http_available() > 0);
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 0);
    OPS->http_end();
    // un error de lectura: -1 y la conexion queda rota
    world_reset();
    P.err_at = 3000;
    CHECK(OPS->http_begin("https://x/y", "PEM", false, 12000));
    CHECK_EQ(OPS->http_get(), 200);
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), 3000);
    CHECK_EQ(OPS->http_read(buf, sizeof(buf)), -1);
    CHECK(!OPS->http_connected() && OPS->http_available() == 0);
    OPS->http_end();
}

// ---- flash ----
static void begin_ok(void)
{
    world_reset();
    P.blen = N_IMG;
}
static void write_all(const uint8_t *d, size_t n)
{
    for (size_t o = 0; o < n; o += 4096) {
        size_t k = n - o < 4096 ? n - o : 4096;
        CHECK_EQ(OPS->upd_write(d + o, k), k);
    }
}
static void t_flash(void)
{
    char md5[33];
    md5_hex(s_body, N_IMG, md5);
    // sin segunda particion: ni se intenta
    begin_ok();
    P.no_next = 1;
    CHECK(!OPS->upd_begin(N_IMG));
    CHECK_EQ(P.begins, 0);
    // esp_ota_begin falla (no cabe / imagen sin confirmar): nada abierto que abortar
    begin_ok();
    P.begin_err = ESP_FAIL;
    CHECK(!OPS->upd_begin(N_IMG));
    OPS->upd_abort();
    CHECK_EQ(P.aborts, 0);
    CHECK_EQ(P.begin_size, N_IMG);
    // lo normal
    begin_ok();
    CHECK(OPS->upd_begin(N_IMG));
    CHECK_EQ(P.begin_size, N_IMG);   // se borra solo lo que ocupa la imagen
    OPS->upd_set_md5(md5);
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_finished());
    CHECK(OPS->upd_end());
    CHECK(OPS->upd_finished());
    CHECK(P.ends == 1 && P.set_boots == 1 && P.aborts == 0);
    CHECK(!memcmp(P.flash, s_body, N_IMG) && P.written == N_IMG);
    CHECK(P.seq[strlen(P.seq) - 1] == 'B' && P.seq[strlen(P.seq) - 2] == 'E');   // la particion de arranque cambia DESPUES de validar, y la ultima
    CHECK(strchr(P.seq, 'B') == P.seq + strlen(P.seq) - 1);
    OPS->upd_abort();   // ya cerrado: no hay nada que abortar
    CHECK_EQ(P.aborts, 0);
    CHECK(!OPS->upd_finished());
    // MD5 en mayusculas (con un cuerpo cuyo MD5 lleva A y F, los extremos del rango que se pasa a minusculas)
    begin_ok();
    {
        for (int tweak = 0; tweak < 400; tweak++) {
            s_body[0] = (unsigned char)tweak;
            md5_hex(s_body, N_IMG, md5);
            if (strchr(md5, 'a') && strchr(md5, 'f')) break;
        }
        CHECK(strchr(md5, 'a') && strchr(md5, 'f'));
    }
    CHECK(OPS->upd_begin(N_IMG));
    char up[33];
    for (int i = 0; i < 33; i++) up[i] = md5[i] >= 'a' && md5[i] <= 'f' ? (char)(md5[i] - 32) : md5[i];
    OPS->upd_set_md5(up);
    write_all(s_body, N_IMG);
    CHECK(OPS->upd_end());
    // MD5 que no es el de lo escrito: no se cierra ni se toca la particion de arranque; el motor aborta
    begin_ok();
    CHECK(OPS->upd_begin(N_IMG));
    char bad[33];
    memcpy(bad, md5, 33);
    bad[31] = bad[31] == '0' ? '1' : '0';
    OPS->upd_set_md5(bad);
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_end());
    CHECK(P.ends == 0 && P.set_boots == 0 && !OPS->upd_finished());
    OPS->upd_abort();
    CHECK_EQ(P.aborts, 1);
    CHECK(count_char(P.seq, 'B') == 0);
    // MD5 correcto pero de OTRA imagen (la que se escribio no es la del manifiesto): mismo rechazo
    begin_ok();
    CHECK(OPS->upd_begin(N_IMG));
    OPS->upd_set_md5(md5);
    write_all(s_body, N_IMG - 1);
    CHECK(!OPS->upd_end());
    CHECK(P.set_boots == 0);
    OPS->upd_abort();
    // sin MD5 utilizable el cierre sigue siendo el de IDF (cabecera, sumas, SHA-256)
    begin_ok();
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(OPS->upd_end());
    CHECK(P.ends == 1 && P.set_boots == 1);
    // la imagen no valida (esp_ota_end falla): no se cambia la particion de arranque
    begin_ok();
    P.end_err = ESP_FAIL;
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_end());
    CHECK(P.ends == 1 && P.set_boots == 0 && !OPS->upd_finished());
    OPS->upd_abort();
    // la imagen es de OTRA aplicacion (Arduino incluido): no se instala
    begin_ok();
    snprintf(P.new_desc.project_name, sizeof(P.new_desc.project_name), "FlexOS_Ultra");
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_end());
    CHECK(P.ends == 1 && P.set_boots == 0 && !OPS->upd_finished());
    begin_ok();   // y un nombre que solo EMPIEZA igual
    snprintf(P.new_desc.project_name, sizeof(P.new_desc.project_name), "flexos_ultra_idf2");
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_end() && P.set_boots == 0);
    begin_ok();
    snprintf(P.new_desc.project_name, sizeof(P.new_desc.project_name), "flexos_ultra_id");   // ...o que es un prefijo
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_end() && P.set_boots == 0);
    // no se puede leer la descripcion de la imagen
    begin_ok();
    P.desc_err = ESP_FAIL;
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_end() && P.set_boots == 0);
    // esp_ota_set_boot_partition falla
    begin_ok();
    P.set_boot_err = ESP_FAIL;
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(!OPS->upd_end());
    CHECK(P.set_boots == 1 && !OPS->upd_finished());
    // escritura que falla: 0 bytes escritos y el MD5 no avanza
    begin_ok();
    P.write_fail_at = 3;
    CHECK(OPS->upd_begin(N_IMG));
    CHECK_EQ(OPS->upd_write(s_body, 4096), 4096);
    CHECK_EQ(OPS->upd_write(s_body, 4096), 4096);
    CHECK_EQ(OPS->upd_write(s_body, 4096), 4096);
    CHECK_EQ(OPS->upd_write(s_body, 4096), 0);
    OPS->upd_abort();
    CHECK_EQ(P.aborts, 1);
    // escribir sin haber abierto
    CHECK_EQ(OPS->upd_write(s_body, 10), 0);
    CHECK(!OPS->upd_end());
    // las dos versiones de un mismo manifiesto no se mezclan: begin -> abort -> begin
    begin_ok();
    CHECK(OPS->upd_begin(N_IMG));
    OPS->upd_set_md5("00000000000000000000000000000001");
    OPS->upd_abort();
    CHECK(OPS->upd_begin(N_IMG));
    write_all(s_body, N_IMG);
    CHECK(OPS->upd_end());   // sin MD5 del intento anterior
}

static void t_pending(void)
{
    world_reset();
    char v[16];
    CHECK(!OPS->load_pending(v, sizeof(v)));
    OPS->save_pending("1.2.0");
    CHECK(OPS->load_pending(v, sizeof(v)) && !strcmp(v, "1.2.0"));
    OPS->clear_pending();
    CHECK(!OPS->load_pending(v, sizeof(v)));
    OPS->restart();
    CHECK_EQ(P.reboots, 1);
    // el cerrojo es de verdad un cerrojo
    P.takes = P.gives = 0;
    OPS->lock();
    CHECK(P.takes == 1 && P.gives == 0);
    OPS->unlock();
    CHECK(P.takes == 1 && P.gives == 1);
}

// ---- el motor real por encima del transporte ----
static int lockbal(void) { return P.takes - P.gives; }
static void prime_img(const char *md5, unsigned total)
{
    flex_ota_service_reset();
    flex_ota_prime("1.2.0", "https://x/fw.bin", md5, total);
}
static flex_ota_info_t info(void)
{
    flex_ota_info_t i;
    flex_ota_get(&i);
    return i;
}
static void hook_cancel(void) { flex_ota_cancel(); }
static void t_engine(void)
{
    char md5[33];
    md5_hex(s_body, N_IMG, md5);
    flex_ota_set(NULL, NULL);   // las operaciones de la placa
    // instalacion completa
    world_reset();
    prime_img(md5, N_IMG);
    CHECK(flex_ota_run_install());
    flex_ota_info_t i = info();
    CHECK(i.state == FLEX_OTA_REBOOTING && i.percent == 100 && i.done == N_IMG && i.total == N_IMG);
    CHECK(P.begins == 1 && P.begin_size == N_IMG && P.written == N_IMG && P.ends == 1 && P.set_boots == 1 && P.aborts == 0 && P.reboots == 1);
    CHECK(!memcmp(P.flash, s_body, N_IMG));
    CHECK(!strcmp(P.pend, "1.2.0"));   // el saludo del siguiente arranque
    CHECK(count_char(P.seq, 'W') == (N_IMG + 4095) / 4096);
    CHECK(P.seq[strlen(P.seq) - 3] == 'E' && P.seq[strlen(P.seq) - 2] == 'B' && P.seq[strlen(P.seq) - 1] == 'R');   // validar, cambiar la particion, reiniciar
    CHECK_EQ(lockbal(), 0);
    // MD5 del manifiesto que no es el de la imagen: ni se cambia la particion ni se reinicia ni se apunta el saludo
    world_reset();
    char bad[33];
    memcpy(bad, md5, 33);
    bad[0] = bad[0] == 'a' ? 'b' : 'a';
    prime_img(bad, N_IMG);
    CHECK(!flex_ota_run_install());
    i = info();
    CHECK(i.state == FLEX_OTA_ERROR && i.error == FLEX_OTA_ERR_CORRUPT);
    CHECK(P.set_boots == 0 && P.reboots == 0 && P.aborts == 1 && !P.pend[0] && P.ends == 0);
    CHECK_EQ(lockbal(), 0);
    // un relleno de ceros no es un MD5: se ignora y la instalacion sigue (IDF valida la imagen por su cuenta)
    world_reset();
    prime_img("00000000000000000000000000000000", N_IMG);
    CHECK(flex_ota_run_install());
    CHECK(P.set_boots == 1 && P.reboots == 1);
    // imagen de otra aplicacion
    world_reset();
    snprintf(P.new_desc.project_name, sizeof(P.new_desc.project_name), "otra_app");
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install());
    i = info();
    CHECK(i.error == FLEX_OTA_ERR_CORRUPT && P.set_boots == 0 && P.reboots == 0 && P.ends == 1 && P.aborts == 0);   // esp_ota_end ya soltó el manejador: no hay nada que abortar
    // esp_ota_end rechaza la imagen
    world_reset();
    P.end_err = ESP_FAIL;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install());
    CHECK(info().error == FLEX_OTA_ERR_CORRUPT && P.set_boots == 0 && P.reboots == 0);
    // cancelar a mitad: aborta, nada se cambia
    world_reset();
    P.hook = hook_cancel;
    P.hook_at = N_IMG / 3;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install());
    i = info();
    CHECK(i.state == FLEX_OTA_ERROR && i.error == FLEX_OTA_ERR_CANCELLED);
    CHECK(P.aborts == 1 && P.set_boots == 0 && P.reboots == 0 && P.ends == 0 && P.written < N_IMG);
    CHECK_EQ(lockbal(), 0);
    // atasco: ni un byte en 15 s
    world_reset();
    P.eagain_at = N_IMG / 2;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install());
    i = info();
    CHECK(i.error == FLEX_OTA_ERR_TIMEOUT && P.aborts == 1 && P.set_boots == 0);
    CHECK(P.now_us >= 15u * 1000u * 1000u);
    // el servidor corta: error de lectura
    world_reset();
    P.err_at = N_IMG / 2;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install());
    CHECK(info().error == FLEX_OTA_ERR_TRUNCATED && P.aborts == 1 && P.set_boots == 0);
    // llega menos de lo anunciado: truncada
    world_reset();
    P.blen = N_IMG - 5000;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install());
    CHECK(info().error == FLEX_OTA_ERR_TRUNCATED && P.aborts == 1 && P.set_boots == 0);
    // llega de mas: solo se escribe lo anunciado
    world_reset();
    P.blen = N_IMG;
    P.hop[0].hdr = N_IMG - 1000;
    prime_img(md5, N_IMG);
    CHECK(flex_ota_run_install() || info().error == FLEX_OTA_ERR_CORRUPT);
    CHECK(P.written == N_IMG - 1000);
    // sin Content-Length (chunked) o fuera de limites
    world_reset();
    P.hop[0].hdr = 0;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_BAD_SIZE && P.begins == 0);
    world_reset();
    P.hop[0].hdr = 1000;
    prime_img(md5, 1000);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_BAD_SIZE && P.begins == 0);
    // no cabe
    world_reset();
    P.begin_err = ESP_FAIL;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_BEGIN && P.written == 0);
    // sin segunda particion
    world_reset();
    P.no_next = 1;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_NO_PARTITION && P.inits == 0);
    // sin memoria para TLS: ni se abre la conexion
    world_reset();
    P.heap = FLEX_OTA_MIN_HEAP - 1;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_LOWMEM && P.inits == 0);
    // sin Wi-Fi
    world_reset();
    P.net = false;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_NO_WIFI && P.inits == 0);
    // el certificado no valida (con el CA fijado, un fallo de transporte se dice TLS)
    world_reset();
    P.hop[0].open_err = ESP_ERR_HTTP_CONNECT;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_TLS && P.begins == 0);
    // la descarga redirige a http://
    world_reset();
    P.hop[0] = (Hop){0, 0, 302, ESP_FAIL};
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_TLS && P.begins == 0);
    // 404
    world_reset();
    P.hop[0].status = 404;
    prime_img(md5, N_IMG);
    CHECK(!flex_ota_run_install() && info().error == FLEX_OTA_ERR_HTTP_STATUS && P.begins == 0);
    // la comprobacion del manifiesto por el transporte de la placa
    world_reset();
    static const char MF[] = "{\"flex_os_ultra_idf\": {\"version\": \"1.2.0\", \"url\": \"https://x/fw.bin\", \"md5\": \"d41d8cd98f00b204e9800998ecf8427e\", \"size\": 100000, \"changelog\": \"Hola\"},"
                             "\"flex_os_ultra\": {\"version\": \"9.0.0\", \"url\": \"https://a/b\"}}";
    P.blen = strlen(MF);
    P.body = (const unsigned char *)MF;
    P.hop[0].hdr = (long long)strlen(MF);
    flex_ota_service_reset();
    CHECK(flex_ota_run_check());
    i = info();
    CHECK(i.state == FLEX_OTA_AVAILABLE && !strcmp(i.remote, "1.2.0") && !strcmp(i.changelog, "Hola") && i.total == 100000);
    CHECK(P.cleanups >= 1);
    // el mismo manifiesto pero la version que corre ya es esa
    world_reset();
    P.blen = strlen(MF);
    P.body = (const unsigned char *)MF;
    P.hop[0].hdr = (long long)strlen(MF);
    snprintf(P.run_desc.version, sizeof(P.run_desc.version), "1.2.0");
    {
        static flex_ota_cfg_t cfg;
        cfg = *flex_ota_port_cfg();   // la version se lee una vez: aqui se prueba el motor con otra configuracion
        cfg.local_version = "1.2.0";
        flex_ota_ops_t ops = *flex_ota_port_ops();
        flex_ota_set(&cfg, &ops);
        flex_ota_service_reset();
        CHECK(flex_ota_run_check());
        CHECK(info().state == FLEX_OTA_UP_TO_DATE);
        flex_ota_set(NULL, NULL);
    }
}

// ---- el arranque: saludo, trabajador y confirmacion (rollback) ----
static void t_start(void)
{
    world_reset();
    P.run_state = ESP_OTA_IMG_PENDING_VERIFY;   // recien instalada
    snprintf(P.pend, sizeof(P.pend), "1.0.0");   // el saludo: la version apuntada es la que corre
    flex_ota_set(NULL, NULL);
    flex_ota_service_reset();
    flex_ota_start();
    CHECK(info().just_updated && !P.pend[0]);
    CHECK(P.task_created == 1 && !strcmp(P.task_name, "flex.ota") && P.task_prio == 1 && P.task_core == 1 && P.task_stack >= 8192);
    CHECK(P.tcreated == 1 && P.tperiod == 5u * 1000u * 1000u && P.tcb != NULL);
    int stops = P.tstops;
    // aun no esta sano: ni confirma ni para el temporizador
    P.now_us = 3u * 1000u * 1000u;
    P.tcb(NULL);
    P.now_us = 14999u * 1000u;
    P.tcb(NULL);
    CHECK(P.marks == 0 && P.tstops == stops);
    // Modo seguro: jamas se confirma (el siguiente reinicio vuelve a la anterior) y se deja de preguntar
    P.safe = true;
    P.now_us = 60u * 1000u * 1000u;
    P.tcb(NULL);
    CHECK(P.marks == 0 && P.tstops == stops + 1);
    // sano a los 15 s: se confirma una vez y se para
    P.safe = false;
    P.now_us = 15000u * 1000u;
    P.mark_err = ESP_FAIL;
    P.tcb(NULL);
    CHECK(P.marks == 1 && P.tstops == stops + 1);   // no se pudo: se reintenta (el temporizador sigue)
    P.mark_err = 0;
    P.tcb(NULL);
    CHECK(P.marks == 2 && P.tstops == stops + 2);
    // ya validada (otra vez): nada que confirmar y se deja de preguntar
    P.run_state = ESP_OTA_IMG_VALID;
    P.tcb(NULL);
    CHECK(P.marks == 2 && P.tstops == stops + 3);
    // sin estado legible (arrancada por USB, sin otadata): tampoco se confirma
    P.run_state = ESP_OTA_IMG_PENDING_VERIFY;
    P.state_err = ESP_ERR_INVALID_ARG;
    P.tcb(NULL);
    CHECK(P.marks == 2 && P.tstops == stops + 4);
    // idempotente
    flex_ota_start();
    CHECK(P.task_created == 1 && P.tcreated == 1);
}

void test_ota_port(void)
{
    t_md5_stub();
    t_start();   // primero: crea el cerrojo, y el resto prueba el motor con el cerrojo de verdad
    t_cfg();
    t_http();
    t_read();
    t_flash();
    t_pending();
    t_engine();
}

int main(void)
{
    test_ota_port();
    if (g_fail) {
        printf("ota_port: %d fallos de %d\n", g_fail, g_checks);
        return 1;
    }
    printf("OK: transporte OTA (%d comprobaciones: configuracion, HTTP, flash con MD5 y proyecto, motor real, saludo y confirmacion de arranque)\n", g_checks);
    return 0;
}
