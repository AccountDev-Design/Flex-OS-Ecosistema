// Arbitraje del tactil (components/flex_touch/src/flex_touch_arb.c) contra el
// tactil de alto nivel de la version Arduino extraido tal cual
// (ref/ref_touch_main.cpp): mismas entradas cuadro a cuadro, mismo "Touch T" y
// mismos gestos de suspension, en guiones fijos y en cientos de miles de
// cuadros aleatorios.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "flex_test.h"
#include "flex_touch_arb.h"

typedef struct {
    bool down, pressed, released, tap, moved, su, sd, sl, sr;
    int x, y, sx, sy, dx, dy;
} ref_t_out;

void ref_touch_reset(void);
void ref_touch_inputs(bool kiosk_filter, int kx, int ky, int kw, int kh, bool kiosk_on, bool typing, bool owns_two,
                      bool pinch_used, bool suspended);
void ref_touch_drop_all(uint32_t now);
int ref_touch_poll(int ev, int x, int y, int n, uint32_t now, ref_t_out *o);

static uint32_t s_rng = 12345;
static uint32_t rnd(uint32_t n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 8) % n;
}

static flex_arb_t A;

static bool same(const ref_t_out *r)
{
    const flex_arb_touch_t *t = &A.t;
    return r->down == t->down && r->pressed == t->pressed && r->released == t->released && r->tap == t->tap &&
           r->moved == t->moved && r->su == t->swipe_up && r->sd == t->swipe_down && r->sl == t->swipe_left &&
           r->sr == t->swipe_right && r->x == t->x && r->y == t->y && r->sx == t->start_x && r->sy == t->start_y &&
           r->dx == t->dx && r->dy == t->dy;
}

static int step(int ev, int x, int y, int n, uint32_t now, int *ref_evt)
{
    ref_t_out r;
    *ref_evt = ref_touch_poll(ev, x, y, n, now, &r);
    int e = flex_arb_poll(&A, ev, x, y, n, now);
    if (!same(&r)) {
        static int shown;
        if (shown++ < 5) {
            fprintf(stderr, "difiere en t=%u ev=%d (%d,%d) n=%d: ref down=%d tap=%d x=%d | idf down=%d tap=%d x=%d\n",
                    (unsigned)now, ev, x, y, n, r.down, r.tap, r.x, A.t.down, A.t.tap, A.t.x);
        }
        return -1;
    }
    return e;
}

static void set_inputs(bool kf, int kx, int ky, int kw, int kh, bool ko, bool ty, bool ow, bool pu, bool su)
{
    ref_touch_inputs(kf, kx, ky, kw, kh, ko, ty, ow, pu, su);
    A.kiosk_filter = kf;
    A.kx = (int16_t)kx;
    A.ky = (int16_t)ky;
    A.kw = (int16_t)kw;
    A.kh = (int16_t)kh;
    A.kiosk_on = ko;
    A.typing = ty;
    A.owns_two = ow;
    if (pu) {
        A.pinch_used = true;
    }
    A.suspended = su;
}

static void reset_both(void)
{
    ref_touch_reset();
    flex_arb_init(&A);
}

// Doble toque de dos dedos: 2 cuadros con n=2 por toque
static int two_finger_tap(uint32_t *now, int *mismatch)
{
    int ev_idf = 0, rev = 0, e;
    for (int k = 0; k < 3; k++) {
        e = step(1, 200, 300, 2, *now, &rev);
        *mismatch |= e < 0 || e != rev;
        ev_idf |= e > 0 ? e : 0;
        *now += 12;
    }
    e = step(0, 0, 0, 0, *now, &rev);
    *mismatch |= e < 0 || e != rev;
    ev_idf |= e > 0 ? e : 0;
    *now += 12;
    return ev_idf;
}

static void scripted(void)
{
    int rev, mismatch = 0;
    uint32_t now = 1000;
    reset_both();
    // toque simple: tap en el punto de apoyo
    step(1, 100, 200, 1, now, &rev);
    step(1, 104, 206, 1, now += 10, &rev);
    CHECK(step(0, 0, 0, 0, now += 10, &rev) == 0 && A.t.tap && A.t.x == 100 && A.t.y == 200);
    // deslizar hacia arriba
    step(1, 240, 700, 1, now += 100, &rev);
    step(1, 240, 500, 1, now += 50, &rev);
    CHECK(step(0, 0, 0, 0, now += 10, &rev) == 0 && A.t.swipe_up);
    // suelta por silencio (90 ms sin cuadros)
    step(1, 50, 50, 1, now += 100, &rev);
    step(-1, 0, 0, 0, now += 50, &rev);
    CHECK(A.t.down);
    step(-1, 0, 0, 0, now += 41, &rev);
    CHECK(!A.t.down && A.t.released);
    // doble toque de dos dedos -> suspender
    now += 500;
    int e1 = two_finger_tap(&now, &mismatch);
    now += 60;
    int e2 = two_finger_tap(&now, &mismatch);
    CHECK(e1 == 0 && e2 == FLEX_ARB_EV_SUSPEND);
    CHECK(!mismatch);
    // tecleando: vetado
    now += 1000;
    set_inputs(false, 0, 0, 0, 0, false, true, false, false, false);
    e1 = two_finger_tap(&now, &mismatch);
    now += 60;
    e2 = two_finger_tap(&now, &mismatch);
    CHECK(e1 == 0 && e2 == 0 && !mismatch);
    // suspendido: doble toque de un dedo -> despertar, y todo se lo traga
    set_inputs(false, 0, 0, 0, 0, false, false, false, false, true);
    now += 1000;
    step(1, 10, 10, 1, now, &rev);
    CHECK(!A.t.down && !A.t.pressed);
    CHECK(step(0, 0, 0, 0, now += 30, &rev) == 0);
    step(1, 10, 10, 1, now += 80, &rev);
    CHECK(step(0, 0, 0, 0, now += 30, &rev) == FLEX_ARB_EV_WAKE && rev == 2);
    // kiosco: zona excluida descartada, el candado de salida gana
    set_inputs(true, 0, 0, 480, 400, true, false, false, false, false);
    now += 1000;
    step(1, 100, 100, 1, now, &rev);
    CHECK(!A.t.down);
    step(1, 460, 56, 1, now += 10, &rev);
    CHECK(A.t.down);
    step(0, 0, 0, 0, now += 10, &rev);
    // tragado del episodio heredado
    set_inputs(false, 0, 0, 0, 0, false, false, false, false, false);
    step(1, 300, 300, 1, now += 200, &rev);
    ref_touch_drop_all(now);
    flex_arb_drop_all(&A, now);
    step(1, 300, 300, 1, now += 10, &rev);
    CHECK(!A.t.down);
    CHECK(step(0, 0, 0, 0, now += 10, &rev) == 0 && !A.t.tap && !A.t.released);
    step(1, 300, 300, 1, now += 50, &rev);
    CHECK(A.t.down && A.t.pressed);
}

static void fuzz(void)
{
    reset_both();
    uint32_t now = 1000;
    int fingers = 0, x = 240, y = 400, mism = 0, ev_mism = 0, events = 0;
    for (int i = 0; i < 400000; i++) {
        // entradas de la UI de vez en cuando
        if (rnd(500) == 0) {
            bool kf = rnd(4) == 0;
            set_inputs(kf, (int)rnd(480), (int)rnd(800), (int)rnd(300) - 20, (int)rnd(300) - 20, rnd(5) == 0,
                       rnd(6) == 0, rnd(6) == 0, rnd(8) == 0, rnd(5) == 0);
        }
        if (rnd(300) == 0) {
            ref_touch_drop_all(now);
            flex_arb_drop_all(&A, now);
        }
        // dedo: episodios cortos y largos, 1-3 dedos, cuadros que faltan
        uint32_t r = rnd(100);
        int ev;
        if (fingers == 0) {
            if (r < 30) {
                fingers = 1 + (rnd(5) == 0) + (rnd(12) == 0);
                x = (int)rnd(480);
                y = (int)rnd(800);
                ev = 1;
            } else {
                ev = r < 60 ? 0 : -1;
            }
        } else if (r < 12) {
            fingers = 0;
            ev = rnd(4) == 0 ? -1 : 0;   // a veces el GT911 no manda el 0
        } else if (r < 30) {
            ev = -1;
        } else {
            x += (int)rnd(41) - 20;
            y += (int)rnd(61) - 30;
            x = x < 0 ? 0 : x > 479 ? 479 : x;
            y = y < 0 ? 0 : y > 799 ? 799 : y;
            if (rnd(10) == 0) {
                fingers = 1 + (int)rnd(3);
            }
            ev = 1;
        }
        int rev;
        int e = step(ev, x, y, ev == 1 ? fingers : 0, now, &rev);
        if (e < 0) {
            mism++;
        } else if (e != rev) {
            ev_mism++;
        }
        events += e > 0;
        now += 1 + rnd(rnd(10) == 0 ? 160 : 25);
    }
    CHECK_EQ_I(mism, 0);
    CHECK_EQ_I(ev_mism, 0);
    CHECK(events > 10);   // el guion llega a disparar suspender/despertar
    printf("tactil: 400000 cuadros aleatorios iguales a Arduino, %d gestos de suspension\n", events);
}

void test_touch_arb(void)
{
    scripted();
    fuzz();
}
