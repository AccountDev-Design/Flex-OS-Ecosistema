// flex_account_ops_t sobre el mundo simulado del Arduino (tests/host/netstub): la MISMA red, NVS, reloj, memoria y tareas con las que se prueba FlexOS_Account.cpp. Asi el
// servicio en C y la referencia corren, paso a paso, sobre el mismo mundo.
#include <string.h>
#include "account_ops_netstub.h"
#include "netstub.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

void ref_fill_random(void *buf, size_t len);   // ref_account_main.cpp: el aleatorio lo fija la prueba (misma secuencia en las dos implementaciones)
const char *flexCloudRootCA();                  // FlexOS_CloudTLS.cpp: las raices de confianza de Flex Cloud

namespace {
Preferences g_pref;
WiFiClientSecure g_sec;
HTTPClient g_http;
SemaphoreHandle_t g_mutex;
TaskHandle_t g_task;
bool g_post;
const uint8_t *g_body;
size_t g_body_len;

void task_stub(void *) {}

uint32_t o_millis(void) { return (uint32_t)millis(); }
void o_delay(uint32_t ms) { vTaskDelay(ms); }   // el reloj virtual avanza y la prueba ve el estado (gNetOnDelay), como en el Arduino
bool o_wifi(void) { return WiFi.status() == WL_CONNECTED; }
void o_random(void *b, size_t n) { ref_fill_random(b, n); }
uint64_t o_mac(void) { return ESP.getEfuseMac(); }
bool o_nvs_begin(void) { return g_pref.begin("flexacct", false); }
void o_nvs_end(void) { g_pref.end(); }
uint32_t o_get_u32(const char *k, uint32_t d) { return g_pref.getUInt(k, d); }
size_t o_put_u32(const char *k, uint32_t v) { return g_pref.putUInt(k, v); }
size_t o_get_str(const char *k, char *out, size_t cap) { return g_pref.getString(k, out, cap); }
size_t o_put_str(const char *k, const char *v) { return g_pref.putString(k, v); }
bool o_get_bool(const char *k, bool d) { return g_pref.getBool(k, d); }
bool o_put_bool(const char *k, bool v) { return g_pref.putBool(k, v) > 0; }
uint8_t o_get_u8(const char *k, uint8_t d) { return g_pref.getUChar(k, d); }
size_t o_put_u8(const char *k, uint8_t v) { return g_pref.putUChar(k, v); }
bool o_is_key(const char *k) { return g_pref.isKey(k); }
void o_clear(void) { g_pref.clear(); }

bool o_tls_room(size_t *f, size_t *b)
{
    if (f) {
        *f = gNetInternalFree;
    }
    if (b) {
        *b = gNetInternalBlock;
    }
    // el mismo suelo que la placa: por debajo, el modo de proteccion; y la pila de mbedTLS pide un bloque contiguo
    return gNetInternalFree >= 40u * 1024u && gNetInternalBlock >= 16u * 1024u;
}
bool o_dns(const char *) { return gNetDnsOk; }

bool o_http_begin(const flex_account_req_t *r)
{
    g_sec = WiFiClientSecure();
    if (r->insecure) {
        g_sec.setInsecure();
    } else {
        g_sec.setCACert(flexCloudRootCA());
    }
    g_sec.setHandshakeTimeout(12);
    g_http = HTTPClient();
    g_http.setTimeout((uint16_t)r->timeout_ms);
    if (r->set_connect_timeout) {
        g_http.setConnectTimeout((int32_t)r->timeout_ms);
    }
    g_http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    g_http.useHTTP10(true);
    g_http.setUserAgent("FlexOS-Ultra/1.0 ESP32-P4");
    if (!g_http.begin(g_sec, r->url)) {
        return false;
    }
    g_post = !strcmp(r->method, "POST");
    if (g_post) {
        g_http.addHeader("Content-Type", "application/json");
    }
    if (r->auth) {
        g_http.addHeader("Authorization", r->auth);
    }
    g_http.addHeader("Accept", "application/json");
    g_http.addHeader("Connection", "close");
    g_body = r->body;
    g_body_len = r->body_len;
    return true;
}
int o_http_send(void) { return g_post ? g_http.POST((uint8_t *)g_body, g_body_len) : g_http.GET(); }
int o_http_tls_error(void) { return g_sec.lastError(nullptr, 0); }
int o_http_size(void) { return g_http.getSize(); }
bool o_http_has_stream(void) { return g_http.getStreamPtr() != nullptr; }
int o_http_available(void)
{
    WiFiClient *s = g_http.getStreamPtr();
    return s ? s->available() : 0;
}
int o_http_read(uint8_t *b, size_t n)
{
    WiFiClient *s = g_http.getStreamPtr();
    return s ? (int)s->readBytes(b, n) : -1;
}
bool o_http_connected(void) { return g_http.connected(); }
void o_http_end(void) { g_http.end(); }

// Como ensureTask de Arduino: la tarea nace en el core 1 con 12 KB de pila; si no hay memoria seguida, falla
bool o_ensure_task(void)
{
    if (g_task) {
        return true;
    }
    BaseType_t rc = xTaskCreatePinnedToCore(task_stub, "flex-account", 12288, nullptr, 1, &g_task, 1);
    if (rc == pdPASS) {
        return true;
    }
    g_task = nullptr;
    return false;
}
void o_lock(void)
{
    if (!g_mutex) {
        g_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void o_unlock(void) { xSemaphoreGive(g_mutex); }

const flex_account_ops_t k_ops = {
    o_millis,      o_delay,     o_wifi,      o_random,    o_mac,        o_nvs_begin,  o_nvs_end,   o_get_u32,       o_put_u32,       o_get_str,
    o_put_str,     o_get_bool,  o_put_bool,  o_get_u8,    o_put_u8,     o_is_key,     o_clear,     o_tls_room,      o_dns,           o_http_begin,
    o_http_send,   o_http_tls_error, o_http_size, o_http_has_stream, o_http_available, o_http_read, o_http_connected, o_http_end, o_ensure_task, o_lock,
    o_unlock,      nullptr,
};
}   // namespace

const flex_account_ops_t *account_ops_netstub(void) { return &k_ops; }
void account_ops_netstub_power_cycle(void) { g_task = nullptr; }   // las tareas se pierden con la RAM
