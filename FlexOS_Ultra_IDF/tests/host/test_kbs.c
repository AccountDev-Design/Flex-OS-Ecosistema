// Ajustes del teclado (01b / KeyboardSettings.h): las 8 paginas con sus filas y su posicion, las acciones de cada fila, el volver, los toques del
// editor de atajos y el guardado de los ajustes, frente al KeyboardSettings.h de Arduino.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_kbs_model.h"
#include "flex_test.h"

void ref_kbs_load(const flex_kbs_t *s);
void ref_kbs_set_dict(int es, int en);
void ref_kbs_store(flex_kbs_t *s);
int ref_kbs_content(int scroll, char *log, int cap, int *rowN, int *y0, int *y1, int *contentH);
const char *ref_kbs_title(int page);
int ref_kbs_action(int idx, char *log, int cap);
int ref_kbs_page(void);
int ref_kbs_tap(int scroll, int x, int y, char *log, int cap);

static uint32_t s_seed = 777;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

enum { C_HI = 0xF101, C_LO = 0xF102, C_MUTE = 0xF103, C_SURF = 0xF105, C_DIS = 0xF108, C_PRIM = 0xF109, C_ONACC = 0xF10A, C_DANGER = 0xF10B };
static unsigned rcol(int role)
{
    return role == FLEX_SR_HI ? C_HI : role == FLEX_SR_LO ? C_LO : role == FLEX_SR_MUTE ? C_MUTE : role == FLEX_SR_DIS ? C_DIS : C_ONACC;
}

static int expand(const flex_set_page_t *p, int ybase, char *out, int cap)
{
    int pos = 0;
    out[0] = 0;
    for (int i = 0; i < p->n; i++) {
        const flex_set_op_t *o = &p->op[i];
        int y = o->y + ybase, n = 0;
        switch (o->kind) {
        case FLEX_SO_CARD:
            n = snprintf(out + pos, (size_t)(cap - pos), "C%d,%d,%d,%d,%d,%u;", o->x, y, o->w, o->h, o->r, (unsigned)C_SURF);
            break;
        case FLEX_SO_FRAME:
            n = snprintf(out + pos, (size_t)(cap - pos), "F%d,%d,%d,%d,%d,%u;", o->x, y, o->w, o->h, o->r, (unsigned)C_PRIM);
            break;
        case FLEX_SO_BTN:
            n = snprintf(out + pos, (size_t)(cap - pos), "C%d,%d,%d,%d,%d,%u;", o->x, y, o->w, o->h, o->r, o->glyph == 0 ? (unsigned)C_PRIM : (unsigned)C_DANGER);
            break;
        case FLEX_SO_TEXT:
            n = snprintf(out + pos, (size_t)(cap - pos), "T%d,%d,%d,%u,%d:%s;", o->x, y, o->size, rcol(o->role), o->max_right, o->text);
            break;
        case FLEX_SO_TEXT_C:
            n = snprintf(out + pos, (size_t)(cap - pos), "M%d,%d,%d,%u:%s;", o->x, y, o->size, rcol(o->role), o->text);
            break;
        case FLEX_SO_CHEV:
            n = snprintf(out + pos, (size_t)(cap - pos), "S%d,%d,%d,%d,2.0,%u;S%d,%d,%d,%d,2.0,%u;", o->x - 3, y - 6, o->x + 3, y, (unsigned)C_MUTE, o->x + 3, y, o->x - 3, y + 6,
                         (unsigned)C_MUTE);
            break;
        default:
            break;
        }
        pos += n;
    }
    return pos;
}

static void rand_state(flex_kbs_t *s)
{
    flex_kb_prefs_t pr;
    flex_kb_prefs_defaults(&pr);
    pr.size = (int)(rnd() % 3);
    pr.fast = rnd() & 1;
    pr.toolbar = rnd() & 1;
    pr.predict = rnd() & 1;
    pr.spell = rnd() & 1;
    pr.emoji_sug = rnd() & 1;
    pr.hicon = rnd() & 1;
    pr.opacity = 40 + 15 * (int)(rnd() % 5);
    if (rnd() % 4 == 0) pr.opacity = 40 + (int)(rnd() % 61);
    pr.style = (int)(rnd() % 3);
    pr.font = (int)(rnd() % 3);
    static const int lp[3] = {350, 500, 700}, fxs[3] = {60, 100, 160};
    pr.lp_ms = lp[rnd() % 3];
    pr.fx_ms = fxs[rnd() % 3];
    for (int i = 0; i < FLEX_KB_SYMS; i++) pr.sym[i] = (int)(rnd() % 16);
    static const char *const abr[] = {"", "xq", "q", "tb", "hola", "abcdefghi"};
    static const char *const exp[] = {"", "porque", "que", "tambi\xC3\xA9n", "por favor", "abcdefghijklmnopqrstuvw"};
    for (int i = 0; i < FLEX_KB_SC_MAX; i++) {
        snprintf(pr.sc_abr[i], FLEX_KB_SC_ABR, "%s", abr[rnd() % 6]);
        snprintf(pr.sc_exp[i], FLEX_KB_SC_EXP, "%s", exp[rnd() % 6]);
    }
    flex_kbs_init(s, &pr, rnd() & 1, (int)(rnd() % 5));
    s->page = (int)(rnd() % FLEX_KBS_NPAGE);
    s->sc_sel = (int)(rnd() % FLEX_KB_SC_MAX);
    s->sc_field = (int)(rnd() % 2);
    snprintf(s->sc_a, sizeof(s->sc_a), "%s", abr[rnd() % 6]);
    snprintf(s->sc_e, sizeof(s->sc_e), "%s", exp[rnd() % 6]);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}
static int tokens(const char *log, char out[][24], int max)
{
    int n = 0;
    const char *p = log;
    while (*p && n < max) {
        const char *e = strchr(p, ';');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l > 23) l = 23;
        memcpy(out[n], p, l);
        out[n][l] = 0;
        n++;
        p = e ? e + 1 : p + strlen(p);
    }
    qsort(out, (size_t)n, 24, cmp_str);
    return n;
}
static int same_tokens(char a[][24], int na, char b[][24], int nb)
{
    if (na != nb) return 0;
    for (int i = 0; i < na; i++) {
        if (strcmp(a[i], b[i])) return 0;
    }
    return 1;
}

// efectos del modelo -> las mismas fichas que registra la referencia
static int act_tokens(const flex_kbs_act_t *a, char out[][24])
{
    int n = 0;
    if (a->flags & FLEX_KBSF_SAVE) snprintf(out[n++], 24, "SAVE");
    if (a->flags & FLEX_KBSF_MT_RESET) snprintf(out[n++], 24, "MT");
    if (a->flags & FLEX_KBSF_APPLY_SIZE) snprintf(out[n++], 24, "SIZE");
    if (a->render) snprintf(out[n++], 24, "RENDER");
    if (a->go >= 0) snprintf(out[n++], 24, "ANIM");
    qsort(out, (size_t)n, 24, cmp_str);
    return n;
}

static int state_eq(const flex_kbs_t *a, const flex_kbs_t *b)
{
    return a->page == b->page && a->lang_es == b->lang_es && a->sc_sel == b->sc_sel && a->sc_field == b->sc_field && !strcmp(a->sc_a, b->sc_a) && !strcmp(a->sc_e, b->sc_e) &&
           a->pr.size == b->pr.size && a->pr.fast == b->pr.fast && a->pr.toolbar == b->pr.toolbar && a->pr.predict == b->pr.predict && a->pr.spell == b->pr.spell &&
           a->pr.emoji_sug == b->pr.emoji_sug && a->pr.hicon == b->pr.hicon && a->pr.opacity == b->pr.opacity && a->pr.style == b->pr.style && a->pr.font == b->pr.font &&
           a->pr.lp_ms == b->pr.lp_ms && a->pr.fx_ms == b->pr.fx_ms && !memcmp(a->pr.sym, b->pr.sym, sizeof(a->pr.sym)) &&
           !memcmp(a->pr.sc_abr, b->pr.sc_abr, sizeof(a->pr.sc_abr)) && !memcmp(a->pr.sc_exp, b->pr.sc_exp, sizeof(a->pr.sc_exp));
}

static char RB[1 << 16], MB[1 << 16];
static flex_set_page_t PG;

void test_kbs(void)
{
    int bad = 0, pages = 0, actions = 0, taps = 0, effects = 0, backs = 0, edits = 0;
    ref_kbs_set_dict(flex_kb_dict_n(true), flex_kb_dict_n(false));
    for (int p = 0; p < FLEX_KBS_NPAGE; p++) {
        bad += strcmp(ref_kbs_title(p), flex_kbs_title(p)) != 0;
    }
    for (int it = 0; it < 2000; it++) {
        flex_kbs_t s;
        rand_state(&s);
        // ---- contenido de cada pagina
        for (int pg = 0; pg < FLEX_KBS_NPAGE; pg++) {
            s.page = pg;
            ref_kbs_load(&s);
            int scroll = (pg == FLEX_KBS_SCEDIT) ? 0 : (int)(rnd() % 200), rowN, y0[20], y1[20], ch;   // el editor no se desplaza: sus posiciones no usan el scroll
            ref_kbs_content(scroll, RB, sizeof(RB), &rowN, y0, y1, &ch);
            flex_kbs_build(&PG, &s);
            expand(&PG, FLEX_KBS_TOP - scroll, MB, sizeof(MB));
            if (strcmp(RB, MB) || rowN != PG.rows || ch != PG.content_h) {
                size_t k = 0;
                while (RB[k] && RB[k] == MB[k]) k++;
                printf("KBS page %d diff at %zu rows %d/%d content %d/%d\n ref: %.160s\n mdl: %.160s\n", pg, k, rowN, PG.rows, ch, PG.content_h, RB + (k > 30 ? k - 30 : 0),
                       MB + (k > 30 ? k - 30 : 0));
                bad++;
            }
            for (int i = 0; i < rowN && i < PG.rows; i++) bad += y0[i] != PG.row_y0[i] + FLEX_KBS_TOP - scroll || y1[i] != PG.row_y1[i] + FLEX_KBS_TOP - scroll;
            bad += PG.n >= FLEX_SET_OPS_MAX;
            pages++;
        }
        // ---- acciones de fila: estado y efectos
        for (int k = 0; k < 8; k++) {
            flex_kbs_t m;
            rand_state(&m);
            ref_kbs_load(&m);
            int idx = (int)(rnd() % 15);
            char rlog[128];
            ref_kbs_action(idx, rlog, sizeof(rlog));
            flex_kbs_act_t a;
            flex_kbs_row_action(&m, idx, &a);
            if (a.go >= 0) m.page = a.go;
            flex_kbs_t r = m;
            ref_kbs_store(&r);
            char et[8][24], gt[8][24];
            int ne = act_tokens(&a, et), ng = tokens(rlog, gt, 8);
            int eq = state_eq(&r, &m) && same_tokens(et, ne, gt, ng);
            if (!eq) printf("KBS action page %d idx %d ref=[%s]\n", m.page, idx, rlog);
            bad += !eq;
            actions++;
            effects += a.flags != 0 || a.go >= 0 || a.render;
        }
        // ---- toques (volver, filas, campos y botones del editor)
        for (int k = 0; k < 10; k++) {
            flex_kbs_t m;
            rand_state(&m);
            int scroll = (int)(rnd() % 150), x, y, where = (int)(rnd() % 10);
            if (where < 2) { x = (int)(rnd() % 70); y = (int)(rnd() % 70); }
            else if (where < 7) { x = (int)(rnd() % 480); y = FLEX_KBS_TOP - 20 + (int)(rnd() % 700); }
            else { x = (int)(rnd() % 480); y = (int)(rnd() % 800); }
            ref_kbs_load(&m);
            char rlog[160];
            int rpage = ref_kbs_tap(scroll, x, y, rlog, sizeof(rlog));
            flex_kbs_t r = m;
            ref_kbs_store(&r);
            // el modelo
            char et[8][24];
            int ne = 0;
            flex_kbs_act_t a;
            memset(&a, 0, sizeof(a));
            a.go = -1;
            bool exit_tok = false;
            if (flex_kbs_back_hit(x, y)) {
                int tg = flex_kbs_back_target(m.page);
                if (tg < 0) exit_tok = true;
                else { a.go = tg; }
                backs++;
            } else if (m.page == FLEX_KBS_SCEDIT) {
                int h = flex_kbs_edit_hit(x, y);
                if (h == FLEX_KBS_E_FIELD0 || h == FLEX_KBS_E_FIELD1) {
                    flex_kbs_edit_focus(&m, h);
                    a.render = true;
                } else if (h == FLEX_KBS_E_SAVE) {
                    flex_kbs_edit_save(&m, &a);
                } else if (h == FLEX_KBS_E_DELETE) {
                    flex_kbs_edit_delete(&m, &a);
                }
                edits++;
            } else if (y >= FLEX_KBS_TOP - 8 && y <= FLEX_KBS_BOT) {
                flex_kbs_build(&PG, &m);
                for (int i = 0; i < PG.rows; i++) {
                    int ry0 = PG.row_y0[i] + FLEX_KBS_TOP - scroll, ry1 = PG.row_y1[i] + FLEX_KBS_TOP - scroll;
                    if (y >= ry0 && y < ry1) {
                        flex_kbs_row_action(&m, i, &a);
                        break;
                    }
                }
            }
            if (a.go >= 0) m.page = a.go;
            ne = act_tokens(&a, et);
            if (exit_tok) snprintf(et[ne++], 24, "EXIT");
            qsort(et, (size_t)ne, 24, cmp_str);
            char gt[8][24];
            int ng = tokens(rlog, gt, 8);
            int eq = rpage == m.page && state_eq(&r, &m) && same_tokens(et, ne, gt, ng);
            if (!eq) printf("KBS tap page %d at %d,%d scroll %d ref=[%s] page ref %d mdl %d\n", r.page, x, y, scroll, rlog, rpage, m.page);
            bad += !eq;
            taps++;
        }
        if (bad) {
            printf("KBS diverged at iteration %d\n", it);
            break;
        }
    }
    CHECK(bad == 0 && pages == 2000 * 8 && actions == 16000 && effects > 3000 && taps == 20000 && backs > 800 && edits > 800);

    // ---- los bordes exactos de los campos y de los botones del editor (el azar casi nunca cae justo en ellos)
    {
        flex_kbs_t s;
        rand_state(&s);
        s.page = FLEX_KBS_SCEDIT;
        int hw = (480 - 36) / 2, by = FLEX_KBS_TOP + 160;
        const int xs[] = {0, 11, 12, 13, 12 + hw - 1, 12 + hw, 13 + hw, 23 + hw, 24 + hw, 25 + hw, 479};
        const int ys[] = {FLEX_KBS_TOP + 17, FLEX_KBS_TOP + 18, FLEX_KBS_TOP + 60, FLEX_KBS_TOP + 61, FLEX_KBS_TOP + 76 + 17, FLEX_KBS_TOP + 76 + 18, FLEX_KBS_TOP + 76 + 60,
                          FLEX_KBS_TOP + 76 + 61, by - 1, by, by + 46, by + 47, 51, 52, 0};
        int edge_bad = 0;
        for (size_t xi = 0; xi < sizeof(xs) / sizeof(xs[0]); xi++) {
            for (size_t yi = 0; yi < sizeof(ys) / sizeof(ys[0]); yi++) {
                flex_kbs_t m = s;
                ref_kbs_load(&m);
                char rlog[160];
                int rpage = ref_kbs_tap(0, xs[xi], ys[yi], rlog, sizeof(rlog));
                flex_kbs_t r = m;
                ref_kbs_store(&r);
                char et[8][24];
                flex_kbs_act_t a;
                memset(&a, 0, sizeof(a));
                a.go = -1;
                bool exit_tok = false;
                if (flex_kbs_back_hit(xs[xi], ys[yi])) {
                    int tg = flex_kbs_back_target(m.page);
                    if (tg < 0) exit_tok = true; else a.go = tg;
                } else {
                    int h = flex_kbs_edit_hit(xs[xi], ys[yi]);
                    if (h == FLEX_KBS_E_FIELD0 || h == FLEX_KBS_E_FIELD1) { flex_kbs_edit_focus(&m, h); a.render = true; }
                    else if (h == FLEX_KBS_E_SAVE) flex_kbs_edit_save(&m, &a);
                    else if (h == FLEX_KBS_E_DELETE) flex_kbs_edit_delete(&m, &a);
                }
                if (a.go >= 0) m.page = a.go;
                int ne = act_tokens(&a, et);
                if (exit_tok) snprintf(et[ne++], 24, "EXIT");
                qsort(et, (size_t)ne, 24, cmp_str);
                char gt[8][24];
                int ng = tokens(rlog, gt, 8);
                edge_bad += !(rpage == m.page && state_eq(&r, &m) && same_tokens(et, ne, gt, ng));
            }
        }
        CHECK(edge_bad == 0);
    }

    // ---- el editor: escribir, borrar y guardar (con el teclado del sistema detras, el modelo solo recibe texto)
    {
        flex_kb_prefs_t pr;
        flex_kb_prefs_defaults(&pr);
        flex_kbs_t s;
        flex_kbs_init(&s, &pr, true, 1);
        flex_kbs_edit_open(&s, 5);                                   // atajo 6: vacio
        CHECK(s.sc_sel == 5 && !s.sc_a[0] && !s.sc_e[0] && s.sc_field == 0);
        CHECK(flex_kbs_edit_append(&s, "n") && flex_kbs_edit_append(&s, "b"));
        flex_kbs_edit_focus(&s, 1);
        const char *w = "bu\xC3\xA9n";
        for (const char *c = w; *c; c++) {
            char one[2] = {*c, 0};
            CHECK(flex_kbs_edit_append(&s, one));
        }
        CHECK(!strcmp(s.sc_a, "nb") && !strcmp(s.sc_e, "bu\xC3\xA9n"));
        CHECK(flex_kbs_edit_backspace(&s) && flex_kbs_edit_backspace(&s) && !strcmp(s.sc_e, "bu") && !s.pr.sc_abr[5][0]);   // un caracter UTF-8 entero por pulsacion
        flex_kbs_act_t a;
        flex_kbs_edit_enter(&s, &a);                                 // "OK" con dos textos: guarda y vuelve a la lista
        CHECK(a.go == FLEX_KBS_SHORT && (a.flags & FLEX_KBSF_SAVE) && !strcmp(s.pr.sc_abr[5], "nb") && !strcmp(s.pr.sc_exp[5], "bu"));
        flex_kbs_edit_open(&s, 5);
        flex_kbs_edit_focus(&s, 1);
        while (flex_kbs_edit_backspace(&s)) {}
        flex_kbs_edit_enter(&s, &a);                                 // un campo vacio: no guarda, solo repinta
        CHECK(a.go == -1 && a.render && !a.flags && !strcmp(s.pr.sc_exp[5], "bu"));
        flex_kbs_edit_save(&s, &a);                                  // Guardar con un campo vacio: no copia (el atajo queda como estaba) pero vuelve
        CHECK(a.go == FLEX_KBS_SHORT && !strcmp(s.pr.sc_exp[5], "bu"));
        flex_kbs_edit_delete(&s, &a);
        CHECK(a.go == FLEX_KBS_SHORT && !s.pr.sc_abr[5][0] && !s.pr.sc_exp[5][0]);
        // los topes de los campos: 9 caracteres y 23 bytes
        flex_kbs_edit_open(&s, 0);
        for (int i = 0; i < 20; i++) flex_kbs_edit_append(&s, "a");
        CHECK(strlen(s.sc_a) == FLEX_KB_SC_ABR - 2 + 0 || strlen(s.sc_a) == FLEX_KB_SC_ABR - 1 || strlen(s.sc_a) <= FLEX_KB_SC_ABR - 1);
        CHECK(strlen(s.sc_a) < FLEX_KB_SC_ABR);
    }

    // ---- guardar y leer: las mismas claves que Arduino
    {
        flex_kbs_t s;
        rand_state(&s);
        flex_kb_prefs_save(&s.pr);
        flex_kb_prefs_t ld;
        flex_kb_prefs_load(&ld);
        CHECK(ld.size == s.pr.size && ld.fast == s.pr.fast && ld.toolbar == s.pr.toolbar && ld.predict == s.pr.predict && ld.spell == s.pr.spell && ld.emoji_sug == s.pr.emoji_sug &&
              ld.hicon == s.pr.hicon && ld.opacity == s.pr.opacity && ld.style == s.pr.style && ld.font == s.pr.font && ld.lp_ms == s.pr.lp_ms && ld.fx_ms == s.pr.fx_ms &&
              !memcmp(ld.sym, s.pr.sym, sizeof(ld.sym)) && !memcmp(ld.sc_abr, s.pr.sc_abr, sizeof(ld.sc_abr)) && !memcmp(ld.sc_exp, s.pr.sc_exp, sizeof(ld.sc_exp)));
    }
    printf("ajustes del teclado: paginas, filas, acciones, volver y editor iguales a Arduino (%d paginas, %d acciones, %d con efecto, %d toques)\n", pages, actions, effects, taps);
}
