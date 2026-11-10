#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY 0xFFFFFFFFu
#define pdPASS 1
#define pdTRUE 1
