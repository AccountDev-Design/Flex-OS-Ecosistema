#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_http_client *esp_http_client_handle_t;
typedef struct {
    const char *url;
    const char *cert_pem;
    int timeout_ms;
    int buffer_size;
    int buffer_size_tx;
    bool keep_alive_enable;
    int max_redirection_count;
    bool skip_cert_common_name_check;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t c);
esp_err_t esp_http_client_close(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c, int ms);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len);
