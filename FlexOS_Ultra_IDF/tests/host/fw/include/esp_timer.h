// Pruebas de host: temporizadores que se crean pero no corren solos.
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct fw_timer *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *arg);
    void *arg;
    const char *name;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *out);
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us);
int64_t esp_timer_get_time(void);
