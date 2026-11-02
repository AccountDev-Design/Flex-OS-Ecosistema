// Personalizar inicio (docs/spec/01a §10): operaciones del modelo, selector de imagenes y
// clasificador de pellizco, comparados con el codigo de FlexOS_Ultra_HomeCfg.h extraido.
#include <stdint.h>
#include <stdio.h>
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
} rs_t;

void ref_home_normalize(rs_t *s);
int ref_hc_op(rs_t *s, int op, int a, int b, int *msg);
void ref_hc_fs_set(int d, int n, const char names[][48], const uint8_t *is_dir);
int ref_hc_scan(char paths[24][80]);
void ref_hc_pz_reset(void);
int ref_hc_pz_step(uint32_t now, int mode, int fingers, uint32_t fingers_ms, int n, const int pts[][3], int *swallow,
                   int *state, int *calls);

static uint32_t s_seed = 4242;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static void to_mine(const rs_t *s)
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

static bool same(const rs_t *s)
{
    if (memcmp(g_home.order, s->order, sizeof(g_home.order)) || g_home.page_n != s->page_n ||
        g_home.main != s->main || g_home.cols != s->cols || g_home.rows != s->rows ||
        g_home.icon_sz != s->icon_sz || g_home.page != s->page || g_home.fav != s->fav || g_home.hidden != s->hidden) {
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

static void random_state(rs_t *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < FLEX_HOME_TOTAL; i++) {
        uint32_t r = rnd() % 10;
        s->order[i] = r < 5 ? 0xFF : r < 9 ? (uint8_t)(rnd() % 19) : (uint8_t)(128 + rnd() % 4);
    }
    s->page_n = (uint8_t)(1 + rnd() % 5);
    s->main = (uint8_t)(rnd() % 5);
    s->cols = (uint8_t)(4 + rnd() % 2);
    s->rows = (uint8_t)(3 + rnd() % 2);
    s->icon_sz = (uint8_t)(rnd() % 3);
    s->page = (int)(rnd() % 5);
    s->fav = rnd() & 0x7FFFF;
    s->hidden = (rnd() % 3 == 0) ? (rnd() & 0x7FFFF) : 0;
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        s->wg_n[p] = (uint8_t)(rnd() % 5);
        for (int k = 0; k < FLEX_HOME_WG_MAX; k++) {
            s->wg[p][k][0] = (uint8_t)(1 + rnd() % 11);
            s->wg[p][k][1] = (uint8_t)(rnd() % 4);
            s->wg[p][k][2] = (uint8_t)(rnd() % 3);
            s->wg[p][k][3] = (uint8_t)(1 + rnd() % 3);
            s->wg[p][k][4] = (uint8_t)(1 + rnd() % 2);
        }
    }
    ref_home_normalize(s);   // un escritorio valido, como el que hay cuando se abre Personalizar
}

// ---- selector de imagenes --------------------------------------------------------------------
typedef struct { int n[3]; char names[3][24][48]; uint8_t dir[3][24]; } fs_t;
static int ls_cb(void *ctx, const char *dir, flex_hc_ent_t *e, int cap)
{
    const fs_t *f = ctx;
    int d = !strcmp(dir, "/Imagenes") ? 0 : !strcmp(dir, "/Camara") ? 1 : 2;
    int n = f->n[d] < cap ? f->n[d] : cap;
    for (int i = 0; i < n; i++) {
        snprintf(e[i].name, sizeof(e[i].name), "%s", f->names[d][i]);
        e[i].dir = f->dir[d][i] != 0;
    }
    return n;
}
static void random_name(char *o, size_t cap, bool jpeg_only)
{
    static const char *const EXT[] = {".jpg", ".JPG", ".jpeg", ".JPEG", ".png", ".jp", ".Jpg", ".jpeg ", "", ".jpe", ".JPg"};
    static const char *const BASE[] = {"a", "foto", "IMG_0001", "x y", "", "una_imagen_con_un_nombre_largo"};
    snprintf(o, cap, "%s%s", BASE[rnd() % 6], EXT[jpeg_only ? rnd() % 4 : rnd() % 11]);
}

// ---- pellizco ---------------------------------------------------------------------------------
typedef struct { int n; int pts[4][3]; } poll_ctx_t;
static int poll_cb(void *c, flex_pz_pt_t out[4])
{
    poll_ctx_t *p = c;
    for (int i = 0; i < p->n; i++) {
        out[i].x = p->pts[i][0];
        out[i].y = p->pts[i][1];
        out[i].id = p->pts[i][2];
    }
    return p->n;
}

static int run_pinch_script(int mode, bool smooth, int *triggers)
{
    flex_pz_t pz;
    flex_pz_init(&pz);
    ref_hc_pz_reset();
    uint32_t now = 1000 + rnd() % 500;
    int diffs = 0;
    // dos dedos que se mueven linealmente (o al azar) entre dos separaciones
    int cx = 140 + (int)(rnd() % 200), cy = 200 + (int)(rnd() % 400);
    int d0 = (mode == 1 ? 150 : 50) + (int)(rnd() % 150), d1 = smooth ? (mode == 1 ? d0 * 45 / 100 : d0 * 3) : (int)(40 + rnd() % 300);
    int steps = 20 + (int)(rnd() % 30), t_move = 3 + (int)(rnd() % 12);
    int ida = (int)(rnd() % 3), idb = 3 + (int)(rnd() % 2);
    for (int k = 0; k < steps + 12; k++) {
        now += 8 + rnd() % 20;
        int fingers = 2, fms = (int)now, n = 2;
        int d = d0 + (d1 - d0) * (k < t_move ? 0 : (k - t_move > 14 ? 14 : k - t_move)) / 14;
        if (!smooth) {
            uint32_t r = rnd() % 12;
            if (r == 0) fingers = 1;
            else if (r == 1) fingers = 0;
            else if (r == 2) n = 3;
            else if (r == 3) n = 1;
            else if (r == 4) fms = (int)now - 200;     // cuenta de dedos caducada
            else if (r == 5) { ida = (int)(rnd() % 5); }   // cambio de dedos
            d = d0 + (int)(rnd() % 200) - 100;
            if (d < 10) d = 10;
        }
        poll_ctx_t pc;
        pc.n = n;
        int ref_pts[4][3];
        for (int i = 0; i < n; i++) {
            int x = i == 0 ? cx - d / 2 : i == 1 ? cx + d / 2 : cx, y = cy + (i == 2 ? 30 : 0);
            pc.pts[i][0] = ref_pts[i][0] = x;
            pc.pts[i][1] = ref_pts[i][1] = y;
            pc.pts[i][2] = ref_pts[i][2] = i == 0 ? ida : i == 1 ? idb : 9;
        }
        int rsw, rst, rcalls;
        int ract = ref_hc_pz_step(now, mode, fingers, (uint32_t)fms, n, (const int(*)[3])ref_pts, &rsw, &rst, &rcalls);
        // contar cuantas veces se pide el multipunto: solo con >= 2 dedos frescos y estado != 2
        int calls = 0;
        flex_pz_pt_t scratch[4];
        (void)scratch;
        // el doble de la prueba cuenta las llamadas a traves de poll_cb envuelto
        struct { poll_ctx_t *pc; int *calls; } w = {&pc, &calls};
        int act = flex_pz_update(&pz, mode, (uint8_t)fingers, (uint32_t)fms, now,
                                 ({ int f(void *c, flex_pz_pt_t o[4]) { (*((typeof(w) *)c)->calls)++; return poll_cb(((typeof(w) *)c)->pc, o); } f; }),
                                 &w);
        int mact = act == FLEX_PZ_OPEN_CFG ? 1 : act == FLEX_PZ_EXIT_CFG ? 2 : 0;
        if (mact != ract || pz.swallow != (bool)rsw || pz.state != rst || calls != rcalls) {
            diffs++;
        }
        if (ract) {
            (*triggers)++;
        }
    }
    return diffs;
}

void test_homecfg(void)
{
    flex_home_set_registry(FLEX_APP_N_TEST, 0x3FFu | (1u << 15) | (1u << 17));
    // 1. Operaciones del modelo encadenadas sobre escritorios al azar
    int diffs = 0, ops_run[10] = {0};
    for (int it = 0; it < 5000; it++) {
        rs_t s;
        random_state(&s);
        to_mine(&s);
        for (int k = 0; k < 6; k++) {
            int op = (int)(rnd() % 10), a = (int)(rnd() % 7) - 1, b = (int)(rnd() % 7) - 1, msg, mine = 0, mmsg = 0;
            if (op == 5) {
                a = 3 + (int)(rnd() % 4);   // 3..6 (acotado a 4..5)
                b = 2 + (int)(rnd() % 4);   // 2..5 (acotado a 3..4)
            } else if (op == 7) {
                b = (int)(rnd() % 14) - 1;  // tipo de widget (incluidos invalidos)
            } else if (op == 9) {
                a = (int)(rnd() % g_home.page_n);
                b = (int)(rnd() % 12000) - 6000;   // deslizamiento en decimas de pixel
            }
            int ref = ref_hc_op(&s, op, a, b, &msg);
            switch (op) {
            case 0: mine = flex_hc_page_items(a); break;
            case 1: { flex_hc_res_t r = flex_hc_page_add(); mine = r == FLEX_HC_OK; mmsg = r == FLEX_HC_MAX_PAGES ? 1 : 0; break; }
            case 2: { flex_hc_res_t r = flex_hc_page_delete(a); mine = r == FLEX_HC_OK; mmsg = r == FLEX_HC_LAST_PAGE ? 2 : 0; break; }
            case 3: flex_hc_page_swap(a, b); break;
            case 4: flex_hc_set_main(a); break;
            case 5: flex_hc_set_grid(a, b); break;
            case 6: flex_hc_reset_layout(); break;
            case 7: mine = flex_hc_wg_add(a, b); break;
            case 8: mine = flex_hc_grid_idx(); break;
            case 9: mine = flex_hc_reorder_apply(a, (float)b / 10.0f, 240); break;
            }
            ops_run[op]++;
            if ((op == 0 || op == 1 || op == 2 || op == 7 || op == 8 || op == 9) && mine != ref) {
                diffs++;
            }
            if (mmsg != msg || !same(&s)) {
                diffs++;
                break;
            }
        }
    }
    if (diffs) {
        fprintf(stderr, "Personalizar inicio: %d diferencias con la version Arduino\n", diffs);
    }
    CHECK_EQ_I(diffs, 0);
    for (int i = 0; i < 10; i++) {
        CHECK(ops_run[i] > 200);   // todas las operaciones se ejercitaron de verdad
    }

    // 2. Selector de imagenes: nombres, extensiones, tope de 24 y de 24 por carpeta
    int sd = 0, nonempty = 0, capped = 0;
    for (int it = 0; it < 3000; it++) {
        fs_t f;
        memset(&f, 0, sizeof(f));
        for (int d = 0; d < 3; d++) {
            f.n[d] = it % 5 == 0 ? 24 : (int)(rnd() % 25);   // 1 de cada 5: carpetas llenas de JPEG (corta en 24)
            for (int i = 0; i < f.n[d]; i++) {
                random_name(f.names[d][i], sizeof(f.names[d][i]), it % 5 == 0);
                f.dir[d][i] = (uint8_t)(it % 5 != 0 && rnd() % 6 == 0);
            }
            ref_hc_fs_set(d, f.n[d], (const char(*)[48])f.names[d], f.dir[d]);
        }
        char rp[24][80], mp[24][80];
        memset(rp, 0, sizeof(rp));
        memset(mp, 0, sizeof(mp));
        int rn = ref_hc_scan(rp), mn = flex_hc_scan_images(ls_cb, &f, mp);
        if (rn != mn) {
            sd++;
        } else {
            for (int i = 0; i < rn; i++) {
                if (strcmp(rp[i], mp[i])) sd++;
            }
        }
        nonempty += rn > 0;
        capped += rn == 24;
    }
    CHECK_EQ_I(sd, 0);
    CHECK(nonempty > 1000 && capped > 20);

    // 3. Pellizco: mismas decisiones, tragado, estado y lecturas del multipunto
    int pd = 0, trig_home = 0, trig_cfg = 0;
    for (int it = 0; it < 6000; it++) {
        int mode = 1 + (int)(rnd() % 2);
        bool smooth = it % 2 == 0;
        pd += run_pinch_script(mode, smooth, mode == 1 ? &trig_home : &trig_cfg);
    }
    for (int it = 0; it < 1500; it++) {      // modo 0 (no permitido): nada y sin lecturas
        pd += run_pinch_script(0, it % 2, &trig_home);
    }
    if (pd) {
        fprintf(stderr, "pellizco: %d pasos distintos de la version Arduino\n", pd);
    }
    CHECK_EQ_I(pd, 0);
    CHECK(trig_home > 100 && trig_cfg > 100);   // abrir y salir se disparan de verdad
    printf("personalizar inicio: operaciones, imagenes y pellizco iguales a Arduino (%d abrir, %d salir)\n", trig_home,
           trig_cfg);
}
