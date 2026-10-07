#include "flex_touch.h"

#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "flex_board.h"
#include "flex_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gt911.h"

#define POLL_MS               8     // sin INT: sondeo. ~125 lecturas/s, <1 ms de bus cada una
#define BACKOFF_MS            100   // con fallos seguidos: menos trafico y menos log
#define BACKOFF_AFTER         3
#define CHIP_RESET_AFTER      8     // fallos seguidos que piden reiniciar el GT911
#define CHIP_RESET_GAP_US     (1000 * 1000)
#define SEARCH_RETRY_MS       2000

static const char *TAG = "flex.touch";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static flex_touch_frame_t s_frame;
static bool s_have_frame;
static flex_touch_info_t s_info;
static TaskHandle_t s_task;

static void publish_info(const gt911_t *gt, bool present)
{
    portENTER_CRITICAL(&s_mux);
    s_info.present = present;
    s_info.addr = gt->addr;
    memcpy(s_info.product_id, gt->product_id, sizeof(s_info.product_id));
    s_info.fw_version = gt->fw_version;
    s_info.cfg_res_x = gt->res_x;
    s_info.cfg_res_y = gt->res_y;
    portEXIT_CRITICAL(&s_mux);
}

static void wait_ms(uint32_t ms)
{
    // Esperas largas en trozos para no hacer saltar el watchdog de tareas.
    while (ms) {
        uint32_t step = ms > 500 ? 500 : ms;
        vTaskDelay(pdMS_TO_TICKS(step));
        esp_task_wdt_reset();
        ms -= step;
    }
}

static void touch_task(void *arg)
{
    (void)arg;
    bool wdt = esp_task_wdt_add(NULL) == ESP_OK;
    gt911_t gt = {0};
    bool present = false;
    uint32_t fails = 0;
    int64_t last_chip_reset_us = 0;
    uint32_t seq = 0;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        if (!present) {
            if (gt911_reset_and_find(&gt) == ESP_OK) {
                present = true;
                fails = 0;
                publish_info(&gt, true);
            } else {
                ESP_LOGW(TAG, "GT911 no detectado (0x5D/0x14); reintento en %d ms", SEARCH_RETRY_MS);
                publish_info(&gt, false);
                wait_ms(SEARCH_RETRY_MS);
            }
            last_wake = xTaskGetTickCount();
            continue;
        }

        flex_touch_frame_t f = {0};
        bool fresh = false;
        esp_err_t err = gt911_poll(&gt, &f, &fresh);
        if (fresh) {
            f.t_read_us = esp_timer_get_time();
            f.seq = ++seq;
            portENTER_CRITICAL(&s_mux);
            s_frame = f;
            s_have_frame = true;
            s_info.frames++;
            portEXIT_CRITICAL(&s_mux);
        }
        if (err == ESP_OK) {
            fails = 0;
        } else {
            fails++;
            portENTER_CRITICAL(&s_mux);
            s_info.read_errors++;
            portEXIT_CRITICAL(&s_mux);
            // El bus ya se recupera solo en flex_i2c. Si aun asi el GT911 no
            // vuelve, se le da su pulso de reset (como la version Arduino) y se
            // le busca en sus dos direcciones. Nunca se rinde.
            int64_t now = esp_timer_get_time();
            if (fails >= CHIP_RESET_AFTER && now - last_chip_reset_us >= CHIP_RESET_GAP_US) {
                last_chip_reset_us = now;
                portENTER_CRITICAL(&s_mux);
                s_info.chip_resets++;
                portEXIT_CRITICAL(&s_mux);
                if (gt911_reset_and_find(&gt) == ESP_OK) {
                    fails = 0;
                    publish_info(&gt, true);
                }
            }
        }
        if (wdt) {
            esp_task_wdt_reset();
        }
        uint32_t period = fails >= BACKOFF_AFTER ? BACKOFF_MS : POLL_MS;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(period));
    }
}

esp_err_t flex_touch_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    const gpio_config_t rst = {
        .pin_bit_mask = 1ULL << FLEX_PIN_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "GPIO de reset del GT911");
    gpio_set_level(FLEX_PIN_TP_RST, 1);

    BaseType_t ok = xTaskCreatePinnedToCore(touch_task, FLEX_TASK_TOUCH_NAME, FLEX_TASK_TOUCH_STACK, NULL,
                                            FLEX_TASK_TOUCH_PRIO, &s_task, FLEX_TASK_TOUCH_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "no se pudo crear la tarea del tactil");
    return ESP_OK;
}

bool flex_touch_get_frame(flex_touch_frame_t *out)
{
    portENTER_CRITICAL(&s_mux);
    bool have = s_have_frame;
    if (have) {
        *out = s_frame;
    }
    portEXIT_CRITICAL(&s_mux);
    return have;
}

void flex_touch_get_info(flex_touch_info_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_info;
    portEXIT_CRITICAL(&s_mux);
}
