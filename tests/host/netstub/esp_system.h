#pragma once
#include <stdint.h>
#include <stddef.h>
void esp_fill_random(void* buf, size_t len);
uint32_t esp_random();
uint32_t esp_get_free_heap_size();
