// Flex OS Ultra · Calculadora: logica pura (docs/spec/03 §3.2; Arduino: FlexOS_Ultra_AppsBasic.h:33-152).
// Cuatro operaciones con linea de operacion visible ("25 + 10"), porcentaje, cambio de signo, borrado y
// "Error" explicito. Sin teclas de memoria. tests/host la compara tecla a tecla con el codigo Arduino.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_CALC_DISP 24

typedef struct {
    char disp[FLEX_CALC_DISP];   // entrada en curso, "0" al empezar
    double acc;
    char op;                     // 0, '+', '-', 'x', '/'
    bool fresh;                  // el proximo digito empieza entrada nueva
    bool err;                    // el display muestra "Error"
} flex_calc_t;

void flex_calc_reset(flex_calc_t *c);
bool flex_calc_finite(double v);
void flex_calc_fmt_to(char *out, size_t n, double v);
double flex_calc_compute(double a, double b, char op);   // x/0 = NaN (se ve "Error")
// Teclas: '0'..'9', '.', 'c' (C), '\b' (DEL), 'n' (+/-), '%', '+', '-', 'x', '/', '='
void flex_calc_key(flex_calc_t *c, char k);
// Etiqueta de la tecla -> codigo ("C", "+/-", "DEL" y el resto, su primer caracter)
char flex_calc_key_from_label(const char *label);
// La linea que se ve: "Error", el numero, "25 +" o "25 + 10"
void flex_calc_line(const flex_calc_t *c, char *out, size_t n);

// Sesion calc.bin v1 (docs/spec/01c 14.6): 40 bytes {double acc; char disp[24]; char op; u8 fresh, err, rsv}
typedef struct {
    double acc;
    char disp[FLEX_CALC_DISP];
    char op;
    uint8_t fresh, err, rsv;
} flex_calc_sess_t;
_Static_assert(sizeof(flex_calc_sess_t) == 40, "calc.bin v1 son 40 bytes");
void flex_calc_pack(const flex_calc_t *c, flex_calc_sess_t *s);
// false: se ignora la sesion entera (disp vacio u operador que no es +-x/) y se abre en "0"
bool flex_calc_unpack(flex_calc_t *c, const flex_calc_sess_t *s);

#ifdef __cplusplus
}
#endif
