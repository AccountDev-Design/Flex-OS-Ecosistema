#pragma once
#include "esp_lcd_types.h"
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, int, const void*, size_t);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t, int, const void*, size_t);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t);
