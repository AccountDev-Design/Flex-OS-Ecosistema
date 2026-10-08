// Simulador del PC: tipos de esp_event para compilar flex_bus.h.
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);
#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t const id = #id
