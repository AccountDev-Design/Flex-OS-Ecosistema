#include "flex_ned.h"

#include <stdlib.h>
#include <string.h>

// ---- UTF-8 (FlexOS_Ultra_Keyboard.h:764-766, Font.h:1702-1745) -------------------------------------------------
int flex_ned_utf8_prev(const char *s, int i)
{
    if (i <= 0) {
        return 0;
    }
    i--;
    while (i > 0 && (s[i] & 0xC0) == 0x80) {
        i--;
    }
    return i;
}

int flex_ned_utf8_next(const char *s, int i)
{
    int L = (int)strlen(s);
    if (i >= L) {
        return L;
    }
    i++;
    while (i < L && (s[i] & 0xC0) == 0x80) {
        i++;
    }
    return i;
}

bool flex_ned_is_word_byte(unsigned char c)
{
    return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static uint32_t utf8_decode(const char **ps)
{
    const uint8_t *s = (const uint8_t *)*ps;
    const uint8_t b = *s++;
    uint32_t cp;
    int extra;
    if (b < 0x80) {
        *ps = (const char *)s;
        return b;
    }
    if ((b & 0xE0) == 0xC0) {
        cp = b & 0x1F;
        extra = 1;
    } else if ((b & 0xF0) == 0xE0) {
        cp = b & 0x0F;
        extra = 2;
    } else if ((b & 0xF8) == 0xF0) {
        cp = b & 0x07;
        extra = 3;
    } else {
        *ps = (const char *)s;
        return 0xFFFD;   // byte suelto o invalido
    }
    for (int k = 0; k < extra; k++) {
        if ((*s & 0xC0) != 0x80) {   // secuencia cortada: se para SIN comerse el byte siguiente
            *ps = (const char *)s;
            return 0xFFFD;
        }
        cp = (cp << 6) | (uint32_t)(*s++ & 0x3F);
    }
    *ps = (const char *)s;
    return cp;
}

static bool cp_zero_width(uint32_t cp)
{
    return cp == 0x00AD || (cp >= 0x200B && cp <= 0x200F) || cp == 0x2060 || cp == 0xFEFF || (cp >= 0xFE00 && cp <= 0xFE0F);
}

uint32_t flex_ned_fold_cp(uint32_t cp)
{
    if (cp < 0x80) {
        return cp;
    }
    if (cp >= 0x2000 && cp <= 0x200A) {
        return ' ';   // espacios de ancho fijo
    }
    switch (cp) {
    case 0x00A0: case 0x1680: case 0x202F: case 0x205F: case 0x3000:
        return ' ';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212:
        return '-';
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032:
        return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x00AB: case 0x00BB: case 0x2033:
        return '"';
    case 0x2022: case 0x2027: case 0x2219:
        return 0xB7;   // vinetas -> punto medio
    case 0x2026:
        return '.';
    default:
        return cp;
    }
}

uint32_t flex_ned_next_cp(const char **ps)
{
    for (;;) {
        const uint32_t cp = utf8_decode(ps);
        if (!cp_zero_width(cp)) {
            return flex_ned_fold_cp(cp);
        }
        if (!**ps) {
            return ' ';
        }
    }
}

// ---- edicion (Keyboard.h:767-822) ---------------------------------------------------------------------------------
void flex_ned_init(flex_ned_t *e, char *buf, size_t cap)
{
    memset(e, 0, sizeof(*e));
    e->buf = buf;
    e->cap = cap;
    e->sel_a = e->sel_b = -1;
    if (buf && cap) {
        buf[0] = '\0';
    }
}

bool flex_ned_has_sel(const flex_ned_t *e)
{
    return e->sel_a >= 0 && e->sel_b > e->sel_a;
}

void flex_ned_clear_sel(flex_ned_t *e)
{
    e->sel_a = e->sel_b = -1;
    e->menu = false;
}

void flex_ned_delete_sel(flex_ned_t *e)
{
    if (!flex_ned_has_sel(e)) {
        return;
    }
    int a = e->sel_a, b = e->sel_b, L = (int)strlen(e->buf);
    if (b > L) {
        b = L;
    }
    if (a > b) {
        a = b;
    }
    memmove(e->buf + a, e->buf + b, (size_t)(L - b + 1));
    e->cur = a;
    flex_ned_clear_sel(e);
    e->follow = true;
    e->rev++;
}

bool flex_ned_insert(flex_ned_t *e, const char *s)
{
    int L = (int)strlen(e->buf), sl = (int)strlen(s);
    int a = 0, b = 0;
    if (flex_ned_has_sel(e)) {
        a = e->sel_a;
        b = e->sel_b > L ? L : e->sel_b;
        if (a > b) {
            a = b;
        }
    }
    // cabe contando lo que la seleccion libera: si no, no se toca NADA (Arduino la borraba y luego rechazaba)
    if (L - (b - a) + sl >= (int)e->cap - 1) {
        return false;
    }
    if (b > a) {
        flex_ned_delete_sel(e);
        L = (int)strlen(e->buf);
    }
    if (e->cur < 0) {
        e->cur = 0;
    }
    if (e->cur > L) {
        e->cur = L;
    }
    memmove(e->buf + e->cur + sl, e->buf + e->cur, (size_t)(L - e->cur + 1));
    memcpy(e->buf + e->cur, s, (size_t)sl);
    e->cur += sl;
    e->menu = false;
    e->follow = true;
    e->rev++;
    return true;
}

void flex_ned_backspace(flex_ned_t *e)
{
    if (flex_ned_has_sel(e)) {
        flex_ned_delete_sel(e);
        return;
    }
    int L = (int)strlen(e->buf);
    if (e->cur > L) {
        e->cur = L;
    }
    if (e->cur <= 0) {
        return;
    }
    int p = flex_ned_utf8_prev(e->buf, e->cur);
    memmove(e->buf + p, e->buf + e->cur, (size_t)(L - e->cur + 1));
    e->cur = p;
    e->menu = false;
    e->follow = true;
    e->rev++;
}

void flex_ned_select_word_at(flex_ned_t *e, int bi)
{
    int L = (int)strlen(e->buf);
    if (L == 0) {
        return;
    }
    if (bi >= L) {
        bi = flex_ned_utf8_prev(e->buf, L);
    }
    if (bi < 0) {
        bi = 0;
    }
    int a = bi;
    while (a > 0) {
        int p = flex_ned_utf8_prev(e->buf, a);
        if (!flex_ned_is_word_byte((unsigned char)e->buf[p])) {
            break;
        }
        a = p;
    }
    int b = bi;
    while (b < L) {
        if (!flex_ned_is_word_byte((unsigned char)e->buf[b])) {
            break;
        }
        b = flex_ned_utf8_next(e->buf, b);
    }
    if (b > a) {
        e->sel_a = a;
        e->sel_b = b;
        e->cur = b;
        e->menu = true;
    }
}

void flex_ned_select_all(flex_ned_t *e)
{
    e->sel_a = 0;
    e->sel_b = (int)strlen(e->buf);
    e->cur = e->sel_b;
    e->menu = flex_ned_has_sel(e);
}

int flex_ned_copy_sel(const flex_ned_t *e, char *out, size_t cap)
{
    out[0] = '\0';
    if (!flex_ned_has_sel(e) || cap == 0) {
        return 0;
    }
    int a = e->sel_a, n = e->sel_b - a, L = (int)strlen(e->buf);
    if (a + n > L) {
        n = L - a;
    }
    if (n >= (int)cap) {
        n = (int)cap - 1;
        while (n > 0 && ((unsigned char)e->buf[a + n] & 0xC0) == 0x80) {
            n--;   // no a mitad de un caracter
        }
    }
    memcpy(out, e->buf + a, (size_t)n);
    out[n] = '\0';
    return n;
}

int flex_ned_current_word(const char *buf, int cur, char *out, int outsz)
{
    out[0] = 0;
    if (!buf || cur <= 0 || outsz <= 1) {
        return 0;
    }
    int a = cur;
    while (a > 0) {
        unsigned char c = (unsigned char)buf[a - 1];
        if (c == ' ' || c == '\n' || c == '\t') {
            break;
        }
        a--;
    }
    int n = cur - a;
    if (n > outsz - 1) {
        n = outsz - 1;
    }
    memcpy(out, buf + a, (size_t)n);
    out[n] = 0;
    return n;
}

// ---- recorrido (Keyboard.h:862-908, 1007-1021) -------------------------------------------------------------------------
int flex_ned_walk(const flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int want_bi, int *out_y)
{
    int x = FLEX_NED_X0, y = FLEX_NED_TOP;
    const char *s = e->buf;
    int bi = 0;
    if (out_y) {
        *out_y = y;
    }
    while (*s) {
        if (bi == want_bi && out_y) {
            *out_y = y;
        }
        if (*s == '\n') {
            s++;
            bi++;
            x = FLEX_NED_X0;
            y += FLEX_NED_LH;
            continue;
        }
        const char *save = s;
        uint32_t cp = flex_ned_next_cp(&s);
        int nb = (int)(s - save);
        int w = adv(cp, ctx);
        if (x + w > FLEX_NED_MAXX) {
            x = FLEX_NED_X0;
            y += FLEX_NED_LH;
        }
        if (bi == want_bi && out_y) {
            *out_y = y;
        }
        x += w;
        bi += nb;
    }
    if (bi == want_bi && out_y) {
        *out_y = y;
    }
    return y + FLEX_NED_LH - FLEX_NED_TOP;   // alto total del contenido
}

static long dist(int px, int py, int x, int y)
{
    return (long)abs(px - x) + (long)abs(py - (y + 8)) * 2;
}

int flex_ned_hit(const flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int px, int py)
{
    int x = FLEX_NED_X0, y = FLEX_NED_TOP - e->scroll;
    const char *s = e->buf;
    int bi = 0, best = 0;
    long bestd = 1L << 30;
    while (*s) {
        if (*s == '\n') {
            long d = dist(px, py, x, y);
            if (d < bestd) {
                bestd = d;
                best = bi;
            }
            s++;
            bi++;
            x = FLEX_NED_X0;
            y += FLEX_NED_LH;
            continue;
        }
        const char *save = s;
        uint32_t cp = flex_ned_next_cp(&s);
        int nb = (int)(s - save);
        int w = adv(cp, ctx);
        if (x + w > FLEX_NED_MAXX) {
            x = FLEX_NED_X0;
            y += FLEX_NED_LH;
        }
        long d0 = dist(px, py, x, y);
        if (d0 < bestd) {
            bestd = d0;
            best = bi;
        }
        long d1 = dist(px, py, x + w, y);
        if (d1 < bestd) {
            bestd = d1;
            best = bi + nb;
        }
        x += w;
        bi += nb;
    }
    long d = dist(px, py, x, y);
    if (d < bestd) {
        best = bi;
    }
    return best;
}

int flex_ned_max_scroll(const flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int txt_bot)
{
    int visible = (txt_bot - 22) - FLEX_NED_TOP;
    if (visible < FLEX_NED_LH) {
        visible = FLEX_NED_LH;
    }
    int m = flex_ned_walk(e, adv, ctx, -1, NULL) - visible;
    return m > 0 ? m : 0;
}

void flex_ned_clamp_scroll(flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int txt_bot)
{
    int m = flex_ned_max_scroll(e, adv, ctx, txt_bot);
    if (e->scroll > m) {
        e->scroll = m;
    }
    if (e->scroll < 0) {
        e->scroll = 0;
    }
}

void flex_ned_ensure_cursor(flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int txt_bot)
{
    int cy = FLEX_NED_TOP;
    flex_ned_walk(e, adv, ctx, e->cur, &cy);
    int top = 56, bot = txt_bot - 8;
    if (cy - e->scroll < top) {
        e->scroll = cy - top;
    }
    if (cy - e->scroll + FLEX_NED_LH > bot) {
        e->scroll = cy + FLEX_NED_LH - bot;
    }
    flex_ned_clamp_scroll(e, adv, ctx, txt_bot);
}

// ---- dibujo: el mismo recorrido de noteDrawText (Keyboard.h:909-1005) ---------------------------------------------------------
static void spell_word(const flex_ned_t *e, const flex_ned_scan_t *c, int ws_bi, int bi, int ws_x, int ws_y, int x, int y)
{
    char w[40];
    int wn = bi - ws_bi;
    if (wn > 39) {
        wn = 39;
    }
    memcpy(w, e->buf + ws_bi, (size_t)wn);
    w[wn] = 0;
    // misma linea, >= 3 bytes y que no este en el diccionario; solo si la raya cae en lo visible
    if (ws_y == y && wn >= 3 && y <= c->y_break && !c->has_word(w, c->user)) {
        c->misspelled(ws_x, x, y, c->user);
    }
}

void flex_ned_scan(const flex_ned_t *e, const flex_ned_scan_t *c, flex_ned_marks_t *m)
{
    int x = FLEX_NED_X0, y = FLEX_NED_TOP - e->scroll;
    const char *s = e->buf;
    int bi = 0;
    bool has_sel = flex_ned_has_sel(e);
    bool spell = c->has_word && c->misspelled;
    int ws_x = x, ws_y = y, ws_bi = 0;
    bool in_word = false;
    flex_ned_marks_t mk = {FLEX_NED_X0, y, FLEX_NED_X0, y, FLEX_NED_X0, y};
#define MARKS()                              \
    do {                                     \
        if (bi == e->cur) {                  \
            mk.cur_x = x;                    \
            mk.cur_y = y;                    \
        }                                    \
        if (bi == e->sel_a) {                \
            mk.a_x = x;                      \
            mk.a_y = y;                      \
        }                                    \
        if (bi == e->sel_b) {                \
            mk.b_x = x;                      \
            mk.b_y = y;                      \
        }                                    \
    } while (0)
    while (*s) {
        if (*s == '\n') {
            if (spell && in_word) {
                spell_word(e, c, ws_bi, bi, ws_x, ws_y, x, y);
                in_word = false;
            }
            MARKS();
            s++;
            bi++;
            x = FLEX_NED_X0;
            y += FLEX_NED_LH;
            continue;
        }
        const char *save = s;
        uint32_t cp = flex_ned_next_cp(&s);
        int nb = (int)(s - save);
        int w = c->adv(cp, c->ctx);
        if (x + w > FLEX_NED_MAXX) {
            x = FLEX_NED_X0;
            y += FLEX_NED_LH;
        }
        if (spell) {
            bool wb = flex_ned_is_word_byte((unsigned char)*save);
            if (wb && !in_word) {
                in_word = true;
                ws_x = x;
                ws_y = y;
                ws_bi = bi;
            } else if (!wb && in_word) {
                spell_word(e, c, ws_bi, bi, ws_x, ws_y, x, y);
                in_word = false;
            }
        }
        MARKS();
        if (c->glyph) {
            flex_ned_glyph_t g = {bi, nb, cp, x, y, w, has_sel && bi >= e->sel_a && bi < e->sel_b,
                                  y + FLEX_NED_LH > FLEX_NED_SHEET_TOP && y <= c->y_break};
            if (g.visible) {
                c->glyph(&g, c->user);
            }
        }
        x += w;
        bi += nb;
    }
    // ultima palabra: si el cursor esta justo ahi se esta escribiendo TODAVIA, y marcarla seria ruido
    if (spell && in_word && e->cur != bi) {
        spell_word(e, c, ws_bi, bi, ws_x, ws_y, x, y);
    }
    MARKS();
#undef MARKS
    if (m) {
        *m = mk;
    }
}

// ---- menu flotante de seleccion --------------------------------------------------------------------------------------------------
void flex_ned_menu_geom(int handle_a_y, flex_ned_menu_t *m)
{
    m->bw = 92;
    m->gap = 4;
    int tot = 4 * m->bw + 3 * m->gap;
    m->x = (480 - tot) / 2;
    m->y = handle_a_y - 44;
    if (m->y < 50) {
        m->y = 50;
    }
    m->panel_x = m->x - 6;
    m->panel_y = m->y - 6;
    m->panel_w = tot + 12;
    m->panel_h = 40;
}

int flex_ned_menu_hit(const flex_ned_menu_t *m, int px, int py)
{
    if (py < m->y || py > m->y + 28) {
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        int bx = m->x + i * (m->bw + m->gap);
        if (px >= bx && px <= bx + m->bw) {
            return i;
        }
    }
    return -1;
}
