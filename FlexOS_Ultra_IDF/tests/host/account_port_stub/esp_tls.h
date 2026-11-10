#pragma once
#include "esp_err.h"
typedef struct esp_tls_error_handle *esp_tls_error_handle_t;
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h, int *esp_tls_code, int *esp_tls_flags);
