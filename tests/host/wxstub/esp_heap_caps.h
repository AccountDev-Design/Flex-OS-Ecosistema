#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM  (1 << 10)
#define MALLOC_CAP_8BIT    (1 << 2)
// El buffer de respuesta se pide a PSRAM y, si no hay, al heap normal. En el
// PC las dos rutas son malloc: lo que se prueba es que se libera y se reutiliza.
static inline void* heap_caps_malloc(size_t n, uint32_t){ return malloc(n); }
static inline void  heap_caps_free(void* p){ free(p); }
