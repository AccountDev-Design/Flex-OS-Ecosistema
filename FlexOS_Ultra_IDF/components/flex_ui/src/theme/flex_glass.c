#include "flex_glass.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "flex_glass_math.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "flex_wallpaper.h"

typedef struct {
    flex_surf_role_t role;
    flex_backdrop_t bd;
    lv_color_t tint;
    lv_opa_t opa;
    int8_t material;          // -1 tema, 0 plano, 1 vidrio, 2 estilo de icono
    uint8_t min_mix;
    bool flat_sheen;
    lv_image_dsc_t view;      // vista sobre el backdrop (vive mientras viva el objeto)
    lv_area_t mix_area;       // area con la que se calculo mix
    uint8_t mix;
    uint8_t mix_level;        // nivel de vidrio con el que se calculo
    lv_color_t mix_tint;
} surf_t;

static lv_color_t flat_color(const surf_t *s)
{
    const flex_palette_t *t = flex_th();
    switch (s->role) {
    case FLEX_SURF_ELEVATED: return t->surf2;
    case FLEX_SURF_ACCENT: return flex_accent();
    case FLEX_SURF_WALL: return FLEX_WALLSURF;
    case FLEX_SURF_TINT: return s->tint;
    default: return t->surf;
    }
}

static lv_color_t glass_tint(const surf_t *s)
{
    const flex_palette_t *t = flex_th();
    switch (s->role) {
    case FLEX_SURF_ELEVATED: return t->glass2;
    case FLEX_SURF_ACCENT: return flex_accent();
    case FLEX_SURF_WALL: return FLEX_WALLSURF2;
    case FLEX_SURF_TINT: return s->tint;
    default: return t->glass;
    }
}

static inline bool area_eq(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 == b->x1 && a->y1 == b->y1 && a->x2 == b->x2 && a->y2 == b->y2;
}

static inline bool area_and(lv_area_t *out, const lv_area_t *a, const lv_area_t *b)
{
    out->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    out->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    out->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    out->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return out->x1 <= out->x2 && out->y1 <= out->y2;
}

static inline lv_opa_t scale(uint32_t a, lv_opa_t opa)
{
    return (lv_opa_t)((a * opa + 127) / 255);
}

static uint8_t compute_mix(surf_t *s, const lv_area_t *coords, lv_color_t tint)
{
    const flex_glass_params_t *gp = flex_glass_params();
    if (area_eq(coords, &s->mix_area) && s->mix_level == gp->level &&
        lv_color_eq(s->mix_tint, tint)) {
        return s->mix;
    }
    uint32_t sum = 0;
    int n = 0;
    if (s->bd == FLEX_BD_FLAT) {
        sum = (uint32_t)flex_glass_luma(flex_lv_to_565(flex_th()->page));
        n = 1;
    } else {
        flex_wallmgr_backdrop_luma(s->bd == FLEX_BD_HOME ? FLEX_WALL_HOME : FLEX_WALL_LOCK, coords, &sum, &n);
    }
    s->mix = flex_glass_tint_mix(gp, sum, n, flex_lv_to_565(tint), s->min_mix);
    s->mix_area = *coords;
    s->mix_level = gp->level;
    s->mix_tint = tint;
    return s->mix;
}

// Vista del backdrop con la geometria del panel: el pixel (x, y) de la pantalla
// es backdrop[y * 480 + x] para todo pixel visible. LVGL solo lee la parte
// visible, asi que el origen puede quedar fuera de la pantalla (paneles que
// entran deslizando); se calcula con enteros para no formar punteros invalidos.
static bool make_view(surf_t *s, const uint16_t *bd, const lv_area_t *c)
{
    int32_t w = lv_area_get_width(c), h = lv_area_get_height(c);
    if (w <= 0 || h <= 0) {
        return false;
    }
    intptr_t off = ((intptr_t)c->y1 * FLEX_WALL_W + c->x1) * 2;
    memset(&s->view, 0, sizeof(s->view));
    s->view.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->view.header.cf = LV_COLOR_FORMAT_RGB565;
    s->view.header.w = (uint32_t)w;
    s->view.header.h = (uint32_t)h;
    s->view.header.stride = FLEX_WALL_W * 2;
    s->view.data = (const uint8_t *)((uintptr_t)bd + (uintptr_t)off);
    s->view.data_size = (uint32_t)(FLEX_WALL_W * 2 * h);
    return true;
}

static void draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    surf_t *s = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!s || !layer || s->opa == 0) {
        return;
    }
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    int32_t w = lv_area_get_width(&c), h = lv_area_get_height(&c);
    int32_t rad = lv_obj_get_style_radius(obj, LV_PART_MAIN);
    int32_t rmax = (w < h ? w : h) / 2;
    if (rad > rmax) {
        rad = rmax;
    }
    const flex_look_prefs_t *lp = flex_look();
    bool glass = s->material < 0 ? lp->glass : s->material == 2 ? lp->icon_style == 1 : s->material == 1;

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius = rad;

    if (!glass) {
        rd.bg_color = flat_color(s);
        rd.bg_opa = s->opa;
        lv_draw_rect(layer, &rd, &c);
        if (s->flat_sheen) {
            // Brillo de los iconos planos: mitad superior, blanco a22 (Icons.h:77)
            lv_area_t top = c;
            top.y2 = c.y1 + h / 2 - 1;
            rd.bg_color = lv_color_white();
            rd.bg_opa = scale(22, s->opa);
            lv_area_t saved = layer->_clip_area, clip;
            if (area_and(&clip, &saved, &top)) {
                layer->_clip_area = clip;
                lv_draw_rect(layer, &rd, &c);
                layer->_clip_area = saved;
            }
        }
        return;
    }

    const flex_glass_params_t *gp = flex_glass_params();
    lv_color_t tint = glass_tint(s);
    // 1) lo de detras, ya desenfocado
    if (s->bd != FLEX_BD_FLAT) {
        const uint16_t *bd = flex_wallmgr_backdrop(s->bd == FLEX_BD_HOME ? FLEX_WALL_HOME : FLEX_WALL_LOCK);
        if (bd && make_view(s, bd, &c)) {
            lv_draw_image_dsc_t id;
            lv_draw_image_dsc_init(&id);
            id.src = &s->view;
            id.clip_radius = rad;
            id.opa = s->opa;
            id.image_area = c;
            lv_draw_image(layer, &id, &c);
        } else {
            // Sin backdrop (sin memoria): tinte translucido, como el respaldo de Arduino (a210)
            rd.bg_color = tint;
            rd.bg_opa = scale(210, s->opa);
            lv_draw_rect(layer, &rd, &c);
            return;
        }
    }
    // 2) tinte adaptativo
    rd.bg_color = tint;
    rd.bg_opa = scale(compute_mix(s, &c, tint), s->opa);
    lv_draw_rect(layer, &rd, &c);
    // 3) especular arriba (blanco) y sombreado abajo (negro)
    lv_draw_rect_dsc_t ld;
    lv_draw_rect_dsc_init(&ld);
    ld.radius = rad;
    ld.bg_opa = LV_OPA_COVER;
    ld.bg_grad.dir = LV_GRAD_DIR_VER;
    ld.bg_grad.stops_count = 4;
    ld.bg_grad.stops[0].color = lv_color_white();
    ld.bg_grad.stops[0].opa = scale(gp->spec, s->opa);
    ld.bg_grad.stops[0].frac = 0;
    ld.bg_grad.stops[1].color = lv_color_white();
    ld.bg_grad.stops[1].opa = 0;
    ld.bg_grad.stops[1].frac = 115;   // 45 %
    ld.bg_grad.stops[2].color = lv_color_black();
    ld.bg_grad.stops[2].opa = 0;
    ld.bg_grad.stops[2].frac = 115;
    ld.bg_grad.stops[3].color = lv_color_black();
    ld.bg_grad.stops[3].opa = scale(gp->shade, s->opa);
    ld.bg_grad.stops[3].frac = 255;
    lv_draw_rect(layer, &ld, &c);
    // 4) borde direccional: claro en la mitad de arriba, oscuro en la de abajo
    //    (en Arduino solo los laterales y el contorno de las esquinas, con pesos
    //    distintos a izquierda y derecha; aqui un trazo de 1 px con el peso medio)
    lv_draw_border_dsc_t bdsc;
    lv_draw_border_dsc_init(&bdsc);
    bdsc.radius = rad;
    bdsc.width = 1;
    bdsc.side = LV_BORDER_SIDE_FULL;
    uint8_t weight = (uint8_t)(((uint32_t)gp->corner_strong + gp->corner_weak) / 2);
    lv_area_t saved = layer->_clip_area, half = c, clip;
    half.y2 = c.y1 + h / 2 - 1;
    if (area_and(&clip, &saved, &half)) {
        layer->_clip_area = clip;
        bdsc.color = lv_color_hex(0xCDD6E4);
        bdsc.opa = scale(weight, s->opa);
        lv_draw_border(layer, &bdsc, &c);
    }
    half = c;
    half.y1 = c.y1 + h / 2;
    if (area_and(&clip, &saved, &half)) {
        layer->_clip_area = clip;
        bdsc.color = lv_color_hex(0x161C28);
        bdsc.opa = scale(weight / 2, s->opa);
        lv_draw_border(layer, &bdsc, &c);
    }
    layer->_clip_area = saved;
}

static void delete_cb(lv_event_t *e)
{
    surf_t *s = lv_event_get_user_data(e);
    free(s);
}

static surf_t *get(lv_obj_t *obj)
{
    uint32_t n = lv_obj_get_event_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(obj, i);
        if (d && lv_event_dsc_get_cb(d) == draw_cb) {
            return lv_event_dsc_get_user_data(d);
        }
    }
    return NULL;
}

void flex_surface(lv_obj_t *obj, flex_surf_role_t role, flex_backdrop_t bd)
{
    surf_t *s = get(obj);
    if (!s) {
        s = calloc(1, sizeof(*s));
        if (!s) {
            return;
        }
        s->opa = LV_OPA_COVER;
        s->material = -1;
        lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, s);
        lv_obj_add_event_cb(obj, delete_cb, LV_EVENT_DELETE, s);
    }
    s->role = role;
    s->bd = bd;
    s->mix_area.x2 = -1;   // recalcular
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_invalidate(obj);
}

void flex_surface_set_tint(lv_obj_t *obj, lv_color_t tint)
{
    surf_t *s = get(obj);
    if (s) {
        s->tint = tint;
        lv_obj_invalidate(obj);
    }
}

void flex_surface_set_opa(lv_obj_t *obj, lv_opa_t opa)
{
    surf_t *s = get(obj);
    if (s && s->opa != opa) {
        s->opa = opa;
        lv_obj_invalidate(obj);
    }
}

void flex_surface_force_material(lv_obj_t *obj, int material)
{
    surf_t *s = get(obj);
    if (s) {
        s->material = (int8_t)(material < 0 ? -1 : material > 2 ? 2 : material);
        lv_obj_invalidate(obj);
    }
}

void flex_surface_set_min_mix(lv_obj_t *obj, uint8_t min_mix)
{
    surf_t *s = get(obj);
    if (s) {
        s->min_mix = min_mix;
        s->mix_area.x2 = -1;
        lv_obj_invalidate(obj);
    }
}

void flex_surface_set_flat_sheen(lv_obj_t *obj, bool on)
{
    surf_t *s = get(obj);
    if (s) {
        s->flat_sheen = on;
        lv_obj_invalidate(obj);
    }
}
