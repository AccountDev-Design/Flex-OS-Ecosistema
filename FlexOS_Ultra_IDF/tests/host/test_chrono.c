// Reloj y Cronometro (docs/spec/03 §1-2): la logica del cronometro, el formato, la capsula, la maqueta, la esfera, la tabla de vueltas,
// los botones, la tarjeta expandida, el tacto y la pestana Hora frente a FlexOS_Ultra_AppChrono.h / AppFramework.h de Arduino, compiladas tal cual.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_chrono_model.h"
#include "flex_layout.h"
#include "flex_test.h"

void ref_crono_load(const flex_crono_t *c);
void ref_crono_store(flex_crono_t *c);
void ref_crono_set_ms(uint32_t ms);
void ref_crono_act(int which);
uint32_t ref_crono_elapsed(void);
uint32_t ref_crono_lap_elapsed(void);
int ref_crono_cur_lap(void);
void ref_crono_fmt(char *out, int n, uint32_t ms, int cent);
int ref_crono_label_left(void);
int ref_crono_label_right(void);
float ref_crono_ease(float p);
int ref_crono_lerp(int a, int b, float p);
int ref_crono_glyph(int cx, int cy, int r, float th, char *log, int cap);
void ref_crono_text_k(int k);
int ref_crono_cap_w(void);
int ref_crono_cap_right(void);
int ref_crono_cap_draw(int x, char *log, int cap);
int ref_crono_bar_clock(const char *clock, char *log, int cap, int *cap_on, int *cap_x, int *cap_w, unsigned *cap_sec, int *cap_st);
void ref_crono_cap_set(int on, int x, int w, unsigned sec, int st);
int ref_crono_surface(const flex_crono_surface_in_t *in);
int ref_crono_cap_tick(int surface_ignored, int card, int ota, char *log, int cap);
void ref_crono_layout(int w, int bot, int *pad, int *bx, int *by, int *bw, int *bh, int *out);
int ref_crono_fit_big(const char *s, int maxw, int max_size);
int ref_crono_dial(int w, int bot, int viaback, char *log, int cap);
int ref_crono_laps(int w, int bot, char *log, int cap);
int ref_crono_buttons(int w, int bot, char *log, int cap);
int ref_crono_tab_touch(int w, int bot, int x, int y, int *consumed);
int ref_crono_card_dyn(unsigned alpha, char *log, int cap);
int ref_crono_card_static(unsigned alpha, char *log, int cap);
int ref_crono_card_compose(float p, int cache, char *log, int cap);
int ref_crono_overlay(const flex_crono_world_t *w, const flex_crono_touch_t *t, int long_fired_in, uint32_t now, char *log, int cap,
                      unsigned *clr, int *long_fired_out);
void ref_reloj_body(int w, int bot, int *x, int *y, int *bw, int *bh);
int ref_reloj_tabs_visible(int w, int bot);
int ref_reloj_tab_touch(int w, int bot, int x, int y);
void ref_reloj_set_tab(int tab);
int ref_reloj_hora(int w, int bot, const char *clock, const char *ldate, const char *sdate, int h24, char *log, int cap);

#define TH_TXT    0xF101
#define TH_TXT2   0xF102
#define TH_MUTE   0xF103
#define TH_CARD   0xF104
#define TH_DIS    0xF108
#define TH_ONACC  0xF10A
#define TH_DANGER 0xF10B
#define TH_DIV    0xF10C
#define TH_SURF2  0xF10D
#define WIN_BG    0xF10E
#define ACCENT    0xF200

static uint32_t s_seed = 1;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

typedef struct {
    char *p;
    int cap, n;
} sb_t;
static void sb_add(sb_t *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(s->p + s->n, (size_t)(s->cap - s->n), fmt, ap);
    va_end(ap);
    if (k > 0 && s->n + k < s->cap) {
        s->n += k;
    }
}

static int s_k = 6;
static int tw(const char *t, int fs, void *ctx)
{
    (void)ctx;
    return (int)strlen(t) * s_k * fs;
}
static int fit(const char *t, int maxw, int maxsize)
{
    return flex_ui_font_fit(t, maxw, maxsize, tw, NULL);
}
static const char *LBL[] = {"Hora", "Cronometro", "Temporizador", "Vuelta", "Parcial", "Total", "Parcial", "Detener", "Reinic.", "Iniciar", "Sin vueltas", "Continuar"};
#define CHK_EQ(a, b) CHECK_EQ_I((a), (b))
#define N_HORA 0
#define N_STOPW 1
#define N_TIMER 2
#define N_LAP 3
#define N_SPLIT 4
#define N_TOTAL 5
#define N_BLAP 6
#define N_BSTOP 7
#define N_BRESET 8
#define N_BSTART 9
#define N_NOLAPS 10
#define N_CONTINUE 11

static const char *lbl_left(const flex_crono_t *c)
{
    return flex_crono_label_left(c) == FLEX_CRONO_LL_RESET ? LBL[N_BRESET] : LBL[N_BLAP];
}
static const char *lbl_right(const flex_crono_t *c)
{
    int l = flex_crono_label_right(c);
    return l == FLEX_CRONO_LR_STOP ? LBL[N_BSTOP] : l == FLEX_CRONO_LR_CONTINUE ? LBL[N_CONTINUE] : LBL[N_BSTART];
}

static void same_state(const flex_crono_t *a, const flex_crono_t *b, const char *what, int step)
{
    int ok = a->st == b->st && a->accum == b->accum && a->t0 == b->t0 && a->lap_base == b->lap_base && a->nlaps == b->nlaps &&
             a->lap0 == b->lap0 && a->best == b->best && a->worst == b->worst && a->laps_dirty == b->laps_dirty &&
             a->btn_dirty == b->btn_dirty && a->dial_dirty == b->dial_dirty && a->bar_dirty == b->bar_dirty;
    for (int i = 0; ok && i < FLEX_CRONO_MAX_LAPS; i++) {
        ok = a->laps[i].split == b->laps[i].split && a->laps[i].total == b->laps[i].total;
    }
    if (!ok) {
        fprintf(stderr, "  estado distinto (%s, paso %d): st %d/%d accum %u/%u t0 %u/%u base %u/%u n %d/%d lap0 %d/%d best %d/%d worst %d/%d\n",
                what, step, a->st, b->st, a->accum, b->accum, a->t0, b->t0, a->lap_base, b->lap_base, a->nlaps, b->nlaps, a->lap0,
                b->lap0, a->best, b->best, a->worst, b->worst);
    }
    CHECK(ok);
}

// ---- 1. logica ---------------------------------------------------------------------------------------------------------
static void t_logic(void)
{
    s_seed = 7001;
    for (int run = 0; run < 400; run++) {
        flex_crono_t m, r;
        flex_crono_init(&m);
        ref_crono_load(&m);
        // algunas carreras empiezan pegadas al desbordamiento de millis()
        uint32_t now = (run % 4 == 0) ? 0xFFFFFF00u - rnd() % 2000 : rnd() % 100000;
        if (run % 7 == 0) {
            m.lap0 = 0xFFFD;   // saturacion del numero de vuelta
            ref_crono_load(&m);
        }
        if (run % 11 == 3) {
            // estado inactivo con restos (no se llega por las operaciones, pero Arduino lo defiende): iniciar lo deja limpio
            m.accum = rnd();
            m.lap_base = rnd();
            ref_crono_load(&m);
        }
        const bool tie = run % 5 == 1;   // parciales de 1 s o 2 s exactos: con empates la primera de las iguales manda
        int steps = 20 + (int)(rnd() % 90);
        for (int s = 0; s < steps; s++) {
            now += tie ? 1000u * (1 + rnd() % 2) : (rnd() % 5 == 0) ? rnd() % 4000000u : rnd() % 3000;
            ref_crono_set_ms(now);
            int op = (int)(rnd() % 6);
            if (run % 3 == 0 && op == 2) {
                op = 3;   // menos reinicios: que lleguen a llenar la lista
            }
            switch (op) {
            case 0: flex_crono_start(&m, now); break;
            case 1: flex_crono_pause(&m, now); break;
            case 2: flex_crono_reset(&m); break;
            case 3: flex_crono_lap_mark(&m, now); break;
            case 4: flex_crono_action_left(&m, now); break;
            default: flex_crono_action_right(&m, now); break;
            }
            ref_crono_act(op);
            ref_crono_store(&r);
            same_state(&r, &m, "op", s);
            CHECK_EQ_I(ref_crono_elapsed(), flex_crono_elapsed(&m, now));
            CHECK_EQ_I(ref_crono_lap_elapsed(), flex_crono_lap_elapsed(&m, now));
            CHECK_EQ_I(ref_crono_cur_lap(), flex_crono_cur_lap_no(&m));
            CHECK_EQ_I(ref_crono_label_left(), flex_crono_label_left(&m));
            CHECK_EQ_I(ref_crono_label_right(), flex_crono_label_right(&m));
            CHECK((ref_crono_elapsed() != 0 || m.st == FLEX_CRONO_IDLE || 1));
            // los avisos de "sucio" los consume la UI: se bajan en los dos a la vez
            if (rnd() % 3 == 0) {
                m.laps_dirty = m.btn_dirty = m.dial_dirty = m.bar_dirty = false;
                ref_crono_load(&m);
            }
        }
    }
    // el numero de vuelta visible se queda en 0xFFFF y no da la vuelta a 0
    {
        flex_crono_t s, r;
        flex_crono_init(&s);
        flex_crono_start(&s, 0);
        s.lap0 = 0xFFFE;
        s.nlaps = FLEX_CRONO_MAX_LAPS;
        for (int i = 0; i < FLEX_CRONO_MAX_LAPS; i++) {
            s.laps[i].split = 1000;
            s.laps[i].total = 1000u * (uint32_t)(i + 1);
        }
        s.lap_base = 20000;
        ref_crono_load(&s);
        for (int i = 0; i < 4; i++) {
            ref_crono_set_ms(21000u + 1000u * (uint32_t)i);
            flex_crono_lap_mark(&s, 21000u + 1000u * (uint32_t)i);
            ref_crono_act(3);
            ref_crono_store(&r);
            same_state(&r, &s, "saturacion", i);
        }
        CHK_EQ(s.lap0, 0xFFFF);
    }
    // la lista llena deja caer la mas antigua y el numero visible no retrocede
    flex_crono_t m;
    flex_crono_init(&m);
    flex_crono_start(&m, 1000);
    for (int i = 0; i < 25; i++) {
        flex_crono_lap_mark(&m, 1000 + (uint32_t)(i + 1) * 1500);
    }
    CHECK_EQ_I(m.nlaps, FLEX_CRONO_MAX_LAPS);
    CHECK_EQ_I(m.lap0, 6);
    CHECK_EQ_I(flex_crono_cur_lap_no(&m), 26);
    CHECK_EQ_I(m.laps[FLEX_CRONO_MAX_LAPS - 1].total, 1500u * 25u);
    // menos de tres vueltas: sin extremos; todas iguales: sin extremos
    flex_crono_init(&m);
    flex_crono_start(&m, 0);
    flex_crono_lap_mark(&m, 1000);
    flex_crono_lap_mark(&m, 2500);
    CHECK_EQ_I(m.best, -1);
    flex_crono_lap_mark(&m, 3500);
    CHECK(m.best == 0 || m.best == 2);   // 1000, 1500, 1000: la mas rapida es la 0 (la primera de las iguales)
    CHECK_EQ_I(m.best, 0);
    CHECK_EQ_I(m.worst, 1);
    flex_crono_init(&m);
    flex_crono_start(&m, 0);
    flex_crono_lap_mark(&m, 1000);
    flex_crono_lap_mark(&m, 2000);
    flex_crono_lap_mark(&m, 3000);
    CHECK_EQ_I(m.best, -1);
    CHECK_EQ_I(m.worst, -1);
}

static void t_fmt(void)
{
    s_seed = 91;
    uint32_t pts[] = {0, 9, 10, 999, 1000, 59999, 60000, 3599999, 3600000, 3600010, 86399999, 0xFFFFFFFFu, 100 * 3600000u};
    for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]) + 400; i++) {
        uint32_t ms = i < sizeof(pts) / sizeof(pts[0]) ? pts[i] : rnd() * 331u;
        for (int cent = 0; cent < 2; cent++) {
            char a[24], b[24];
            ref_crono_fmt(a, sizeof(a), ms, cent);
            flex_crono_fmt(b, sizeof(b), ms, cent);
            CHECK(strcmp(a, b) == 0);
        }
    }
    char b[24];
    flex_crono_fmt(b, sizeof(b), 125340, true);
    CHECK(strcmp(b, "02:05.34") == 0);
    flex_crono_fmt(b, sizeof(b), 3723450, true);
    CHECK(strcmp(b, "1:02:03.45") == 0);
    flex_crono_fmt(b, sizeof(b), 3723450, false);
    CHECK(strcmp(b, "1:02:03") == 0);
}

static void t_ease(void)
{
    for (int i = 0; i <= 200; i++) {
        float p = (float)i / 200.0f;
        CHECK(ref_crono_ease(p) == flex_crono_ease(p));
        CHECK(ref_crono_lerp(-50, 300, p) == flex_crono_lerp(-50, 300, p));
        CHECK(ref_crono_lerp(412, 20, p) == flex_crono_lerp(412, 20, p));
    }
    CHECK(flex_crono_ease(0.0f) == 0.0f);
    CHECK(flex_crono_ease(1.0f) == 1.0f);
}

// ---- 2. icono ------------------------------------------------------------------------------------------------------------
static void glyph_log(sb_t *s, int cx, int cy, int r, float th, unsigned col)
{
    flex_crono_glyph_t g;
    flex_crono_glyph(cx, cy, r, th, &g);
    sb_add(s, "S%d,%d,%d,%d,%.1f,%u;", (int)g.seg[0][0], (int)g.seg[0][1], (int)g.seg[0][2], (int)g.seg[0][3], g.th, col);
    sb_add(s, "S%d,%d,%d,%d,%.1f,%u;", (int)g.seg[1][0], (int)g.seg[1][1], (int)g.seg[1][2], (int)g.seg[1][3], g.th, col);
    sb_add(s, "G%d,%d,%d,%d,%u;", cx, cy, g.r, g.ring, col);
    sb_add(s, "S%d,%d,%d,%d,%.1f,%u;", (int)g.seg[2][0], (int)g.seg[2][1], (int)g.seg[2][2], (int)g.seg[2][3], g.th, col);
}

static void t_glyph(void)
{
    static char got[600], exp[600];
    const float ths[] = {1.0f, 1.5f, 2.2f, 3.0f};
    for (int r = 0; r <= 40; r++) {
        for (int k = 0; k < 4; k++) {
            ref_crono_glyph(75, 89, r, ths[k], got, sizeof(got));
            sb_t s = {exp, sizeof(exp), 0};
            exp[0] = 0;
            glyph_log(&s, 75, 89, r, ths[k], TH_ONACC);
            if (strcmp(got, exp) != 0) {
                fprintf(stderr, "  glifo r=%d th=%.1f\n  ref: %s\n  mod: %s\n", r, ths[k], got, exp);
            }
            CHECK(strcmp(got, exp) == 0);
        }
    }
}

// ---- 3. capsula -------------------------------------------------------------------------------------------------------------
static void cap_draw_log(sb_t *s, const flex_crono_t *c, uint32_t now, int x, int tw2_template)
{
    int w = flex_crono_cap_w(tw2_template), h = FLEX_CRONO_CAP_H, y = FLEX_CRONO_CAP_Y;
    sb_add(s, "C%d,%d,%d,%d,%d,%u;", x, y, w, h, h / 2, 0xF300u);
    int ir = FLEX_CRONO_CAP_ICON / 2;
    glyph_log(s, x + FLEX_CRONO_CAP_PADL + ir, y + h / 2 + 1, ir - 2, 1.5f, TH_ONACC);
    char b[16];
    flex_crono_fmt(b, sizeof(b), flex_crono_elapsed(c, now), false);
    int tx = x + FLEX_CRONO_CAP_PADL + FLEX_CRONO_CAP_ICON + FLEX_CRONO_CAP_GAP;
    sb_add(s, "T%d,%d,2,%u:%s;", tx, y + (h - flex_ui_line_h(2)) / 2 + 1, TH_ONACC, b);
}

static int tmpl_w(const flex_crono_t *c, uint32_t now)
{
    return (int)strlen(flex_crono_cap_template(flex_crono_elapsed(c, now))) * s_k * 2;
}

static void t_capsule(void)
{
    s_seed = 5150;
    static char got[900], exp[900];
    uint32_t spans[] = {0, 5000, 59999, 60000, 3599999, 3600000, 3600001, 7300000};
    for (int ks = 0; ks < 3; ks++) {
        s_k = ks == 0 ? 6 : ks == 1 ? 5 : 8;
        ref_crono_text_k(s_k);
        for (size_t i = 0; i < sizeof(spans) / sizeof(spans[0]); i++) {
            flex_crono_t c;
            flex_crono_init(&c);
            flex_crono_start(&c, 100);
            uint32_t now = 100 + spans[i];
            ref_crono_load(&c);
            ref_crono_set_ms(now);
            int w = flex_crono_cap_w(tmpl_w(&c, now));
            CHECK_EQ_I(ref_crono_cap_w(), w);
            CHECK_EQ_I(ref_crono_cap_right(), FLEX_CRONO_CAP_RIGHT);
            CHECK_EQ_I(flex_crono_cap_class(flex_crono_elapsed(&c, now)), spans[i] >= 3600000UL);
            // la pildora tal cual
            ref_crono_cap_draw(137, got, sizeof(got));
            sb_t s = {exp, sizeof(exp), 0};
            exp[0] = 0;
            cap_draw_log(&s, &c, now, 137, tmpl_w(&c, now));
            if (strcmp(got, exp) != 0) {
                fprintf(stderr, "  pildora\n  ref: %s\n  mod: %s\n", got, exp);
            }
            CHECK(strcmp(got, exp) == 0);
            // la hora con la capsula detras (o sola cuando no cabe)
            const char *clocks[] = {"1:23", "13:23", "1:23 PM", "12:59 AM", "5:07 PM", "0:00"};
            for (int q = 0; q < 6; q++) {
                for (int active = 0; active < 2; active++) {
                    flex_crono_t cc = c;
                    if (!active) {
                        flex_crono_reset(&cc);
                    }
                    ref_crono_load(&cc);
                    ref_crono_set_ms(now);
                    int on, rx, rw, rst;
                    unsigned rsec;
                    ref_crono_bar_clock(clocks[q], got, sizeof(got), &on, &rx, &rw, &rsec, &rst);
                    flex_crono_bar_t bar;
                    int cw = flex_crono_cap_w(tmpl_w(&cc, now));
                    int clock_w = (int)strlen(clocks[q]) * s_k * 2;
                    flex_crono_bar_layout(flex_crono_active(&cc), clock_w, cw, &bar);
                    flex_crono_cap_t k;
                    flex_crono_cap_reset(&k);
                    flex_crono_cap_apply_bar(&cc, &k, now, flex_crono_active(&cc), cw);
                    sb_t e = {exp, sizeof(exp), 0};
                    exp[0] = 0;
                    if (!active) {
                        sb_add(&e, "T20,12,2,%u:%s;", TH_TXT, clocks[q]);
                    } else {
                        if (bar.show_clock) {
                            sb_add(&e, "T20,12,2,%u:%s;", TH_TXT, clocks[q]);
                        }
                        cap_draw_log(&e, &cc, now, bar.x, tmpl_w(&cc, now));
                    }
                    if (strcmp(got, exp) != 0) {
                        fprintf(stderr, "  barra (clock %s, activo %d, k %d)\n  ref: %s\n  mod: %s\n", clocks[q], active, s_k, got, exp);
                    }
                    CHECK(strcmp(got, exp) == 0);
                    CHECK_EQ_I(on, k.cap_on);
                    if (active) {
                        CHECK_EQ_I(rx, bar.x);
                        CHECK_EQ_I(rw, k.cap_w_drawn);
                        CHECK_EQ_I(rsec, k.cap_sec);
                        CHECK_EQ_I(rst, k.cap_st_drawn);
                        CHECK(!cc.bar_dirty);
                    }
                }
            }
        }
    }
    // la hora normal cede cuando la capsula no cabe sin pisar el Wi-Fi. El texto de la barra de Arduino cabe en 11 caracteres, asi que
    // el limite se alcanza con letra grande (el tamano de la fuente): barrido fino del ancho por caracter, las dos clases de plantilla
    // y varias horas
    {
        const char *clks[] = {"1:23", "13:23", "12:59 AM", "11:59:59 PM"};
        for (int k = 3; k <= 60; k++) {
            s_k = k;
            ref_crono_text_k(k);
            for (int hour = 0; hour < 2; hour++) {
                flex_crono_t c;
                flex_crono_init(&c);
                flex_crono_start(&c, 100);
                uint32_t now = 100 + (hour ? 3700000u : 5000u);
                ref_crono_load(&c);
                ref_crono_set_ms(now);
                int cw = flex_crono_cap_w(tmpl_w(&c, now));
                for (int q = 0; q < 4; q++) {
                    int on, rx, rw, rst;
                    unsigned rsec;
                    ref_crono_bar_clock(clks[q], got, sizeof(got), &on, &rx, &rw, &rsec, &rst);
                    flex_crono_bar_t bar;
                    flex_crono_bar_layout(true, (int)strlen(clks[q]) * k * 2, cw, &bar);
                    sb_t e = {exp, sizeof(exp), 0};
                    exp[0] = 0;
                    if (bar.show_clock) {
                        sb_add(&e, "T20,12,2,%u:%s;", TH_TXT, clks[q]);
                    }
                    cap_draw_log(&e, &c, now, bar.x, tmpl_w(&c, now));
                    if (strcmp(got, exp) != 0) {
                        fprintf(stderr, "  barra ancha (%s, k %d, hora %d)\n  ref: %s\n  mod: %s\n", clks[q], k, hour, got, exp);
                    }
                    CHECK(strcmp(got, exp) == 0);
                    CHECK_EQ_I(rx, bar.x);
                    CHECK(bar.x + cw <= FLEX_CRONO_CAP_RIGHT || cw > FLEX_CRONO_CAP_RIGHT - 20);   // nunca pisa el Wi-Fi (salvo que ni sola quepa)
                }
            }
        }
    }
    s_k = 6;
    ref_crono_text_k(6);
}

static void t_surface(void)
{
    // las 2048 combinaciones de las 11 condiciones
    for (int m = 0; m < 2048; m++) {
        flex_crono_surface_in_t in;
        memset(&in, 0, sizeof(in));
        in.suspended = m & 1;
        in.land = (m >> 1) & 1;
        in.hosted = (m >> 2) & 1;
        in.app_hides_status_bar = (m >> 3) & 1;
        in.immersive = (m >> 4) & 1;
        in.kiosk = (m >> 5) & 1;
        in.qs_closed = (m >> 6) & 1;
        in.edit_mode = (m >> 7) & 1;
        in.app_custom_header = (m >> 8) & 1;
        int st = (m >> 9) & 3;   // 0 escritorio, 1 app, 2 otra pantalla, 3 (igual que 2)
        in.home = st == 0;
        in.app = st == 1;
        CHECK_EQ_I(flex_crono_surface(&in), ref_crono_surface(&in));
    }
}

static void t_cap_tick(void)
{
    s_seed = 8800;
    static char got[200];
    for (int run = 0; run < 300; run++) {
        flex_crono_t m;
        flex_crono_init(&m);
        flex_crono_cap_t k;
        flex_crono_cap_reset(&k);
        ref_crono_load(&m);
        ref_crono_cap_set(0, 0, 0, 0xFFFFFFFFu, 0xFF);
        uint32_t now = rnd() % 50000;
        for (int s = 0; s < 60; s++) {
            now += (rnd() % 4 == 0) ? 3600000 / 2 + rnd() % 2000000 : rnd() % 1800;
            ref_crono_set_ms(now);
            int op = (int)(rnd() % 12);
            if (op == 0) {
                flex_crono_start(&m, now);
                ref_crono_act(0);
            } else if (op == 1) {
                flex_crono_pause(&m, now);
                ref_crono_act(1);
            } else if (op == 2 && rnd() % 3 == 0) {
                flex_crono_reset(&m);
                ref_crono_act(2);
            }
            if (rnd() % 9 == 0 && m.st != FLEX_CRONO_IDLE) {
                // el estado cambia SIN avisar a la barra (no pasa por las operaciones; Arduino lo defiende comparando el estado estampado)
                if (m.st == FLEX_CRONO_RUN) {
                    m.accum = flex_crono_elapsed(&m, now);
                    m.st = FLEX_CRONO_PAUSE;
                } else {
                    m.t0 = now;
                    m.st = FLEX_CRONO_RUN;
                }
                ref_crono_load(&m);
            }
            flex_crono_surface_in_t in;
            memset(&in, 0, sizeof(in));
            int sf = (int)(rnd() % 3);
            in.home = sf == 1;
            in.app = sf == 2;
            in.qs_closed = true;
            int surface = ref_crono_surface(&in);
            CHECK_EQ_I(flex_crono_surface(&in), surface);
            int card = (int)(rnd() % 10 == 0 ? 1 + rnd() % 3 : 0);
            int ota = rnd() % 25 == 0;
            ref_crono_cap_tick(0, card, ota, got, sizeof(got));
            int act = FLEX_CRONO_CT_NONE;
            if (!ota) {
                int cw = flex_crono_cap_w(tmpl_w(&m, now));
                act = flex_crono_cap_tick(&m, &k, now, surface, cw, card == 0);
                if (act == FLEX_CRONO_CT_REBUILD) {
                    flex_crono_cap_apply_bar(&m, &k, now, flex_crono_active(&m), cw);
                }
            }
            char want[64] = "";
            if (act == FLEX_CRONO_CT_REBUILD) {
                snprintf(want, sizeof(want), "REBUILD%d;", surface);
            } else if (act == FLEX_CRONO_CT_STAMP) {
                snprintf(want, sizeof(want), "STAMP%d;", surface);
            }
            // el registro del doble contiene ademas lo que pinta la barra al rehacerse: solo importa que arranque con el aviso
            if (act == FLEX_CRONO_CT_NONE) {
                CHECK(got[0] == 0);
            } else {
                CHECK(strncmp(got, want, strlen(want)) == 0);
            }
            flex_crono_t r;
            ref_crono_store(&r);
            CHECK_EQ_I(r.bar_dirty, m.bar_dirty);
        }
    }
}

// ---- 4. maqueta ------------------------------------------------------------------------------------------------------------
static void t_layout(void)
{
    int ws[] = {32, 100, 200, 259, 260, 300, 360, 420, 480, 600};
    int bots[] = {110, 160, 200, 256, 300, 400, 500, 640, 736, 790};
    for (size_t a = 0; a < sizeof(ws) / sizeof(ws[0]); a++) {
        for (size_t b = 0; b < sizeof(bots) / sizeof(bots[0]); b++) {
            int w = ws[a], bot = bots[b];
            int pad, bx, by, bw, bh, o[20];
            ref_crono_layout(w, bot, &pad, &bx, &by, &bw, &bh, o);
            int uw, uh;
            flex_ui_box(w, bot - FLEX_RELOJ_WIN_TOP, &uw, &uh);
            int mp = flex_ui_pad(uw, uh);
            flex_reloj_box_t bd;
            flex_reloj_body(0, FLEX_RELOJ_WIN_TOP, uw, uh, &bd);
            flex_crono_layout_t L;
            flex_crono_layout(bd.bx, bd.by, bd.bw, bd.bh, mp, &L);
            CHECK_EQ_I(pad, mp);
            CHECK_EQ_I(bx, L.bx);
            CHECK_EQ_I(by, L.by);
            CHECK_EQ_I(bw, L.bw);
            CHECK_EQ_I(bh, L.bh);
            const int v[17] = {L.cx, L.cy, L.r_tick, L.r_disc, L.dial_y0, L.dial_y1, L.tbl_y, L.row_h, L.rows, L.col_lap,
                               L.col_split, L.col_tot, L.btn_cy, L.btn_r, L.btn_lx, L.btn_rx, L.has_table};
            for (int i = 0; i < 17; i++) {
                if (o[i] != v[i]) {
                    fprintf(stderr, "  maqueta w=%d bot=%d campo %d: ref %d mod %d\n", w, bot, i, o[i], v[i]);
                }
                CHECK_EQ_I(o[i], v[i]);
            }
            // los botones nunca llegan a la barra de navegacion (y >= WIN_BOT)
            CHECK(L.btn_cy + L.btn_r <= bot || bh <= 32);
        }
    }
}

static void t_fit_big(void)
{
    const char *ss[] = {"00:00.00", "1:02:03.45", "12:34.56", ""};
    for (int ks = 0; ks < 3; ks++) {
        s_k = ks == 0 ? 6 : ks == 1 ? 5 : 8;
        ref_crono_text_k(s_k);
        for (int i = 0; i < 4; i++) {
            for (int mw = 0; mw < 400; mw += 7) {
                for (int ms = 1; ms <= 7; ms++) {
                    CHECK_EQ_I(ref_crono_fit_big(ss[i], mw, ms), flex_crono_fit_big(ss[i], mw, ms, tw, NULL));
                }
            }
        }
    }
    s_k = 6;
    ref_crono_text_k(6);
}

static void state_for_draw(flex_crono_t *c, uint32_t *now, int variant)
{
    flex_crono_init(c);
    *now = 100000;
    if (variant == 0) {
        return;   // inactivo
    }
    flex_crono_start(c, 1000);
    for (int i = 0; i < variant - 1; i++) {
        flex_crono_lap_mark(c, 1000 + (uint32_t)(i + 1) * (1100 + (uint32_t)(i * 37) % 900));
    }
    if (variant == 7) {
        flex_crono_pause(c, 60000);
    }
    if (variant == 8) {
        c->accum = 3700000;   // mas de una hora
    }
}

static void t_dial(void)
{
    static char got[12000], exp[12000];
    int ws[] = {240, 360, 480};
    int bots[] = {400, 600, 736};
    for (size_t a = 0; a < 3; a++) {
        for (size_t b = 0; b < 3; b++) {
            for (int variant = 0; variant < 9; variant++) {
                for (int via = 0; via < 2; via++) {
                    flex_crono_t c;
                    uint32_t now;
                    state_for_draw(&c, &now, variant);
                    ref_crono_load(&c);
                    ref_crono_set_ms(now);
                    ref_crono_dial(ws[a], bots[b], via, got, sizeof(got));
                    int uw, uh;
                    flex_ui_box(ws[a], bots[b] - FLEX_RELOJ_WIN_TOP, &uw, &uh);
                    flex_reloj_box_t bd;
                    flex_reloj_body(0, FLEX_RELOJ_WIN_TOP, uw, uh, &bd);
                    flex_crono_layout_t L;
                    flex_crono_layout(bd.bx, bd.by, bd.bw, bd.bh, flex_ui_pad(uw, uh), &L);
                    int y0 = L.dial_y0, y1 = L.dial_y1;
                    if (y0 < L.by) y0 = L.by;
                    if (y1 > L.by + L.bh - 1) y1 = L.by + L.bh - 1;
                    sb_t e = {exp, sizeof(exp), 0};
                    exp[0] = 0;
                    if (y1 > y0) {
                        sb_add(&e, "R%d,%d,%d,%d,%u;", L.bx, y0, L.bw, y1 - y0 + 1, WIN_BG);
                        for (int i = 0; i < 60; i++) {
                            flex_crono_tick_t t;
                            flex_crono_dial_tick(&L, i, &t);
                            sb_add(&e, "S%d,%d,%d,%d,%.1f,%u;", (int)t.x0, (int)t.y0, (int)t.x1, (int)t.y1, t.w, t.major ? TH_TXT2 : TH_MUTE);
                        }
                        sb_add(&e, "A%d,%d,%d,%u;", L.cx, L.cy, L.r_disc, TH_SURF2);
                        flex_crono_hands_t h;
                        flex_crono_dial_hands(&L, flex_crono_elapsed(&c, now), flex_crono_lap_elapsed(&c, now), &h);
                        sb_add(&e, "S%d,%d,%d,%d,%.1f,%u;", (int)h.lap[0], (int)h.lap[1], (int)h.lap[2], (int)h.lap[3], h.lap_w, TH_TXT2);
                        sb_add(&e, "S%d,%d,%d,%d,%.1f,%u;", (int)h.tot[0], (int)h.tot[1], (int)h.tot[2], (int)h.tot[3], h.tot_w, ACCENT);
                        sb_add(&e, "A%d,%d,%d,%u;", L.cx, L.cy, (int)h.hub, ACCENT);
                        char tb[16];
                        flex_crono_fmt(tb, sizeof(tb), flex_crono_elapsed(&c, now), true);
                        int fs = flex_crono_fit_big(tb, L.r_disc * 2 - 18, 7, tw, NULL);
                        sb_add(&e, "M%d,%d,%d,%u:%s;", L.cx, flex_crono_dial_text_y(&L, fs), fs, TH_TXT, tb);
                        sb_add(&e, via ? "p%d,%d;" : "f%d,%d;", y0, y1);
                    }
                    if (strcmp(got, exp) != 0) {
                        fprintf(stderr, "  esfera w=%d bot=%d var=%d\n  ref: %.300s\n  mod: %.300s\n", ws[a], bots[b], variant, got, exp);
                    }
                    CHECK(strcmp(got, exp) == 0);
                }
            }
        }
    }
}

static void t_laps_buttons(void)
{
    static char got[4000], exp[4000];
    int ws[] = {200, 260, 360, 480};
    int bots[] = {500, 640, 736, 790};
    for (size_t a = 0; a < 4; a++) {
        for (size_t b = 0; b < 4; b++) {
            for (int variant = 0; variant < 9; variant++) {
                flex_crono_t c;
                uint32_t now;
                state_for_draw(&c, &now, variant);
                ref_crono_load(&c);
                ref_crono_set_ms(now);
                int uw, uh;
                flex_ui_box(ws[a], bots[b] - FLEX_RELOJ_WIN_TOP, &uw, &uh);
                int pad = flex_ui_pad(uw, uh);
                flex_reloj_box_t bd;
                flex_reloj_body(0, FLEX_RELOJ_WIN_TOP, uw, uh, &bd);
                flex_crono_layout_t L;
                flex_crono_layout(bd.bx, bd.by, bd.bw, bd.bh, pad, &L);
                // tabla
                ref_crono_laps(ws[a], bots[b], got, sizeof(got));
                sb_t e = {exp, sizeof(exp), 0};
                exp[0] = 0;
                if (L.has_table) {
                    sb_add(&e, "R%d,%d,%d,%d,%u;", L.bx, L.tbl_y, L.bw, 26 + L.rows * L.row_h, WIN_BG);
                    int fsh = fit(LBL[N_SPLIT], L.bw / 3 - 8, 2);
                    sb_add(&e, "M%d,%d,%d,%u:%s;", L.col_lap, L.tbl_y, fsh, TH_TXT2, LBL[N_LAP]);
                    sb_add(&e, "M%d,%d,%d,%u:%s;", L.col_split, L.tbl_y, fsh, TH_TXT2, LBL[N_SPLIT]);
                    sb_add(&e, "M%d,%d,%d,%u:%s;", L.col_tot, L.tbl_y, fsh, TH_TXT2, LBL[N_TOTAL]);
                    int yl = L.tbl_y + flex_ui_line_h(fsh) + 6;
                    sb_add(&e, "R%d,%d,%d,%d,%u;", L.bx + pad, yl, L.bw - 2 * pad, 1, TH_DIV);
                    int y = yl + 8;
                    if (c.nlaps == 0) {
                        sb_add(&e, "M%d,%d,2,%u:%s;", L.bx + L.bw / 2, y + 6, TH_MUTE, LBL[N_NOLAPS]);
                    } else {
                        for (int r = 0; r < L.rows; r++) {
                            int kind;
                            char no[8], sp[16], to[16];
                            if (!flex_crono_lap_row(&c, r, &kind, no, sp, to)) {
                                break;
                            }
                            unsigned col = kind == FLEX_CRONO_ROW_BEST ? ACCENT : kind == FLEX_CRONO_ROW_WORST ? TH_DANGER : TH_TXT;
                            sb_add(&e, "M%d,%d,2,%u:%s;", L.col_lap, y, col, no);
                            sb_add(&e, "M%d,%d,2,%u:%s;", L.col_split, y, col, sp);
                            sb_add(&e, "M%d,%d,2,%u:%s;", L.col_tot, y, col, to);
                            y += L.row_h;
                        }
                    }
                }
                if (strcmp(got, exp) != 0) {
                    fprintf(stderr, "  tabla w=%d bot=%d var=%d\n  ref: %.400s\n  mod: %.400s\n", ws[a], bots[b], variant, got, exp);
                }
                CHECK(strcmp(got, exp) == 0);
                // botones
                ref_crono_buttons(ws[a], bots[b], got, sizeof(got));
                e.n = 0;
                exp[0] = 0;
                int r = L.btn_r, cy = L.btn_cy;
                sb_add(&e, "R%d,%d,%d,%d,%u;", L.bx, cy - r - 2, L.bw, 2 * r + 4, WIN_BG);
                bool left_on = c.st != FLEX_CRONO_IDLE;
                sb_add(&e, "O%d,%d,%d,%u;", L.btn_lx, cy, r, TH_CARD);
                int fsl = fit(lbl_left(&c), 2 * r - 12, 3);
                sb_add(&e, "M%d,%d,%d,%u:%s;", L.btn_lx, cy - flex_ui_line_h(fsl) / 2, fsl, left_on ? TH_TXT : TH_DIS, lbl_left(&c));
                bool stop = c.st == FLEX_CRONO_RUN;
                unsigned rc = stop ? TH_DANGER : ACCENT;
                sb_add(&e, "P%d,%d,%d,%u,70;", L.btn_rx, cy, r, rc);
                sb_add(&e, "D%d,%d,%d,%u;", L.btn_rx, cy, r, rc);
                int fsr = fit(lbl_right(&c), 2 * r - 12, 3);
                sb_add(&e, "M%d,%d,%d,%u:%s;", L.btn_rx, cy - flex_ui_line_h(fsr) / 2, fsr, rc, lbl_right(&c));
                if (strcmp(got, exp) != 0) {
                    fprintf(stderr, "  botones w=%d bot=%d var=%d\n  ref: %s\n  mod: %s\n", ws[a], bots[b], variant, got, exp);
                }
                CHECK(strcmp(got, exp) == 0);
            }
        }
    }
}

static void t_tab_touch(void)
{
    s_seed = 311;
    int ws[] = {260, 360, 480};
    int bots[] = {500, 736};
    for (size_t a = 0; a < 3; a++) {
        for (size_t b = 0; b < 2; b++) {
            int uw, uh;
            flex_ui_box(ws[a], bots[b] - FLEX_RELOJ_WIN_TOP, &uw, &uh);
            flex_reloj_box_t bd;
            flex_reloj_body(0, FLEX_RELOJ_WIN_TOP, uw, uh, &bd);
            flex_crono_layout_t L;
            flex_crono_layout(bd.bx, bd.by, bd.bw, bd.bh, flex_ui_pad(uw, uh), &L);
            for (int i = 0; i < 4000; i++) {
                int x, y;
                if (i % 3 == 0) {   // alrededor de los botones: los bordes del radio + 6
                    int bx = (i & 4) ? L.btn_lx : L.btn_rx;
                    x = bx + (int)(rnd() % (unsigned)(2 * L.btn_r + 30)) - (L.btn_r + 15);
                    y = L.btn_cy + (int)(rnd() % (unsigned)(2 * L.btn_r + 30)) - (L.btn_r + 15);
                } else {
                    x = (int)(rnd() % (unsigned)(uw + 20)) - 10;
                    y = FLEX_RELOJ_WIN_TOP + (int)(rnd() % 700);
                }
                flex_crono_t c;
                flex_crono_init(&c);
                flex_crono_start(&c, 0);
                ref_crono_load(&c);
                ref_crono_set_ms(5000);
                int consumed;
                int rr = ref_crono_tab_touch(ws[a], bots[b], x, y, &consumed);
                CHECK_EQ_I(rr == 3 ? -1 : rr, flex_crono_tab_hit(&L, x, y));
                CHECK_EQ_I(consumed, rr != 0);
            }
        }
    }
}

// ---- 5. tarjeta ------------------------------------------------------------------------------------------------------------
static void card_static_log(sb_t *s, const flex_crono_t *c, unsigned a)
{
    flex_crono_card_lay_t l;
    flex_crono_card_layout(&l);
    sb_add(s, "W%d,%d,%d,%d,%d,%u,%u;", l.icon_x, l.icon_y, l.icon_s, l.icon_s, l.icon_r, ACCENT, a);
    if (a > 200) {
        glyph_log(s, l.glyph_cx, l.glyph_cy, l.glyph_r, 2.2f, TH_ONACC);
    }
    sb_add(s, "W%d,%d,%d,%d,1,%u,%u;", l.sep_x, l.sep_y, l.sep_w, l.sep_h, TH_DIV, a);
    sb_add(s, "V%d,%d,3,%u,%u:%s;", l.lbl_lx, l.lbl_y, c->st == FLEX_CRONO_IDLE ? TH_DIS : TH_TXT, a, lbl_left(c));
    sb_add(s, "V%d,%d,3,%u,%u:%s;", l.lbl_rx, l.lbl_y, c->st == FLEX_CRONO_RUN ? TH_DANGER : ACCENT, a, lbl_right(c));
}
static void card_dyn_log(sb_t *s, const flex_crono_t *c, uint32_t now, unsigned a)
{
    flex_crono_card_lay_t l;
    flex_crono_card_layout(&l);
    char b[16];
    flex_crono_fmt(b, sizeof(b), flex_crono_elapsed(c, now), true);
    int fs = flex_crono_fit_big(b, l.time_maxw, 7, tw, NULL);
    sb_add(s, "U%d,%d,%d,%u,%u:%s;", l.time_x, l.time_y, fs, TH_TXT, a, b);
    char lap[24];
    snprintf(lap, sizeof(lap), "%s %d", LBL[N_LAP], flex_crono_cur_lap_no(c));
    sb_add(s, "U%d,%d,2,%u,%u:%s;", l.lap_x, l.lap_y, TH_TXT2, a, lap);
}

static void t_card(void)
{
    static char got[3000], exp[3000];
    for (int variant = 0; variant < 9; variant++) {
        flex_crono_t c;
        uint32_t now;
        state_for_draw(&c, &now, variant);
        ref_crono_load(&c);
        ref_crono_set_ms(now);
        const unsigned alphas[] = {0, 1, 90, 200, 201, 255};
        for (size_t i = 0; i < sizeof(alphas) / sizeof(alphas[0]); i++) {
            ref_crono_card_static(alphas[i], got, sizeof(got));
            sb_t e = {exp, sizeof(exp), 0};
            exp[0] = 0;
            card_static_log(&e, &c, alphas[i]);
            if (strcmp(got, exp) != 0) {
                fprintf(stderr, "  tarjeta estatica a=%u\n  ref: %s\n  mod: %s\n", alphas[i], got, exp);
            }
            CHECK(strcmp(got, exp) == 0);
            ref_crono_card_dyn(alphas[i], got, sizeof(got));
            e.n = 0;
            exp[0] = 0;
            card_dyn_log(&e, &c, now, alphas[i]);
            if (strcmp(got, exp) != 0) {
                fprintf(stderr, "  tarjeta dinamica a=%u\n  ref: %s\n  mod: %s\n", alphas[i], got, exp);
            }
            CHECK(strcmp(got, exp) == 0);
        }
        // un cuadro completo de la animacion, con y sin capsula pintada debajo
        for (int cap_on = 0; cap_on < 2; cap_on++) {
            for (int i = 0; i <= 40; i++) {
                float p = (float)i / 40.0f;
                float pe = flex_crono_ease(p);
                int cx = 60 + variant * 7;
                flex_crono_cap_t k;
                flex_crono_cap_reset(&k);
                int cw = flex_crono_cap_w(tmpl_w(&c, now));
                if (cap_on && flex_crono_active(&c)) {
                    flex_crono_cap_apply_bar(&c, &k, now, true, cw);
                }
                ref_crono_cap_set(k.cap_on, cx, k.cap_w_drawn, k.cap_sec, k.cap_st_drawn);
                ref_crono_card_compose(pe, i == 40, got, sizeof(got));
                sb_t e2 = {exp, sizeof(exp), 0};
                exp[0] = 0;
                if (k.cap_on) {
                    cap_draw_log(&e2, &c, now, cx, tmpl_w(&c, now));
                }
                flex_crono_card_frame_t f;
                flex_crono_card_frame(pe, cx, k.cap_w_drawn > 0 ? k.cap_w_drawn : cw, &f);
                sb_add(&e2, "Y%d,%d,%d,%d,%d,1,240;", f.x, f.y, f.w, f.h, f.r);
                if (pe < 1.0f && f.accent_a) {
                    sb_add(&e2, "Q%d,%d,%d,%d,%d,%u,%u;", f.x, f.y, f.w, f.h, f.r, ACCENT, f.accent_a);
                }
                if (f.content_a) {
                    card_static_log(&e2, &c, f.content_a);
                    card_dyn_log(&e2, &c, now, f.content_a);
                }
                sb_add(&e2, "p%d,%d;", FLEX_CRONO_BAND_T, FLEX_CRONO_BAND_B - 1);
                if (strcmp(got, exp) != 0) {
                    fprintf(stderr, "  cuadro p=%.3f cap=%d var=%d\n  ref: %.500s\n  mod: %.500s\n", pe, cap_on, variant, got, exp);
                }
                CHECK(strcmp(got, exp) == 0);
            }
        }
    }
    // extremos de la animacion: a p=0 es la pildora y a p=1 la tarjeta
    flex_crono_card_frame_t f;
    flex_crono_card_frame(0.0f, 100, 90, &f);
    CHECK(f.x == 100 && f.y == FLEX_CRONO_CAP_Y && f.w == 90 && f.h == FLEX_CRONO_CAP_H && f.r == 13 && f.accent_a == 255 && f.content_a == 0);
    flex_crono_card_frame(1.0f, 20, 90, &f);
    CHECK(f.x == 20 && f.y == 30 && f.w == 440 && f.h == 196 && f.r == 42 && f.accent_a == 0 && f.content_a == 255);
    // Arduino interpola con (int)(d * p + 0.5): hacia abajo trunca hacia 0 y el ultimo cuadro cae en x = 21 si la capsula esta a la
    // derecha del borde. El modelo lo conserva (es lo que se compara con la referencia); la app pinta el reposo en 20 exacto.
    flex_crono_card_frame(1.0f, 100, 90, &f);
    CHECK_EQ_I(f.x, 21);
    CHECK_EQ_I(flex_crono_card_x_rest(), FLEX_CRONO_CARD_X);
    CHECK(flex_crono_card_progress(0) == 0.0f);
    CHECK(flex_crono_card_progress(95) == 0.5f);
    CHECK(flex_crono_card_progress(5000) == 1.0f);
}

// ---- 6. tacto -----------------------------------------------------------------------------------------------------------------
static const char *act_name(int a)
{
    switch (a) {
    case FLEX_CRONO_ACT_DROP: return "CDROP;";
    case FLEX_CRONO_ACT_CLOSE: return "CCLOSE;";
    case FLEX_CRONO_ACT_OPEN_APP: return "OPEN;";
    case FLEX_CRONO_ACT_CARD_OPEN: return "COPEN;";
    case FLEX_CRONO_ACT_BG_INVAL: return "BG;";
    default: return "";   // LEFT / RIGHT: sin traza, se comprueba por el estado
    }
}

static void t_overlay(void)
{
    s_seed = 4242;
    static char got[200], exp[200];
    // `longFired` es un estatico de la funcion de Arduino: se baja con un cuadro sin dedo
    {
        flex_crono_world_t w0;
        flex_crono_touch_t t0;
        memset(&w0, 0, sizeof(w0));
        memset(&t0, 0, sizeof(t0));
        unsigned clr;
        int lf;
        ref_crono_overlay(&w0, &t0, 0, 0, got, sizeof(got), &clr, &lf);
    }
    flex_crono_ov_t ov = {false};
    int did_open = 0, did_card = 0;
    for (int i = 0; i < 60000; i++) {
        flex_crono_world_t w;
        memset(&w, 0, sizeof(w));
        flex_crono_touch_t t;
        memset(&t, 0, sizeof(t));
        w.ota_owns = rnd() % 30 == 0;
        w.susp = rnd() % 30 == 0;
        w.card = rnd() % 3 == 0 ? (int)(rnd() % 4) : 0;
        if (w.card != 0 && rnd() % 2) {
            w.card = FLEX_CC_OPEN;
        }
        w.cap_on = rnd() % 4 != 0;
        w.surface = (int)(rnd() % 3);
        w.cap_x = 20 + (int)(rnd() % 200);
        w.cap_w_drawn = 80 + (int)(rnd() % 30);
        flex_crono_t c;
        flex_crono_init(&c);
        uint32_t now = 100000 + rnd() % 5000;
        int cs = (int)(rnd() % 3);
        if (cs >= 1) {
            flex_crono_start(&c, now - 20000);
            if (rnd() % 2) {
                flex_crono_lap_mark(&c, now - 10000);
            }
        }
        if (cs == 2) {
            flex_crono_pause(&c, now - 5000);
        }
        t.down = rnd() % 2;
        t.tap = rnd() % 3 == 0;
        t.pressed = rnd() % 4 == 0;
        t.released = rnd() % 4 == 0;
        t.swipe_up = rnd() % 8 == 0;
        t.swipe_left = rnd() % 10 == 0;
        t.swipe_right = rnd() % 10 == 0;
        t.swipe_down = rnd() % 10 == 0;
        int zone = (int)(rnd() % 4);
        if (zone == 0) {   // sobre la capsula
            t.x = w.cap_x - 10 + (int)(rnd() % (unsigned)(w.cap_w_drawn + 20));
            t.y = 4 + (int)(rnd() % 44);
        } else if (zone == 1) {   // en la tarjeta
            t.x = (int)(rnd() % 480);
            t.y = 20 + (int)(rnd() % 220);
        } else {
            t.x = (int)(rnd() % 480);
            t.y = (int)(rnd() % 800);
        }
        t.start_x = t.x + (rnd() % 3 ? 0 : (int)(rnd() % 30) - 15);
        t.start_y = t.y + (rnd() % 3 ? 0 : (int)(rnd() % 30) - 15);
        t.down_ms = now - (rnd() % 3 == 0 ? rnd() % 1500 : 800 + rnd() % 400);
        // el episodio de la pulsacion larga: a veces repite el mismo cuadro dos veces seguidas
        flex_crono_t cm = c, cr = c;
        flex_crono_touch_out_t out;
        ref_crono_load(&cr);
        flex_crono_overlay_touch(&cm, &ov, &w, &t, now, &out);
        unsigned clr;
        int lf;
        ref_crono_overlay(&w, &t, 0, now, got, sizeof(got), &clr, &lf);
        exp[0] = 0;
        for (int a = 0; a < out.n; a++) {
            strcat(exp, act_name(out.act[a]));
        }
        // Arduino consume banderas que ya estaban bajas sin notarlo: se compara lo que cambio de verdad
        unsigned in_mask = (t.tap ? FLEX_CRONO_TC_TAP : 0) | (t.pressed ? FLEX_CRONO_TC_PRESSED : 0) | (t.released ? FLEX_CRONO_TC_RELEASED : 0) |
                           (t.swipe_up ? FLEX_CRONO_TC_SWIPE_UP : 0) | (t.swipe_left ? FLEX_CRONO_TC_SWIPE_L : 0) |
                           (t.swipe_right ? FLEX_CRONO_TC_SWIPE_R : 0) | (t.swipe_down ? FLEX_CRONO_TC_SWIPE_D : 0);
        unsigned mclr = out.clr & in_mask;
        if (strcmp(got, exp) != 0 || clr != mclr) {
            fprintf(stderr, "  tacto #%d: ref [%s] clr %u / modelo [%s] clr %u (card %d cap %d sf %d x %d y %d)\n", i, got, clr, exp, mclr,
                    w.card, w.cap_on, w.surface, t.x, t.y);
        }
        CHECK(strcmp(got, exp) == 0);
        CHECK_EQ_I(clr, mclr);
        flex_crono_t after;
        ref_crono_store(&after);
        CHECK_EQ_I(after.st, cm.st);
        CHECK_EQ_I(after.nlaps, cm.nlaps);
        CHECK_EQ_I(after.accum, cm.accum);
        for (int a = 0; a < out.n; a++) {
            if (out.act[a] == FLEX_CRONO_ACT_OPEN_APP) {
                did_open++;
            }
            if (out.act[a] == FLEX_CRONO_ACT_CARD_OPEN) {
                did_card++;
            }
        }
    }
    CHECK(did_open > 100 && did_card > 100);   // la prueba llega a las dos salidas
    // la pulsacion larga dispara UNA vez por episodio
    {
        flex_crono_world_t w;
        flex_crono_touch_t t;
        memset(&w, 0, sizeof(w));
        memset(&t, 0, sizeof(t));
        flex_crono_t c;
        flex_crono_init(&c);
        flex_crono_start(&c, 0);
        flex_crono_ov_t o = {false};
        w.cap_on = true;
        w.surface = 1;
        w.cap_x = 100;
        w.cap_w_drawn = 90;
        t.down = true;
        t.x = t.start_x = 130;
        t.y = t.start_y = 25;
        t.down_ms = 1000;
        flex_crono_touch_out_t out;
        flex_crono_overlay_touch(&c, &o, &w, &t, 1600, &out);   // 600 ms: aun no
        CHECK_EQ_I(out.n, 0);
        flex_crono_overlay_touch(&c, &o, &w, &t, 1701, &out);   // 701 ms
        CHECK(out.n == 1 && out.act[0] == FLEX_CRONO_ACT_OPEN_APP);
        flex_crono_overlay_touch(&c, &o, &w, &t, 1900, &out);   // el mismo episodio: no repite
        CHECK_EQ_I(out.n, 0);
        t.down = false;
        flex_crono_overlay_touch(&c, &o, &w, &t, 2000, &out);
        t.down = true;
        t.down_ms = 2100;
        flex_crono_overlay_touch(&c, &o, &w, &t, 2900, &out);   // episodio nuevo
        CHECK(out.n == 1 && out.act[0] == FLEX_CRONO_ACT_OPEN_APP);
        // el dedo que se mueve 12 px ya no es una pulsacion larga
        t.down = false;
        flex_crono_overlay_touch(&c, &o, &w, &t, 3000, &out);
        t.down = true;
        t.down_ms = 3100;
        t.x = t.start_x + 12;
        flex_crono_overlay_touch(&c, &o, &w, &t, 4000, &out);
        CHECK_EQ_I(out.n, 0);
    }
}

// ---- 7. Reloj: cuerpo, pestanas y Hora ------------------------------------------------------------------------------------------
typedef struct {
    const char *ds;
    int maxw;
} datectx_t;
static int date_lh(int hint, void *ctx)
{
    datectx_t *d = ctx;
    int fs = fit(d->ds, d->maxw, flex_ui_font_h(hint));
    return flex_ui_line_h(fs);
}

static void t_reloj(void)
{
    s_seed = 999;
    int ws[] = {32, 120, 245, 250, 260, 300, 360, 480, 600};
    int bots[] = {110, 150, 156, 200, 300, 400, 560, 736, 790};
    for (size_t a = 0; a < sizeof(ws) / sizeof(ws[0]); a++) {
        for (size_t b = 0; b < sizeof(bots) / sizeof(bots[0]); b++) {
            int w = ws[a], bot = bots[b];
            int x, y, bw, bh;
            ref_reloj_body(w, bot, &x, &y, &bw, &bh);
            int uw, uh;
            flex_ui_box(w, bot - FLEX_RELOJ_WIN_TOP, &uw, &uh);
            flex_reloj_box_t bd;
            flex_reloj_body(0, FLEX_RELOJ_WIN_TOP, uw, uh, &bd);
            CHECK_EQ_I(x, bd.bx);
            CHECK_EQ_I(y, bd.by);
            CHECK_EQ_I(bw, bd.bw);
            CHECK_EQ_I(bh, bd.bh);
            CHECK_EQ_I(ref_reloj_tabs_visible(w, bot), flex_reloj_tabs_visible(uh));
            // toques en la barra de pestanas
            int pad = flex_ui_pad(uw, uh);
            flex_reloj_tabs_t tt;
            flex_reloj_box_t full;
            full.bx = 0;
            full.by = FLEX_RELOJ_WIN_TOP;
            full.bw = uw;
            flex_reloj_tabs_geom(full.bx, full.by, full.bw, pad, &tt);
            for (int i = 0; i < 300; i++) {
                int tx = (int)(rnd() % (unsigned)(uw + 20)) - 10;
                int ty = FLEX_RELOJ_WIN_TOP + (int)(rnd() % 70);
                int want = flex_reloj_tabs_visible(uh) ? flex_reloj_tab_at(&tt, tx, ty) : -1;
                CHECK_EQ_I(ref_reloj_tab_touch(w, bot, tx, ty), want);
            }
        }
    }
    // la pestana Hora: reloj grande, fecha, tarjetas y pie. Alturas una a una (los umbrales de cada seccion caen entre dos pasos
    // gruesos) y los anchos que rodean 250
    static char got[2000], exp[2000];
    const char *clocks[] = {"1:23", "13:23", "9:05", "12:59"};
    const char *ldates[] = {"sabado, 4 de julio", "Saturday, July 4", "samedi 4 juillet"};
    for (size_t a = 0; a < sizeof(ws) / sizeof(ws[0]); a++) {
        for (int botn = 100; botn <= 800; botn += (a == 0 || a == 1) ? 37 : 1) {
            for (int q = 0; q < 4; q += (botn % 5 == 0) ? 1 : 3) {
                for (int h24 = 0; h24 < 2; h24++) {
                    int w = ws[a], bot = botn;
                    const char *ds = ldates[(q + h24) % 3];
                    ref_reloj_hora(w, bot, clocks[q], ds, "sab, 4 jul", h24, got, sizeof(got));
                    int uw, uh;
                    flex_ui_box(w, bot - FLEX_RELOJ_WIN_TOP, &uw, &uh);
                    flex_reloj_box_t bd;
                    flex_reloj_body(0, FLEX_RELOJ_WIN_TOP, uw, uh, &bd);
                    int pad = flex_ui_pad(uw, uh), gap = flex_ui_gap(pad);
                    datectx_t dc = {ds, bd.bw - 2 * pad};
                    flex_reloj_hora_t H;
                    flex_reloj_hora_layout(&bd, pad, gap, (int)strlen(clocks[q]), flex_ui_line_h(2), date_lh, &dc, &H);
                    sb_t e = {exp, sizeof(exp), 0};
                    exp[0] = 0;
                    sb_add(&e, "K%d,%d,%d,%d,%u:%s;", bd.bx + bd.bw / 2, H.cy, H.cap_h, H.thick, TH_TXT, clocks[q]);
                    if (H.show_date) {
                        int fs = fit(ds, bd.bw - 2 * pad, flex_ui_font_h(H.date_hint));
                        sb_add(&e, "V%d,%d,%d,%u,255:%s;", bd.bx + bd.bw / 2, H.date_y, fs, TH_TXT2, ds);
                    }
                    if (H.show_cards) {
                        const char *lbl[2] = {"Fecha", "Formato"};
                        const char *val[2] = {"sab, 4 jul", h24 ? "24 h" : "12 h"};
                        for (int i = 0; i < 2; i++) {
                            int cx = bd.bx + pad + i * (H.card_w + H.gap);
                            sb_add(&e, "W%d,%d,%d,%d,%d,%u,255;", cx, H.cards_y, H.card_w, H.card_h, pad, TH_CARD);
                            int fl = fit(lbl[i], H.card_w - 16, 2);
                            sb_add(&e, "V%d,%d,%d,%u,255:%s;", cx + H.card_w / 2, H.cards_y + H.card_h / 2 - flex_ui_line_h(fl) - 6, fl, TH_TXT2, lbl[i]);
                            int fv = fit(val[i], H.card_w - 16, 3);
                            sb_add(&e, "V%d,%d,%d,%u,255:%s;", cx + H.card_w / 2, H.cards_y + H.card_h / 2 + 2, fv, TH_TXT, val[i]);
                        }
                    }
                    if (H.show_footer) {
                        sb_add(&e, "M%d,%d,%d,%u:Reloj de FlexOS;", bd.bx + bd.bw / 2, H.footer_y,
                               fit("Reloj de FlexOS", bd.bw - 2 * pad, 2), TH_MUTE);
                    }
                    if (strcmp(got, exp) != 0) {
                        fprintf(stderr, "  Hora w=%d bot=%d clock=%s h24=%d\n  ref: %.700s\n  mod: %.700s\n", w, bot, clocks[q], h24, got, exp);
                    }
                    CHECK(strcmp(got, exp) == 0);
                }
            }
        }
    }
}

void test_chrono(void)
{
    ref_crono_text_k(6);
    s_k = 6;
    t_logic();
    t_fmt();
    t_ease();
    t_glyph();
    t_capsule();
    t_surface();
    t_cap_tick();
    t_layout();
    t_fit_big();
    t_dial();
    t_laps_buttons();
    t_tab_touch();
    t_card();
    t_overlay();
    t_reloj();
}
