#include "flex_ui.h"

#include <inttypes.h>
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "flex_core.h"
#include "flex_display.h"
#include "flex_display_lvgl.h"
#include "flex_display_lvgl_service.h"
#include "flex_i2c.h"
#include "flex_metrics.h"
#include "flex_touch.h"
#include "flex_touch_lvgl.h"
#include "flex_ui_diag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

// Techo del reposo entre vueltas: aunque LVGL no tenga timers pendientes, la
// tarea vuelve a tiempo para el watchdog y para las metricas.
#define UI_MAX_IDLE_MS      50
#define METRICS_LOG_EVERY   5

static const char *TAG = "flex.ui";

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool diag_get_touch(flex_diag_touch_t *out)
{
    flex_touch_frame_t f;
    if (!flex_touch_get_frame(&f)) {
        return false;
    }
    out->count = f.count > FLEX_DIAG_MAX_POINTS ? FLEX_DIAG_MAX_POINTS : f.count;
    for (uint8_t i = 0; i < out->count; i++) {
        out->pts[i].id = f.pts[i].id;
        out->pts[i].x = (int16_t)f.pts[i].x;
        out->pts[i].y = (int16_t)f.pts[i].y;
    }
    return true;
}

static void diag_get_sys(flex_diag_sys_t *out)
{
    memset(out, 0, sizeof(*out));
    out->heap_internal_free_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    out->heap_internal_min_kb = (uint32_t)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024);
    out->psram_free_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    const flex_boot_info_t *bi = flex_core_boot_info();
    out->chip_rev = bi->chip_rev;
    out->build_rev_min = bi->build_rev_min;
    out->build_rev_max = bi->build_rev_max;
    flex_touch_info_t ti;
    flex_touch_get_info(&ti);
    out->touch_present = ti.present;
    out->touch_addr = ti.addr;
    memcpy(out->touch_id, ti.product_id, sizeof(out->touch_id));
    out->touch_res_x = ti.cfg_res_x;
    out->touch_res_y = ti.cfg_res_y;
    out->touch_errors = ti.read_errors;
    flex_i2c_stats_t is;
    flex_i2c_get_stats(&is);
    out->i2c_errors = is.errors;
    out->i2c_recoveries = is.recoveries;
    out->i2c_wedged = is.wedged;
}

static void diag_set_brightness(uint8_t pct)
{
    flex_display_set_brightness(pct);
}

static flex_diag_ops_t s_diag_ops = {
    .get_metrics = flex_metrics_get,
    .get_touch = diag_get_touch,
    .get_sys = diag_get_sys,
    .set_brightness = diag_set_brightness,
};

static void log_metrics(void)
{
#if CONFIG_FLEX_DEBUG_METRICS
    static uint32_t windows;
    if (++windows % METRICS_LOG_EVERY) {
        return;
    }
    flex_metrics_t m;
    flex_metrics_get(&m);
    ESP_LOGI(TAG, "panel %.1f Hz | %.1f fps | render %.1f/%.1f ms | CPU UI %.0f%% | toque>pantalla %.1f/%.1f ms (n=%" PRIu32
             ") | vsync vencidas %" PRIu32,
             (double)m.panel_hz, (double)m.fps, (double)m.render_ms_avg, (double)m.render_ms_max,
             (double)m.ui_cpu_pct, (double)m.touch_shown_ms_avg, (double)m.touch_shown_ms_max, m.touch_samples,
             m.vsync_timeouts);
#endif
}

static void ui_task(void *arg)
{
    (void)arg;
    esp_err_t err = flex_display_init();
    if (err != ESP_OK) {
        // Sin pantalla no hay interfaz. No se reinicia en bucle (cada PANIC
        // escribe un volcado en la flash): se avisa por el log y se espera.
        for (;;) {
            ESP_LOGE(TAG, "la pantalla no arranco (%s); la interfaz queda detenida", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *disp = flex_display_lvgl_create();
    if (!disp) {
        for (;;) {
            ESP_LOGE(TAG, "no se pudo crear la pantalla de LVGL");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    if (!flex_touch_lvgl_create(disp)) {
        ESP_LOGW(TAG, "sin entrada tactil en LVGL");
    }
    s_diag_ops.initial_brightness = flex_display_get_brightness();
    flex_ui_diag_create(&s_diag_ops);
    ESP_LOGI(TAG, "interfaz LVGL %d.%d.%d en marcha (nucleo %d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH, xPortGetCoreID());

    bool wdt = esp_task_wdt_add(NULL) == ESP_OK;
    for (;;) {
        int64_t t0 = esp_timer_get_time();
        uint32_t next_ms = lv_timer_handler();
        int64_t t1 = esp_timer_get_time();
        flex_metrics_ui_busy((t1 - t0) - flex_display_lvgl_take_wait_us());
        if (flex_metrics_tick(t1, flex_display_vsync_count())) {
            log_metrics();
        }
        if (wdt) {
            esp_task_wdt_reset();
        }
        if (next_ms > UI_MAX_IDLE_MS) {
            next_ms = UI_MAX_IDLE_MS;
        }
        flex_display_lvgl_idle(next_ms ? next_ms : 1);
    }
}

esp_err_t flex_ui_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(ui_task, FLEX_TASK_UI_NAME, FLEX_TASK_UI_STACK, NULL, FLEX_TASK_UI_PRIO,
                                            NULL, FLEX_TASK_UI_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "no se pudo crear la tarea de UI");
    return ESP_OK;
}
