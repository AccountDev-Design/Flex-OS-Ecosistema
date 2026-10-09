// Flex OS Ultra · iconos del panel rapido. Ver flex_qs_icons.h.
#include "flex_qs_icons.h"

#include <math.h>
#include <stdlib.h>
#include "flex_qs_model.h"
#include "flex_theme.h"

#define DEG 0.01745329f

typedef struct {
    lv_layer_t *L;
    lv_opa_t opa;
    lv_color_t c;
} pen_t;

typedef struct {
    flex_qi_t icon;
    int32_t s;
    int32_t value;
} qi_t;

static void fill_circle(const pen_t *p, float cx, float cy, float r)
{
    if (r < 0.5f) {
        r = 0.5f;
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = p->c;
    d.bg_opa = p->opa;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {(int32_t)lroundf(cx - r), (int32_t)lroundf(cy - r), (int32_t)lroundf(cx + r) - 1,
                   (int32_t)lroundf(cy + r) - 1};
    lv_draw_rect(p->L, &d, &a);
}

static void ring(const pen_t *p, float cx, float cy, float r, float w)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = p->c;
    d.border_opa = p->opa;
    d.border_width = (int32_t)lroundf(w < 1 ? 1 : w);
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {(int32_t)lroundf(cx - r), (int32_t)lroundf(cy - r), (int32_t)lroundf(cx + r) - 1,
                   (int32_t)lroundf(cy + r) - 1};
    lv_draw_rect(p->L, &d, &a);
}

static void rrect(const pen_t *p, float x, float y, float w, float h, int32_t r, int32_t border)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    if (border > 0) {
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = p->c;
        d.border_opa = p->opa;
        d.border_width = border;
    } else {
        d.bg_color = p->c;
        d.bg_opa = p->opa;
    }
    d.radius = r;
    lv_area_t a = {(int32_t)x, (int32_t)y, (int32_t)x + (int32_t)w - 1, (int32_t)y + (int32_t)h - 1};
    if (a.x2 < a.x1 || a.y2 < a.y1) {
        return;
    }
    lv_draw_rect(p->L, &d, &a);
}

// strokeSegAA(x0, y0, x1, y1, w): trazo de grosor w con extremos redondos
static void seg(const pen_t *p, float x0, float y0, float x1, float y1, float w)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = p->c;
    d.opa = p->opa;
    d.width = (int32_t)lroundf(w < 1 ? 1 : w);
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = (lv_value_precise_t)x0;
    d.p1.y = (lv_value_precise_t)y0;
    d.p2.x = (lv_value_precise_t)x1;
    d.p2.y = (lv_value_precise_t)y1;
    lv_draw_line(p->L, &d);
}

static void tri(const pen_t *p, float x0, float y0, float x1, float y1, float x2, float y2)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = p->c;
    d.opa = p->opa;
    d.p[0].x = (lv_value_precise_t)x0;
    d.p[0].y = (lv_value_precise_t)y0;
    d.p[1].x = (lv_value_precise_t)x1;
    d.p[1].y = (lv_value_precise_t)y1;
    d.p[2].x = (lv_value_precise_t)x2;
    d.p[2].y = (lv_value_precise_t)y2;
    lv_draw_triangle(p->L, &d);
}

// arcStroke(cx, cy, r, a0, a1, w): grados, 0 = derecha, sentido horario en pantalla
static void arc(const pen_t *p, float cx, float cy, float r, float a0, float a1, float w)
{
    float lo = a0 < a1 ? a0 : a1, hi = a0 < a1 ? a1 : a0;
    while (lo < 0) {
        lo += 360;
        hi += 360;
    }
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = p->c;
    d.opa = p->opa;
    d.width = (int32_t)lroundf(w < 1 ? 1 : w);
    d.rounded = 1;
    d.center.x = (int32_t)lroundf(cx);
    d.center.y = (int32_t)lroundf(cy);
    d.radius = (uint16_t)lroundf(r + w / 2);
    if (hi - lo >= 360.0f) {
        d.start_angle = 0;
        d.end_angle = 360;
    } else {
        d.start_angle = (lv_value_precise_t)fmodf(lo, 360.0f);
        d.end_angle = (lv_value_precise_t)fmodf(hi, 360.0f);
    }
    lv_draw_arc(p->L, &d);
}

// Media luna: el circulo (0, 0, R1) menos el (ox, oy, R2), en tiras de triangulos
// entre el borde exterior y el interior (sin pintar el hueco).
static void crescent(const pen_t *p, float cx, float cy, float R1, float ox, float oy, float R2)
{
    float d = sqrtf(ox * ox + oy * oy);
    if (d < 0.001f) {
        return;
    }
    // puntos de corte de las dos circunferencias
    float a = (R1 * R1 - R2 * R2 + d * d) / (2 * d);
    float hh = R1 * R1 - a * a;
    if (hh <= 0) {
        fill_circle(p, cx, cy, R1);
        return;
    }
    float h = sqrtf(hh), ux = ox / d, uy = oy / d;
    float px = a * ux, py = a * uy;
    float t1x = px - h * uy, t1y = py + h * ux, t2x = px + h * uy, t2y = py - h * ux;
    // angulos en el circulo exterior (por el lado lejano) y en el interior (por el cercano)
    float o1 = atan2f(t1y, t1x), o2 = atan2f(t2y, t2x);
    float i1 = atan2f(t1y - oy, t1x - ox), i2 = atan2f(t2y - oy, t2x - ox);
    float away = atan2f(-uy, -ux);
    // exterior: de o1 a o2 pasando por "away"
    float so = o2 - o1;
    while (so <= 0) {
        so += 2 * (float)M_PI;
    }
    float mid = o1 + so / 2;
    if (fabsf(remainderf(mid - away, 2 * (float)M_PI)) > (float)M_PI / 2) {
        so -= 2 * (float)M_PI;
    }
    // interior: de i1 a i2 por el lado de dentro del exterior (el que mira a "away")
    float si = i2 - i1;
    while (si <= 0) {
        si += 2 * (float)M_PI;
    }
    float midi = i1 + si / 2;
    if (fabsf(remainderf(midi - away, 2 * (float)M_PI)) > (float)M_PI / 2) {
        si -= 2 * (float)M_PI;
    }
    // Cada tramo se solapa un poco con el siguiente: dos triangulos que solo
    // comparten un borde dejan una costura clara (los dos lo suavizan).
    const int N = 18;
    for (int k = 0; k < N; k++) {
        float f0 = (float)k / N, f1 = (k + 1.35f) / N;
        f1 = f1 > 1.0f ? 1.0f : f1;
        float a0 = o1 + so * f0, a1 = o1 + so * f1, b0 = i1 + si * f0, b1 = i1 + si * f1;
        float pox = cx + R1 * cosf(a0), poy = cy + R1 * sinf(a0);
        float qox = cx + R1 * cosf(a1), qoy = cy + R1 * sinf(a1);
        float pix = cx + ox + R2 * cosf(b0), piy = cy + oy + R2 * sinf(b0);
        float qix = cx + ox + R2 * cosf(b1), qiy = cy + oy + R2 * sinf(b1);
        tri(p, pox, poy, qox, qoy, pix, piy);
        tri(p, qox, qoy, qix, qiy, pix, piy);
        tri(p, pox, poy, qix, qiy, pix, piy);   // la otra diagonal: sin costura dentro del tramo
    }
}

static void draw_icon(const pen_t *p, flex_qi_t ic, float cx, float cy, float s, int32_t value)
{
    switch (ic) {
    case FLEX_QI_WIFI: {
        float r = s * 0.42f, by = cy + s * 0.26f;
        arc(p, cx, by, r, 225, 315, 3);
        arc(p, cx, by, r * 0.66f, 225, 315, 3);
        fill_circle(p, cx, by, 3);
        break;
    }
    case FLEX_QI_AIRPLANE: {
        float u = s * 0.5f;
        tri(p, cx - u, cy + u * 0.34f, cx + u, cy + u * 0.34f, cx, cy - u * 0.16f);
        rrect(p, cx - u * 0.15f, cy - u * 0.62f, u * 0.30f, u * 1.44f, 2, 0);
        tri(p, cx - u * 0.15f, cy - u * 0.56f, cx + u * 0.15f, cy - u * 0.56f, cx, cy - u * 0.98f);
        tri(p, cx - u * 0.42f, cy + u * 0.94f, cx + u * 0.42f, cy + u * 0.94f, cx, cy + u * 0.56f);
        break;
    }
    case FLEX_QI_BLE: {
        float u = s * 0.46f;
        seg(p, cx, cy - u, cx, cy + u, 1.7f);
        seg(p, cx, cy - u, cx + u * 0.66f, cy - u * 0.42f, 1.7f);
        seg(p, cx + u * 0.66f, cy - u * 0.42f, cx - u * 0.62f, cy + u * 0.42f, 1.7f);
        seg(p, cx, cy + u, cx + u * 0.66f, cy + u * 0.42f, 1.7f);
        seg(p, cx + u * 0.66f, cy + u * 0.42f, cx - u * 0.62f, cy - u * 0.42f, 1.7f);
        break;
    }
    case FLEX_QI_SPEAKER:
    case FLEX_QI_MUTE: {
        float r = s / 2;
        rrect(p, cx - r / 2, cy - r / 4, r / 3, r / 2, 0, 0);
        tri(p, cx - r / 6, cy, cx + r / 6, cy - r / 2, cx + r / 6, cy + r / 2);
        if (ic == FLEX_QI_MUTE) {
            seg(p, cx + r / 4, cy - r / 3, cx + r / 2, cy + r / 3, 2.2f);
            seg(p, cx + r / 2, cy - r / 3, cx + r / 4, cy + r / 3, 2.2f);
        } else {
            if (value > 5) {
                seg(p, cx + r / 3, cy - r / 5, cx + r / 3, cy + r / 5, 2.0f);
            }
            if (value > 45) {
                seg(p, cx + r / 2, cy - r / 3, cx + r / 2, cy + r / 3, 2.0f);
            }
        }
        break;
    }
    case FLEX_QI_SUN:
        fill_circle(p, cx, cy, s * 0.21f);
        for (int k = 0; k < 8; k++) {
            float a = k * 0.7853982f;
            seg(p, cx + cosf(a) * s * 0.32f, cy + sinf(a) * s * 0.32f, cx + cosf(a) * s * 0.46f,
                cy + sinf(a) * s * 0.46f, 1.6f);
        }
        break;
    case FLEX_QI_MOON:
        crescent(p, cx, cy, s * 0.44f, s * 0.24f, -s * 0.22f, s * 0.40f);
        break;
    case FLEX_QI_GLASS:
    case FLEX_QI_GLASSFX:
        rrect(p, cx - s * 0.46f, cy - s * 0.30f, s * 0.66f, s * 0.66f, 6, 1);
        rrect(p, cx - s * 0.16f, cy - s * 0.46f, s * 0.62f, s * 0.62f, 6, 1);
        if (ic == FLEX_QI_GLASSFX) {
            seg(p, cx - s * 0.30f, cy + s * 0.18f, cx - s * 0.10f, cy - s * 0.10f, 2.0f);
        }
        break;
    case FLEX_QI_RESET: {
        float r = s * 0.34f;
        arc(p, cx, cy, r, -320, -40, 2);   // de 40 a 320 grados en sentido antihorario (y hacia arriba)
        float ex = cx + cosf(40 * DEG) * r, ey = cy - sinf(40 * DEG) * r;
        tri(p, ex - s * 0.14f, ey - s * 0.02f, ex + s * 0.10f, ey - s * 0.12f, ex + s * 0.04f, ey + s * 0.14f);
        break;
    }
    case FLEX_QI_BATTSAVE:
        rrect(p, cx - s * 0.42f, cy - s * 0.28f, s * 0.76f, s * 0.56f, 4, 1);
        rrect(p, cx + s * 0.36f, cy - s * 0.10f, s * 0.10f, s * 0.22f, 0, 0);
        tri(p, cx - s * 0.06f, cy - s * 0.20f, cx + s * 0.14f, cy - s * 0.20f, cx - s * 0.02f, cy);
        tri(p, cx - s * 0.02f, cy, cx + s * 0.18f, cy, cx - s * 0.04f, cy + s * 0.22f);
        break;
    case FLEX_QI_GEAR:
        // disco con agujero (anillo de 0.11 a 0.26) y ocho dientes
        ring(p, cx, cy, s * 0.26f, s * 0.15f);
        for (int k = 0; k < 8; k++) {
            float a = k * 0.7853982f;
            fill_circle(p, cx + cosf(a) * s * 0.42f, cy + sinf(a) * s * 0.42f, s * 0.10f);
        }
        break;
    case FLEX_QI_SIGNAL:
        for (int k = 0; k < 4; k++) {
            float bw = s * 0.13f, bh = s * (0.18f + k * 0.16f);
            rrect(p, cx - s * 0.44f + k * s * 0.24f, cy + s * 0.42f - bh, bw, bh, 2, 0);
        }
        break;
    case FLEX_QI_MONITOR:
        rrect(p, cx - s * 0.46f, cy - s * 0.40f, s * 0.92f, s * 0.62f, 4, 1);
        rrect(p, cx - s * 0.12f, cy + s * 0.22f, s * 0.24f, s * 0.14f, 0, 0);
        rrect(p, cx - s * 0.32f, cy + s * 0.36f, s * 0.64f, s * 0.10f, 0, 0);
        break;
    case FLEX_QI_UPDATE:
        rrect(p, cx - s * 0.08f, cy - s * 0.44f, s * 0.17f, s * 0.42f, 0, 0);
        tri(p, cx - s * 0.26f, cy - s * 0.06f, cx + s * 0.26f, cy - s * 0.06f, cx, cy + s * 0.24f);
        rrect(p, cx - s * 0.34f, cy + s * 0.32f, s * 0.68f, s * 0.11f, 2, 0);
        break;
    case FLEX_QI_FOLDER:
        rrect(p, cx - s * 0.46f, cy - s * 0.34f, s * 0.42f, s * 0.16f, 3, 0);
        rrect(p, cx - s * 0.46f, cy - s * 0.24f, s * 0.92f, s * 0.60f, 5, (int32_t)lroundf(s * 0.13f));
        break;
    case FLEX_QI_CAMERA:
        rrect(p, cx - s * 0.16f, cy - s * 0.44f, s * 0.32f, s * 0.12f, 3, 0);
        rrect(p, cx - s * 0.48f, cy - s * 0.32f, s * 0.96f, s * 0.68f, 6, 3);
        ring(p, cx, cy + s * 0.02f, s * 0.21f, 2);
        fill_circle(p, cx, cy + s * 0.02f, s * 0.12f);
        break;
    case FLEX_QI_IMAGE:
        rrect(p, cx - s * 0.46f, cy - s * 0.36f, s * 0.92f, s * 0.72f, 5, 1);
        fill_circle(p, cx - s * 0.20f, cy - s * 0.14f, s * 0.09f);
        tri(p, cx - s * 0.36f, cy + s * 0.30f, cx - s * 0.02f, cy + s * 0.30f, cx - s * 0.19f, cy + s * 0.02f);
        tri(p, cx - s * 0.16f, cy + s * 0.30f, cx + s * 0.40f, cy + s * 0.30f, cx + s * 0.12f, cy - s * 0.06f);
        break;
    case FLEX_QI_STOPWATCH:
        arc(p, cx, cy + s * 0.06f, s * 0.38f, 0, 360, 3);
        rrect(p, cx - s * 0.13f, cy - s * 0.48f, s * 0.26f, s * 0.10f, 0, 0);
        seg(p, cx, cy + s * 0.06f, cx + s * 0.20f, cy - s * 0.14f, 1.6f);
        break;
    case FLEX_QI_LOCK:
        arc(p, cx, cy - s * 0.10f, s * 0.26f, 180, 360, 3);
        rrect(p, cx - s * 0.36f, cy - s * 0.10f, s * 0.72f, s * 0.52f, 5, 0);
        break;
    case FLEX_QI_POWER:
        arc(p, cx, cy + s * 0.04f, s * 0.36f, -68, 248, 3);
        rrect(p, cx - s * 0.05f, cy - s * 0.42f, s * 0.11f, s * 0.40f, 0, 0);
        break;
    case FLEX_QI_CLOCK:
        arc(p, cx, cy, s * 0.42f, 0, 360, 3);
        seg(p, cx, cy, cx, cy - s * 0.26f, 1.6f);
        seg(p, cx, cy, cx + s * 0.20f, cy + s * 0.06f, 1.6f);
        break;
    case FLEX_QI_PENCIL:
        seg(p, cx - s * 0.30f, cy + s * 0.30f, cx + s * 0.28f, cy - s * 0.28f, 2.4f);
        tri(p, cx - s * 0.42f, cy + s * 0.42f, cx - s * 0.34f, cy + s * 0.14f, cx - s * 0.14f, cy + s * 0.34f);
        break;
    case FLEX_QI_PLUS:
        rrect(p, cx - s * 0.36f, cy - s * 0.07f, s * 0.72f, s * 0.14f, 2, 0);
        rrect(p, cx - s * 0.07f, cy - s * 0.36f, s * 0.14f, s * 0.72f, 2, 0);
        break;
    case FLEX_QI_MINUS:
        rrect(p, cx - s * 0.34f, cy - s * 0.07f, s * 0.68f, s * 0.14f, 2, 0);
        break;
    case FLEX_QI_DND: {
        float r = s * 0.30f;
        fill_circle(p, cx, cy - r * 0.15f, r * 0.72f);
        rrect(p, cx - r * 0.95f, cy + r * 0.45f, r * 1.9f, 2, 0, 0);
        fill_circle(p, cx, cy + r * 0.85f, r * 0.22f);
        seg(p, cx - r, cy - r, cx + r, cy + r, 2.4f);
        break;
    }
    case FLEX_QI_ROTATE:   // boton de orientacion del editor (qpDrawEditChrome)
        arc(p, cx, cy, 7, 30, 300, 2);
        tri(p, cx + 4, cy - 10, cx + 11, cy - 5, cx + 3, cy - 1);
        break;
    default:
        break;
    }
}

static void qi_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    qi_t *q = lv_event_get_user_data(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    pen_t p = {lv_event_get_layer(e), lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN),
               lv_obj_get_style_text_color(obj, LV_PART_MAIN)};
    float cx = c.x1 + lv_area_get_width(&c) / 2.0f, cy = c.y1 + lv_area_get_height(&c) / 2.0f;
    draw_icon(&p, q->icon, cx, cy, (float)q->s, q->value);
}

static void qi_delete_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

lv_obj_t *flex_qi_create(lv_obj_t *parent, flex_qi_t icon, int32_t s)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, s + 8, s + 8);   // margen para el grosor de los trazos
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    qi_t *q = calloc(1, sizeof(*q));
    if (q) {
        q->icon = icon;
        q->s = s;
        q->value = 100;
        lv_obj_add_event_cb(o, qi_draw_cb, LV_EVENT_DRAW_MAIN, q);
        lv_obj_add_event_cb(o, qi_delete_cb, LV_EVENT_DELETE, q);
    }
    lv_obj_set_style_text_color(o, flex_th()->txt, 0);
    return o;
}

void flex_qi_set(lv_obj_t *obj, flex_qi_t icon, int32_t value)
{
    uint32_t n = lv_obj_get_event_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(obj, i);
        if (d && lv_event_dsc_get_cb(d) == qi_draw_cb) {
            qi_t *q = lv_event_dsc_get_user_data(d);
            if (q->icon != icon || q->value != value) {
                q->icon = icon;
                q->value = value;
                lv_obj_invalidate(obj);
            }
            return;
        }
    }
}

flex_qi_t flex_qs_ctl_icon(int id)
{
    static const uint8_t MAP[FLEX_QS_COUNT] = {
        FLEX_QI_WIFI,   FLEX_QI_AIRPLANE, FLEX_QI_BLE,      FLEX_QI_SUN,     FLEX_QI_MOON,      FLEX_QI_GLASS,
        FLEX_QI_BATTSAVE, FLEX_QI_GEAR,   FLEX_QI_SIGNAL,   FLEX_QI_MONITOR, FLEX_QI_GEAR,      FLEX_QI_UPDATE,
        FLEX_QI_FOLDER, FLEX_QI_GEAR,     FLEX_QI_CAMERA,   FLEX_QI_IMAGE,   FLEX_QI_STOPWATCH, FLEX_QI_LOCK,
        FLEX_QI_POWER,  FLEX_QI_CLOCK,    FLEX_QI_SPEAKER,  FLEX_QI_MUTE,    FLEX_QI_DND,       FLEX_QI_GLASSFX,
    };
    return (id >= 0 && id < FLEX_QS_COUNT) ? (flex_qi_t)MAP[id] : FLEX_QI_NONE;
}
