#pragma once
#include "esp_lcd_types.h"
typedef struct esp_lcd_dsi_bus_t* esp_lcd_dsi_bus_handle_t;
typedef enum { MIPI_DSI_PHY_CLK_SRC_DEFAULT = 0 } mipi_dsi_phy_clock_source_t;
typedef enum { MIPI_DSI_DPI_CLK_SRC_DEFAULT = 0 } mipi_dsi_dpi_clock_source_t;
typedef struct { int bus_id; uint8_t num_data_lanes; mipi_dsi_phy_clock_source_t phy_clk_src; uint32_t lane_bit_rate_mbps; } esp_lcd_dsi_bus_config_t;
typedef struct { uint8_t virtual_channel; int lcd_cmd_bits; int lcd_param_bits; } esp_lcd_dbi_io_config_t;
typedef struct {
  uint32_t h_size, v_size;
  uint32_t hsync_pulse_width, hsync_back_porch, hsync_front_porch;
  uint32_t vsync_pulse_width, vsync_back_porch, vsync_front_porch;
} esp_lcd_video_timing_t;
typedef struct {
  uint8_t virtual_channel;
  mipi_dsi_dpi_clock_source_t dpi_clk_src;
  uint32_t dpi_clock_freq_mhz;
  lcd_color_rgb_pixel_format_t pixel_format;
  uint8_t num_fbs;
  esp_lcd_video_timing_t video_timing;
  struct { unsigned use_dma2d:1; unsigned disable_lp:1; } flags;
} esp_lcd_dpi_panel_config_t;
typedef struct { void* color_buffer; int flushed_y1, flushed_y2; } esp_lcd_dpi_panel_event_data_t;
typedef bool (*esp_lcd_dpi_panel_color_trans_done_cb_t)(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void*);
typedef struct { esp_lcd_dpi_panel_color_trans_done_cb_t on_color_trans_done; void* on_refresh_done; } esp_lcd_dpi_panel_event_callbacks_t;
esp_err_t esp_lcd_new_dsi_bus(const esp_lcd_dsi_bus_config_t*, esp_lcd_dsi_bus_handle_t*);
esp_err_t esp_lcd_new_panel_io_dbi(esp_lcd_dsi_bus_handle_t, const esp_lcd_dbi_io_config_t*, esp_lcd_panel_io_handle_t*);
esp_err_t esp_lcd_new_panel_dpi(esp_lcd_dsi_bus_handle_t, const esp_lcd_dpi_panel_config_t*, esp_lcd_panel_handle_t*);
esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t, const esp_lcd_dpi_panel_event_callbacks_t*, void*);
