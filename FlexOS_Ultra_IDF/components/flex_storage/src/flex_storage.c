#include "flex_storage.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "flex_bus.h"
#include "flex_core.h"
#include "flex_fs_path.h"
#include "flex_inbox.h"
#include "flex_kv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define JOB_QUEUE_DEPTH      32
#define SETTINGS_DEBOUNCE_US (300 * 1000)
#define WRITE_CHUNK          (16 * 1024)
#define FS_INFO_GAP_US       (5 * 1000 * 1000)
#define TMP_SUFFIX           ".tmp~"
#define REPLY_RETRIES        50
#define REPLY_RETRY_MS       10

static const char *TAG = "flex.storage";

// Espacios de NVS que se cargan en la cache al arrancar (los de la version Arduino).
static const char *const k_namespaces[] = {FLEX_NVS_NS, "flexcare", "flexphone"};

typedef enum {
    JOB_WRITE, JOB_REMOVE, JOB_RENAME, JOB_MKDIR, JOB_READ, JOB_FLUSH, JOB_NVS_ERASE, JOB_FS_FORMAT,
} job_op_t;

typedef struct {
    job_op_t op;
    char path[FLEX_FS_PATH_MAX];
    char path2[FLEX_FS_PATH_MAX];
    uint8_t *data;
    size_t len;
    void *cb;
    void *user;
    SemaphoreHandle_t done;   // JOB_FLUSH: quien espera
} job_t;

typedef struct {
    void *cb;
    void *user;
    esp_err_t err;
    uint8_t *data;
    size_t len;
    bool is_read;
} reply_t;

static flex_kv_t s_kv;
static SemaphoreHandle_t s_kv_mtx;
static QueueHandle_t s_jobs;
static TaskHandle_t s_task;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static esp_err_t s_flush_err = ESP_OK;   // resultado de la ultima pasada pedida (s_mux)
static flex_storage_status_t s_st;
static int64_t s_last_set_us;
static int64_t s_fs_info_us;
static uint32_t s_token;

// ---------------------------------------------------------------- utilidades
static void kv_lock(void *ctx)
{
    (void)ctx;
    xSemaphoreTake(s_kv_mtx, portMAX_DELAY);
}

static void kv_unlock(void *ctx)
{
    (void)ctx;
    xSemaphoreGive(s_kv_mtx);
}

static void status_update(void (*fn)(flex_storage_status_t *, void *), void *arg)
{
    portENTER_CRITICAL(&s_mux);
    fn(&s_st, arg);
    portEXIT_CRITICAL(&s_mux);
}

static void st_set_nvs(flex_storage_status_t *st, void *arg)
{
    esp_err_t e = *(esp_err_t *)arg;
    st->nvs_ok = e == ESP_OK;
    st->nvs_err = e;
}

static void st_set_fs(flex_storage_status_t *st, void *arg)
{
    esp_err_t e = *(esp_err_t *)arg;
    st->fs_mounted = e == ESP_OK;
    st->fs_err = e;
}

static void st_write_error(flex_storage_status_t *st, void *arg)
{
    (void)arg;
    st->write_errors++;
}

static bool valid_rel_path(const char *p)
{
    return flex_fs_path_valid(p, strlen(FLEX_FS_BASE), strlen(TMP_SUFFIX), FLEX_FS_PATH_MAX);
}

bool flex_fs_abs(const char *path, char *out, size_t cap)
{
    if (!valid_rel_path(path)) {
        return false;
    }
    int n = snprintf(out, cap, "%s%s", FLEX_FS_BASE, path);
    return n > 0 && (size_t)n < cap;
}

// ---------------------------------------------------------------- NVS
static void nvs_load_namespace(const char *ns)
{
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, ns, NVS_TYPE_ANY, &it);
    nvs_handle_t h = 0;
    if (err == ESP_OK && nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        nvs_release_iterator(it);
        return;
    }
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        int64_t v = 0;
        switch (info.type) {
        case NVS_TYPE_U8: { uint8_t x; if (nvs_get_u8(h, info.key, &x) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_U8, x); break; }
        case NVS_TYPE_I8: { int8_t x; if (nvs_get_i8(h, info.key, &x) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_I8, x); break; }
        case NVS_TYPE_U16: { uint16_t x; if (nvs_get_u16(h, info.key, &x) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_U16, x); break; }
        case NVS_TYPE_I16: { int16_t x; if (nvs_get_i16(h, info.key, &x) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_I16, x); break; }
        case NVS_TYPE_U32: { uint32_t x; if (nvs_get_u32(h, info.key, &x) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_U32, x); break; }
        case NVS_TYPE_I32: { int32_t x; if (nvs_get_i32(h, info.key, &x) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_I32, x); break; }
        case NVS_TYPE_U64: { uint64_t x; if (nvs_get_u64(h, info.key, &x) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_U64, (int64_t)x); break; }
        case NVS_TYPE_I64: { if (nvs_get_i64(h, info.key, &v) == ESP_OK) flex_kv_load_num(&s_kv, ns, info.key, FLEX_KV_I64, v); break; }
        case NVS_TYPE_STR:
        case NVS_TYPE_BLOB: {
            size_t len = 0;
            bool str = info.type == NVS_TYPE_STR;
            esp_err_t e = str ? nvs_get_str(h, info.key, NULL, &len) : nvs_get_blob(h, info.key, NULL, &len);
            if (e == ESP_OK && len <= FLEX_KV_BUF_MAX) {
                uint8_t *buf = malloc(len ? len : 1);
                if (buf) {
                    e = str ? nvs_get_str(h, info.key, (char *)buf, &len) : nvs_get_blob(h, info.key, buf, &len);
                    if (e == ESP_OK) {
                        flex_kv_load_buf(&s_kv, ns, info.key, str ? FLEX_KV_STR : FLEX_KV_BLOB, buf, len);
                    }
                    free(buf);
                }
            }
            break;
        }
        default:
            break;
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (h) {
        nvs_close(h);
    }
}

static esp_err_t nvs_bring_up(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // Sin borrar: esa NVS puede tener los ajustes de la version Arduino.
        ESP_LOGE(TAG, "NVS no disponible (%s): ajustes solo en RAM; reparar requiere confirmacion",
                 esp_err_to_name(err));
        return err;
    }
    for (size_t i = 0; i < sizeof(k_namespaces) / sizeof(k_namespaces[0]); i++) {
        nvs_load_namespace(k_namespaces[i]);
    }
    ESP_LOGI(TAG, "NVS lista: %u claves en cache", (unsigned)s_kv.count);
    return ESP_OK;
}

static esp_err_t nvs_write_snapshot(const flex_kv_snapshot_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(s->ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (s->erase) {
        err = nvs_erase_key(h, s->key);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        switch (s->type) {
        case FLEX_KV_U8:  err = nvs_set_u8(h, s->key, (uint8_t)s->num); break;
        case FLEX_KV_I8:  err = nvs_set_i8(h, s->key, (int8_t)s->num); break;
        case FLEX_KV_U16: err = nvs_set_u16(h, s->key, (uint16_t)s->num); break;
        case FLEX_KV_I16: err = nvs_set_i16(h, s->key, (int16_t)s->num); break;
        case FLEX_KV_U32: err = nvs_set_u32(h, s->key, (uint32_t)s->num); break;
        case FLEX_KV_I32: err = nvs_set_i32(h, s->key, (int32_t)s->num); break;
        case FLEX_KV_U64: err = nvs_set_u64(h, s->key, (uint64_t)s->num); break;
        case FLEX_KV_I64: err = nvs_set_i64(h, s->key, s->num); break;
        case FLEX_KV_STR: err = nvs_set_str(h, s->key, s->len ? (const char *)s->buf : ""); break;
        case FLEX_KV_BLOB: err = nvs_set_blob(h, s->key, s->buf, s->len); break;
        default: err = ESP_ERR_INVALID_ARG; break;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// ESP_OK solo si TODO lo pendiente quedo grabado: quien encadena escrituras
// (la clave del sistema: hash nuevo antes de borrar el antiguo) decide con esto.
static esp_err_t settings_flush(void)
{
    bool nvs_ok;
    portENTER_CRITICAL(&s_mux);
    nvs_ok = s_st.nvs_ok;
    portEXIT_CRITICAL(&s_mux);
    if (!nvs_ok) {
        return ESP_ERR_INVALID_STATE;   // quedan pendientes en RAM; no hay donde grabar
    }
    flex_kv_snapshot_t snap;
    size_t guard = s_kv.count + 4;
    for (;;) {
        if (guard-- == 0) {
            return ESP_ERR_TIMEOUT;   // otra tarea no para de escribir: la siguiente pasada sigue
        }
        if (!flex_kv_next_dirty(&s_kv, &snap)) {
            return ESP_OK;
        }
        esp_err_t err = nvs_write_snapshot(&snap);
        if (err == ESP_OK) {
            flex_kv_mark_written(&s_kv, &snap);
        } else {
            status_update(st_write_error, NULL);
            ESP_LOGE(TAG, "no se pudo grabar %s/%s: %s", snap.ns, snap.key, esp_err_to_name(err));
            flex_bus_post(FLEX_EV_STORAGE, FLEX_STORAGE_EV_WRITE_ERROR, &err, sizeof(err));
            flex_kv_snapshot_free(&snap);
            return err;   // se reintenta en la siguiente pasada
        }
        flex_kv_snapshot_free(&snap);
        esp_task_wdt_reset();
    }
}

// ---------------------------------------------------------------- LittleFS
static esp_err_t fs_mount(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = FLEX_FS_BASE,
        .partition_label = FLEX_FS_LABEL,
        .format_if_mount_failed = false,   // NUNCA formatear por un fallo al montar
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS ('%s') no monta (%s): NO se formatea; los datos siguen en la flash", FLEX_FS_LABEL,
                 esp_err_to_name(err));
    }
    return err;
}

static void fs_info_refresh(bool force)
{
    int64_t now = esp_timer_get_time();
    if (!force && now - s_fs_info_us < FS_INFO_GAP_US) {
        return;
    }
    s_fs_info_us = now;
    size_t total = 0, used = 0;
    if (esp_littlefs_info(FLEX_FS_LABEL, &total, &used) == ESP_OK) {
        portENTER_CRITICAL(&s_mux);
        s_st.fs_total = total;
        s_st.fs_used = used;
        portEXIT_CRITICAL(&s_mux);
    }
}

static esp_err_t mkdirs_for(const char *abs_path)
{
    char tmp[FLEX_FS_PATH_MAX];
    strlcpy(tmp, abs_path, sizeof(tmp));
    for (char *p = tmp + sizeof(FLEX_FS_BASE); *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                return ESP_FAIL;
            }
            *p = '/';
        }
    }
    return ESP_OK;
}

static esp_err_t do_write(const char *rel, const uint8_t *data, size_t len)
{
    char path[FLEX_FS_PATH_MAX], tmp[FLEX_FS_PATH_MAX];
    if (!flex_fs_abs(rel, path, sizeof(path)) || snprintf(tmp, sizeof(tmp), "%s%s", path, TMP_SUFFIX) >= (int)sizeof(tmp)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mkdirs_for(path) != ESP_OK) {
        return ESP_FAIL;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return ESP_FAIL;
    }
    size_t off = 0;
    while (off < len) {
        size_t n = len - off > WRITE_CHUNK ? WRITE_CHUNK : len - off;
        if (fwrite(data + off, 1, n, f) != n) {
            fclose(f);
            unlink(tmp);
            return ESP_ERR_NO_MEM;   // sin espacio o error de flash
        }
        off += n;
        esp_task_wdt_reset();
    }
    if (fclose(f) != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t flex_fs_read_file(const char *rel, uint8_t **data, size_t *len, size_t max_len)
{
    char path[FLEX_FS_PATH_MAX];
    if (!data || !len || !flex_fs_abs(rel, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    struct stat stt;
    if (stat(path, &stt) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if ((size_t)stt.st_size > max_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *buf = malloc((size_t)stt.st_size + 1);   // +1: NUL comodo para texto
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        free(buf);
        return ESP_FAIL;
    }
    size_t n = fread(buf, 1, (size_t)stt.st_size, f);
    fclose(f);
    if (n != (size_t)stt.st_size) {
        free(buf);
        return ESP_FAIL;
    }
    buf[n] = 0;
    *data = buf;
    *len = n;
    return ESP_OK;
}

bool flex_fs_exists(const char *rel)
{
    char path[FLEX_FS_PATH_MAX];
    struct stat stt;
    return flex_fs_abs(rel, path, sizeof(path)) && stat(path, &stt) == 0;
}

// ---------------------------------------------------------------- trabajos
static void reply_in_ui(void *arg)
{
    reply_t *r = arg;
    if (r->is_read) {
        ((flex_fs_read_cb_t)r->cb)(r->err, r->data, r->len, r->user);
    } else {
        ((flex_fs_done_cb_t)r->cb)(r->err, r->user);
    }
    free(r);
}

static void reply(job_t *job, esp_err_t err, uint8_t *data, size_t len)
{
    if (!job->cb) {
        free(data);
        return;
    }
    reply_t *r = malloc(sizeof(*r));
    if (r) {
        *r = (reply_t){job->cb, job->user, err, data, len, job->op == JOB_READ};
        // Quien pidio el trabajo espera la respuesta (p. ej. una pantalla con
        // "Guardando..."): si el buzon esta lleno se reintenta un rato, esta
        // tarea no es la de UI y puede esperar.
        for (int i = 0; i < REPLY_RETRIES; i++) {
            if (flex_inbox_post(reply_in_ui, r)) {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(REPLY_RETRY_MS));
        }
        free(r);
    }
    ESP_LOGE(TAG, "respuesta de un trabajo perdida (buzon lleno %d ms)", REPLY_RETRIES * REPLY_RETRY_MS);
    free(data);
}

static void run_job(job_t *job)
{
    esp_err_t err = ESP_OK;
    char a[FLEX_FS_PATH_MAX], b[FLEX_FS_PATH_MAX];
    bool fs_ok;
    portENTER_CRITICAL(&s_mux);
    fs_ok = s_st.fs_mounted;
    portEXIT_CRITICAL(&s_mux);
    bool needs_fs = job->op <= JOB_READ;
    if (needs_fs && !fs_ok) {
        reply(job, ESP_ERR_INVALID_STATE, NULL, 0);
        return;
    }
    switch (job->op) {
    case JOB_WRITE:
        err = do_write(job->path, job->data, job->len);
        fs_info_refresh(false);
        break;
    case JOB_REMOVE:
        err = flex_fs_abs(job->path, a, sizeof(a)) ? (unlink(a) == 0 || rmdir(a) == 0 ? ESP_OK : ESP_FAIL)
                                                  : ESP_ERR_INVALID_ARG;
        fs_info_refresh(false);
        break;
    case JOB_RENAME:
        err = (flex_fs_abs(job->path, a, sizeof(a)) && flex_fs_abs(job->path2, b, sizeof(b)))
                  ? (rename(a, b) == 0 ? ESP_OK : ESP_FAIL)
                  : ESP_ERR_INVALID_ARG;
        break;
    case JOB_MKDIR:
        err = flex_fs_abs(job->path, a, sizeof(a))
                  ? ((mkdirs_for(a) == ESP_OK && (mkdir(a, 0775) == 0 || errno == EEXIST)) ? ESP_OK : ESP_FAIL)
                  : ESP_ERR_INVALID_ARG;
        break;
    case JOB_READ: {
        uint8_t *data = NULL;
        size_t len = 0;
        err = flex_fs_read_file(job->path, &data, &len, job->len);
        reply(job, err, data, len);
        return;
    }
    case JOB_FLUSH:
        err = settings_flush();
        portENTER_CRITICAL(&s_mux);
        s_flush_err = err;
        portEXIT_CRITICAL(&s_mux);
        if (job->done) {
            xSemaphoreGive(job->done);
        }
        return;
    case JOB_NVS_ERASE:
        nvs_flash_deinit();
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
        status_update(st_set_nvs, &err);
        if (err == ESP_OK) {
            // La cache conserva los valores en uso: se vuelven a grabar todos.
            flex_kv_mark_all_dirty(&s_kv);
            s_last_set_us = 0;
            flex_bus_post(FLEX_EV_STORAGE, FLEX_STORAGE_EV_NVS_READY, NULL, 0);
        }
        break;
    case JOB_FS_FORMAT:
        err = esp_littlefs_format(FLEX_FS_LABEL);
        if (err == ESP_OK && !esp_littlefs_mounted(FLEX_FS_LABEL)) {
            err = fs_mount();
        }
        status_update(st_set_fs, &err);
        if (err == ESP_OK) {
            fs_info_refresh(true);
            flex_bus_post(FLEX_EV_STORAGE, FLEX_STORAGE_EV_FS_MOUNTED, NULL, 0);
        }
        break;
    }
    if (err != ESP_OK && job->op != JOB_READ) {
        status_update(st_write_error, NULL);
        ESP_LOGW(TAG, "trabajo %d sobre '%s' fallo: %s", (int)job->op, job->path, esp_err_to_name(err));
    }
    reply(job, err, NULL, 0);
}

static void storage_task(void *arg)
{
    (void)arg;
    bool wdt = esp_task_wdt_add(NULL) == ESP_OK;
    for (;;) {
        job_t *job = NULL;
        bool dirty = flex_kv_dirty_count(&s_kv) > 0;
        TickType_t wait = pdMS_TO_TICKS(dirty ? 100 : 1000);
        if (xQueueReceive(s_jobs, &job, wait) == pdTRUE && job) {
            run_job(job);
            free(job->data);
            free(job);
        }
        if (dirty && esp_timer_get_time() - s_last_set_us >= SETTINGS_DEBOUNCE_US) {
            settings_flush();
        }
        portENTER_CRITICAL(&s_mux);
        s_st.pending_settings = (uint32_t)flex_kv_dirty_count(&s_kv);
        s_st.pending_jobs = (uint32_t)uxQueueMessagesWaiting(s_jobs);
        portEXIT_CRITICAL(&s_mux);
        if (wdt) {
            esp_task_wdt_reset();
        }
    }
}

static esp_err_t enqueue(job_t *job)
{
    if (!s_jobs) {
        free(job->data);
        free(job);
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        free(job->data);
        free(job);
        return ESP_ERR_NO_MEM;   // cola llena: el llamante decide
    }
    return ESP_OK;
}

static job_t *new_job(job_op_t op, const char *path, void *cb, void *user)
{
    if (path && !valid_rel_path(path)) {
        return NULL;
    }
    job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return NULL;
    }
    job->op = op;
    if (path) {
        strlcpy(job->path, path, sizeof(job->path));
    }
    job->cb = cb;
    job->user = user;
    return job;
}

esp_err_t flex_fs_write_async(const char *path, const void *data, size_t len, flex_fs_done_cb_t cb, void *user)
{
    job_t *job = new_job(JOB_WRITE, path, (void *)cb, user);
    if (!job) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len) {
        job->data = malloc(len);   // >16 KB acaba en PSRAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
        if (!job->data) {
            free(job);
            return ESP_ERR_NO_MEM;
        }
        memcpy(job->data, data, len);
    }
    job->len = len;
    return enqueue(job);
}

esp_err_t flex_fs_remove_async(const char *path, flex_fs_done_cb_t cb, void *user)
{
    job_t *job = new_job(JOB_REMOVE, path, (void *)cb, user);
    return job ? enqueue(job) : ESP_ERR_INVALID_ARG;
}

esp_err_t flex_fs_rename_async(const char *from, const char *to, flex_fs_done_cb_t cb, void *user)
{
    if (!valid_rel_path(to)) {
        return ESP_ERR_INVALID_ARG;
    }
    job_t *job = new_job(JOB_RENAME, from, (void *)cb, user);
    if (!job) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(job->path2, to, sizeof(job->path2));
    return enqueue(job);
}

esp_err_t flex_fs_mkdir_async(const char *path, flex_fs_done_cb_t cb, void *user)
{
    job_t *job = new_job(JOB_MKDIR, path, (void *)cb, user);
    return job ? enqueue(job) : ESP_ERR_INVALID_ARG;
}

esp_err_t flex_fs_read_async(const char *path, size_t max_len, flex_fs_read_cb_t cb, void *user)
{
    job_t *job = new_job(JOB_READ, path, (void *)cb, user);
    if (!job) {
        return ESP_ERR_INVALID_ARG;
    }
    job->len = max_len;
    return enqueue(job);
}

esp_err_t flex_cfg_flush(uint32_t timeout_ms)
{
    job_t *job = new_job(JOB_FLUSH, NULL, NULL, NULL);
    if (!job) {
        return ESP_ERR_NO_MEM;
    }
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done) {
        free(job);
        return ESP_ERR_NO_MEM;
    }
    job->done = done;
    esp_err_t err = enqueue(job);
    if (err == ESP_OK && xSemaphoreTake(done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
        // El trabajo aun puede usar el semaforo: se deja vivo (fuga acotada a este caso raro).
        return err;
    }
    vSemaphoreDelete(done);
    if (err == ESP_OK) {
        // Resultado de una pasada que termino DESPUES de encolar esta peticion
        // (la suya o una posterior): cubre todo lo escrito antes de llamar.
        portENTER_CRITICAL(&s_mux);
        err = s_flush_err;
        portEXIT_CRITICAL(&s_mux);
    }
    return err;
}

uint32_t flex_storage_repair_token(void)
{
    uint32_t t = esp_random() | 1u;
    portENTER_CRITICAL(&s_mux);
    s_token = t;
    portEXIT_CRITICAL(&s_mux);
    return t;
}

static bool take_token(uint32_t token)
{
    bool ok;
    portENTER_CRITICAL(&s_mux);
    ok = token != 0 && token == s_token;
    s_token = 0;
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

esp_err_t flex_storage_nvs_erase_confirmed(uint32_t token)
{
    if (!take_token(token)) {
        return ESP_ERR_INVALID_ARG;
    }
    job_t *job = new_job(JOB_NVS_ERASE, NULL, NULL, NULL);
    return job ? enqueue(job) : ESP_ERR_NO_MEM;
}

esp_err_t flex_storage_fs_format_confirmed(uint32_t token)
{
    if (!take_token(token)) {
        return ESP_ERR_INVALID_ARG;
    }
    job_t *job = new_job(JOB_FS_FORMAT, NULL, NULL, NULL);
    return job ? enqueue(job) : ESP_ERR_NO_MEM;
}

void flex_storage_get_status(flex_storage_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_st;
    portEXIT_CRITICAL(&s_mux);
}

// ---------------------------------------------------------------- ajustes
static void changed(const char *ns, const char *key)
{
    s_last_set_us = esp_timer_get_time();
    flex_setting_changed_t ev = {0};
    strlcpy(ev.ns, ns, sizeof(ev.ns));
    strlcpy(ev.key, key, sizeof(ev.key));
    flex_bus_post(FLEX_EV_SETTINGS, FLEX_SETTINGS_EV_CHANGED, &ev, sizeof(ev));
}

static esp_err_t set_num(const char *ns, const char *key, flex_kv_type_t t, int64_t v)
{
    if (!s_kv_mtx) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(ns) >= FLEX_KV_NS_MAX || strlen(key) >= FLEX_KV_KEY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (flex_kv_set_num(&s_kv, ns, key, t, v)) {
        changed(ns, key);
    }
    return ESP_OK;
}

static esp_err_t set_buf(const char *ns, const char *key, flex_kv_type_t t, const void *d, size_t len)
{
    if (!s_kv_mtx) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(ns) >= FLEX_KV_NS_MAX || strlen(key) >= FLEX_KV_KEY_MAX || len > FLEX_KV_BUF_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (flex_kv_set_buf(&s_kv, ns, key, t, d, len)) {
        changed(ns, key);
    }
    return ESP_OK;
}

static int64_t get_num(const char *ns, const char *key, flex_kv_type_t t, int64_t def)
{
    int64_t v;
    return (s_kv_mtx && flex_kv_get_num(&s_kv, ns, key, t, &v)) ? v : def;
}

bool flex_kvs_get_bool(const char *ns, const char *key, bool def) { return get_num(ns, key, FLEX_KV_U8, def) != 0; }
int32_t flex_kvs_get_i32(const char *ns, const char *key, int32_t def) { return (int32_t)get_num(ns, key, FLEX_KV_I32, def); }
uint32_t flex_kvs_get_u32(const char *ns, const char *key, uint32_t def) { return (uint32_t)get_num(ns, key, FLEX_KV_U32, def); }

size_t flex_kvs_get_blob(const char *ns, const char *key, void *out, size_t cap)
{
    size_t len = 0;
    if (!s_kv_mtx || !flex_kv_get_buf(&s_kv, ns, key, FLEX_KV_BLOB, out, cap, &len)) {
        return 0;
    }
    return len;
}

void flex_kvs_get_str(const char *ns, const char *key, char *out, size_t cap, const char *def)
{
    if (!out || !cap) {
        return;
    }
    size_t len = 0;
    if (s_kv_mtx && flex_kv_get_buf(&s_kv, ns, key, FLEX_KV_STR, out, cap - 1, &len)) {
        out[len < cap - 1 ? len : cap - 1] = '\0';   // NVS guarda el NUL; se asegura igualmente
        return;
    }
    strlcpy(out, def ? def : "", cap);
}

esp_err_t flex_kvs_set_bool(const char *ns, const char *key, bool v) { return set_num(ns, key, FLEX_KV_U8, v ? 1 : 0); }
esp_err_t flex_kvs_set_i32(const char *ns, const char *key, int32_t v) { return set_num(ns, key, FLEX_KV_I32, v); }
esp_err_t flex_kvs_set_u32(const char *ns, const char *key, uint32_t v) { return set_num(ns, key, FLEX_KV_U32, v); }
esp_err_t flex_kvs_set_blob(const char *ns, const char *key, const void *d, size_t len) { return set_buf(ns, key, FLEX_KV_BLOB, d, len); }

esp_err_t flex_kvs_set_str(const char *ns, const char *key, const char *v)
{
    v = v ? v : "";
    return set_buf(ns, key, FLEX_KV_STR, v, strlen(v) + 1);
}

bool flex_cfg_get_bool(const char *key, bool def) { return flex_kvs_get_bool(FLEX_NVS_NS, key, def); }
int32_t flex_cfg_get_i32(const char *key, int32_t def) { return flex_kvs_get_i32(FLEX_NVS_NS, key, def); }
uint32_t flex_cfg_get_u32(const char *key, uint32_t def) { return flex_kvs_get_u32(FLEX_NVS_NS, key, def); }
uint8_t flex_cfg_get_u8(const char *key, uint8_t def) { return (uint8_t)get_num(FLEX_NVS_NS, key, FLEX_KV_U8, def); }
int8_t flex_cfg_get_i8(const char *key, int8_t def) { return (int8_t)get_num(FLEX_NVS_NS, key, FLEX_KV_I8, def); }
void flex_cfg_get_str(const char *key, char *out, size_t cap, const char *def) { flex_kvs_get_str(FLEX_NVS_NS, key, out, cap, def); }
size_t flex_cfg_get_blob(const char *key, void *out, size_t cap) { return flex_kvs_get_blob(FLEX_NVS_NS, key, out, cap); }
esp_err_t flex_cfg_set_bool(const char *key, bool v) { return flex_kvs_set_bool(FLEX_NVS_NS, key, v); }
esp_err_t flex_cfg_set_i32(const char *key, int32_t v) { return flex_kvs_set_i32(FLEX_NVS_NS, key, v); }
esp_err_t flex_cfg_set_u32(const char *key, uint32_t v) { return flex_kvs_set_u32(FLEX_NVS_NS, key, v); }
esp_err_t flex_cfg_set_u8(const char *key, uint8_t v) { return set_num(FLEX_NVS_NS, key, FLEX_KV_U8, v); }
esp_err_t flex_cfg_set_i8(const char *key, int8_t v) { return set_num(FLEX_NVS_NS, key, FLEX_KV_I8, v); }
esp_err_t flex_cfg_set_str(const char *key, const char *v) { return flex_kvs_set_str(FLEX_NVS_NS, key, v); }
esp_err_t flex_cfg_set_blob(const char *key, const void *d, size_t len) { return flex_kvs_set_blob(FLEX_NVS_NS, key, d, len); }

esp_err_t flex_cfg_erase(const char *key)
{
    if (!s_kv_mtx) {
        return ESP_ERR_INVALID_STATE;
    }
    if (flex_kv_erase(&s_kv, FLEX_NVS_NS, key)) {
        changed(FLEX_NVS_NS, key);
    }
    return ESP_OK;
}

// ---------------------------------------------------------------- arranque
esp_err_t flex_storage_init(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_kv_mtx = xSemaphoreCreateMutex();
    s_jobs = xQueueCreate(JOB_QUEUE_DEPTH, sizeof(job_t *));
    ESP_RETURN_ON_FALSE(s_kv_mtx && s_jobs, ESP_ERR_NO_MEM, TAG, "sin memoria");
    flex_kv_init(&s_kv, kv_lock, kv_unlock, NULL);

    esp_err_t nvs_err = nvs_bring_up();
    status_update(st_set_nvs, &nvs_err);
    flex_bus_post(FLEX_EV_STORAGE, nvs_err == ESP_OK ? FLEX_STORAGE_EV_NVS_READY : FLEX_STORAGE_EV_NVS_UNAVAILABLE,
                  &nvs_err, sizeof(nvs_err));

    esp_err_t fs_err = fs_mount();
    status_update(st_set_fs, &fs_err);
    if (fs_err == ESP_OK) {
        fs_info_refresh(true);
        size_t total = s_st.fs_total, used = s_st.fs_used;
        ESP_LOGI(TAG, "LittleFS montado en %s: %u KB usados de %u KB", FLEX_FS_BASE, (unsigned)(used / 1024),
                 (unsigned)(total / 1024));
    }
    flex_bus_post(FLEX_EV_STORAGE, fs_err == ESP_OK ? FLEX_STORAGE_EV_FS_MOUNTED : FLEX_STORAGE_EV_FS_UNAVAILABLE,
                  &fs_err, sizeof(fs_err));

    BaseType_t ok = xTaskCreatePinnedToCore(storage_task, FLEX_TASK_STORAGE_NAME, FLEX_TASK_STORAGE_STACK, NULL,
                                            FLEX_TASK_STORAGE_PRIO, &s_task, FLEX_TASK_STORAGE_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea de almacenamiento");
    return ESP_OK;
}
