#include "flex_display_lvgl.h"

#include <stdint.h>
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flex_board.h"
#include "flex_display.h"
#include "flex_display_lvgl_service.h"
#include "flex_display_priv.h"
#include "flex_flip_rows.h"
#include "flex_metrics.h"
#include "sdkconfig.h"

// Tres cuadros (~50 ms): si el DMA no ha terminado en ese tiempo algo va mal;
// se sigue adelante para que la interfaz no se congele nunca.
#define VSYNC_TIMEOUT_MS 50

static const char *TAG = "flex.display.lvgl";

static lv_display_t *s_disp;
// Filas escritas por la CPU en el framebuffer de atras (ver flex_flip_rows.h).
static flex_flip_rows_t s_rows;
static bool s_flip_pending;
static bool s_first_frame_shown;
static int64_t s_wait_us;
static uint32_t s_timeouts;

static void finish_flip(bool shown)
{
    s_flip_pending = false;
    if (!shown) {
        return;
    }
    flex_metrics_frame_shown(g_flex_dsi.shown_us);
    if (!s_first_frame_shown) {
        // El retroiluminado se enciende con el primer cuadro de LVGL ya en el
        // panel: nunca se ve el arranque del ST7701 ni un buffer a medias.
        s_first_frame_shown = true;
        flex_display_backlight_enable(true);
    }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    flex_flip_rows_add(&s_rows, area->y1, area->y2);
    if (!lv_display_flush_is_last(disp)) {
        // En modo DIRECT LVGL ya ha dibujado en su sitio: no hay nada que enviar.
        lv_display_flush_ready(disp);
        return;
    }

    int32_t y1, y2;
#if CONFIG_FLEX_DISPLAY_MSYNC_FULL_FRAME
    flex_flip_rows_take(&s_rows, 1, &y1, &y2);
#else
    flex_flip_rows_take(&s_rows, 0, &y1, &y2);
#endif

    // px_map es el framebuffer completo (modo DIRECT): el driver reconoce que es
    // uno de los suyos, escribe la cache de esas filas a la PSRAM y lo elige
    // para el SIGUIENTE cuadro del DMA. No copia nada.
    esp_err_t err = esp_lcd_panel_draw_bitmap(flex_display_panel(), 0, y1, FLEX_LCD_H_RES, y2 + 1, px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap: %s", esp_err_to_name(err));
        lv_display_flush_ready(disp);
        return;
    }
    // Despues de que draw_bitmap haya guardado el indice nuevo. La ISR corre en
    // este mismo nucleo, asi que la primera que vea esta marca ya relanzo el DMA
    // sobre el framebuffer nuevo y el anterior queda libre.
    g_flex_dsi.flip_armed = true;
    flex_metrics_frame_flipped(esp_timer_get_time());

    s_flip_pending = true;
    // Sin lv_display_flush_ready(): LVGL llamara a flush_wait_cb antes de tocar
    // el otro framebuffer, y mientras tanto puede atender el tactil y los timers.
}

static void wait_for_flip(TickType_t ticks)
{
    int64_t t0 = esp_timer_get_time();
    bool ok = xSemaphoreTake(g_flex_dsi.fb_done, ticks) == pdTRUE;
    int64_t waited = esp_timer_get_time() - t0;
    s_wait_us += waited;
    if (ok) {
        flex_metrics_vsync_wait(waited, false);
        finish_flip(true);
        return;
    }
    if (ticks == 0) {
        return;
    }
    // Vencio la espera: se desarma antes de vaciar el semaforo, para que una ISR
    // que llegue justo ahora no deje un aviso viejo para el siguiente cuadro.
    g_flex_dsi.flip_armed = false;
    xSemaphoreTake(g_flex_dsi.fb_done, 0);
    flex_metrics_vsync_wait(waited, true);
    if (s_timeouts++ < 5) {
        ESP_LOGW(TAG, "fin de cuadro no recibido en %d ms (vsync=%" PRIu32 ")", VSYNC_TIMEOUT_MS,
                 g_flex_dsi.vsync_count);
    }
    finish_flip(false);
}

static void flush_wait_cb(lv_display_t *disp)
{
    (void)disp;
    if (s_flip_pending) {
        wait_for_flip(pdMS_TO_TICKS(VSYNC_TIMEOUT_MS));
    }
}

static void render_start_cb(lv_event_t *e)
{
    (void)e;
    flex_metrics_render_start(esp_timer_get_time());
}

lv_display_t *flex_display_lvgl_create(void)
{
    void *fb0 = NULL, *fb1 = NULL;
    flex_display_get_fbs(&fb0, &fb1);
    if (!fb0 || !fb1) {
        ESP_LOGE(TAG, "pantalla sin iniciar");
        return NULL;
    }
    lv_display_t *disp = lv_display_create(FLEX_LCD_H_RES, FLEX_LCD_V_RES);
    if (!disp) {
        return NULL;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    // buf_1 = fb1: el DPI arranca mostrando fb0, y LVGL dibuja primero en buf_1.
    lv_display_set_buffers(disp, fb1, fb0, FLEX_LCD_H_RES * FLEX_LCD_V_RES * (FLEX_LCD_BITS_PER_PIXEL / 8),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_flush_wait_cb(disp, flush_wait_cb);
    lv_display_add_event_cb(disp, render_start_cb, LV_EVENT_RENDER_START, NULL);
    flex_flip_rows_init(&s_rows, FLEX_LCD_V_RES);
    s_disp = disp;
    return disp;
}

void flex_display_lvgl_idle(uint32_t max_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(max_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    if (!s_flip_pending) {
        vTaskDelay(ticks);
        return;
    }
    // Con un cambio de FB pendiente, la tarea de UI duerme en el propio aviso de
    // fin de cuadro: se despierta en cuanto el panel estrena el cuadro (o cuando
    // toque el siguiente timer de LVGL, lo que llegue antes). Esta espera es
    // reposo, no bloquea el dibujo: no cuenta como espera de fin de cuadro.
    if (xSemaphoreTake(g_flex_dsi.fb_done, ticks) == pdTRUE) {
        finish_flip(true);
        lv_display_flush_ready(s_disp);
    }
}

int64_t flex_display_lvgl_take_wait_us(void)
{
    int64_t w = s_wait_us;
    s_wait_us = 0;
    return w;
}
