#pragma once
#include "FreeRTOS.h"
// La tarea se ejecuta ENTERA al crearla (fw_fake.c); vTaskDelete(NULL) vuelve.
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *out);
void vTaskDelay(TickType_t t);
void vTaskDelete(TaskHandle_t t);
