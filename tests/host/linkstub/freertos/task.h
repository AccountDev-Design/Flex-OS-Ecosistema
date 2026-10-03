#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
// Hilos de verdad (std::thread), como las tareas del P4.
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack,
                                   void* arg, UBaseType_t prio, TaskHandle_t* handle, BaseType_t core);
void vTaskDelete(TaskHandle_t t);
void vTaskDelay(TickType_t ticks);
