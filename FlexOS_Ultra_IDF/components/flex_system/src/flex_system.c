#include "flex_system.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "driver/temperature_sensor.h"
#include "esp_check.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "flex_bus.h"
#include "flex_core.h"
#include "flex_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAMPLE_MS            2000
#define LOW_INTERNAL_BYTES   (48 * 1024)
#define HOT_C                80.0f
#define CARE_NS              "flexcare"
#define BOOTS_KEY            "idfboots"   // clave propia: no pisa las de la version Arduino

static const char *TAG = "flex.system";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static flex_sys_snapshot_t s_snap;
static flex_sys_snapshot_t s_work;   // la tarea compone aqui y copia bajo el cerrojo
static temperature_sensor_handle_t s_tsens;
static TaskStatus_t *s_tasks;
static UBaseType_t s_tasks_cap;
static configRUN_TIME_COUNTER_TYPE s_prev_total;
static configRUN_TIME_COUNTER_TYPE s_prev_idle[2];

typedef struct {
    TaskHandle_t h;
    configRUN_TIME_COUNTER_TYPE rt;
} prev_rt_t;
static prev_rt_t *s_prev_rt;
static size_t s_prev_n;

static const char *reset_reason_text(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "Encendido";
    case ESP_RST_EXT:       return "Pin externo";
    case ESP_RST_SW:        return "Reinicio por software";
    case ESP_RST_PANIC:     return "Fallo (PANIC)";
    case ESP_RST_INT_WDT:   return "Watchdog de interrupciones";
    case ESP_RST_TASK_WDT:  return "Watchdog de tareas";
    case ESP_RST_WDT:       return "Watchdog";
    case ESP_RST_DEEPSLEEP: return "Salida de suspension profunda";
    case ESP_RST_BROWNOUT:  return "Caida de tension";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    case ESP_RST_EFUSE:     return "Error de eFuse";
    case ESP_RST_PWR_GLITCH:return "Glitch de alimentacion";
    case ESP_RST_CPU_LOCKUP:return "CPU bloqueada";
    default:                return "Desconocido";
    }
}

static configRUN_TIME_COUNTER_TYPE prev_runtime(TaskHandle_t h)
{
    for (size_t i = 0; i < s_prev_n; i++) {
        if (s_prev_rt[i].h == h) {
            return s_prev_rt[i].rt;
        }
    }
    return 0;
}

static bool prev_known(TaskHandle_t h)
{
    for (size_t i = 0; i < s_prev_n; i++) {
        if (s_prev_rt[i].h == h) {
            return true;
        }
    }
    return false;
}

static void sample_tasks(flex_sys_snapshot_t *w)
{
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n + 4 > s_tasks_cap) {
        free(s_tasks);
        free(s_prev_rt);
        s_tasks_cap = n + 8;
        s_tasks = calloc(s_tasks_cap, sizeof(TaskStatus_t));
        s_prev_rt = calloc(s_tasks_cap, sizeof(prev_rt_t));
        s_prev_n = 0;
        if (!s_tasks || !s_prev_rt) {
            s_tasks_cap = 0;
            return;
        }
    }
    configRUN_TIME_COUNTER_TYPE total = 0;
    n = uxTaskGetSystemState(s_tasks, s_tasks_cap, &total);
    configRUN_TIME_COUNTER_TYPE dt = total - s_prev_total;
    w->ntasks = 0;
    configRUN_TIME_COUNTER_TYPE idle[2] = {0, 0};
    for (UBaseType_t i = 0; i < n; i++) {
        TaskStatus_t *t = &s_tasks[i];
        configRUN_TIME_COUNTER_TYPE d = t->ulRunTimeCounter - prev_runtime(t->xHandle);
        BaseType_t core = t->xCoreID;
        if (strncmp(t->pcTaskName, "IDLE", 4) == 0 && core >= 0 && core < 2) {
            idle[core] = t->ulRunTimeCounter;
        }
        if (w->ntasks < FLEX_SYS_MAX_TASKS) {
            flex_sys_task_t *o = &w->tasks[w->ntasks++];
            strlcpy(o->name, t->pcTaskName, sizeof(o->name));
            o->stack_free = (uint32_t)t->usStackHighWaterMark * sizeof(StackType_t);
            o->prio = (uint8_t)t->uxCurrentPriority;
            o->core = (core == tskNO_AFFINITY) ? -1 : (int8_t)core;
            // Sin muestra anterior de esta tarea (recien creada o tras crecer la
            // tabla) no hay ventana: 0 en vez de todo su tiempo acumulado.
            bool known = prev_known(t->xHandle);
            float pct = (known && dt && s_prev_total) ? 100.0f * (float)d / (float)dt / 2.0f : 0.0f;
            o->cpu_pct = pct > 100.0f ? 100.0f : pct;
        }
    }
    for (int c = 0; c < 2; c++) {
        configRUN_TIME_COUNTER_TYPE di = idle[c] - s_prev_idle[c];
        w->cpu_load[c] = (dt && s_prev_total) ? 100.0f - 100.0f * (float)di / (float)dt : 0.0f;
        if (w->cpu_load[c] < 0) {
            w->cpu_load[c] = 0;
        }
        s_prev_idle[c] = idle[c];
    }
    s_prev_total = total;
    s_prev_n = 0;
    for (UBaseType_t i = 0; i < n && i < s_tasks_cap; i++) {
        s_prev_rt[s_prev_n++] = (prev_rt_t){s_tasks[i].xHandle, s_tasks[i].ulRunTimeCounter};
    }
}

static void sample(void)
{
    flex_sys_snapshot_t *w = &s_work;
    w->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    w->heap_int_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    w->heap_int_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    w->heap_int_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    w->heap_psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    w->heap_psram_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    w->heap_psram_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    w->psram_total = esp_psram_is_initialized() ? (uint32_t)esp_psram_get_size() : 0;
    w->temp_ok = s_tsens && temperature_sensor_get_celsius(s_tsens, &w->temp_c) == ESP_OK;
    sample_tasks(w);
    w->samples++;
    portENTER_CRITICAL(&s_mux);
    s_snap = *w;
    portEXIT_CRITICAL(&s_mux);

    if (w->heap_int_free < LOW_INTERNAL_BYTES) {
        flex_bus_post(FLEX_EV_SYSTEM, FLEX_SYS_EV_LOW_INTERNAL_RAM, &w->heap_int_free, sizeof(uint32_t));
    }
    if (w->temp_ok && w->temp_c >= HOT_C) {
        flex_bus_post(FLEX_EV_SYSTEM, FLEX_SYS_EV_HOT, &w->temp_c, sizeof(float));
    }
}

static void system_task(void *arg)
{
    (void)arg;
    bool wdt = esp_task_wdt_add(NULL) == ESP_OK;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        sample();
        if (wdt) {
            esp_task_wdt_reset();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_MS));
    }
}

static void read_crash(flex_sys_snapshot_t *w)
{
    if (esp_core_dump_image_check() != ESP_OK) {
        return;
    }
    esp_core_dump_summary_t *sum = malloc(sizeof(*sum));
    if (!sum) {
        return;
    }
    if (esp_core_dump_get_summary(sum) == ESP_OK) {
        w->crash_present = true;
        strlcpy(w->crash_task, sum->exc_task, sizeof(w->crash_task));
        w->crash_pc = sum->exc_pc;
        w->crash_cause = sum->ex_info.mcause;
        if (esp_core_dump_get_panic_reason(w->crash_reason, sizeof(w->crash_reason)) != ESP_OK) {
            w->crash_reason[0] = '\0';
        }
        ESP_LOGW(TAG, "hay un fallo guardado: tarea '%s', PC 0x%08" PRIx32 ", mcause %" PRIu32 " (%s)", w->crash_task,
                 w->crash_pc, w->crash_cause, w->crash_reason);
    }
    free(sum);
}

esp_err_t flex_system_start(void)
{
    memset(&s_work, 0, sizeof(s_work));
    strlcpy(s_work.reset_reason, reset_reason_text(esp_reset_reason()), sizeof(s_work.reset_reason));
    uint32_t boots = flex_kvs_get_u32(CARE_NS, BOOTS_KEY, 0) + 1;
    flex_kvs_set_u32(CARE_NS, BOOTS_KEY, boots);
    s_work.boot_count = boots;
    read_crash(&s_work);
    if (s_work.crash_present) {
        flex_bus_post(FLEX_EV_SYSTEM, FLEX_SYS_EV_CRASH_FOUND, NULL, 0);
    }

    const temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    if (temperature_sensor_install(&tcfg, &s_tsens) != ESP_OK || temperature_sensor_enable(s_tsens) != ESP_OK) {
        ESP_LOGW(TAG, "sensor de temperatura no disponible");
        s_tsens = NULL;
    }
    portENTER_CRITICAL(&s_mux);
    s_snap = s_work;
    portEXIT_CRITICAL(&s_mux);

    BaseType_t ok = xTaskCreatePinnedToCore(system_task, FLEX_TASK_SYSTEM_NAME, FLEX_TASK_SYSTEM_STACK, NULL,
                                            FLEX_TASK_SYSTEM_PRIO, NULL, FLEX_TASK_SYSTEM_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea de sistema");
    return ESP_OK;
}

void flex_system_get(flex_sys_snapshot_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_snap;
    portEXIT_CRITICAL(&s_mux);
}
