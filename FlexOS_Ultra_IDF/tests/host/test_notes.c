// Notas (docs/spec/03 §6.2, §6.5): geometria de la lista y sesion notas.bin v3 frente a AppNotes.h de Arduino.
#include <stdio.h>
#include <string.h>
#include "flex_note_model.h"
#include "flex_test.h"

void ref_nl_set(int n, int scroll);
void ref_nl_rect(int i, int *x, int *y, int *w, int *h);
int ref_nl_max_scroll(void);
void ref_nl_fab(int *x, int *y, int *r);
int ref_nl_card_h(void);
int ref_notes_save(int view, const char *path, int kbl, int kbf, const char *text, int cur, int a, int b, int escroll, int lscroll, unsigned char *out);
void ref_notes_load(const unsigned char *in, size_t n, int exists, int *view, char *path, int *kbl, int *kbf, int *cur, int *a, int *b, int *escroll, int *lscroll, int *valid);

static uint32_t s_seed = 6060;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

void test_notes(void)
{
    int bad = 0;
    // ---- lista: tarjetas, desplazamiento y boton flotante
    bad += flex_nl_card_h() != ref_nl_card_h();
    for (int n = 0; n <= FLEX_NL_MAX; n++) {
        ref_nl_set(n, 0);
        bad += flex_nl_max_scroll(n) != ref_nl_max_scroll();
        for (int sc = 0; sc < 900; sc += 37) {
            ref_nl_set(n, sc);
            for (int i = 0; i < n; i += 1 + (int)(rnd() % 3)) {
                int rx, ry, rw, rh, mx, my, mw, mh;
                ref_nl_rect(i, &rx, &ry, &rw, &rh);
                flex_nl_card_rect(i, sc, &mx, &my, &mw, &mh);
                bad += rx != mx || ry != my || rw != mw || rh != mh;
            }
        }
    }
    {
        int rx, ry, rr, mx, my, mr;
        ref_nl_fab(&rx, &ry, &rr);
        flex_nl_fab(&mx, &my, &mr);
        bad += rx != mx || ry != my || rr != mr;
    }
    CHECK(bad == 0);
    // hit-tests (los de noteListTick, escritos aqui segun el spec)
    CHECK(flex_nl_fab_hit(396, 650) && flex_nl_fab_hit(396 + 56, 650) && !flex_nl_fab_hit(396 + 57, 650) && !flex_nl_fab_hit(100, 100));
    CHECK(flex_nl_bar_hit(460, 700) == 1 && flex_nl_bar_hit(381, 700) == 1 && flex_nl_bar_hit(380, 700) == 2 && flex_nl_bar_hit(251, 700) == 2 &&
          flex_nl_bar_hit(250, 700) == 0 && flex_nl_bar_hit(460, 671) == 0 && flex_nl_bar_hit(460, 733) == 0);
    CHECK(flex_nl_card_at(3, 0, 20, 100) == 0 && flex_nl_card_at(3, 0, 250, 100) == 1 && flex_nl_card_at(3, 0, 20, 100 + 307) == 2 && flex_nl_card_at(3, 0, 240, 360) == -1);

    // ---- sesion: lo que se escribe, byte a byte
    CHECK(sizeof(flex_note_sess_t) == 116);
    int sb = 0, saves = 0;
    for (int i = 0; i < 4000; i++) {
        char path[96] = "";
        int view = (int)(rnd() % 2);
        if (rnd() % 4) snprintf(path, sizeof(path), "/Notas/Sin t\xC3\xADtulo %u.txt", (unsigned)(rnd() % 1000));
        char text[300];
        int tl = (int)(rnd() % 200);
        memset(text, 'x', (size_t)tl);
        text[tl] = 0;
        int cur = (int)(rnd() % 400) - 50, a = (int)(rnd() % 200) - 20, b = (int)(rnd() % 200) - 20;
        int kbl = (int)(rnd() % 6), kbf = (int)(rnd() % 32), es = (int)(rnd() % 4000) - 100, ls = (int)(rnd() % 4000) - 100;
        unsigned char ref[160];
        int rn = ref_notes_save(view, path, kbl, kbf, text, cur, a, b, es, ls, ref);
        flex_note_sess_t s;
        flex_note_sess_pack(&s, view == 1, path, (uint8_t)kbl, (uint8_t)kbf, cur, tl, a, b, es, ls);
        sb += rn != (int)sizeof(s) || memcmp(ref, &s, sizeof(s)) != 0;
        saves++;
    }
    CHECK(sb == 0);
    // ---- sesion: lo que se lee (incluidos ficheros corruptos y rutas sin terminar)
    int lb = 0;
    for (int i = 0; i < 4000; i++) {
        unsigned char raw[sizeof(flex_note_sess_t)];
        for (size_t k = 0; k < sizeof(raw); k++) raw[k] = (unsigned char)(rnd() % 256);
        if (rnd() % 2) raw[0] = (unsigned char)(rnd() % 3);          // vista casi siempre valida
        if (rnd() % 2) { raw[20] = '/'; raw[21] = 'N'; raw[22] = 0; }   // ruta corta terminada
        if (rnd() % 3 == 0) raw[sizeof(raw) - 1] = 'z';              // ruta SIN terminar: se corta
        int exists = (int)(rnd() % 2);
        int rv, rkl, rkf, rc, ra, rb, res, rls, rvalid;
        char rpath[160];
        ref_notes_load(raw, sizeof(raw), exists, &rv, rpath, &rkl, &rkf, &rc, &ra, &rb, &res, &rls, &rvalid);
        flex_note_sess_t s;
        memcpy(&s, raw, sizeof(s));
        flex_note_sess_normalize(&s);
        if (s.view == 1 && !exists) {   // el fichero ya no existe: la vista vuelve a la lista (lo decide el llamante)
            s.view = 0;
            s.path[0] = 0;
        }
        lb += rv != s.view || strcmp(rpath, s.path) || rkl != s.kb_layout || rkf != s.kb_flags || rc != s.cursor || ra != s.sel_a ||
              rb != s.sel_b || res != s.editor_scroll || rls != s.list_scroll || !rvalid;
    }
    CHECK(lb == 0);
    // una sesion de otro tamano se descarta: la maneja flex_session (version y tamano), aqui solo se valida el contenido
    CHECK(flex_note_sel_valid(0, 1, 5) && flex_note_sel_valid(2, 5, 5) && !flex_note_sel_valid(-1, 3, 5) && !flex_note_sel_valid(3, 3, 5) && !flex_note_sel_valid(2, 6, 5));
    printf("notas: lista y sesion notas.bin v3 iguales a Arduino (%d sesiones, 4000 lecturas)\n", saves);
}
