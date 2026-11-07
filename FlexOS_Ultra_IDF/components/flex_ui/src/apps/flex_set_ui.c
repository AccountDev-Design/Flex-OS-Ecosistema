// Flex OS Ultra · convierte la lista de operaciones de dibujo del modelo de Ajustes (flex_set_model) en objetos LVGL. La usan Ajustes y Ajustes del teclado.
#include "flex_set_ui.h"

#include <stdio.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_icons.h"
#include "flex_kit.h"
#include "flex_theme.h"

lv_obj_t *flex_set_box(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_color_t role_color(int role)
{
    const flex_palette_t *t = flex_th();
    switch (role) {
    case FLEX_SR_HI:
        return t->txt;
    case FLEX_SR_LO:
        return t->txt2;
    case FLEX_SR_MUTE:
        return t->mute;
    case FLEX_SR_DIS:
        return t->disabled;
    default:
        return t->on_acc;
    }
}

static bool inside(const flex_set_op_t *c, const flex_set_op_t *o)
{
    return o->x >= c->x && o->x <= c->x + c->w && o->y >= c->y && o->y <= c->y + c->h;
}

int flex_set_ops_render(lv_obj_t *parent, const flex_set_page_t *pg, int y_shift, lv_obj_t **cards, int ncards, lv_event_cb_t click_cb)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *card = NULL;
    const flex_set_op_t *cop = NULL;
    int made = 0;
    for (int i = 0; i < pg->n; i++) {
        const flex_set_op_t *o = &pg->op[i];
        if (o->kind == FLEX_SO_CARD) {
            card = flex_set_box(parent, o->x, o->y + y_shift, o->w, o->h, o->r);
            flex_surface(card, FLEX_SURF_CARD, FLEX_BD_FLAT);
            if (click_cb) {
                lv_obj_set_clickable(card, true);
                lv_obj_set_user_data(card, (void *)(intptr_t)made);
                lv_obj_add_event_cb(card, click_cb, LV_EVENT_CLICKED, NULL);
            }
            if (cards && made < ncards) {
                cards[made] = card;
            }
            made++;
            cop = o;
            continue;
        }
        lv_obj_t *par = parent;
        int x = o->x, y = o->y + y_shift;
        if (card && cop && inside(cop, o)) {
            par = card;   // lo que cae dentro de la tarjeta es suyo: sus coordenadas pasan a ser las de la tarjeta
            x = o->x - cop->x;
            y = o->y - cop->y;
        } else {
            card = NULL;
            cop = NULL;
        }
        switch (o->kind) {
        case FLEX_SO_GLYPH: {
            lv_obj_t *g = flex_glyph_create(par, FLEX_GLYPH_SET_ROW, 24, 24);
            flex_glyph_set(g, FLEX_GLYPH_SET_ROW, o->glyph);
            lv_obj_set_pos(g, x - 12, y - 12);
            lv_obj_set_style_text_color(g, flex_c565(o->color), 0);
            lv_obj_set_style_bg_color(g, t->surf, 0);   // el hueco de la chincheta: el color de la tarjeta
            lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
            break;
        }
        case FLEX_SO_CATICON: {
            lv_obj_t *g = flex_glyph_create(par, FLEX_GLYPH_SET_CAT, o->w, o->w);
            flex_glyph_set(g, FLEX_GLYPH_SET_CAT, o->glyph);
            lv_obj_set_pos(g, x, y);
            lv_obj_set_style_text_color(g, flex_c565(o->color), 0);
            lv_obj_set_style_bg_color(g, t->surf, 0);
            lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
            break;
        }
        case FLEX_SO_TEXT: {
            lv_obj_t *l = flex_kit_text(par, o->text, o->size, role_color(o->role), x, y, false);
            if (o->max_right > 0) {   // recorte por la derecha SIN puntos suspensivos (drawTextClip)
                int w = o->max_right - o->x;
                flex_label_one_line(l, w > 1 ? w : 1);
                lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            }
            break;
        }
        case FLEX_SO_TEXT_R: {
            lv_obj_t *l = flex_kit_text(par, o->text, o->size, role_color(o->role), 0, y, false);
            flex_label_one_line(l, x > 1 ? x : 1);   // alineado a la derecha en x
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_x(l, 0);
            break;
        }
        case FLEX_SO_TEXT_C:
            flex_kit_text(par, o->text, o->size, role_color(o->role), x, y, true);
            break;
        case FLEX_SO_CHEV: {
            lv_obj_t *g = flex_glyph_create(par, FLEX_GLYPH_CHEVRON_RIGHT, 10, 16);
            lv_obj_set_pos(g, x - 5, y - 8);
            lv_obj_set_style_text_color(g, t->mute, 0);
            break;
        }
        case FLEX_SO_BACK: {
            lv_obj_t *g = flex_glyph_create(par, FLEX_GLYPH_CHEVRON_LEFT, 16, 28);
            lv_obj_set_pos(g, 16, 44 + y_shift);
            lv_obj_set_style_text_color(g, t->txt, 0);
            break;
        }
        case FLEX_SO_FRAME: {   // campo enfocado: contorno del color de acento
            lv_obj_t *f = flex_set_box(par, x, y, o->w, o->h, o->r);
            lv_obj_set_style_border_width(f, 2, 0);
            lv_obj_set_style_border_color(f, t->primary, 0);
            lv_obj_set_style_border_opa(f, LV_OPA_COVER, 0);
            break;
        }
        case FLEX_SO_BTN: {
            lv_obj_t *b = flex_set_box(par, x, y, o->w, o->h, o->r);
            lv_obj_set_style_bg_color(b, o->glyph == 0 ? t->primary : t->danger, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            break;
        }
        default:
            break;
        }
    }
    return made;
}
