// Calculadora (docs/spec/03 §3.2): cada tecla, la linea de operacion y la sesion calc.bin, frente a Arduino.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_calc_model.h"
#include "flex_test.h"

void ref_calc_reset(void);
void ref_calc_key(const char *label);
void ref_calc_state(char *disp, double *acc, char *op, int *fresh, int *err, char *line);
int ref_calc_save(unsigned char *out);
int ref_calc_load(const unsigned char *in, size_t n, char *disp, double *acc, char *op, int *fresh, int *err);

static uint32_t s_seed = 2468;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}
static const char *const KEYS[19] = {"C", "+/-", "%", "/", "7", "8", "9", "x", "4", "5", "6", "-", "1", "2", "3", "+", "0", ".", "="};

static bool same_acc(double a, double b)
{
    return (a != a && b != b) || memcmp(&a, &b, sizeof(a)) == 0 || a == b;
}

void test_calc(void)
{
    int bad = 0, errs = 0, chained = 0;
    for (int it = 0; it < 30000; it++) {
        flex_calc_t c;
        flex_calc_reset(&c);
        ref_calc_reset();
        int n = 1 + (int)(rnd() % 40);
        for (int k = 0; k < n && !bad; k++) {
            // mas digitos que operadores, y a veces "DEL"
            const char *key = rnd() % 14 == 0 ? "DEL" : KEYS[rnd() % 3 == 0 ? rnd() % 19 : 4 + rnd() % 15 - (rnd() % 4 == 0 ? 0 : 0)];
            if (rnd() % 9 == 0) key = KEYS[rnd() % 19];
            ref_calc_key(key);
            flex_calc_key(&c, flex_calc_key_from_label(key));
            char rd[24], rl[48], ml[48], ro;
            double ra;
            int rf, re;
            ref_calc_state(rd, &ra, &ro, &rf, &re, rl);
            flex_calc_line(&c, ml, sizeof(ml));
            if (strcmp(rd, c.disp) || !same_acc(ra, c.acc) || ro != c.op || rf != c.fresh || re != c.err || strcmp(rl, ml)) bad++;
            errs += re;
            chained += ro != 0 && !rf;
        }
    }
    CHECK_EQ_I(bad, 0);
    CHECK(errs > 500 && chained > 5000);   // se ejercitaron "Error" y las operaciones a medias
    // casos de la especificacion
    flex_calc_t c;
    flex_calc_reset(&c);
    const char *seq[] = {"2", "+", "3", "+", "4", "="};
    for (int i = 0; i < 6; i++) flex_calc_key(&c, flex_calc_key_from_label(seq[i]));
    CHECK(strcmp(c.disp, "9") == 0);
    char l[48];
    flex_calc_reset(&c);
    const char *s2[] = {"2", "5", "+", "1", "0"};
    for (int i = 0; i < 5; i++) flex_calc_key(&c, flex_calc_key_from_label(s2[i]));
    flex_calc_line(&c, l, sizeof(l));
    CHECK(strcmp(l, "25 + 10") == 0);
    flex_calc_reset(&c);
    flex_calc_key(&c, '5');
    flex_calc_key(&c, '/');
    flex_calc_key(&c, '0');
    flex_calc_key(&c, '=');
    flex_calc_line(&c, l, sizeof(l));
    CHECK(strcmp(l, "Error") == 0 && c.err);

    // sesion: lo que escribe Arduino lo lee esta y al reves, y las sesiones imposibles se ignoran igual
    int sd = 0;
    for (int it = 0; it < 6000; it++) {
        flex_calc_t a;
        flex_calc_reset(&a);
        ref_calc_reset();
        for (int k = 0, n = 1 + (int)(rnd() % 12); k < n; k++) {
            const char *key = KEYS[rnd() % 19];
            ref_calc_key(key);
            flex_calc_key(&a, flex_calc_key_from_label(key));
        }
        unsigned char rf[64];
        int rn = ref_calc_save(rf);
        flex_calc_sess_t m;
        flex_calc_pack(&a, &m);
        if (rn != (int)sizeof(m) || memcmp(rf, &m, sizeof(m))) sd++;
        // sesion al azar (a veces danada): cargar con las dos y comparar
        flex_calc_sess_t w = m;
        switch (rnd() % 5) {
        case 0: w.op = "+-x/*?"[rnd() % 6]; break;
        case 1: w.disp[0] = 0; break;
        case 2: memset(w.disp, 'z', sizeof(w.disp)); break;   // sin terminar
        case 3: w.acc = (double)(rnd() % 1000) / 7.0; break;
        default: break;
        }
        char rd[24], ro;
        double racc;
        int rfr, rer;
        ref_calc_load((const unsigned char *)&w, sizeof(w), rd, &racc, &ro, &rfr, &rer);
        flex_calc_t b;
        flex_calc_reset(&b);
        flex_calc_unpack(&b, &w);
        if (strcmp(rd, b.disp) || !same_acc(racc, b.acc) || ro != b.op || rfr != b.fresh || rer != b.err) sd++;
    }
    CHECK_EQ_I(sd, 0);
    printf("calculadora: teclas, linea y sesion iguales a Arduino (%d errores, %d operaciones a medias)\n", errs, chained);
}
