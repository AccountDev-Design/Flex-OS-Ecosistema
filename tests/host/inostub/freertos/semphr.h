#pragma once
#include "FreeRTOS.h"
typedef void* SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex();
// Mutex RECURSIVO: lo usa el candado del motor de trabajos (los caminos
// de coworkPump se anidan). El doble no simula bloqueo -- las pruebas
// corren en un hilo -- pero tiene que existir para que el sketch enlace.
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t);
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*);
void vSemaphoreDelete(SemaphoreHandle_t);
