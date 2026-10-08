#include "flex_bus.h"

#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "flex_core.h"
#include "freertos/FreeRTOS.h"

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

static const char *TAG = "flex.bus";
static esp_event_loop_handle_t s_loop;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_dropped;

esp_err_t flex_bus_init(void)
{
    if (s_loop) {
        return ESP_OK;
    }
    const esp_event_loop_args_t args = {
        .queue_size = 48,
        .task_name = FLEX_TASK_BUS_NAME,
        .task_priority = FLEX_TASK_BUS_PRIO,
        .task_stack_size = FLEX_TASK_BUS_STACK,
        .task_core_id = FLEX_TASK_BUS_CORE,
    };
    ESP_RETURN_ON_ERROR(esp_event_loop_create(&args, &s_loop), TAG, "bucle de eventos");
    return ESP_OK;
}

esp_err_t flex_bus_post(esp_event_base_t base, int32_t id, const void *data, size_t len)
{
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > FLEX_BUS_MAX_DATA) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = esp_event_post_to(s_loop, base, id, data, len, 0);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_mux);
        uint32_t n = ++s_dropped;
        portEXIT_CRITICAL(&s_mux);
        if (n == 1 || n % 100 == 0) {
            ESP_LOGW(TAG, "evento %s/%d descartado (%" PRIu32 " en total): %s", base, (int)id, n,
                     esp_err_to_name(err));
        }
    }
    return err;
}

esp_err_t flex_bus_subscribe(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg)
{
    ESP_RETURN_ON_FALSE(s_loop, ESP_ERR_INVALID_STATE, TAG, "bus sin iniciar");
    return esp_event_handler_register_with(s_loop, base, id, handler, arg);
}

esp_err_t flex_bus_unsubscribe(esp_event_base_t base, int32_t id, esp_event_handler_t handler)
{
    ESP_RETURN_ON_FALSE(s_loop, ESP_ERR_INVALID_STATE, TAG, "bus sin iniciar");
    return esp_event_handler_unregister_with(s_loop, base, id, handler);
}

uint32_t flex_bus_dropped(void)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t n = s_dropped;
    portEXIT_CRITICAL(&s_mux);
    return n;
}
