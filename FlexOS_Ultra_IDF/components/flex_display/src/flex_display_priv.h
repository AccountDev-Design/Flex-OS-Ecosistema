// Estado compartido entre el arranque del panel y el puerto de LVGL.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Vive en RAM interna (variable estatica en .bss de DRAM): con
// CONFIG_LCD_DSI_ISR_CACHE_SAFE el driver exige que el contexto de la ISR no
// este en PSRAM.
typedef struct {
    SemaphoreHandle_t fb_done;   // la ISR lo da solo si habia un cambio de FB armado
    volatile uint32_t vsync_count;
    volatile bool     flip_armed;
    volatile int64_t  shown_us;  // instante del fin de cuadro que estreno el FB nuevo
} flex_dsi_ctx_t;

extern flex_dsi_ctx_t g_flex_dsi;

esp_lcd_panel_handle_t flex_display_panel(void);
void flex_display_get_fbs(void **fb0, void **fb1);
