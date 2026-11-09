// Simulador del PC: servicios del sistema simulados para la interfaz.
//  * Ajustes: la cache real (flex_kv.c) en memoria, sin NVS.
//  * Archivos: no hay LittleFS (las operaciones devuelven "no disponible").
//  * Bus: los eventos se cuentan; el buzon ejecuta en la siguiente vuelta.
//  * Sistema: una muestra fija, marcada como SIMULADA.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "flex_bus.h"
#include "flex_fs_names.h"
#include "flex_fs_ops.h"
#include "flex_fs_path.h"
#include "flex_i2c.h"
#include "flex_inbox.h"
#include "flex_kv.h"
#include "flex_metrics.h"
#include "flex_ntp.h"
#include "flex_storage.h"
#include "flex_system.h"
#include "flex_touch.h"

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
bool flex_inbox_in_ui(void) { return true; }   // el simulador es una sola tarea

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
uint64_t flex_kvs_get_u64(const char *ns, const char *key, uint64_t def) { return (uint64_t)gnum(ns, key, FLEX_KV_U64, (int64_t)def); }
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
esp_err_t flex_kvs_set_u64(const char *ns, const char *key, uint64_t v) { return snum(ns, key, FLEX_KV_U64, (int64_t)v); }
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
static bool s_sim_mounted = true;
void sim_fs_mounted(bool on) { s_sim_mounted = on; }   // la particion de datos monta o no (escenas de "sin almacenamiento")
void flex_storage_get_status(flex_storage_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->nvs_ok = true;
    out->fs_mounted = s_sim_mounted;
    out->fs_err = s_sim_mounted ? ESP_OK : ESP_ERR_NOT_FOUND;
    out->fs_total = 0xAE0000;
    out->fs_used = 3 * 1024 * 1024;   // SIMULADO
}
// Archivos en memoria (solo si la escena los activa con sim_fs_enable): para las sesiones de app
static struct { char path[96]; uint8_t data[1100]; size_t len; bool used; } s_mf[16];
static bool s_mf_on, s_mf_fail, s_mf_late_fail;
static int s_mf_writes;
// Callbacks diferidos: la escritura se pone en cola y su resultado llega despues (como con la tarea de almacenamiento)
static bool s_mf_defer;
static struct { flex_fs_done_cb_t cb; void *u; } s_mf_pend[8];
static int s_mf_npend;
void sim_fs_defer(bool on) { s_mf_defer = on; }
void sim_fs_complete(bool fail)
{
    int n = s_mf_npend;
    s_mf_npend = 0;
    for (int i = 0; i < n; i++) {
        if (s_mf_pend[i].cb) s_mf_pend[i].cb(fail ? ESP_FAIL : ESP_OK, s_mf_pend[i].u);
    }
}
void sim_fs_enable(bool on) { s_mf_on = on; if (!on) memset(s_mf, 0, sizeof(s_mf)); s_mf_writes = 0; }
void sim_fs_fail_writes(bool queue_fail, bool disk_fail) { s_mf_fail = queue_fail; s_mf_late_fail = disk_fail; }
int sim_fs_writes(void) { return s_mf_writes; }
size_t sim_fs_get(const char *p, uint8_t *out, size_t cap)
{
    for (int i = 0; i < 16; i++) if (s_mf[i].used && !strcmp(s_mf[i].path, p)) { size_t n = s_mf[i].len < cap ? s_mf[i].len : cap; memcpy(out, s_mf[i].data, n); return n; }
    return 0;
}
void sim_fs_put(const char *p, const uint8_t *d, size_t l)
{
    for (int i = 0; i < 16; i++) if (!s_mf[i].used || !strcmp(s_mf[i].path, p)) { snprintf(s_mf[i].path, sizeof(s_mf[i].path), "%s", p); memcpy(s_mf[i].data, d, l); s_mf[i].len = l; s_mf[i].used = true; return; }
}

// ---- Disco real (directorio temporal) -----------------------------------------------------------------
// Las escenas que prueban el kit de archivos trabajan con las MISMAS operaciones que la tarea de
// almacenamiento del firmware (flex_fs_ops.c, flex_fs_names.c) sobre un directorio de verdad, y los avisos
// llegan por el buzon (en la siguiente vuelta), como los de la tarea de almacenamiento. Con el disco apagado
// rige el modo en memoria de arriba. Las perillas sim_fs_fail_writes valen tambien aqui.
static char s_disk[64];
static bool s_disk_on;
static void disk_cleanup(void)
{
    if (s_disk[0]) {
        bool f;
        flex_fs_wipe_tree(s_disk, &f);
        rmdir(s_disk);
    }
}
static bool disk_abs(const char *rel, char *out, size_t n)
{
    return s_disk_on && flex_fs_path_valid(rel, 0, 0, 160) && snprintf(out, n, "%s%s", s_disk, rel) < (int)n;
}
void sim_disk_enable(bool on)
{
    if (on && !s_disk[0]) {
        snprintf(s_disk, sizeof(s_disk), "/tmp/flexsim.XXXXXX");
        if (!mkdtemp(s_disk)) {
            s_disk[0] = 0;
        } else {
            atexit(disk_cleanup);
        }
    }
    if (s_disk[0]) {
        bool f;
        flex_fs_wipe_tree(s_disk, &f);   // arranca vacio
    }
    s_disk_on = on && s_disk[0];
    s_mf_writes = 0;
}
bool sim_disk_active(void) { return s_disk_on; }
static void disk_mkdirs(char *abs)   // carpetas de la ruta (sin el ultimo componente)
{
    for (char *q = abs + strlen(s_disk) + 1; *q; q++) {
        if (*q == '/') {
            *q = 0;
            mkdir(abs, 0775);
            *q = '/';
        }
    }
}
bool sim_disk_put(const char *rel, const void *d, size_t l)
{
    char a[200];
    if (!disk_abs(rel, a, sizeof(a))) return false;
    disk_mkdirs(a);
    FILE *f = fopen(a, "wb");
    if (!f) return false;
    bool ok = l == 0 || fwrite(d, 1, l, f) == l;
    return fclose(f) == 0 && ok;
}
size_t sim_disk_get(const char *rel, void *out, size_t cap)
{
    char a[200];
    if (!disk_abs(rel, a, sizeof(a))) return (size_t)-1;
    FILE *f = fopen(a, "rb");
    if (!f) return (size_t)-1;
    size_t n = fread(out, 1, cap, f);
    fclose(f);
    return n;
}
bool sim_disk_exists(const char *rel)
{
    char a[200];
    struct stat st;
    return disk_abs(rel, a, sizeof(a)) && stat(a, &st) == 0;
}
bool sim_disk_is_dir(const char *rel)
{
    char a[200];
    struct stat st;
    return disk_abs(rel, a, sizeof(a)) && stat(a, &st) == 0 && S_ISDIR(st.st_mode);
}
bool sim_disk_mkdir(const char *rel)
{
    char a[200];
    if (!disk_abs(rel, a, sizeof(a))) return false;
    disk_mkdirs(a);
    return mkdir(a, 0775) == 0 || errno == EEXIST;
}

typedef struct {
    int kind;   // 0 hecho, 1 lectura, 2 cuenta, 3 listado, 4 nombre, 5 estadistica, 6 carpeta, 7 medida de Almacenamiento
    void *cb, *user;
    esp_err_t err;
    uint8_t *data;
    size_t len;
    uint32_t a, b;
} dpost_t;
static void dpost_run(void *arg)
{
    dpost_t *p = arg;
    if (p->cb) {
        switch (p->kind) {
        case 1: ((flex_fs_read_cb_t)p->cb)(p->err, p->data, p->len, p->user); p->data = NULL; break;   // la lectura se queda con el buffer
        case 2: ((flex_fs_count_cb_t)p->cb)(p->err, p->len, p->user); break;
        case 3: ((flex_fs_list_cb_t)p->cb)(p->err, (const flex_fs_ent_t *)p->data, p->len, p->user); break;
        case 4: ((flex_fs_name_cb_t)p->cb)(p->err, p->data ? (const char *)p->data : "", p->user); break;
        case 5: ((flex_fs_stat_cb_t)p->cb)(p->err, p->a, p->b, p->user); break;
        case 6: ((flex_fs_explore_cb_t)p->cb)(p->err, (const flex_fsx_ent_t *)p->data, p->len, p->a, p->user); break;
        case 7: ((flex_fs_scan_cb_t)p->cb)(p->err, (const flex_fsx_scan_t *)p->data, p->user); break;
        default: ((flex_fs_done_cb_t)p->cb)(p->err, p->user); break;
        }
    }
    free(p->data);
    free(p);
}
// Respuestas de carpeta y de medida retenidas (kinds 6 y 7): las escenas las sueltan cuando quieren, para probar que una respuesta tardia de otra carpeta no se pinta
static bool s_hold_explore, s_hold_read;
static dpost_t *s_held[4];
static int s_nheld;
void sim_explore_hold(bool on) { s_hold_explore = on; }
void sim_read_hold(bool on) { s_hold_read = on; }   // retiene las respuestas de lectura de archivo (kind 1): la app sigue "leyendo"
static esp_err_t dpost(const dpost_t *t)
{
    dpost_t *p = malloc(sizeof(*p));
    if (!p) {
        free(t->data);
        return ESP_ERR_NO_MEM;
    }
    *p = *t;
    if (((s_hold_explore && (p->kind == 6 || p->kind == 7)) || (s_hold_read && p->kind == 1)) && s_nheld < 4) {
        s_held[s_nheld++] = p;
        return ESP_OK;
    }
    if (!flex_inbox_post(dpost_run, p)) {
        free(p->data);
        free(p);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void sim_explore_release_n(int n)   // suelta las n primeras respuestas retenidas (en el orden en que se pidieron)
{
    int k = n < s_nheld ? n : s_nheld;
    for (int i = 0; i < k; i++) {
        if (!flex_inbox_post(dpost_run, s_held[i])) {
            free(s_held[i]->data);
            free(s_held[i]);
        }
    }
    for (int i = k; i < s_nheld; i++) {
        s_held[i - k] = s_held[i];
    }
    s_nheld -= k;
}
void sim_explore_release(void) { sim_explore_release_n(4); }
static esp_err_t fsx_err(flex_fsx_res_t r)
{
    switch (r) {
    case FLEX_FSX_OK: return ESP_OK;
    case FLEX_FSX_ARG: return ESP_ERR_INVALID_ARG;
    case FLEX_FSX_NOTFOUND: return ESP_ERR_NOT_FOUND;
    case FLEX_FSX_EXISTS: return ESP_ERR_INVALID_STATE;
    default: return ESP_FAIL;
    }
}
static esp_err_t disk_done(void *cb, void *u, esp_err_t err)
{
    return dpost(&(dpost_t){.kind = 0, .cb = cb, .user = u, .err = err});
}
esp_err_t flex_fs_write_async(const char *p, const void *d, size_t l, flex_fs_done_cb_t cb, void *u)
{
    if (s_disk_on) {
        char a[200], tmp[220];
        if (!disk_abs(p, a, sizeof(a)) || snprintf(tmp, sizeof(tmp), "%s.tmp", a) >= (int)sizeof(tmp)) return ESP_ERR_INVALID_ARG;
        if (s_mf_fail) return ESP_ERR_INVALID_STATE;   // no se pudo ni poner en cola
        s_mf_writes++;
        esp_err_t e = ESP_OK;
        if (s_mf_late_fail) {
            e = ESP_FAIL;
        } else {
            disk_mkdirs(a);
            FILE *f = fopen(tmp, "wb");
            if (!f) e = ESP_FAIL;
            else {
                if (l && fwrite(d, 1, l, f) != l) e = ESP_ERR_NO_MEM;
                if (fclose(f) != 0 && e == ESP_OK) e = ESP_FAIL;
                if (e == ESP_OK && rename(tmp, a) != 0) e = ESP_FAIL;
                if (e != ESP_OK) unlink(tmp);
            }
        }
        return disk_done((void *)cb, u, e);
    }
    if (!s_mf_on) {
        (void)p; (void)d; (void)l;
        if (cb) cb(ESP_ERR_INVALID_STATE, u);
        return ESP_OK;
    }
    if (s_mf_fail || l > sizeof(s_mf[0].data)) {
        return ESP_ERR_INVALID_STATE;   // no se pudo ni poner en cola
    }
    s_mf_writes++;
    if (s_mf_defer && s_mf_npend < 8) {
        sim_fs_put(p, d, l);
        s_mf_pend[s_mf_npend].cb = cb;
        s_mf_pend[s_mf_npend].u = u;
        s_mf_npend++;
        return ESP_OK;
    }
    if (!s_mf_late_fail) {
        sim_fs_put(p, d, l);
    }
    if (cb) cb(s_mf_late_fail ? ESP_FAIL : ESP_OK, u);
    return ESP_OK;
}
esp_err_t flex_fs_read_async(const char *p, size_t m, flex_fs_read_cb_t cb, void *u)
{
    if (s_disk_on) {
        char a[200];
        struct stat st;
        if (!disk_abs(p, a, sizeof(a))) return ESP_ERR_INVALID_ARG;
        dpost_t t = {.kind = 1, .cb = (void *)cb, .user = u, .err = ESP_ERR_NOT_FOUND};
        if (stat(a, &st) == 0) {
            if ((size_t)st.st_size > m) {
                t.err = ESP_ERR_INVALID_SIZE;
            } else if ((t.data = malloc((size_t)st.st_size + 1)) != NULL) {
                FILE *f = fopen(a, "rb");
                size_t n = f ? fread(t.data, 1, (size_t)st.st_size, f) : 0;
                if (f) fclose(f);
                if (f && n == (size_t)st.st_size) {
                    t.data[n] = 0;
                    t.len = n;
                    t.err = ESP_OK;
                } else {
                    free(t.data);
                    t.data = NULL;
                    t.err = ESP_FAIL;
                }
            } else {
                t.err = ESP_ERR_NO_MEM;
            }
        }
        return dpost(&t);
    }
    if (s_mf_on) {
        for (int i = 0; i < 16; i++) {
            if (s_mf[i].used && !strcmp(s_mf[i].path, p) && s_mf[i].len <= m) {
                uint8_t *c = malloc(s_mf[i].len);
                memcpy(c, s_mf[i].data, s_mf[i].len);
                if (cb) cb(ESP_OK, c, s_mf[i].len, u);
                else free(c);
                return ESP_OK;
            }
        }
    }
    if (cb) cb(ESP_ERR_NOT_FOUND, NULL, 0, u);
    return ESP_OK;
}
esp_err_t flex_fs_remove_async(const char *p, flex_fs_done_cb_t cb, void *u)
{
    char a[200];
    if (s_disk_on) {
        if (!disk_abs(p, a, sizeof(a))) return ESP_ERR_INVALID_ARG;
        return disk_done((void *)cb, u, (unlink(a) == 0 || rmdir(a) == 0) ? ESP_OK : ESP_FAIL);
    }
    if (cb) cb(ESP_ERR_INVALID_STATE, u);
    return ESP_OK;
}
esp_err_t flex_fs_rename_async(const char *f, const char *t, flex_fs_done_cb_t cb, void *u)
{
    char a[200], b[200];
    if (s_disk_on) {
        if (!disk_abs(f, a, sizeof(a)) || !disk_abs(t, b, sizeof(b))) return ESP_ERR_INVALID_ARG;
        return disk_done((void *)cb, u, rename(a, b) == 0 ? ESP_OK : ESP_FAIL);
    }
    if (cb) cb(ESP_ERR_INVALID_STATE, u);
    return ESP_OK;
}
esp_err_t flex_fs_mkdir_async(const char *p, flex_fs_done_cb_t cb, void *u)
{
    if (s_disk_on) {
        char a[200];
        if (!disk_abs(p, a, sizeof(a))) return ESP_ERR_INVALID_ARG;
        sim_disk_mkdir(p);
        return disk_done((void *)cb, u, sim_disk_is_dir(p) ? ESP_OK : ESP_FAIL);
    }
    if (cb) cb(ESP_ERR_INVALID_STATE, u);
    return ESP_OK;
}
bool flex_fs_exists(const char *p) { return s_disk_on && sim_disk_exists(p); }

// ---- kit de archivos: las operaciones reales sobre el disco temporal (con el disco apagado: sin sistema) ----
#define FSX_ASYNC(CALL)                                                                                  \
    do {                                                                                                  \
        if (!s_disk_on) {                                                                                 \
            if (cb) cb(ESP_ERR_INVALID_STATE, u);                                                         \
            return ESP_OK;                                                                                \
        }                                                                                                 \
        return disk_done((void *)cb, u, fsx_err(CALL));                                                   \
    } while (0)
esp_err_t flex_fs_delete_async(const char *p, flex_fs_done_cb_t cb, void *u) { if (!p) return ESP_ERR_INVALID_ARG; FSX_ASYNC(flex_fsx_delete(s_disk, p)); }
esp_err_t flex_fs_rename_name_async(const char *p, const char *n, flex_fs_done_cb_t cb, void *u)
{
    if (!p || !n || strlen(n) >= FLEX_FS_NAME_MAX) return ESP_ERR_INVALID_ARG;
    FSX_ASYNC(flex_fsx_rename(s_disk, p, n));
}
esp_err_t flex_fs_trash_async(const char *p, flex_fs_done_cb_t cb, void *u) { if (!p) return ESP_ERR_INVALID_ARG; FSX_ASYNC(flex_fsx_trash(s_disk, p)); }
esp_err_t flex_fs_restore_async(const char *n, flex_fs_done_cb_t cb, void *u) { if (!n) return ESP_ERR_INVALID_ARG; FSX_ASYNC(flex_fsx_restore(s_disk, n)); }
esp_err_t flex_fs_empty_trash_async(flex_fs_done_cb_t cb, void *u) { FSX_ASYNC(flex_fsx_empty_trash(s_disk)); }
esp_err_t flex_fs_new_file_async(const char *dir, const char *name, const char *ext, flex_fs_name_cb_t cb, void *u)
{
    if (!dir || !name || !cb) return ESP_ERR_INVALID_ARG;
    if (!s_disk_on) {
        cb(ESP_ERR_INVALID_STATE, "", u);
        return ESP_OK;
    }
    char out[160];
    flex_fsx_res_t r = flex_fsx_new_file(s_disk, dir, name, ext ? ext : "", out, sizeof(out));
    return dpost(&(dpost_t){.kind = 4, .cb = (void *)cb, .user = u, .err = fsx_err(r), .data = r == FLEX_FSX_OK ? (uint8_t *)strdup(out) : NULL});
}
esp_err_t flex_fs_read_head_async(const char *p, size_t n, flex_fs_read_cb_t cb, void *u)
{
    if (!p || !cb || n == 0 || n > 4096) return ESP_ERR_INVALID_ARG;
    if (!s_disk_on) {
        cb(ESP_ERR_INVALID_STATE, NULL, 0, u);
        return ESP_OK;
    }
    uint8_t *d = NULL;
    size_t l = 0;
    flex_fsx_res_t r = flex_fsx_read_head(s_disk, p, n, &d, &l);
    return dpost(&(dpost_t){.kind = 1, .cb = (void *)cb, .user = u, .err = fsx_err(r), .data = d, .len = l});
}
esp_err_t flex_fs_dirstat_async(const char *dir, flex_fs_stat_cb_t cb, void *u)
{
    if (!dir || !cb) return ESP_ERR_INVALID_ARG;
    if (!s_disk_on) {
        cb(ESP_ERR_INVALID_STATE, 0, 0, u);
        return ESP_OK;
    }
    uint32_t n = 0, b = 0;
    flex_fsx_res_t r = flex_fsx_dirstat(s_disk, dir, &n, &b);
    return dpost(&(dpost_t){.kind = 5, .cb = (void *)cb, .user = u, .err = fsx_err(r), .a = n, .b = b});
}
// Almacenamiento y Archivos (las mismas funciones puras que la tarea de almacenamiento, sobre el disco temporal)
esp_err_t flex_fs_explore_async(const char *dir, size_t max, flex_fs_explore_cb_t cb, void *u)
{
    if (!dir || !cb || max == 0 || max > FLEX_FSX_LIST_SCAN) return ESP_ERR_INVALID_ARG;
    if (!s_disk_on) {
        cb(ESP_ERR_INVALID_STATE, NULL, 0, 0, u);
        return ESP_OK;
    }
    flex_fsx_ent_t *e = calloc(max, sizeof(*e));
    size_t n = 0, total = 0;
    flex_fsx_res_t r = e ? flex_fsx_list(s_disk, dir, e, max, &n, &total) : FLEX_FSX_ARG;
    return dpost(&(dpost_t){.kind = 6, .cb = (void *)cb, .user = u, .err = fsx_err(r), .data = (uint8_t *)e, .len = n, .a = (uint32_t)total});
}
esp_err_t flex_fs_scan_async(const char *skip, flex_fs_scan_cb_t cb, void *u)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (!s_disk_on) {
        cb(ESP_ERR_INVALID_STATE, NULL, u);
        return ESP_OK;
    }
    flex_fsx_scan_t *sc = calloc(1, sizeof(*sc));
    flex_fsx_res_t r = sc ? flex_fsx_scan(s_disk, skip, sc) : FLEX_FSX_ARG;
    return dpost(&(dpost_t){.kind = 7, .cb = (void *)cb, .user = u, .err = fsx_err(r), .data = (uint8_t *)sc});
}
uint32_t flex_storage_repair_token(void) { return 0x1234; }
esp_err_t flex_storage_nvs_erase_confirmed(uint32_t t) { (void)t; return ESP_ERR_INVALID_STATE; }
esp_err_t flex_storage_fs_format_confirmed(uint32_t t) { (void)t; return ESP_ERR_INVALID_STATE; }

// ---- sistema -----------------------------------------------------------------
void sim_net_online(bool on) { flex_net_set_online(on); }   // el simulador pone el estado de la red; lo guarda el servicio de hora por red (flex_net)
static float s_sim_loops = 58.0f;   // vueltas de la tarea de UI por segundo (la escena lo cambia)
void sim_metrics_set_loops(float n) { s_sim_loops = n; }
void flex_metrics_get(flex_metrics_t *o)
{
    memset(o, 0, sizeof(*o));
    o->ui_loops = s_sim_loops;
}
static uint32_t s_sim_fw = 1564656;   // el tamano de la imagen de esta version; la escena lo cambia
static int s_sim_reset = 1;           // ESP_RST_POWERON
void sim_system_set(uint32_t fw_size, int reset_reason)
{
    s_sim_fw = fw_size;
    s_sim_reset = reset_reason;
}
uint32_t flex_system_fw_size(void) { return s_sim_fw; }
static uint32_t s_sim_abnormal;
void sim_system_abnormal(uint32_t n) { s_sim_abnormal = n; }
uint32_t flex_system_abnormal_boots(void) { return s_sim_abnormal; }
int flex_system_reset_reason(void) { return s_sim_reset; }
// Device Care: recuperaciones del bus I2C y vigilancia de la tarea de UI (la escena los pone)
static uint32_t s_sim_i2c_recover;
void sim_i2c_recoveries(uint32_t n) { s_sim_i2c_recover = n; }
void flex_i2c_get_stats(flex_i2c_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->recoveries = s_sim_i2c_recover;
}
static bool s_sim_wdt = true;
void sim_task_wdt(bool ok) { s_sim_wdt = ok; }
bool sim_task_wdt_ok(void) { return s_sim_wdt; }
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

// ---- Modo seguro: el arranque lo decide la escena ---------------------------------------
#include "flex_safeboot.h"
#include "lvgl.h"
static flex_safe_eval_t s_safe;
static int s_reboots;
void flex_safeboot_eval(void) {}
bool flex_safe_mode(void) { return s_safe.safe; }
int flex_safe_fails(void) { return s_safe.fails; }
int flex_safe_cause(void) { return s_safe.cause; }
static bool s_exit_fail;
static void (*s_exit_cb)(void);
static void exit_fail_async(void *arg)
{
    (void)arg;
    if (s_exit_cb) s_exit_cb();
}
bool flex_safe_exit_and_reboot(void (*on_fail)(void))
{
    s_safe.fails = 0;
    flex_kvs_set_i32("flexsafe", "fails", 0);
    if (s_exit_fail) {
        s_exit_cb = on_fail;
        lv_async_call(exit_fail_async, NULL);   // en el firmware llega por el buzon de la UI
        return true;
    }
    s_reboots++;
    return true;
}
void sim_safe_exit_fail(bool on) { s_exit_fail = on; }
void sim_safe_set(int reason, int saved_fails) { s_safe = flex_safe_eval(reason, saved_fails, reason, true); }
void sim_safe_clear(void) { s_safe = (flex_safe_eval_t){0}; }
int sim_reboots(void) { return s_reboots; }

// Vaciar una carpeta: en el simulador se cuenta (no hay LittleFS)
static int s_wipes;
static char s_wipe_last[64];
esp_err_t flex_fs_wipe_dir_async(const char *p, flex_fs_count_cb_t cb, void *u)
{
    s_wipes++;
    snprintf(s_wipe_last, sizeof(s_wipe_last), "%s", p ? p : "");
    if (cb) {
        cb(ESP_OK, 0, u);
    }
    return ESP_OK;
}
int sim_fs_wipes(const char **last) { if (last) { *last = s_wipe_last; } return s_wipes; }

// ---- Restablecimiento de fabrica: el motor real (modelo) sobre un disco simulado -----------
// Nada se borra en el simulador: se anota que etapa corrio. Las escenas inyectan fallos.
#include "flex_reset.h"
#include "lvgl.h"
static flex_fr_marker_t s_fr, s_fr_disk;
static flex_fr_boot_t s_fr_boot;
static uint32_t s_fr_token;
static int s_fr_runs[FLEX_FR_FAIL + 1];
static int s_fr_fail_stage, s_fr_reboots;
static bool s_fr_fail_arm;
static flex_reset_cb_t s_fr_cb;
static void *s_fr_user;
static lv_timer_t *s_fr_tmr;
static int s_fr_mode;   // 0 avanzar, 1 armar, 2 reintentar

static bool fr_save(void *ctx, const flex_fr_marker_t *m)
{
    (void)ctx;
    if (s_fr_fail_arm && m->stage == FLEX_FR_ARMED && !s_fr_disk.pending) {
        return false;
    }
    s_fr_disk = *m;
    return true;
}
static bool fr_run(void *ctx, int st)
{
    (void)ctx;
    s_fr_runs[st]++;
    return st != s_fr_fail_stage;
}
static const flex_fr_ops_t FR_OPS = {fr_save, fr_run, NULL};

static void fr_note(int st, int err) { if (s_fr_cb) { s_fr_cb(st, err, s_fr_user); } }

static void fr_tick(lv_timer_t *t)
{
    (void)t;
    if (s_fr_mode == 1 || s_fr_mode == 2) {
        bool ok = s_fr_mode == 1 ? flex_fr_arm(&FR_OPS, &s_fr) : flex_fr_retry(&FR_OPS, &s_fr);
        s_fr_mode = 0;
        if (!ok) {
            s_fr.stage = FLEX_FR_FAIL;
            fr_note(FLEX_FR_FAIL, s_fr.err);
            lv_timer_delete(s_fr_tmr);
            s_fr_tmr = NULL;
        }
        return;
    }
    if (s_fr.stage >= FLEX_FR_ARMED && s_fr.stage <= FLEX_FR_DEFAULTS) {
        fr_note(s_fr.stage, 0);
        flex_fr_step(&FR_OPS, &s_fr);
        return;
    }
    fr_note(s_fr.stage, s_fr.err);
    lv_timer_delete(s_fr_tmr);
    s_fr_tmr = NULL;
}

static bool fr_spawn(int mode, flex_reset_cb_t cb, void *user)
{
    if (s_fr_tmr) {
        return false;
    }
    s_fr_cb = cb;
    s_fr_user = user;
    s_fr_mode = mode;
    s_fr_tmr = lv_timer_create(fr_tick, 40, NULL);
    return true;
}

bool flex_reset_boot_check(void) { s_fr_boot = flex_fr_boot_decision(&s_fr_disk); s_fr = s_fr_disk; return s_fr_boot == FLEX_FR_BOOT_RESUME; }
flex_fr_boot_t flex_reset_boot_state(void) { return s_fr_boot; }
const flex_fr_marker_t *flex_reset_marker(void) { return &s_fr; }
uint32_t flex_reset_token(void) { s_fr_token = (uint32_t)rand() | 1u; return s_fr_token; }
bool flex_reset_start(uint32_t token, flex_reset_cb_t cb, void *user)
{
    bool ok = token && token == s_fr_token;
    s_fr_token = 0;
    return ok && fr_spawn(1, cb, user);
}
bool flex_reset_resume(flex_reset_cb_t cb, void *user)
{
    return s_fr_boot == FLEX_FR_BOOT_RESUME && s_fr.stage != FLEX_FR_FAIL && fr_spawn(0, cb, user);
}
bool flex_reset_retry(flex_reset_cb_t cb, void *user) { return s_fr.stage == FLEX_FR_FAIL && fr_spawn(2, cb, user); }
void flex_reset_confirm_clean_boot(void)
{
    if (s_fr_boot == FLEX_FR_BOOT_DONE) {
        s_fr_boot = FLEX_FR_BOOT_NORMAL;
        s_fr_disk.pending = false;
    }
}
void flex_reset_reboot(void) { s_fr_reboots++; }

// para las escenas
void sim_reset_clear(void) { memset(&s_fr, 0, sizeof(s_fr)); memset(&s_fr_disk, 0, sizeof(s_fr_disk)); memset(s_fr_runs, 0, sizeof(s_fr_runs)); s_fr_boot = FLEX_FR_BOOT_NORMAL; s_fr_fail_stage = 0; s_fr_fail_arm = false; }
void sim_reset_fail(int stage, bool arm) { s_fr_fail_stage = stage; s_fr_fail_arm = arm; }
int sim_reset_runs(int stage) { return s_fr_runs[stage]; }
int sim_reset_reboots(void) { return s_fr_reboots; }
const flex_fr_marker_t *sim_reset_disk(void) { return &s_fr_disk; }
void sim_reset_set_disk(const flex_fr_marker_t *m) { s_fr_disk = *m; }

// ---- Cronometro: el servicio real (flex_crono.c); la escena de widgets lo pone en un estado conocido ----------
void flex_crono_svc_test_set(uint32_t elapsed_ms, bool running);
void sim_crono_set(uint32_t ms, bool running) { flex_crono_svc_test_set(ms, running); }

// ---- tactil multipunto: los puntos los fija la escena (sim_set_frame) ------------------------
static flex_touch_frame_t s_frame;
static bool s_frame_ok;
bool flex_touch_get_frame(flex_touch_frame_t *out)
{
    if (!s_frame_ok) {
        return false;
    }
    *out = s_frame;
    return true;
}
// pts[i] = {x, y, id}; n = 0 borra el cuadro
void sim_set_frame(const int pts[][3], int n)
{
    memset(&s_frame, 0, sizeof(s_frame));
    s_frame.count = (uint8_t)(n > FLEX_TOUCH_MAX_POINTS ? FLEX_TOUCH_MAX_POINTS : n);
    for (int i = 0; i < s_frame.count; i++) {
        s_frame.pts[i].x = (uint16_t)pts[i][0];
        s_frame.pts[i].y = (uint16_t)pts[i][1];
        s_frame.pts[i].id = (uint8_t)pts[i][2];
    }
    s_frame_ok = n > 0;
}

// ---- listado de carpetas: la escena pone los archivos (sim_fs_set) ---------------------------
static struct {
    char dir[24];
    flex_fs_ent_t e[32];
    int n;
} s_fsd[4];
void sim_fs_set(const char *dir, const char *const *names, int n)
{
    for (int i = 0; i < 4; i++) {
        if (!s_fsd[i].dir[0] || !strcmp(s_fsd[i].dir, dir)) {
            snprintf(s_fsd[i].dir, sizeof(s_fsd[i].dir), "%s", dir);
            s_fsd[i].n = n > 32 ? 32 : n;
            for (int k = 0; k < s_fsd[i].n; k++) {
                memset(&s_fsd[i].e[k], 0, sizeof(s_fsd[i].e[k]));
                snprintf(s_fsd[i].e[k].name, sizeof(s_fsd[i].e[k].name), "%s", names[k]);
            }
            return;
        }
    }
}
esp_err_t flex_fs_list_async(const char *path, size_t max, flex_fs_list_cb_t cb, void *user)
{
    if (s_disk_on) {
        char a[200], b[400];
        if (!disk_abs(path, a, sizeof(a))) return ESP_ERR_INVALID_ARG;
        flex_fs_ent_t *ents = max ? calloc(max, sizeof(*ents)) : NULL;
        size_t n = 0;
        DIR *d = opendir(a);
        esp_err_t err = d ? ESP_OK : ESP_ERR_NOT_FOUND;
        while (d && ents && n < max) {
            struct dirent *de = readdir(d);
            if (!de) break;
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || strlen(de->d_name) >= FLEX_FS_NAME_MAX) continue;
            struct stat st;
            snprintf(b, sizeof(b), "%s/%s", a, de->d_name);
            if (stat(b, &st) != 0) continue;
            strcpy(ents[n].name, de->d_name);
            ents[n].dir = S_ISDIR(st.st_mode);
            ents[n].size = ents[n].dir ? 0 : (uint32_t)st.st_size;
            n++;
        }
        if (d) closedir(d);
        return dpost(&(dpost_t){.kind = 3, .cb = (void *)cb, .user = user, .err = err, .data = (uint8_t *)ents, .len = n});
    }
    for (int i = 0; i < 4; i++) {
        if (!strcmp(s_fsd[i].dir, path)) {
            cb(ESP_OK, s_fsd[i].e, (size_t)s_fsd[i].n < max ? (size_t)s_fsd[i].n : max, user);
            return ESP_OK;
        }
    }
    cb(ESP_ERR_NOT_FOUND, NULL, 0, user);
    return ESP_OK;
}
