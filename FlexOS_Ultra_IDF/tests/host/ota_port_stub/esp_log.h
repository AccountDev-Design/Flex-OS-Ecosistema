#pragma once
#include <stdarg.h>
void ota_stub_log(int level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGE(t, f, ...) ota_stub_log(3, t, f, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) ota_stub_log(2, t, f, ##__VA_ARGS__)
#define ESP_LOGI(t, f, ...) ota_stub_log(1, t, f, ##__VA_ARGS__)
const char *esp_err_to_name(int e);
