// El arbitraje del tactil entre el GT911 y LVGL (ver flex_touch_arb.h).
#include "flex_touch_lvgl.h"

#include <stdint.h>

static flex_arb_t s_arb;
static bool s_init;
static flex_touch_gesture_cb_t s_cb;
static lv_indev_t *s_indev;
static bool s_lv_down;   // lo ultimo que vio LVGL
static flex_touch_sys_hook_t s_hook;

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

void flex_touch_set_sys_hook(flex_touch_sys_hook_t hook)
{
    s_hook = hook;
}

// Distancia (con signo) de un eje al punto de encaje mas cercano, sin limitarse
// a los que se ven (lv_obj_update_snap solo mira los que caen dentro del
// objeto: con la pagina movida 20 px elegiria la siguiente).
static int32_t snap_dist(lv_obj_t *o, bool hor)
{
    lv_scroll_snap_t al = hor ? lv_obj_get_scroll_snap_x(o) : lv_obj_get_scroll_snap_y(o);
    if (al == LV_SCROLL_SNAP_NONE) {
        return 0;
    }
    lv_area_t oc;
    lv_obj_get_coords(o, &oc);
    int32_t p0 = hor ? oc.x1 + lv_obj_get_style_pad_left(o, LV_PART_MAIN) : oc.y1 + lv_obj_get_style_pad_top(o, LV_PART_MAIN);
    int32_t p1 = hor ? oc.x2 - lv_obj_get_style_pad_right(o, LV_PART_MAIN) : oc.y2 - lv_obj_get_style_pad_bottom(o, LV_PART_MAIN);
    int32_t best = INT32_MAX;
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        if (lv_obj_is_hidden(c) || lv_obj_is_floating(c) || !lv_obj_is_snappable(c)) {
            continue;
        }
        lv_area_t cc;
        lv_obj_get_coords(c, &cc);
        int32_t c0 = hor ? cc.x1 : cc.y1, c1 = hor ? cc.x2 : cc.y2, d;
        if (al == LV_SCROLL_SNAP_START) {
            d = c0 - p0;
        } else if (al == LV_SCROLL_SNAP_END) {
            d = c1 - p1;
        } else {
            d = (c0 + c1) / 2 - (p0 + p1) / 2;
        }
        if (LV_ABS(d) < LV_ABS(best)) {
            best = d;
        }
    }
    return best == INT32_MAX ? 0 : best;
}

// Suelta lo que LVGL tenga pulsado sin click. Si el dedo estaba desplazando
// algo (las paginas del escritorio), vuelve a su punto de encaje mas cercano:
// cortar el scroll a medias lo dejaria movido unos pixeles.
static void lv_release(lv_indev_t *indev)
{
    lv_obj_t *scr = lv_indev_get_scroll_obj(indev);
    lv_indev_reset(indev, NULL);
    if (scr) {
        int32_t dx = snap_dist(scr, true), dy = snap_dist(scr, false);
        if (dx || dy) {
            lv_obj_scroll_by(scr, -dx, -dy, LV_ANIM_ON);
        }
    }
}

void flex_touch_drop_all(void)
{
    flex_arb_drop_all(flex_touch_arb(), lv_tick_get());
    if (s_indev) {
        lv_release(s_indev);   // el objeto pulsado deja de serlo, sin click
    }
    s_lv_down = false;
}

void flex_touch_feed(lv_indev_t *indev, lv_indev_data_t *data, int ev, int x, int y, int fingers)
{
    s_indev = indev;
    flex_arb_t *a = flex_touch_arb();
    int evt = flex_arb_poll(a, ev, x, y, fingers, lv_tick_get());
    const flex_arb_touch_t *t = &a->t;
    if (s_hook && !a->suspended && s_hook(t)) {
        // El episodio es de una capa del sistema: LVGL no lo ve. Sigue siendo
        // actividad (el bloqueo por inactividad no puede saltar con el dedo encima).
        if (t->down) {
            lv_display_trigger_activity(lv_indev_get_display(indev));
        }
        if (s_lv_down) {
            lv_release(indev);
            s_lv_down = false;
        }
        data->point.x = t->x;
        data->point.y = t->y;
        data->state = LV_INDEV_STATE_RELEASED;
        if (evt != FLEX_ARB_EV_NONE && s_cb) {
            s_cb(evt);
        }
        return;
    }
    if (s_lv_down && !t->down && !t->released) {
        // El dedo sigue apoyado pero el arbitraje se lo traga (dos dedos,
        // suspension): LVGL no puede verlo como "soltado" o haria click.
        lv_release(indev);
    }
    s_lv_down = t->down;
    data->point.x = t->x;
    data->point.y = t->y;
    data->state = t->down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (evt != FLEX_ARB_EV_NONE && s_cb) {
        s_cb(evt);
    }
}
