#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE          ((BaseType_t)1)
#define pdFALSE         ((BaseType_t)0)
#define pdPASS          pdTRUE
#define portMAX_DELAY   ((TickType_t)0xFFFFFFFFu)
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
