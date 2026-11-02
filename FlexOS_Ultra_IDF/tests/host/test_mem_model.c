// Memoria de las apps (docs/spec/01c §17): huella medida, soltar en LRU, admision al abrir, alivio
// automatico y desalojo por presupuesto, comparados con FlexOS_Ultra_Core.h extraido. Tambien corre
// test_mem.cpp de Arduino (el nucleo de decision) sobre la copia de flex_portable.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "flex_mem_model.h"
#include "flex_test.h"

int test_mem_core(void);
void ref_mem_setup(const uint8_t *state, const uint32_t *seen, uint32_t bg_flags, uint32_t shed_mask);
uint32_t ref_mem_op(int op, int a, uint32_t b, uint32_t now, bool blocked, bool act_state);
void ref_mem_get(uint8_t *state, uint32_t *mem, uint8_t *known, uint8_t *shed);
FlexMemAlerts *ref_mem_alerts(void);

#define N 19
#define MB(x) ((uint32_t)((x) * 1024u * 1024u))

typedef struct {
    FlexMemSnap truth, snap;
    uint32_t sys_chunk, sys_left;
    uint32_t app_bytes[N];
    uint8_t hint_only[N];
    uint32_t bg_work, bg_flags, shed_mask;
    bool radio_busy;
    uint8_t term_ok[N];
    uint32_t term_frees[N];
    int cards[N], ncards;
    char log[600];
    int nlog;
    char notes[16][80];
    int nnotes;
    flex_mem_t *m;   // solo en el mundo de la implementacion nueva: terminate cierra la app
} world_t;
static world_t *W;

static void lg(char c, int a)
{
    if (W->nlog < 590) {
        W->log[W->nlog++] = c;
        W->log[W->nlog++] = (char)('A' + a);
    }
}
// ---- el mundo que ven las dos implementaciones ----
uint32_t w_free_psram(void) { return W->truth.psFree; }
void w_sample_now(void) { W->snap = W->truth; lg('S', 0); }
const FlexMemSnap *w_snap(void) { return &W->snap; }
uint32_t w_shed_system(void)
{
    uint32_t g = W->sys_left < W->sys_chunk ? W->sys_left : W->sys_chunk;
    W->sys_left -= g;
    W->truth.psFree += g;
    if (W->truth.psLargest + g <= W->truth.psFree) W->truth.psLargest += g;
    lg('Y', 0);
    return g;
}
bool w_bg_busy(int a) { return (W->bg_work >> a) & 1; }
bool w_has_shed(int a) { return (W->shed_mask >> a) & 1; }
size_t w_app_shed(int a)
{
    uint32_t g = W->app_bytes[a];
    W->app_bytes[a] = 0;
    W->truth.psFree += g;
    if (W->truth.psLargest + g <= W->truth.psFree) W->truth.psLargest += g;
    lg('H', a);
    return g || W->hint_only[a] ? 1 : 0;
}
bool w_radio_busy(void) { return W->radio_busy; }
bool w_terminate(int a)
{
    lg('T', a);
    if (!W->term_ok[a]) return false;
    W->truth.psFree += W->term_frees[a];
    W->truth.psLargest += W->term_frees[a] / 2;
    if (W->m) W->m->state[a] = FLEX_ALIFE_CLOSED;
    return true;
}
void w_drop_card(int a)
{
    for (int c = 0; c < W->ncards; c++) {
        if (W->cards[c] == a) {
            memmove(&W->cards[c], &W->cards[c + 1], (size_t)(W->ncards - c - 1) * sizeof(int));
            W->ncards--;
            return;
        }
    }
}
void w_notify(const char *t, const char *s)
{
    if (W->nnotes < 16) snprintf(W->notes[W->nnotes++], 80, "%.38s|%.38s", t, s);
}
int w_card_count(void) { return W->ncards; }
int w_card_app(int c) { return W->cards[c]; }
void w_drop_card_at(int c)
{
    memmove(&W->cards[c], &W->cards[c + 1], (size_t)(W->ncards - c - 1) * sizeof(int));
    W->ncards--;
}

// ---- operaciones de la implementacion nueva ----
static uint32_t m_free(void *c) { (void)c; return w_free_psram(); }
static void m_sample(void *c) { (void)c; w_sample_now(); }
static const FlexMemSnap *m_snap(void *c) { (void)c; return w_snap(); }
static uint32_t m_shed_sys(void *c) { (void)c; return w_shed_system(); }
static bool m_bg(void *c, int a) { (void)c; return ((W->bg_flags >> a) & 1) && w_bg_busy(a); }
static bool m_has(void *c, int a) { (void)c; return w_has_shed(a); }
static size_t m_shed(void *c, int a) { (void)c; return w_app_shed(a); }
static bool m_radio(void *c) { (void)c; return w_radio_busy(); }
static bool m_term(void *c, int a) { (void)c; return w_terminate(a); }
static void m_drop(void *c, int a) { (void)c; w_drop_card(a); }
static void m_notify(void *c, const char *t, const char *s) { (void)c; w_notify(t, s); }
static const flex_mem_ops_t OPS = {m_free, m_sample, m_snap, m_shed_sys, m_bg, m_has, m_shed, m_radio, m_term, m_drop, m_notify, NULL};

static uint32_t s_seed = 31337;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static void random_world(world_t *w)
{
    memset(w, 0, sizeof(*w));
    uint32_t total = MB(32), free_b = rnd() % 2 ? MB(2) + (rnd() % MB(5)) : MB(2) + (rnd() % MB(13));
    w->truth.psTotal = total;
    w->truth.psFree = free_b;
    w->truth.psLargest = rnd() % 4 == 0 ? free_b / (2 + rnd() % 8) : free_b - (rnd() % (free_b / 4 + 1));
    w->truth.psPeakUsed = total - free_b;
    w->truth.inTotal = 400 * 1024;
    w->truth.inFree = (30 + rnd() % 180) * 1024;
    w->truth.inMin = 20 * 1024 + rnd() % (100 * 1024);
    w->truth.fsTotal = MB(8);
    w->truth.fsUsed = rnd() % MB(8);
    w->truth.fsValid = (uint8_t)(rnd() % 4 != 0);
    w->snap = w->truth;
    w->sys_chunk = rnd() % 3 == 0 ? 0 : rnd() % MB(1);
    w->sys_left = w->sys_chunk * (1 + rnd() % 3);
    for (int i = 0; i < N; i++) {
        w->app_bytes[i] = rnd() % 3 == 0 ? 0 : (rnd() % MB(2));
        w->hint_only[i] = (uint8_t)(rnd() % 5 == 0);
        w->term_ok[i] = (uint8_t)(rnd() % 5 != 0);
        w->term_frees[i] = rnd() % MB(4);
    }
    w->bg_work = rnd() & 0x7FFFF;
    w->bg_flags = rnd() & 0x7FFFF;
    w->shed_mask = rnd() & 0x7FFFF;
    w->radio_busy = rnd() % 10 == 0;
    w->ncards = 0;
    for (int i = 0; i < N; i++) if (rnd() % 2) w->cards[w->ncards++] = i;
}

static bool same_world(const world_t *a, const world_t *b)
{
    return a->truth.psFree == b->truth.psFree && a->truth.psLargest == b->truth.psLargest &&
           a->snap.psFree == b->snap.psFree && a->nlog == b->nlog && !memcmp(a->log, b->log, (size_t)a->nlog) &&
           a->nnotes == b->nnotes && a->ncards == b->ncards && !memcmp(a->cards, b->cards, (size_t)a->ncards * sizeof(int)) &&
           !memcmp(a->app_bytes, b->app_bytes, sizeof(a->app_bytes)) && a->sys_left == b->sys_left &&
           (a->nnotes == 0 || !memcmp(a->notes, b->notes, (size_t)a->nnotes * 80));
}

void test_mem_model(void)
{
    CHECK_EQ_I(test_mem_core(), 0);   // el nucleo de decision de Arduino (test_mem.cpp) sobre flex_portable
    int crit_calls = 0, bad = 0, shed_runs = 0, closed_total = 0, notes_total = 0, denied = 0;
    for (int it = 0; it < 12000; it++) {
        static world_t wa, wb;
        random_world(&wa);
        wb = wa;
        flex_mem_t mine;
        flex_mem_init(&mine, N);
        wb.m = &mine;
        uint8_t st[N];
        uint32_t seen[N];
        int running = -1;
        for (int i = 0; i < N; i++) {
            st[i] = (uint8_t)(rnd() % 4 == 0 ? FLEX_ALIFE_CLOSED : FLEX_ALIFE_SUSPENDED);
            if (running < 0 && rnd() % 6 == 0) { st[i] = FLEX_ALIFE_RUNNING; running = i; }
            seen[i] = rnd() % 5 == 0 ? 7 : rnd() % 100000;   // empates incluidos
            mine.state[i] = st[i];
            mine.seen_ms[i] = seen[i];
        }
        W = &wa;
        ref_mem_setup(st, seen, wb.bg_flags, wb.shed_mask);
        uint32_t now = 100000 + rnd() % 1000;
        for (int k = 0; k < (it % 3 == 0 ? 14 : 8) && !bad; k++) {
            int op = (int)(rnd() % 8), a = (int)(rnd() % N);
            uint32_t b = rnd() % 3 == 0 ? 0 : rnd() % MB(3);
            bool blocked = rnd() % 8 == 0, act = rnd() % 5 != 0;
            if (it % 3 == 0) {   // 1 de cada 3: rachas de avisos con la memoria subiendo y bajando entre ticks
                op = rnd() % 10 ? 3 : (int)(rnd() % 8);
                uint32_t f = MB(2) + rnd() % MB(13);
                wa.truth.psFree = wb.truth.psFree = wa.snap.psFree = wb.snap.psFree = f;
                wa.truth.psLargest = wb.truth.psLargest = wa.snap.psLargest = wb.snap.psLargest = rnd() % 3 ? f - f / 8 : f / 6;
                blocked = rnd() % 20 == 0;
                act = rnd() % 8 != 0;
            }
            now += it % 3 == 0 ? rnd() % 9000 : (rnd() % 4 == 0 ? 61000 : rnd() % 5000);
            if (op == 2) b = rnd() % 3;
            if (op == 5) b = rnd() % 4 == 0 ? 0 : MB(1) + rnd() % MB(16);
            if (op == 5 || op == 6 || op == 7) ref_mem_get(st, (uint32_t[N]){0}, (uint8_t[N]){0}, (uint8_t[N]){0});   // sin efecto
            uint32_t rr, mr = 0;
            W = &wa;
            rr = ref_mem_op(op, a, b, now, blocked, act);
            W = &wb;
            switch (op) {
            case 0: mr = flex_mem_shed_app(&mine, &OPS, a); break;
            case 1: mr = flex_mem_shed_all(&mine, &OPS, b); break;
            case 2: mr = (uint32_t)flex_mem_admit(&mine, &OPS, (int)b); denied += mr != 0; break;
            case 3: flex_mem_alert_tick(&mine, &OPS, now, blocked, act && !wb.radio_busy); break;
            case 4: { int pre = flexMemLevel(&wb.truth) == FLEXMEM_LV_CRITICAL; crit_calls += pre; closed_total += flex_mem_enforce_budget(&mine, &OPS); break; }
            case 5: flex_mem_commit(&mine, &OPS, a, b); break;
            case 6: flex_mem_released(&mine, a, b); break;
            case 7: flex_mem_forget(&mine, a); break;
            }
            if (op < 3 && rr != mr) bad++;
            uint8_t rs[N], rk[N], rsh[N];
            uint32_t rm[N];
            ref_mem_get(rs, rm, rk, rsh);
            for (int i = 0; i < N; i++) {
                if (rs[i] != mine.state[i] || rm[i] != mine.mem[i] || rk[i] != mine.known[i] || rsh[i] != mine.shed[i]) {
                    bad++;
                }
            }
            if (memcmp(ref_mem_alerts(), &mine.alerts, sizeof(FlexMemAlerts)) || !same_world(&wa, &wb)) { bad++; }
            shed_runs += op == 1;
        }
        notes_total += wb.nnotes;
    }
    if (bad) fprintf(stderr, "memoria: %d diferencias con la version Arduino\n", bad);
    CHECK_EQ_I(bad, 0);
    CHECK(shed_runs > 5000 && closed_total > 500 && notes_total > 500 && denied > 500);
    printf("memoria: soltar, admitir, alivio y desalojo iguales a Arduino (%d cierres por presupuesto, %d avisos)\n",
           closed_total, notes_total);
}
