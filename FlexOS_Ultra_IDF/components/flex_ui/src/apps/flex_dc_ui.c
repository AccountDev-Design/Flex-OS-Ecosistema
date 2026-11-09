// Flex OS Ultra · pinta con LVGL las operaciones de dibujo de Device Care (ver flex_dc_ui.h).
#include "flex_dc_ui.h"

#include <math.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_kit.h"
#include "flex_frame.h"
#include "flex_theme.h"

lv_color_t flex_dc_ui_color(int role)
{
    const flex_palette_t *t = flex_th();
    switch (role) {
    case DCC_TXT: return t->txt;
    case DCC_TXT2: return t->txt2;
    case DCC_MUTE: return t->mute;
    case DCC_OK: return t->ok;
    case DCC_WARN: return t->warn;
    case DCC_ERR: return t->err;
    case DCC_PRIM: return t->primary;
    case DCC_ONACC: return t->on_acc;
    case DCC_ACCS: return t->acc_soft;
    case DCC_TRACK: return t->track;
    case DCC_BORDER: return t->border;
    case DCC_SURF: return t->surf;
    case DCC_SURF2: return t->surf2;
    case DCC_DIV: return t->divider;
    case DCC_DANGER: return t->danger;
    case DCC_ACCENT: return flex_accent();
    case DCC_WINBG:
    default: return t->win;
    }
}

static bool is_vector(int kind)
{
    return kind != DCO_SURF && kind != DCO_TEXT && kind != DCO_TEXT_C && kind != DCO_TEXT_R && kind != DCO_TEXT_CLIP;
}

typedef struct {
    flex_dc_op_t *op;
    int n, cap;
    uint32_t sig;       // firma de la ESTRUCTURA de lo que no es vector (superficies y textos, sin el contenido de los textos)
    lv_obj_t *vobj;     // el objeto de dibujo
    lv_obj_t **lab;     // una etiqueta por cada operacion de texto, en orden
    int nlab;
    int h;              // alto del contenedor
} vec_t;

static int s_builds, s_vec_updates;
int flex_dc_ui_builds(void) { return s_builds; }
int flex_dc_ui_vec_updates(void) { return s_vec_updates; }

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}
// Firma de la estructura de todo lo que no es vector: si no cambia, la pagina nueva solo trae otro dibujo y otros contenidos de texto
static uint32_t page_sig(const flex_dc_page_t *pg)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < pg->n; i++) {
        const flex_dc_op_t *o = &pg->op[i];
        if (is_vector(o->kind)) {
            continue;
        }
        int16_t v[7] = {o->x, o->y, o->w, o->h, o->r, o->mr, 0};
        uint8_t k[4] = {o->kind, o->role, o->size, o->surf};
        h = fnv(h, k, sizeof(k));
        h = fnv(h, v, sizeof(v));
    }
    return h;
}

static void rect_area(lv_area_t *a, int ox, int oy, int x, int y, int w, int h)
{
    a->x1 = ox + x;
    a->y1 = oy + y;
    a->x2 = ox + x + (w > 0 ? w : 1) - 1;
    a->y2 = oy + y + (h > 0 ? h : 1) - 1;
}

static void vec_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    vec_t *v = lv_event_get_user_data(e);
    lv_layer_t *L = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    int ox = co.x1, oy = co.y1;
    for (int i = 0; i < v->n; i++) {
        const flex_dc_op_t *o = &v->op[i];
        lv_color_t col = flex_dc_ui_color(o->role);
        switch (o->kind) {
        case DCO_RECT:
        case DCO_RRECT: {
            if (o->kind == DCO_RECT && o->role == DCC_WINBG) {
                break;   // el fondo de la pantalla ya es el de la ventana
            }
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = col;
            d.bg_opa = LV_OPA_COVER;
            d.radius = o->kind == DCO_RRECT ? o->r : 0;
            lv_area_t a;
            rect_area(&a, ox, oy, o->x, o->y, o->w, o->h);
            lv_draw_rect(L, &d, &a);
            break;
        }
        case DCO_OUTLINE: {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_TRANSP;
            d.border_color = col;
            d.border_width = 1;
            d.border_opa = LV_OPA_COVER;
            d.radius = o->r;
            lv_area_t a;
            rect_area(&a, ox, oy, o->x, o->y, o->w, o->h);
            lv_draw_rect(L, &d, &a);
            break;
        }
        case DCO_DISC:
        case DCO_CIRCLE:
        case DCO_RING: {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.radius = LV_RADIUS_CIRCLE;
            if (o->kind == DCO_DISC) {
                d.bg_color = col;
                d.bg_opa = LV_OPA_COVER;
            } else {
                d.bg_opa = LV_OPA_TRANSP;
                d.border_color = col;
                d.border_width = o->kind == DCO_RING ? (o->th > 0 ? o->th : 1) : 1;   // anillo: radio exterior r y grosor th
                d.border_opa = LV_OPA_COVER;
            }
            lv_area_t a = {ox + o->x - o->r, oy + o->y - o->r, ox + o->x + o->r, oy + o->y + o->r};
            lv_draw_rect(L, &d, &a);
            break;
        }
        case DCO_ARC: {
            // arcStroke: radio f[2] sobre la LINEA CENTRAL y grosor th; LVGL: radio EXTERIOR
            float a0 = o->f[3], a1 = o->f[4];
            lv_draw_arc_dsc_t d;
            lv_draw_arc_dsc_init(&d);
            d.color = col;
            d.width = o->th > 0 ? o->th : 1;
            d.rounded = 1;   // strokeSeg con radio th/2: extremos redondos
            d.center.x = ox + (int32_t)lroundf(o->f[0]);
            d.center.y = oy + (int32_t)lroundf(o->f[1]);
            d.radius = (uint16_t)lroundf(o->f[2] + d.width / 2.0f);
            if (a1 - a0 >= 359.5f) {
                lv_draw_rect_dsc_t rd;   // vuelta entera: un anillo
                lv_draw_rect_dsc_init(&rd);
                rd.radius = LV_RADIUS_CIRCLE;
                rd.bg_opa = LV_OPA_TRANSP;
                rd.border_color = col;
                rd.border_width = d.width;
                rd.border_opa = LV_OPA_COVER;
                lv_area_t a = {d.center.x - d.radius, d.center.y - d.radius, d.center.x + d.radius, d.center.y + d.radius};
                lv_draw_rect(L, &rd, &a);
                break;
            }
            if (a1 - a0 <= 0.01f) {
                break;
            }
            float s = fmodf(a0, 360.0f);
            if (s < 0) {
                s += 360.0f;
            }
            d.start_angle = (lv_value_precise_t)lroundf(s);
            d.end_angle = (lv_value_precise_t)lroundf(fmodf(s + (a1 - a0), 360.0f));
            lv_draw_arc(L, &d);
            break;
        }
        case DCO_STROKE: {
            lv_draw_line_dsc_t d;
            lv_draw_line_dsc_init(&d);
            d.color = col;
            int wd = (int)lroundf(2.0f * o->f[4]);
            d.width = wd < 1 ? 1 : wd;
            d.round_start = 1;
            d.round_end = 1;
            d.p1.x = ox + lroundf(o->f[0]);
            d.p1.y = oy + lroundf(o->f[1]);
            d.p2.x = ox + lroundf(o->f[2]);
            d.p2.y = oy + lroundf(o->f[3]);
            lv_draw_line(L, &d);
            break;
        }
        case DCO_TRI: {
            lv_draw_triangle_dsc_t d;
            lv_draw_triangle_dsc_init(&d);
            d.color = col;
            d.opa = LV_OPA_COVER;
            for (int k = 0; k < 3; k++) {
                d.p[k].x = ox + o->p[2 * k];
                d.p[k].y = oy + o->p[2 * k + 1];
            }
            lv_draw_triangle(L, &d);
            break;
        }
        default:
            break;
        }
    }
}

static void vec_delete_cb(lv_event_t *e)
{
    vec_t *v = lv_event_get_user_data(e);
    if (v) {
        lv_free(v->op);
        lv_free(v->lab);
        lv_free(v);
    }
}

static int count_vec(const flex_dc_page_t *pg)
{
    int nv = 0;
    for (int i = 0; i < pg->n; i++) {
        nv += is_vector(pg->op[i].kind) ? 1 : 0;
    }
    return nv;
}

static bool load_vec(vec_t *v, const flex_dc_page_t *pg)
{
    int nv = count_vec(pg);
    if (nv > v->cap) {
        flex_dc_op_t *np = lv_malloc(sizeof(flex_dc_op_t) * (size_t)nv);
        if (!np) {
            return false;
        }
        lv_free(v->op);
        v->op = np;
        v->cap = nv;
    }
    v->n = 0;
    for (int i = 0; i < pg->n; i++) {
        if (is_vector(pg->op[i].kind)) {
            v->op[v->n++] = pg->op[i];
        }
    }
    return true;
}

lv_obj_t *flex_dc_ui_render(lv_obj_t *parent, const flex_dc_page_t *pg)
{
    return flex_dc_ui_render_h(parent, pg, 800);
}

static void put_label(vec_t *v, int *nl, lv_obj_t *l)
{
    if (v->lab && *nl < v->nlab) {
        v->lab[(*nl)++] = l;
    }
}

static int count_text(const flex_dc_page_t *pg)
{
    int n = 0;
    for (int i = 0; i < pg->n; i++) {
        int k = pg->op[i].kind;
        n += (k == DCO_TEXT || k == DCO_TEXT_C || k == DCO_TEXT_R || k == DCO_TEXT_CLIP) ? 1 : 0;
    }
    return n;
}

lv_obj_t *flex_dc_ui_render_h(lv_obj_t *parent, const flex_dc_page_t *pg, int height)
{
    s_builds++;
    lv_obj_t *box = flex_box(parent);
    lv_obj_set_pos(box, 0, 0);
    lv_obj_set_size(box, 480, height);
    lv_obj_set_clickable(box, false);
    // 1) superficies del sistema (debajo de todo)
    for (int i = 0; i < pg->n; i++) {
        const flex_dc_op_t *o = &pg->op[i];
        if (o->kind == DCO_SURF) {
            lv_obj_t *s = flex_box(box);
            lv_obj_set_pos(s, o->x, o->y);
            lv_obj_set_size(s, o->w > 0 ? o->w : 1, o->h > 0 ? o->h : 1);
            lv_obj_set_style_radius(s, o->r, 0);
            lv_obj_set_clickable(s, false);
            flex_surface(s, o->surf ? FLEX_SURF_ELEVATED : FLEX_SURF_CARD, FLEX_BD_FLAT);   // UIS_CARD / UIS_ELEVATED
        }
    }
    // 2) los vectores, en un solo objeto
    vec_t *v = lv_malloc(sizeof(*v));
    if (!v) {
        return box;
    }
    memset(v, 0, sizeof(*v));
    v->h = height;
    v->sig = page_sig(pg);
    v->nlab = count_text(pg);
    v->lab = v->nlab > 0 ? lv_malloc(sizeof(lv_obj_t *) * (size_t)v->nlab) : NULL;
    int nl = 0;
    if (count_vec(pg) > 0 && load_vec(v, pg)) {
        lv_obj_t *o = flex_box(box);
        lv_obj_set_pos(o, 0, 0);
        lv_obj_set_size(o, 480, height);
        lv_obj_set_clickable(o, false);
        lv_obj_add_event_cb(o, vec_draw_cb, LV_EVENT_DRAW_MAIN, v);
        v->vobj = o;
    }
    lv_obj_set_user_data(box, v);
    lv_obj_add_event_cb(box, vec_delete_cb, LV_EVENT_DELETE, v);
    // 3) los textos
    for (int i = 0; i < pg->n; i++) {
        const flex_dc_op_t *o = &pg->op[i];
        lv_color_t col = flex_dc_ui_color(o->role);
        switch (o->kind) {
        case DCO_TEXT:
            put_label(v, &nl, flex_kit_text(box, o->text, o->size, col, o->x, o->y, false));
            break;
        case DCO_TEXT_C:
            put_label(v, &nl, flex_kit_text(box, o->text, o->size, col, o->x, o->y, true));
            break;
        case DCO_TEXT_R: {   // drawTextR: el borde derecho del texto en x
            const lv_font_t *f = flex_ui_font(o->size);
            int w = (int)flex_text_width(o->text, f);
            put_label(v, &nl, flex_kit_text(box, o->text, o->size, col, o->x - w, o->y, false));
            break;
        }
        case DCO_TEXT_CLIP: {   // drawTextClip: se corta en `mr` (sin puntos suspensivos)
            lv_obj_t *l = flex_kit_text(box, o->text, o->size, col, o->x, o->y, false);
            int w = o->mr - o->x;
            flex_label_one_line(l, w > 1 ? w : 1);
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            put_label(v, &nl, l);
            break;
        }
        default:
            break;
        }
    }
    return box;
}

bool flex_dc_ui_update(lv_obj_t *box, const flex_dc_page_t *pg)
{
    vec_t *v = box ? lv_obj_get_user_data(box) : NULL;
    if (!v || v->sig != page_sig(pg)) {
        return false;
    }
    int nv = count_vec(pg);
    if ((nv > 0) != (v->vobj != NULL)) {
        return false;   // aparece o desaparece el objeto de dibujo: reconstruir
    }
    if (nv > 0 && !load_vec(v, pg)) {
        return false;
    }
    // los contenidos de texto (la cifra del anillo que cuenta, los porcentajes) se cambian en su etiqueta
    int nl = 0;
    for (int i = 0; i < pg->n; i++) {
        const flex_dc_op_t *o = &pg->op[i];
        if (o->kind != DCO_TEXT && o->kind != DCO_TEXT_C && o->kind != DCO_TEXT_R && o->kind != DCO_TEXT_CLIP) {
            continue;
        }
        lv_obj_t *l = nl < v->nlab ? v->lab[nl] : NULL;
        nl++;
        if (!l || !strcmp(lv_label_get_text(l), o->text)) {
            continue;
        }
        lv_label_set_text(l, o->text);
        if (o->kind == DCO_TEXT_R) {
            lv_obj_set_x(l, o->x - (int)flex_text_width(o->text, flex_ui_font(o->size)));
        }
    }
    if (v->vobj) {
        lv_obj_invalidate(v->vobj);
    }
    s_vec_updates++;
    return true;
}
