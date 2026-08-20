#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
// El simulador NO crea hilos: la tarea de red no corre y las pruebas llaman
// directamente a las funciones de parseo, que es lo que se quiere verificar.
static inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t,
                                                 void*, UBaseType_t, TaskHandle_t* h, BaseType_t){
  if(h) *h = nullptr;
  return pdPASS;
}
static inline void vTaskDelay(TickType_t){}
