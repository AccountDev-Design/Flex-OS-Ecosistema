#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM   (1u << 10)
#define MALLOC_CAP_8BIT     (1u << 2)
#define MALLOC_CAP_INTERNAL (1u << 11)
void*  heap_caps_malloc(size_t n, uint32_t caps);
void   heap_caps_free(void* p);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
