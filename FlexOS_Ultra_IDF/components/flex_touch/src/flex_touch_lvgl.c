#include "flex_touch_lvgl.h"

#include "esp_timer.h"
#include "flex_metrics.h"
#include "flex_touch.h"

// El indev se lee con el mismo ritmo que la tarea del tactil publica cuadros.
#define READ_PERIOD_MS 8

static uint32_t s_seq;
static int32_t s_x, s_y;
static bool s_pressed;

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    flex_touch_frame_t f;
    if (!flex_touch_get_frame(&f)) {
        data->point.x = s_x;
        data->point.y = s_y;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    bool pressed = f.count > 0;
    bool moved = pressed && (f.pts[0].x != s_x || f.pts[0].y != s_y);
    if (f.seq != s_seq) {
        s_seq = f.seq;
        // Solo cuenta para la latencia un cuadro que cambia algo visible:
        // pulsar, soltar o mover. Un dedo quieto no genera un cuadro nuevo.
        if (pressed != s_pressed || moved) {
            flex_metrics_input(f.t_read_us, esp_timer_get_time());
        }
    }
    if (pressed) {
        s_x = f.pts[0].x;
        s_y = f.pts[0].y;
    }
    s_pressed = pressed;
    data->point.x = s_x;
    data->point.y = s_y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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
    lv_timer_t *t = lv_indev_get_read_timer(indev);
    if (t) {
        lv_timer_set_period(t, READ_PERIOD_MS);
    }
    return indev;
}
