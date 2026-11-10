#pragma once
#include <stddef.h>
#include "esp_err.h"
esp_err_t flex_kvs_set_str(const char *ns, const char *key, const char *v);
void flex_kvs_get_str(const char *ns, const char *key, char *out, size_t cap, const char *def);
