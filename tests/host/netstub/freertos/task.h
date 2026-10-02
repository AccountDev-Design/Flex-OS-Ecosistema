#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
// No se crean hilos: la prueba ejecuta la vuelta de la tarea a mano.
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                       UBaseType_t prio, TaskHandle_t* handle);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* handle, BaseType_t core);
void vTaskDelete(TaskHandle_t t);
void vTaskDelay(TickType_t ticks);
// Notificaciones de tarea: la prueba cuenta los avisos (gNetTaskNotifies) y
// esperar solo avanza el reloj virtual.
BaseType_t xTaskNotifyGive(TaskHandle_t t);
uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticks);
extern unsigned gNetTaskNotifies;
// Registro de las tareas creadas (las pruebas comprueban nombre, nucleo y pila)
// y fallo programable: gNetTaskFail = N hace fallar las N proximas creaciones,
// como xTaskCreate cuando no queda un bloque contiguo de SRAM interna.
#include <string>
#include <vector>
struct NetTaskRec { std::string name; int core; uint32_t stack; };   // core -1 = sin fijar (tskNO_AFFINITY)
extern std::vector<NetTaskRec> gNetTasks;
extern unsigned gNetTaskFail;
