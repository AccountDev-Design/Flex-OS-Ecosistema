// Flex OS Ultra · convierte la lista de operaciones de Almacenamiento y Archivos en objetos LVGL (ver flex_stor_ui.h).
#include "flex_stor_ui.h"

#include <string.h>
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_icons.h"
#include "flex_kit.h"
#include "flex_theme.h"

lv_color_t flex_st_ui_color(int role, uint16_t rgb)
{
    const flex_palette_t *t = flex_th();
    switch (role) {
    case FSC_TXT:
        return t->txt;
    case FSC_TXT2:
        return t->txt2;
    case FSC_MUTE:
        return t->mute;
    case FSC_OK:
        return t->ok;
    case FSC_WARN:
        return t->warn;
    case FSC_ERR:
        return t->err;
    case FSC_PRIM:
        return t->primary;
    case FSC_ONACC:
        return t->on_acc;
    case FSC_ACCS:
        return t->acc_soft;
    case FSC_TRACK:
        return t->track;
    case FSC_BORDER:
        return t->border;
    case FSC_SURF:
        return t->surf;
    case FSC_ACCENT:
        return flex_accent();
    case FSC_SEL:
        return t->sel;
    case FSC_SURF2:
        return t->surf2;
    case FSC_RGB:
    default:
        return flex_c565(rgb);
    }
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w > 0 ? w : 1, h > 0 ? h : 1);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void fill(lv_obj_t *o, lv_color_t c)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

int flex_st_ui_render(lv_obj_t *parent, const flex_st_page_t *pg, int dx, int dy)
{
    const flex_palette_t *t = flex_th();
    int made = 0;
    for (int i = 0; i < pg->n; i++) {
        const flex_st_op_t *o = &pg->op[i];
        int x = o->x + dx, y = o->y + dy;
        switch (o->kind) {
        case FST_SURF: {
            lv_obj_t *s = box(parent, x, y, o->w, o->h, o->r);
            flex_surface(s, o->role == FSC_SURF_ELEV ? FLEX_SURF_ELEVATED : FLEX_SURF_CARD, FLEX_BD_FLAT);
            break;
        }
        case FST_RRECT:
            fill(box(parent, x, y, o->w, o->h, o->r), flex_st_ui_color(o->role, o->rgb));
            break;
        case FST_OUTLINE: {
            lv_obj_t *b = box(parent, x, y, o->w, o->h, o->r);
            lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, flex_st_ui_color(o->role, o->rgb), 0);
            lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
            break;
        }
        case FST_DOT: {   // circulo relleno: centro (x, y), radio r
            lv_obj_t *d = box(parent, x - o->r, y - o->r, 2 * o->r + 1, 2 * o->r + 1, LV_RADIUS_CIRCLE);
            fill(d, flex_st_ui_color(o->role, o->rgb));
            break;
        }
        case FST_TEXT: {
            lv_obj_t *l = flex_kit_text(parent, o->text, o->size, flex_st_ui_color(o->role, o->rgb), x, y, false);
            if (o->max_right > 0) {   // recorte por la derecha SIN puntos suspensivos (drawTextClip)
                int w = o->max_right - o->x;
                flex_label_one_line(l, w > 1 ? w : 1);
                lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            }
            break;
        }
        case FST_TEXT_R: {
            lv_obj_t *l = flex_kit_text(parent, o->text, o->size, flex_st_ui_color(o->role, o->rgb), 0, y, false);
            int w = x - dx > 1 ? x : 1;
            flex_label_one_line(l, w);   // alineado a la derecha en x
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_x(l, 0);
            break;
        }
        case FST_TEXT_C:
            flex_kit_text(parent, o->text, o->size, flex_st_ui_color(o->role, o->rgb), x, y, true);
            break;
        case FST_FOLDER: {
            flex_st_rect_t r[3];
            flex_st_folder_rects(x, y, o->w, r);
            for (int k = 0; k < 3; k++) {
                fill(box(parent, r[k].x, r[k].y, r[k].w, r[k].h, r[k].r), flex_c565(r[k].rgb));
            }
            break;
        }
        case FST_CHEV: {   // los dos trazos ">" desde (x, y): 8 de ancho y 15 de alto
            lv_obj_t *g = flex_glyph_create(parent, FLEX_GLYPH_CHEVRON_RIGHT, 10, 16);
            lv_obj_set_pos(g, x - 1, y - 1);
            lv_obj_set_style_text_color(g, flex_st_ui_color(o->role, o->rgb), 0);
            break;
        }
        case FST_CLOUD: {   // ckCloudGlyph(x, y, r): el glifo se centra en el objeto
            int s = o->r, side = s * 3;
            lv_obj_t *g = flex_glyph_create(parent, FLEX_GLYPH_CLOUD, side, side);
            flex_glyph_set(g, FLEX_GLYPH_CLOUD, s);
            lv_obj_set_pos(g, x - side / 2, y - side / 2);
            lv_obj_set_style_text_color(g, flex_st_ui_color(o->role, o->rgb), 0);
            break;
        }
        default:
            continue;
        }
        made++;
    }
    (void)t;
    return made;
}
