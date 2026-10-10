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
static gt911_t s_gt;   // un solo alta en el bus: la usan el filtro de encendido y la tarea

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

// Publica "ningun dedo" si lo ultimo publicado era un toque. Se usa cuando los
// datos del GT911 dejan de ser fiables (lecturas fallidas seguidas o reinicio
// del chip, que ademas tira el cuadro de "dedo levantado" que tuviera pendiente):
// sin esto LVGL veria el dedo pulsado para siempre.
static void publish_release(uint32_t *seq)
{
    portENTER_CRITICAL(&s_mux);
    if (s_have_frame && s_frame.count) {
        s_frame.count = 0;
        s_frame.seq = ++*seq;
        s_frame.t_read_us = esp_timer_get_time();
    }
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
    bool present = false;
    uint32_t fails = 0;
    int64_t last_chip_reset_us = 0;
    uint32_t seq = 0;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        if (!present) {
            if (gt911_reset_and_find(&s_gt) == ESP_OK) {
                present = true;
                fails = 0;
                publish_info(&s_gt, true);
            } else {
                ESP_LOGW(TAG, "GT911 no detectado (0x5D/0x14); reintento en %d ms", SEARCH_RETRY_MS);
                publish_info(&s_gt, false);
                wait_ms(SEARCH_RETRY_MS);
            }
            last_wake = xTaskGetTickCount();
            continue;
        }

        flex_touch_frame_t f = {0};
        bool fresh = false;
        esp_err_t err = gt911_poll(&s_gt, &f, &fresh);
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
            if (fails == BACKOFF_AFTER) {
                publish_release(&seq);
            }
            // El bus ya se recupera solo en flex_i2c. Si aun asi el GT911 no
            // vuelve, se le da su pulso de reset (como la version Arduino) y se
            // le busca en sus dos direcciones. Nunca se rinde.
            int64_t now = esp_timer_get_time();
            if (fails >= CHIP_RESET_AFTER && now - last_chip_reset_us >= CHIP_RESET_GAP_US) {
                last_chip_reset_us = now;
                portENTER_CRITICAL(&s_mux);
                s_info.chip_resets++;
                portEXIT_CRITICAL(&s_mux);
                bool found = gt911_reset_and_find(&s_gt) == ESP_OK;
                publish_release(&seq);
                if (found) {
                    fails = 0;
                    publish_info(&s_gt, true);
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
    // El apagado completo lo deja retenido en alto (flex_power): sin soltarlo, el
    // pulso de reset del GT911 no llegaria al chip. Ya en alto: sin glitch.
    gpio_hold_dis(FLEX_PIN_TP_RST);

    BaseType_t ok = xTaskCreatePinnedToCore(touch_task, FLEX_TASK_TOUCH_NAME, FLEX_TASK_TOUCH_STACK, NULL,
                                            FLEX_TASK_TOUCH_PRIO, &s_task, FLEX_TASK_TOUCH_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "no se pudo crear la tarea del tactil");
    return ESP_OK;
}

// ---- filtro de encendido (antes de flex_touch_start) ------------------------------------
static int64_t s_gate_finger_us;
static uint8_t s_gate_count;

esp_err_t flex_touch_gate_open(void)
{
    if (s_task) {
        return ESP_ERR_INVALID_STATE;
    }
    return gt911_find(&s_gt);
}

int flex_touch_gate_fingers(void)
{
    if (s_task || !s_gt.dev) {
        return -1;
    }
    flex_touch_frame_t f = {0};
    bool fresh = false;
    if (gt911_poll(&s_gt, &f, &fresh) != ESP_OK) {
        return -1;
    }
    int64_t now = esp_timer_get_time();
    if (fresh) {
        s_gate_count = f.count;
        s_gate_finger_us = now;
    }
    // Sin cuadro nuevo el chip no tiene nada que contar: vale el ultimo mientras
    // sea reciente (Power.h:1074, 120 ms).
    return now - s_gate_finger_us > FLEX_TOUCH_GATE_STALE_US ? 0 : s_gate_count;
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
