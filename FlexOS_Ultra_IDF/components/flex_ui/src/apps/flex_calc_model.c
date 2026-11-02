#include "flex_calc_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void flex_calc_reset(flex_calc_t *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->disp, "0");
    c->fresh = true;
}

// Finito sin depender de macros de math.h: NaN falla (v == v); los infinitos fallan el rango
bool flex_calc_finite(double v)
{
    return (v == v) && (v > -1.0e308) && (v < 1.0e308);
}

void flex_calc_fmt_to(char *out, size_t n, double v)
{
    if (!flex_calc_finite(v)) {
        snprintf(out, n, "Error");
        return;
    }
    if (v == 0) {
        v = 0;   // evita "-0"
    }
    snprintf(out, n, "%g", v);
}

static void fmt(flex_calc_t *c, double v)
{
    c->err = !flex_calc_finite(v);
    flex_calc_fmt_to(c->disp, sizeof(c->disp), v);
}

double flex_calc_compute(double a, double b, char op)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case 'x': return a * b;
    case '/': return (b != 0) ? (a / b) : (double)__builtin_nan("");
    }
    return b;
}

void flex_calc_key(flex_calc_t *c, char k)
{
    // Con "Error" en pantalla el estado numerico no vale: cualquier entrada de numero limpia primero;
    // los operadores se ignoran (no hay operando valido)
    if (c->err) {
        if (k == 'c' || (k >= '0' && k <= '9') || k == '.') {
            strcpy(c->disp, "0");
            c->acc = 0;
            c->op = 0;
            c->fresh = true;
            c->err = false;
            if (k == 'c') {
                return;
            }
        } else {
            return;
        }
    }
    int L = (int)strlen(c->disp);
    if (k >= '0' && k <= '9') {
        if (c->fresh || (L == 1 && c->disp[0] == '0')) {
            c->disp[0] = k;
            c->disp[1] = 0;
        } else if (L < 16) {
            c->disp[L] = k;
            c->disp[L + 1] = 0;
        }
        c->fresh = false;
    } else if (k == '.') {
        if (c->fresh) {
            strcpy(c->disp, "0.");
            c->fresh = false;
        } else if (!strchr(c->disp, '.') && L < 15) {
            c->disp[L] = '.';
            c->disp[L + 1] = 0;
        }
    } else if (k == 'c') {
        strcpy(c->disp, "0");
        c->acc = 0;
        c->op = 0;
        c->fresh = true;
    } else if (k == '\b') {
        if (!c->fresh && L > 0) {
            c->disp[L - 1] = 0;
            if (c->disp[0] == 0) {
                strcpy(c->disp, "0");
            }
        }
    } else if (k == 'n') {
        if (strcmp(c->disp, "0") != 0) {
            if (c->disp[0] == '-') {
                memmove(c->disp, c->disp + 1, strlen(c->disp));
            } else {
                memmove(c->disp + 1, c->disp, strlen(c->disp) + 1);
                c->disp[0] = '-';
            }
        }
    } else if (k == '%') {
        fmt(c, atof(c->disp) / 100.0);
        c->fresh = true;
    } else if (k == '+' || k == '-' || k == 'x' || k == '/') {
        if (c->op && !c->fresh) {
            // una operacion a medias se cierra antes de encadenar la nueva: "2 + 3 + 4" da 9
            double r = flex_calc_compute(c->acc, atof(c->disp), c->op);
            if (!flex_calc_finite(r)) {
                fmt(c, r);
                c->op = 0;
                c->fresh = true;
                return;
            }
            c->acc = r;
        } else if (!c->op) {
            c->acc = atof(c->disp);
        }
        // (con operador puesto y fresh el usuario ha cambiado de idea: solo cambia el signo de la operacion)
        c->op = k;
        c->fresh = true;
    } else if (k == '=') {
        if (c->op) {
            c->acc = flex_calc_compute(c->acc, atof(c->disp), c->op);
            fmt(c, c->acc);
            c->op = 0;
        }
        c->fresh = true;
    }
}

char flex_calc_key_from_label(const char *t)
{
    if (!strcmp(t, "C")) {
        return 'c';
    }
    if (!strcmp(t, "+/-")) {
        return 'n';
    }
    if (!strcmp(t, "DEL")) {
        return '\b';
    }
    return t[0];
}

void flex_calc_line(const flex_calc_t *c, char *out, size_t n)
{
    if (c->err) {
        snprintf(out, n, "Error");
        return;
    }
    if (!c->op) {
        snprintf(out, n, "%s", c->disp);
        return;
    }
    char acc[24];
    flex_calc_fmt_to(acc, sizeof(acc), c->acc);
    const char op[2] = {c->op, 0};
    if (c->fresh) {
        snprintf(out, n, "%s %s", acc, op);   // operador recien pulsado
    } else {
        snprintf(out, n, "%s %s %s", acc, op, c->disp);
    }
}

void flex_calc_pack(const flex_calc_t *c, flex_calc_sess_t *s)
{
    memset(s, 0, sizeof(*s));
    s->acc = c->acc;
    snprintf(s->disp, sizeof(s->disp), "%s", c->disp);   // solo hasta el NUL: el resto a cero, como calcSaveSess
    s->op = c->op;
    s->fresh = c->fresh;
    s->err = c->err;
}

bool flex_calc_unpack(flex_calc_t *c, const flex_calc_sess_t *s)
{
    char disp[FLEX_CALC_DISP];
    memcpy(disp, s->disp, sizeof(disp));
    disp[sizeof(disp) - 1] = 0;   // se termina a la fuerza, como calcLoadSess
    if (!disp[0]) {
        return false;             // sesion incoherente: se ignora
    }
    if (s->op != 0 && s->op != '+' && s->op != '-' && s->op != 'x' && s->op != '/') {
        return false;             // operador imposible: se ignora
    }
    snprintf(c->disp, sizeof(c->disp), "%s", disp);
    c->acc = s->acc;
    c->op = s->op;
    c->fresh = s->fresh != 0;
    c->err = s->err != 0;
    return true;
}
