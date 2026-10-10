#include <stdlib.h>
#include <string.h>
#include "flex_home_model.h"
#include "flex_storage.h"
#include "flex_test.h"

typedef struct {
    uint8_t order[FLEX_HOME_TOTAL];
    uint8_t page_n, main, cols, rows, icon_sz;
    int page;
    uint8_t wg[FLEX_HOME_PAGES_MAX][FLEX_HOME_WG_MAX][5];
    uint8_t wg_n[FLEX_HOME_PAGES_MAX];
    uint32_t fav, hidden;
} ref_state_t;

void ref_home_normalize(ref_state_t *s);
void ref_home_grid(uint8_t cols, uint8_t rows, uint8_t icon_sz, int out[8]);
void ref_wg_rect(uint8_t cols, uint8_t rows, uint8_t icon_sz, const uint8_t w5[5], int out[4]);
void ref_home_toggle(ref_state_t *s, int id, int op);

static uint32_t s_seed = 777;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static void to_mine(const ref_state_t *s)
{
    memcpy(g_home.order, s->order, sizeof(g_home.order));
    g_home.page_n = s->page_n;
    g_home.main = s->main;
    g_home.cols = s->cols;
    g_home.rows = s->rows;
    g_home.icon_sz = s->icon_sz;
    g_home.page = s->page;
    g_home.fav = s->fav;
    g_home.hidden = s->hidden;
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        g_home.wg_n[p] = s->wg_n[p];
        for (int k = 0; k < FLEX_HOME_WG_MAX; k++) {
            flex_home_wg_t *w = &g_home.wg[p][k];
            w->type = s->wg[p][k][0];
            w->col = s->wg[p][k][1];
            w->row = s->wg[p][k][2];
            w->w = s->wg[p][k][3];
            w->h = s->wg[p][k][4];
        }
    }
}

static bool same(const ref_state_t *s)
{
    if (memcmp(g_home.order, s->order, sizeof(g_home.order)) || g_home.page_n != s->page_n ||
        g_home.main != s->main || g_home.page != s->page || g_home.fav != s->fav || g_home.hidden != s->hidden) {
        return false;
    }
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        if (g_home.wg_n[p] != s->wg_n[p]) {
            return false;
        }
        for (int k = 0; k < g_home.wg_n[p]; k++) {
            const flex_home_wg_t *w = &g_home.wg[p][k];
            if (w->type != s->wg[p][k][0] || w->col != s->wg[p][k][1] || w->row != s->wg[p][k][2] ||
                w->w != s->wg[p][k][3] || w->h != s->wg[p][k][4]) {
                return false;
            }
        }
    }
    return true;
}

void test_home(void)
{
    flex_home_set_registry(FLEX_APP_N_TEST, 0x3FFu | (1u << 15) | (1u << 17));   // APP_DEF_FAV: 0..9, Flex Store, Device Care
    // Geometria: todas las rejillas y tamanos de icono
    for (int c = 3; c <= 6; c++) {
        for (int r = 2; r <= 5; r++) {
            for (int sz = 0; sz <= 3; sz++) {
                int ref[8], m[8];
                ref_home_grid((uint8_t)c, (uint8_t)r, (uint8_t)sz, ref);
                g_home.cols = (uint8_t)c;
                g_home.rows = (uint8_t)r;
                g_home.icon_sz = (uint8_t)sz;
                flex_home_grid(&m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &m[6]);
                m[7] = flex_home_dots_y();
                CHECK(memcmp(ref, m, sizeof(ref)) == 0);
                for (int k = 0; k < 40; k++) {
                    uint8_t w5[5] = {(uint8_t)(1 + rnd() % 11), (uint8_t)(rnd() % 5), (uint8_t)(rnd() % 5),
                                     (uint8_t)(1 + rnd() % 4), (uint8_t)(1 + rnd() % 4)};
                    int a[4], b[4];
                    ref_wg_rect((uint8_t)c, (uint8_t)r, (uint8_t)sz, w5, a);
                    flex_home_wg_t w = {w5[0], w5[1], w5[2], w5[3], w5[4]};
                    flex_home_wg_rect(&w, &b[0], &b[1], &b[2], &b[3]);
                    CHECK(memcmp(a, b, sizeof(a)) == 0);
                }
            }
        }
    }
    // Normalizacion: estados al azar (incluidos corruptos) -> mismo resultado
    int diffs = 0;
    for (int it = 0; it < 4000; it++) {
        ref_state_t s;
        memset(&s, 0, sizeof(s));
        for (int i = 0; i < FLEX_HOME_TOTAL; i++) {
            uint32_t r = rnd() % 10;
            s.order[i] = r < 5 ? 0xFF : r < 9 ? (uint8_t)(rnd() % 22) : (uint8_t)(128 + rnd() % 4);
        }
        s.page_n = (uint8_t)(rnd() % 7);
        s.main = (uint8_t)(rnd() % 6);
        s.cols = (uint8_t)(3 + rnd() % 3);
        s.rows = (uint8_t)(2 + rnd() % 3);
        s.icon_sz = (uint8_t)(rnd() % 3);
        s.page = (int)(rnd() % 7) - 1;
        s.fav = rnd() & 0x7FFFF;
        s.hidden = (rnd() % 3 == 0) ? (rnd() & 0x7FFFF) : 0;
        for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
            s.wg_n[p] = (uint8_t)(rnd() % 8);
            for (int k = 0; k < FLEX_HOME_WG_MAX; k++) {
                s.wg[p][k][0] = (uint8_t)(rnd() % 13);
                s.wg[p][k][1] = (uint8_t)(rnd() % 5);
                s.wg[p][k][2] = (uint8_t)(rnd() % 5);
                s.wg[p][k][3] = (uint8_t)(rnd() % 5);
                s.wg[p][k][4] = (uint8_t)(rnd() % 5);
            }
        }
        to_mine(&s);
        ref_home_normalize(&s);
        flex_home_normalize();
        if (!same(&s)) {
            diffs++;
        }
    }
    if (diffs) {
        fprintf(stderr, "normalizacion del escritorio: %d estados distintos de la version Arduino\n", diffs);
    }
    CHECK_EQ_I(diffs, 0);

    // Caja de aplicaciones: favorita / ocultar, sobre escritorios ya normales,
    // encadenando operaciones (drwFavToggle / drwHideToggle de Arduino)
    int tdiffs = 0;
    for (int it = 0; it < 3000; it++) {
        ref_state_t s;
        memset(&s, 0, sizeof(s));
        for (int i = 0; i < FLEX_HOME_TOTAL; i++) {
            uint32_t r = rnd() % 10;
            s.order[i] = r < 6 ? 0xFF : (uint8_t)(rnd() % 19);
        }
        s.page_n = (uint8_t)(1 + rnd() % 5);
        s.main = 0;
        s.cols = (uint8_t)(3 + rnd() % 3);
        s.rows = (uint8_t)(2 + rnd() % 3);
        s.icon_sz = (uint8_t)(rnd() % 3);
        s.fav = rnd() & 0x7FFFF;
        s.hidden = (rnd() % 3 == 0) ? (rnd() & 0x7FFFF) : 0;
        for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
            s.wg_n[p] = (uint8_t)(rnd() % 3);
            for (int k = 0; k < FLEX_HOME_WG_MAX; k++) {
                s.wg[p][k][0] = (uint8_t)(rnd() % 13);
                s.wg[p][k][1] = (uint8_t)(rnd() % 5);
                s.wg[p][k][2] = (uint8_t)(rnd() % 5);
                s.wg[p][k][3] = (uint8_t)(1 + rnd() % 4);
                s.wg[p][k][4] = (uint8_t)(1 + rnd() % 4);
            }
        }
        ref_home_normalize(&s);
        to_mine(&s);
        for (int k = 0; k < 6; k++) {
            int id = (int)(rnd() % 21) - 1;   // incluye -1 y 19 (fuera de rango)
            int op = (int)(rnd() % 2);
            ref_home_toggle(&s, id, op);
            if (op == 0) {
                flex_home_fav_toggle(id);
            } else {
                flex_home_hide_toggle(id);
            }
            if (!same(&s)) {
                tdiffs++;
                to_mine(&s);
            }
        }
    }
    if (tdiffs) {
        fprintf(stderr, "favorita/ocultar: %d operaciones distintas de la version Arduino\n", tdiffs);
    }
    CHECK_EQ_I(tdiffs, 0);
    CHECK(!flex_app_can_hide(10) && flex_app_can_hide(0));   // IC_AJUSTES = 10
}

// ---- carga desde la NVS con migraciones, contra homeOrderLoad de Arduino ----
void ref_home_load(uint8_t order[FLEX_HOME_TOTAL], uint8_t misc[6], uint32_t masks[3], uint8_t wg[157]);
void test_cfg_reset(void);
esp_err_t flex_cfg_set_i32(const char *k, int32_t v);
esp_err_t flex_cfg_set_blob(const char *k, const void *d, size_t l);

static void random_nvs(uint32_t seed)
{
    s_seed = seed;
    test_cfg_reset();
    uint32_t r = rnd() % 4;
    uint8_t b[100];
    for (int i = 0; i < 100; i++) {
        uint32_t q = rnd() % 10;
        b[i] = q < 5 ? 0xFF : q < 9 ? (uint8_t)(rnd() % 22) : (uint8_t)(128 + rnd() % 3);
    }
    if (r == 0) flex_cfg_set_blob("hordq", b, 100);
    if (r == 1) flex_cfg_set_blob("hordp", b, 36);
    if (r == 2) flex_cfg_set_blob("hord", b, 12);
    if (rnd() % 5 == 0) flex_cfg_set_blob("hordq", b, 50);   // tamano corrupto
    if (rnd() % 2) flex_cfg_set_i32("hpgn", (int32_t)(rnd() % 8) - 1);
    if (rnd() % 2) flex_cfg_set_i32("hpmain", (int32_t)(rnd() % 7) - 1);
    if (rnd() % 2) flex_cfg_set_i32("hgrid", (int32_t)(((3 + rnd() % 4) << 8) | (2 + rnd() % 4)));
    if (rnd() % 2) flex_cfg_set_i32("hicon", (int32_t)(rnd() % 5) - 1);
    if (rnd() % 2) flex_cfg_set_i32("hflag", (int32_t)(rnd() % 40) - 2);
    if (rnd() % 3) flex_cfg_set_i32("appfav", (int32_t)(rnd() & 0x3FFFFF));
    if (rnd() % 3) flex_cfg_set_i32("apphide", (int32_t)((rnd() % 3 == 0) ? (rnd() & 0x3FFFFF) : 0));
    if (rnd() % 2) flex_cfg_set_i32("appn", (int32_t)(14 + rnd() % 8));
    if (rnd() % 2) flex_cfg_set_i32("appver", (int32_t)(rnd() % 4));
    // 16 bits: Arduino truncaba los candados a uint16_t al cargar (Prefs.h:276);
    // aqui se conservan los 32 (se prueba aparte).
    if (rnd() % 2) flex_cfg_set_i32("applockm", (int32_t)(rnd() & 0xFFFF));
    uint8_t w[157];
    for (int i = 0; i < 157; i++) w[i] = (uint8_t)(rnd() % 6);
    w[0] = 'W';
    uint32_t wr = rnd() % 4;
    if (wr == 0) { w[1] = 2; flex_cfg_set_blob("hwg2", w, 157); }
    if (wr == 1) { w[1] = 1; flex_cfg_set_blob("hwg", w, 82); }
    if (wr == 2) { memset(w + 2, 0, 155); w[1] = 2; flex_cfg_set_blob("hwg2", w, 157); }
}

void test_home_load(void)
{
    flex_home_set_registry(FLEX_APP_N_TEST, 0x3FFu | (1u << 15) | (1u << 17));   // APP_DEF_FAV: 0..9, Flex Store, Device Care
    int diffs = 0;
    for (uint32_t it = 1; it <= 3000; it++) {
        uint8_t order[100], misc[6], wg[157];
        uint32_t masks[3];
        random_nvs(it * 7919u);
        ref_home_load(order, misc, masks, wg);
        random_nvs(it * 7919u);
        flex_home_load();
        uint8_t m6[6] = {g_home.page_n, g_home.main, g_home.cols, g_home.rows, g_home.icon_sz, (uint8_t)g_home.page};
        bool ok = memcmp(order, g_home.order, 100) == 0 && memcmp(misc, m6, 6) == 0 && masks[0] == g_home.fav &&
                  masks[1] == g_home.hidden;
        ok = ok && masks[2] == g_home.lock;
        for (int p = 0; ok && p < FLEX_HOME_PAGES_MAX; p++) {
            ok = wg[2 + p * 31] == g_home.wg_n[p];
            for (int k = 0; ok && k < g_home.wg_n[p]; k++) {
                const uint8_t *e = &wg[2 + p * 31 + 1 + k * 5];
                const flex_home_wg_t *x = &g_home.wg[p][k];
                ok = e[0] == x->type && e[1] == x->col && e[2] == x->row && e[3] == x->w && e[4] == x->h;
            }
        }
        if (!ok) {
            if (diffs < 2) {
                fprintf(stderr, "carga del escritorio distinta (semilla %u): order %d misc %d/%d/%d/%d/%d/%d vs %d/%d/%d/%d/%d/%d fav %x/%x hid %x/%x lock %x/%x\n",
                        (unsigned)it, memcmp(order, g_home.order, 100) != 0, misc[0], misc[1], misc[2], misc[3], misc[4], misc[5],
                        m6[0], m6[1], m6[2], m6[3], m6[4], m6[5], (unsigned)masks[0], (unsigned)g_home.fav,
                        (unsigned)masks[1], (unsigned)g_home.hidden, (unsigned)masks[2], (unsigned)g_home.lock);
                for (int i = 0; i < 40; i++) fprintf(stderr, "%02x%s", order[i], i % 20 == 19 ? "\n" : " ");
                for (int i = 0; i < 40; i++) fprintf(stderr, "%02x%s", g_home.order[i], i % 20 == 19 ? "\n" : " ");
            }
            diffs++;
        }
    }
    CHECK_EQ_I(diffs, 0);
    // Los candados de las apps 16..18 (Flex Phone, Device Care, Musica) sobreviven a la carga.
    test_cfg_reset();
    flex_cfg_set_i32("appfav", 0x0FFF);
    flex_cfg_set_i32("appver", 3);
    flex_cfg_set_i32("applockm", (1 << 16) | (1 << 18) | 1);
    flex_home_load();
    CHECK_EQ_I(g_home.lock, (1u << 16) | (1u << 18) | 1u);
}

// ---- Modo edicion: las mismas operaciones que Arduino sobre escritorios al azar ----
int ref_ed_op(ref_state_t *s, int op, int a, int b, int c, int out[4]);

static int my_ed_op(int op, int a, int b, int c, int out[4])
{
    switch (op) {
    case 0: return flex_home_slot_at(a, b);
    case 1: { int cc = -9, rr = -9; int r = flex_home_layout_cell_at(a, b, &cc, &rr); out[0] = cc; out[1] = rr; return r; }
    case 2: g_home.page = a; flex_home_ed_move(a, b, c); return 0;
    case 3: return flex_home_wg_at(a, b, c);
    case 4: flex_home_wg_remove(a, b); return 0;
    case 5: return flex_home_wg_to_page(a, b, c);
    case 6: return flex_home_band_bot();
    case 7: { flex_home_wg_limits(a, &out[0], &out[1], &out[2], &out[3]); return flex_home_wg_can_resize(a); }
    case 8: return flex_home_first_free_cell(a);
    }
    return 0;
}

void test_home_edit(void)
{
    int diffs = 0, ops = 0;
    for (int it = 0; it < 3000; it++) {
        ref_state_t s;
        memset(&s, 0, sizeof(s));
        for (int i = 0; i < FLEX_HOME_TOTAL; i++) {
            uint32_t r = rnd() % 10;
            s.order[i] = r < 5 ? 0xFF : (uint8_t)(rnd() % 19);
        }
        s.page_n = (uint8_t)(1 + rnd() % 5);
        s.cols = (uint8_t)(4 + rnd() % 2);
        s.rows = (uint8_t)(3 + rnd() % 2);
        s.icon_sz = (uint8_t)(rnd() % 3);
        s.fav = rnd() & 0x7FFFF;
        for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
            s.wg_n[p] = (uint8_t)(rnd() % 7);
            for (int k = 0; k < FLEX_HOME_WG_MAX; k++) {
                s.wg[p][k][0] = (uint8_t)(1 + rnd() % 11);
                s.wg[p][k][1] = (uint8_t)(rnd() % 5);
                s.wg[p][k][2] = (uint8_t)(rnd() % 5);
                s.wg[p][k][3] = (uint8_t)(1 + rnd() % 4);
                s.wg[p][k][4] = (uint8_t)(1 + rnd() % 3);
            }
        }
        ref_home_normalize(&s);
        to_mine(&s);
        for (int k = 0; k < 8; k++, ops++) {
            int op = (int)(rnd() % 9);
            int a = (int)(rnd() % 520) - 20, b = (int)(rnd() % 820) - 10, c = (int)(rnd() % 820) - 10;
            if (op == 2) { a = (int)(rnd() % s.page_n); b = (int)(rnd() % 22) - 1; c = (int)(rnd() % 22) - 1; }
            if (op == 3) { a = (int)(rnd() % 6) - 1; }
            if (op == 4) { a = (int)(rnd() % 6) - 1; b = (int)(rnd() % 8) - 1; }
            if (op == 5) { a = (int)(rnd() % 6) - 1; b = (int)(rnd() % 8) - 1; c = (int)(rnd() % 6) - 1; }
            if (op == 7) { a = (int)(rnd() % 14) - 1; }
            if (op == 8) { a = (int)(rnd() % s.page_n); }
            int ro[4] = {0}, mo[4] = {0};
            int rr = ref_ed_op(&s, op, a, b, c, ro);
            int mr = my_ed_op(op, a, b, c, mo);
            if (rr != mr || memcmp(ro, mo, sizeof(ro)) || !same(&s)) {
                if (diffs < 5) {
                    fprintf(stderr, "edicion: op %d (%d,%d,%d) ref %d mio %d\n", op, a, b, c, rr, mr);
                }
                diffs++;
                to_mine(&s);
            }
        }
    }
    CHECK_EQ_I(diffs, 0);

    // edEdgeCheck: 700 ms contra un borde (34 px), solo si existe la pagina vecina
    flex_home_edge_t e = {0, 0};
    CHECK(flex_home_ed_edge(&e, 20, 1, 3, 1000) == 0);    // empieza a contar
    CHECK(flex_home_ed_edge(&e, 20, 1, 3, 1700) == 0);    // 700: aun no (> 700)
    CHECK(flex_home_ed_edge(&e, 20, 1, 3, 1701) == -1);
    CHECK(flex_home_ed_edge(&e, 240, 1, 3, 1800) == 0 && e.dir == 0);
    CHECK(flex_home_ed_edge(&e, 470, 2, 3, 1900) == 0 && e.dir == 0);   // ultima pagina: no hay vecina
    CHECK(flex_home_ed_edge(&e, 446, 0, 3, 2000) == 0 && e.dir == 1);  // 480-34
    CHECK(flex_home_ed_edge(&e, 470, 0, 3, 2800) == 1);
    printf("edicion del escritorio: %d operaciones iguales a Arduino\n", ops);
}
