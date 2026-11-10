#pragma once
#include <stdint.h>
#include "FreeRTOS.h"
typedef struct acct_stub_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
void vTaskDelay(TickType_t t);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *name, uint32_t stack, void *arg, unsigned prio, TaskHandle_t *out, int core);
