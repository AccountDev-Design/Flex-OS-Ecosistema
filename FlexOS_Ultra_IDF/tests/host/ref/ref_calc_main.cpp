// Referencia: la calculadora de la version Arduino (AppsBasic.h:33-152 y su sesion) tal cual.
#include "ref_prelude.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IC_CALC 11
#define FS_DIR_SESS "/System/Sessions"
static uint8_t g_file[64]; static size_t g_file_n; static bool g_fs = true;
static bool flexFsReady() { return g_fs; }
static bool sessWrite(const char *, uint16_t, uint16_t, const void *d, size_t n) { memcpy(g_file, d, n); g_file_n = n; return true; }
static size_t sessRead(const char *, uint16_t, uint16_t, void *o, size_t max) { size_t n = g_file_n < max ? g_file_n : max; memcpy(o, g_file, n); return n; }
#include "ref_calc.inc"
extern "C" {
void ref_calc_reset() { strcpy(calcDisp, "0"); calcAcc = 0; calcOp = 0; calcFresh = true; calcErr = false; }
void ref_calc_key(const char *label) { calcKeyFromLabel(label); }
void ref_calc_state(char *disp, double *acc, char *op, int *fresh, int *err, char *line)
{
    strcpy(disp, calcDisp); *acc = calcAcc; *op = calcOp; *fresh = calcFresh; *err = calcErr; calcLine(line, 48);
}
// sesion: guardar con el estado actual y leer lo que escribio / cargar un archivo dado
int ref_calc_save(unsigned char *out) { calcSaveSess(); memcpy(out, g_file, g_file_n); return (int)g_file_n; }
int ref_calc_load(const unsigned char *in, size_t n, char *disp, double *acc, char *op, int *fresh, int *err)
{
    ref_calc_reset();
    memcpy(g_file, in, n); g_file_n = n;
    calcLoadSess();
    strcpy(disp, calcDisp); *acc = calcAcc; *op = calcOp; *fresh = calcFresh; *err = calcErr;
    return 0;
}
}
