#pragma once
#include "FreeRTOS.h"

typedef void* SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t        xSemaphoreTake(SemaphoreHandle_t s, TickType_t t);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t s);
void              vSemaphoreDelete(SemaphoreHandle_t s);
