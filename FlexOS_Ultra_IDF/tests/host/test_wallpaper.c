#include <stdlib.h>
#include <string.h>
#include "flex_test.h"
#include "flex_wallpaper.h"

void ref_wall_render(uint16_t *buf, int id, bool blobs, int y0, int y1);
void ref_wall_palette(const uint16_t *buf, uint16_t *acc, uint16_t *acc2);
uint16_t ref_on_color(uint16_t c);

#define NPIX (FLEX_WALL_W * FLEX_WALL_H)

static long diff_count(const uint16_t *a, const uint16_t *b)
{
    long n = 0;
    for (long i = 0; i < NPIX; i++) {
        n += a[i] != b[i];
    }
    return n;
}

void test_wallpaper(void)
{
    uint16_t *ref = malloc(NPIX * 2), *mine = malloc(NPIX * 2);
    for (int id = 0; id < FLEX_WALL_N; id++) {
        for (int blobs = 0; blobs < 2; blobs++) {
            memset(ref, 0x5A, NPIX * 2);
            memset(mine, 0x5A, NPIX * 2);
            ref_wall_render(ref, id, blobs, 0, FLEX_WALL_H - 1);
            flex_wall_render(mine, id, blobs, 0, FLEX_WALL_H - 1);
            long d = diff_count(ref, mine);
            if (d) {
                fprintf(stderr, "fondo %d (blobs %d): %ld pixeles distintos\n", id, blobs, d);
            }
            CHECK_EQ_I(d, 0);
            // Que no sea un fondo vacio: las esquinas opuestas difieren (salvo Onyx, casi negro).
            CHECK(id == 4 || mine[(FLEX_WALL_H - 1) * FLEX_WALL_W] != mine[FLEX_WALL_W - 1]);
            CHECK(mine[0] != 0x5A5A);
            uint16_t a1, a2;
            flex_wall_palette_t p;
            ref_wall_palette(ref, &a1, &a2);
            flex_wall_palette(mine, &p);
            CHECK_EQ_I(p.acc, a1);
            CHECK_EQ_I(p.acc2, a2);
        }
        // Por bandas (como se pinta durante un gesto): igual que de una vez.
        memset(mine, 0x5A, NPIX * 2);
        for (int y = 0; y < FLEX_WALL_H; y += 37) {
            flex_wall_render(mine, id, true, y, y + 36);
        }
        ref_wall_render(ref, id, true, 0, FLEX_WALL_H - 1);
        CHECK_EQ_I(diff_count(ref, mine), 0);
    }
    // Ruta segura: id invalido o "imagen" sin cargar = fondo 0.
    flex_wall_render(mine, 99, false, 0, FLEX_WALL_H - 1);
    ref_wall_render(ref, 0, false, 0, FLEX_WALL_H - 1);
    CHECK_EQ_I(diff_count(ref, mine), 0);
    for (uint32_t c = 0; c < 65536; c += 7) {
        CHECK(flex_on_color((uint16_t)c) == ref_on_color((uint16_t)c));
    }
    free(ref);
    free(mine);
}
