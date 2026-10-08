#include "flex_wallpaper.h"

#include <stdlib.h>
#include <string.h>

#define W FLEX_WALL_W
#define H FLEX_WALL_H

const char *const flex_wall_names[FLEX_WALL_N] = {
    "Flex Original", "Aurora", "Nocturno", "Halo", "Onyx", "Oceano", "Violeta", "Naturaleza",
};

const flex_look_t flex_looks[FLEX_LOOK_N] = {
    {"Flex Original", 1, 0, 0, 0, 0, 60, 110, 235, 140, 180, 250},
    {"Claro", 0, 1, 0, 1, 0, 45, 95, 225, 130, 180, 250},
    {"Oscuro", 1, 1, 1, 2, 0, 96, 124, 235, 160, 180, 255},
    {"AMOLED", 1, 0, 0, 4, 0, 120, 140, 255, 180, 196, 255},
    {"Oceano", 1, 1, 1, 5, 1, 38, 190, 196, 150, 240, 236},
    {"Violeta", 1, 1, 1, 6, 1, 186, 96, 232, 224, 168, 248},
    {"Naturaleza", 0, 1, 0, 7, 1, 72, 170, 80, 168, 224, 150},
    {"Alto contraste", 1, 0, 0, 4, 0, 255, 214, 10, 255, 236, 120},
};

#define TC(r, g, b) flex_rgb565((r), (g), (b))
// v/255 exacto para v en [0, 65534] sin dividir.
#define DIV255(v) (uint16_t)(((v) + 1u + ((v) >> 8)) >> 8)

uint16_t flex_mix565(uint16_t a, uint16_t b, uint8_t t)
{
    uint32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint32_t it = 255u - t;
    uint32_t rr = ar * it + br * t, rg = ag * it + bg * t, rb = ab * it + bb * t;
    return (uint16_t)((DIV255(rr) << 11) | (DIV255(rg) << 5) | DIV255(rb));
}

int flex_isqrt32(int v)
{
    if (v <= 0) {
        return 0;
    }
    uint32_t op = (uint32_t)v, res = 0, one = 1u << 30;
    while (one > op) {
        one >>= 2;
    }
    while (one) {
        if (op >= res + one) {
            op -= res + one;
            res += one << 1;
        }
        res >>= 1;
        one >>= 2;
    }
    return (int)res;
}

// ---- estado de una generacion (recorte = banda pedida) ---------------------
typedef struct {
    uint16_t *buf;
    int y0, y1;
    uint8_t lut[512];
    int lut_rmax;
} wall_ctx_t;

#define WLUT_N 512
#define WLUT_SHIFT 11
#define WLUT_DMAX 1023

static uint16_t s_grad_lut[256];   // fondo 0 por t
static uint8_t s_tx_lut[W];        // rampa horizontal
static bool s_lut_ready;

static void ensure_lut(void)
{
    if (s_lut_ready) {
        return;
    }
    const uint16_t green = flex_rgb565(80, 224, 74), blue = flex_rgb565(40, 150, 245),
                   purple = flex_rgb565(112, 46, 230);
    for (int t = 0; t < 256; t++) {
        s_grad_lut[t] = (t < 128) ? flex_mix565(purple, blue, (uint8_t)(t * 2))
                                  : flex_mix565(blue, green, (uint8_t)((t - 128) * 2));
    }
    for (int x = 0; x < W; x++) {
        s_tx_lut[x] = (uint8_t)((x * 255) / (W - 1));
    }
    s_lut_ready = true;
}

static void lut_disc(wall_ctx_t *c, int r_in, int r_out, uint8_t a_in)
{
    if (r_out < 1) {
        r_out = 1;
    }
    if (r_in > r_out) {
        r_in = r_out;
    }
    for (int i = 0; i < WLUT_N; i++) {
        int d = flex_isqrt32(i << WLUT_SHIFT);
        c->lut[i] = d <= r_in ? a_in : d >= r_out ? 0 : (uint8_t)((int)a_in * (r_out - d) / (r_out - r_in));
    }
    c->lut_rmax = r_out;
}

static void lut_ring(wall_ctx_t *c, int r, int hw, uint8_t a_pk)
{
    if (hw < 1) {
        hw = 1;
    }
    for (int i = 0; i < WLUT_N; i++) {
        int d = flex_isqrt32(i << WLUT_SHIFT), k = d > r ? d - r : r - d;
        c->lut[i] = (k >= hw) ? 0 : (uint8_t)((int)a_pk * (hw - k) / hw);
    }
    c->lut_rmax = r + hw;
}

// Mezcla col con el alfa de la LUT segun la distancia a (cx, cy). Solo recorre
// el circulo donde el alfa puede no ser 0 (+48 de margen por el redondeo del
// indice de la LUT), como la version Arduino.
static void radial(wall_ctx_t *c, int cx, int cy, uint16_t col)
{
    int y0 = c->y0, y1 = c->y1, x0 = 0, x1 = W - 1;
    bool bounded = c->lut_rmax <= WLUT_DMAX;
    int R = c->lut_rmax + 48;
    if (bounded) {
        if (cy - R > y0) {
            y0 = cy - R;
        }
        if (cy + R < y1) {
            y1 = cy + R;
        }
    }
    for (int y = y0; y <= y1; y++) {
        int32_t dy = y - cy, dy2 = dy * dy;
        int xa = x0, xb = x1;
        if (bounded) {
            int hw = flex_isqrt32(R * R - (int)dy2);
            if (cx - hw > xa) {
                xa = cx - hw;
            }
            if (cx + hw < xb) {
                xb = cx + hw;
            }
            if (xa > xb) {
                continue;
            }
        }
        uint16_t *row = c->buf + (size_t)y * W;
        int32_t dx = xa - cx, dx2 = dx * dx;
        for (int x = xa; x <= xb; x++) {
            uint32_t idx = (uint32_t)((dx2 + dy2) >> WLUT_SHIFT);
            if (idx >= WLUT_N) {
                idx = WLUT_N - 1;
            }
            uint8_t a = c->lut[idx];
            if (a) {
                row[x] = (a >= 255) ? col : flex_mix565(row[x], col, a);
            }
            dx2 += 2 * dx + 1;
            dx++;
        }
    }
}

// Disco opaco con degradado lineal (acumulador de 64 bits: en 32 desbordaria).
static void disc(wall_ctx_t *c, int cx, int cy, int r, uint16_t c0, uint16_t c1, int dirx, int diry)
{
    if (r <= 0) {
        return;
    }
    int r2 = r * r, y0 = cy - r, y1 = cy + r;
    if (y0 < c->y0) {
        y0 = c->y0;
    }
    if (y1 > c->y1) {
        y1 = c->y1;
    }
    int denom = r * (abs(dirx) + abs(diry));
    if (denom < 1) {
        denom = 1;
    }
    for (int y = y0; y <= y1; y++) {
        int dy = y - cy, hw = flex_isqrt32(r2 - dy * dy);
        if (hw <= 0) {
            continue;
        }
        int xa = cx - hw, xb = cx + hw;
        if (xa < 0) {
            xa = 0;
        }
        if (xb > W - 1) {
            xb = W - 1;
        }
        if (xa > xb) {
            continue;
        }
        int32_t base = (int32_t)dy * diry;
        int32_t acc = (int32_t)(((int64_t)((int32_t)(xa - cx) * dirx + base) * 127 * 65536) / denom) + (128 << 16);
        int32_t step = (int32_t)(((int64_t)dirx * 127 * 65536) / denom);
        uint16_t *row = c->buf + (size_t)y * W;
        for (int x = xa; x <= xb; x++) {
            int32_t tt = acc >> 16;
            tt = tt < 0 ? 0 : tt > 255 ? 255 : tt;
            row[x] = flex_mix565(c0, c1, (uint8_t)tt);
            acc += step;
        }
    }
}

static void diag3(wall_ctx_t *c, uint16_t lo, uint16_t mid, uint16_t hi)
{
    uint16_t lut[256];
    for (int t = 0; t < 256; t++) {
        lut[t] = (t < 128) ? flex_mix565(lo, mid, (uint8_t)(t * 2)) : flex_mix565(mid, hi, (uint8_t)((t - 128) * 2));
    }
    for (int y = c->y0; y <= c->y1; y++) {
        int ty = ((H - 1 - y) * 255) / (H - 1);
        uint16_t *row = c->buf + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            row[x] = lut[(s_tx_lut[x] + ty) >> 1];
        }
    }
}

static void corners4(wall_ctx_t *c, uint16_t tl, uint16_t tr, uint16_t bl, uint16_t br)
{
    for (int y = c->y0; y <= c->y1; y++) {
        uint8_t ty = (uint8_t)((y * 255) / (H - 1));
        uint16_t L = flex_mix565(tl, bl, ty), R = flex_mix565(tr, br, ty);
        uint16_t *row = c->buf + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            row[x] = flex_mix565(L, R, s_tx_lut[x]);
        }
    }
}

// Circulo relleno con alfa (fillCircleA de la version Arduino, gLand = false).
static void circle_a(wall_ctx_t *c, int cx, int cy, int r, uint16_t col, uint8_t a)
{
    for (int j = 0; j < 2 * r + 1; j++) {
        int y = cy - r + j;
        if (y < c->y0 || y > c->y1) {
            continue;
        }
        int dy = j - r, dx = flex_isqrt32(r * r - dy * dy);
        int x = cx - dx, w = 2 * dx + 1;
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (x + w > W) {
            w = W - x;
        }
        uint16_t *p = c->buf + (size_t)y * W + x;
        for (int i = 0; i < w; i++) {
            p[i] = flex_mix565(p[i], col, a);
        }
    }
}

static void wall_flex_original(wall_ctx_t *c, bool blobs)
{
    for (int y = c->y0; y <= c->y1; y++) {
        int ty = ((H - 1 - y) * 255) / (H - 1);
        uint16_t *row = c->buf + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            row[x] = s_grad_lut[(s_tx_lut[x] + ty) >> 1];
        }
    }
    if (blobs) {
        circle_a(c, 360, 150, 220, flex_rgb565(150, 235, 180), 60);
        circle_a(c, 90, 560, 260, flex_rgb565(150, 160, 240), 55);
    }
}

static void wall_aurora(wall_ctx_t *c)
{
    corners4(c, TC(205, 238, 236), TC(48, 132, 240), TC(72, 208, 206), TC(24, 86, 220));
    lut_disc(c, 236, 258, 90);
    radial(c, 330, 205, TC(255, 255, 255));
    disc(c, 330, 205, 246, TC(26, 68, 205), TC(104, 176, 252), -1, -1);
    lut_disc(c, 244, 264, 105);
    radial(c, 96, 486, TC(255, 255, 255));
    disc(c, 96, 486, 250, TC(86, 220, 176), TC(186, 248, 220), 1, -1);
    lut_disc(c, 150, 330, 60);
    radial(c, 430, 760, TC(140, 205, 255));
}

static void wall_nocturno(wall_ctx_t *c)
{
    diag3(c, TC(4, 5, 14), TC(8, 9, 26), TC(5, 6, 18));
    disc(c, 340, 110, 300, TC(10, 12, 38), TC(62, 70, 152), -1, -1);
    lut_ring(c, 300, 6, 225);
    radial(c, 340, 110, TC(182, 192, 255));
    disc(c, 92, 620, 330, TC(9, 10, 34), TC(70, 78, 160), 1, -1);
    lut_ring(c, 330, 7, 240);
    radial(c, 92, 620, TC(196, 204, 255));
    disc(c, 456, 468, 186, TC(8, 9, 30), TC(48, 54, 124), -1, 1);
    lut_ring(c, 186, 5, 205);
    radial(c, 456, 468, TC(164, 174, 250));
    lut_disc(c, 70, 260, 34);
    radial(c, 286, 300, TC(120, 134, 235));
}

static void wall_halo(wall_ctx_t *c)
{
    diag3(c, TC(2, 2, 7), TC(5, 5, 16), TC(2, 2, 8));
    lut_disc(c, 230, 430, 70);
    radial(c, 72, 430, TC(46, 74, 205));
    lut_disc(c, 112, 132, 255);
    radial(c, 206, 402, TC(17, 22, 58));
    lut_ring(c, 118, 26, 190);
    radial(c, 206, 402, TC(74, 112, 236));
    lut_ring(c, 122, 5, 235);
    radial(c, 206, 402, TC(176, 198, 255));
    lut_ring(c, 252, 34, 150);
    radial(c, 206, 402, TC(58, 92, 224));
    lut_ring(c, 256, 5, 200);
    radial(c, 206, 402, TC(158, 182, 255));
    lut_ring(c, 392, 44, 120);
    radial(c, 206, 402, TC(44, 74, 206));
    lut_ring(c, 396, 5, 165);
    radial(c, 206, 402, TC(140, 168, 250));
}

static void wall_onyx(wall_ctx_t *c)
{
    for (int y = c->y0; y <= c->y1; y++) {
        memset(c->buf + (size_t)y * W, 0, W * sizeof(uint16_t));
    }
    lut_disc(c, 60, 380, 46);
    radial(c, 240, 690, TC(30, 64, 180));
}

static void wall_oceano(wall_ctx_t *c)
{
    diag3(c, TC(3, 32, 72), TC(10, 116, 168), TC(38, 206, 192));
    lut_disc(c, 70, 300, 50);
    radial(c, 400, 180, TC(180, 244, 236));
}

static void wall_violeta(wall_ctx_t *c)
{
    diag3(c, TC(26, 8, 58), TC(122, 40, 180), TC(232, 124, 204));
    lut_disc(c, 80, 320, 46);
    radial(c, 90, 250, TC(255, 210, 240));
}

static void wall_naturaleza(wall_ctx_t *c)
{
    diag3(c, TC(8, 44, 22), TC(58, 138, 58), TC(186, 222, 122));
    lut_disc(c, 80, 300, 44);
    radial(c, 380, 640, TC(236, 250, 190));
}

void flex_wall_render(uint16_t *buf, int id, bool blobs, int y0, int y1)
{
    if (!buf) {
        return;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= H) {
        y1 = H - 1;
    }
    if (y0 > y1) {
        return;
    }
    ensure_lut();
    wall_ctx_t c = {.buf = buf, .y0 = y0, .y1 = y1, .lut_rmax = WLUT_DMAX + 1};
    if (id < 0 || id >= FLEX_WALL_N) {
        id = 0;   // ruta segura (tambien para la imagen si no esta cargada)
    }
    switch (id) {
    case 1: wall_aurora(&c); break;
    case 2: wall_nocturno(&c); break;
    case 3: wall_halo(&c); break;
    case 4: wall_onyx(&c); break;
    case 5: wall_oceano(&c); break;
    case 6: wall_violeta(&c); break;
    case 7: wall_naturaleza(&c); break;
    default: wall_flex_original(&c, blobs); break;
    }
}

uint8_t flex_lum565(uint16_t c)
{
    int r = ((c >> 11) & 0x1F) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
    return (uint8_t)((r * 77 + g * 151 + b * 28) >> 8);
}

uint16_t flex_on_color(uint16_t bg)
{
    return flex_lum565(bg) > 140 ? TC(16, 18, 26) : TC(255, 255, 255);
}

void flex_wall_palette(const uint16_t *src, flex_wall_palette_t *out)
{
    int best = -1, br = 90, bg = 150, bb = 245;
    for (int y = 8; y < H; y += 40) {
        for (int x = 8; x < W; x += 24) {
            uint16_t c = src[(size_t)y * W + x];
            int r = ((c >> 11) & 0x1F) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
            int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            int sat = (mx - mn) * (mx > 40 ? 1 : 0);   // ignora lo casi negro
            if (sat > best) {
                best = sat;
                br = r;
                bg = g;
                bb = b;
            }
        }
    }
    int lr = br, lg = bg, lb = bb;
    if (((lr * 77 + lg * 151 + lb * 28) >> 8) < 70) {
        lr = lr * 2 + 40;
        lg = lg * 2 + 40;
        lb = lb * 2 + 40;
    }
    lr = lr > 255 ? 255 : lr;
    lg = lg > 255 ? 255 : lg;
    lb = lb > 255 ? 255 : lb;
    out->acc = flex_rgb565((uint8_t)lr, (uint8_t)lg, (uint8_t)lb);
    out->acc2 = flex_rgb565((uint8_t)((lr + 255) / 2), (uint8_t)((lg + 255) / 2), (uint8_t)((lb + 255) / 2));
}
