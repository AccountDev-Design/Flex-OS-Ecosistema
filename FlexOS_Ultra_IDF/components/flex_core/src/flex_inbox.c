#include "flex_inbox.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/queue.h"

typedef struct {
    flex_inbox_fn_t fn;
    void *arg;
} item_t;

static const char *TAG = "flex.inbox";
static QueueHandle_t s_q;
static TaskHandle_t s_ui;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_dropped;

esp_err_t flex_inbox_init(size_t depth, TaskHandle_t ui_task)
{
    if (s_q) {
        return ESP_OK;
    }
    s_q = xQueueCreate(depth, sizeof(item_t));
    ESP_RETURN_ON_FALSE(s_q, ESP_ERR_NO_MEM, TAG, "cola del buzon");
    s_ui = ui_task;
    return ESP_OK;
}

bool flex_inbox_post(flex_inbox_fn_t fn, void *arg)
{
    if (!s_q || !fn) {
        return false;
    }
    const item_t it = {fn, arg};
    if (xQueueSend(s_q, &it, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_mux);
        s_dropped++;
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    if (s_ui) {
        xTaskNotifyGive(s_ui);
    }
    return true;
}

size_t flex_inbox_drain(size_t max)
{
    size_t n = 0;
    item_t it;
    while (s_q && n < max && xQueueReceive(s_q, &it, 0) == pdTRUE) {
        it.fn(it.arg);
        n++;
    }
    return n;
}

uint32_t flex_inbox_dropped(void)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t n = s_dropped;
    portEXIT_CRITICAL(&s_mux);
    return n;
}
