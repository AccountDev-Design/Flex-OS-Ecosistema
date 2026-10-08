#include "flex_wallmgr.h"

#include <stdlib.h>
#include <string.h>
#include "flex_glass_math.h"
#include "flex_theme.h"
#include "flex_wallpaper.h"

#define W FLEX_WALL_W
#define H FLEX_WALL_H
#define BYTES ((size_t)W * H * 2)

typedef struct {
    uint16_t *img;          // fondo nitido
    uint16_t *blur;         // backdrop desenfocado
    int id;                 // fondo generado en img (-1 = ninguno)
    bool blobs;
    int blur_r;             // radio con el que se genero blur (-1 = ninguno)
    lv_image_dsc_t dsc;
} wall_t;

static wall_t s_w[2] = {{.id = -1, .blur_r = -1}, {.id = -1, .blur_r = -1}};

static void set_dsc(wall_t *w)
{
    memset(&w->dsc, 0, sizeof(w->dsc));
    w->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    w->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    w->dsc.header.w = W;
    w->dsc.header.h = H;
    w->dsc.header.stride = W * 2;
    w->dsc.data = (const uint8_t *)w->img;
    w->dsc.data_size = BYTES;
}

// La imagen del almacenamiento (200) llega con el decodificador JPEG (Fase 9);
// hasta entonces, la ruta segura de la version Arduino: el fondo 0.
static int effective_id(uint8_t pref)
{
    return pref < FLEX_WALL_N ? pref : 0;
}

static void ensure(flex_wall_which_t which)
{
    wall_t *w = &s_w[which];
    const flex_look_prefs_t *lp = flex_look();
    int id = effective_id(which == FLEX_WALL_HOME ? lp->wall_home : lp->wall_lock);
    bool blobs = which == FLEX_WALL_HOME;   // manchas del fondo 0 solo en el escritorio
    if (!w->img) {
        w->img = malloc(BYTES);   // > 16 KB: PSRAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
        if (!w->img) {
            LV_LOG_WARN("sin memoria para el fondo %d", (int)which);
            return;
        }
        w->id = -1;
    }
    if (w->id != id || w->blobs != blobs) {
        flex_wall_render(w->img, id, blobs, 0, H - 1);
        w->id = id;
        w->blobs = blobs;
        w->blur_r = -1;
        set_dsc(w);   // sin cache de imagenes en LVGL (LV_CACHE_DEF_SIZE = 0): nada que soltar
        if (which == FLEX_WALL_HOME) {
            flex_wall_palette_t p;
            flex_wall_palette(w->img, &p);
            flex_theme_set_wall_accent(p.acc, p.acc2);
        }
    }
    // El backdrop solo hace falta con Liquid Glass (material o iconos de vidrio).
    bool need_blur = lp->glass || lp->icon_style == 1;
    int r = flex_glass_params()->blur_r;
    if (!need_blur) {
        free(w->blur);
        w->blur = NULL;
        w->blur_r = -1;
        return;
    }
    if (w->blur && w->blur_r == r) {
        return;
    }
    if (!w->blur) {
        w->blur = malloc(BYTES);
        if (!w->blur) {
            LV_LOG_WARN("sin memoria para el backdrop %d: el vidrio cae a tinte", (int)which);
            return;
        }
    }
    memcpy(w->blur, w->img, BYTES);
    flex_glass_blur(w->blur, W, H, r);
    w->blur_r = r;
}

static void on_theme(void *ctx)
{
    (void)ctx;
    ensure(FLEX_WALL_HOME);
    ensure(FLEX_WALL_LOCK);
}

void flex_wallmgr_init(void)
{
    ensure(FLEX_WALL_HOME);
    ensure(FLEX_WALL_LOCK);
    flex_theme_listen(on_theme, NULL);
}

const lv_image_dsc_t *flex_wallmgr_image(flex_wall_which_t w)
{
    return s_w[w].img ? &s_w[w].dsc : NULL;
}

const uint16_t *flex_wallmgr_backdrop(flex_wall_which_t w)
{
    if (!s_w[w].blur) {
        ensure(w);   // se solto por memoria: se rehace al necesitarlo
    }
    return s_w[w].blur;
}

bool flex_wallmgr_backdrop_luma(flex_wall_which_t w, const lv_area_t *a, uint32_t *sum, int *n)
{
    const uint16_t *b = s_w[w].blur;
    *sum = 0;
    *n = 0;
    if (!b) {
        return false;
    }
    int x1 = a->x1 < 0 ? 0 : a->x1, y1 = a->y1 < 0 ? 0 : a->y1;
    int x2 = a->x2 > W - 1 ? W - 1 : a->x2, y2 = a->y2 > H - 1 ? H - 1 : a->y2;
    // 1 de cada 4 filas x 1 de cada 8 columnas (Theme.h, tinte adaptativo)
    for (int y = y1; y <= y2; y += 4) {
        const uint16_t *row = b + (size_t)y * W;
        for (int x = x1; x <= x2; x += 8) {
            *sum += (uint32_t)flex_glass_luma(row[x]);
            (*n)++;
        }
    }
    return *n > 0;
}

size_t flex_wallmgr_bytes(void)
{
    size_t n = 0;
    for (int i = 0; i < 2; i++) {
        n += (s_w[i].img ? BYTES : 0) + (s_w[i].blur ? BYTES : 0);
    }
    return n;
}

size_t flex_wallmgr_shed(void)
{
    size_t n = 0;
    for (int i = 0; i < 2; i++) {
        if (s_w[i].blur) {
            free(s_w[i].blur);
            s_w[i].blur = NULL;
            s_w[i].blur_r = -1;
            n += BYTES;
        }
    }
    return n;
}
