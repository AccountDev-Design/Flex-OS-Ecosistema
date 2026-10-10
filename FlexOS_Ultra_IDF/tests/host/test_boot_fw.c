// Pruebas de host del codigo de ARRANQUE del firmware (no del modelo): se
// compila components/flex_system/src/flex_safeboot.c tal cual, sobre una NVS
// falsa que separa lo que esta en la cache de ajustes (RAM) de lo que ya esta
// grabado. Un "corte" entre arranques tira la cache: solo cuenta lo grabado.
//   tests/host/run.sh
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "flex_inbox.h"
#include "flex_safeboot.h"
#include "flex_storage.h"
#include "freertos/task.h"

static int s_fail;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("boot_fw: FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); \
            s_fail++;                                               \
        }                                                           \
    } while (0)

// ---- NVS falsa: cache y disco --------------------------------------------------------
typedef struct {
    char key[16];
    int32_t v;
    bool set;
} slot_t;
static slot_t s_cache[4], s_disk[4];
static bool s_flush_fails;
static int s_flushes;

static slot_t *find(slot_t *t, const char *key, bool add)
{
    for (int i = 0; i < 4; i++) {
        if (t[i].set && strcmp(t[i].key, key) == 0) {
            return &t[i];
        }
    }
    for (int i = 0; add && i < 4; i++) {
        if (!t[i].set) {
            snprintf(t[i].key, sizeof(t[i].key), "%s", key);
            t[i].set = true;
            return &t[i];
        }
    }
    return NULL;
}

int32_t flex_kvs_get_i32(const char *ns, const char *key, int32_t def)
{
    slot_t *s = strcmp(ns, "flexsafe") == 0 ? find(s_cache, key, false) : NULL;
    return s ? s->v : def;
}

esp_err_t flex_kvs_set_i32(const char *ns, const char *key, int32_t v)
{
    if (strcmp(ns, "flexsafe") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    find(s_cache, key, true)->v = v;   // solo la cache: el escritor graba mas tarde
    return ESP_OK;
}

esp_err_t flex_cfg_flush(uint32_t timeout_ms)
{
    (void)timeout_ms;
    s_flushes++;
    if (s_flush_fails) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(s_disk, s_cache, sizeof(s_disk));
    return ESP_OK;
}

static int32_t disk_i32(const char *key, int32_t def)
{
    slot_t *s = find(s_disk, key, false);
    return s ? s->v : def;
}

// Arranque: la cache se carga de lo grabado (lo que no llego al disco se pierde)
static void power_cycle(void)
{
    memcpy(s_cache, s_disk, sizeof(s_cache));
}

// ---- sistema falso ---------------------------------------------------------------------
static esp_reset_reason_t s_reason;
static int s_restarts;
esp_reset_reason_t esp_reset_reason(void)
{
    return s_reason;
}
void esp_restart(void)
{
    s_restarts++;
}

struct fw_timer {
    int unused;
};
static struct fw_timer s_timer;
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *out)
{
    (void)a;
    *out = &s_timer;
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us)
{
    (void)t;
    (void)us;
    return ESP_OK;
}
int64_t esp_timer_get_time(void)
{
    return 0;
}

static int s_tasks;
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *out)
{
    (void)name;
    (void)stack;
    (void)prio;
    s_tasks++;
    if (out) {
        *out = (TaskHandle_t)&s_tasks;   // el handle existe antes de que la tarea corra
    }
    fn(arg);
    return pdPASS;
}
void vTaskDelay(TickType_t t)
{
    (void)t;
}
void vTaskDelete(TaskHandle_t t)
{
    (void)t;
}

static int s_posts;
bool flex_inbox_post(flex_inbox_fn_t fn, void *arg)
{
    s_posts++;
    fn(arg);   // la tarea de UI lo ejecutaria en su siguiente vuelta
    return true;
}

static int s_fail_cb;
static void on_fail(void)
{
    s_fail_cb++;
}

int main(void)
{
    // 1) Bucle de caidas al levantar la interfaz: cada arranque cuenta y se cae
    //    ANTES del volcado diferido. El contador tiene que estar grabado al volver
    //    flex_safeboot_eval, o nunca llega a 3 y el Modo seguro no entra.
    s_reason = ESP_RST_PANIC;
    for (int boot = 1; boot <= FLEX_SAFE_FAIL_MAX; boot++) {
        power_cycle();
        flex_safeboot_eval();
        CHECK(disk_i32("fails", 0) == boot);   // grabado ya, no solo en la cache
        CHECK(disk_i32("cause", -1) == ESP_RST_PANIC);
        CHECK(flex_safe_mode() == (boot >= FLEX_SAFE_FAIL_MAX));
    }
    CHECK(flex_safe_mode());

    // 2) Arranque normal sin fallos previos: nada que escribir, ni volcado forzado
    memset(s_disk, 0, sizeof(s_disk));
    power_cycle();
    s_reason = ESP_RST_POWERON;
    int fl = s_flushes;
    flex_safeboot_eval();
    CHECK(s_flushes == fl);
    CHECK(!flex_safe_mode());

    // 3) "Reiniciar normalmente" sin poder grabar: NO reinicia y avisa a la UI
    s_reason = ESP_RST_PANIC;
    for (int boot = 1; boot <= FLEX_SAFE_FAIL_MAX; boot++) {
        power_cycle();
        flex_safeboot_eval();
    }
    CHECK(flex_safe_mode());
    s_flush_fails = true;
    int rs = s_restarts;
    CHECK(flex_safe_exit_and_reboot(on_fail));
    CHECK(s_restarts == rs && s_fail_cb == 1);
    CHECK(disk_i32("fails", 0) == FLEX_SAFE_FAIL_MAX);   // sigue grabado: el reintento es posible

    // 4) Reintento con el escritor libre: graba el 0 y reinicia; el siguiente
    //    arranque (por software) ya no entra en Modo seguro
    s_flush_fails = false;
    CHECK(flex_safe_exit_and_reboot(on_fail));
    CHECK(s_restarts == rs + 1 && s_fail_cb == 1);
    CHECK(disk_i32("fails", -1) == 0);
    power_cycle();
    s_reason = ESP_RST_SW;
    flex_safeboot_eval();
    CHECK(!flex_safe_mode());

    if (s_fail) {
        printf("boot_fw: %d fallos\n", s_fail);
        return 1;
    }
    printf("OK: arranque (Modo seguro: contador grabado antes de seguir, salida sin reinicio a ciegas)\n");
    return 0;
}
