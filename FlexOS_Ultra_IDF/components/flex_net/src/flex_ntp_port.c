// Flex OS Ultra · hora por red: lo de la placa (reloj, NVS, tareas). NO PROBADO EN HARDWARE REAL: sin Wi-Fi (X3) el servicio no tiene red que usar.
#include "esp_log.h"
#include "esp_timer.h"
#include "flex_ntp.h"
#include "flex_safeboot.h"
#include "flex_storage.h"
#include "flex_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "flex.ntp";
#define NS_TIME "flexos_time"

// El escritorio o el bloqueo: la interfaz pone la suya fuerte (flex_shell); sin ella, siempre listo
__attribute__((weak)) bool flex_net_system_ready(void)
{
    return true;
}

static SemaphoreHandle_t s_mtx;
static TaskHandle_t s_task;

static uint32_t p_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint32_t p_utc(void) { return flex_time_utc(); }
static void p_set_utc(uint32_t utc, bool net) { flex_time_set_utc(utc, net); }
static bool p_synced(void) { return flex_time_synced(); }
static bool p_airplane(void) { return flex_cfg_get_bool("airpl", false); }   // la misma clave que Arduino (gAirplane) y el panel rapido
static bool p_ready(void) { return flex_net_system_ready(); }
static bool p_safe(void) { return flex_safe_mode(); }
static bool p_nvs_ok(void)
{
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    return st.nvs_ok;
}
static bool p_load(uint64_t *epoch, uint64_t *last)
{
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    if (!st.nvs_ok && !st.nvs_readonly) {
        return false;   // ni siquiera se puede leer: hora de fabrica
    }
    *epoch = flex_kvs_get_u64(NS_TIME, "epoch", 0);
    *last = flex_kvs_get_u64(NS_TIME, "lastsync", 0);
    return true;
}
static bool p_save(uint64_t epoch, uint64_t last)
{
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    if (!st.nvs_ok) {
        return false;
    }
    return flex_kvs_set_u64(NS_TIME, "epoch", epoch) == ESP_OK && flex_kvs_set_u64(NS_TIME, "lastsync", last) == ESP_OK;
}
static uint32_t p_query(const char *host) { return flex_ntp_udp_query(host, FLEX_NTP_PORT, (uint32_t)FLEX_NTP_TIMEOUT_MS); }
static void p_wake(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}
static void p_lock(void)
{
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

static const flex_ntp_env_t k_port = {
    .millis = p_millis, .utc = p_utc, .set_utc = p_set_utc, .synced = p_synced, .airplane = p_airplane, .system_ready = p_ready, .safe_mode = p_safe,
    .nvs_ok = p_nvs_ok, .load = p_load, .save = p_save, .query = p_query, .wake = p_wake, .lock = p_lock, .unlock = p_unlock,
};

const flex_ntp_env_t *flex_ntp_port_env(void)
{
    return &k_port;
}

// El trabajador (baja prioridad): cada segundo decide cuando hay que preguntar y, si toca, pregunta (hasta 12 s por servidor caido). La interfaz solo
// pone banderas y lo despierta: nunca espera a la red.
static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        flex_ntp_tick_service();
        if (flex_ntp_work_pending()) {
            flex_ntp_work();
        }
    }
}

void flex_ntp_start(void)
{
    if (s_task) {
        return;
    }
    s_mtx = xSemaphoreCreateMutex();
    if (xTaskCreatePinnedToCore(worker, "flex.ntp", 5120, NULL, 1, &s_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "no se pudo crear la tarea de la hora por red");
        s_task = NULL;
    }
}
