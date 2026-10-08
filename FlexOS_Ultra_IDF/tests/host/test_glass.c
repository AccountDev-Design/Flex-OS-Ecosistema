#include <stdlib.h>
#include <string.h>
#include "flex_glass_math.h"
#include "flex_test.h"

void ref_glass_level(uint8_t lvl, uint8_t out[9]);
int ref_glass_luma(uint16_t c);
uint8_t ref_glass_tint_mix(uint32_t sum, int n, uint16_t tint, uint8_t min_mix);
void ref_glass_shade(int j, int h, uint16_t *c, uint8_t *a);
int ref_glass_inset(int j, int h, int rad);
void ref_glass_blur(uint16_t *buf, int w, int h, int R);

static uint32_t s_seed = 12345;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 8;
}

void test_glass(void)
{
    flex_glass_params_t p;
    for (int lvl = 0; lvl <= 255; lvl++) {
        uint8_t ref[9];
        ref_glass_level((uint8_t)lvl, ref);
        flex_glass_level_apply((uint8_t)lvl, &p);
        uint8_t mine[9] = {p.level, p.blur_r, p.tint_min, p.tint_max, p.tint_base, p.spec, p.shade,
                           p.corner_strong, p.corner_weak};
        CHECK(memcmp(ref, mine, 9) == 0);
    }
    // Nivel 50 = material historico.
    flex_glass_level_apply(50, &p);
    CHECK_EQ_I(p.blur_r, 6);
    CHECK_EQ_I(p.tint_min, 46);
    CHECK_EQ_I(p.tint_max, 70);
    CHECK_EQ_I(p.spec, 26);
    CHECK_EQ_I(p.shade, 30);
    for (uint32_t c = 0; c < 65536; c += 3) {
        CHECK_EQ_I(flex_glass_luma((uint16_t)c), ref_glass_luma((uint16_t)c));
    }
    for (int lvl = 0; lvl <= 100; lvl += 25) {
        uint8_t ref[9];
        ref_glass_level((uint8_t)lvl, ref);   // deja el nivel en la referencia
        flex_glass_level_apply((uint8_t)lvl, &p);
        for (int k = 0; k < 2000; k++) {
            int n = (int)(rnd() % 50);
            uint32_t sum = n ? (rnd() % 267) * (uint32_t)n : 0;
            uint16_t tint = (uint16_t)rnd();
            uint8_t mm = (k % 3 == 0) ? 150 : 0;
            CHECK_EQ_I(flex_glass_tint_mix(&p, sum, n, tint, mm), ref_glass_tint_mix(sum, n, tint, mm));
        }
        for (int h = 1; h < 120; h += 7) {
            for (int j = 0; j < h; j++) {
                uint16_t c1, c2;
                uint8_t a1, a2;
                flex_glass_shade_row(&p, j, h, &c1, &a1);
                ref_glass_shade(j, h, &c2, &a2);
                CHECK(c1 == c2 && a1 == a2);
            }
        }
    }
    for (int rad = 0; rad < 40; rad += 3) {
        for (int h = 2 * rad + 1; h < 2 * rad + 30; h += 5) {
            for (int j = 0; j < h; j++) {
                CHECK_EQ_I(flex_glass_inset(j, h, rad), ref_glass_inset(j, h, rad));
            }
        }
    }
    // Blur: geometrias y radios al azar, y la pantalla entera.
    uint16_t *a = malloc(480 * 800 * 2), *b = malloc(480 * 800 * 2);
    for (int k = 0; k < 60; k++) {
        int w = 1 + (int)(rnd() % 480), h = 1 + (int)(rnd() % 300), R = (int)(rnd() % 17);
        if (k == 0) {
            w = 480;
            h = 800;
            R = 6;
        }
        for (int i = 0; i < w * h; i++) {
            a[i] = b[i] = (uint16_t)rnd();
        }
        flex_glass_blur(a, w, h, R);
        ref_glass_blur(b, w, h, R);
        CHECK(memcmp(a, b, (size_t)w * h * 2) == 0);
    }
    free(a);
    free(b);
}
