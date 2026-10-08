// Simulador de la INTERFAZ COMPLETA de Flex OS Ultra en el PC (sin SDL).
//
// Compila los mismos archivos de flex_ui que el firmware (tema, vidrio,
// iconos, shell, apps) contra servicios simulados (stubs/), los conduce con un
// reloj simulado y un guion de toques, y guarda capturas 480x800.
//
//   flexos_ui_sim <carpeta> [escena ...]      (sin escenas: todas)
//
// Lo que se ve es lo que dibuja LVGL; los datos del sistema son SIMULADOS.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_storage.h"
#include "lvgl.h"
#include "ui_sim.h"

#define W 480
#define H 800

static uint32_t s_ms;
static uint16_t *s_fb;
static lv_display_t *s_disp;
static const char *s_out = "sim/out";
static sim_touch_t s_touch;

static uint32_t tick_cb(void) { return s_ms; }
uint16_t *sim_fb(void) { return s_fb; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    int32_t w = lv_area_get_width(a);
    for (int32_t y = a->y1; y <= a->y2; y++) {
        memcpy(s_fb + (size_t)y * W + a->x1, px + (size_t)(y - a->y1) * w * 2, (size_t)w * 2);
    }
    lv_display_flush_ready(d);
}

static void indev_read(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point.x = s_touch.x;
    d->point.y = s_touch.y;
    d->state = s_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void sim_run(uint32_t ms)
{
    uint32_t end = s_ms + ms;
    while (s_ms < end) {
        s_ms += 4;
        extern size_t flex_inbox_drain(size_t);
        flex_inbox_drain(16);
        lv_timer_handler();
    }
}

void sim_touch(int x, int y, bool pressed)
{
    s_touch.x = x;
    s_touch.y = y;
    s_touch.pressed = pressed;
}

void sim_tap(int x, int y)
{
    sim_touch(x, y, true);
    sim_run(80);
    sim_touch(x, y, false);
    sim_run(200);
}

void sim_drag(int x0, int y0, int x1, int y1, uint32_t ms)
{
    for (uint32_t t = 0; t <= ms; t += 8) {
        float f = (float)t / (float)ms;
        sim_touch((int)(x0 + (x1 - x0) * f), (int)(y0 + (y1 - y0) * f), true);
        sim_run(8);
    }
    sim_touch(x1, y1, false);
    sim_run(300);
}

void sim_shot(const char *name)
{
    lv_refr_now(s_disp);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", s_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "no puedo escribir %s\n", path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = s_fb[i];
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                          (uint8_t)((c & 0x1F) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("captura %s\n", path);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        s_out = argv[1];
    }
    lv_init();
    lv_tick_set_cb(tick_cb);
    s_fb = calloc(W * H, 2);
    s_disp = lv_display_create(W, H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    static uint8_t *buf;
    buf = aligned_alloc(64, W * H * 2);
    lv_display_set_buffers(s_disp, buf, NULL, W * H * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, indev_read);

    int fails = 0;
    for (int i = 0; sim_scenes[i].name; i++) {
        bool want = argc <= 2;
        for (int a = 2; a < argc; a++) {
            want |= strcmp(argv[a], sim_scenes[i].name) == 0;
        }
        if (want) {
            printf("== escena %s\n", sim_scenes[i].name);
            fails += sim_scenes[i].run() ? 0 : 1;
        }
    }
    printf("%s: %d escenas con fallos\n", fails ? "FALLO" : "OK", fails);
    return fails ? 1 : 0;
}
