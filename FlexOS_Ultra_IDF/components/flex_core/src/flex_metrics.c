#include "flex_metrics.h"

#include <string.h>

#define WINDOW_US 1000000

typedef struct {
    int64_t sum_us;
    int64_t max_us;
    uint32_t n;
} acc_t;

static struct {
    int64_t window_start_us;
    uint32_t vsync_at_start;
    uint32_t frames_in_window;
    uint32_t frames_total;
    uint32_t vsync_timeouts;
    int64_t render_start_us;
    int64_t ui_busy_us;
    acc_t render, vsync_wait, touch_used, touch_flip, touch_shown;
    int latency_state;          // 0 libre, 1 espera cambio de FB, 2 espera fin de cuadro
    int64_t latency_t0;
    flex_metrics_t last;
} s;

static void acc_add(acc_t *a, int64_t v)
{
    if (v < 0) {
        return;
    }
    a->sum_us += v;
    if (v > a->max_us) {
        a->max_us = v;
    }
    a->n++;
}

static float acc_avg_ms(const acc_t *a)
{
    return a->n ? (float)a->sum_us / (float)a->n / 1000.0f : 0.0f;
}

static float acc_max_ms(const acc_t *a)
{
    return (float)a->max_us / 1000.0f;
}

void flex_metrics_render_start(int64_t now_us)
{
    if (s.render_start_us == 0) {
        s.render_start_us = now_us;
    }
}

void flex_metrics_frame_flipped(int64_t now_us)
{
    s.frames_in_window++;
    s.frames_total++;
    if (s.render_start_us != 0) {
        acc_add(&s.render, now_us - s.render_start_us);
        s.render_start_us = 0;
    }
    if (s.latency_state == 1) {
        acc_add(&s.touch_flip, now_us - s.latency_t0);
        s.latency_state = 2;
    }
}

void flex_metrics_frame_shown(int64_t vsync_us)
{
    if (s.latency_state == 2) {
        acc_add(&s.touch_shown, vsync_us - s.latency_t0);
        s.latency_state = 0;
    }
}

void flex_metrics_vsync_wait(int64_t waited_us, bool timed_out)
{
    acc_add(&s.vsync_wait, waited_us);
    if (timed_out) {
        s.vsync_timeouts++;
    }
}

void flex_metrics_input(int64_t t_read_us, int64_t t_used_us)
{
    acc_add(&s.touch_used, t_used_us - t_read_us);
    if (s.latency_state == 0) {
        s.latency_state = 1;
        s.latency_t0 = t_read_us;
    }
}

void flex_metrics_ui_busy(int64_t busy_us)
{
    if (busy_us > 0) {
        s.ui_busy_us += busy_us;
    }
}

bool flex_metrics_tick(int64_t now_us, uint32_t vsync_total)
{
    if (s.window_start_us == 0) {
        s.window_start_us = now_us;
        s.vsync_at_start = vsync_total;
        return false;
    }
    int64_t elapsed = now_us - s.window_start_us;
    if (elapsed < WINDOW_US) {
        return false;
    }
    float secs = (float)elapsed / 1e6f;
    flex_metrics_t m = {
        .panel_hz = (float)(vsync_total - s.vsync_at_start) / secs,
        .fps = (float)s.frames_in_window / secs,
        .render_ms_avg = acc_avg_ms(&s.render),
        .render_ms_max = acc_max_ms(&s.render),
        .vsync_wait_ms_avg = acc_avg_ms(&s.vsync_wait),
        .ui_cpu_pct = 100.0f * (float)s.ui_busy_us / (float)elapsed,
        .touch_used_ms_avg = acc_avg_ms(&s.touch_used),
        .touch_flip_ms_avg = acc_avg_ms(&s.touch_flip),
        .touch_flip_ms_max = acc_max_ms(&s.touch_flip),
        .touch_shown_ms_avg = acc_avg_ms(&s.touch_shown),
        .touch_shown_ms_max = acc_max_ms(&s.touch_shown),
        .touch_samples = s.touch_shown.n,
        .frames_total = s.frames_total,
        .vsync_timeouts = s.vsync_timeouts,
    };
    s.last = m;
    s.window_start_us = now_us;
    s.vsync_at_start = vsync_total;
    s.frames_in_window = 0;
    s.ui_busy_us = 0;
    memset(&s.render, 0, sizeof(s.render));
    memset(&s.vsync_wait, 0, sizeof(s.vsync_wait));
    memset(&s.touch_used, 0, sizeof(s.touch_used));
    memset(&s.touch_flip, 0, sizeof(s.touch_flip));
    memset(&s.touch_shown, 0, sizeof(s.touch_shown));
    return true;
}

void flex_metrics_get(flex_metrics_t *out)
{
    if (out) {
        *out = s.last;
    }
}
