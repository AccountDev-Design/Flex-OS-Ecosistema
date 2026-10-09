// Panel rapido (flex_qs_model.c) contra el modelo de Arduino (QuickPanel.h,
// QuickPanelGlass.h, QuickPanelEdit.h) extraido tal cual: registro, tamanos,
// fabrica, normalizacion, blob flexqs/qp1, carga, maquetacion, ajuste del alto
// de la tarjeta, editor y catalogo, con disponibilidades y entradas aleatorias.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "flex_qs_model.h"
#include "flex_test.h"

void ref_qs_set(uint32_t avail, bool glass);
void ref_qs_meta(int id, const char **name, const char **title, uint8_t *t, uint8_t *sz, uint8_t *ori, uint8_t *cat);
bool ref_qs_shown(int id);
void ref_qs_first_size(uint8_t mask, uint8_t *w, uint8_t *h);
bool ref_qs_size_allowed(int id, int w, int h);
bool ref_qs_next_size(int id, int w, int h, int dir, uint8_t *nw, uint8_t *nh);
void ref_qs_factory(uint8_t *items, uint8_t *n, uint8_t *grows);
uint8_t ref_qs_normalize(uint8_t *items, uint8_t n, uint8_t *grows);
bool ref_qs_deserialize(const uint8_t *b, uint8_t *items, uint8_t *n, uint8_t *grows);
void ref_qs_load(const uint8_t *b, size_t rd, uint8_t *items, uint8_t *n, uint8_t *grows);
void ref_qs_serialize(const uint8_t *items, uint8_t n, uint8_t grows, uint8_t *b);
int ref_qs_layout(const uint8_t *items, int n, int group_px, bool edit, int16_t *blocks, uint8_t *tiles, int *tile_n,
                  int *content_h, int *group_blk);
int ref_qs_relayout(const uint8_t *items, uint8_t n, bool edit, float gh, uint8_t grows, float *gh_out,
                    int *content_h, int *scroll_max);
float ref_qs_group_snap(const uint8_t *items, uint8_t n, float gh, uint8_t *rows, uint32_t *dur);
void ref_qs_tile_center(int k, int gy, int *cx, int *cy);
bool ref_qs_edit(uint8_t *items, uint8_t *n, int op, int a, int b);
int ref_qs_catalog(const uint8_t *items, uint8_t n, uint8_t *out);

static uint32_t s_seed = 4242;
static uint32_t rnd(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

// La disponibilidad de la prueba, igual para las dos implementaciones
static uint32_t s_av;
static bool s_glass;
static bool av(int id)
{
    return (s_av >> id) & 1u;
}
static bool shown(int id)
{
    return av(id) && !(id == FLEX_QS_GLASSFX && !s_glass);
}
static void set_av(uint32_t m, bool glass)
{
    s_av = m;
    s_glass = glass;
    ref_qs_set(m, glass);
}

static int s_diff;
#define SAME(cond)                                                                   \
    do {                                                                             \
        if (!(cond)) {                                                               \
            if (s_diff++ < 10) {                                                     \
                fprintf(stderr, "qs: distinto de Arduino (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            }                                                                        \
        }                                                                            \
    } while (0)

static bool same_cfg(const flex_qs_cfg_t *c, const uint8_t *items, uint8_t n, uint8_t grows)
{
    return c->n == n && c->grows == grows && memcmp(c->it, items, (size_t)n * 5) == 0;
}

static void rand_items(flex_qs_item_t *it, int n, int idmax)
{
    for (int i = 0; i < n; i++) {
        it[i].id = (uint8_t)(rnd() % (uint32_t)idmax);
        it[i].w = (uint8_t)(rnd() % 6);
        it[i].h = (uint8_t)(rnd() % 4);
        it[i].ori = (uint8_t)(rnd() % 4);
        it[i].vis = (uint8_t)(rnd() % 5 ? 1 : (rnd() % 3));
    }
}

static uint32_t rand_avail(void)
{
    switch (rnd() % 4) {
    case 0:
        return 0xFFFFFFu;   // todo
    case 1:
        return rnd() & 0xFFFFFFu;
    case 2:
        return 0xFFFFFFu & ~(1u << (rnd() % 24)) & ~(1u << (rnd() % 24));
    default:
        return rnd() & rnd() & 0xFFFFFFu;   // poco disponible
    }
}

// Configuracion valida aleatoria (normalizada) para maquetar y editar
static void rand_cfg(flex_qs_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->n = (uint8_t)(rnd() % 25);
    c->grows = (uint8_t)(rnd() % 8);
    rand_items(c->it, c->n, rnd() % 4 ? FLEX_QS_COUNT : 32);
    c->n = flex_qs_normalize(c->it, c->n, &c->grows, av);
    for (int i = 0; i < c->n; i++) {
        if (rnd() % 9 == 0) {
            c->it[i].vis = 0;
        }
    }
}

static void t_registry(void)
{
    for (int id = 0; id < FLEX_QS_COUNT; id++) {
        const char *n, *t;
        uint8_t ty, sz, ori, cat;
        ref_qs_meta(id, &n, &t, &ty, &sz, &ori, &cat);
        const flex_qs_meta_t *m = flex_qs_meta(id);
        CHECK(m && strcmp(m->name, n) == 0 && strcmp(m->title, t) == 0);
        CHECK(m && m->type == ty && m->sizes == sz && m->oris == ori && m->cat == cat);
    }
    CHECK(flex_qs_meta(-1) == NULL && flex_qs_meta(FLEX_QS_COUNT) == NULL);
    for (int mask = 0; mask < 16; mask++) {
        uint8_t w1, h1, w2, h2;
        ref_qs_first_size((uint8_t)mask, &w1, &h1);
        flex_qs_first_size((uint8_t)mask, &w2, &h2);
        CHECK(w1 == w2 && h1 == h2);
    }
    for (int id = -2; id < FLEX_QS_COUNT + 3; id++) {
        for (int w = 0; w < 6; w++) {
            for (int h = 0; h < 4; h++) {
                CHECK(ref_qs_size_allowed(id, w, h) == flex_qs_size_allowed(id, w, h));
                for (int dir = -1; dir <= 1; dir += 2) {
                    uint8_t a = 9, b = 9, c = 9, d = 9;
                    bool r1 = ref_qs_next_size(id, w, h, dir, &a, &b);
                    bool r2 = flex_qs_next_size(id, w, h, dir, &c, &d);
                    CHECK(r1 == r2 && (!r1 || (a == c && b == d)));
                }
            }
        }
    }
}

static void t_factory_blob_load(void)
{
    for (int it = 0; it < 30000; it++) {
        set_av(rand_avail(), rnd() & 1);
        uint8_t ri[FLEX_QS_MAX_ITEMS * 5], rn, rg;
        flex_qs_cfg_t c;
        memset(&c, 0, sizeof(c));
        ref_qs_factory(ri, &rn, &rg);
        flex_qs_factory(&c, av);
        SAME(same_cfg(&c, ri, rn, rg));

        // normalizar una lista cualquiera
        flex_qs_item_t raw[FLEX_QS_MAX_ITEMS];
        uint8_t n = (uint8_t)(rnd() % 30), g1 = (uint8_t)(rnd() % 9), g2 = g1;
        rand_items(raw, FLEX_QS_MAX_ITEMS, 32);
        memcpy(ri, raw, sizeof(raw));
        uint8_t o1 = ref_qs_normalize(ri, n, &g1);
        uint8_t o2 = flex_qs_normalize(raw, n, &g2, av);
        SAME(o1 == o2 && g1 == g2 && memcmp(ri, raw, (size_t)o1 * 5) == 0);

        // blob: cabecera y contenido aleatorios (a veces validos)
        uint8_t b[FLEX_QS_BLOB_N];
        for (int i = 0; i < FLEX_QS_BLOB_N; i++) {
            b[i] = (uint8_t)rnd();
        }
        if (rnd() % 8) {
            b[0] = 'Q';
            b[1] = (uint8_t)(rnd() % 4);
            b[2] = (uint8_t)(rnd() % 27);
            b[3] = (uint8_t)(rnd() % 8);
            for (int i = 0; i < FLEX_QS_MAX_ITEMS; i++) {
                uint8_t *p = b + 4 + i * 5;
                p[0] = (uint8_t)(rnd() % (rnd() % 5 ? FLEX_QS_COUNT : 40));
                p[1] = (uint8_t)(rnd() % 5);
                p[2] = (uint8_t)(rnd() % 3);
                p[3] = (uint8_t)(rnd() % 4);
                p[4] = (uint8_t)(rnd() % 4 ? 1 : 0);
            }
        }
        memset(&c, 0, sizeof(c));
        c.grows = 3;
        bool d1 = ref_qs_deserialize(b, ri, &rn, &rg);
        bool d2 = flex_qs_deserialize(&c, b, av);
        SAME(d1 == d2);
        if (d1 && d2) {
            SAME(same_cfg(&c, ri, rn, rg));
        }
        const uint8_t *bp = rnd() % 10 ? b : NULL;
        size_t rd = !bp ? 0 : rnd() % 6 ? FLEX_QS_BLOB_N : rnd() % 200;
        memset(&c, 0, sizeof(c));
        ref_qs_load(bp, rd, ri, &rn, &rg);
        flex_qs_load(&c, bp, rd, av);
        SAME(same_cfg(&c, ri, rn, rg));

        // serializar: byte a byte
        uint8_t s1[FLEX_QS_BLOB_N], s2[FLEX_QS_BLOB_N];
        memcpy(ri, c.it, sizeof(c.it));
        ref_qs_serialize(ri, c.n, c.grows, s1);
        flex_qs_serialize(&c, s2);
        SAME(memcmp(s1, s2, FLEX_QS_BLOB_N) == 0);
    }
    // carga sin blob (b = NULL en las dos)
    set_av(0xFFFFFFu, true);
    uint8_t ri[FLEX_QS_MAX_ITEMS * 5], rn, rg;
    flex_qs_cfg_t c;
    memset(&c, 0, sizeof(c));
    ref_qs_load(NULL, 0, ri, &rn, &rg);
    CHECK(!flex_qs_load(&c, NULL, 0, av));
    CHECK(same_cfg(&c, ri, rn, rg));
}

static void t_layout(void)
{
    static const int GPX[] = {0, 1, 100, 232, 260, 330, 428, 526, 600};
    for (int it = 0; it < 60000; it++) {
        set_av(rand_avail(), rnd() & 1);
        flex_qs_cfg_t c;
        rand_cfg(&c);
        int gpx = (rnd() & 1) ? GPX[rnd() % 9] : (int)(rnd() % 700);
        bool edit = rnd() & 1;
        int16_t rb[(FLEX_QS_MAX_ITEMS + 2) * 6];
        uint8_t rt[FLEX_QS_MAX_ITEMS];
        int rtn, rch, rgb;
        uint8_t items[FLEX_QS_MAX_ITEMS * 5];
        memcpy(items, c.it, sizeof(items));
        int rn = ref_qs_layout(items, c.n, gpx, edit, rb, rt, &rtn, &rch, &rgb);
        flex_qs_layout_t L;
        flex_qs_layout(&L, c.it, c.n, gpx, edit, shown);
        bool same = rn == L.blk_n && rtn == L.tile_n && rch == L.content_h && rgb == L.group_blk &&
                    memcmp(rt, L.tiles, (size_t)rtn) == 0;
        for (int i = 0; same && i < rn; i++) {
            const int16_t *o = rb + i * 6;
            same = o[0] == L.blk[i].kind && o[1] == L.blk[i].item && o[2] == L.blk[i].x && o[3] == L.blk[i].y &&
                   o[4] == L.blk[i].w && o[5] == L.blk[i].h;
        }
        SAME(same);

        // qpRelayout (panel o edicion): alto de la tarjeta acotado y scroll maximo
        float gh = (float)(rnd() % 700) + (float)(rnd() % 100) / 100.0f, gh_out;
        int rch2, rsm;
        int rpx = ref_qs_relayout(items, c.n, edit, gh, c.grows, &gh_out, &rch2, &rsm);
        int want = edit ? flex_qp_group_h(c.grows) : (gh <= 0 ? flex_qp_group_h(c.grows) : (int)(gh + 0.5f));
        int px = flex_qs_relayout(&L, c.it, c.n, want, edit, shown);
        SAME(px == rpx && L.content_h == rch2 && flex_qs_scroll_max(&L) == rsm);

        // "snap" del alto de la tarjeta al soltar el asa
        if (!edit) {
            uint8_t rr = 0, mr = 0;
            uint32_t rd;
            float rto = ref_qs_group_snap(items, c.n, gh, &rr, &rd);
            flex_qs_relayout(&L, c.it, c.n, (int)(gh + 0.5f), false, shown);
            mr = 3;   // la referencia parte de qpGrows = 3
            int mto = flex_qs_group_snap(&L, gh, &mr);
            SAME((float)mto == rto && mr == rr && flex_qs_group_snap_ms(gh, (float)mto) == rd);
        }
    }
    // "snap" exhaustivo con alturas enteras: los empates (a mitad de dos filas)
    // se resuelven igual que Arduino (gana la fila mas baja)
    set_av(0xFFFFFFu, true);
    static const uint8_t ONE[] = {FLEX_QS_THEME, FLEX_QS_POWERSAVE, FLEX_QS_GLASS, FLEX_QS_CRONO, FLEX_QS_NTP,
                                  FLEX_QS_DND, FLEX_QS_LOCK, FLEX_QS_FILES, FLEX_QS_CAMERA, FLEX_QS_GALLERY,
                                  FLEX_QS_SETTINGS, FLEX_QS_WIFI, FLEX_QS_AIRPLANE, FLEX_QS_CONN, FLEX_QS_DEX,
                                  FLEX_QS_OTA, FLEX_QS_MUTE, FLEX_QS_BLE};
    for (int k = 0; k <= 18; k++) {
        flex_qs_cfg_t c;
        memset(&c, 0, sizeof(c));
        for (int i = 0; i < k; i++) {
            c.it[c.n++] = (flex_qs_item_t){ONE[i], 1, 1, FLEX_QOR_H, 1};
        }
        c.it[c.n++] = (flex_qs_item_t){FLEX_QS_BRIGHT, 4, 1, FLEX_QOR_H, 1};
        uint8_t items[FLEX_QS_MAX_ITEMS * 5];
        memcpy(items, c.it, sizeof(items));
        for (int gh = 1; gh < 700; gh++) {
            uint8_t rr = 0, mr = 0;
            uint32_t rd;
            float rto = ref_qs_group_snap(items, c.n, (float)gh, &rr, &rd);
            flex_qs_layout_t L;
            flex_qs_relayout(&L, c.it, c.n, gh, false, shown);
            int mto = flex_qs_group_snap(&L, (float)gh, &mr);
            SAME((float)mto == rto && mr == rr && flex_qs_group_snap_ms((float)gh, (float)mto) == rd);
        }
    }
    for (int k = 0; k < 40; k++) {
        int a, b, c, d;
        ref_qs_tile_center(k, 202 + k, &a, &b);
        flex_qs_tile_center(k, 202 + k, &c, &d);
        CHECK(a == c && b == d);
    }
}

static void t_edit_catalog(void)
{
    for (int it = 0; it < 40000; it++) {
        set_av(rand_avail(), rnd() & 1);
        flex_qs_cfg_t c;
        rand_cfg(&c);
        uint8_t items[FLEX_QS_MAX_ITEMS * 5];
        for (int step = 0; step < 12; step++) {
            int op = (int)(rnd() % 3);
            int a = op == 2 ? (int)(rnd() % (FLEX_QS_COUNT + 2)) : (int)(rnd() % 27) - 1;
            int b = (int)(rnd() % 27) - 1;
            memcpy(items, c.it, sizeof(items));
            uint8_t rn = c.n;
            bool r1 = ref_qs_edit(items, &rn, op, a, b);
            bool r2 = true;
            if (op == 0) {
                r2 = flex_qs_edit_remove(&c, a);
            } else if (op == 1) {
                flex_qs_edit_move(&c, a, b);
            } else {
                r2 = flex_qs_edit_add(&c, a, shown);
            }
            SAME(r1 == r2 && rn == c.n && memcmp(items, c.it, sizeof(items)) == 0);
            uint8_t o1[FLEX_QS_COUNT], o2[FLEX_QS_COUNT];
            int n1 = ref_qs_catalog(items, rn, o1);
            int n2 = flex_qs_catalog(&c, o2, shown);
            SAME(n1 == n2 && memcmp(o1, o2, (size_t)n1) == 0);
        }
    }
}

// Coordenadas de fabrica de docs/spec/01b §5.4 (todo disponible, con audio)
static void t_factory_positions(void)
{
    set_av(0xFFFFFFu, true);
    flex_qs_cfg_t c;
    flex_qs_factory(&c, av);
    c.n = flex_qs_normalize(c.it, c.n, &c.grows, av);
    flex_qs_layout_t L;
    flex_qs_relayout(&L, c.it, c.n, flex_qp_group_h(c.grows), false, shown);
    struct { int id, x, y, w, h; } want[] = {
        {FLEX_QS_WIFI, 16, 0, 218, 74},   {FLEX_QS_AIRPLANE, 246, 0, 218, 74}, {-1, 16, 86, 448, 330},
        {FLEX_QS_BRIGHT, 16, 428, 448, 74}, {FLEX_QS_VOLUME, 16, 514, 448, 74}, {FLEX_QS_DEX, 16, 600, 218, 74},
        {FLEX_QS_OTA, 246, 600, 218, 74}, {FLEX_QS_CONN, 16, 686, 218, 74},
    };
    CHECK_EQ_I(L.blk_n, 8);
    for (int i = 0; i < 8 && i < L.blk_n; i++) {
        const flex_qs_block_t *k = &L.blk[i];
        if (want[i].id >= 0) {
            CHECK(k->kind == FLEX_QB_ITEM && c.it[k->item].id == want[i].id);
        } else {
            CHECK(k->kind == FLEX_QB_GROUP);
        }
        // en pantalla el contenido empieza en y = 116 (cabecera fija)
        CHECK(k->x == want[i].x && k->y + FLEX_QP_HDR_H == want[i].y + 116 && k->w == want[i].w && k->h == want[i].h);
    }
    CHECK_EQ_I(L.tile_n, 11);
    CHECK_EQ_I(flex_qs_scroll_max(&L), 110);
    CHECK_EQ_I(c.grows, 3);
}

// Politica de ESP-IDF: lo que se conserva (keep) y lo que se ve (shown) son
// preguntas distintas. "Bloquear" sin PIN sigue en la configuracion.
static bool keep_all(int id)
{
    return id != FLEX_QS_BLE && id != FLEX_QS_RETIRED_10 && id != FLEX_QS_RETIRED_13;
}
static bool shown_no_lock(int id)
{
    return keep_all(id) && id != FLEX_QS_LOCK;
}
static void t_keep_vs_shown(void)
{
    flex_qs_cfg_t c;
    flex_qs_factory(&c, keep_all);
    bool has_lock = false;
    for (int i = 0; i < c.n; i++) {
        has_lock |= c.it[i].id == FLEX_QS_LOCK;
    }
    CHECK(has_lock);
    flex_qs_layout_t L;
    flex_qs_layout(&L, c.it, c.n, flex_qp_group_h(3), false, shown_no_lock);
    for (int k = 0; k < L.tile_n; k++) {
        CHECK(c.it[L.tiles[k]].id != FLEX_QS_LOCK);
    }
    uint8_t b[FLEX_QS_BLOB_N];
    flex_qs_serialize(&c, b);
    flex_qs_cfg_t d;
    CHECK(flex_qs_deserialize(&d, b, keep_all));
    CHECK(d.n == c.n && memcmp(d.it, c.it, (size_t)c.n * 5) == 0);
    // blob v1 sin "No molestar": se adopta al final
    uint8_t v1[FLEX_QS_BLOB_N];
    flex_qs_cfg_t e = c;
    for (int i = 0; i < e.n; i++) {
        if (e.it[i].id == FLEX_QS_DND) {
            flex_qs_edit_remove(&e, i);
            break;
        }
    }
    flex_qs_serialize(&e, v1);
    v1[1] = 1;
    CHECK(flex_qs_deserialize(&d, v1, keep_all));
    CHECK(d.n == c.n && d.it[d.n - 1].id == FLEX_QS_DND);
}

void test_qs(void)
{
    t_registry();
    t_factory_blob_load();
    t_layout();
    t_edit_catalog();
    t_factory_positions();
    t_keep_vs_shown();
    CHECK_EQ_I(s_diff, 0);
    printf("panel rapido: %s\n", s_diff ? "DISTINTO de Arduino" : "130000 casos iguales a Arduino");
}
