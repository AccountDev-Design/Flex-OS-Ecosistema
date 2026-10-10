// Flex OS Ultra · restablecimiento de fabrica en el firmware. Ver flex_reset.h.
//
// Cada paso (armar el marcador, cada etapa) corre ENTERO dentro del escritor de
// almacenamiento (flex_storage_exclusive): ningun volcado de ajustes pendientes
// se cuela entre medias y la cache se mantiene coherente con lo borrado. Una
// tarea propia espera a cada paso y avisa a la interfaz por el buzon.
//
// NO PROBADO EN HARDWARE REAL. Borra datos de verdad: solo lo arranca el
// asistente tras la confirmacion del usuario (ficha de un solo uso).
#include "flex_reset.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "flex_inbox.h"
#include "flex_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "flex.reset";

#define STEP_TIMEOUT_MS (180 * 1000)   // formatear LittleFS puede tardar

static flex_fr_marker_t s_m;
static flex_fr_boot_t s_boot;
static uint32_t s_token;
static TaskHandle_t s_task;
static flex_reset_cb_t s_cb;
static void *s_user;
static bool s_ok;   // resultado del ultimo paso exclusivo

// ---- marcador (dentro del escritor) ---------------------------------------------------
static bool save_marker(void *ctx, const flex_fr_marker_t *m)
{
    (void)ctx;
    return flex_storage_x_nvs_set_u8(FLEX_FR_NS, "pending", m->pending ? 1 : 0) == ESP_OK &&
           flex_storage_x_nvs_set_i32(FLEX_FR_NS, "ver", m->ver) == ESP_OK &&
           flex_storage_x_nvs_set_i32(FLEX_FR_NS, "stage", m->stage) == ESP_OK &&
           flex_storage_x_nvs_set_i32(FLEX_FR_NS, "err", m->err) == ESP_OK;
}

// ---- etapas (dentro del escritor; todas idempotentes) ------------------------------------
static bool run_stage(void *ctx, int stage)
{
    (void)ctx;
    static const char *const LOCK_KEYS[] = {"lockpin", "lockpass", "locktype", "lockfails", "lockhsh",
                                            "lockslt", "lockitr",  "locklen",  "lockjrn"};
    static const char *const DIRS[] = {"/Paint", "/Notas", "/System", "/System/Sessions", "/System/Cache",
                                       "/Documentos", "/Papelera"};
    bool failed = false;
    switch (stage) {
    case FLEX_FR_ARMED:
        return true;   // la interfaz ya cerro las apps y vacio Recientes
    case FLEX_FR_TOKENS:
        // Wi-Fi y cuenta enteros, y la clave del bloqueo (Wi-Fi aun no esta: Fase 6)
        return flex_storage_x_nvs_erase_ns("flexos_wifi") == ESP_OK &&
               flex_storage_x_nvs_erase_ns("flexacct") == ESP_OK &&
               flex_storage_x_nvs_erase_keys("flexos", LOCK_KEYS, sizeof(LOCK_KEYS) / sizeof(LOCK_KEYS[0])) ==
                   ESP_OK;
    case FLEX_FR_REMOTE:
        ESP_LOGI(TAG, "sin servicio de cuenta remota: no hay sesion que revocar");
        return true;
    case FLEX_FR_APPDATA: {
        flex_storage_status_t st;
        flex_storage_get_status(&st);
        if (st.fs_mounted) {
            flex_storage_x_wipe_dir("/System/Sessions", &failed);
            flex_storage_x_wipe_dir("/System/Cache", &failed);
        }
        return !failed;
    }
    case FLEX_FR_FILES: {
        if (flex_storage_x_fs_format() != ESP_OK) {
            ESP_LOGE(TAG, "no se pudo formatear LittleFS");
            return false;
        }
        flex_storage_status_t st;
        flex_storage_get_status(&st);
        for (size_t i = 0; st.fs_mounted && i < sizeof(DIRS) / sizeof(DIRS[0]); i++) {
            if (flex_storage_x_mkdir(DIRS[i]) != ESP_OK) {
                return false;
            }
        }
        return true;
    }
    case FLEX_FR_NVS:
        // TODO lo que hay en la NVS menos el marcador (Arduino dejaba el vinculo con el
        // telefono, Flex Storage, Device Care... §13.5: un aparato "restablecido" seguia emparejado)
        return flex_storage_x_nvs_erase_all_except(FLEX_FR_NS) == ESP_OK;
    case FLEX_FR_DEFAULTS:
        return true;   // sin nada en la NVS, cada modulo arranca con sus valores de fabrica
    default:
        return false;
    }
}

static const flex_fr_ops_t OPS = {save_marker, run_stage, NULL};

static void x_arm(void *ctx)
{
    (void)ctx;
    s_ok = flex_fr_arm(&OPS, &s_m);
}

static void x_step(void *ctx)
{
    (void)ctx;
    flex_fr_step(&OPS, &s_m);
    s_ok = true;
}

static void x_retry(void *ctx)
{
    (void)ctx;
    s_ok = flex_fr_retry(&OPS, &s_m);
}

// ---- avisos a la interfaz -------------------------------------------------------------------
typedef struct {
    flex_reset_cb_t cb;
    void *user;
    int stage, err;
} note_t;

static void note_ui(void *arg)
{
    note_t *n = arg;
    n->cb(n->stage, n->err, n->user);
    free(n);
}

static void notify(int stage, int err)
{
    if (!s_cb) {
        return;
    }
    note_t *n = malloc(sizeof(*n));
    if (!n) {
        return;
    }
    *n = (note_t){s_cb, s_user, stage, err};
    for (int i = 0; i < 50; i++) {
        if (flex_inbox_post(note_ui, n)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    free(n);
}

// ---- tarea ----------------------------------------------------------------------------------
enum { MODE_START, MODE_RESUME, MODE_RETRY };

static void reset_task(void *arg)
{
    int mode = (int)(intptr_t)arg;
    void (*first)(void *) = mode == MODE_START ? x_arm : mode == MODE_RETRY ? x_retry : NULL;
    if (first) {
        if (flex_storage_exclusive(first, NULL, STEP_TIMEOUT_MS) != ESP_OK || !s_ok) {
            if (s_m.stage != FLEX_FR_FAIL) {
                s_m.stage = FLEX_FR_FAIL;
            }
            ESP_LOGE(TAG, "no se pudo %s el marcador", mode == MODE_START ? "armar" : "retomar");
            notify(FLEX_FR_FAIL, s_m.err);
            s_task = NULL;
            vTaskDelete(NULL);
        }
    }
    while (s_m.stage >= FLEX_FR_ARMED && s_m.stage <= FLEX_FR_DEFAULTS) {
        notify(s_m.stage, 0);
        ESP_LOGI(TAG, "etapa %d: %s", s_m.stage, flex_fr_stage_name(s_m.stage));
        if (flex_storage_exclusive(x_step, NULL, STEP_TIMEOUT_MS) != ESP_OK) {
            s_m.err = s_m.stage;   // el escritor no contesto: lo anotado manda al reintentar
            s_m.stage = FLEX_FR_FAIL;
        }
    }
    if (s_m.stage == FLEX_FR_FAIL) {
        ESP_LOGE(TAG, "fallo en la etapa %d", s_m.err);
    }
    notify(s_m.stage, s_m.err);
    s_task = NULL;
    vTaskDelete(NULL);
}

static bool spawn(int mode, flex_reset_cb_t cb, void *user)
{
    if (s_task) {
        return false;
    }
    s_cb = cb;
    s_user = user;
    return xTaskCreate(reset_task, "flex_reset", 4096, (void *)(intptr_t)mode, 4, &s_task) == pdPASS;
}

// ---- API ------------------------------------------------------------------------------------
bool flex_reset_boot_check(void)
{
    s_m.pending = flex_kvs_get_bool(FLEX_FR_NS, "pending", false);
    s_m.ver = flex_kvs_get_i32(FLEX_FR_NS, "ver", 0);
    s_m.stage = flex_kvs_get_i32(FLEX_FR_NS, "stage", 0);
    s_m.err = flex_kvs_get_i32(FLEX_FR_NS, "err", 0);
    s_boot = flex_fr_boot_decision(&s_m);
    switch (s_boot) {
    case FLEX_FR_BOOT_RESUME:
        ESP_LOGW(TAG, "borrado interrumpido: se retoma en la etapa %d", s_m.stage);
        return true;
    case FLEX_FR_BOOT_DISCARD:
        ESP_LOGW(TAG, "marcador de restablecimiento desconocido (ver %d): se descarta", s_m.ver);
        flex_kvs_set_bool(FLEX_FR_NS, "pending", false);
        return false;
    case FLEX_FR_BOOT_DONE:
        ESP_LOGI(TAG, "arranque tras un restablecimiento terminado");
        return false;
    default:
        return false;
    }
}

flex_fr_boot_t flex_reset_boot_state(void)
{
    return s_boot;
}

const flex_fr_marker_t *flex_reset_marker(void)
{
    return &s_m;
}

uint32_t flex_reset_token(void)
{
    s_token = esp_random() | 1u;
    return s_token;
}

bool flex_reset_start(uint32_t token, flex_reset_cb_t cb, void *user)
{
    bool ok = token != 0 && token == s_token;
    s_token = 0;   // un solo uso
    return ok && spawn(MODE_START, cb, user);
}

bool flex_reset_resume(flex_reset_cb_t cb, void *user)
{
    if (s_boot != FLEX_FR_BOOT_RESUME || s_m.stage == FLEX_FR_FAIL) {
        return false;
    }
    return spawn(MODE_RESUME, cb, user);
}

bool flex_reset_retry(flex_reset_cb_t cb, void *user)
{
    return s_m.stage == FLEX_FR_FAIL && spawn(MODE_RETRY, cb, user);
}

void flex_reset_confirm_clean_boot(void)
{
    if (s_boot != FLEX_FR_BOOT_DONE) {
        return;
    }
    s_boot = FLEX_FR_BOOT_NORMAL;
    flex_kvs_set_bool(FLEX_FR_NS, "pending", false);
    flex_kvs_set_i32(FLEX_FR_NS, "stage", FLEX_FR_NONE);
    ESP_LOGI(TAG, "arranque limpio confirmado: marcador de recuperacion borrado");
}

void flex_reset_reboot(void)
{
    flex_cfg_flush(1500);
    esp_restart();
}
