// Simulador del PC: servicios del sistema simulados para la interfaz.
//  * Ajustes: la cache real (flex_kv.c) en memoria, sin NVS.
//  * Archivos: no hay LittleFS (las operaciones devuelven "no disponible").
//  * Bus: los eventos se cuentan; el buzon ejecuta en la siguiente vuelta.
//  * Sistema: una muestra fija, marcada como SIMULADA.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_bus.h"
#include "flex_inbox.h"
#include "flex_kv.h"
#include "flex_storage.h"
#include "flex_system.h"

ESP_EVENT_DEFINE_BASE(FLEX_EV_SYSTEM);
ESP_EVENT_DEFINE_BASE(FLEX_EV_STORAGE);
ESP_EVENT_DEFINE_BASE(FLEX_EV_SETTINGS);
ESP_EVENT_DEFINE_BASE(FLEX_EV_NET);
ESP_EVENT_DEFINE_BASE(FLEX_EV_ACCOUNT);
ESP_EVENT_DEFINE_BASE(FLEX_EV_CLOUD);
ESP_EVENT_DEFINE_BASE(FLEX_EV_MEDIA);
ESP_EVENT_DEFINE_BASE(FLEX_EV_AUDIO);
ESP_EVENT_DEFINE_BASE(FLEX_EV_SENSOR);
ESP_EVENT_DEFINE_BASE(FLEX_EV_POWER);
ESP_EVENT_DEFINE_BASE(FLEX_EV_OTA);
ESP_EVENT_DEFINE_BASE(FLEX_EV_NOTIF);

static flex_kv_t s_kv;
static bool s_kv_ready;
static void kv(void)
{
    if (!s_kv_ready) {
        flex_kv_init(&s_kv, NULL, NULL, NULL);
        s_kv_ready = true;
    }
}

// ---- bus ---------------------------------------------------------------------
#define MAX_SUBS 64
static struct {
    esp_event_base_t base;
    int32_t id;
    esp_event_handler_t h;
    void *arg;
} s_subs[MAX_SUBS];
static int s_nsubs;
esp_err_t flex_bus_init(void) { return ESP_OK; }
esp_err_t flex_bus_post(esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    (void)len;
    for (int i = 0; i < s_nsubs; i++) {
        if (s_subs[i].base == base && (s_subs[i].id == id || s_subs[i].id == -1)) {
            s_subs[i].h(s_subs[i].arg, base, id, (void *)data);
        }
    }
    return ESP_OK;
}
esp_err_t flex_bus_subscribe(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg)
{
    if (s_nsubs >= MAX_SUBS) {
        return ESP_ERR_NO_MEM;
    }
    s_subs[s_nsubs].base = base;
    s_subs[s_nsubs].id = id;
    s_subs[s_nsubs].h = handler;
    s_subs[s_nsubs].arg = arg;
    s_nsubs++;
    return ESP_OK;
}
esp_err_t flex_bus_unsubscribe(esp_event_base_t base, int32_t id, esp_event_handler_t handler)
{
    for (int i = 0; i < s_nsubs; i++) {
        if (s_subs[i].base == base && s_subs[i].id == id && s_subs[i].h == handler) {
            s_subs[i] = s_subs[--s_nsubs];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
uint32_t flex_bus_dropped(void) { return 0; }

// ---- buzon -------------------------------------------------------------------
#define INBOX 64
static struct {
    flex_inbox_fn_t fn;
    void *arg;
} s_q[INBOX];
static int s_qh, s_qt;
esp_err_t flex_inbox_init(size_t depth, TaskHandle_t ui) { (void)depth; (void)ui; return ESP_OK; }
bool flex_inbox_post(flex_inbox_fn_t fn, void *arg)
{
    int n = (s_qt + 1) % INBOX;
    if (n == s_qh) {
        return false;
    }
    s_q[s_qt].fn = fn;
    s_q[s_qt].arg = arg;
    s_qt = n;
    return true;
}
size_t flex_inbox_drain(size_t max)
{
    size_t n = 0;
    while (s_qh != s_qt && n < max) {
        int i = s_qh;
        s_qh = (s_qh + 1) % INBOX;
        s_q[i].fn(s_q[i].arg);
        n++;
    }
    return n;
}
uint32_t flex_inbox_dropped(void) { return 0; }

// ---- ajustes -----------------------------------------------------------------
static int64_t gnum(const char *ns, const char *k, flex_kv_type_t t, int64_t def)
{
    kv();
    int64_t v;
    return flex_kv_get_num(&s_kv, ns, k, t, &v) ? v : def;
}
static esp_err_t snum(const char *ns, const char *k, flex_kv_type_t t, int64_t v)
{
    kv();
    flex_kv_set_num(&s_kv, ns, k, t, v);
    return ESP_OK;
}
bool flex_kvs_get_bool(const char *ns, const char *key, bool def) { return gnum(ns, key, FLEX_KV_U8, def) != 0; }
int32_t flex_kvs_get_i32(const char *ns, const char *key, int32_t def) { return (int32_t)gnum(ns, key, FLEX_KV_I32, def); }
uint32_t flex_kvs_get_u32(const char *ns, const char *key, uint32_t def) { return (uint32_t)gnum(ns, key, FLEX_KV_U32, def); }
size_t flex_kvs_get_blob(const char *ns, const char *key, void *out, size_t cap)
{
    kv();
    size_t len = 0;
    return flex_kv_get_buf(&s_kv, ns, key, FLEX_KV_BLOB, out, cap, &len) ? len : 0;
}
void flex_kvs_get_str(const char *ns, const char *key, char *out, size_t cap, const char *def)
{
    kv();
    size_t len = 0;
    if (cap && flex_kv_get_buf(&s_kv, ns, key, FLEX_KV_STR, out, cap - 1, &len)) {
        out[len < cap - 1 ? len : cap - 1] = 0;
        return;
    }
    snprintf(out, cap, "%s", def ? def : "");
}
esp_err_t flex_kvs_set_bool(const char *ns, const char *key, bool v) { return snum(ns, key, FLEX_KV_U8, v); }
esp_err_t flex_kvs_set_i32(const char *ns, const char *key, int32_t v) { return snum(ns, key, FLEX_KV_I32, v); }
esp_err_t flex_kvs_set_u32(const char *ns, const char *key, uint32_t v) { return snum(ns, key, FLEX_KV_U32, v); }
esp_err_t flex_kvs_set_blob(const char *ns, const char *key, const void *d, size_t len)
{
    kv();
    flex_kv_set_buf(&s_kv, ns, key, FLEX_KV_BLOB, d, len);
    return ESP_OK;
}
esp_err_t flex_kvs_set_str(const char *ns, const char *key, const char *v)
{
    kv();
    v = v ? v : "";
    flex_kv_set_buf(&s_kv, ns, key, FLEX_KV_STR, v, strlen(v) + 1);
    return ESP_OK;
}
bool flex_cfg_get_bool(const char *k, bool d) { return flex_kvs_get_bool(FLEX_NVS_NS, k, d); }
int32_t flex_cfg_get_i32(const char *k, int32_t d) { return flex_kvs_get_i32(FLEX_NVS_NS, k, d); }
uint32_t flex_cfg_get_u32(const char *k, uint32_t d) { return flex_kvs_get_u32(FLEX_NVS_NS, k, d); }
uint8_t flex_cfg_get_u8(const char *k, uint8_t d) { return (uint8_t)gnum(FLEX_NVS_NS, k, FLEX_KV_U8, d); }
int8_t flex_cfg_get_i8(const char *k, int8_t d) { return (int8_t)gnum(FLEX_NVS_NS, k, FLEX_KV_I8, d); }
void flex_cfg_get_str(const char *k, char *o, size_t c, const char *d) { flex_kvs_get_str(FLEX_NVS_NS, k, o, c, d); }
size_t flex_cfg_get_blob(const char *k, void *o, size_t c) { return flex_kvs_get_blob(FLEX_NVS_NS, k, o, c); }
esp_err_t flex_cfg_set_bool(const char *k, bool v) { return flex_kvs_set_bool(FLEX_NVS_NS, k, v); }
esp_err_t flex_cfg_set_i32(const char *k, int32_t v) { return flex_kvs_set_i32(FLEX_NVS_NS, k, v); }
esp_err_t flex_cfg_set_u32(const char *k, uint32_t v) { return flex_kvs_set_u32(FLEX_NVS_NS, k, v); }
esp_err_t flex_cfg_set_u8(const char *k, uint8_t v) { return snum(FLEX_NVS_NS, k, FLEX_KV_U8, v); }
esp_err_t flex_cfg_set_i8(const char *k, int8_t v) { return snum(FLEX_NVS_NS, k, FLEX_KV_I8, v); }
esp_err_t flex_cfg_set_str(const char *k, const char *v) { return flex_kvs_set_str(FLEX_NVS_NS, k, v); }
esp_err_t flex_cfg_set_blob(const char *k, const void *d, size_t l) { return flex_kvs_set_blob(FLEX_NVS_NS, k, d, l); }
esp_err_t flex_cfg_erase(const char *k) { kv(); flex_kv_erase(&s_kv, FLEX_NVS_NS, k); return ESP_OK; }
esp_err_t flex_cfg_flush(uint32_t t) { (void)t; return ESP_OK; }
esp_err_t flex_cfg_flush_async(void) { return ESP_OK; }

// ---- almacenamiento ----------------------------------------------------------
void flex_storage_get_status(flex_storage_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->nvs_ok = true;
    out->fs_mounted = true;
    out->fs_total = 0xAE0000;
    out->fs_used = 3 * 1024 * 1024;   // SIMULADO
}
esp_err_t flex_fs_write_async(const char *p, const void *d, size_t l, flex_fs_done_cb_t cb, void *u)
{
    (void)p; (void)d; (void)l;
    if (cb) cb(ESP_ERR_INVALID_STATE, u);
    return ESP_OK;
}
esp_err_t flex_fs_read_async(const char *p, size_t m, flex_fs_read_cb_t cb, void *u)
{
    (void)p; (void)m;
    if (cb) cb(ESP_ERR_NOT_FOUND, NULL, 0, u);
    return ESP_OK;
}
esp_err_t flex_fs_remove_async(const char *p, flex_fs_done_cb_t cb, void *u) { (void)p; if (cb) cb(ESP_ERR_INVALID_STATE, u); return ESP_OK; }
esp_err_t flex_fs_rename_async(const char *a, const char *b, flex_fs_done_cb_t cb, void *u) { (void)a; (void)b; if (cb) cb(ESP_ERR_INVALID_STATE, u); return ESP_OK; }
esp_err_t flex_fs_mkdir_async(const char *p, flex_fs_done_cb_t cb, void *u) { (void)p; if (cb) cb(ESP_ERR_INVALID_STATE, u); return ESP_OK; }
bool flex_fs_exists(const char *p) { (void)p; return false; }
uint32_t flex_storage_repair_token(void) { return 0x1234; }
esp_err_t flex_storage_nvs_erase_confirmed(uint32_t t) { (void)t; return ESP_ERR_INVALID_STATE; }
esp_err_t flex_storage_fs_format_confirmed(uint32_t t) { (void)t; return ESP_ERR_INVALID_STATE; }

// ---- sistema -----------------------------------------------------------------
void flex_system_get(flex_sys_snapshot_t *o)
{
    memset(o, 0, sizeof(*o));
    o->uptime_s = 3725;
    o->heap_int_free = 312 * 1024;
    o->heap_int_min = 280 * 1024;
    o->heap_int_largest = 200 * 1024;
    o->heap_psram_free = 27 * 1024 * 1024;
    o->heap_psram_min = 26 * 1024 * 1024;
    o->heap_psram_largest = 20 * 1024 * 1024;
    o->psram_total = 32 * 1024 * 1024;
    o->temp_ok = true;
    o->temp_c = 41.5f;
    o->cpu_load[0] = 12.0f;
    o->cpu_load[1] = 23.0f;
    snprintf(o->reset_reason, sizeof(o->reset_reason), "Encendido (SIMULADO)");
    o->boot_count = 7;
    o->samples = 1;
}

// ---- pantalla (retroiluminado y panel) ----------------------------------------
#include "flex_display.h"
static uint8_t s_bright = 80, s_raw = 80;
static bool s_bl_on = true, s_panel_on = true;
void flex_display_set_brightness(uint8_t pct) { s_bright = pct < 5 ? 5 : pct > 100 ? 100 : pct; }
uint8_t flex_display_get_brightness(void) { return s_bright; }
void flex_display_backlight_enable(bool on) { s_bl_on = on; s_raw = on ? s_bright : 0; }
void flex_display_backlight_raw(uint8_t pct) { s_raw = pct; s_bl_on = pct > 0; }
int sim_drawn_shell_state(void);
static int s_on_drawn = -1;   // shell en el ultimo cuadro dibujado al mandar DISPON
void flex_display_panel_on(bool on)
{
    if (on && !s_panel_on) {
        s_on_drawn = sim_drawn_shell_state();
    }
    s_panel_on = on;
}
int sim_panel_on_drawn_state(void) { return s_on_drawn; }
uint8_t sim_display_backlight(void) { return s_bl_on ? s_raw : 0; }
bool sim_display_panel_on(void) { return s_panel_on; }
static bool s_sleep_in;
void flex_display_sleep_in(void) { s_panel_on = false; s_sleep_in = true; }
bool sim_display_sleep_in(void) { return s_sleep_in; }

// ---- apagado completo: en el simulador se registra y se vuelve ------------------------
#include "flex_poweroff.h"
static int s_deep_sleeps;
void flex_poweroff_deep_sleep(void)
{
    flex_cfg_set_bool("cleanoff", true);   // lo mismo que guarda el firmware antes de dormir
    flex_cfg_set_i32("bright", flex_display_get_brightness());
    s_deep_sleeps++;
}
int sim_deep_sleeps(void) { return s_deep_sleeps; }
