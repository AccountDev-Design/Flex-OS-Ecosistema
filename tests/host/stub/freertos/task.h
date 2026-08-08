#pragma once
#include "FreeRTOS.h"

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

// El simulador NO crea hilos: devuelve pdFALSE y deja el handle a NULL.
// Asi el codigo de la app se compila y se puede recorrer su ciclo de vida
// sin que una tarea de red real interfiera en las comprobaciones.
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name,
                                   uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* handle,
                                   BaseType_t core);
void vTaskDelete(TaskHandle_t t);
void vTaskDelay(TickType_t ticks);
