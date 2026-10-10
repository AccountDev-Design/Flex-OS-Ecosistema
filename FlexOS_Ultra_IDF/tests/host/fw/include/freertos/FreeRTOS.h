// Pruebas de host: lo minimo de FreeRTOS que usa el codigo de firmware probado.
#pragma once
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
