#include "flex_display.h"

#include <inttypes.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flex_board.h"
#include "flex_display_priv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "st7701_init.h"

#define BL_TIMER        LEDC_TIMER_0
#define BL_CHANNEL      LEDC_CHANNEL_0
#define BL_MODE         LEDC_LOW_SPEED_MODE
#define BL_RES          LEDC_TIMER_10_BIT
#define BL_DUTY_MAX     ((1U << 10) - 1U)
#define BL_MIN_PCT      5

static const char *TAG = "flex.display";

flex_dsi_ctx_t g_flex_dsi;

static esp_ldo_channel_handle_t s_ldo;
static esp_lcd_dsi_bus_handle_t s_bus;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static void *s_fb[2];
static uint8_t s_brightness = CONFIG_FLEX_BACKLIGHT_DEFAULT_PCT;
static bool s_bl_ready;
static bool s_bl_on;

// Fin de cada cuadro completo del DMA (driver: mipi_dsi_dma_trans_done_cb).
// Cuando se llama, el DMA ya se ha relanzado sobre el framebuffer elegido por
// el ultimo draw_bitmap, y el anterior se ha leido entero. Corre en el nucleo
// de la UI, asi que si ve flip_armed es que empezo despues de que la UI
// guardara el indice nuevo: este cuadro ya sale del framebuffer nuevo.
static bool IRAM_ATTR on_fb_complete(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata,
                                     void *user_ctx)
{
    (void)panel;
    (void)edata;   // el driver siempre pasa NULL
    flex_dsi_ctx_t *ctx = (flex_dsi_ctx_t *)user_ctx;
    ctx->vsync_count++;
    if (!ctx->flip_armed) {
        return false;
    }
    ctx->shown_us = esp_timer_get_time();
    ctx->flip_armed = false;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(ctx->fb_done, &hp);
    return hp == pdTRUE;
}

static uint32_t pct_to_duty(uint8_t pct)
{
    // Misma curva que la version Arduino (map(pct, 0, 100, 25, 255) en 8 bits),
    // llevada a 10 bits: el 5 % sigue siendo visible.
    uint32_t duty8 = 25U + (uint32_t)pct * (255U - 25U) / 100U;
    return duty8 * BL_DUTY_MAX / 255U;
}

static void bl_write(uint32_t duty)
{
    if (!s_bl_ready) {
        // Sin PWM: encendido fijo por GPIO, como el respaldo de la version Arduino.
        gpio_set_level(FLEX_PIN_LCD_BL, duty ? 1 : 0);
        return;
    }
    ledc_set_duty(BL_MODE, BL_CHANNEL, duty);
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t t = {
        .speed_mode = BL_MODE,
        .duty_resolution = BL_RES,
        .timer_num = BL_TIMER,
        .freq_hz = FLEX_BL_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "temporizador LEDC del brillo");
    const ledc_channel_config_t c = {
        .gpio_num = FLEX_PIN_LCD_BL,
        .speed_mode = BL_MODE,
        .channel = BL_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&c), TAG, "canal LEDC del brillo");
    s_bl_ready = true;
    return ESP_OK;
}

void flex_display_set_brightness(uint8_t pct)
{
    if (pct < BL_MIN_PCT) {
        pct = BL_MIN_PCT;
    }
    if (pct > 100) {
        pct = 100;
    }
    s_brightness = pct;
    if (s_bl_on) {
        bl_write(pct_to_duty(pct));
    }
}

uint8_t flex_display_get_brightness(void)
{
    return s_brightness;
}

void flex_display_backlight_enable(bool on)
{
    s_bl_on = on;
    bl_write(on ? pct_to_duty(s_brightness) : 0);
}

uint32_t flex_display_vsync_count(void)
{
    return g_flex_dsi.vsync_count;
}

esp_lcd_panel_handle_t flex_display_panel(void)
{
    return s_panel;
}

void flex_display_get_fbs(void **fb0, void **fb1)
{
    *fb0 = s_fb[0];
    *fb1 = s_fb[1];
}

static void panel_hw_reset(void)
{
    // Tiempos de la version Arduino, probados en esta placa.
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << FLEX_PIN_LCD_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(FLEX_PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(FLEX_PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(FLEX_PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

// tx_param por DBI no informa de errores (el driver devuelve ESP_OK siempre) y
// no espera a que el comando salga: solo a que haya hueco en la FIFO.
static void st7701_send_init(void)
{
    for (size_t i = 0; i < sizeof(k_st7701_init) / sizeof(k_st7701_init[0]); i++) {
        esp_lcd_panel_io_tx_param(s_io, k_st7701_init[i].cmd, k_st7701_init[i].data, k_st7701_init[i].len);
    }
    esp_lcd_panel_io_tx_param(s_io, ST7701_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_lcd_panel_io_tx_param(s_io, ST7701_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static esp_err_t wait_first_frames(void)
{
    uint32_t start = g_flex_dsi.vsync_count;
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (g_flex_dsi.vsync_count - start >= 2) {
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t flex_display_init(void)
{
    ESP_RETURN_ON_FALSE(s_panel == NULL, ESP_ERR_INVALID_STATE, TAG, "pantalla ya iniciada");
    ESP_LOGI(TAG, "arranque del panel en el nucleo %d", xPortGetCoreID());

    // 0) Retroiluminado apagado durante todo el arranque (evita el destello blanco)
    if (backlight_init() != ESP_OK) {
        ESP_LOGW(TAG, "sin PWM de brillo: el retroiluminado ira encendido/apagado por GPIO");
        const gpio_config_t bl = {
            .pin_bit_mask = 1ULL << FLEX_PIN_LCD_BL,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&bl);
        gpio_set_level(FLEX_PIN_LCD_BL, 0);
    }

    // 1) LDO interno que alimenta el PHY MIPI. Antes que el bus: el driver del
    //    bus espera el enganche del PLL del PHY sin limite de tiempo.
    const esp_ldo_channel_config_t ldo = {
        .chan_id = FLEX_DSI_PHY_LDO_CHAN,
        .voltage_mv = FLEX_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo, &s_ldo), TAG, "LDO del PHY MIPI");

    // 2) Bus DSI. phy_clk_src = 0: el driver elige la referencia del PLL segun
    //    la revision del chip para la que se compila.
    const esp_lcd_dsi_bus_config_t bus = {
        .bus_id = 0,
        .num_data_lanes = FLEX_DSI_LANES,
        .phy_clk_src = 0,
        .lane_bit_rate_mbps = FLEX_DSI_LANE_MBPS,
#if CONFIG_FLEX_DSI_CLOCK_LANE_FORCE_HS
        .flags.clock_lane_force_hs = 1,
#endif
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &s_bus), TAG, "bus DSI");

    // 3) Canal de comandos DCS
    const esp_lcd_dbi_io_config_t dbi = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_bus, &dbi, &s_io), TAG, "IO DBI");

    // 4) Panel DPI con DOS framebuffers en PSRAM (negros al reservarse).
    //    in_color_format y no pixel_format: este vale 0 = RGB888 si se deja a cero.
    //    Sin DMA2D: con LVGL en modo DIRECT nunca se copia nada al framebuffer.
    const esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = 0,
        .dpi_clock_freq_mhz = FLEX_DPI_CLK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = FLEX_LCD_H_RES,
            .v_size = FLEX_LCD_V_RES,
            .hsync_pulse_width = FLEX_DPI_HSYNC_PW,
            .hsync_back_porch = FLEX_DPI_HSYNC_BP,
            .hsync_front_porch = FLEX_DPI_HSYNC_FP,
            .vsync_pulse_width = FLEX_DPI_VSYNC_PW,
            .vsync_back_porch = FLEX_DPI_VSYNC_BP,
            .vsync_front_porch = FLEX_DPI_VSYNC_FP,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(s_bus, &dpi, &s_panel), TAG, "panel DPI");
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &s_fb[0], &s_fb[1]), TAG, "framebuffers");

    // 5) Aviso de fin de cuadro, registrado antes de arrancar el video
    g_flex_dsi.fb_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(g_flex_dsi.fb_done, ESP_ERR_NO_MEM, TAG, "semaforo de fin de cuadro");
    const esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_frame_buf_complete = on_fb_complete,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, &g_flex_dsi), TAG,
                        "callback de fin de cuadro (debe estar en IRAM)");

    // 6) Reset fisico del ST7701 y su tabla de arranque
    panel_hw_reset();
#if CONFIG_FLEX_DISPLAY_DCS_BEFORE_VIDEO
    // Orden de esp_lcd_st7701 (Espressif): todo el DCS en modo comando y luego video.
    st7701_send_init();
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "arranque del video DPI");
#else
    // Orden de la version Arduino, el probado en esta placa: video primero y
    // la tabla DCS con el video ya corriendo.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "arranque del video DPI");
    st7701_send_init();
#endif

    if (wait_first_frames() != ESP_OK) {
        ESP_LOGE(TAG, "el DMA del DPI no entrega cuadros: revisar alimentacion y enlace DSI");
        return ESP_ERR_TIMEOUT;
    }

#if CONFIG_FLEX_DISPLAY_BOOT_TEST_PATTERN
    flex_display_test_pattern(true);
    flex_display_backlight_enable(true);
    vTaskDelay(pdMS_TO_TICKS(2000));
    flex_display_backlight_enable(false);
    flex_display_test_pattern(false);
#endif
    ESP_LOGI(TAG, "panel %dx%d listo: 2 FB en PSRAM (%p, %p)", FLEX_LCD_H_RES, FLEX_LCD_V_RES, s_fb[0], s_fb[1]);
    return ESP_OK;
}

esp_err_t flex_display_test_pattern(bool on)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "pantalla sin iniciar");
    return esp_lcd_dpi_panel_set_pattern(s_panel, on ? MIPI_DSI_PATTERN_BAR_VERTICAL : MIPI_DSI_PATTERN_NONE);
}
