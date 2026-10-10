// Restablecimiento de fabrica: marcador transaccional (docs/spec/01c §13.6/§13.8).
// Un "almacen" simulado con cortes de corriente en cualquier operacion: tras
// cada corte, el arranque decide con lo guardado y retoma hasta terminar.
#include <string.h>
#include "flex_reset.h"
#include "flex_test.h"

typedef struct {
    flex_fr_marker_t disk;      // lo que hay en la NVS
    bool data[FLEX_FR_DEFAULTS + 1];   // datos que cada etapa borra (true = aun estan)
    int ops_left;               // corte de corriente tras N operaciones (-1 = nunca)
    bool dead;
    int fail_run_stage;         // etapa que falla al ejecutarse (0 = ninguna)
    int fail_save_n;            // la N-esima escritura del marcador no cuadra al releer (0 = ninguna)
    int saves;
    int runs[FLEX_FR_DEFAULTS + 1];
} sim_t;

static bool tick(sim_t *s)
{
    if (s->dead) {
        return false;
    }
    if (s->ops_left == 0) {
        s->dead = true;   // se va la luz: esta operacion no llega a hacerse
        return false;
    }
    if (s->ops_left > 0) {
        s->ops_left--;
    }
    return true;
}

static bool sim_save(void *ctx, const flex_fr_marker_t *m)
{
    sim_t *s = ctx;
    if (!tick(s)) {
        return false;
    }
    s->saves++;
    if (s->fail_save_n && s->saves == s->fail_save_n) {
        return false;   // escrito pero la relectura no cuadra: no se confia
    }
    s->disk = *m;
    return true;
}

static bool sim_run(void *ctx, int stage)
{
    sim_t *s = ctx;
    if (!tick(s)) {
        return false;
    }
    s->runs[stage]++;
    if (s->fail_run_stage == stage) {
        return false;
    }
    s->data[stage] = false;   // idempotente: borrar lo ya borrado no falla
    return true;
}

static void sim_init(sim_t *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = FLEX_FR_ARMED; i <= FLEX_FR_DEFAULTS; i++) {
        s->data[i] = true;
    }
    s->ops_left = -1;
}

static bool all_gone(const sim_t *s)
{
    for (int i = FLEX_FR_ARMED; i <= FLEX_FR_DEFAULTS; i++) {
        if (s->data[i]) {
            return false;
        }
    }
    return true;
}

// El motor de la tarea: armar (si hace falta) y avanzar hasta DONE o FAIL.
static int engine(const flex_fr_ops_t *ops, flex_fr_marker_t *m, bool arm)
{
    if (arm && !flex_fr_arm(ops, m)) {
        return m->stage;
    }
    while (m->stage >= FLEX_FR_ARMED && m->stage <= FLEX_FR_DEFAULTS) {
        flex_fr_step(ops, m);
    }
    return m->stage;
}

void test_reset(void)
{
    // Decisiones del arranque
    flex_fr_marker_t m = {0};
    CHECK_EQ_I(flex_fr_boot_decision(&m), FLEX_FR_BOOT_NORMAL);
    m = (flex_fr_marker_t){true, 2, FLEX_FR_FILES, 0};
    CHECK_EQ_I(flex_fr_boot_decision(&m), FLEX_FR_BOOT_DISCARD);
    m = (flex_fr_marker_t){true, FLEX_FR_VER, FLEX_FR_DONE, 0};
    CHECK_EQ_I(flex_fr_boot_decision(&m), FLEX_FR_BOOT_DONE);
    for (int st = FLEX_FR_ARMED; st <= FLEX_FR_FAIL; st++) {
        if (st != FLEX_FR_DONE) {
            m = (flex_fr_marker_t){true, FLEX_FR_VER, st, 0};
            CHECK_EQ_I(flex_fr_boot_decision(&m), FLEX_FR_BOOT_RESUME);
        }
    }
    m = (flex_fr_marker_t){true, FLEX_FR_VER, 0, 0};
    CHECK_EQ_I(flex_fr_boot_decision(&m), FLEX_FR_BOOT_DISCARD);
    m = (flex_fr_marker_t){true, FLEX_FR_VER, 77, 0};
    CHECK_EQ_I(flex_fr_boot_decision(&m), FLEX_FR_BOOT_DISCARD);

    // Sin cortes: 7 etapas, todo borrado, marcador DONE
    sim_t s;
    sim_init(&s);
    flex_fr_ops_t ops = {sim_save, sim_run, &s};
    CHECK_EQ_I(engine(&ops, &m, true), FLEX_FR_DONE);
    CHECK(all_gone(&s));
    CHECK(s.disk.pending && s.disk.stage == FLEX_FR_DONE);

    // Un corte en CADA operacion posible: el arranque retoma y termina; ninguna
    // etapa se salta y lo de despues del corte no se borra antes de tiempo.
    int cuts = 0;
    for (int k = 0; k < 40; k++) {
        sim_init(&s);
        s.ops_left = k;
        flex_fr_marker_t mm = {0};
        engine(&ops, &mm, true);
        if (!s.dead) {
            break;   // ya no hay mas puntos de corte
        }
        cuts++;
        // lo que dice el disco es lo unico que sobrevive
        flex_fr_marker_t disk = s.disk;
        bool armed = disk.pending;
        for (int i = FLEX_FR_ARMED; i <= FLEX_FR_DEFAULTS; i++) {
            if (armed && disk.stage <= FLEX_FR_DEFAULTS && i > disk.stage) {
                CHECK(s.data[i]);   // etapas posteriores a la anotada: intactas
            }
            if (!armed) {
                CHECK(s.data[i]);   // sin armar no se ha tocado nada
            }
        }
        s.dead = false;
        s.ops_left = -1;
        flex_fr_boot_t d = flex_fr_boot_decision(&disk);
        if (!armed) {
            CHECK_EQ_I(d, FLEX_FR_BOOT_NORMAL);
            continue;
        }
        CHECK(d == FLEX_FR_BOOT_RESUME || d == FLEX_FR_BOOT_DONE);
        mm = disk;
        if (mm.stage == FLEX_FR_FAIL) {
            CHECK(flex_fr_retry(&ops, &mm));
        }
        CHECK_EQ_I(engine(&ops, &mm, false), FLEX_FR_DONE);
        CHECK(all_gone(&s));
        CHECK_EQ_I(s.disk.stage, FLEX_FR_DONE);
    }
    CHECK(cuts >= 14);

    // El marcador no se puede armar: nada borrado, FAIL con err 0, reintentar arma
    sim_init(&s);
    s.fail_save_n = 1;
    flex_fr_marker_t mf = {0};
    CHECK(!flex_fr_arm(&ops, &mf));
    CHECK(mf.stage == FLEX_FR_FAIL && mf.err == 0 && !mf.pending);
    for (int i = FLEX_FR_ARMED; i <= FLEX_FR_DEFAULTS; i++) {
        CHECK(s.data[i] && s.runs[i] == 0);
    }
    CHECK(flex_fr_retry(&ops, &mf));
    CHECK_EQ_I(engine(&ops, &mf, false), FLEX_FR_DONE);
    CHECK(all_gone(&s));

    // Falla una etapa: FAIL con err, lo de despues intacto; reintentar retoma esa etapa
    sim_init(&s);
    s.fail_run_stage = FLEX_FR_FILES;
    flex_fr_marker_t me = {0};
    CHECK_EQ_I(engine(&ops, &me, true), FLEX_FR_FAIL);
    CHECK_EQ_I(me.err, FLEX_FR_FILES);
    CHECK(s.disk.stage == FLEX_FR_FAIL && s.disk.err == FLEX_FR_FILES);
    CHECK(!s.data[FLEX_FR_APPDATA] && s.data[FLEX_FR_FILES] && s.data[FLEX_FR_NVS]);
    s.fail_run_stage = 0;
    CHECK(flex_fr_retry(&ops, &me));
    CHECK_EQ_I(me.stage, FLEX_FR_FILES);
    CHECK_EQ_I(engine(&ops, &me, false), FLEX_FR_DONE);
    CHECK(all_gone(&s));
    CHECK_EQ_I(s.runs[FLEX_FR_APPDATA], 1);   // lo ya hecho no se repite

    // La etapa se hizo pero el marcador no se pudo anotar: FAIL y se repite (idempotente)
    sim_init(&s);
    s.fail_save_n = 4;   // 1 armar, 2 tras ARMED, 3 tras TOKENS, 4 tras REMOTE
    flex_fr_marker_t mg = {0};
    CHECK_EQ_I(engine(&ops, &mg, true), FLEX_FR_FAIL);
    CHECK_EQ_I(mg.err, FLEX_FR_REMOTE);
    CHECK(flex_fr_retry(&ops, &mg));
    CHECK_EQ_I(engine(&ops, &mg, false), FLEX_FR_DONE);
    CHECK_EQ_I(s.runs[FLEX_FR_REMOTE], 2);

    // Textos y pasos
    CHECK_EQ_I(flex_fr_step_index(FLEX_FR_ARMED), 1);
    CHECK_EQ_I(flex_fr_step_index(FLEX_FR_DEFAULTS), 7);
    CHECK(strcmp(flex_fr_stage_name(FLEX_FR_TOKENS), "Borrando credenciales y tokens") == 0);
    CHECK(flex_fr_stage_name(FLEX_FR_DONE)[0] == 0);
    printf("restablecimiento: %d puntos de corte, todos retoman y terminan\n", cuts);
}
