// Rasterizador de circulos y segmentos suaves (flex_raster.c) frente a fillCircleAA / strokeSegAA de FlexOS_Ultra_Gfx.h, pixel a pixel,
// sobre buffers de cualquier tamano y posicion, con recorte, sobre contenido previo (mezcla) y con geometrias degeneradas.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_raster.h"
#include "flex_test.h"

void ref_raster_circle(uint16_t *screen, int cx0, int cx1, int cy0, int cy1, float cx, float cy, float r, uint16_t col);
void ref_raster_seg(uint16_t *screen, int cx0, int cx1, int cy0, int cy1, float x0, float y0, float x1, float y1, float r, uint16_t col);

static uint32_t s_seed = 4242;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}
static float frnd(float lo, float hi)
{
    return lo + (hi - lo) * (float)(rnd() % 100001) / 100000.0f;
}

static uint16_t g_screen[480 * 800];
static uint16_t g_mine[480 * 800 + 64];

void test_raster(void)
{
    int bad = 0, shapes = 0, touched = 0;
    for (int it = 0; it < 500; it++) {
        // el buffer del lienzo: un trozo de la pantalla, a veces con relleno de fila (stride > w)
        int w = (it % 50 == 0) ? 464 : 16 + (int)(rnd() % 110), h = (it % 50 == 0) ? 574 : 16 + (int)(rnd() % 110);
        int ox = (int)(rnd() % (unsigned)(480 - w + 1)), oy = (int)(rnd() % (unsigned)(800 - h + 1));
        int stride = w + (int)(rnd() % 3) * 5;
        for (int i = 0; i < 480 * 800; i++) g_screen[i] = 0;
        for (int i = 0; i < stride * h; i++) g_mine[i] = 0;
        // contenido previo igual en los dos (para que se vea la mezcla)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint16_t v = (uint16_t)(rnd() % 65536);
                g_screen[(oy + y) * 480 + ox + x] = v;
                g_mine[y * stride + x] = v;
            }
        }
        flex_raster_t r;
        flex_raster_init(&r, g_mine, w, h, stride, ox, oy);
        // recorte del llamante (a veces menor que el buffer, a veces mayor)
        int cx0 = ox, cx1 = ox + w - 1, cy0 = oy, cy1 = oy + h - 1;
        if (rnd() % 3 == 0) {
            cx0 = ox - 5 + (int)(rnd() % (unsigned)(w / 2 + 5));
            cx1 = ox + w / 2 + (int)(rnd() % (unsigned)(w / 2 + 20));
            cy0 = oy - 5 + (int)(rnd() % (unsigned)(h / 2 + 5));
            cy1 = oy + h / 2 + (int)(rnd() % (unsigned)(h / 2 + 20));
        }
        r.cx0 = cx0;
        r.cx1 = cx1;
        r.cy0 = cy0;
        r.cy1 = cy1;
        // el recorte efectivo de la referencia: el del llamante cruzado con el buffer y con la pantalla
        int ex0 = cx0 > ox ? cx0 : ox, ex1 = cx1 < ox + w - 1 ? cx1 : ox + w - 1;
        int ey0 = cy0 > oy ? cy0 : oy, ey1 = cy1 < oy + h - 1 ? cy1 : oy + h - 1;
        for (int k = 0; k < 10; k++) {
            uint16_t col = (uint16_t)(rnd() % 65536);
            float rad = (rnd() % 8 == 0) ? frnd(0.0f, 0.9f) : frnd(0.5f, 30.0f);
            float ax = (float)ox + frnd(-40.0f, (float)w + 40.0f), ay = (float)oy + frnd(-40.0f, (float)h + 40.0f);
            shapes++;
            if (rnd() % 3 == 0) {
                ref_raster_circle(g_screen, ex0, ex1, ey0, ey1, ax, ay, rad, col);
                flex_raster_fill_circle_aa(&r, ax, ay, rad, col);
            } else {
                float bx = ax, by = ay;
                if (rnd() % 6) {
                    bx = (float)ox + frnd(-40.0f, (float)w + 40.0f);
                    by = (float)oy + frnd(-40.0f, (float)h + 40.0f);
                }
                if (rnd() % 5 == 0) {   // horizontales, verticales y de un pixel
                    if (rnd() % 2) by = ay; else bx = ax;
                }
                ref_raster_seg(g_screen, ex0, ex1, ey0, ey1, ax, ay, bx, by, rad, col);
                flex_raster_stroke_seg_aa(&r, ax, ay, bx, by, rad, col);
            }
        }
        // lo de dentro del buffer, igual; lo de fuera, intacto (la referencia no pudo tocarlo: su recorte esta dentro)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint16_t a = g_screen[(oy + y) * 480 + ox + x], b = g_mine[y * stride + x];
                bad += a != b;
                touched += a != 0;
            }
        }
        for (int y = 0; y < 800 && !bad; y += 7) {
            for (int x = 0; x < 480; x++) {
                if ((x < ox || x >= ox + w || y < oy || y >= oy + h) && g_screen[y * 480 + x] != 0) {
                    bad++;
                }
            }
        }
        for (int i = w; i < stride && h > 0; i++) {   // el relleno de fila del buffer propio no se toca
            bad += g_mine[i] != 0;
        }
        if (bad) {
            printf("RASTER diverged in iteration %d (%dx%d at %d,%d stride %d)\n", it, w, h, ox, oy, stride);
            break;
        }
    }
    CHECK(bad == 0 && shapes == 5000 && touched > 100000);
    // sin buffer o sin superficie: no hace nada ni revienta
    flex_raster_t z;
    flex_raster_init(&z, NULL, 0, 0, 0, 0, 0);
    flex_raster_fill_circle_aa(&z, 5, 5, 4, 0xFFFF);
    flex_raster_stroke_seg_aa(&z, 0, 0, 9, 9, 3, 0xFFFF);
    uint16_t one[4] = {0, 0, 0, 0};
    flex_raster_init(&z, one, 2, 2, 2, 100, 100);
    flex_raster_fill_circle_aa(&z, 5, 5, 4, 0xFFFF);                  // lejos: no pinta
    flex_raster_stroke_seg_aa(&z, 0, 0, 9, 9, 3, 0xFFFF);
    CHECK(one[0] == 0 && one[1] == 0 && one[2] == 0 && one[3] == 0);
    flex_raster_fill(&z, 0x1234);
    CHECK(one[0] == 0x1234 && one[3] == 0x1234);
    CHECK(flex_raster_mix565(0x0000, 0xFFFF, 0) == 0x0000 && flex_raster_mix565(0x0000, 0xFFFF, 255) == 0xFFFF);
    printf("raster: circulos y segmentos suaves iguales a Arduino pixel a pixel (%d figuras, %d pixeles pintados)\n", shapes, touched);
}
