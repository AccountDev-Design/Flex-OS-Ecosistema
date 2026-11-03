#include "flex_note_model.h"

#include <string.h>

#define SCR_W 480
#define SCR_H 800

int flex_nl_card_h(void)
{
    int h = (SCR_H - FLEX_NL_CARD_TOP - 90) / 2 - 44;
    if (h < 120) {
        h = 120;
    }
    if (h > 300) {
        h = 300;
    }
    return h;
}

void flex_nl_card_rect(int i, int scroll, int *x, int *y, int *w, int *h)
{
    int col = i % FLEX_NL_COLS, row = i / FLEX_NL_COLS;
    *w = (SCR_W - 3 * 16) / FLEX_NL_COLS;
    *h = flex_nl_card_h();
    *x = 16 + col * (*w + 16);
    *y = FLEX_NL_CARD_TOP + row * (*h + 44) - scroll;
}

int flex_nl_max_scroll(int n)
{
    int rows = (n + FLEX_NL_COLS - 1) / FLEX_NL_COLS;
    int need = FLEX_NL_CARD_TOP + rows * (flex_nl_card_h() + 44) + 40;
    int m = need - (SCR_H - 60);
    return m > 0 ? m : 0;
}

int flex_nl_card_at(int n, int scroll, int px, int py)
{
    for (int i = 0; i < n; i++) {
        int x, y, w, h;
        flex_nl_card_rect(i, scroll, &x, &y, &w, &h);
        if (px >= x && px <= x + w && py >= y && py <= y + h) {
            return i;
        }
    }
    return -1;
}

void flex_nl_fab(int *x, int *y, int *r)
{
    *r = 56;
    *x = SCR_W - 84;
    *y = SCR_H - 150;
}

bool flex_nl_fab_hit(int px, int py)
{
    int fx, fy, fr;
    flex_nl_fab(&fx, &fy, &fr);
    long dx = px - fx, dy = py - fy;
    return dx * dx + dy * dy <= (long)fr * fr;
}

int flex_nl_bar_hit(int px, int py)
{
    int by = SCR_H - 128;
    if (py < by || py > by + 60) {
        return 0;
    }
    if (px > SCR_W - 100) {
        return 1;
    }
    if (px > SCR_W - 230) {
        return 2;
    }
    return 0;
}

void flex_note_sess_pack(flex_note_sess_t *s, bool editor_open, const char *path, uint8_t kb_layout, uint8_t kb_flags, int cursor,
                         int len, int sel_a, int sel_b, int editor_scroll, int list_scroll)
{
    memset(s, 0, sizeof(*s));
    s->view = (editor_open && path && path[0]) ? 1 : 0;
    s->kb_layout = kb_layout;
    s->kb_flags = kb_flags;
    int c = cursor;
    if (c < 0) {
        c = 0;
    }
    if (c > len) {
        c = len;
    }
    s->cursor = (uint16_t)c;
    s->sel_a = (int16_t)sel_a;
    s->sel_b = (int16_t)sel_b;
    s->editor_scroll = editor_scroll;
    s->list_scroll = list_scroll;
    if (path) {
        strncpy(s->path, path, sizeof(s->path) - 1);
    }
}

void flex_note_sess_normalize(flex_note_sess_t *s)
{
    s->path[sizeof(s->path) - 1] = '\0';
    if (!(s->view == 1 && s->path[0])) {
        s->view = 0;
        s->path[0] = '\0';
    }
    if (s->kb_layout >= 4) {
        s->kb_layout = 0;
    }
    if (s->editor_scroll < 0) {
        s->editor_scroll = 0;
    }
    if (s->list_scroll < 0) {
        s->list_scroll = 0;
    }
}

bool flex_note_sel_valid(int sel_a, int sel_b, int len)
{
    return !(sel_a < 0 || sel_b <= sel_a || sel_b > len);
}
