// Dobles de host para compilar flex_ota_port.c SIN ESP-IDF: solo lo que ese archivo usa. La conducta (lo que contesta cada funcion) la guioniza test_ota_port.c.
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_HTTP_EAGAIN 0x7005
#define ESP_ERR_HTTP_CONNECT 0x7001
