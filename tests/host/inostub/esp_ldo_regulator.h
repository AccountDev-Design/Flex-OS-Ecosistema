#pragma once
#include "esp_system.h"
typedef struct { int chan_id; int voltage_mv; struct { unsigned adjustable:1; unsigned owned_by_hw:1; } flags; } esp_ldo_channel_config_t;
typedef struct esp_ldo_channel_t* esp_ldo_channel_handle_t;
esp_err_t esp_ldo_acquire_channel(const esp_ldo_channel_config_t*, esp_ldo_channel_handle_t*);
esp_err_t esp_ldo_release_channel(esp_ldo_channel_handle_t);
