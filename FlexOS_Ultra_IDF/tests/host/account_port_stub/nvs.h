#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum { NVS_TYPE_U8 = 0x01, NVS_TYPE_U32 = 0x04, NVS_TYPE_STR = 0x21 } nvs_type_t;
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110C
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *v);
esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v);
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len);
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v);
esp_err_t nvs_find_key(nvs_handle_t h, const char *k, nvs_type_t *t);
esp_err_t nvs_erase_all(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
