// Flex OS Ultra · app Calculadora (docs/spec/03 §3; Arduino: FlexOS_Ultra_AppsBasic.h:33-337).
//
// Cuatro operaciones con la linea de la operacion visible ("25 + 10"), porcentaje, cambio de signo, borrado
// y "Error" explicito; sin teclas de memoria. La logica es flex_calc_model (probada tecla a tecla contra
// Arduino). Aqui: el reparto adaptativo del lienzo (la rejilla 5x4 tiene prioridad sobre el display), las
// teclas con su material y el ciclo de vida (la primera apertura tras el arranque recupera la operacion
// visible de la sesion calc.bin; las demas empiezan en "0").
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_calc_model.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_theme.h"

static const char *const LBL[5][4] = {{"C", "+/-", "%", "/"}, {"7", "8", "9", "x"}, {"4", "5", "6", "-"},
                                       {"1", "2", "3", "+"},   {"0", ".", "=", "DEL"}};
#define ORANGE lv_color_make(245, 150, 40)   // naranja de marca: igual en claro y oscuro

static flex_calc_t S;
static bool s_restore;                       // load_session dejo una operacion valida para la primera apertura
static struct {
    lv_obj_t *root, *disp, *text;
    int dw, dh;
} A;

// ---- reparto del lienzo (calcLayout, AppsBasic.h:179-218): m margen, gap hueco, dh alto del display --------
static void layout(int bw, int bh, int *m, int *gap, int *dh, int *kw, int *kh)
{
    int mm = bw / 30;
    mm = mm < 4 ? 4 : mm > 16 ? 16 : mm;
    if (4 * mm > bh) {
        mm = bh / 8;
        if (mm < 2) mm = 2;
    }
    int g = bw / 40;
    g = g < 3 ? 3 : g > 12 ? 12 : g;
    int avail = bh - 2 * mm;
    if (avail < 20) avail = 20;
    int need = 5 * 6 + 4 * g;                    // minimo vital de la rejilla
    while (g > 2 && need > avail * 3 / 4) {
        g--;
        need = 5 * 6 + 4 * g;
    }
    int d = avail / 5;                           // el display aspira a ~1/5...
    if (d > 120) d = 120;
    int max_d = avail - mm - need;               // ...pero nunca a costa de la rejilla
    if (d > max_d) d = max_d;
    if (d < 0) d = 0;
    int grid_h = avail - d - (d > 0 ? mm : 0);
    int h = (grid_h - 4 * g) / 5;
    int w = (bw - 2 * mm - 3 * g) / 4;
    *m = mm;
    *gap = g;
    *dh = d;
    *kh = h < 6 ? 6 : h;
    *kw = w < 6 ? 6 : w;
}

static const lv_font_t *font_for_size(int fs)
{
    return fs >= 5 ? FLEX_FONT_S5 : fs == 4 ? FLEX_FONT_S4 : fs == 3 ? FLEX_FONT_S3 : fs == 2 ? FLEX_FONT_S2 : FLEX_FONT_S1;
}

// Tamano de fuente segun la tecla, para que la etiqueta nunca se salga (calcFontFor)
static int key_fs(int bw, int bh, const char *t)
{
    int lim = bw < bh ? bw : bh;
    int fs = lim >= 56 ? 4 : lim >= 38 ? 3 : lim >= 24 ? 2 : 1;
    if (strlen(t) > 1 && fs > 1) fs--;
    while (fs > 1 && flex_text_width(t, font_for_size(fs)) > bw - 6) fs--;
    return fs;
}

static void refresh_display(void)
{
    if (!A.text) {
        return;
    }
    char ln[56];
    flex_calc_line(&S, ln, sizeof(ln));
    int dfs = A.dh >= 90 ? 5 : A.dh >= 64 ? 4 : A.dh >= 40 ? 3 : 2;
    while (dfs > 1 && flex_text_width(ln, font_for_size(dfs)) > A.dw - 20) dfs--;
    lv_obj_set_style_text_font(A.text, font_for_size(dfs), 0);
    lv_label_set_text(A.text, ln);
    lv_obj_set_width(A.text, A.dw - 20);
    flex_label_cap_y(A.text, A.dh / 2 - dfs * 4 + 4);
}

static void key_cb(lv_event_t *e)
{
    const char *lbl = lv_event_get_user_data(e);
    flex_calc_key(&S, flex_calc_key_from_label(lbl));
    refresh_display();                           // solo el display
    flex_session_mark_dirty(IC_CALC);            // la operacion visible es estado de sesion
}

static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    if (!s_restore) {
        flex_calc_reset(&S);                     // las aperturas que no son la primera tras el arranque empiezan en "0"
    }
    s_restore = false;
    int bw = lv_area_get_width(area), bh = lv_area_get_height(area);
    int m, gap, dh, kw, kh;
    layout(bw, bh, &m, &gap, &dh, &kw, &kh);
    A.root = root;
    A.dw = bw - 2 * m;
    A.dh = dh;
    A.disp = NULL;
    A.text = NULL;
    if (dh > 0) {                                // el display cede alto a la rejilla y puede omitirse
        int rad = dh / 5;
        rad = rad > 14 ? 14 : rad < 2 ? 2 : rad;
        A.disp = flex_box(root);
        lv_obj_set_pos(A.disp, m, m);
        lv_obj_set_size(A.disp, A.dw, dh);
        lv_obj_set_style_radius(A.disp, rad, 0);
        flex_surface(A.disp, FLEX_SURF_CARD, FLEX_BD_FLAT);
        A.text = flex_label(A.disp, "", FLEX_FONT_S5, flex_th()->txt);
        lv_obj_set_pos(A.text, 10, 0);
        lv_obj_set_style_text_align(A.text, LV_TEXT_ALIGN_RIGHT, 0);
        refresh_display();
    }
    int gy = m + dh + (dh > 0 ? m : 0);
    int rad = kw / 6;
    rad = rad > 14 ? 14 : rad < 3 ? 3 : rad;
    const flex_palette_t *t = flex_th();
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            const char *tl = LBL[r][c];
            bool acc = c == 3 || (r == 4 && c == 2);   // operadores y "=": naranja de marca
            lv_obj_t *k = flex_box(root);
            lv_obj_set_pos(k, m + c * (kw + gap), gy + r * (kh + gap));
            lv_obj_set_size(k, kw, kh);
            lv_obj_set_style_radius(k, rad, 0);
            if (acc) {
                flex_surface(k, FLEX_SURF_TINT, FLEX_BD_FLAT);
                flex_surface_set_tint(k, ORANGE);
            } else {
                flex_surface(k, r == 0 ? FLEX_SURF_ELEVATED : FLEX_SURF_CARD, FLEX_BD_FLAT);
            }
            lv_obj_set_clickable(k, true);
            lv_obj_add_event_cb(k, key_cb, LV_EVENT_CLICKED, (void *)tl);
            int fs = key_fs(kw, kh, tl);
            lv_obj_t *l = flex_label(k, tl, font_for_size(fs), acc ? t->on_acc : t->txt);
            flex_label_cap_center(l, kw / 2, kh / 2 - fs * 4 + 4);
            lv_obj_set_clickable(l, false);
        }
    }
}

static void on_close(void)
{
    A.root = A.disp = A.text = NULL;
}

// Sesion calc.bin v1 (docs/spec/01c 14.6): {double acc; char disp[24]; char op; u8 fresh, err, rsv}, 40 bytes
static bool save_session(void *out, size_t cap, size_t *len)
{
    flex_calc_sess_t v;
    if (cap < sizeof(v)) {
        return false;
    }
    flex_calc_pack(&S, &v);
    memcpy(out, &v, sizeof(v));
    *len = sizeof(v);
    return true;
}

static void load_session(const void *data, size_t len)
{
    flex_calc_reset(&S);
    s_restore = false;
    if (data && len == sizeof(flex_calc_sess_t)) {
        flex_calc_sess_t v;
        memcpy(&v, data, sizeof(v));
        s_restore = flex_calc_unpack(&S, &v);   // incoherente: se ignora y abre en "0"
    }
}

// Pruebas: la linea que se ve ahora
void flex_app_calc_line(char *out, size_t n)
{
    flex_calc_line(&S, out, n);
}

// Pruebas: el texto que el display muestra de verdad (la etiqueta LVGL, no el estado)
const char *flex_app_calc_display_text(void)
{
    return A.text ? lv_label_get_text(A.text) : "";
}

const flex_app_ops_t flex_app_calc_ops = {
    .on_create = on_create,
    .on_close = on_close,
    .save_session = save_session,
    .load_session = load_session,
};
