#pragma once
#include <stdint.h>
#include "esp_system.h"
typedef enum { ESP_EXT1_WAKEUP_ALL_LOW=0, ESP_EXT1_WAKEUP_ANY_HIGH=1, ESP_EXT1_WAKEUP_ANY_LOW=2 } esp_sleep_ext1_wakeup_mode_t;
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED=0, ESP_SLEEP_WAKEUP_EXT1=4, ESP_SLEEP_WAKEUP_TIMER=5 } esp_sleep_source_t;
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t, esp_sleep_ext1_wakeup_mode_t);
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t, esp_sleep_ext1_wakeup_mode_t);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t);
esp_sleep_source_t esp_sleep_get_wakeup_cause();
void esp_deep_sleep_start();
