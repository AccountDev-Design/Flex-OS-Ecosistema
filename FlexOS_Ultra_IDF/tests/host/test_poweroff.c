// Apagado completo: deslizador "desliza para apagar" (formulas de poffTick,
// Power.h:834-878) y filtro de encendido de 3 s (poffWakeGate, Power.h:1056-1083,
// con la salida temprana sin dedo que pide docs/spec/01c §10.6).
#include <string.h>
#include "flex_poweroff.h"
#include "flex_test.h"

// ---- referencia de Arduino (copiada de poffTick) ------------------------------------
#define A_TRACK_X 40
#define A_TRACK_Y 352
#define A_TRACK_W 400
#define A_TRACK_H 96
#define A_PAD 6
#define A_KNOB_D (A_TRACK_H - 2 * A_PAD)
#define A_RUN (A_TRACK_W - 2 * A_PAD - A_KNOB_D)

static bool ref_grab(int knob, int x, int y)
{
    int kx = A_TRACK_X + A_PAD + knob;
    return y >= A_TRACK_Y && y <= A_TRACK_Y + A_TRACK_H && x >= kx - 24 && x <= kx + A_KNOB_D + 24;
}

static int ref_knob(int x, int grab)
{
    int v = x - grab - (A_TRACK_X + A_PAD);
    if (v < 0) {
        v = 0;
    }
    if (v > A_RUN) {
        v = A_RUN;
    }
    return v;
}

static int ref_spring(int knob, int target)
{
    if (knob != target) {
        int d = target - knob;
        knob += (d > 0 ? (d + 3) / 4 : (d - 3) / 4);
        if (abs(target - knob) < 3) {
            knob = target;
        }
    }
    return knob;
}

static void t_slider(void)
{
    CHECK_EQ_I(FLEX_POFF_RUN, 304);
    CHECK_EQ_I(A_RUN, FLEX_POFF_RUN);
    int same = 0;
    for (int knob = 0; knob <= FLEX_POFF_RUN; knob += 8) {
        for (int y = 330; y <= 470; y += 3) {
            for (int x = -10; x <= 490; x += 3) {
                same += flex_poff_grab(knob, x, y) == ref_grab(knob, x, y);
            }
        }
    }
    CHECK_EQ_I(same, 39 * 47 * 167);
    for (int grab = -24; grab <= 108; grab += 4) {
        for (int x = -20; x <= 500; x++) {
            if (flex_poff_knob_at(x, grab) != ref_knob(x, grab)) {
                CHECK(!"knob_at distinto de Arduino");
                grab = 1000;
                break;
            }
        }
    }
    // 92 %: el primer pomo que apaga es el 280
    CHECK(!flex_poff_done(279));
    CHECK(flex_poff_done(280));
    CHECK(flex_poff_done(FLEX_POFF_RUN));
    CHECK(!flex_poff_done(0));
    // vuelta a 0: identica a Arduino paso a paso y siempre termina
    for (int k0 = 0; k0 <= FLEX_POFF_RUN; k0++) {
        int a = k0, b = k0, steps = 0;
        while ((a != 0 || b != 0) && steps < 60) {
            a = flex_poff_spring_step(a, 0);
            b = ref_spring(b, 0);
            if (a != b) {
                break;
            }
            steps++;
        }
        if (a != b || a != 0 || steps >= 60) {
            CHECK(!"vuelta del pomo distinta de Arduino o sin terminar");
            break;
        }
    }
    CHECK_EQ_I(flex_poff_spring_step(100, 100), 100);
}

// ---- filtro de encendido ------------------------------------------------------------------
// fingers(t) para t en ms; devuelve el resultado y el instante de la decision.
typedef int (*finger_fn)(uint32_t t);

static flex_wg_result_t run_gate(finger_fn f, uint32_t *at)
{
    flex_wakegate_t g;
    flex_wakegate_init(&g, 1000);   // el reloj no empieza en 0
    for (uint32_t t = 0; t <= 10000; t += FLEX_WG_POLL_MS) {
        flex_wg_result_t r = flex_wakegate_step(&g, 1000 + t, f(t));
        if (r != FLEX_WG_WAIT) {
            *at = t;
            return r;
        }
    }
    *at = 99999;
    return FLEX_WG_WAIT;
}

static int f_hold(uint32_t t) { (void)t; return 1; }
static int f_none(uint32_t t) { (void)t; return 0; }
static int f_late200(uint32_t t) { return t >= 200 ? 1 : 0; }
static int f_late350(uint32_t t) { return t >= 350 ? 2 : 0; }
static int f_blip(uint32_t t) { return (t >= 2900 && t < 3000) ? 0 : 1; }   // suelta 100 ms a los 2,9 s
static int f_lift(uint32_t t) { return t < 1000 ? 1 : 0; }
static int f_from1300(uint32_t t) { return t >= 1300 ? 1 : 0; }
static int f_two(uint32_t t) { (void)t; return 2; }

static void t_gate(void)
{
    uint32_t at;
    CHECK_EQ_I(run_gate(f_hold, &at), FLEX_WG_BOOT);
    CHECK_EQ_I(at, FLEX_WG_HOLD_MS);   // 3 s sostenidos: arranca
    CHECK_EQ_I(run_gate(f_two, &at), FLEX_WG_BOOT);
    CHECK_EQ_I(run_gate(f_none, &at), FLEX_WG_SLEEP);
    CHECK_EQ_I(at, FLEX_WG_NOFINGER_MS);   // sin dedo: a dormir enseguida (Arduino: 4,2 s)
    CHECK_EQ_I(run_gate(f_late200, &at), FLEX_WG_BOOT);
    CHECK_EQ_I(at, 200 + FLEX_WG_HOLD_MS);
    CHECK_EQ_I(run_gate(f_late350, &at), FLEX_WG_SLEEP);
    CHECK_EQ_I(at, FLEX_WG_NOFINGER_MS);
    CHECK_EQ_I(run_gate(f_blip, &at), FLEX_WG_SLEEP);   // la cuenta vuelve a empezar y no da tiempo
    CHECK_EQ_I(at, FLEX_WG_WINDOW_MS);
    CHECK_EQ_I(run_gate(f_lift, &at), FLEX_WG_SLEEP);
    CHECK_EQ_I(at, 1000 - FLEX_WG_POLL_MS + FLEX_WG_NOFINGER_MS);   // ultimo dedo a 990 ms + 300
    CHECK_EQ_I(run_gate(f_from1300, &at), FLEX_WG_SLEEP);   // a los 1,3 s ya no caben 3 s en 4,2 s
    CHECK(at <= FLEX_WG_WINDOW_MS);

    // Invariantes con secuencias al azar: solo arranca tras 3 s seguidos con dedo,
    // y solo duerme sin dedo 300 ms seguidos o al acabar la ventana.
    uint32_t seed = 12345;
    int boots = 0, sleeps = 0, bad = 0;
    for (int run = 0; run < 4000; run++) {
        flex_wakegate_t g;
        flex_wakegate_init(&g, 0);
        uint32_t last_zero = 0, hold_start = 0, last_finger = 0;
        bool holding = false;
        int mode = run % 4;
        for (uint32_t t = 0; t <= 6000; t += FLEX_WG_POLL_MS) {
            seed = seed * 1103515245u + 12345u;
            int r = (int)((seed >> 16) % 1000);
            int n = mode == 0 ? (r < 997 ? 1 : 0) : mode == 1 ? (r < 950 ? 1 : 0) : mode == 2 ? (r < 500 ? 1 : 0) : (r < 20 ? 1 : 0);
            if (n) {
                if (!holding) {
                    holding = true;
                    hold_start = t;
                }
                last_finger = t;
            } else {
                holding = false;
                last_zero = t;
            }
            flex_wg_result_t res = flex_wakegate_step(&g, t, n);
            if (res == FLEX_WG_BOOT) {
                boots++;
                bad += !(holding && t - hold_start >= FLEX_WG_HOLD_MS && t < FLEX_WG_WINDOW_MS + FLEX_WG_POLL_MS);
                break;
            }
            if (res == FLEX_WG_SLEEP) {
                sleeps++;
                bad += !((!n && t - last_finger >= FLEX_WG_NOFINGER_MS) || t >= FLEX_WG_WINDOW_MS);
                break;
            }
            bad += t > FLEX_WG_WINDOW_MS;   // nunca sigue esperando pasada la ventana
        }
        (void)last_zero;
    }
    CHECK_EQ_I(bad, 0);
    CHECK(boots > 100 && sleeps > 100);
    printf("apagado: deslizador igual a Arduino; filtro de encendido %d arranques / %d a dormir sin fallos\n", boots,
           sleeps);
}

void test_poweroff(void)
{
    t_slider();
    t_gate();
}
