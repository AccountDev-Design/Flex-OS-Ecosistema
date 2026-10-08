#include "flex_glass_math.h"

#include <string.h>
#include "flex_wallpaper.h"

#define TINT_DIFF_MAX 128
#define STRIP 128

void flex_glass_level_apply(uint8_t level, flex_glass_params_t *p)
{
    int lv = level > 100 ? 100 : level;
    lv = (lv / FLEX_GLASS_LVL_STEP) * FLEX_GLASS_LVL_STEP;
    int d = lv - FLEX_GLASS_LVL_DEF;   // division entera de C: trunca hacia cero, como Arduino
    p->level = (uint8_t)lv;
    p->blur_r = (uint8_t)(6 + (d * 4) / 50);
    p->tint_min = (uint8_t)(46 + (d * 16) / 50);
    p->tint_max = (uint8_t)(70 + (d * 20) / 50);
    p->tint_base = (uint8_t)((p->tint_min + p->tint_max) / 2);
    p->spec = (uint8_t)(26 + (d * 12) / 50);
    p->shade = (uint8_t)(30 + (d * 14) / 50);
    p->corner_strong = (uint8_t)(156 + (d * 40) / 50);
    p->corner_weak = (uint8_t)(104 + (d * 30) / 50);
}

int flex_glass_luma(uint16_t c)
{
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return ((r + g) * 5 + b * 2) >> 1;
}

uint8_t flex_glass_tint_mix(const flex_glass_params_t *p, uint32_t luma_sum, int luma_n, uint16_t tint,
                            uint8_t min_mix)
{
    uint8_t m;
    if (luma_n <= 0) {
        m = p->tint_base;
    } else {
        int dif = (int)(luma_sum / (uint32_t)luma_n) - flex_glass_luma(tint);
        dif = dif < 0 ? -dif : dif;
        if (dif > TINT_DIFF_MAX) {
            dif = TINT_DIFF_MAX;
        }
        m = (uint8_t)(p->tint_min + (dif * (p->tint_max - p->tint_min)) / TINT_DIFF_MAX);
    }
    return m < min_mix ? min_mix : m;
}

void flex_glass_shade_row(const flex_glass_params_t *p, int j, int h, uint16_t *col, uint8_t *a)
{
    float fj = (float)j;
    if (fj < h * 0.45f) {
        *col = flex_rgb565(255, 255, 255);
        *a = (uint8_t)((1.0f - fj / (h * 0.45f)) * p->spec);
    } else {
        *col = flex_rgb565(0, 0, 0);
        *a = (uint8_t)(((fj - h * 0.45f) / (h * 0.55f)) * p->shade);
    }
}

int flex_glass_inset(int j, int h, int rad)
{
    if (j < rad) {
        int dy = rad - 1 - j;
        return rad - flex_isqrt32(rad * rad - dy * dy);
    }
    if (j >= h - rad) {
        int dy = j - (h - rad);
        return rad - flex_isqrt32(rad * rad - dy * dy);
    }
    return 0;
}

// ---- box-blur ----------------------------------------------------------------
// (suma * recip[n]) >> 20 == suma / n exactamente: suma <= 63 * 33 y n <= 33.
static uint32_t s_recip[2 * FLEX_GLASS_BLUR_RMAX + 2];
static uint16_t s_line[FLEX_WALL_H > FLEX_WALL_W ? FLEX_WALL_H : FLEX_WALL_W];
static uint16_t s_sr[STRIP], s_sg[STRIP], s_sb[STRIP];
static uint16_t s_ring[FLEX_GLASS_BLUR_RMAX + 1][STRIP];

#define UN(c, r, g, b) do { r = ((c) >> 11) & 0x1F; g = ((c) >> 5) & 0x3F; b = (c) & 0x1F; } while (0)
#define PK(r, g, b) (uint16_t)(((r) << 11) | ((g) << 5) | (b))

void flex_glass_blur(uint16_t *buf, int w, int h, int R)
{
    if (!buf || w <= 0 || h <= 0 || w > (int)(sizeof(s_line) / sizeof(s_line[0]))) {
        return;
    }
    R = R < 0 ? 0 : R > FLEX_GLASS_BLUR_RMAX ? FLEX_GLASS_BLUR_RMAX : R;
    for (int n = 1; n <= 2 * R + 1; n++) {
        s_recip[n] = 1048576u / (uint32_t)n + 1u;
    }
    int r, g, b;
    for (int j = 0; j < h; j++) {
        uint16_t *row = buf + (size_t)j * w;
        memcpy(s_line, row, (size_t)w * 2);
        int sr = 0, sg = 0, sb = 0, win = 0;
        for (int i = 0; i <= R && i < w; i++) {
            UN(s_line[i], r, g, b);
            sr += r;
            sg += g;
            sb += b;
            win++;
        }
        uint32_t rc = s_recip[win];
        for (int i = 0; i < w; i++) {
            row[i] = PK((int)(((uint32_t)sr * rc) >> 20), (int)(((uint32_t)sg * rc) >> 20),
                        (int)(((uint32_t)sb * rc) >> 20));
            int add = i + R + 1, rem = i - R;
            if (add < w) {
                UN(s_line[add], r, g, b);
                sr += r;
                sg += g;
                sb += b;
                win++;
            }
            if (rem >= 0) {
                UN(s_line[rem], r, g, b);
                sr -= r;
                sg -= g;
                sb -= b;
                win--;
            }
            rc = s_recip[win];
        }
    }
    const int ring_n = R + 1;
    for (int i0 = 0; i0 < w; i0 += STRIP) {
        int n = w - i0 > STRIP ? STRIP : w - i0;
        memset(s_sr, 0, (size_t)n * 2);
        memset(s_sg, 0, (size_t)n * 2);
        memset(s_sb, 0, (size_t)n * 2);
        int win = 0;
        for (int j = 0; j <= R && j < h; j++) {
            const uint16_t *row = buf + (size_t)j * w + i0;
            for (int i = 0; i < n; i++) {
                UN(row[i], r, g, b);
                s_sr[i] += r;
                s_sg[i] += g;
                s_sb[i] += b;
            }
            win++;
        }
        for (int j = 0; j < h; j++) {
            uint16_t *row = buf + (size_t)j * w + i0;
            memcpy(s_ring[j % ring_n], row, (size_t)n * 2);
            uint32_t rc = s_recip[win];
            for (int i = 0; i < n; i++) {
                row[i] = PK((int)(((uint32_t)s_sr[i] * rc) >> 20), (int)(((uint32_t)s_sg[i] * rc) >> 20),
                            (int)(((uint32_t)s_sb[i] * rc) >> 20));
            }
            int add = j + R + 1, rem = j - R;
            if (add < h) {
                const uint16_t *a = buf + (size_t)add * w + i0;
                for (int i = 0; i < n; i++) {
                    UN(a[i], r, g, b);
                    s_sr[i] += r;
                    s_sg[i] += g;
                    s_sb[i] += b;
                }
                win++;
            }
            if (rem >= 0) {
                const uint16_t *d = s_ring[rem % ring_n];
                for (int i = 0; i < n; i++) {
                    UN(d[i], r, g, b);
                    s_sr[i] -= r;
                    s_sg[i] -= g;
                    s_sb[i] -= b;
                }
                win--;
            }
        }
    }
}
