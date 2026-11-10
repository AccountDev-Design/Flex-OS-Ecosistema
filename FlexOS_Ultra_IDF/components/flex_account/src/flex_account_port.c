// Flex OS Ultra · Flex Account: lo de la placa. NO PROBADO EN HARDWARE REAL: compila contra ESP-IDF 5.5, pero nadie ha vinculado una cuenta en una ESP32-P4 con esto. El nucleo
// (flex_account_model.c) y el servicio (flex_account.c) se prueban en el host contra FlexOS_Account.cpp de Arduino; aqui solo hay transporte, almacenamiento y tarea.
//
// Diferencias con Arduino (todas hacia mas estricto o hacia lo nativo):
//   1. TLS SIEMPRE con las raices de Flex Cloud (certs/cloud_root_bundle.pem de flex_net, las mismas que usa Arduino para validar la sesion). Arduino pedia el codigo de
//      enlace con setInsecure() y se apoyaba en la firma ES256 de la respuesta; aqui tampoco existe el TLS sin verificar (como en la OTA), asi que enlazar necesita la hora
//      de red ajustada (un certificado "aun no valido" a causa de un reloj en 1970 se dice como fallo de certificado y "Reintentar" funciona en cuanto hay hora).
//   2. El cuerpo de la respuesta se lee entero (como mucho 8 KB + 1) dentro de http_send: el servicio lo consume de memoria. Lo observable es lo mismo; la cancelacion del
//      enlace se atiende entre peticiones (como mucho el plazo de 15 s de una).
// Lo que se conserva: NVS "flexacct" con las mismas claves y tipos que Preferences de Arduino (token / address / display como cadenas, linked / authst como u8, installId
// como u32), asi una placa que cambia de firmware conserva su cuenta; cada escritura se confirma al momento (nvs_commit), en el orden en que la pide el servicio; la tarea
// nace en el core 1 con 12 KB de pila; y el identificador del aparato sale de la MAC de eFuse con el mismo formato.
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "flex_account_port.h"
#include "flex_ntp.h"   // flex_net_online(): el estado publicado por el Wi-Fi; nunca se consulta la radio desde aqui
#include "flex_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "flex.account";
#define NS_ACCOUNT "flexacct"
#define TASK_STACK 12288
#define BODY_CAP (FLEX_ACCOUNT_ENVELOPE_MAX + 1)   // un byte de mas: el servicio ve que la respuesta se pasa de 8 KB

#ifdef FLEX_ACCOUNT_HAVE_ROOTS
extern const char cloud_roots_pem_start[] asm("_binary_cloud_root_bundle_pem_start");
#define CLOUD_ROOTS cloud_roots_pem_start
#else
#define CLOUD_ROOTS ""   // sin raices no se conecta (la placa no tiene TLS sin verificar)
#endif

static SemaphoreHandle_t s_mtx;
static TaskHandle_t s_task;

// ---- tiempo, red, memoria ----
static uint32_t p_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void p_delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
static bool p_wifi(void) { return flex_net_online(); }
static void p_random(void *b, size_t n) { esp_fill_random(b, n); }
// ESP.getEfuseMac() de Arduino: los 6 bytes de la MAC base en un uint64 con el primero como byte bajo
static uint64_t p_mac(void)
{
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    uint64_t v = 0;
    for (int i = 5; i >= 0; i--) {
        v = (v << 8) | mac[i];
    }
    return v;
}
static bool p_tls_room(size_t *f, size_t *b)
{
    size_t fr = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), bl = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);   // mbedTLS reserva sus buffers de registro en la interna
    if (f) {
        *f = fr;
    }
    if (b) {
        *b = bl;
    }
    return flex_tls_room_ok(fr, bl);
}
static bool p_dns_ok(const char *host)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res = NULL;
    int rc = getaddrinfo(host, NULL, &hints, &res);
    if (res) {
        freeaddrinfo(res);
    }
    return rc == 0;
}

// ---- NVS "flexacct": como Preferences de Arduino, con cada escritura confirmada al momento ----
static nvs_handle_t s_nvs;
static bool s_nvs_open;
static bool p_nvs_begin(void)
{
    if (s_nvs_open) {
        return true;
    }
    if (nvs_open(NS_ACCOUNT, NVS_READWRITE, &s_nvs) != ESP_OK) {
        return false;
    }
    s_nvs_open = true;
    return true;
}
static void p_nvs_end(void)
{
    if (s_nvs_open) {
        nvs_close(s_nvs);
        s_nvs_open = false;
    }
}
static uint32_t p_get_u32(const char *k, uint32_t d)
{
    uint32_t v = d;
    return nvs_get_u32(s_nvs, k, &v) == ESP_OK ? v : d;
}
static size_t p_put_u32(const char *k, uint32_t v) { return nvs_set_u32(s_nvs, k, v) == ESP_OK && nvs_commit(s_nvs) == ESP_OK ? sizeof(v) : 0; }
// nvs_get_str: longitud CON terminador, y 0 si no existe o no cabe (el servicio la corrige con flex_nvs_str_len, como en Arduino)
static size_t p_get_str(const char *k, char *out, size_t cap)
{
    size_t len = cap;
    return nvs_get_str(s_nvs, k, out, &len) == ESP_OK ? len : 0;
}
static size_t p_put_str(const char *k, const char *v) { return nvs_set_str(s_nvs, k, v) == ESP_OK && nvs_commit(s_nvs) == ESP_OK ? strlen(v) : 0; }
static uint8_t p_get_u8(const char *k, uint8_t d)
{
    uint8_t v = d;
    return nvs_get_u8(s_nvs, k, &v) == ESP_OK ? v : d;
}
static size_t p_put_u8(const char *k, uint8_t v) { return nvs_set_u8(s_nvs, k, v) == ESP_OK && nvs_commit(s_nvs) == ESP_OK ? 1 : 0; }
static bool p_get_bool(const char *k, bool d) { return p_get_u8(k, d ? 1 : 0) != 0; }   // Preferences guarda un bool como u8
static bool p_put_bool(const char *k, bool v) { return p_put_u8(k, v ? 1 : 0) == 1; }
static bool p_is_key(const char *k)
{
    nvs_type_t t;
    return nvs_find_key(s_nvs, k, &t) == ESP_OK;
}
static void p_clear(void)
{
    nvs_erase_all(s_nvs);
    nvs_commit(s_nvs);
}

// ---- HTTP (esp_http_client, una conexion cada vez; el cuerpo se lee entero en http_send) ----
static struct {
    esp_http_client_handle_t cli;
    uint8_t *body;
    size_t len, pos;
    int declared, tls_err;
    bool opened;
    const uint8_t *req_body;
    size_t req_len;
} H;

static void h_close(void)
{
    if (H.cli) {
        esp_http_client_close(H.cli);
        esp_http_client_cleanup(H.cli);
    }
    free(H.body);
    memset(&H, 0, sizeof(H));
}

// El codigo de mbedTLS de la ultima conexion fallida (0 / -1 = DNS, TCP o tiempo agotado: como lastError() de arduino-esp32)
static esp_err_t h_event(esp_http_client_event_t *ev)
{
    if (ev->event_id == HTTP_EVENT_ERROR && ev->data) {
        int code = 0, flags = 0;
        esp_tls_error_handle_t eh = (esp_tls_error_handle_t)ev->data;
        if (esp_tls_get_and_clear_last_error(eh, &code, &flags) != ESP_OK) {
            code = 0;
        }
        if (code != 0) {
            H.tls_err = code;
        }
    }
    return ESP_OK;
}

static bool p_http_begin(const flex_account_req_t *r)
{
    h_close();
    if (!CLOUD_ROOTS[0]) {
        return false;   // sin las raices no hay con que validar
    }
    esp_http_client_config_t cfg = {
        .url = r->url,
        .cert_pem = CLOUD_ROOTS,
        .method = !strcmp(r->method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = (int)r->timeout_ms,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .event_handler = h_event,
        .keep_alive_enable = false,
        .disable_auto_redirect = true,   // una redireccion no es una respuesta del servicio de cuentas
        .skip_cert_common_name_check = false,
    };
    H.cli = esp_http_client_init(&cfg);
    if (!H.cli) {
        return false;
    }
    esp_http_client_set_header(H.cli, "Accept", "application/json");
    esp_http_client_set_header(H.cli, "Connection", "close");
    esp_http_client_set_header(H.cli, "Accept-Encoding", "identity");
    esp_http_client_set_header(H.cli, "User-Agent", "FlexOS-Ultra/1.0 ESP32-P4");
    if (!strcmp(r->method, "POST")) {
        esp_http_client_set_header(H.cli, "Content-Type", "application/json");
    }
    if (r->auth) {
        esp_http_client_set_header(H.cli, "Authorization", r->auth);
    }
    H.req_body = r->body;
    H.req_len = r->body_len;
    return true;
}

static int p_http_send(void)
{
    if (!H.cli) {
        return -1;
    }
    H.tls_err = 0;
    esp_err_t e = esp_http_client_open(H.cli, (int)H.req_len);
    if (e != ESP_OK) {
        return -1;
    }
    H.opened = true;
    if (H.req_len && esp_http_client_write(H.cli, (const char *)H.req_body, (int)H.req_len) != (int)H.req_len) {
        return -1;
    }
    int64_t cl = esp_http_client_fetch_headers(H.cli);
    if (cl < 0 && cl != -ESP_ERR_HTTP_EAGAIN && !esp_http_client_is_chunked_response(H.cli)) {
        return -1;
    }
    int status = esp_http_client_get_status_code(H.cli);
    H.declared = esp_http_client_is_chunked_response(H.cli) || cl < 0 ? -1 : (cl > 0x7fffffff ? 0x7fffffff : (int)cl);
    // Cuerpo entero (BODY_CAP como mucho): el servicio lo consume de memoria
    if (H.declared <= (int)FLEX_ACCOUNT_ENVELOPE_MAX) {
        H.body = malloc(BODY_CAP);
        if (H.body) {
            size_t used = 0;
            while (used < BODY_CAP) {
                int n = esp_http_client_read(H.cli, (char *)H.body + used, (int)(BODY_CAP - used));
                if (n <= 0) {
                    break;
                }
                used += (size_t)n;
            }
            H.len = used;
        }
    }
    return status;
}
static int p_http_tls_error(void) { return H.tls_err ? H.tls_err : -1; }
static int p_http_size(void) { return H.declared; }
static bool p_http_has_stream(void) { return H.cli && H.opened && H.body; }
static int p_http_available(void) { return (int)(H.len - H.pos); }
static int p_http_read(uint8_t *b, size_t n)
{
    size_t k = H.len - H.pos < n ? H.len - H.pos : n;
    memcpy(b, H.body + H.pos, k);
    H.pos += k;
    return (int)k;
}
static bool p_http_connected(void) { return H.pos < H.len; }
static void p_http_end(void) { h_close(); }

// ---- la tarea de fondo (core 1, 12 KB de pila; el 0 es del presentador grafico) ----
static void account_task(void *arg)
{
    (void)arg;
    for (;;) {
        flex_account_step();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
static bool p_ensure_task(void)
{
    if (s_task) {
        return true;
    }
    if (xTaskCreatePinnedToCore(account_task, "flex-account", TASK_STACK, NULL, 1, &s_task, 1) == pdPASS) {
        return true;
    }
    s_task = NULL;
    ESP_LOGW(TAG, "no se pudo crear la tarea: SRAM interna %u KB, mayor bloque %u KB (la pila pide 12 KB seguidos)", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024u));
    return false;
}
static void p_lock(void)
{
    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
    }
    if (s_mtx) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
    }
}
static void p_unlock(void)
{
    if (s_mtx) {
        xSemaphoreGive(s_mtx);
    }
}
static void p_log(const char *msg) { ESP_LOGI(TAG, "%s", msg); }   // el servicio nunca le pasa la credencial

const flex_account_cfg_t *flex_account_port_cfg(void)
{
    static flex_account_cfg_t c;
    if (!c.fw_version) {
        c.fw_version = esp_app_get_description()->version;   // PROJECT_VER: "1.0.0"
    }
    return &c;
}

const flex_account_ops_t *flex_account_port_ops(void)
{
    static const flex_account_ops_t ops = {
        .millis = p_millis, .delay_ms = p_delay, .wifi_connected = p_wifi, .fill_random = p_random, .efuse_mac = p_mac,
        .nvs_begin = p_nvs_begin, .nvs_end = p_nvs_end, .nvs_get_u32 = p_get_u32, .nvs_put_u32 = p_put_u32, .nvs_get_str = p_get_str, .nvs_put_str = p_put_str,
        .nvs_get_bool = p_get_bool, .nvs_put_bool = p_put_bool, .nvs_get_u8 = p_get_u8, .nvs_put_u8 = p_put_u8, .nvs_is_key = p_is_key, .nvs_clear = p_clear,
        .tls_room = p_tls_room, .tls_dns_ok = p_dns_ok, .http_begin = p_http_begin, .http_send = p_http_send, .http_tls_error = p_http_tls_error, .http_size = p_http_size,
        .http_has_stream = p_http_has_stream, .http_available = p_http_available, .http_read = p_http_read, .http_connected = p_http_connected, .http_end = p_http_end,
        .ensure_task = p_ensure_task, .lock = p_lock, .unlock = p_unlock, .log = p_log,
    };
    return &ops;
}

void flex_account_start(void)
{
    flex_account_set(flex_account_port_cfg(), flex_account_port_ops());
    flex_account_begin();
}
