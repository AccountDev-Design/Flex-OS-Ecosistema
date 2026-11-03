#include "flex_raster.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define AA_EPS 0.02f   // holgura de los atajos (Gfx.h)
#define DIV255(v) (uint16_t)(((v) + 1u + ((v) >> 8)) >> 8)

uint16_t flex_raster_mix565(uint16_t a, uint16_t b, uint8_t t)
{
    uint32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint32_t it = 255u - t;
    uint32_t rr = ar * it + br * t;
    uint32_t rg = ag * it + bg * t;
    uint32_t rb = ab * it + bb * t;
    return (uint16_t)((DIV255(rr) << 11) | (DIV255(rg) << 5) | DIV255(rb));
}

void flex_raster_init(flex_raster_t *r, uint16_t *buf, int w, int h, int stride, int ox, int oy)
{
    r->buf = buf;
    r->w = w;
    r->h = h;
    r->stride = stride;
    r->ox = ox;
    r->oy = oy;
    r->cx0 = ox;
    r->cx1 = ox + w - 1;
    r->cy0 = oy;
    r->cy1 = oy + h - 1;
}

void flex_raster_fill(flex_raster_t *r, uint16_t color)
{
    if (!r->buf) {
        return;
    }
    for (int y = 0; y < r->h; y++) {
        uint16_t *p = r->buf + (size_t)y * (size_t)r->stride;
        for (int x = 0; x < r->w; x++) {
            p[x] = color;
        }
    }
}

// recorte efectivo: el del llamante cruzado con el buffer
typedef struct {
    uint16_t *buf;
    int stride, ox, oy;
    int x0, x1, y0, y1;
} ctx_t;

static bool ctx_make(const flex_raster_t *r, ctx_t *c)
{
    if (!r->buf || r->w <= 0 || r->h <= 0) {
        return false;
    }
    c->buf = r->buf;
    c->stride = r->stride;
    c->ox = r->ox;
    c->oy = r->oy;
    c->x0 = r->cx0 > r->ox ? r->cx0 : r->ox;
    c->x1 = r->cx1 < r->ox + r->w - 1 ? r->cx1 : r->ox + r->w - 1;
    c->y0 = r->cy0 > r->oy ? r->cy0 : r->oy;
    c->y1 = r->cy1 < r->oy + r->h - 1 ? r->cy1 : r->oy + r->h - 1;
    return c->x0 <= c->x1 && c->y0 <= c->y1;
}

static inline uint16_t *at(const ctx_t *c, int x, int y)
{
    return c->buf + (size_t)(y - c->oy) * (size_t)c->stride + (size_t)(x - c->ox);
}

static inline void px(const ctx_t *c, int x, int y, uint16_t col)
{
    if (y < c->y0 || y > c->y1 || x < c->x0 || x > c->x1) {
        return;
    }
    *at(c, x, y) = col;
}

static inline void pxa(const ctx_t *c, int x, int y, uint16_t col, uint8_t a)
{
    if (y < c->y0 || y > c->y1 || x < c->x0 || x > c->x1) {
        return;
    }
    if (a >= 255) {
        *at(c, x, y) = col;
        return;
    }
    if (a == 0) {
        return;
    }
    uint16_t *p = at(c, x, y);
    *p = flex_raster_mix565(*p, col, a);
}

static void hline(const ctx_t *c, int x, int y, int w, uint16_t col)
{
    if (w <= 0 || y < c->y0 || y > c->y1) {
        return;
    }
    if (x < c->x0) {
        w -= c->x0 - x;
        x = c->x0;
    }
    if (x + w > c->x1 + 1) {
        w = c->x1 + 1 - x;
    }
    if (w <= 0) {
        return;
    }
    uint16_t *p = at(c, x, y);
    for (int i = 0; i < w; i++) {
        p[i] = col;
    }
}

static bool clip_box(const ctx_t *c, int *x0, int *x1, int *y0, int *y1)
{
    if (*x0 < c->x0) *x0 = c->x0;
    if (*x1 > c->x1) *x1 = c->x1;
    if (*y0 < c->y0) *y0 = c->y0;
    if (*y1 > c->y1) *y1 = c->y1;
    return *x0 <= *x1 && *y0 <= *y1;
}

void flex_raster_fill_circle_aa(const flex_raster_t *r, float cx, float cy, float rad, uint16_t col)
{
    ctx_t c;
    if (!ctx_make(r, &c)) {
        return;
    }
    int x0 = (int)floorf(cx - rad - 1), x1 = (int)ceilf(cx + rad + 1);
    int y0 = (int)floorf(cy - rad - 1), y1 = (int)ceilf(cy + rad + 1);
    if (!clip_box(&c, &x0, &x1, &y0, &y1)) {
        return;
    }
    const float rOut = rad + 0.5f;
    const float rOutS = rOut + AA_EPS, rInS = rad - 0.5f - AA_EPS;
    const float rOut2 = rOutS * rOutS, rIn2 = (rInS > 0.0f) ? rInS * rInS : -1.0f;
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y - cy, dy2 = dy * dy;
        if (dy2 >= rOut2) {
            continue;   // fila entera fuera del disco
        }
        int sx0 = 1, sx1 = 0;
        if (rIn2 > 0.0f && dy2 < rIn2) {
            float hw = sqrtf(rIn2 - dy2);
            sx0 = (int)floorf(cx - hw) + 1;
            sx1 = (int)ceilf(cx + hw) - 1;
            if (sx0 < x0) sx0 = x0;
            if (sx1 > x1) sx1 = x1;
            if (sx0 <= sx1) {
                hline(&c, sx0, y, sx1 - sx0 + 1, col);
            } else {
                sx0 = 1;
                sx1 = 0;
            }
        }
        for (int x = x0; x <= x1; x++) {
            if (x >= sx0 && x <= sx1) {
                x = sx1;
                continue;   // ya relleno de una pasada
            }
            float dx = (float)x - cx, d2 = dx * dx + dy2;
            if (d2 >= rOut2) {
                continue;
            }
            float cov = rOut - sqrtf(d2);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            pxa(&c, x, y, col, (uint8_t)(cov * 255));
        }
    }
}

void flex_raster_stroke_seg_aa(const flex_raster_t *r, float x0, float y0, float x1, float y1, float rad, uint16_t col)
{
    ctx_t c;
    if (!ctx_make(r, &c)) {
        return;
    }
    int minx = (int)floorf(fminf(x0, x1) - rad - 1), maxx = (int)ceilf(fmaxf(x0, x1) + rad + 1);
    int miny = (int)floorf(fminf(y0, y1) - rad - 1), maxy = (int)ceilf(fmaxf(y0, y1) + rad + 1);
    if (!clip_box(&c, &minx, &maxx, &miny, &maxy)) {
        return;
    }
    const float sdx = x1 - x0, sdy = y1 - y0;
    const float l2 = sdx * sdx + sdy * sdy;
    const bool deg = (l2 <= 0.0f);   // segmento degenerado: t = 0
    const float rOut = rad + 0.5f;
    const float rOutS = rOut + AA_EPS, rInS = rad - 0.5f - AA_EPS;
    const float rOut2 = rOutS * rOutS, rIn2 = (rInS > 0.0f) ? rInS * rInS : -1.0f;
    const float lineRej = rOut2 * l2;
    for (int y = miny; y <= maxy; y++) {
        float fy = (float)y;
        float ty = (fy - y0) * sdy;
        float cy_ = (fy - y0) * sdx;
        for (int x = minx; x <= maxx; x++) {
            float fx = (float)x;
            if (!deg) {
                float cross = (fx - x0) * sdy - cy_;
                if (cross * cross >= lineRej) {
                    continue;   // fuera de la banda de la recta
                }
            }
            float t = deg ? 0.0f : (((fx - x0) * sdx + ty) / l2);
            if (t < 0) t = 0;
            else if (t > 1) t = 1;
            float qx = x0 + t * sdx - fx, qy = y0 + t * sdy - fy;
            float d2 = qx * qx + qy * qy;
            if (d2 >= rOut2) {
                continue;
            }
            if (d2 <= rIn2) {
                px(&c, x, y, col);
                continue;
            }
            float cov = rOut - sqrtf(d2);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            pxa(&c, x, y, col, (uint8_t)(cov * 255));
        }
    }
}
