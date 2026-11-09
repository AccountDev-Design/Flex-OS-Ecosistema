// Flex OS Ultra · Device Care: las pantallas como operaciones de dibujo (ver flex_dc_screens.h). Cada funcion repite, expresion por expresion, la de dibujo de
// FlexOS_Ultra_DeviceCare.h / FlexOS_Ultra_DeviceTests.h.
#include "flex_dc_screens.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_clock.h"

#define T_(id) flex_dc_t((id), c->li)

// ---- operaciones ----
static flex_dc_op_t *add(flex_dc_page_t *pg, int kind)
{
    if (pg->n >= FLEX_DC_OPS_MAX) {
        return NULL;
    }
    flex_dc_op_t *o = &pg->op[pg->n++];
    memset(o, 0, sizeof(*o));
    o->kind = (uint8_t)kind;
    return o;
}
static void box_(flex_dc_page_t *pg, int kind, int x, int y, int w, int h, int r, int role)
{
    flex_dc_op_t *o = add(pg, kind);
    if (o) {
        o->x = (int16_t)x;
        o->y = (int16_t)y;
        o->w = (int16_t)w;
        o->h = (int16_t)h;
        o->r = (int16_t)r;
        o->role = (uint8_t)role;
    }
}
static void surf(flex_dc_page_t *pg, int x, int y, int w, int h, int r) { box_(pg, DCO_SURF, x, y, w, h, r, DCC_SURF); }
static void rect(flex_dc_page_t *pg, int x, int y, int w, int h, int role) { box_(pg, DCO_RECT, x, y, w, h, 0, role); }
static void rrect(flex_dc_page_t *pg, int x, int y, int w, int h, int r, int role) { box_(pg, DCO_RRECT, x, y, w, h, r, role); }
static void outline(flex_dc_page_t *pg, int x, int y, int w, int h, int r, int role) { box_(pg, DCO_OUTLINE, x, y, w, h, r, role); }
static void disc(flex_dc_page_t *pg, int cx, int cy, int r, int role) { box_(pg, DCO_DISC, cx, cy, 0, 0, r, role); }
static void circ(flex_dc_page_t *pg, int cx, int cy, int r, int role) { box_(pg, DCO_CIRCLE, cx, cy, 0, 0, r, role); }
static void ring(flex_dc_page_t *pg, int cx, int cy, int r, int th, int role)
{
    flex_dc_op_t *o = add(pg, DCO_RING);
    if (o) {
        o->x = (int16_t)cx;
        o->y = (int16_t)cy;
        o->r = (int16_t)r;
        o->th = (int16_t)th;
        o->role = (uint8_t)role;
    }
}
static void arc(flex_dc_page_t *pg, float cx, float cy, float r, float a0, float a1, int th, int role)
{
    flex_dc_op_t *o = add(pg, DCO_ARC);
    if (o) {
        o->f[0] = cx;
        o->f[1] = cy;
        o->f[2] = r;
        o->f[3] = a0;
        o->f[4] = a1;
        o->th = (int16_t)th;
        o->role = (uint8_t)role;
    }
}
static void stroke(flex_dc_page_t *pg, float x0, float y0, float x1, float y1, float rad, int role)
{
    flex_dc_op_t *o = add(pg, DCO_STROKE);
    if (o) {
        o->f[0] = x0;
        o->f[1] = y0;
        o->f[2] = x1;
        o->f[3] = y1;
        o->f[4] = rad;
        o->role = (uint8_t)role;
    }
}
static void tri(flex_dc_page_t *pg, int x0, int y0, int x1, int y1, int x2, int y2, int role)
{
    flex_dc_op_t *o = add(pg, DCO_TRI);
    if (o) {
        o->p[0] = (int16_t)x0; o->p[1] = (int16_t)y0; o->p[2] = (int16_t)x1; o->p[3] = (int16_t)y1; o->p[4] = (int16_t)x2; o->p[5] = (int16_t)y2;
        o->role = (uint8_t)role;
    }
}
static void text(flex_dc_page_t *pg, int kind, int x, int y, int size, int role, const char *s, int mr)
{
    flex_dc_op_t *o = add(pg, kind);
    if (o) {
        o->x = (int16_t)x;
        o->y = (int16_t)y;
        o->size = (uint8_t)size;
        o->role = (uint8_t)role;
        o->mr = (int16_t)mr;
        snprintf(o->text, sizeof(o->text), "%.*s", (int)sizeof(o->text) - 1, s);
    }
}
static void hit_add(flex_dc_page_t *pg, int x, int y, int w, int h, int id)
{
    if (pg->nhit >= FLEX_DC_HITS_MAX) {
        return;
    }
    pg->hit[pg->nhit].x0 = (int16_t)x;
    pg->hit[pg->nhit].y0 = (int16_t)y;
    pg->hit[pg->nhit].x1 = (int16_t)(x + w);
    pg->hit[pg->nhit].y1 = (int16_t)(y + h);
    pg->hit[pg->nhit].id = (uint8_t)id;
    pg->nhit++;
}
int flex_dc_hit_test(const flex_dc_page_t *pg, int px, int py)
{
    for (int i = 0; i < pg->nhit; i++) {
        if (px >= pg->hit[i].x0 && px < pg->hit[i].x1 && py >= pg->hit[i].y0 && py < pg->hit[i].y1) {
            return pg->hit[i].id;
        }
    }
    return -1;
}

static int pad_of(const flex_dc_ctx_t *c) { return flex_ui_pad(c->bw, c->bh); }          // uiPad()
static int gap_of(const flex_dc_ctx_t *c) { return flex_ui_gap(pad_of(c)); }             // uiGap()
static int fit(const flex_dc_ctx_t *c, const char *s, int w, int mx) { return flex_ui_font_fit(s, w, mx, c->tw, c->tctx); }   // uiFontFit
static int line_h(int fs) { return flex_ui_line_h(fs); }
static int text_w(const flex_dc_ctx_t *c, const char *s, int fs) { return c->tw(s, fs, c->tctx); }

// ---- iconos ----
static void glyph_phone(flex_dc_page_t *pg, int cx, int cy, int h, int col, float th)
{
    int w = h * 52 / 100;
    outline(pg, cx - w / 2, cy - h / 2, w, h, w / 5, col);
    outline(pg, cx - w / 2 + 1, cy - h / 2 + 1, w - 2, h - 2, w / 5, col);
    stroke(pg, (float)(cx - w / 8), (float)(cy - h / 2 + 3), (float)(cx + w / 8), (float)(cy - h / 2 + 3), th, col);
}

void flex_dc_icon(flex_dc_page_t *pg, int which, int x, int y, int S, int col, int acc)
{
    int cx = x + S / 2, cy = y + S / 2;
    float th = S / 16.0f;
    if (th < 1.4f) {
        th = 1.4f;
    }
    switch (which) {
    case DCI_STATUS: {
        glyph_phone(pg, cx, cy, (int)(S * 0.74f), col, th);
        int r = S / 4;
        disc(pg, cx + S / 5, cy + S / 5, r, acc);
        stroke(pg, cx + S / 5 - r * 0.42f, (float)(cy + S / 5), cx + S / 5 - r * 0.08f, cy + S / 5 + r * 0.36f, th, DCC_ONACC);
        stroke(pg, cx + S / 5 - r * 0.08f, cy + S / 5 + r * 0.36f, cx + S / 5 + r * 0.45f, cy + S / 5 - r * 0.34f, th, DCC_ONACC);
    } break;
    case DCI_DIAG: {
        int w = (int)(S * 0.78f), h = (int)(S * 0.58f);
        outline(pg, cx - w / 2, cy - h / 2 - S / 12, w, h, 5, col);
        float bx = (float)(cx - w / 2 + 4), byy = (float)(cy - S / 12);
        stroke(pg, bx, byy, bx + w * 0.22f, byy, th, acc);
        stroke(pg, bx + w * 0.22f, byy, bx + w * 0.34f, byy - h * 0.28f, th, acc);
        stroke(pg, bx + w * 0.34f, byy - h * 0.28f, bx + w * 0.48f, byy + h * 0.26f, th, acc);
        stroke(pg, bx + w * 0.48f, byy + h * 0.26f, bx + w * 0.60f, byy, th, acc);
        stroke(pg, bx + w * 0.60f, byy, bx + w - 8, byy, th, acc);
        rect(pg, cx - w / 8, cy + h / 2 - S / 12, w / 4, S / 8, col);
        rect(pg, cx - w / 4, cy + h / 2 + S / 24, w / 2, 2, col);
    } break;
    case DCI_FALL: {
        int w = (int)(S * 0.34f), h = (int)(S * 0.56f);
        float a = -0.44f, ca = cosf(a), sa = sinf(a);
        float px1[4] = {-w / 2.0f, w / 2.0f, w / 2.0f, -w / 2.0f};
        float py1[4] = {-h / 2.0f, -h / 2.0f, h / 2.0f, h / 2.0f};
        float rx[4], ry[4];
        for (int i = 0; i < 4; i++) {
            rx[i] = cx + px1[i] * ca - py1[i] * sa;
            ry[i] = cy - S / 10 + px1[i] * sa + py1[i] * ca;
        }
        for (int i = 0; i < 4; i++) {
            stroke(pg, rx[i], ry[i], rx[(i + 1) % 4], ry[(i + 1) % 4], th, col);
        }
        stroke(pg, cx + S * 0.30f, cy - S * 0.22f, cx + S * 0.30f, cy + S * 0.24f, th, acc);
        tri(pg, (int)(cx + S * 0.30f), (int)(cy + S * 0.38f), (int)(cx + S * 0.22f), (int)(cy + S * 0.20f), (int)(cx + S * 0.38f), (int)(cy + S * 0.20f), acc);
        stroke(pg, cx - S * 0.36f, cy + S * 0.40f, cx + S * 0.06f, cy + S * 0.40f, th, col);
    } break;
    case DCI_HEALTH: {
        int r = S / 5;
        disc(pg, cx - r, cy - r / 2, r, acc);
        disc(pg, cx + r, cy - r / 2, r, acc);
        tri(pg, cx - 2 * r, cy - r / 2, cx + 2 * r, cy - r / 2, cx, cy + 2 * r - 2, acc);
        stroke(pg, cx - r * 1.6f, (float)cy, cx - r * 0.6f, (float)cy, th, DCC_ONACC);
        stroke(pg, cx - r * 0.6f, (float)cy, cx - r * 0.2f, cy - r * 0.7f, th, DCC_ONACC);
        stroke(pg, cx - r * 0.2f, cy - r * 0.7f, cx + r * 0.2f, cy + r * 0.6f, th, DCC_ONACC);
        stroke(pg, cx + r * 0.2f, cy + r * 0.6f, cx + r * 0.6f, (float)cy, th, DCC_ONACC);
        stroke(pg, cx + r * 0.6f, (float)cy, cx + r * 1.6f, (float)cy, th, DCC_ONACC);
    } break;
    case DCI_OPT: {
        int r = (int)(S * 0.34f);
        arc(pg, (float)cx, (float)(cy + r / 3), (float)r, 180, 360, (int)(th * 2), col);
        for (int k = 0; k <= 4; k++) {
            float ang = (180 + k * 45) * 0.0174532925f;
            stroke(pg, cx + cosf(ang) * (r - th * 2), cy + r / 3 + sinf(ang) * (r - th * 2), cx + cosf(ang) * (r - th * 4), cy + r / 3 + sinf(ang) * (r - th * 4), th * 0.7f, col);
        }
        float na = (180 + 62) * 0.0174532925f;
        stroke(pg, (float)cx, (float)(cy + r / 3), cx + cosf(na) * r * 0.82f, cy + r / 3 + sinf(na) * r * 0.82f, th, acc);
        disc(pg, cx, cy + r / 3, (int)(th * 1.4f), acc);
    } break;
    case DCI_HIST: {
        int r = (int)(S * 0.32f);
        arc(pg, (float)cx, (float)cy, (float)r, 300, 640, (int)(th * 1.8f), col);
        tri(pg, cx - r, cy - r / 2, cx - r, cy + r / 2, cx - r + (int)(th * 3), cy, col);
        stroke(pg, (float)cx, (float)cy, (float)cx, cy - r * 0.52f, th, acc);
        stroke(pg, (float)cx, (float)cy, cx + r * 0.40f, (float)cy, th, acc);
        disc(pg, cx, cy, (int)(th * 1.1f), acc);
    } break;
    default:
        break;
    }
}

// ---- glifos de los botones ----
void flex_dc_btn_check(flex_dc_page_t *pg, int cx, int cy, int r, int col)
{
    stroke(pg, (float)(cx - r), (float)cy, cx - r * 0.25f, cy + r * 0.7f, 2.4f, col);
    stroke(pg, cx - r * 0.25f, cy + r * 0.7f, (float)(cx + r), cy - r * 0.75f, 2.4f, col);
}

void flex_dc_button(flex_dc_page_t *pg, const flex_dc_ctx_t *c, int x, int y, int w, int h, const char *label, int glyph, int bg, int fg, int hit_id)
{
    rrect(pg, x, y, w, h, h / 2, bg);
    int gx = x + h / 2 + 2, gy = y + h / 2, gr = h / 5;
    (void)gx;
    int tw = text_w(c, label, 2);
    int total = (glyph == DCB_NONE) ? tw : (gr * 2 + 10 + tw);
    int sx = x + (w - total) / 2;
    if (glyph != DCB_NONE) {
        gx = sx + gr;
        switch (glyph) {
        case DCB_PLAY:
            tri(pg, gx - gr / 2, gy - gr, gx - gr / 2, gy + gr, gx + gr, gy, fg);
            break;
        case DCB_RETRY:
            arc(pg, (float)gx, (float)gy, (float)gr, 40, 330, 3, fg);
            tri(pg, gx + gr - 4, gy - gr + 1, gx + gr + 4, gy - gr + 3, gx + gr - 1, gy - gr + 8, fg);
            break;
        case DCB_CHECK:
            flex_dc_btn_check(pg, gx, gy, gr, fg);
            break;
        case DCB_POWER:
            arc(pg, (float)gx, (float)gy, (float)gr, 300, 600, 3, fg);
            stroke(pg, (float)gx, (float)(gy - gr), (float)gx, gy - gr * 0.15f, 2.6f, fg);
            break;
        default:
            break;
        }
        sx += gr * 2 + 10;
    }
    text(pg, DCO_TEXT, sx, y + (h - line_h(2)) / 2, 2, fg, label, 0);
    if (hit_id != 0xFF) {
        hit_add(pg, x, y, w, h, hit_id);
    }
}

// ---- piezas ----
static int level_role(int lv) { return flex_dc_level_color(lv) == FDC_COL_OK ? DCC_OK : flex_dc_level_color(lv) == FDC_COL_WARN ? DCC_WARN : flex_dc_level_color(lv) == FDC_COL_ERR ? DCC_ERR : DCC_MUTE; }
static int score_role(int sc) { return flex_dc_score_color(sc) == FDC_COL_OK ? DCC_OK : flex_dc_score_color(sc) == FDC_COL_WARN ? DCC_WARN : DCC_ERR; }


void flex_dc_score_ring_c(flex_dc_page_t *pg, const flex_dc_ctx_t *c, int cx, int cy, int r, int score, float p, int col)
{
    if (p < 0) {
        p = 0;
    }
    if (p > 1) {
        p = 1;
    }
    int th = r / 6;
    if (th < 4) {
        th = 4;
    }
    ring(pg, cx, cy, r, th, DCC_TRACK);
    float a1 = -90.0f + 360.0f * (score / 100.0f) * p;
    arc(pg, (float)cx, (float)cy, (float)(r - th / 2), -90.0f, a1, th, col);
    char b[8];
    snprintf(b, sizeof(b), "%d", (int)(score * p + 0.5f));
    int fs = fit(c, b, r * 3 / 2, 6);
    text(pg, DCO_TEXT_C, cx, cy - line_h(fs) / 2 - 2, fs, DCC_TXT, b, 0);
    text(pg, DCO_TEXT_C, cx, cy + r / 3 + 2, 1, DCC_MUTE, "/100", 0);
}

static int row_led(flex_dc_page_t *pg, int x, int y, int w, const char *label, const char *value, int lv)
{
    disc(pg, x + 9, y + 9, 6, level_role(lv));
    text(pg, DCO_TEXT_CLIP, x + 24, y, 2, DCC_TXT, label, x + w / 2 + 24);
    text(pg, DCO_TEXT_R, x + w, y + 1, 1, DCC_TXT2, value, 0);
    return y + 26;
}

float flex_dc_ring_p(uint32_t e)
{
    float p = e >= FLEX_DC_RING_MS ? 1.0f : (float)e / (float)FLEX_DC_RING_MS;
    return 1.0f - (1.0f - p) * (1.0f - p);
}

// ---- inicio ----
typedef struct { int x, y, cw, ch, gap; } grid_t;
static grid_t home_layout(const flex_dc_ctx_t *c)
{
    grid_t g;
    int pad = pad_of(c);
    g.gap = gap_of(c);
    g.x = c->bx + pad;
    g.cw = (c->bw - 2 * pad - g.gap) / 2;
    int avail = c->bh - (c->bh * 30 / 100) - 2 * pad;
    g.ch = (avail - 2 * g.gap) / 3;
    if (g.ch > 132) {
        g.ch = 132;
    }
    if (g.ch < 56) {
        g.ch = 56;
    }
    g.y = c->by + c->bh - pad - 3 * g.ch - 2 * g.gap;
    return g;
}

void flex_dc_home_card_geom(const flex_dc_ctx_t *c, int *cx, int *cy, int *cw, int *cardH)
{
    int pad = pad_of(c);
    grid_t g = home_layout(c);
    *cardH = g.y - c->by - pad - gap_of(c);
    if (*cardH < 96) {
        *cardH = 96;
    }
    *cx = c->bx + pad;
    *cy = c->by + pad;
    *cw = c->bw - 2 * pad;
}

static void civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468L;
    long era = (z >= 0 ? z : z - 146096L) / 146097L;
    unsigned doe = (unsigned)(z - era * 146097L);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}
static long local_days(uint32_t utc)
{
    long local = (long)utc + FLEX_TZ_OFFSET_SEC;
    long days = local / 86400L;
    if (local % 86400L < 0) {
        days--;
    }
    return days;
}

void flex_dc_home_card(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_home_t *h, int cx, int cy, int cw, int cardH, float ringP)
{
    surf(pg, cx, cy, cw, cardH, 24);
    outline(pg, cx, cy, cw, cardH, 24, DCC_BORDER);
    int rr = cardH / 2 - 16;
    if (rr > 54) {
        rr = 54;
    }
    if (rr < 26) {
        rr = 26;
    }
    int rcx = cx + 24 + rr, rcy = cy + cardH / 2;
    if (h->score_ok) {
        flex_dc_score_ring_c(pg, c, rcx, rcy, rr, h->score, ringP, score_role(h->score));
    } else {
        ring(pg, rcx, rcy, rr, rr / 6, DCC_TRACK);
        text(pg, DCO_TEXT_C, rcx, rcy - 8, 4, DCC_MUTE, "--", 0);
    }
    int tx = rcx + rr + 18;
    int tw = cx + cw - 16 - tx;
    const char *word = h->score_ok ? flex_dc_score_word(h->score, c->li) : T_(DCS_NOTAVAIL);
    text(pg, DCO_TEXT_CLIP, tx, rcy - 30, 3, DCC_TXT, word, tx + tw);
    char sub[64];
    if (h->last_check) {
        int y2, mo, d;
        civil_from_days(local_days(h->last_check), &y2, &mo, &d);
        snprintf(sub, sizeof(sub), "%s: %d %s", T_(DCS_LASTCHECK), d, c->mo_short(c->li, (mo - 1) % 12));
    } else {
        snprintf(sub, sizeof(sub), "%s: %s", T_(DCS_LASTCHECK), T_(DCS_NEVER));
    }
    text(pg, DCO_TEXT_CLIP, tx, rcy - 2, 1, DCC_TXT2, sub, tx + tw);
    const char *fs2 = h->fall_on ? (h->bno_avail ? T_(DCS_MONITORING) : T_(DCS_UNAVAILABLE)) : T_(DCS_MONITOR_OFF);
    int fc = h->fall_on ? (h->bno_avail ? DCC_OK : DCC_WARN) : DCC_MUTE;
    disc(pg, tx + 6, rcy + 26, 5, fc);
    text(pg, DCO_TEXT_CLIP, tx + 18, rcy + 19, 1, DCC_TXT2, fs2, tx + tw);
}

static void draw_tile(flex_dc_page_t *pg, const flex_dc_ctx_t *c, int idx, int x, int y, int w, int h, const char *label, int icon)
{
    surf(pg, x, y, w, h, 20);
    outline(pg, x, y, w, h, 20, DCC_BORDER);
    int S = h * 42 / 100;
    if (S > 56) {
        S = 56;
    }
    flex_dc_icon(pg, icon, x + (w - S) / 2, y + h / 8, S, DCC_TXT2, DCC_ACCENT);
    int fs = fit(c, label, w - 12, 2);
    text(pg, DCO_TEXT_C, x + w / 2, y + h - line_h(fs) - 10, fs, DCC_TXT, label, 0);
    hit_add(pg, x, y, w, h, DCH_T0 + idx);
}

void flex_dc_home(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_home_t *h)
{
    flex_dc_home_p(pg, c, h, 1.0f);
}

void flex_dc_home_p(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_home_t *h, float ringP)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int cx, cy, cw, cardH;
    flex_dc_home_card_geom(c, &cx, &cy, &cw, &cardH);
    flex_dc_home_card(pg, c, h, cx, cy, cw, cardH, ringP);
    hit_add(pg, cx, cy, cw, cardH, DCH_HERO);
    grid_t g = home_layout(c);
    static const int ICN[6] = {DCI_STATUS, DCI_DIAG, DCI_FALL, DCI_HEALTH, DCI_OPT, DCI_HIST};
    static const int LBL[6] = {DCS_STATUS, DCS_DIAG, DCS_FALL, DCS_HEALTH, DCS_OPT, DCS_HIST};
    for (int i = 0; i < 6; i++) {
        int col = i % 2, row = i / 2;
        int x = g.x + col * (g.cw + g.gap);
        int y = g.y + row * (g.ch + g.gap);
        draw_tile(pg, c, i, x, y, g.cw, g.ch, T_(LBL[i]), ICN[i]);
    }
}

// ---- estado ----
static const int METRIC_LBL[DCH_N] = {DCS_MEMORY, DCS_PSRAM, DCS_STABILITY, DCS_WATCHDOG, DCS_SERVICES, DCS_ERRORS, DCS_REBOOTS, DCS_STORAGE};

void flex_dc_status(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_metric_t m[DCH_N], unsigned loop_rate)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    int x = c->bx + pad, w = c->bw - 2 * pad;
    int y = c->by + pad;
    uint8_t sc = 0;
    bool ok = flex_dc_score_from(m, &sc);
    surf(pg, x, y, w, 96, 20);
    outline(pg, x, y, w, 96, 20, DCC_BORDER);
    flex_dc_icon(pg, DCI_STATUS, x + 16, y + 16, 64, DCC_TXT2, DCC_ACCENT);
    text(pg, DCO_TEXT_CLIP, x + 94, y + 20, 3, DCC_TXT, ok ? flex_dc_score_word(sc, c->li) : T_(DCS_NOTAVAIL), x + w - 12);
    char b[64];
    text(pg, DCO_TEXT_CLIP, x + 94, y + 50, 1, DCC_TXT2, "Flex OS Ultra \xC2\xB7 ESP32-P4 \xC2\xB7 480x800", x + w - 12);
    if (loop_rate) {
        snprintf(b, sizeof(b), "%s: %lu/s", T_(DCS_LOOPRATE), (unsigned long)loop_rate);
    } else {
        snprintf(b, sizeof(b), "%s: %s", T_(DCS_LOOPRATE), T_(DCS_NOTAVAIL));
    }
    text(pg, DCO_TEXT_CLIP, x + 94, y + 68, 1, DCC_MUTE, b, x + w - 12);
    y += 96 + gap_of(c);
    for (int i = 0; i < DCH_N; i++) {
        if (y + 26 > c->by + c->bh - pad - 62) {
            break;
        }
        y = row_led(pg, x, y, w, T_(METRIC_LBL[i]), m[i].detail, m[i].level);
    }
    int bhh = 46, byy = c->by + c->bh - pad - bhh;
    flex_dc_button(pg, c, x, byy, w, bhh, T_(DCS_DIAG), DCB_PLAY, DCC_PRIM, DCC_ONACC, DCH_RUNDIAG);
}

// ---- salud ----
void flex_dc_health(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_metric_t m[DCH_N], float ringP)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    int x = c->bx + pad, w = c->bw - 2 * pad;
    int y = c->by + pad;
    uint8_t sc = 0;
    bool ok = flex_dc_score_from(m, &sc);
    int rr = 46, rcx = c->bx + c->bw / 2, rcy = y + rr + 4;
    if (ok) {
        flex_dc_score_ring_c(pg, c, rcx, rcy, rr, sc, ringP, score_role(sc));
    } else {
        ring(pg, rcx, rcy, rr, rr / 6, DCC_TRACK);
        text(pg, DCO_TEXT_C, rcx, rcy - 8, 4, DCC_MUTE, "--", 0);
    }
    y = rcy + rr + 8;
    text(pg, DCO_TEXT_C, c->bx + c->bw / 2, y, 2, DCC_TXT2, ok ? flex_dc_score_word(sc, c->li) : T_(DCS_NOTAVAIL), 0);
    y += line_h(2) + gap_of(c);
    for (int i = 0; i < DCH_N; i++) {
        if (y + 40 > c->by + c->bh - pad - 56) {
            break;
        }
        disc(pg, x + 9, y + 9, 6, level_role(m[i].level));
        text(pg, DCO_TEXT_CLIP, x + 24, y, 2, DCC_TXT, T_(METRIC_LBL[i]), x + w / 2 + 20);
        text(pg, DCO_TEXT_R, x + w, y + 2, 1, DCC_TXT2, m[i].detail, 0);
        if (m[i].pct >= 0) {
            int by2 = y + 22, bhh = 5;
            rrect(pg, x + 24, by2, w - 24, bhh, bhh / 2, DCC_TRACK);
            rrect(pg, x + 24, by2, (w - 24) * m[i].pct / 100, bhh, bhh / 2, level_role(m[i].level));
            y += 34;
        } else {
            y += 26;
        }
    }
    int bhh = 46, byy = c->by + c->bh - pad - bhh;
    flex_dc_button(pg, c, x, byy, w, bhh, T_(DCS_DIAG), DCB_PLAY, DCC_PRIM, DCC_ONACC, DCH_HEALTH_DIAG);
}

// ---- optimizacion ----
void flex_dc_optim(flex_dc_page_t *pg, const flex_dc_ctx_t *c, uint32_t ps_total, uint32_t ps_free)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    int x = c->bx + pad, w = c->bw - 2 * pad;
    int y = c->by + pad;
    flex_dc_icon(pg, DCI_OPT, c->bx + c->bw / 2 - 44, y, 88, DCC_TXT2, DCC_ACCENT);
    y += 96;
    text(pg, DCO_TEXT_C, c->bx + c->bw / 2, y, 3, DCC_TXT, T_(DCS_SAFEACTIONS), 0);
    y += line_h(3) + 8;
    static const int ACT[4] = {DCS_OPT1, DCS_OPT2, DCS_OPT3, DCS_OPT4};
    for (int i = 0; i < 4; i++) {
        disc(pg, x + 8, y + 8, 5, DCC_ACCENT);
        text(pg, DCO_TEXT_CLIP, x + 24, y, 1, DCC_TXT2, T_(ACT[i]), x + w);
        y += 24;
    }
    y += 8;
    text(pg, DCO_TEXT_CLIP, x, y, 1, DCC_MUTE, T_(DCS_OPTNOTE), x + w);
    y += 20;
    text(pg, DCO_TEXT_CLIP, x, y, 1, DCC_MUTE, T_(DCS_OPTNOFW), x + w);
    y += 30;
    char b[48], f1[24];
    if (ps_total) {
        flexMemFmt(ps_free, f1, sizeof(f1));
        snprintf(b, sizeof(b), "PSRAM: %s", f1);
    } else {
        snprintf(b, sizeof(b), "PSRAM: %s", T_(DCS_NOTAVAIL));
    }
    text(pg, DCO_TEXT_CLIP, x, y, 2, DCC_TXT, b, x + w);
    int bhh = 46, byy = c->by + c->bh - pad - bhh;
    flex_dc_button(pg, c, x, byy, w, bhh, T_(DCS_OPTRUN), DCB_PLAY, DCC_PRIM, DCC_ONACC, DCH_OPT_RUN);
}

// ---- motor de diagnostico ----
static void diag_ring_content(flex_dc_page_t *pg, int rcx, int rcy, int rr, int stage, uint32_t anim_ms)
{
    int th = rr / 6;
    ring(pg, rcx, rcy, rr, th, DCC_TRACK);
    float p = (float)stage / (float)(DG_N - 1);
    if (p > 1) {
        p = 1;
    }
    arc(pg, (float)rcx, (float)rcy, (float)(rr - th / 2), -90.0f, -90.0f + 360.0f * p, th, DCC_PRIM);
    if (stage < DG_DONE) {
        uint32_t e = anim_ms % 1200u;
        float ph = (e < 600u) ? (float)e / 600.0f : 1.0f - (float)(e - 600u) / 600.0f;
        disc(pg, rcx, rcy, (int)(rr * 0.34f + rr * 0.10f * ph), DCC_SURF2);
        flex_dc_icon(pg, DCI_DIAG, rcx - 22, rcy - 22, 44, DCC_TXT2, DCC_ACCENT);
    } else {
        disc(pg, rcx, rcy, (int)(rr * 0.42f), DCC_OK);
        flex_dc_btn_check(pg, rcx, rcy, rr / 4, DCC_ONACC);
    }
}

void flex_dc_diag(flex_dc_page_t *pg, const flex_dc_ctx_t *c, int stage, bool post, uint32_t anim_ms)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    int x = c->bx + pad, w = c->bw - 2 * pad;
    int rr = 54;
    int rcx = c->bx + c->bw / 2, rcy = c->by + pad + rr + 8;
    diag_ring_content(pg, rcx, rcy, rr, stage, anim_ms);
    int y = rcy + rr + gap_of(c);
    text(pg, DCO_TEXT_C, c->bx + c->bw / 2, y, 3, DCC_TXT, post ? T_(DCS_EVPOST) : T_(DCS_DIAG), 0);
    y += line_h(3) + gap_of(c);
    const char *nm[DG_N];
    nm[DG_SYS] = T_(DCS_SYSTEM);
    nm[DG_IMU] = T_(DCS_IMUNAME);
    nm[DG_SCREEN] = T_(DCS_SCREEN);
    nm[DG_TOUCH] = T_(DCS_TOUCH);
    nm[DG_DONE] = T_(DCS_DONE);
    for (int i = 0; i < DG_N; i++) {
        bool done = (stage > i);
        bool cur = (stage == i);
        if (done) {
            disc(pg, x + 12, y + 9, 10, DCC_OK);
            flex_dc_btn_check(pg, x + 12, y + 9, 5, DCC_ONACC);
        } else {
            circ(pg, x + 12, y + 9, 10, cur ? DCC_PRIM : DCC_DIV);
            if (cur) {
                disc(pg, x + 12, y + 9, 5, DCC_PRIM);
            }
        }
        text(pg, DCO_TEXT_CLIP, x + 32, y, 2, cur ? DCC_TXT : (done ? DCC_TXT2 : DCC_MUTE), nm[i], x + w);
        y += 30;
    }
    y += 4;
    text(pg, DCO_TEXT_CLIP, x, y, 1, DCC_MUTE, T_(DCS_OPTNOTE), x + w);
}

// ---- resultado ----
void flex_dc_result(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_result_t *r, uint32_t anim_ms)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    int x = c->bx + pad, w = c->bw - 2 * pad;
    int y = c->by + pad;
    const flex_dc_res_t *res = r->res;
    int rr = 62, rcx = c->bx + c->bw / 2, rcy = y + rr + 6;
    if (res->final_ok) {
        flex_dc_score_ring_c(pg, c, rcx, rcy, rr, res->final, flex_dc_ring_p(anim_ms), score_role(res->final));
    } else {
        ring(pg, rcx, rcy, rr, rr / 6, DCC_TRACK);
        text(pg, DCO_TEXT_C, rcx, rcy - 10, 4, DCC_MUTE, "--", 0);
    }
    y = rcy + rr + gap_of(c);
    text(pg, DCO_TEXT_C, c->bx + c->bw / 2, y, 3, DCC_TXT, res->final_ok ? flex_dc_score_word(res->final, c->li) : T_(DCS_NOTAVAIL), 0);
    y += line_h(3) + gap_of(c);

    char v[32];
    if (res->sys_ok) {
        snprintf(v, sizeof(v), "%u/100", (unsigned)res->sys);
    } else {
        snprintf(v, sizeof(v), "%s", T_(DCS_NOTAVAIL));
    }
    y = row_led(pg, x, y, w, T_(DCS_SYSTEM), v, res->sys_ok ? (res->sys >= 80 ? DCL_OK : (res->sys >= 55 ? DCL_WARN : DCL_BAD)) : DCL_NA);

    if (res->imu_avail) {
        int bits = 0;
        for (int i = 0; i < 5; i++) {
            if (res->imu_mask & (1 << i)) {
                bits++;
            }
        }
        snprintf(v, sizeof(v), "%d/5", bits);
        y = row_led(pg, x, y, w, T_(DCS_IMUNAME), v, bits == 5 ? DCL_OK : (bits >= 3 ? DCL_WARN : DCL_BAD));
    } else {
        y = row_led(pg, x, y, w, T_(DCS_IMUNAME), T_(DCS_NOTAVAIL), DCL_NA);
    }

    if (res->screen >= 0) {
        y = row_led(pg, x, y, w, T_(DCS_SCREEN), res->screen ? T_(DCS_NORMAL) : T_(DCS_ATTENTION), res->screen ? DCL_OK : DCL_BAD);
    } else {
        y = row_led(pg, x, y, w, T_(DCS_SCREEN), T_(DCS_NOTAVAIL), DCL_NA);
    }

    if (res->touch >= 0) {
        snprintf(v, sizeof(v), "%d%%", res->touch);
        y = row_led(pg, x, y, w, T_(DCS_TOUCH), v, res->touch >= 95 ? DCL_OK : (res->touch >= 60 ? DCL_WARN : DCL_BAD));
    } else {
        y = row_led(pg, x, y, w, T_(DCS_TOUCH), T_(DCS_NOTAVAIL), DCL_NA);
    }

    {
        y += 8;
        int colw = w / 2;
        int y0 = y;
        for (int i = 0; i < FLEX_DC_SYS_ROWS; i++) {
            const char *lab = r->rows[i].label >= 0 ? T_(r->rows[i].label) : r->rows[i].lit;
            if (!lab || !lab[0]) {
                continue;
            }
            int col = i % 2, row = i / 2;
            int rx = x + col * colw, ry = y0 + row * 22;
            if (ry + 20 > c->by + c->bh - pad - 108) {
                break;
            }
            disc(pg, rx + 5, ry + 7, 4, level_role(r->rows[i].level));
            text(pg, DCO_TEXT_CLIP, rx + 14, ry, 1, DCC_TXT2, lab, rx + colw - 62);
            text(pg, DCO_TEXT_R, rx + colw - 8, ry, 1, DCC_TXT, r->rows[i].value, 0);
        }
        y = y0 + ((FLEX_DC_SYS_ROWS + 1) / 2) * 22 + 6;
    }

    if (r->post && r->last_evt_ok) {
        y += 6;
        snprintf(v, sizeof(v), "%s %u \xC2\xB7 %.1f g", T_(DCS_CONFIDENCE), r->evt_conf, r->evt_peak_g);
        text(pg, DCO_TEXT_CLIP, x, y, 1, DCC_TXT2, v, x + w);
    }

    int bhh = 46, byy = c->by + c->bh - pad - bhh;
    flex_dc_button(pg, c, x, byy, w, bhh, T_(DCS_DONE), DCB_CHECK, DCC_PRIM, DCC_ONACC, DCH_RES_OK);
    flex_dc_button(pg, c, x, byy - bhh - 10, w, bhh, T_(DCS_REPEAT), DCB_RETRY, DCC_SURF, DCC_TXT, DCH_RES_AGAIN);
}

// ---- test tactil ----
void flex_dc_touch_geom(const flex_dc_ctx_t *c, flex_dc_ttgeom_t *g)
{
    int pad = pad_of(c);
    g->foot = 96;
    g->gx = c->bx + pad;
    g->gy = c->by + pad + 26;
    g->cw = (c->bw - 2 * pad) / FLEX_DC_TT_COLS;
    g->ch = (c->bh - 2 * pad - 26 - g->foot) / FLEX_DC_TT_ROWS;
    if (g->ch < 12) {
        g->ch = 12;
    }
}

void flex_dc_touch_test(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const uint8_t cell[FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS], int hit, int last_x, int last_y)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    flex_dc_ttgeom_t g;
    flex_dc_touch_geom(c, &g);
    text(pg, DCO_TEXT_C, c->bx + c->bw / 2, c->by + pad, 1, DCC_TXT2, T_(DCS_TTHINT), 0);
    for (int r = 0; r < FLEX_DC_TT_ROWS; r++) {
        for (int cc = 0; cc < FLEX_DC_TT_COLS; cc++) {
            int x = g.gx + cc * g.cw, y = g.gy + r * g.ch;
            bool on = cell[r * FLEX_DC_TT_COLS + cc] != 0;
            if (on) {
                rrect(pg, x + 2, y + 2, g.cw - 4, g.ch - 4, 6, DCC_OK);
                flex_dc_btn_check(pg, x + g.cw / 2, y + g.ch / 2, g.ch / 5, DCC_ONACC);
            } else {
                rrect(pg, x + 2, y + 2, g.cw - 4, g.ch - 4, 6, DCC_SURF);
                outline(pg, x + 2, y + 2, g.cw - 4, g.ch - 4, 6, DCC_DIV);
            }
        }
    }
    int cov = hit * 100 / (FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS);
    char b[64];
    int fy = c->by + c->bh - g.foot;
    snprintf(b, sizeof(b), "%d%%  \xC2\xB7  %d/%d", cov, hit, FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS);
    text(pg, DCO_TEXT, c->bx + pad, fy, 2, cov >= 95 ? DCC_OK : (cov >= 60 ? DCC_WARN : DCC_TXT2), b, 0);
    if (last_x >= 0) {
        snprintf(b, sizeof(b), "X %d  Y %d", last_x, last_y);
    } else {
        snprintf(b, sizeof(b), "%s", T_(DCS_NOTAVAIL));
    }
    text(pg, DCO_TEXT_R, c->bx + c->bw - pad, fy + 2, 1, DCC_MUTE, b, 0);
    flex_dc_button(pg, c, c->bx + pad, c->by + c->bh - pad - 46, c->bw - 2 * pad, 46, T_(DCS_DONE), DCB_CHECK, DCC_PRIM, DCC_ONACC, DCH_TOUCH_END);
}

// ---- pregunta final del test de pantalla ----
void flex_dc_scr_ask(flex_dc_page_t *pg, const flex_dc_ctx_t *c)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, 0, 0, 480, c->by + c->bh > 800 ? c->by + c->bh : 800, DCC_WINBG);   // fillRect(0, 0, SCR_W, SCR_H, WIN_BG)
    int pad = pad_of(c);
    flex_dc_icon(pg, DCI_DIAG, c->bx + c->bw / 2 - 40, c->by + c->bh / 4, 80, DCC_TXT2, DCC_ACCENT);
    int fs = fit(c, T_(DCS_SCRASK1), c->bw - 2 * pad, 2);
    text(pg, DCO_TEXT_C, c->bx + c->bw / 2, c->by + c->bh / 4 + 100, fs, DCC_TXT, T_(DCS_SCRASK1), 0);
    text(pg, DCO_TEXT_C, c->bx + c->bw / 2, c->by + c->bh / 4 + 126, fs, DCC_TXT, T_(DCS_SCRASK2), 0);
    int bhh = 48, bwid = c->bw - 2 * pad;
    int byy = c->by + c->bh - pad - bhh;
    flex_dc_button(pg, c, c->bx + pad, byy, bwid, bhh, T_(DCS_NO), DCB_RETRY, DCC_SURF, DCC_DANGER, DCH_SCR_NO);
    flex_dc_button(pg, c, c->bx + pad, byy - bhh - 10, bwid, bhh, T_(DCS_YES), DCB_CHECK, DCC_PRIM, DCC_ONACC, DCH_SCR_YES);
}

// ---- deteccion de caidas sin controlador (A17h) ----
const char *flex_dc_fall_note(int li)
{
    static const char *const N[FLEX_DC_LANGS] = {
        "Esta versi\xC3\xB3n a\xC3\xBAn no incluye el controlador del sensor.",
        "This version doesn't include the sensor driver yet.",
        "Cette version n'inclut pas encore le pilote du capteur.",
        "Esta vers\xC3\xA3o ainda n\xC3\xA3o inclui o driver do sensor.",
        "Questa versione non include ancora il driver del sensore.",
    };
    return N[li >= 0 && li < FLEX_DC_LANGS ? li : 1];
}

void flex_dc_fall_unavailable(flex_dc_page_t *pg, const flex_dc_ctx_t *c)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    int w = c->bw - 2 * pad;
    int x = c->bx + pad;
    int cx = c->bx + c->bw / 2;
    int y = c->by + pad;
    text(pg, DCO_TEXT_C, cx, y, 3, DCC_TXT, T_(DCS_FALL), 0);
    y += line_h(3) + 6;
    int fs = fit(c, T_(DCS_UNAVAILABLE), w, 2);
    text(pg, DCO_TEXT_C, cx, y, fs, DCC_TXT2, T_(DCS_UNAVAILABLE), 0);
    y += line_h(fs) + gap_of(c);
    int S = 88;
    flex_dc_icon(pg, DCI_FALL, cx - S / 2, y, S, DCC_TXT2, DCC_ACCENT);
    y += S + gap_of(c);
    // la tarjeta del modulo que espera la funcion (los mismos textos de Arduino)
    int ch = 112;
    surf(pg, x, y, w, ch, 20);
    outline(pg, x, y, w, ch, 20, DCC_BORDER);
    text(pg, DCO_TEXT_C, cx, y + 14, 1, DCC_MUTE, T_(DCS_NEEDHW), 0);
    text(pg, DCO_TEXT_C, cx, y + 36, 3, DCC_TXT, "TENSTAR GY-BNO085", 0);
    text(pg, DCO_TEXT_C, cx, y + 36 + line_h(3) + 4, 2, DCC_ACCS, T_(DCS_NEEDHW_3), 0);
    int fn = fit(c, flex_dc_fall_note(c->li), w - 24, 1);
    text(pg, DCO_TEXT_C, cx, y + ch - line_h(fn) - 10, fn, DCC_MUTE, flex_dc_fall_note(c->li), 0);
    int bhh = 46, bwid = 200;
    flex_dc_button(pg, c, c->bx + (c->bw - bwid) / 2, c->by + c->bh - pad - bhh, bwid, bhh, T_(DCS_UNDERSTOOD), DCB_CHECK, DCC_PRIM, DCC_ONACC, DCH_FALLOK);
}

// ---- historial ----
static void hist_icon(flex_dc_page_t *pg, int kind, int x, int y, int S)
{
    switch (kind) {
    case DC_EV_FALL:
        flex_dc_icon(pg, DCI_FALL, x, y, S, DCC_WARN, DCC_WARN);
        break;
    case DC_EV_POST:
        flex_dc_icon(pg, DCI_STATUS, x, y, S, DCC_TXT2, DCC_ACCENT);
        break;
    case DC_EV_OPT:
        flex_dc_icon(pg, DCI_OPT, x, y, S, DCC_TXT2, DCC_ACCENT);
        break;
    default:
        flex_dc_icon(pg, DCI_DIAG, x, y, S, DCC_TXT2, DCC_ACCENT);
        break;
    }
}

void flex_dc_hist(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_hist_t *h, int scroll)
{
    pg->n = 0;
    pg->nhit = 0;
    rect(pg, c->bx, c->by, c->bw, c->bh, DCC_WINBG);
    int pad = pad_of(c);
    int x = c->bx + pad, w = c->bw - 2 * pad;
    int y = c->by + pad - scroll;
    if (h->n == 0) {
        flex_dc_icon(pg, DCI_HIST, c->bx + c->bw / 2 - 32, c->by + c->bh / 3, 64, DCC_MUTE, DCC_MUTE);
        text(pg, DCO_TEXT_C, c->bx + c->bw / 2, c->by + c->bh / 3 + 80, 3, DCC_TXT2, T_(DCS_NOEVENTS), 0);
        text(pg, DCO_TEXT_C, c->bx + c->bw / 2, c->by + c->bh / 3 + 112, 1, DCC_MUTE, T_(DCS_EMPTYHIST), 0);
        return;
    }
    int lastDay = -1;
    for (int i = 0; i < h->n; i++) {
        const flex_dc_rec_t *r = &h->rec[i];
        long days = local_days(r->utc);
        int yy, mo, dd;
        civil_from_days(days, &yy, &mo, &dd);
        if ((int)days != lastDay) {
            lastDay = (int)days;
            if (y + 22 > c->by && y < c->by + c->bh) {
                char hd[32];
                snprintf(hd, sizeof(hd), "%02d %s", dd, c->mo_short(c->li, (mo - 1) % 12));
                text(pg, DCO_TEXT, x, y, 2, DCC_TXT2, hd, 0);
            }
            y += 26;
        }
        if (y + 62 > c->by && y < c->by + c->bh) {
            surf(pg, x, y, w, 58, 16);
            hist_icon(pg, r->kind, x + 10, y + 11, 36);
            const char *title = (r->kind == DC_EV_FALL) ? T_(DCS_EVFALL) : (r->kind == DC_EV_POST) ? T_(DCS_EVPOST) : (r->kind == DC_EV_OPT) ? T_(DCS_EVOPT) : T_(DCS_EVDIAG);
            text(pg, DCO_TEXT_CLIP, x + 54, y + 10, 2, DCC_TXT, title, x + w - 60);
            char sub[64];
            if (r->kind == DC_EV_FALL) {
                snprintf(sub, sizeof(sub), "%s: %u \xC2\xB7 %s %u.%u g", T_(DCS_CONFIDENCE), (unsigned)r->score, T_(DCS_IMPACT), (unsigned)(r->extra / 10), (unsigned)(r->extra % 10));
                text(pg, DCO_TEXT_R, x + w - 12, y + 12, 3, DCC_WARN, "!", 0);
            } else if (r->kind == DC_EV_OPT) {
                snprintf(sub, sizeof(sub), "%s", T_(DCS_OPTRUN));
            } else {
                snprintf(sub, sizeof(sub), "%s %u/100", T_(DCS_SCORE), (unsigned)r->score);
                char sc[8];
                snprintf(sc, sizeof(sc), "%u", (unsigned)r->score);
                text(pg, DCO_TEXT_R, x + w - 12, y + 12, 3, score_role(r->score), sc, 0);
            }
            text(pg, DCO_TEXT_CLIP, x + 54, y + 34, 1, DCC_TXT2, sub, x + w - 60);
        }
        y += 66;
    }
}
