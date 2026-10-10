#include "flex_touch_lvgl.h"

#include "esp_timer.h"
#include "flex_metrics.h"
#include "flex_touch.h"

// El indev se lee con el mismo ritmo que la tarea del tactil publica cuadros.
// Lo que llega a LVGL pasa antes por el arbitraje (flex_touch_feed.c).
#define READ_PERIOD_MS 8

static uint32_t s_seq;
static bool s_shown_down;

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    flex_touch_frame_t f;
    int ev = -1, x = 0, y = 0, n = 0;
    if (flex_touch_get_frame(&f) && f.seq != s_seq) {
        // Cuadro nuevo del GT911 (como gtPoll de Arduino: 1 con dedo, 0 sin dedos)
        s_seq = f.seq;
        n = f.count;
        ev = n > 0 ? 1 : 0;
        x = f.pts[0].x;
        y = f.pts[0].y;
    }
    flex_touch_feed(indev, data, ev, x, y, n);
    // Latencia toque -> pantalla: solo cuadros que cambian algo visible
    bool down = data->state == LV_INDEV_STATE_PRESSED;
    if (ev != -1 && (down != s_shown_down || down)) {
        flex_metrics_input(f.t_read_us, esp_timer_get_time());
    }
    s_shown_down = down;
}

lv_indev_t *flex_touch_lvgl_create(lv_display_t *disp)
{
    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        return NULL;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    lv_indev_set_display(indev, disp);
    flex_touch_lvgl_tune(indev);
    lv_timer_t *t = lv_indev_get_read_timer(indev);
    if (t) {
        lv_timer_set_period(t, READ_PERIOD_MS);
    }
    return indev;
}
