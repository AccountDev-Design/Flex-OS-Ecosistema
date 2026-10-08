// El arbitraje del tactil entre el GT911 y LVGL (ver flex_touch_arb.h).
#include "flex_touch_lvgl.h"

static flex_arb_t s_arb;
static bool s_init;
static flex_touch_gesture_cb_t s_cb;
static lv_indev_t *s_indev;
static bool s_lv_down;   // lo ultimo que vio LVGL

flex_arb_t *flex_touch_arb(void)
{
    if (!s_init) {
        flex_arb_init(&s_arb);
        s_init = true;
    }
    return &s_arb;
}

void flex_touch_set_gesture_cb(flex_touch_gesture_cb_t cb)
{
    s_cb = cb;
}

void flex_touch_drop_all(void)
{
    flex_arb_drop_all(flex_touch_arb(), lv_tick_get());
    if (s_indev) {
        lv_indev_reset(s_indev, NULL);   // el objeto pulsado deja de serlo, sin click
    }
    s_lv_down = false;
}

void flex_touch_feed(lv_indev_t *indev, lv_indev_data_t *data, int ev, int x, int y, int fingers)
{
    s_indev = indev;
    flex_arb_t *a = flex_touch_arb();
    int evt = flex_arb_poll(a, ev, x, y, fingers, lv_tick_get());
    const flex_arb_touch_t *t = &a->t;
    if (s_lv_down && !t->down && !t->released) {
        // El dedo sigue apoyado pero el arbitraje se lo traga (dos dedos,
        // suspension): LVGL no puede verlo como "soltado" o haria click.
        lv_indev_reset(indev, NULL);
    }
    s_lv_down = t->down;
    data->point.x = t->x;
    data->point.y = t->y;
    data->state = t->down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (evt != FLEX_ARB_EV_NONE && s_cb) {
        s_cb(evt);
    }
}
