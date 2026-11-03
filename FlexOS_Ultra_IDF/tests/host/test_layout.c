// Kit de maquetacion y Calendario (docs/spec/02 §8.4, 03 §4) frente a Arduino.
#include <string.h>
#include "flex_cal_model.h"
#include "flex_layout.h"
#include "flex_test.h"

void ref_ui_set(int canvas_w, int k);
void ref_ui_box(int *x, int *y, int *w, int *h);
int ref_ui_pad(void);
int ref_ui_gap(void);
int ref_ui_font_fit(const char *t, int maxw, int maxsize);
int ref_ui_font_h(int lh);
int ref_ui_line_h(int fs);
int ref_days(int y, int m);

static int g_k;
static int tw(const char *t, int fs, void *c)
{
    (void)c;
    return (int)strlen(t) * g_k * fs;
}

void test_layout(void)
{
    int bad = 0;
    // pad / gap / caja para todos los lienzos razonables (ventanas de DeX incluidas)
    for (int w = 0; w <= 900; w += 7) {
        ref_ui_set(w, 6);
        int rx, ry, rw, rh, mw, mh;
        ref_ui_box(&rx, &ry, &rw, &rh);
        flex_ui_box(w, 736 - 96, &mw, &mh);
        bad += rw != mw || rh != mh;
        int pad = flex_ui_pad(mw, mh);
        bad += pad != ref_ui_pad() || flex_ui_gap(pad) != ref_ui_gap();
    }
    for (int lh = -3; lh < 80; lh++) bad += ref_ui_font_h(lh) != flex_ui_font_h(lh);
    for (int fs = -1; fs < 9; fs++) bad += ref_ui_line_h(fs) != flex_ui_line_h(fs);
    for (int k = 3; k < 9; k++) {
        ref_ui_set(480, k);
        g_k = k;
        for (int len = 0; len < 14; len++) {
            char t[16];
            memset(t, 'x', (size_t)len);
            t[len] = 0;
            for (int maxw = 0; maxw < 260; maxw += 5) {
                for (int ms = -1; ms < 8; ms++) {
                    bad += ref_ui_font_fit(t, maxw, ms) != flex_ui_font_fit(t, maxw, ms, tw, NULL);
                }
            }
        }
    }
    CHECK_EQ_I(bad, 0);

    // Calendario: dias del mes (cada anio de 1900 a 2400) y el resto de la geometria de la spec
    int cd = 0;
    for (int y = 1900; y <= 2400; y++) {
        for (int m = 1; m <= 12; m++) cd += ref_days(y, m) != flex_cal_dim(y, m);
    }
    CHECK_EQ_I(cd, 0);
    CHECK_EQ_I(flex_cal_dim(2024, 2), 29);
    CHECK_EQ_I(flex_cal_dim(1900, 2), 28);
    CHECK_EQ_I(flex_cal_dim(2000, 2), 29);
    // sabado 4 de julio de 2026: el dia 1 cae en miercoles (3) y julio tiene 5 filas
    CHECK_EQ_I(flex_cal_first(6, 4), 3);
    CHECK_EQ_I(flex_cal_rows(3, 31), 5);
    CHECK_EQ_I(flex_cal_rows(6, 31), 6);   // el caso de seis filas
    CHECK_EQ_I(flex_cal_rows(0, 28), 4);   // febrero que empieza en domingo: cuatro
    // el dia 1 coincide con el calculo directo para todos los dias de un anio completo
    int fd = 0, wd1 = 3;   // 1 jul 2026 = miercoles
    for (int d = 1; d <= 31; d++) fd += flex_cal_first((wd1 + d - 1) % 7, d) != wd1;
    CHECK_EQ_I(fd, 0);
    // panel lateral "Hoy": a pantalla completa (480 de ancho) se ve y la rejilla queda en 265; en un lienzo estrecho no
    int sw, gw;
    int pad = flex_ui_pad(480, 640), gap = flex_ui_gap(pad);
    CHECK_EQ_I(flex_cal_side(480, pad, gap, &sw, &gw), 1);
    CHECK_EQ_I(sw, 160);
    CHECK_EQ_I(gw, 265);
    CHECK_EQ_I(flex_cal_side(400, flex_ui_pad(400, 640), gap, &sw, &gw), 0);
    CHECK_EQ_I(gw, 400 - 2 * flex_ui_pad(400, 640));
    CHECK_EQ_I(pad, 20);
    CHECK_EQ_I(gap, 15);
}
