// Flex OS Ultra · app Calendario (docs/spec/03 §4; Arduino: FlexOS_Ultra_AppsBasic.h:351-403).
//
// El mes en curso con el dia de hoy resaltado; sin navegacion de meses ni eventos. Maqueta contra el lienzo
// real (kit adaptativo): la rejilla manda y el panel lateral "Hoy" (dia grande y fecha larga) aparece entero
// cuando el ancho da para las dos cosas. Se repinta al cambiar el minuto (por si cambia el dia) y al
// reanudar; no guarda ningun estado propio, asi que no tiene sesion.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_cal_model.h"
#include "flex_clock.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_theme.h"

static struct {
    lv_obj_t *root;
    lv_area_t area;
    int y, mo, d, wd;          // lo que se dibujo (para saber si cambio el dia)
    char hdr[40];
    int cells, side, first, rows, dots;   // para las pruebas
} C;

static void build(void)
{
    if (!C.root) {
        return;
    }
    lv_obj_clean(C.root);
    struct tm tm;
    flex_clock_now(&tm);
    C.y = tm.tm_year + 1900;
    C.mo = tm.tm_mon + 1;
    C.d = tm.tm_mday;
    C.wd = tm.tm_wday;
    const flex_palette_t *t = flex_th();
    int bw = lv_area_get_width(&C.area), bh = lv_area_get_height(&C.area), cw0, ch0;
    flex_ui_box(bw, bh, &cw0, &ch0);
    bw = cw0;
    bh = ch0;
    int pad = flex_ui_pad(bw, bh), gap = flex_ui_gap(pad);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "%s %d", flex_i18n_mo_full[flex_li()][C.mo - 1], C.y);   // el mes en minuscula como en Arduino
    int sideW, gridW;
    bool side = flex_cal_side(bw, pad, gap, &sideW, &gridW);
    snprintf(C.hdr, sizeof(C.hdr), "%s", hdr);
    C.side = side;
    C.cells = 0;
    C.dots = 0;

    int y = pad;
    int fsH = flex_ui_font_fit(hdr, gridW, flex_ui_font_h(bh / 12), flex_ui_text_w, NULL);
    flex_kit_text(C.root, hdr, fsH, t->txt, pad + gridW / 2, y, true);
    y += flex_ui_line_h(fsH) + gap / 2;

    // fila de los dias de la semana (empieza en domingo): la inicial del dia en el idioma activo (Arduino
    // la tenia fija en espanol "D L M M J V S": aqui sigue al idioma y en espanol sale igual)
    int cw = gridW / 7;
    int fsW = flex_ui_font_fit("W", cw - 2, 2, flex_ui_text_w, NULL);
    for (int i = 0; i < 7; i++) {
        char ini[8] = {0};
        const char *wd = flex_i18n_wd_short[flex_li()][i];
        int n = 0;
        ini[n++] = (char)((wd[0] >= 'a' && wd[0] <= 'z') ? wd[0] - 32 : wd[0]);   // inicial en mayuscula (ASCII)
        flex_kit_text(C.root, ini, fsW, t->txt2, pad + i * cw + cw / 2, y, true);
    }
    y += flex_ui_line_h(fsW) + gap / 2;

    int first = flex_cal_first(C.wd, C.d);
    int dim = flex_cal_dim(C.y, C.mo);
    int rows = flex_cal_rows(first, dim);
    C.first = first;
    C.rows = rows;
    int availH = bh - y - pad;
    int ch = availH / rows;
    if (ch < 12) {
        ch = 12;
    }
    int fsD = flex_ui_font_h(ch * 2 / 3);
    fsD = flex_ui_font_fit("88", cw - 2, fsD, flex_ui_text_w, NULL);   // dos cifras caben en la celda (Arduino no las encogia)
    int rad = (cw < ch ? cw : ch) / 2 - 2;
    if (rad < 6) {
        rad = 6;
    }
    for (int d = 1; d <= dim; d++) {
        int cell = first + d - 1, r = cell / 7, c = cell % 7;
        int cx = pad + c * cw + cw / 2, cy = y + r * ch;
        if (cy + ch > bh) {
            break;   // nunca fuera del marco
        }
        if (d == C.d) {   // hoy: seleccion
            C.dots++;
            lv_obj_t *dot = lv_obj_create(C.root);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 2 * rad, 2 * rad);
            lv_obj_set_pos(dot, cx - rad, cy + ch / 2 - rad);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, t->primary, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_clickable(dot, false);
        }
        C.cells++;
        char ds[12];   // sitio para cualquier int: el compilador no sabe que d <= 31
        snprintf(ds, sizeof(ds), "%d", d);
        flex_kit_text_vc(C.root, ds, fsD, d == C.d ? t->on_acc : t->txt, cx, cy + ch / 2);   // centrado en la celda / el circulo
    }
    if (side) {
        int sx = pad + gridW + gap, sy = pad, shh = bh - 2 * pad;
        lv_obj_t *p = flex_box(C.root);
        lv_obj_set_pos(p, sx, sy);
        lv_obj_set_size(p, sideW, shh);
        lv_obj_set_style_radius(p, pad, 0);
        flex_surface(p, FLEX_SURF_CARD, FLEX_BD_FLAT);
        lv_obj_set_clickable(p, false);
        flex_kit_text(p, "Hoy", flex_ui_font_fit("Hoy", sideW - 16, 3, flex_ui_text_w, NULL), t->txt2, sideW / 2, pad, true);
        char dd[8];
        snprintf(dd, sizeof(dd), "%d", C.d);
        int fsBig = flex_ui_font_fit(dd, sideW - 24, flex_ui_font_h(shh / 3), flex_ui_text_w, NULL);
        flex_kit_text(p, dd, fsBig, t->acc_soft, sideW / 2, shh / 2 - flex_ui_line_h(fsBig), true);
        char ld[64];
        flex_date_long(ld, sizeof(ld), C.wd, C.d, C.mo);
        flex_kit_text(p, ld, flex_ui_font_fit(ld, sideW - 16, 2, flex_ui_text_w, NULL), t->txt, sideW / 2,
                      shh / 2 + flex_ui_line_h(2), true);
    }
}

static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    C.root = root;
    C.area = *area;
    build();
}

static void on_resume(void)
{
    build();   // reanudar es repintar: no guarda ningun estado navegable propio
}

static void on_minute(void)
{
    struct tm tm;
    flex_clock_now(&tm);
    if (tm.tm_mday != C.d || tm.tm_mon + 1 != C.mo || tm.tm_year + 1900 != C.y) {
        build();   // cambio de dia
    }
}

static void on_close(void)
{
    memset(&C, 0, sizeof(C));
}

// Pruebas: lo dibujado
int flex_app_calendar_today(void)
{
    return C.d;
}
const char *flex_app_calendar_header(void)
{
    return C.hdr;
}
int flex_app_calendar_dots(void)
{
    return C.dots;
}
void flex_app_calendar_stats(int *cells, int *side, int *first, int *rows)
{
    *cells = C.cells;
    *side = C.side;
    *first = C.first;
    *rows = C.rows;
}

const flex_app_ops_t flex_app_calend_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_minute = on_minute,
    .on_close = on_close,
};
