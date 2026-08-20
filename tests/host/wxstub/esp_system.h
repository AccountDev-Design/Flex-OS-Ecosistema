#pragma once
#include <stdint.h>
// Heap libre "de sobra": el filtro de memoria del modulo no debe disparar
// en las pruebas de parseo.
static inline uint32_t esp_get_free_heap_size(){ return 200000; }
