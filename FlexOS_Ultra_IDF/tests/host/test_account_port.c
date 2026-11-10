// Flex Account (N3d): flex_account_port.c COMPILADO EN EL HOST contra dobles de ESP-IDF (tests/host/account_port_stub): NVS, esp_http_client, esp_tls, eFuse, memoria, tareas y
// temporizador. Se comprueba cada operacion de la placa (NVS con las mismas claves y tipos que Preferences, cada escritura confirmada; HTTP con las raices fijadas, SIEMPRE
// validado, el cuerpo entero con su tope; el motivo TLS; la tarea en el core 1 con 12 KB; la MAC como ESP.getEfuseMac) y el servicio REAL entero sobre ellas (el servidor
// firma sus respuestas): vincular, reiniciar, validar y desvincular. NO es una prueba en la placa.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account_fake.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "flex_account_port.h"
#include "flex_ntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static int g_checks, g_fails;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fails++; printf("  FALLO: "); printf(__VA_ARGS__); printf("  (%s:%d)\n", __FILE__, __LINE__); } } while (0)

// ---- las raices: el simbolo que en la placa embebe CMake ----
extern const char cloud_roots_pem_start[] asm("_binary_cloud_root_bundle_pem_start");
const char cloud_roots_pem_start[] =
#include "account_roots.inc"
    ;

// ---- dobles ----
static int64_t g_now_us = 1000000;
int64_t esp_timer_get_time(void) { return g_now_us; }
void vTaskDelay(TickType_t t) { g_now_us += (int64_t)t * 1000; account_fake_advance(t); }
const char *esp_err_to_name(int e) { (void)e; return "ERR"; }
void acct_stub_log(int level, const char *tag, const char *fmt, ...)
{
    (void)level; (void)tag; (void)fmt;
}
const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t d = {"1.0.0", "flexos_ultra_idf"};
    return &d;
}
static size_t g_free_int = 200u << 10, g_block_int = 100u << 10;
size_t heap_caps_get_free_size(unsigned caps) { return caps & MALLOC_CAP_INTERNAL ? g_free_int : 1u << 24; }
size_t heap_caps_get_largest_free_block(unsigned caps) { return caps & MALLOC_CAP_INTERNAL ? g_block_int : 1u << 22; }
esp_err_t esp_efuse_mac_get_default(uint8_t *mac)
{
    static const uint8_t m[6] = {0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1};   // ESP.getEfuseMac() = 0x0000A1B2C3D4E5F6
    memcpy(mac, m, 6);
    return ESP_OK;
}
void esp_fill_random(void *buf, size_t len)
{
    static uint32_t x = 0x1234567u;
    for (size_t i = 0; i < len; i++) {
        x = x * 1664525u + 1013904223u;
        ((uint8_t *)buf)[i] = (uint8_t)(x >> 24);
    }
}
static bool g_online = true;
bool flex_net_online(void) { return g_online; }

// FreeRTOS: el mutex NO es recursivo (un doble Take se anota: en la placa seria un bloqueo eterno)
struct acct_stub_mutex { int held; };
struct acct_stub_task { int x; };
static int g_double_take, g_mutex_creates;
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static struct acct_stub_mutex m;
    g_mutex_creates++;
    return &m;
}
int xSemaphoreTake(SemaphoreHandle_t m, TickType_t t)
{
    (void)t;
    if (m->held) g_double_take++;
    m->held = 1;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t m)
{
    m->held = 0;
    return pdTRUE;
}
static struct { int n, fail; char name[24]; uint32_t stack; unsigned prio; int core; } g_task;
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *name, uint32_t stack, void *arg, unsigned prio, TaskHandle_t *out, int core)
{
    (void)f; (void)arg;
    if (g_task.fail) {
        g_task.fail--;
        if (out) *out = NULL;
        return 0;
    }
    static struct acct_stub_task t;
    g_task.n++;
    snprintf(g_task.name, sizeof(g_task.name), "%s", name);
    g_task.stack = stack;
    g_task.prio = prio;
    g_task.core = core;
    if (out) *out = &t;
    return pdPASS;
}

// DNS: el del sistema no se toca
#include <netdb.h>
static int g_dns_rc;
int stub_getaddrinfo(const char *h, const char *s, const struct addrinfo *hints, struct addrinfo **res)
{
    (void)h; (void)s; (void)hints;
    *res = g_dns_rc ? NULL : calloc(1, sizeof(struct addrinfo));
    return g_dns_rc;
}
void stub_freeaddrinfo(struct addrinfo *r) { free(r); }

// ---- NVS en memoria con los tipos de la NVS real ----
typedef struct { char key[16]; int type; char str[96]; uint32_t num; bool used; } nv_t;
static nv_t g_nv[16];
static int g_nvs_open_fail, g_nvs_opens, g_nvs_closes, g_commits, g_sets, g_open_now;
static nv_t *nv_find(const char *k)
{
    for (int i = 0; i < 16; i++) if (g_nv[i].used && !strcmp(g_nv[i].key, k)) return &g_nv[i];
    return NULL;
}
static nv_t *nv_slot(const char *k, int type)
{
    nv_t *n = nv_find(k);
    if (!n) {
        for (int i = 0; i < 16 && !n; i++) if (!g_nv[i].used) n = &g_nv[i];
        snprintf(n->key, sizeof(n->key), "%s", k);
        n->used = true;
    }
    n->type = type;
    n->str[0] = 0;
    n->num = 0;
    return n;
}
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    (void)mode;
    if (g_nvs_open_fail || strcmp(ns, "flexacct")) return ESP_FAIL;
    g_nvs_opens++;
    g_open_now++;
    *h = 7;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; g_nvs_closes++; g_open_now--; }
esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *v) { (void)h; nv_t *n = nv_find(k); if (!n || n->type != NVS_TYPE_U32) return ESP_ERR_NVS_NOT_FOUND; *v = n->num; return ESP_OK; }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v) { (void)h; g_sets++; nv_slot(k, NVS_TYPE_U32)->num = v; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h; nv_t *n = nv_find(k); if (!n || n->type != NVS_TYPE_U8) return ESP_ERR_NVS_NOT_FOUND; *v = (uint8_t)n->num; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h; g_sets++; nv_slot(k, NVS_TYPE_U8)->num = v; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len)
{
    (void)h;
    nv_t *n = nv_find(k);
    if (!n || n->type != NVS_TYPE_STR) return ESP_ERR_NVS_NOT_FOUND;
    size_t need = strlen(n->str) + 1;
    if (!out) { *len = need; return ESP_OK; }
    if (*len < need) { *len = need; return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out, n->str, need);
    *len = need;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) { (void)h; g_sets++; snprintf(nv_slot(k, NVS_TYPE_STR)->str, 96, "%s", v); return ESP_OK; }
esp_err_t nvs_find_key(nvs_handle_t h, const char *k, nvs_type_t *t) { (void)h; nv_t *n = nv_find(k); if (!n) return ESP_ERR_NVS_NOT_FOUND; if (t) *t = (nvs_type_t)n->type; return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t h) { (void)h; memset(g_nv, 0, sizeof(g_nv)); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; g_commits++; return ESP_OK; }

// ---- esp_http_client: el servidor simulado detras ----
static struct esp_http_client { int x; } s_cli;
static char g_first_post[700];
static struct {
    esp_http_client_config_t cfg;
    bool inited, opened, closed, cleaned;
    int inits, cleanups;
    char hdr[8][2][200];
    int nh;
    char wbody[700];
    size_t wlen;
    char reply[16384];
    size_t rlen, rpos;
    int status, open_err, tls_code, write_short, force_chunked, fetch_neg;
    int64_t force_len;   // >= 0: Content-Length que dice la cabecera en lugar del real
    bool reads_called;
    char url[400];
} C;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    memset(&C.cfg, 0, sizeof(C.cfg));
    C.cfg = *c;
    snprintf(C.url, sizeof(C.url), "%s", c->url);
    C.inited = true;
    C.inits++;
    C.nh = 0;
    C.status = 0;   // cada peticion la contesta el servidor (salvo que la prueba fije un estado DESPUES de empezarla)
    C.rlen = C.rpos = 0;
    C.wlen = 0;
    C.reads_called = false;
    C.opened = C.closed = C.cleaned = false;
    return &s_cli;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v)
{
    (void)c;
    snprintf(C.hdr[C.nh][0], 200, "%s", k);
    snprintf(C.hdr[C.nh][1], 200, "%s", v);
    C.nh++;
    return ESP_OK;
}
static const char *hdr(const char *k)
{
    for (int i = 0; i < C.nh; i++) if (!strcmp(C.hdr[i][0], k)) return C.hdr[i][1];
    return NULL;
}
static struct esp_tls_error_handle { int x; } s_eh;
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h, int *code, int *flags)
{
    (void)h;
    *code = C.tls_code;
    *flags = 0;
    return C.tls_code ? ESP_OK : ESP_FAIL;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int wl)
{
    (void)c;
    (void)wl;
    if (C.open_err) {
        esp_http_client_event_t ev = {HTTP_EVENT_ERROR, &s_eh};
        if (C.cfg.event_handler) C.cfg.event_handler(&ev);
        return ESP_FAIL;
    }
    C.opened = true;
    return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t c, const char *buf, int len)
{
    (void)c;
    if (C.write_short) return len - 1;
    memcpy(C.wbody, buf, (size_t)len < sizeof(C.wbody) ? (size_t)len : sizeof(C.wbody) - 1);
    C.wbody[(size_t)len < sizeof(C.wbody) ? (size_t)len : sizeof(C.wbody) - 1] = 0;
    C.wlen = (size_t)len;
    if (!g_first_post[0] && strstr(C.wbody, "hardwareId")) snprintf(g_first_post, sizeof(g_first_post), "%s", C.wbody);
    return len;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    (void)c;
    // la peticion llega al servidor aqui (ya con su cuerpo)
    if (C.status == 0) {
        char auth[200] = "";
        const char *a = hdr("Authorization");
        if (a) snprintf(auth, sizeof(auth), "%s", a);
        C.status = account_fake_serve(C.cfg.method == HTTP_METHOD_POST ? "POST" : "GET", C.url, auth, C.wbody, C.reply, sizeof(C.reply));
        C.rlen = strlen(C.reply);
        C.rpos = 0;
    }
    if (C.fetch_neg) return -1;
    if (C.force_chunked) return -1;
    return C.force_len >= 0 ? C.force_len : (int64_t)C.rlen;
}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t c) { (void)c; return C.force_chunked != 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return C.status; }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void)c; C.closed = true; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; C.cleaned = true; C.cleanups++; return ESP_OK; }
int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len)
{
    (void)c;
    C.reads_called = true;
    size_t k = C.rlen - C.rpos < (size_t)len ? C.rlen - C.rpos : (size_t)len;
    memcpy(buf, C.reply + C.rpos, k);
    C.rpos += k;
    return (int)k;
}
static void c_reset(void)
{
    memset(&C, 0, sizeof(C));
    C.force_len = -1;
}

// ---- las pruebas ----
static const flex_account_ops_t *OPS_;

static void t_basics(void)
{
    OPS_ = flex_account_port_ops();
    CHECK(OPS_ && OPS_->millis && OPS_->http_begin && OPS_->nvs_begin, "las operaciones de la placa estan todas");
    CHECK(OPS_->millis() == 1000u, "millis = esp_timer en ms");
    g_now_us += 4294967296000ll + 5000;   // da la vuelta de 32 bits
    CHECK(OPS_->millis() == (uint32_t)((g_now_us) / 1000), "millis da la vuelta como uint32");
    CHECK(OPS_->efuse_mac() == 0x0000A1B2C3D4E5F6ull, "la MAC es ESP.getEfuseMac(): %llx", (unsigned long long)OPS_->efuse_mac());
    g_online = true;
    CHECK(OPS_->wifi_connected(), "Wi-Fi = lo que publica flex_net");
    g_online = false;
    CHECK(!OPS_->wifi_connected(), "sin Wi-Fi");
    g_online = true;
    uint8_t r1[16], r2[16];
    OPS_->fill_random(r1, 16);
    OPS_->fill_random(r2, 16);
    CHECK(memcmp(r1, r2, 16), "el aleatorio avanza");
    // memoria para el handshake: suelo de 40 KB internos y 16 KB seguidos
    size_t f = 0, b = 0;
    g_free_int = 200u << 10; g_block_int = 100u << 10;
    CHECK(OPS_->tls_room(&f, &b) && f == (200u << 10) && b == (100u << 10), "con memoria");
    g_free_int = 40u << 10; g_block_int = 16u << 10;
    CHECK(OPS_->tls_room(NULL, NULL), "justo en el suelo cabe");
    g_free_int = (40u << 10) - 1;
    CHECK(!OPS_->tls_room(&f, NULL) && f == (40u << 10) - 1, "un byte por debajo de la interna: no");
    g_free_int = 200u << 10; g_block_int = (16u << 10) - 1;
    CHECK(!OPS_->tls_room(NULL, &b) && b == (16u << 10) - 1, "un byte por debajo del bloque: no");
    g_free_int = 200u << 10; g_block_int = 100u << 10;
    g_dns_rc = 0;
    CHECK(OPS_->tls_dns_ok("flex.test"), "DNS resuelve");
    g_dns_rc = 1;
    CHECK(!OPS_->tls_dns_ok("flex.test"), "DNS no resuelve");
    g_dns_rc = 0;
}

static void t_nvs(void)
{
    memset(g_nv, 0, sizeof(g_nv));
    g_nvs_open_fail = 1;
    CHECK(!OPS_->nvs_begin(), "NVS que no abre: false");
    g_nvs_open_fail = 0;
    CHECK(OPS_->nvs_begin() && OPS_->nvs_begin(), "abrir dos veces no abre dos");
    CHECK(g_nvs_opens - g_nvs_closes == 1 && g_open_now == 1, "un solo manejador abierto");
    int c0 = g_commits;
    CHECK(OPS_->nvs_put_str("token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ") == 43, "put_str devuelve strlen");
    CHECK(g_commits == c0 + 1, "cada escritura se confirma al momento");
    CHECK(OPS_->nvs_put_str("address", "ana@flex") == 8 && OPS_->nvs_put_str("display", "") == 0, "cadena vacia: 0 bytes");
    CHECK(OPS_->nvs_put_bool("linked", true) && g_commits == c0 + 4, "put_bool confirma");
    CHECK(nv_find("linked")->type == NVS_TYPE_U8 && nv_find("linked")->num == 1, "un bool se guarda como u8 (como Preferences)");
    CHECK(nv_find("token")->type == NVS_TYPE_STR && nv_find("address")->type == NVS_TYPE_STR && nv_find("display")->type == NVS_TYPE_STR, "las tres cadenas son cadenas");
    CHECK(OPS_->nvs_put_u8("authst", 2) == 1 && nv_find("authst")->type == NVS_TYPE_U8, "authst: u8");
    CHECK(OPS_->nvs_put_u32("installId", 0xCAFEBABEu) == 4 && nv_find("installId")->type == NVS_TYPE_U32, "installId: u32");
    CHECK(OPS_->nvs_get_u32("installId", 1) == 0xCAFEBABEu && OPS_->nvs_get_u32("nada", 77) == 77, "u32: valor y por omision");
    CHECK(OPS_->nvs_get_u8("authst", 0) == 2 && OPS_->nvs_get_u8("nada", 9) == 9, "u8: valor y por omision");
    CHECK(OPS_->nvs_get_u32("authst", 5) == 5 && OPS_->nvs_get_u8("installId", 6) == 6, "otro tipo: se toma por inexistente");
    CHECK(OPS_->nvs_get_bool("linked", false) && !OPS_->nvs_get_bool("nada", false) && OPS_->nvs_get_bool("nada", true), "bool");
    char buf[64];
    memset(buf, 'x', sizeof(buf));
    CHECK(OPS_->nvs_get_str("token", buf, sizeof(buf)) == 44 && !strcmp(buf, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ"), "get_str: longitud CON terminador (44)");
    CHECK(OPS_->nvs_get_str("token", buf, 43) == 0, "sin sitio para el terminador: 0");
    CHECK(OPS_->nvs_get_str("token", buf, 44) == 44, "justo cabe");
    CHECK(OPS_->nvs_get_str("nada", buf, sizeof(buf)) == 0 && OPS_->nvs_get_str("linked", buf, sizeof(buf)) == 0, "no existe / otro tipo: 0");
    CHECK(OPS_->nvs_is_key("token") && OPS_->nvs_is_key("linked") && !OPS_->nvs_is_key("nada"), "is_key");
    int s0 = g_sets;
    OPS_->nvs_clear();
    CHECK(!OPS_->nvs_is_key("token") && !OPS_->nvs_is_key("installId") && g_sets == s0, "clear borra todo el espacio");
    OPS_->nvs_end();
    OPS_->nvs_end();
    CHECK(g_open_now == 0 && g_nvs_opens == g_nvs_closes, "nvs_end cierra (y cerrar de mas no rompe)");
}

static flex_account_req_t mkreq(const char *method, const char *url, const char *auth, const char *body, bool insecure, bool conn_to)
{
    flex_account_req_t r = {.method = method, .url = url, .auth = auth, .body = (const uint8_t *)body, .body_len = body ? strlen(body) : 0, .insecure = insecure, .timeout_ms = 15000,
                            .set_connect_timeout = conn_to};
    return r;
}
static void sign_in_world(void)   // un mundo con el servidor listo
{
    account_fake_reset();
    account_fake_set_polls(0);
}

static void t_http(void)
{
    sign_in_world();
    c_reset();
    // POST de codigo: pedido "inseguro" por el servicio (como Arduino), pero la placa valida SIEMPRE
    flex_account_req_t r = mkreq("POST", FLEX_ACCOUNT_CODE_URL, NULL, "{\"tokenHash\":\"0000000000000000000000000000000000000000000000000000000000000000\"}", true, false);
    CHECK(OPS_->http_begin(&r), "begin");
    CHECK(C.cfg.cert_pem == cloud_roots_pem_start && C.cfg.cert_pem[0] == '-', "el TLS usa las raices fijadas aunque el servicio pida inseguro");
    CHECK(C.cfg.method == HTTP_METHOD_POST && C.cfg.timeout_ms == 15000 && C.cfg.disable_auto_redirect && !C.cfg.keep_alive_enable && !C.cfg.skip_cert_common_name_check,
          "POST, 15 s, sin redirecciones, sin conexion persistente, con el nombre del servidor comprobado");
    CHECK(C.cfg.event_handler != NULL && !strcmp(C.url, FLEX_ACCOUNT_CODE_URL), "gancho de errores y URL");
    CHECK(hdr("Content-Type") && !strcmp(hdr("Content-Type"), "application/json") && !strcmp(hdr("Accept"), "application/json") && !strcmp(hdr("Connection"), "close") &&
              !hdr("Authorization"), "cabeceras del POST (sin Authorization)");
    CHECK(hdr("User-Agent") && strstr(hdr("User-Agent"), "FlexOS-Ultra") && hdr("Accept-Encoding") && !strcmp(hdr("Accept-Encoding"), "identity"), "agente y sin gzip");
    int st = OPS_->http_send();
    CHECK(st == 201 && C.wlen == strlen(r.body ? (const char *)r.body : "") && C.opened, "el POST llega al servidor con su cuerpo: %d", st);
    int sz = OPS_->http_size();
    CHECK(sz > 0 && OPS_->http_has_stream() && OPS_->http_available() == sz && OPS_->http_connected(), "cuerpo completo disponible: %d", sz);
    uint8_t chunk[100];
    int tot = 0, n;
    while ((n = OPS_->http_read(chunk, sizeof(chunk))) > 0) tot += n;
    CHECK(tot == sz && !OPS_->http_connected() && OPS_->http_available() == 0, "se lee todo y acaba");
    OPS_->http_end();
    CHECK(C.closed && C.cleaned && !OPS_->http_has_stream(), "http_end cierra y libera");

    // GET con Authorization (sin cuerpo); el CA es el mismo; el timeout de conexion no lo expone esp_http_client por separado
    c_reset();
    flex_account_req_t g = mkreq("GET", FLEX_ACCOUNT_SESSION_URL, "Bearer abc", NULL, false, true);
    CHECK(OPS_->http_begin(&g) && C.cfg.method == HTTP_METHOD_GET && C.cfg.cert_pem == cloud_roots_pem_start, "GET validado");
    CHECK(hdr("Authorization") && !strcmp(hdr("Authorization"), "Bearer abc") && !hdr("Content-Type"), "Authorization solo cuando se pide, y un GET no lleva Content-Type");
    OPS_->http_end();

    // un begin sobre otro libera el anterior (ASan lo comprueba)
    c_reset();
    OPS_->http_begin(&g);
    OPS_->http_begin(&g);
    CHECK(C.inits == 2 && C.cleanups == 1, "un begin sobre otro libera el cliente anterior (%d / %d)", C.inits, C.cleanups);
    OPS_->http_end();

    // fallos de transporte: -1 y el motivo TLS del gancho (o -1 si no hubo)
    c_reset();
    OPS_->http_begin(&r);
    C.open_err = 1;
    C.tls_code = -0x2700;
    CHECK(OPS_->http_send() == -1 && OPS_->http_tls_error() == -0x2700, "fallo de certificado: -1 y el codigo de mbedTLS");
    OPS_->http_end();
    c_reset();
    OPS_->http_begin(&r);
    C.open_err = 1;
    CHECK(OPS_->http_send() == -1 && OPS_->http_tls_error() == -1, "sin codigo TLS (DNS, TCP o tiempo): -1 como lastError() de Arduino");
    OPS_->http_end();
    c_reset();
    OPS_->http_begin(&r);
    C.write_short = 1;
    CHECK(OPS_->http_send() == -1, "el cuerpo del POST no se envia entero: fallo");
    OPS_->http_end();
    c_reset();
    OPS_->http_begin(&r);
    C.fetch_neg = 1;
    CHECK(OPS_->http_send() == -1, "cabeceras ilegibles: fallo");
    OPS_->http_end();
    CHECK(OPS_->http_send() == -1 && !OPS_->http_has_stream(), "sin begin no hay peticion");

    // el codigo TLS viejo no se arrastra a la peticion siguiente
    c_reset();
    OPS_->http_begin(&r);
    C.open_err = 1;
    C.tls_code = -0x7F00;
    OPS_->http_send();
    OPS_->http_end();
    c_reset();
    OPS_->http_begin(&r);
    C.open_err = 1;
    C.tls_code = 0;
    OPS_->http_send();
    CHECK(OPS_->http_tls_error() == -1, "cada peticion empieza sin motivo");
    OPS_->http_end();

    // topes del cuerpo: dicho > 8 KB no se lee; sin Content-Length se corta a 8 KB + 1 (el servicio ve que se pasa)
    c_reset();
    OPS_->http_begin(&g);
    C.force_len = FLEX_ACCOUNT_ENVELOPE_MAX + 1;
    snprintf(C.reply, sizeof(C.reply), "{}");
    C.status = 200;
    CHECK(OPS_->http_send() == 200 && OPS_->http_size() == (int)FLEX_ACCOUNT_ENVELOPE_MAX + 1 && !OPS_->http_has_stream() && !C.reads_called, "declarado > 8 KB: ni se lee");
    OPS_->http_end();
    c_reset();
    OPS_->http_begin(&g);
    C.force_chunked = 1;
    memset(C.reply, 'x', 10000);
    C.rlen = 10000;
    C.status = 200;
    CHECK(OPS_->http_send() == 200 && OPS_->http_size() == -1 && OPS_->http_has_stream() && OPS_->http_available() == (int)FLEX_ACCOUNT_ENVELOPE_MAX + 1, "sin Content-Length: 8 KB + 1");
    OPS_->http_end();
    c_reset();
    OPS_->http_begin(&g);
    C.force_len = FLEX_ACCOUNT_ENVELOPE_MAX;
    memset(C.reply, 'y', FLEX_ACCOUNT_ENVELOPE_MAX);
    C.rlen = FLEX_ACCOUNT_ENVELOPE_MAX;
    C.status = 200;
    CHECK(OPS_->http_send() == 200 && OPS_->http_size() == (int)FLEX_ACCOUNT_ENVELOPE_MAX && OPS_->http_available() == (int)FLEX_ACCOUNT_ENVELOPE_MAX, "justo 8 KB: se lee todo");
    OPS_->http_end();
    // 4xx / 5xx / cuerpo vacio: el estado llega tal cual
    c_reset();
    OPS_->http_begin(&g);
    C.status = 404;
    C.force_len = 0;
    CHECK(OPS_->http_send() == 404 && OPS_->http_size() == 0, "404 sin cuerpo");
    OPS_->http_end();
}

static void t_task(void)
{
    memset(&g_task, 0, sizeof(g_task));
    g_task.fail = 1;
    CHECK(!OPS_->ensure_task() && g_task.n == 0, "sin memoria para la pila: false");
    CHECK(OPS_->ensure_task() && g_task.n == 1, "al reintentar nace");
    CHECK(!strcmp(g_task.name, "flex-account") && g_task.stack == 12288 && g_task.core == 1 && g_task.prio == 1, "en el core 1, con 12 KB, prioridad 1 (como Arduino)");
    CHECK(OPS_->ensure_task() && g_task.n == 1, "ya existe: no se crea otra");
}

static void t_lock(void)
{
    g_double_take = 0;
    OPS_->lock();
    OPS_->unlock();
    OPS_->lock();
    OPS_->unlock();
    CHECK(g_double_take == 0 && g_mutex_creates == 1, "el cerrojo se crea una vez y se toma / suelta sin anidar");
}

static void nvs_has(const char *k, int type, const char *what)
{
    nv_t *n = nv_find(k);
    CHECK(n && n->type == type, "%s", what);
}

static void t_service(void)
{
    // el servicio REAL sobre las operaciones de la placa
    memset(g_nv, 0, sizeof(g_nv));
    account_fake_reset();
    account_fake_set_polls(1);
    g_online = true;
    account_fake_set_wifi(true);
    c_reset();
    flex_account_start();
    CHECK(flex_account_ops() && !memcmp(flex_account_ops(), flex_account_port_ops(), sizeof(flex_account_ops_t)), "flex_account_start pone las operaciones de la placa");
    flex_account_snapshot_t s;
    flex_account_snapshot(&s);
    CHECK(s.state == FLEX_ACCOUNT_UNLINKED && s.link == FLEX_LINK_UNLINKED, "primer arranque: sin cuenta");
    CHECK(nv_find("installId") && nv_find("installId")->type == NVS_TYPE_U32 && nv_find("installId")->num != 0, "se crea un id de instalacion (u32)");
    CHECK(flex_account_request_code("FlexOS de Ana"), "se acepta la peticion");
    flex_account_step();
    flex_account_snapshot(&s);
    CHECK(s.state == FLEX_ACCOUNT_LINKED && s.linked && !strcmp(s.flex_address, "ana.p4@flex"), "vinculada: estado %d", (int)s.state);
    // lo guardado, con las claves y los tipos de Preferences
    nvs_has("token", NVS_TYPE_STR, "token: cadena");
    nvs_has("address", NVS_TYPE_STR, "address: cadena");
    nvs_has("display", NVS_TYPE_STR, "display: cadena");
    nvs_has("linked", NVS_TYPE_U8, "linked: u8");
    CHECK(strlen(nv_find("token")->str) == 43 && !strcmp(nv_find("address")->str, "ana.p4@flex") && nv_find("linked")->num == 1, "valores guardados");
    CHECK(g_open_now == 0, "la NVS queda cerrada");
    // la peticion del codigo iba con TLS validado (nunca inseguro) y llevaba el id de este aparato; la credencial no viajo
    CHECK(account_fake_count(AF_POSTS) == 1 && account_fake_count(AF_POLLS) == 2, "una peticion de codigo y dos consultas");
    char bearer[64];
    CHECK(flex_account_copy_bearer(bearer, sizeof(bearer)) && strlen(bearer) == 43, "credencial de 43");
    CHECK(strstr(g_first_post, "\"hardwareId\":\"P4-A1B2C3D4E5F6-") && strstr(g_first_post, "\"flexVersion\":\"1.0.0\"") && strstr(g_first_post, "\"model\":\"ESP32-P4\"") &&
              !strstr(g_first_post, bearer), "el POST lleva el id del aparato (MAC de eFuse), la version del firmware y NO la credencial");

    // reiniciar (se pierde la RAM, la NVS sobrevive): sigue vinculada y se valida con Bearer y TLS verificado
    flex_account_test_power_cycle();
    flex_account_snapshot(&s);
    CHECK(s.linked && s.link == FLEX_LINK_LINKED_OFFLINE && !strcmp(s.flex_address, "ana.p4@flex"), "tras reiniciar: vinculada, pendiente de comprobar");
    int sess0 = account_fake_count(AF_SESSIONS);
    for (int i = 0; i < 60; i++) {
        flex_account_step();
        vTaskDelay(100);
    }
    flex_account_snapshot(&s);
    CHECK(account_fake_count(AF_SESSIONS) == sess0 + 1 && s.link == FLEX_LINK_LINKED, "una validacion y LINKED");
    CHECK(C.cfg.cert_pem == cloud_roots_pem_start && C.cfg.method == HTTP_METHOD_GET && C.inits > 0, "la validacion lleva TLS verificado");

    // desvincular: la NVS se borra entera y queda un id nuevo
    uint32_t old_id = nv_find("installId")->num;
    flex_account_forget_local();
    CHECK(!nv_find("token") && !nv_find("address") && !nv_find("linked") && !flex_account_linked(), "desvinculada: sin credencial en la NVS");
    CHECK(nv_find("installId") && nv_find("installId")->num != old_id && nv_find("installId")->num != 0, "id de instalacion nuevo");
    CHECK(g_open_now == 0 && g_double_take == 0, "sin manejadores abiertos ni cerrojos anidados");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    account_fake_reset();
    t_basics();
    t_nvs();
    t_http();
    t_task();
    t_lock();
    t_service();
    CHECK(C.inits == 0 || C.cleanups <= C.inits, "(sin fugas de clientes)");
    if (g_fails) {
        printf("FALLOS: %d de %d (Flex Account, puerto de la placa)\n", g_fails, g_checks);
        return 1;
    }
    printf("OK: Flex Account, puerto de la placa (%d comprobaciones, con dobles de ESP-IDF)\n", g_checks);
    return 0;
}
