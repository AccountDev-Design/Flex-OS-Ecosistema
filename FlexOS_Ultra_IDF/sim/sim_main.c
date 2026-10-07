// Simulador de la pantalla de prueba de Flex OS Ultra (Fase 1), sin SDL.
//
// Compila la MISMA pantalla que el firmware (flex_ui_diag.c, LVGL puro) y la
// conduce con un reloj simulado y un guion de toques. Ademas modela lo que el
// PC no tiene: la cache de la CPU frente a la PSRAM que lee el DMA del panel.
//
//   s_cpu[i]  lo que LVGL escribe en el framebuffer i (vista de la CPU)
//   s_dma[i]  lo que el DMA leeria de la PSRAM; solo se actualiza con las filas
//             que el puerto real (flex_flip_rows.h) manda escribir al cambiar de FB
//
// Comprobaciones (cualquier fallo -> codigo de salida 1):
//   1. Lo "mostrado" (s_dma del FB visible) es identico, pixel a pixel, a un
//      render completo de la pantalla (lv_snapshot) en cada paso del guion.
//      Si al calculo de filas se le escapa algo, aqui aparece.
//   2. LVGL nunca escribe en el framebuffer que se esta mostrando (sin tearing).
//   3. LVGL nunca entrega el FB visible como nuevo cuadro.
//   4. El deslizador de brillo llega a la funcion de brillo.
//   5. Una pantalla quieta (sin animacion ni toques) casi no genera cuadros.
//
// Valores de memoria, chip y GT911: SIMULADOS (no hay placa).
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flex_flip_rows.h"
#include "flex_metrics.h"
#include "flex_ui_diag.h"
#include "lvgl.h"

#define W        480
#define H        800
#define BPP      2
#define STRIDE   (W * BPP)
#define FB_SIZE  (STRIDE * H)
#define STEP_MS  4
#define END_MS   4200
#define VSYNC_US 16540   // 34,2857 MHz / (581 x 976)

static uint32_t s_ms;
static uint8_t *s_cpu[2];
static uint8_t *s_dma[2];
static uint8_t *s_front_copy;     // vista de CPU del FB visible cuando paso a visible
static int s_front;               // el DPI arranca barriendo fb0
static int s_pending = -1;
static flex_flip_rows_t s_rows;
static lv_display_t *s_disp;

static unsigned s_flips, s_compared, s_bad_frames, s_front_writes, s_front_submits;
static unsigned s_bright_calls;
static uint8_t s_bright_last;

typedef struct {
    uint32_t t0, t1;          // [t0, t1) en ms
    int x0, y0, x1, y1;       // primer dedo: interpola de (x0,y0) a (x1,y1)
    int x2, y2;               // segundo dedo si x2 >= 0
} touch_seg_t;

// Coordenadas sacadas del layout de flex_ui_diag.c (480x800).
static const touch_seg_t k_script[] = {
    {600, 700, 8, 8, 8, 8, -1, -1},              // esquina 0,0
    {900, 1000, 471, 791, 471, 791, -1, -1},     // esquina 479,799
    {1200, 1600, 60, 200, 420, 700, -1, -1},     // arrastre en diagonal
    {1800, 2100, 120, 400, 120, 400, 360, 400},  // dos dedos
    {2300, 2380, 280, 532, 280, 532, -1, -1},    // interruptor "Estres" -> on
    {2700, 2780, 280, 532, 280, 532, -1, -1},    // interruptor "Estres" -> off
    {2950, 3350, 60, 485, 300, 485, -1, -1},     // deslizador de brillo (acaba en ~65 %)
    {3500, 3580, 60, 532, 60, 532, -1, -1},      // interruptor "Animacion" -> off
};

static const struct {
    uint32_t t;
    const char *name;
} k_shots[] = {
    {400, "01_inicio"},
    {1400, "02_arrastre"},
    {1950, "03_dos_dedos"},
    {2500, "04_estres"},
    {3360, "05_brillo"},
    {4200, "06_quieta"},
};

static uint32_t tick_cb(void)
{
    return s_ms;
}

static int64_t now_us(void)
{
    return (int64_t)s_ms * 1000;
}

static bool script_touch(flex_diag_touch_t *out)
{
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < sizeof(k_script) / sizeof(k_script[0]); i++) {
        const touch_seg_t *s = &k_script[i];
        if (s_ms < s->t0 || s_ms >= s->t1) {
            continue;
        }
        float f = (float)(s_ms - s->t0) / (float)(s->t1 - s->t0);
        out->count = 1;
        out->pts[0].id = 0;
        out->pts[0].x = (int16_t)(s->x0 + (s->x1 - s->x0) * f);
        out->pts[0].y = (int16_t)(s->y0 + (s->y1 - s->y0) * f);
        if (s->x2 >= 0) {
            out->count = 2;
            out->pts[1].id = 1;
            out->pts[1].x = (int16_t)s->x2;
            out->pts[1].y = (int16_t)s->y2;
        }
        return true;
    }
    return true;
}

static void indev_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static int32_t lx, ly;
    static bool was_pressed;
    flex_diag_touch_t t;
    script_touch(&t);
    bool pressed = t.count > 0;
    if (pressed) {
        bool moved = t.pts[0].x != lx || t.pts[0].y != ly;
        if (moved || !was_pressed) {
            flex_metrics_input(now_us() - 4000, now_us());
        }
        lx = t.pts[0].x;
        ly = t.pts[0].y;
    } else if (was_pressed) {
        flex_metrics_input(now_us() - 4000, now_us());
    }
    was_pressed = pressed;
    data->point.x = lx;
    data->point.y = ly;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void sys_cb(flex_diag_sys_t *out)
{
    memset(out, 0, sizeof(*out));
    out->heap_internal_free_kb = 300;
    out->heap_internal_min_kb = 280;
    out->psram_free_kb = 30000;
    out->chip_rev = 100;
    out->build_rev_min = 1;
    out->build_rev_max = 199;
    out->touch_present = true;
    out->touch_addr = 0x5D;
    memcpy(out->touch_id, "SIM", 4);
    out->touch_res_x = W;
    out->touch_res_y = H;
}

static void bright_cb(uint8_t pct)
{
    s_bright_calls++;
    s_bright_last = pct;
}

static int fb_index(const uint8_t *p)
{
    return p == s_cpu[0] ? 0 : (p == s_cpu[1] ? 1 : -1);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    flex_flip_rows_add(&s_rows, area->y1, area->y2);
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    int idx = fb_index(px_map);
    if (idx < 0) {
        fprintf(stderr, "flush con un buffer que no es un framebuffer\n");
        exit(1);
    }
    if (idx == s_front) {
        s_front_submits++;
    }
    int32_t y1, y2;
    flex_flip_rows_take(&s_rows, 0, &y1, &y2);
    // Modelo de esp_cache_msync(C2M) sobre esas filas
    memcpy(s_dma[idx] + (size_t)y1 * STRIDE, s_cpu[idx] + (size_t)y1 * STRIDE, (size_t)(y2 - y1 + 1) * STRIDE);
    s_pending = idx;
    s_flips++;
    flex_metrics_frame_flipped(now_us());
}

// Fin de cuadro simulado: el FB entregado pasa a ser el visible.
static void complete_flip(void)
{
    if (s_pending < 0) {
        return;
    }
    if (memcmp(s_cpu[s_front], s_front_copy, FB_SIZE) != 0) {
        s_front_writes++;   // LVGL escribio en el FB mientras se mostraba
    }
    s_front = s_pending;
    s_pending = -1;
    memcpy(s_front_copy, s_cpu[s_front], FB_SIZE);
    flex_metrics_frame_shown(now_us() + VSYNC_US / 2);
}

static void flush_wait_cb(lv_display_t *disp)
{
    (void)disp;
    complete_flip();
}

static void render_start_cb(lv_event_t *e)
{
    (void)e;
    flex_metrics_render_start(now_us());
}

static void write_ppm(const char *dir, const char *name, const uint8_t *fb)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = (uint16_t)(fb[2 * i] | (fb[2 * i + 1] << 8));
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                          (uint8_t)((c & 0x1F) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// Compara lo mostrado con un render completo de referencia.
static void verify(const char *outdir)
{
    lv_draw_buf_t *ref = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!ref) {
        fprintf(stderr, "lv_snapshot_take fallo\n");
        exit(1);
    }
    unsigned diff = 0;
    for (int y = 0; y < H; y++) {
        const uint8_t *a = s_dma[s_front] + (size_t)y * STRIDE;
        const uint8_t *b = ref->data + (size_t)y * ref->header.stride;
        if (memcmp(a, b, STRIDE) != 0) {
            for (int x = 0; x < W; x++) {
                diff += (a[2 * x] != b[2 * x]) || (a[2 * x + 1] != b[2 * x + 1]);
            }
        }
    }
    s_compared++;
    if (diff) {
        if (s_bad_frames++ == 0) {
            fprintf(stderr, "t=%u ms: %u pixeles distintos entre lo mostrado y la referencia\n", s_ms, diff);
            write_ppm(outdir, "fallo_mostrado", s_dma[s_front]);
            uint8_t *packed = malloc(FB_SIZE);
            for (int y = 0; y < H; y++) {
                memcpy(packed + (size_t)y * STRIDE, ref->data + (size_t)y * ref->header.stride, STRIDE);
            }
            write_ppm(outdir, "fallo_referencia", packed);
            free(packed);
        }
    }
    lv_draw_buf_destroy(ref);
}

static uint8_t *alloc_fb(void)
{
    uint8_t *p = aligned_alloc(64, FB_SIZE);
    if (!p) {
        exit(1);
    }
    memset(p, 0, FB_SIZE);   // el driver DPI reserva los FB a cero (negro)
    return p;
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : ".";
    for (int i = 0; i < 2; i++) {
        s_cpu[i] = alloc_fb();
        s_dma[i] = alloc_fb();
    }
    s_front_copy = alloc_fb();
    flex_flip_rows_init(&s_rows, H);

    lv_init();
    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(W, H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    // Igual que el firmware: buf_1 = fb1 porque el DPI arranca mostrando fb0.
    lv_display_set_buffers(s_disp, s_cpu[1], s_cpu[0], FB_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_flush_wait_cb(s_disp, flush_wait_cb);
    lv_display_add_event_cb(s_disp, render_start_cb, LV_EVENT_RENDER_START, NULL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read);
    lv_indev_set_display(indev, s_disp);
    lv_timer_set_period(lv_indev_get_read_timer(indev), 8);

    static flex_diag_ops_t ops = {
        .get_metrics = flex_metrics_get,
        .get_touch = script_touch,
        .get_sys = sys_cb,
        .set_brightness = bright_cb,
        .initial_brightness = 80,
    };
    flex_ui_diag_create(&ops);

    size_t shot = 0;
    unsigned quiet_flips_start = 0;
    for (s_ms = 0; s_ms <= END_MS; s_ms += STEP_MS) {
        lv_timer_handler();
        lv_refr_now(s_disp);
        complete_flip();
        lv_display_flush_ready(s_disp);
        flex_metrics_tick(now_us(), (uint32_t)(now_us() / VSYNC_US));
        verify(outdir);
        if (s_ms == 3800) {
            quiet_flips_start = s_flips;
        }
        if (shot < sizeof(k_shots) / sizeof(k_shots[0]) && s_ms >= k_shots[shot].t) {
            write_ppm(outdir, k_shots[shot].name, s_dma[s_front]);
            shot++;
        }
    }
    unsigned quiet_flips = s_flips - quiet_flips_start;

    printf("cuadros entregados:            %u\n", s_flips);
    printf("comprobaciones contra referencia: %u (distintas: %u)\n", s_compared, s_bad_frames);
    printf("escrituras en el FB visible:   %u\n", s_front_writes);
    printf("FB visible entregado de nuevo: %u\n", s_front_submits);
    printf("llamadas al brillo:            %u (ultimo %u %%)\n", s_bright_calls, s_bright_last);
    printf("cuadros con la pantalla quieta (3,8-4,2 s): %u\n", quiet_flips);

    bool ok = s_flips > 0 && s_bad_frames == 0 && s_front_writes == 0 && s_front_submits == 0 &&
              s_bright_calls > 0 && s_bright_last >= 60 && s_bright_last <= 70 && quiet_flips <= 2;
    printf(ok ? "RESULTADO: OK\n" : "RESULTADO: FALLO\n");
    return ok ? 0 : 1;
}
