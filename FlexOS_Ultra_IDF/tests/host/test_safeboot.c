// Modo seguro: decision de arranque (safeBootEval, Session.h:165-187) y arranque
// estable (safeStableTick). Casos de docs/spec/01c §11.7 y la referencia de Arduino.
#include "flex_safeboot.h"
#include "flex_test.h"

// Referencia: safeBootEval de Arduino tal cual (sin NVS: entra lo guardado)
static void ref_eval(int rr, int saved, int saved_cause, bool have_cause, int *fails, int *cause, bool *safe)
{
    bool abnormal = rr == FLEX_RST_PANIC || rr == FLEX_RST_INT_WDT || rr == FLEX_RST_TASK_WDT || rr == FLEX_RST_WDT ||
                    rr == FLEX_RST_BROWNOUT;
    int f = saved;
    if (f < 0) {
        f = 0;
    }
    if (f > 250) {
        f = 250;
    }
    int c = rr;
    if (!abnormal) {
        c = have_cause ? saved_cause : rr;
    }
    if (abnormal) {
        if (f < 250) {
            f++;
        }
        c = rr;
    }
    *fails = f;
    *cause = c;
    *safe = f >= 3;
}

// Una cadena de arranques: devuelve si el ultimo es Modo seguro.
static flex_safe_eval_t chain(const int *rr, int n)
{
    int fails = 0, cause = 0;
    bool have = false;
    flex_safe_eval_t e = {0};
    for (int i = 0; i < n; i++) {
        e = flex_safe_eval(rr[i], fails, cause, have);
        fails = e.fails;
        if (e.write) {
            cause = e.cause;
            have = true;
        }
    }
    return e;
}

void test_safeboot(void)
{
    // 3 PANIC seguidos -> Modo seguro
    const int a[] = {FLEX_RST_PANIC, FLEX_RST_PANIC, FLEX_RST_PANIC};
    flex_safe_eval_t e = chain(a, 3);
    CHECK(e.safe);
    CHECK_EQ_I(e.fails, 3);
    CHECK_EQ_I(e.cause, FLEX_RST_PANIC);
    // PANIC, POWERON, PANIC -> 2 (encender NO limpia)
    const int b[] = {FLEX_RST_PANIC, FLEX_RST_POWERON, FLEX_RST_PANIC};
    e = chain(b, 3);
    CHECK(!e.safe);
    CHECK_EQ_I(e.fails, 2);
    // el reinicio voluntario y el del OTA (SW) y el deep sleep no suman
    const int c[] = {FLEX_RST_PANIC, FLEX_RST_SW, FLEX_RST_DEEPSLEEP, FLEX_RST_SW, FLEX_RST_TASK_WDT};
    e = chain(c, 5);
    CHECK_EQ_I(e.fails, 2);
    CHECK_EQ_I(e.cause, FLEX_RST_TASK_WDT);
    // un arranque normal conserva la causa de la cadena
    e = flex_safe_eval(FLEX_RST_POWERON, 4, FLEX_RST_BROWNOUT, true);
    CHECK(e.safe && !e.write && e.cause == FLEX_RST_BROWNOUT);
    e = flex_safe_eval(FLEX_RST_POWERON, 0, 0, false);
    CHECK_EQ_I(e.cause, FLEX_RST_POWERON);
    // tope 250 y basura en NVS
    e = flex_safe_eval(FLEX_RST_PANIC, 250, 0, true);
    CHECK_EQ_I(e.fails, 250);
    e = flex_safe_eval(FLEX_RST_PANIC, 9999, 0, true);
    CHECK_EQ_I(e.fails, 250);
    e = flex_safe_eval(FLEX_RST_POWERON, -7, 0, true);
    CHECK_EQ_I(e.fails, 0);
    // el P4 tambien cuenta el bloqueo de CPU y el fallo de alimentacion
    CHECK(flex_safe_abnormal(FLEX_RST_CPU_LOCKUP) && flex_safe_abnormal(FLEX_RST_PWR_GLITCH));
    CHECK(!flex_safe_abnormal(FLEX_RST_EXT) && !flex_safe_abnormal(FLEX_RST_UNKNOWN));
    // arranque estable: a los 60 s, solo fuera del Modo seguro y con algo que limpiar
    CHECK(!flex_safe_stable_clear(false, 2, 59999));
    CHECK(flex_safe_stable_clear(false, 2, 60000));
    CHECK(!flex_safe_stable_clear(false, 0, 90000));
    CHECK(!flex_safe_stable_clear(true, 3, 90000));
    // igual que Arduino para todos los motivos de Arduino y lo guardado
    int same = 0, total = 0;
    const int rrs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    for (unsigned i = 0; i < sizeof(rrs) / sizeof(rrs[0]); i++) {
        for (int sf = -3; sf <= 253; sf++) {
            for (int hc = 0; hc < 2; hc++) {
                int rf, rc;
                bool rs;
                ref_eval(rrs[i], sf, 6, hc, &rf, &rc, &rs);
                flex_safe_eval_t m = flex_safe_eval(rrs[i], sf, 6, hc);
                same += m.fails == rf && m.cause == rc && m.safe == rs;
                total++;
            }
        }
    }
    CHECK_EQ_I(same, total);
    CHECK(flex_safe_cause_text(FLEX_RST_PANIC)[0] == 'F');
    CHECK(flex_safe_cause_text(123)[0] == 'R');
}
