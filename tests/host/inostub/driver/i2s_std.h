#pragma once
// Doble de la API I2S "std" de ESP-IDF 5.x: SOLO tipos y firmas, para que
// FlexOS_Audio.cpp (el driver del ES8311 que va a la placa) se compile en el
// PC. Aqui no hay DMA ni pines: ninguna prueba ejecuta estas funciones.
#include <stdint.h>
#include <stddef.h>
#include "esp_system.h"
#include "driver/gpio.h"

typedef struct i2s_channel_obj_t* i2s_chan_handle_t;
typedef enum { I2S_NUM_0 = 0, I2S_NUM_1 = 1 } i2s_port_t;
typedef enum { I2S_ROLE_MASTER = 0, I2S_ROLE_SLAVE = 1 } i2s_role_t;
typedef enum { I2S_DATA_BIT_WIDTH_8BIT = 8, I2S_DATA_BIT_WIDTH_16BIT = 16,
               I2S_DATA_BIT_WIDTH_24BIT = 24, I2S_DATA_BIT_WIDTH_32BIT = 32 } i2s_data_bit_width_t;
typedef enum { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 } i2s_slot_mode_t;

typedef struct {
  i2s_port_t id;
  i2s_role_t role;
  uint32_t   dma_desc_num;
  uint32_t   dma_frame_num;
  bool       auto_clear;
  int        intr_priority;
} i2s_chan_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(num, rl) { (num), (rl), 6, 240, false, 0 }

typedef struct { uint32_t sample_rate_hz; int clk_src; int mclk_multiple; } i2s_std_clk_config_t;
typedef struct { i2s_data_bit_width_t data_bit_width; int slot_bit_width; i2s_slot_mode_t slot_mode;
                 int slot_mask; uint32_t ws_width; bool ws_pol; bool bit_shift; } i2s_std_slot_config_t;
typedef struct { int mclk_inv, bclk_inv, ws_inv; } i2s_std_gpio_invert_t;
typedef struct { gpio_num_t mclk, bclk, ws, dout, din; i2s_std_gpio_invert_t invert_flags; } i2s_std_gpio_config_t;
typedef struct { i2s_std_clk_config_t clk_cfg; i2s_std_slot_config_t slot_cfg; i2s_std_gpio_config_t gpio_cfg; } i2s_std_config_t;

#define I2S_STD_CLK_DEFAULT_CONFIG(rate) { (rate), 0, 256 }
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) { (bits), 0, (mode), 3, (uint32_t)(bits), false, true }

esp_err_t i2s_new_channel(const i2s_chan_config_t* cfg, i2s_chan_handle_t* tx, i2s_chan_handle_t* rx);
esp_err_t i2s_del_channel(i2s_chan_handle_t h);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h, const i2s_std_config_t* cfg);
esp_err_t i2s_channel_enable(i2s_chan_handle_t h);
esp_err_t i2s_channel_disable(i2s_chan_handle_t h);
esp_err_t i2s_channel_write(i2s_chan_handle_t h, const void* src, size_t size, size_t* written, uint32_t timeout_ms);
