#include "flex_fk_model.h"

#include <string.h>

#define SCR_W 480
#define SCR_H 800

int flex_fk_menu_rows(bool cloud)
{
    return cloud ? 5 : 4;
}

void flex_fk_menu_geom(int px, int py, bool cloud, flex_fk_rect_t *r)
{
    r->w = FLEX_FK_MENU_W;
    r->h = flex_fk_menu_rows(cloud) * FLEX_FK_MENU_RH + 2 * FLEX_FK_MENU_PAD;
    r->x = px;
    r->y = py;
    if (r->x + r->w > SCR_W - 8) {
        r->x = SCR_W - 8 - r->w;
    }
    if (r->x < 8) {
        r->x = 8;
    }
    if (r->y + r->h > SCR_H - 70) {
        r->y = SCR_H - 70 - r->h;
    }
    if (r->y < 30) {
        r->y = 30;
    }
}

int flex_fk_menu_hit(const flex_fk_rect_t *r, bool cloud, int px, int py)
{
    if (px < r->x || px > r->x + r->w || py < r->y || py > r->y + r->h) {
        return -1;
    }
    int i = (py - r->y - FLEX_FK_MENU_PAD) / FLEX_FK_MENU_RH;
    if (i < 0) {
        i = 0;
    }
    if (i > flex_fk_menu_rows(cloud) - 1) {
        i = flex_fk_menu_rows(cloud) - 1;
    }
    return i;
}

void flex_fk_ask_geom(flex_fk_rect_t *card, flex_fk_rect_t *cancel, flex_fk_rect_t *ok)
{
    card->w = SCR_W - 72;
    card->h = 220;
    card->x = 36;
    card->y = (SCR_H - card->h) / 2;
    int by = card->y + card->h - 76, bw = (card->w - 48) / 2;
    cancel->x = card->x + 16;
    cancel->y = by;
    cancel->w = bw;
    cancel->h = 56;
    ok->x = card->x + 32 + bw;
    ok->y = by;
    ok->w = bw;
    ok->h = 56;
}

int flex_fk_ask_hit(int px, int py)
{
    flex_fk_rect_t c, no, yes;
    flex_fk_ask_geom(&c, &no, &yes);
    if (py >= no.y && py <= no.y + no.h) {
        if (px >= no.x && px <= no.x + no.w) {
            return -1;
        }
        if (px >= yes.x && px <= yes.x + yes.w) {
            return 1;
        }
    }
    if (px < c.x || px > c.x + c.w || py < c.y || py > c.y + c.h) {
        return -1;
    }
    return 0;
}

int flex_fk_trash_max_scroll(int n)
{
    int need = FLEX_FK_TRASH_TOP + n * FLEX_FK_TRASH_RH + 30;
    int m = need - (SCR_H - 140);
    return m > 0 ? m : 0;
}

void flex_fk_textbox_layout(int x, int y, int w, int h, const char *s, int size, int line_h, flex_fk_width_fn width,
                            void *wctx, flex_fk_line_fn emit, void *ectx)
{
    if (!s || !*s) {
        return;
    }
    int lh = line_h + 4, cy = y, li = 0;
    char line[80];
    const char *p = s;
    while (*p && cy + lh <= y + h) {
        unsigned char b = (unsigned char)*p;
        if (b == '\n' || b == '\r') {
            line[li] = 0;
            if (li) {
                emit(x, cy, line, ectx);
            }
            cy += lh;
            li = 0;
            p++;
            continue;
        }
        int cl = 1;
        if ((b & 0xE0) == 0xC0) {
            cl = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cl = 3;
        } else if ((b & 0xF8) == 0xF0) {
            cl = 4;
        }
        if (li + cl >= (int)sizeof(line) - 1) {
            line[li] = 0;
            emit(x, cy, line, ectx);
            cy += lh;
            li = 0;
        }
        // un texto truncado a mitad de un caracter (cl pasa del final) no se lee mas alla del NUL
        int avail = 0;
        while (avail < cl && p[avail]) {
            avail++;
        }
        memcpy(line + li, p, (size_t)avail);
        line[li + avail] = 0;
        if (width(line, size, wctx) > w) {
            if (li == 0) {
                p += avail;   // un solo caracter mas ancho que la caja: se descarta
                continue;
            }
            line[li] = 0;
            emit(x, cy, line, ectx);
            cy += lh;
            li = 0;
            continue;
        }
        li += avail;
        p += avail;
    }
    if (li && cy + lh <= y + h) {
        line[li] = 0;
        emit(x, cy, line, ectx);
    }
}

bool flex_fk_name_append(char *buf, size_t cap, const char *s)
{
    if (!buf || !s || cap == 0 || strchr(s, '/')) {
        return false;   // '/' partiria la ruta y dejaria el fichero en otra carpeta sin avisar
    }
    size_t L = strlen(buf), sl = strlen(s);
    if (L + sl >= cap - 1) {
        return false;
    }
    memcpy(buf + L, s, sl);
    buf[L + sl] = '\0';
    return true;
}

void flex_fk_name_back(char *buf)
{
    size_t L = buf ? strlen(buf) : 0;
    if (L > 0) {
        size_t q = L - 1;
        while (q > 0 && ((unsigned char)buf[q] & 0xC0) == 0x80) {
            q--;
        }
        buf[q] = '\0';
    }
}
