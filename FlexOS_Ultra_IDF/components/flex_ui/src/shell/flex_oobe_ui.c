// Flex OS Ultra · primera configuracion (OOBE): idioma y nombre del equipo
// (docs/spec/01a §3; Arduino: FlexOS_Ultra_Home.h:323-438).
//
// Se entra con NVS "oobe" = false (placa virgen o tras restablecer). Sobre el
// fondo de pantalla, en colores claros fijos. El idioma cambia en vivo. El paso
// de Flex Account necesita Wi-Fi (Fase 6) y la cuenta (Fase 7): cuando existan
// se engancha en flex_oobe_account_step(); hoy el OOBE termina tras el nombre.
// Al terminar: "oobe" = true, "lang", "name" y la pantalla de bloqueo.
#include <stdio.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_oobe_model.h"
#include "flex_reset.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"

#define SCR_W 480
#define SCR_H 800
#define ROW_Y0 158
#define ROW_H  74
#define ROW_GAP 14

enum { O_NONE = 0, O_LANG, O_NAME };

static struct {
    lv_obj_t *root, *content, *title, *btn_l, *row[FLEX_NLANG], *row_l[FLEX_NLANG], *check[FLEX_NLANG];
    lv_obj_t *field_hint, *field_txt, *cursor;
    int view, sel;
    char name[24];
} O;

static void show(int view);

// Paso de Flex Account: lo aporta la cuenta cuando exista (Fases 6-7). done() lo cierra.
__attribute__((weak)) bool flex_oobe_account_step(void (*done)(void))
{
    (void)done;
    return false;   // no disponible: el OOBE sigue
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t a)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

// ---- idioma (enterOobeLang / oobeLangTick) -------------------------------------------------
static const char *lang_label(int i)
{
    return i == 5 ? "Chinese" : flex_i18n_lang_endonym[i];   // sin glifos CJK
}

// Check verde de 2 px: (380, cy) -> (386, cy+8) -> (400, cy-10)
static lv_obj_t *check_mark(lv_obj_t *p, int cy)
{
    static lv_point_precise_t pts[3] = {{0, 10}, {6, 18}, {20, 0}};   // igual para todas las filas
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, 3);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(0x28A05A), 0);
    lv_obj_set_pos(l, 380, cy - 10);
    lv_obj_set_clickable(l, false);
    return l;
}

static void lang_refresh(void)
{
    lv_label_set_text(O.title, flex_t(FLEX_S_SELLANG));
    flex_label_cap_center(O.title, SCR_W / 2, 78);
    lv_label_set_text(O.btn_l, flex_t(FLEX_S_CONTINUE));
    flex_label_cap_center(O.btn_l, 196, 725 - 704);
    for (int i = 0; i < FLEX_NLANG; i++) {
        bool on = i == O.sel;
        lv_obj_set_style_bg_opa(O.row[i], on ? 235 : 55, 0);
        lv_obj_set_style_text_color(O.row_l[i], on ? lv_color_hex(0x1C1C26) : lv_color_white(), 0);
        lv_obj_set_hidden(O.check[i], !on);
    }
}

static void lang_row_cb(lv_event_t *e)
{
    O.sel = (int)(intptr_t)lv_event_get_user_data(e);
    flex_set_lang(O.sel);   // el idioma cambia en vivo
    lang_refresh();
}

static void lang_go(void *arg)
{
    (void)arg;
    if (O.view == O_LANG) {
        show(O_NAME);
    }
}

static void lang_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(lang_go, NULL);
}

static void build_lang(lv_obj_t *p)
{
    O.title = flex_label(p, "", FLEX_FONT_S3, lv_color_white());
    for (int i = 0; i < FLEX_NLANG; i++) {
        int y = ROW_Y0 + i * (ROW_H + ROW_GAP);
        O.row[i] = box(p, 44, y, 392, ROW_H, 18, lv_color_white(), 55);
        lv_obj_set_clickable(O.row[i], true);
        lv_obj_add_event_cb(O.row[i], lang_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        O.row_l[i] = flex_label(O.row[i], lang_label(i), FLEX_FONT_S3, lv_color_white());
        lv_obj_set_x(O.row_l[i], 72 - 44);
        flex_label_cap_y(O.row_l[i], 27);
        O.check[i] = check_mark(p, y + 37);
    }
    lv_obj_t *b = box(p, 44, 704, 392, 60, 30, lv_color_white(), LV_OPA_COVER);
    lv_obj_set_clickable(b, true);
    lv_obj_add_event_cb(b, lang_btn_cb, LV_EVENT_CLICKED, NULL);
    O.btn_l = flex_label(b, "", FLEX_FONT_S3, lv_color_hex(0x2850C8));
    lang_refresh();
}

// ---- nombre (enterOobeName / oobeNameTick) ---------------------------------------------------
static void name_refresh(void)
{
    bool empty = O.name[0] == 0;
    lv_obj_set_hidden(O.field_hint, !empty);
    lv_obj_set_hidden(O.field_txt, empty);
    lv_obj_set_hidden(O.cursor, empty);
    if (!empty) {
        lv_label_set_text(O.field_txt, O.name);
        lv_obj_update_layout(O.field_txt);
        lv_obj_set_x(O.cursor, 64 - 44 + lv_obj_get_width(O.field_txt) + 2);   // cursor fijo tras el texto
    }
}

static void finish(void)
{
    char fin[24];
    flex_oobe_name_final(O.name, fin, sizeof(fin));
    flex_cfg_set_str("name", fin);
    flex_cfg_set_i32("lang", flex_lang());
    flex_cfg_set_bool("oobe", true);   // cfgSaveOobe
    lv_obj_delete(O.root);
    O.root = O.content = NULL;
    O.view = O_NONE;
    flex_shell_lock();   // el fin del OOBE lleva al bloqueo, no al escritorio
}

static void name_ok(void *arg)
{
    (void)arg;
    if (O.view != O_NAME) {
        return;
    }
    char fin[24];
    flex_oobe_name_final(O.name, fin, sizeof(fin));
    flex_cfg_set_str("name", fin);
    flex_cfg_set_i32("lang", flex_lang());
    if (!flex_oobe_account_step(finish)) {
        finish();
    }
}

static void key_cb(lv_event_t *e)
{
    char k = (char)(intptr_t)lv_event_get_user_data(e);
    if (k == '\n') {
        lv_async_call(name_ok, NULL);
        return;
    }
    if (flex_oobe_name_key(O.name, sizeof(O.name), k)) {
        name_refresh();
    }
}

static lv_obj_t *key(lv_obj_t *p, int x, int y, int w, const char *legend, char k)
{
    lv_obj_t *o = box(p, x, y, w, 52, 8, lv_color_hex(0xFAFAFC), LV_OPA_COVER);
    lv_obj_set_clickable(o, true);
    lv_obj_add_event_cb(o, key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
    if (legend) {
        lv_obj_t *l = flex_label(o, legend, FLEX_FONT_S2, lv_color_hex(0x1C1C26));
        flex_label_cap_center(l, w / 2, 19);
    }
    return o;
}

static void build_name(lv_obj_t *p)
{
    memset(O.name, 0, sizeof(O.name));   // enterOobeName vacia el nombre
    lv_obj_t *t = flex_label(p, flex_t(FLEX_S_YOURNAME), FLEX_FONT_S3, lv_color_white());
    flex_label_cap_center(t, SCR_W / 2, 70);
    lv_obj_t *f = box(p, 44, 150, 392, 64, 16, lv_color_white(), LV_OPA_COVER);
    O.field_hint = flex_label(f, flex_t(FLEX_S_NAMEHINT), FLEX_FONT_S2, lv_color_hex(0x96969E));
    lv_obj_set_x(O.field_hint, 64 - 44);
    flex_label_cap_y(O.field_hint, 174 - 150);
    O.field_txt = flex_label(f, "", FLEX_FONT_S3, lv_color_hex(0x18181E));
    lv_obj_set_x(O.field_txt, 64 - 44);
    flex_label_cap_y(O.field_txt, 172 - 150);
    O.cursor = box(f, 0, 166 - 150, 3, 32, 0, lv_color_hex(0x3778F0), LV_OPA_COVER);
    static const char *const R[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const int X0[3] = {13, 36, 36}, Y[3] = {544, 604, 664};
    for (int r = 0; r < 3; r++) {
        for (int i = 0; R[r][i]; i++) {
            char leg[2] = {R[r][i], 0};
            key(p, X0[r] + i * 46, Y[r], 40, leg, R[r][i]);
        }
    }
    key(p, 358, 664, 86, "<-", '\b');
    lv_obj_t *sp = key(p, 48, 724, 298, NULL, ' ');
    box(sp, 149 - 30, 24, 60, 4, 2, lv_color_hex(0x5A5A64), LV_OPA_COVER);
    key(p, 352, 724, 120, "OK", '\n');
    name_refresh();
}

// ---- vistas -------------------------------------------------------------------------------------
static void show(int view)
{
    if (!O.root) {
        return;
    }
    if (O.content) {
        lv_obj_delete(O.content);
    }
    O.view = view;
    O.content = flex_box(O.root);
    lv_obj_set_size(O.content, SCR_W, SCR_H);
    lv_obj_set_clickable(O.content, false);
    if (view == O_LANG) {
        build_lang(O.content);
    } else {
        build_name(O.content);
    }
    flex_touch_drop_all();
}

void flex_oobe_start(void)
{
    // Tras un restablecimiento terminado, el marcador se borra al llegar aqui
    // como aparato nuevo (enterOobeLang).
    flex_reset_confirm_clean_boot();
    flex_shell_oobe_begin();
    if (!O.root) {
        O.root = flex_box(flex_shell_screen());
        lv_obj_set_size(O.root, SCR_W, SCR_H);
        lv_obj_set_clickable(O.root, true);
        lv_obj_set_style_bg_opa(O.root, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(O.root, lv_color_hex(0x202434), 0);
        const lv_image_dsc_t *w = flex_wallmgr_image(FLEX_WALL_HOME);
        if (w) {
            lv_obj_t *img = lv_image_create(O.root);   // sobre el fondo de pantalla
            lv_image_set_src(img, w);
            lv_obj_set_clickable(img, false);
        }
        lv_obj_move_foreground(O.root);
    }
    // Arduino ponia el espanol siempre; si ya habia un idioma guardado se respeta
    O.sel = flex_lang();
    show(O_LANG);
}

bool flex_oobe_active(void)
{
    return O.root != NULL;
}

int flex_oobe_view(void)
{
    return O.view;
}

const char *flex_oobe_name(void)
{
    return O.name;
}
