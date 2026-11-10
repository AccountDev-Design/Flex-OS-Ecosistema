#pragma once
#include "FreeRTOS.h"
typedef struct ota_stub_mutex *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t m, TickType_t t);
int xSemaphoreGive(SemaphoreHandle_t m);
