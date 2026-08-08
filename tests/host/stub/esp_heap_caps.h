#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM  (1 << 10)
#define MALLOC_CAP_8BIT    (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
void*  heap_caps_malloc(size_t n, uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
