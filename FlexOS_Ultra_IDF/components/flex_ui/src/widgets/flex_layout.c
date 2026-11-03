#include "flex_layout.h"

void flex_ui_box(int canvas_w, int canvas_h, int *w, int *h)
{
    *w = canvas_w < 32 ? 32 : canvas_w;
    *h = canvas_h < 32 ? 32 : canvas_h;
}

int flex_ui_pad(int w, int h)
{
    int m = (w < h ? w : h) / 24;
    if (m < 5) {
        m = 5;
    }
    if (m > 22) {
        m = 22;
    }
    return m;
}

int flex_ui_gap(int pad)
{
    int g = pad * 3 / 4;
    return g < 4 ? 4 : g;
}

int flex_ui_font_h(int line_h)
{
    if (line_h >= 44) return 5;
    if (line_h >= 33) return 4;
    if (line_h >= 23) return 3;
    if (line_h >= 14) return 2;
    return 1;
}

int flex_ui_line_h(int fs)
{
    return fs <= 1 ? 8 : fs * 9;
}

int flex_ui_font_fit(const char *t, int max_w, int max_size, flex_ui_textw_fn text_w, void *ctx)
{
    int fs = max_size;
    if (fs > 5) fs = 5;
    if (fs < 1) fs = 1;
    while (fs > 1 && text_w(t, fs, ctx) > max_w) {
        fs--;
    }
    return fs;
}
