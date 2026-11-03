// Editor de Notas (docs/spec/03 §6.4): edicion, recorrido, hit-test, scroll y dibujo (cursor, seleccion, manijas,
// revision ortografica, menu flotante) frente al codigo de Keyboard.h de Arduino, con las mismas llamadas graficas anotadas.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_ned.h"
#include "flex_test.h"

void ref_ned_font(void);
int ref_ned_adv(unsigned cp);
void ref_ned_set(const char *t, size_t max, int kbtop, int spell, int cur, int a, int b, int menu, int scroll);
void ref_ned_set_follow(int f);
void ref_ned_get(char *t, int *cur, int *a, int *b, int *menu, int *scroll, int *follow);
void ref_ned_insert(const char *s);
void ref_ned_backspace(void);
void ref_ned_delete_sel(void);
void ref_ned_select_word(int bi);
void ref_ned_select_all(void);
int ref_ned_walk(int want, int *y);
int ref_ned_hit(int px, int py);
int ref_ned_max_scroll(void);
void ref_ned_clamp(void);
void ref_ned_ensure(void);
int ref_ned_draw(char *log, int cap, int *marks);
int ref_ned_menu_hit(int px, int py);
int ref_ned_word(const char *b, int cur, char *out, int n);

static uint32_t s_seed = 55501;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}
static int adv(uint32_t cp, void *c)
{
    (void)c;
    return ref_ned_adv(cp);
}

#define CAP 512
static char s_buf[CAP];
static flex_ned_t E;
static int g_kbtop = 390;

static const char *const PIECES[] = {"hola", " ", "mundo", "\n", "a", "\xC3\xB1", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "zz", "tecnologia", "x",
                                     " ", "\xE2\x80\x99", "\xC2\xA0", "\xE2\x80\x8B", "casa", "12", "\xC3\xA1rbol", "\n\n"};
#define NP (int)(sizeof(PIECES) / sizeof(PIECES[0]))

static void random_text(char *out, size_t cap, int pieces)
{
    out[0] = 0;
    for (int i = 0; i < pieces; i++) {
        const char *p = PIECES[rnd() % NP];
        if (strlen(out) + strlen(p) + 1 < cap) strcat(out, p);
    }
}

static void to_ref(int spell)
{
    ref_ned_set(E.buf, E.cap, g_kbtop, spell, E.cur, E.sel_a, E.sel_b, E.menu, E.scroll);
    ref_ned_set_follow(E.follow);
}
// Compara el estado del editor con el de la referencia. false = difieren.
static bool same_state(void)
{
    char t[4096];
    int cur, a, b, menu, scroll, follow;
    ref_ned_get(t, &cur, &a, &b, &menu, &scroll, &follow);
    return !strcmp(t, E.buf) && cur == E.cur && a == E.sel_a && b == E.sel_b && menu == E.menu && follow == E.follow;
}

// ---- el dibujo de Arduino, reconstruido a partir del recorrido del firmware ---------------------------------
static char g_log[1 << 16];
static int g_ln;
static void lg(const char *f, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(tmp, sizeof(tmp), f, ap);
    va_end(ap);
    g_ln += snprintf(g_log + g_ln, sizeof(g_log) - (size_t)g_ln, "%s", tmp);
}
static void on_glyph(const flex_ned_glyph_t *g, void *u)
{
    (void)u;
    if (g->selected) lg("R %d %d %d %d %d\n", g->x - 1, g->y - 2, g->w + 1, FLEX_NED_LH - 4, 2);
    char one[6];
    int n = g->nb > 5 ? 5 : g->nb;
    memcpy(one, E.buf + g->bi, (size_t)n);
    one[n] = 0;
    lg("T %d %d %s %d %d\n", g->x, g->y, one, 2, 3);
}
static bool has_word(const char *w, void *u)
{
    (void)u;
    return (strlen(w) % 2) == 0;
}
static void on_miss(int x0, int x1, int y, void *u)
{
    (void)u;
    for (int px = x0; px < x1; px += 3) lg("R %d %d %d %d %d\n", px, y + FLEX_NED_LH - 7, 2, 2, 4);
}

static int fw_draw(int spell, int *marks)
{
    int txt_bot = g_kbtop - 8;
    if (E.follow) {
        E.follow = false;
        flex_ned_ensure_cursor(&E, adv, NULL, txt_bot);
    } else {
        flex_ned_clamp_scroll(&E, adv, NULL, txt_bot);
    }
    g_ln = 0;
    g_log[0] = 0;
    lg("R %d %d %d %d %d\n", 8, 48, 480 - 16, txt_bot - 48, 1);
    flex_ned_scan_t sc = {.adv = adv, .y_break = txt_bot - 22, .glyph = on_glyph, .has_word = spell ? has_word : NULL,
                          .misspelled = spell ? on_miss : NULL};
    flex_ned_marks_t m;
    flex_ned_scan(&E, &sc, &m);
    if (flex_ned_has_sel(&E)) {
        lg("V %d %d %d %d\n", m.a_x, m.a_y - 2, 24, 5);
        lg("C %d %d %d %d\n", m.a_x, m.a_y + 24, 7, 5);
        lg("V %d %d %d %d\n", m.b_x, m.b_y - 2, 24, 5);
        lg("C %d %d %d %d\n", m.b_x, m.b_y + 24, 7, 5);
    } else {
        lg("R %d %d %d %d %d\n", m.cur_x + 1, m.cur_y - 2, 2, 22, 5);
    }
    if (E.menu) {
        static const char *const IT[4] = {"Cortar", "Copiar", "Pegar", "Todo"};
        flex_ned_menu_t mn;
        flex_ned_menu_geom(m.a_y, &mn);
        lg("S %d %d %d %d %d %d\n", mn.panel_x, mn.panel_y, mn.panel_w, mn.panel_h, 8, 1);
        for (int i = 0; i < 4; i++) {
            int bx = mn.x + i * (mn.bw + mn.gap);
            lg("S %d %d %d %d %d %d\n", bx, mn.y, mn.bw, 28, 6, 1);
            lg("TC %d %d %s %d %d\n", bx + mn.bw / 2, mn.y + 7, IT[i], 2, 3);
        }
    }
    marks[0] = m.cur_x; marks[1] = m.cur_y; marks[2] = m.a_x; marks[3] = m.a_y; marks[4] = m.b_x; marks[5] = m.b_y;
    return g_ln;
}

// Quita del registro las lineas de manijas y cursor (V, C y la R del cursor ya estan al final): se usa cuando la
// seleccion cae fuera de lo visible, donde Arduino deja las manijas en (18, ...) por cortar el recorrido.
static void strip_tail(char *log)
{
    char *p = strstr(log, "\nV ");
    if (p) p[1] = 0;
}

void test_ned(void)
{
    ref_ned_font();
    int bad = 0, draws = 0, spelled = 0, sels = 0;

    for (int iter = 0; iter < 3000; iter++) {
        char txt[CAP];
        random_text(txt, sizeof(txt), 1 + (int)(rnd() % 40));
        flex_ned_init(&E, s_buf, CAP);
        snprintf(s_buf, CAP, "%s", txt);
        int L = (int)strlen(txt);
        E.cur = L ? (int)(rnd() % (uint32_t)(L + 1)) : 0;
        // una posicion cualquiera de byte puede caer a mitad de un caracter; Arduino la acepta igual
        int mode = (int)(rnd() % 4);
        if (mode == 1 && L > 2) {
            E.sel_a = (int)(rnd() % (uint32_t)(L - 1));
            E.sel_b = E.sel_a + 1 + (int)(rnd() % (uint32_t)(L - E.sel_a - 1 + 1));
            if (E.sel_b > L) E.sel_b = L;
            E.cur = E.sel_b;
            E.menu = rnd() & 1;
        }
        g_kbtop = (rnd() & 1) ? 390 : 480;
        int spell = (int)(rnd() % 2);
        E.scroll = (int)(rnd() % 120);
        E.follow = rnd() & 1;
        to_ref(spell);
        // -- recorrido y scroll
        int y1, y2;
        int h1 = flex_ned_walk(&E, adv, NULL, E.cur, &y1), h2 = ref_ned_walk(E.cur, &y2);
        if (h1 != h2 || y1 != y2) { bad++; if (bad < 4) printf("walk: h %d/%d y %d/%d cur=%d\n", h1, h2, y1, y2, E.cur); }
        if (flex_ned_max_scroll(&E, adv, NULL, g_kbtop - 8) != ref_ned_max_scroll()) { bad++; if (bad < 4) printf("maxscroll\n"); }
        for (int k = 0; k < 8; k++) {
            int px = (int)(rnd() % 520) - 20, py = (int)(rnd() % 700);
            if (flex_ned_hit(&E, adv, NULL, px, py) != ref_ned_hit(px, py)) { bad++; if (bad < 4) printf("hit\n"); }
        }
        // -- dibujo
        char rlog[1 << 16];
        int rm[8], fm[6];
        flex_ned_t save = E;
        ref_ned_draw(rlog, sizeof(rlog), rm);   // la referencia aplica ensure/clamp segun su 'follow'
        fw_draw(spell, fm);
        int ref_scroll;
        {
            char t2[4096];
            int c2, a2, b2, m2, f2;
            ref_ned_get(t2, &c2, &a2, &b2, &m2, &ref_scroll, &f2);
        }
        if (ref_scroll != E.scroll) { bad++; if (bad < 4) printf("scroll ref=%d fw=%d follow=%d\n", ref_scroll, E.scroll, save.follow); }
        // si la seleccion no es visible, Arduino deja las manijas en el margen: se compara solo lo anterior
        bool sel_visible = !flex_ned_has_sel(&E) || (fm[3] >= 0 && fm[5] >= 0 && fm[3] <= g_kbtop - 30 && fm[5] <= g_kbtop - 30);
        if (!sel_visible) {
            strip_tail(rlog);
            strip_tail(g_log);
        }
        // un cursor fuera de la hoja (se arrastro el scroll): Arduino lo deja en (18, ..) por cortar el recorrido; el firmware
        // lo coloca donde esta y la hoja lo recorta. No se compara su linea.
        if (!flex_ned_has_sel(&E) && !(fm[1] <= g_kbtop - 8 - 22 && fm[1] + FLEX_NED_LH > 48)) {
            for (char *l = rlog + strlen(rlog) - 2; l > rlog; l--) if (*l == '\n') { l[1] = 0; break; }
            for (char *l = g_log + strlen(g_log) - 2; l > g_log; l--) if (*l == '\n') { l[1] = 0; break; }
        }
        bad += strcmp(rlog, g_log) != 0;
        if (strcmp(rlog, g_log) != 0 && bad < 4) {
            FILE *f1 = fopen("/tmp/claude-0/ned_ref.txt", "w"), *f2 = fopen("/tmp/claude-0/ned_fw.txt", "w");
            fputs(rlog, f1); fputs(g_log, f2); fclose(f1); fclose(f2);
            printf("DIFF iter=%d spell=%d kbtop=%d scroll=%d sel=%d..%d cur=%d menu=%d txt=[%s]\n", iter, spell, g_kbtop, E.scroll, E.sel_a, E.sel_b, E.cur, E.menu, E.buf);
        }
        draws++;
        spelled += spell && strstr(g_log, " 4\n") != NULL;
        sels += flex_ned_has_sel(&E);
    }
    CHECK(bad == 0);
    CHECK(spelled > 100 && sels > 300);   // los casos cubren ortografia y seleccion de verdad

    // ---- edicion: secuencias de insertar / borrar / seleccionar frente a Arduino, con topes pequenos
    static const char *const INS[] = {"a", "hola", "\xC3\xB1", "\n", " ", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "texto largo de prueba para llenar"};
    int eb = 0, full = 0;
    for (int seq = 0; seq < 400; seq++) {
        size_t cap = 24 + rnd() % 40;
        flex_ned_init(&E, s_buf, cap);
        to_ref(0);
        ref_ned_set("", cap, 390, 0, 0, -1, -1, 0, 0);
        for (int step = 0; step < 60; step++) {
            int op = (int)(rnd() % 8);
            int L = (int)strlen(E.buf);
            // en Arduino un lleno borra antes la seleccion: se evita ese caso (diferencia deliberada, comprobada abajo)
            if (op <= 2) {
                const char *s = INS[rnd() % 8];
                if (flex_ned_has_sel(&E) && L - (E.sel_b - E.sel_a) + (int)strlen(s) < (int)cap - 1 && L + (int)strlen(s) >= (int)cap - 1) {
                    continue;   // cabe SOLO gracias a la seleccion: Arduino lo rechazaba; aqui entra (comprobado abajo)
                }
                bool ok = flex_ned_insert(&E, s);
                full += !ok;
                ref_ned_insert(s);
                if (!ok && flex_ned_has_sel(&E)) {
                    char t[4096]; int c, a, b, m, sc, f;
                    ref_ned_get(t, &c, &a, &b, &m, &sc, &f);
                    // Arduino ya borro la seleccion; el firmware no: se alinea para seguir comparando
                    if (a < 0 && flex_ned_has_sel(&E)) {
                        flex_ned_delete_sel(&E);
                        E.follow = true;
                    }
                }
            } else if (op == 3) {
                flex_ned_backspace(&E);
                ref_ned_backspace();
            } else if (op == 4 && L > 0) {
                int bi = (int)(rnd() % (uint32_t)(L + 2));
                flex_ned_select_word_at(&E, bi);
                ref_ned_select_word(bi);
            } else if (op == 5) {
                flex_ned_select_all(&E);
                ref_ned_select_all();
            } else if (op == 6) {
                flex_ned_clear_sel(&E);
                ref_ned_set(E.buf, cap, 390, 0, E.cur, -1, -1, 0, 0);
                ref_ned_set_follow(E.follow);
            } else if (op == 7 && L > 0) {
                E.cur = (int)(rnd() % (uint32_t)(L + 1));
                E.cur = flex_ned_utf8_prev(E.buf, E.cur + (E.cur < L ? 1 : 0) > L ? L : E.cur);
                ref_ned_set(E.buf, cap, 390, 0, E.cur, E.sel_a, E.sel_b, E.menu, 0);
                ref_ned_set_follow(E.follow);
            }
            if (!same_state()) {
                if (eb == 0) {
                    char t2[4096]; int c2, a2, b2, m2, s2, f2;
                    ref_ned_get(t2, &c2, &a2, &b2, &m2, &s2, &f2);
                    printf("EDIT op=%d cap=%zu\n ref=[%s] cur=%d sel=%d..%d menu=%d follow=%d\n fw =[%s] cur=%d sel=%d..%d menu=%d follow=%d\n", op, cap, t2, c2, a2, b2, m2, f2, E.buf, E.cur, E.sel_a, E.sel_b, E.menu, E.follow);
                }
                eb++;
                break;
            }
        }
    }
    CHECK(eb == 0);
    CHECK(full > 50);

    // ---- palabra en curso
    for (int i = 0; i < 2000; i++) {
        char t[200];
        random_text(t, sizeof(t), 1 + (int)(rnd() % 12));
        int cur = (int)(rnd() % (strlen(t) + 3)) - 1;
        char a[40], b[40];
        bad += flex_ned_current_word(t, cur, a, sizeof(a)) != ref_ned_word(t, cur, b, sizeof(b));
        ref_ned_word(t, cur, b, sizeof(b));
        flex_ned_current_word(t, cur, a, sizeof(a));
        bad += strcmp(a, b) != 0;
    }
    CHECK(bad == 0);

    // ---- menu: geometria y toque
    {
        int mb = 0;
        for (int ay = -20; ay < 600; ay += 3) {
            flex_ned_t e2;
            char b2[16];
            flex_ned_init(&e2, b2, sizeof(b2));
            snprintf(b2, sizeof(b2), "ab");
            to_ref(0);
            ref_ned_set("ab", 64, 390, 0, 2, 0, 2, 1, 0);
            char rlog[1 << 16];
            int rm[8];
            ref_ned_draw(rlog, sizeof(rlog), rm);   // fija noteMenuX/Y con la manija en y = 60
            flex_ned_menu_t mn;
            flex_ned_menu_geom(rm[3], &mn);
            mb += mn.x != rm[6] || mn.y != rm[7];
            for (int px = 0; px < 480; px += 13) {
                for (int py = mn.y - 5; py < mn.y + 40; py += 7) mb += flex_ned_menu_hit(&mn, px, py) != ref_ned_menu_hit(px, py);
            }
            (void)ay;
        }
        CHECK(mb == 0);
    }

    // ---- diferencias deliberadas con Arduino
    {
        char b[16];
        flex_ned_init(&E, b, sizeof(b));
        CHECK(flex_ned_insert(&E, "0123456789"));
        E.sel_a = 2;
        E.sel_b = 6;
        E.cur = 6;
        CHECK(!flex_ned_insert(&E, "abcdefghijklmnop"));   // no cabe ni liberando la seleccion: no se toca NADA
        CHECK(!strcmp(b, "0123456789") && E.sel_a == 2 && E.sel_b == 6);
        uint32_t r0 = E.rev;
        CHECK(flex_ned_insert(&E, "WXYZ"));                // reemplazo del mismo tamano: es un cambio (rev)
        CHECK(!strcmp(b, "01WXYZ6789") && E.rev == r0 + 2);   // borrar la seleccion + insertar
        char out[4];
        E.sel_a = 0;
        E.sel_b = 10;
        CHECK(flex_ned_copy_sel(&E, out, sizeof(out)) == 3 && !strcmp(out, "01W"));
        flex_ned_init(&E, b, sizeof(b));
        snprintf(b, sizeof(b), "a\xC3\xB1" "b");   // "añb": copiar con 3 bytes de sitio corta en un limite de caracter
        E.sel_a = 0;
        E.sel_b = 4;
        CHECK(flex_ned_copy_sel(&E, out, 3) == 1 && !strcmp(out, "a"));
    }
    printf("editor de Notas: edicion, recorrido, hit-test, scroll y dibujo iguales a Arduino (%d dibujos, %d con seleccion, %d con ortografia)\n", draws, sels, spelled);
}
