#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, BaseType_t);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(TickType_t);
TaskHandle_t xTaskGetCurrentTaskHandle();
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
BaseType_t xTaskNotifyGive(TaskHandle_t);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
