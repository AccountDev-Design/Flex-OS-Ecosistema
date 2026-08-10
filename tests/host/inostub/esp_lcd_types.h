#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_system.h"
typedef struct esp_lcd_panel_t*    esp_lcd_panel_handle_t;
typedef struct esp_lcd_panel_io_t* esp_lcd_panel_io_handle_t;
typedef enum { LCD_COLOR_PIXEL_FORMAT_RGB565 = 0, LCD_COLOR_PIXEL_FORMAT_RGB666, LCD_COLOR_PIXEL_FORMAT_RGB888 } lcd_color_rgb_pixel_format_t;
