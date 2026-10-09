// Teclado del sistema: la logica pura de flex_kb_layout.c contra el codigo de
// la version Arduino extraido (ref/ref_kb_main.cpp), bit a bit.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "flex_kb_layout.h"
#include "flex_storage.h"
#include "flex_test.h"

void ref_kb_geom(int size, int bot, int extras, int toolbar, int predict, int layout, int out[25]);
int ref_kb_cell_at(int px, int py);
int ref_kb_frow_hit(int px, int py);
const char *ref_kb_base(int layout, int cell);
void ref_kb_resolve(int layout, int shift, int cell, int consume, char res[8], int *shift_after);
void ref_kb_fn(int layout, int shift, int lang_es, int fi, int out[3]);
const char *ref_kb_layer_label(int layout);
void ref_kb_prefs_normalize(int v[10]);
void ref_kb_font(int font_sc, int style, int out[3]);
const char *ref_kb_sym_at(const int sym[4], int i);
void ref_kb_shortcuts(char abr[8][10], char exp[8][24]);
void ref_kb_fx_start(int cell, uint32_t now, int fx_ms);
int ref_kb_fx_level(int cell, uint32_t now, int fx_ms);
void ref_kb_fx_reset(void);
int ref_kb_suggest(int lang_es, int predict, int emoji, const char *pref, const char *out[4], int maxn);
int ref_kb_dict_has(int lang_es, const char *w);
int ref_kb_dict_n(int lang_es);
const char *ref_kb_dict_word(int lang_es, int i);
int ref_kb_current_word(const char *buf, int cur, char *out, int outsz);
int ref_kb_variants(char b, const char *var[4]);
int ref_kb_is_vowel(int layout, int cell);
int ref_kb_utf8_prev(const char *s, int i);
int ref_kb_utf8_count(const char *s);
void ref_kb_pass_reset(void);
void ref_kb_pass_append(const char *s);
void ref_kb_pass_back(void);
const char *ref_kb_pass(void);
int ref_kb_pass_cap(void);
void ref_kb_field_reset(int field);
void ref_kb_field_append(const char *s);
void ref_kb_field_back(void);
const char *ref_kb_field(void);
void test_cfg_reset(void);

static uint32_t s_seed = 4242;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

// ---- geometria y hit-test --------------------------------------------------------
static void geom_case(int size, int bot, int extras, int toolbar, int predict, int layout, bool full_scan)
{
    int ref[25];
    ref_kb_geom(size, bot, extras, toolbar, predict, layout, ref);
    int tb = flex_kb_toolbar_h(extras, toolbar);
    int ch = flex_kb_chips_h(extras, layout, predict);
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, size, bot, tb + ch);
    int k = 0;
    CHECK_EQ_I(g.kw, ref[k++]);
    CHECK_EQ_I(g.kh, ref[k++]);
    CHECK_EQ_I(g.gap, ref[k++]);
    CHECK_EQ_I(g.x, ref[k++]);
    CHECK_EQ_I(flex_kb_rows_top(&g), ref[k++]);
    CHECK_EQ_I(flex_kb_panel_top(&g), ref[k++]);
    CHECK_EQ_I(flex_kb_toolbar_y(&g), ref[k++]);
    CHECK_EQ_I(flex_kb_chips_y(&g, tb), ref[k++]);
    CHECK_EQ_I(flex_kb_func_y(&g), ref[k++]);
    CHECK_EQ_I(tb + ch, ref[k++]);
    CHECK_EQ_I(tb, ref[k++]);
    CHECK_EQ_I(ch, ref[k++]);
    CHECK_EQ_I(flex_kb_size_check(&g), ref[k++]);
    for (int i = 0; i < FLEX_KB_FKEYS; i++) {
        CHECK_EQ_I(flex_kb_fkey_x(&g, i), ref[k++]);
    }
    for (int i = 0; i < FLEX_KB_FKEYS; i++) {
        CHECK_EQ_I(flex_kb_fkey_w(&g, i), ref[k++]);
    }
    // Hit-test: toda la pantalla (o una muestra) da la misma tecla que Arduino.
    int bad_cell = 0, bad_fn = 0, n = 0;
    if (full_scan) {
        for (int y = -4; y < FLEX_KB_SCR_H + 4; y++) {
            for (int x = -4; x < FLEX_KB_SCR_W + 4; x++) {
                bad_cell += flex_kb_cell_at(&g, x, y) != ref_kb_cell_at(x, y);
                bad_fn += flex_kb_frow_hit(&g, x, y) != ref_kb_frow_hit(x, y);
                n++;
            }
        }
    } else {
        for (int i = 0; i < 4000; i++) {
            int x = (int)(rnd() % 500) - 10, y = (int)(rnd() % 820) - 10;
            bad_cell += flex_kb_cell_at(&g, x, y) != ref_kb_cell_at(x, y);
            bad_fn += flex_kb_frow_hit(&g, x, y) != ref_kb_frow_hit(x, y);
            n++;
        }
    }
    CHECK_EQ_I(bad_cell, 0);
    CHECK_EQ_I(bad_fn, 0);
    (void)n;
}

static void test_geometry(void)
{
    for (int size = -1; size <= 3; size++) {
        for (int bot = 0; bot <= 64; bot += 64) {
            geom_case(size, bot, 0, 0, 0, 0, size >= 0 && size <= 2);
            for (int extras = 0; extras <= 1; extras++) {
                for (int tb = 0; tb <= 1; tb++) {
                    for (int pr = 0; pr <= 1; pr++) {
                        for (int lay = 0; lay < 4; lay++) {
                            geom_case(size, bot, extras, tb, pr, lay, false);
                        }
                    }
                }
            }
        }
    }
    // Valores de referencia legibles (docs/spec/01b §Teclado del sistema)
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 0, 0);
    CHECK_EQ_I(g.x, 6);
    CHECK_EQ_I(flex_kb_rows_top(&g), 546);
    CHECK_EQ_I(flex_kb_panel_top(&g), 542);
    CHECK_EQ_I(flex_kb_func_y(&g), 732);
    CHECK_EQ_I(flex_kb_fkey_x(&g, 3), 181);
    CHECK_EQ_I(flex_kb_fkey_w(&g, 3), 192);
    flex_kb_geom_init(&g, FLEX_KB_SIZE_COMPACT, 0, 0);
    CHECK_EQ_I(g.x, 7);
    CHECK_EQ_I(flex_kb_rows_top(&g), 578);
    flex_kb_geom_init(&g, FLEX_KB_SIZE_BIG, 64, 88);
    CHECK_EQ_I(flex_kb_rows_top(&g), 434);
    CHECK_EQ_I(flex_kb_panel_top(&g), 342);
    CHECK(flex_kb_size_check(&g));
    // Ningun ancho de tecla de funcion cae cerca de .5: el resultado no depende
    // de si el compilador contrae la expresion float a FMA.
    for (int size = 0; size <= 2; size++) {
        flex_kb_geom_init(&g, size, 0, 0);
        int usable = flex_kb_grid_w(&g) - 5 * g.gap;
        static const double w[6] = {0.135, 0.125, 0.110, 0.420, 0.100, 0.110};
        for (int i = 0; i < 6; i++) {
            double v = usable * w[i], fr = v - floor(v);
            CHECK(fabs(fr - 0.5) > 0.01);
        }
    }
}

// ---- mapas, shift y fila de funciones ------------------------------------------------
static void test_layout_resolve(void)
{
    for (int lay = 0; lay < 4; lay++) {
        for (int cell = 0; cell < FLEX_KB_CELLS; cell++) {
            flex_kb_state_t st = {.layout = (uint8_t)lay, .shift = false, .lang_es = true};
            CHECK(strcmp(flex_kb_key_base(&st, cell), ref_kb_base(lay, cell)) == 0);
            for (int shift = 0; shift <= 1; shift++) {
                for (int consume = 0; consume <= 1; consume++) {
                    char out[6], ref[8];
                    int ref_shift;
                    st.shift = shift;
                    const char *r = flex_kb_resolve(&st, flex_kb_key_base(&st, cell), out, consume);
                    ref_kb_resolve(lay, shift, cell, consume, ref, &ref_shift);
                    CHECK(strcmp(r, ref) == 0);
                    CHECK_EQ_I(st.shift, ref_shift);
                }
            }
            CHECK_EQ_I(flex_kb_is_vowel_cell(&st, cell), ref_kb_is_vowel(lay, cell));
        }
        flex_kb_state_t st = {.layout = (uint8_t)lay};
        CHECK(strcmp(flex_kb_layer_label(&st), ref_kb_layer_label(lay)) == 0);
    }
    // n con tilde -> N con tilde; ( -> { solo en ?123
    flex_kb_state_t st = {.layout = FLEX_KB_LAYOUT_ES, .shift = true, .lang_es = true};
    char out[6];
    CHECK(strcmp(flex_kb_resolve(&st, "\xC3\xB1", out, true), "\xC3\x91") == 0);
    CHECK(!st.shift);
    // Fila de funciones: todos los estados y teclas
    for (int lay = 0; lay < 4; lay++) {
        for (int sh = 0; sh <= 1; sh++) {
            for (int le = 0; le <= 1; le++) {
                for (int fi = 0; fi < FLEX_KB_FKEYS; fi++) {
                    flex_kb_state_t s = {.layout = (uint8_t)lay, .shift = sh, .lang_es = le};
                    int ref[3];
                    ref_kb_fn(lay, sh, le, fi, ref);
                    CHECK_EQ_I(flex_kb_fn_apply(&s, fi), fi);
                    CHECK_EQ_I(s.layout, ref[0]);
                    CHECK_EQ_I(s.shift, ref[1]);
                    CHECK_EQ_I(s.lang_es, ref[2]);
                }
            }
        }
    }
    flex_kb_state_t s;
    flex_kb_state_init(&s, false);
    CHECK_EQ_I(s.layout, FLEX_KB_LAYOUT_EN);
    CHECK(strcmp(flex_kb_lang_label(&s), "EN") == 0);
    CHECK_EQ_I(flex_kb_fn_apply(&s, 7), -1);
    flex_kb_set_layout_state(&s, FLEX_KB_LAYOUT_ES);
    CHECK(s.lang_es);
    CHECK(strcmp(flex_kb_key_base(&s, 30), "") == 0);
}

// ---- preferencias -------------------------------------------------------------------
static void test_prefs(void)
{
    for (int it = 0; it < 3000; it++) {
        int v[10], ref[10];
        static const int lp[] = {350, 500, 700, 0, 499, 1000};
        static const int fx[] = {60, 100, 160, 0, 99, 500};
        v[0] = (int)(rnd() % 6) - 2;
        v[1] = (int)(rnd() % 6) - 2;
        v[2] = (int)(rnd() % 6) - 2;
        v[3] = (int)(rnd() % 160) - 20;
        v[4] = lp[rnd() % 6];
        v[5] = fx[rnd() % 6];
        for (int i = 0; i < 4; i++) {
            v[6 + i] = (int)(rnd() % 24) - 4;
        }
        memcpy(ref, v, sizeof(v));
        ref_kb_prefs_normalize(ref);
        flex_kb_prefs_t p;
        flex_kb_prefs_defaults(&p);
        p.size = v[0];
        p.style = v[1];
        p.font = v[2];
        p.opacity = v[3];
        p.lp_ms = v[4];
        p.fx_ms = v[5];
        for (int i = 0; i < 4; i++) {
            p.sym[i] = v[6 + i];
        }
        flex_kb_prefs_normalize(&p);
        CHECK_EQ_I(p.size, ref[0]);
        CHECK_EQ_I(p.style, ref[1]);
        CHECK_EQ_I(p.font, ref[2]);
        CHECK_EQ_I(p.opacity, ref[3]);
        CHECK_EQ_I(p.lp_ms, ref[4]);
        CHECK_EQ_I(p.fx_ms, ref[5]);
        for (int i = 0; i < 4; i++) {
            CHECK_EQ_I(p.sym[i], ref[6 + i]);
        }
    }
    for (int f = 0; f < 3; f++) {
        for (int s = 0; s < 3; s++) {
            int ref[3];
            ref_kb_font(f, s, ref);
            flex_kb_prefs_t p;
            flex_kb_prefs_defaults(&p);
            p.font = f;
            p.style = s;
            CHECK_EQ_I(flex_kb_font_size(&p), ref[0]);
            CHECK_EQ_I(flex_kb_font_dy(&p), ref[1]);
            CHECK_EQ_I(flex_kb_radius(&p), ref[2]);
        }
    }
    flex_kb_prefs_t p;
    flex_kb_prefs_defaults(&p);
    for (int it = 0; it < 200; it++) {
        int sym[4];
        for (int i = 0; i < 4; i++) {
            sym[i] = (int)(rnd() % 20) - 2;
            p.sym[i] = sym[i];
        }
        for (int i = -1; i <= 4; i++) {
            CHECK(strcmp(flex_kb_sym_at(&p, i), ref_kb_sym_at(sym, i)) == 0);
        }
    }
    char abr[8][10], exp[8][24];
    ref_kb_shortcuts(abr, exp);
    flex_kb_prefs_defaults(&p);
    CHECK(memcmp(abr, p.sc_abr, sizeof(abr)) == 0);
    CHECK(memcmp(exp, p.sc_exp, sizeof(exp)) == 0);

    // Carga desde la cache de ajustes: mismas claves y valores por defecto que Arduino
    test_cfg_reset();
    flex_kb_prefs_load(&p);
    CHECK_EQ_I(p.size, FLEX_KB_SIZE_NORMAL);
    CHECK(p.fast && p.toolbar && p.predict && !p.spell && !p.emoji_sug && !p.hicon);
    CHECK_EQ_I(p.opacity, 100);
    CHECK_EQ_I(p.fx_ms, 100);
    CHECK_EQ_I(p.lp_ms, 500);
    CHECK(strcmp(p.sc_exp[0], "porque") == 0);
    flex_cfg_set_i32("kbsize", FLEX_KB_SIZE_BIG);
    flex_cfg_set_bool("kbfast", false);
    flex_cfg_set_bool("kbhicon", true);
    flex_cfg_set_i32("kbopa", 7);
    flex_cfg_set_i32("kbfx", 160);
    flex_cfg_set_i32("kbstyle", 2);
    uint8_t sb[4] = {9, 8, 7, 6};
    flex_cfg_set_blob("kbsyms", sb, 4);
    flex_kb_prefs_load(&p);
    CHECK_EQ_I(p.size, FLEX_KB_SIZE_BIG);
    CHECK(!p.fast && p.hicon);
    CHECK_EQ_I(p.opacity, 40);
    CHECK_EQ_I(p.fx_ms, 160);
    CHECK_EQ_I(p.style, 2);
    CHECK_EQ_I(p.sym[0], 9);
    CHECK_EQ_I(p.sym[3], 6);
    test_cfg_reset();
}

// ---- destello por tiempo ------------------------------------------------------------------
static void test_fx(void)
{
    static const int fxs[3] = {60, 100, 160};
    for (int it = 0; it < 2000; it++) {
        int fx_ms = fxs[rnd() % 3];
        int cell = (int)(rnd() % 32) - 1;
        uint32_t t0 = rnd();
        flex_kb_fx_t fx;
        flex_kb_fx_reset(&fx);
        ref_kb_fx_reset();
        flex_kb_fx_start(&fx, cell, t0);
        ref_kb_fx_start(cell, t0, fx_ms);
        for (int q = 0; q < 6; q++) {
            uint32_t now = t0 + rnd() % 220;
            int c = (q & 1) ? cell : (int)(rnd() % 30);
            CHECK_EQ_I(flex_kb_fx_level(&fx, c, now, fx_ms), ref_kb_fx_level(c, now, fx_ms));
        }
    }
    flex_kb_fx_t fx;
    flex_kb_fx_reset(&fx);
    flex_kb_fx_start(&fx, 12, 1000);
    CHECK_EQ_I(flex_kb_fx_level(&fx, 12, 1000, 100), 255);
    CHECK_EQ_I(flex_kb_fx_tick(&fx, 1099, 100), -1);
    CHECK_EQ_I(flex_kb_fx_tick(&fx, 1100, 100), 12);
    CHECK_EQ_I(flex_kb_fx_tick(&fx, 1101, 100), -1);
    CHECK_EQ_I(flex_kb_fx_level(&fx, 12, 1050, 100), 0);
    // Animacion de entrada: 0.3 s lineal, (1 - p) * kbh truncado
    CHECK_EQ_I(flex_kb_slide_off(258, 0), 258);
    CHECK_EQ_I(flex_kb_slide_off(258, 150), 129);
    CHECK_EQ_I(flex_kb_slide_off(258, 299), (int)((1.0f - 299 / 300.0f) * 258));
    CHECK_EQ_I(flex_kb_slide_off(258, 300), 0);
    CHECK_EQ_I(flex_kb_slide_off(258, 5000), 0);
    CHECK(flex_kb_is_tap(15, -15, 549));
    CHECK(!flex_kb_is_tap(16, 0, 100));
    CHECK(!flex_kb_is_tap(0, -16, 100));
    CHECK(!flex_kb_is_tap(0, 0, 550));
}

// ---- texto UTF-8: borrar un caracter COMPLETO ----------------------------------------------
static const char *k_pieces[] = {"a", "Z", " ", "\xC3\xB1", "\xC3\x91", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", ":'(", "{"};
#define N_PIECES ((int)(sizeof(k_pieces) / sizeof(k_pieces[0])))

static void test_utf8(void)
{
    char mine[64];
    int cap = ref_kb_pass_cap();
    CHECK_EQ_I(cap, 64);
    for (int it = 0; it < 400; it++) {
        mine[0] = 0;
        ref_kb_pass_reset();
        for (int op = 0; op < 60; op++) {
            if (rnd() % 4 == 0) {
                ref_kb_pass_back();
                flex_kb_buf_backspace(mine);
            } else {
                const char *p = k_pieces[rnd() % N_PIECES];
                ref_kb_pass_append(p);
                flex_kb_buf_append(mine, sizeof(mine), p);
            }
            CHECK(strcmp(mine, ref_kb_pass()) == 0);
            CHECK_EQ_I(flex_kb_utf8_count(mine), ref_kb_utf8_count(mine));
            int L = (int)strlen(mine);
            for (int i = 0; i <= L; i++) {
                CHECK_EQ_I(flex_kb_utf8_prev(mine, i), ref_kb_utf8_prev(mine, i));
            }
        }
    }
    // Campo del editor de atajos (capacidad 10 y 24)
    for (int f = 0; f <= 1; f++) {
        char buf[24] = "";
        size_t cap2 = f == 0 ? FLEX_KB_SC_ABR : FLEX_KB_SC_EXP;
        ref_kb_field_reset(f);
        for (int op = 0; op < 400; op++) {
            if (rnd() % 3 == 0) {
                ref_kb_field_back();
                flex_kb_buf_backspace(buf);
            } else {
                const char *p = k_pieces[rnd() % N_PIECES];
                ref_kb_field_append(p);
                flex_kb_buf_append(buf, cap2, p);
            }
            CHECK(strcmp(buf, ref_kb_field()) == 0);
        }
    }
    // "<-" se come la n con tilde entera, no medio byte
    char s[16] = "Contrase\xC3\xB1";
    CHECK(flex_kb_buf_backspace(s));
    CHECK(strcmp(s, "Contrase") == 0);
    s[0] = 0;
    CHECK(!flex_kb_buf_backspace(s));
}

// ---- acentos y autocompletado ------------------------------------------------------------
static void test_words(void)
{
    for (int c = 0; c < 256; c++) {
        const char *a[4] = {0}, *b[4] = {0};
        int na = flex_kb_variants((char)c, a), nb = ref_kb_variants((char)c, b);
        CHECK_EQ_I(na, nb);
        for (int i = 0; i < na && i < nb; i++) {
            CHECK(strcmp(a[i], b[i]) == 0);
        }
    }
    flex_kb_prefs_t p;
    flex_kb_prefs_defaults(&p);
    for (int lang = 0; lang <= 1; lang++) {
        CHECK_EQ_I(flex_kb_dict_n(lang), ref_kb_dict_n(lang));
        int n = ref_kb_dict_n(lang);
        for (int i = 0; i < n; i++) {
            const char *w = ref_kb_dict_word(lang, i);
            CHECK_EQ_I(flex_kb_dict_has(&p, lang, w), 1);
            // prefijos de cada palabra, en mayusculas y sin tilde
            int L = (int)strlen(w);
            for (int k = 1; k <= L && k <= 4; k++) {
                char pref[8];
                memcpy(pref, w, (size_t)k);
                pref[k] = 0;
                if (k == 1 && pref[0] >= 'a' && pref[0] <= 'z') {
                    pref[0] = (char)(pref[0] - 32);
                }
                for (int emo = 0; emo <= 1; emo++) {
                    const char *a[4], *b[4];
                    p.emoji_sug = emo;
                    int na = flex_kb_suggest(&p, lang, pref, a, 3);
                    int nb = ref_kb_suggest(lang, 1, emo, pref, b, 3);
                    CHECK_EQ_I(na, nb);
                    for (int q = 0; q < na && q < nb; q++) {
                        CHECK(strcmp(a[q], b[q]) == 0);
                    }
                }
            }
        }
    }
    p.emoji_sug = false;
    static const char *probe[] = {"xq", "pf", "mas", "MAN", "nino", "zzz", "", "risa", "ri", "\xC3\xB1", "tamano", "q"};
    for (unsigned i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
        for (int lang = 0; lang <= 1; lang++) {
            CHECK_EQ_I(flex_kb_dict_has(&p, lang, probe[i]), ref_kb_dict_has(lang, probe[i]));
            for (int predict = 0; predict <= 1; predict++) {
                const char *a[4], *b[4];
                p.predict = predict;
                int na = flex_kb_suggest(&p, lang, probe[i], a, 3);
                int nb = ref_kb_suggest(lang, predict, 0, probe[i], b, 3);
                CHECK_EQ_I(na, nb);
                for (int q = 0; q < na && q < nb; q++) {
                    CHECK(strcmp(a[q], b[q]) == 0);
                }
            }
        }
    }
    p.predict = true;
    const char *a[4];
    CHECK_EQ_I(flex_kb_suggest(&p, true, "xq", a, 3) >= 1, 1);
    CHECK(strcmp(a[0], "porque") == 0);
    // Palabra en construccion
    static const char *bufs[] = {"hola mund", "uno\ndos", "", "  ", "tres\tcua", "palabra"};
    for (unsigned i = 0; i < sizeof(bufs) / sizeof(bufs[0]); i++) {
        int L = (int)strlen(bufs[i]);
        for (int cur = 0; cur <= L; cur++) {
            for (int sz = 1; sz <= 6; sz += 5) {
                char x[8], y[8];
                CHECK_EQ_I(flex_kb_current_word(bufs[i], cur, x, sz), ref_kb_current_word(bufs[i], cur, y, sz));
                CHECK(strcmp(x, y) == 0);
            }
        }
    }
}

void test_kb(void)
{
    test_geometry();
    test_layout_resolve();
    test_prefs();
    test_fx();
    test_utf8();
    test_words();
}
