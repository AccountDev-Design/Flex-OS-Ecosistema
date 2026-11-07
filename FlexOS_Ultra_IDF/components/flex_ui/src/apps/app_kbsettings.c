// Flex OS Ultra · Ajustes del teclado (FlexOS_Ultra_KeyboardSettings.h; inventario A11k).
//
// Pantalla propia, a todo el alto, que se abre desde Ajustes > Personalizacion > Teclado y desde el engranaje de la barra del teclado de Notas
// (flex_kbsettings_open) y que al salir avisa a quien la abrio. Son 8 paginas navegables (principal, idiomas, diseno y tamano, deslizar y tocar,
// simbolos, atajos, editor de atajos, sobre teclado) que el modelo (flex_kbs_model.c, probado frente a Arduino) entrega como operaciones de dibujo;
// aqui solo hay LVGL: la lista con scroll nativo, la transicion de 90 ms segun el estilo de animacion del sistema y, en el editor de atajos, el TECLADO
// DEL SISTEMA (flex_kb) en vez de una copia dibujada: el mismo teclado, que escribe en el campo enfocado.
//
// Como Optimizar, es modal sobre la app en primer plano: Atras la cierra por paginas (flex_kbs_back), Inicio / Recientes / el bloqueo la cierran del todo.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_filekit.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_icons.h"
#include "flex_kb.h"
#include "flex_kbs_model.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_set_ui.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define KBS_ANIM_MS 90
#define KBS_VIEW_TOP (FLEX_KBS_TOP - 8)

static struct {
    lv_obj_t *root, *page, *view, *content, *ed, *kb;
    lv_obj_t *rowcard[FLEX_SET_ROW_MAX];
    int nrows;
    flex_kbs_t S;
    int scroll;
    bool open, animating;
    void (*on_back)(void);
} K;

static bool s_lang_es = true;   // kbLangEs: el idioma activo del teclado dura lo que el encendido
static flex_set_page_t PG;

static void page_build(bool animate);
static void apply(const flex_kbs_act_t *a);
void flex_kbs_back(void);

static void sync_lang(void)
{
    if (K.kb) {
        K.S.lang_es = flex_kb_get_lang_es(K.kb);   // el idioma que se dejo en el teclado del editor es el activo (kbLangEs es global en Arduino)
    }
    s_lang_es = K.S.lang_es;
}

static void exit_screen(void)
{
    if (!K.open) {
        return;
    }
    sync_lang();
    void (*cb)(void) = K.on_back;
    lv_anim_delete(&K, NULL);
    lv_obj_t *root = K.root;
    memset(&K, 0, sizeof(K));
    lv_obj_delete_async(root);   // se llega aqui desde el toque de un hijo suyo
    if (cb) {
        cb();   // quien la abrio vuelve a leer los ajustes del teclado (kbApplySize, kbChipsBuild...)
    }
}

static void go_page(int page)
{
    sync_lang();
    K.S.page = page;
    K.scroll = 0;
    page_build(true);
}

static void apply(const flex_kbs_act_t *a)
{
    if (a->flags & FLEX_KBSF_SAVE) {
        flex_kb_prefs_save(&K.S.pr);   // kbPrefsSave: solo las claves del teclado
    }
    // FLEX_KBSF_APPLY_SIZE / MT_RESET: el teclado lee sus ajustes al crearse o con flex_kb_reload_prefs; no hay superficie multitactil que reiniciar
    if (a->go >= 0) {
        go_page(a->go);
    } else if (a->render) {
        page_build(false);
    }
}

static void row_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (K.animating) {
        return;
    }
    flex_kbs_act_t a;
    flex_kbs_row_action(&K.S, i, &a);
    apply(&a);
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    flex_kbs_back();
}

static void scroll_end_cb(lv_event_t *e)
{
    (void)e;
    K.scroll = (int)lv_obj_get_scroll_y(K.view);
}

// ---- editor de atajos ------------------------------------------------------------------------------------------------------------------------------
static void ed_render(void)
{
    if (K.ed) {
        lv_obj_delete(K.ed);
    }
    K.ed = flex_set_box(K.page, 0, 0, 480, 800, 0);
    flex_set_page_t *pg = &PG;
    flex_kbs_build(pg, &K.S);
    flex_set_ops_render(K.ed, pg, FLEX_KBS_TOP, NULL, 0, NULL);
    if (K.kb) {
        lv_obj_move_foreground(K.kb);   // el teclado por encima de los campos
    }
}

static void ed_hit_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    flex_kbs_act_t a;
    if (code == FLEX_KBS_E_FIELD0 || code == FLEX_KBS_E_FIELD1) {
        flex_kbs_edit_focus(&K.S, code);
        ed_render();
        return;
    }
    if (code == FLEX_KBS_E_SAVE) {
        flex_kbs_edit_save(&K.S, &a);
    } else {
        flex_kbs_edit_delete(&K.S, &a);
    }
    apply(&a);
}

static lv_obj_t *ed_zone(int code, int x, int y, int w, int h)
{
    lv_obj_t *z = flex_set_box(K.page, x, y, w, h, 0);
    lv_obj_set_clickable(z, true);
    lv_obj_set_user_data(z, (void *)(intptr_t)code);
    lv_obj_add_event_cb(z, ed_hit_cb, LV_EVENT_CLICKED, NULL);
    return z;
}

static void kb_text_cb(lv_obj_t *kb, const char *utf8, void *user)
{
    (void)kb;
    (void)user;
    flex_kbs_edit_append(&K.S, utf8);
    ed_render();
}

static void kb_back_cb(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    if (flex_kbs_edit_backspace(&K.S)) {
        ed_render();
    }
}

static void kb_enter_cb(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    flex_kbs_act_t a;
    flex_kbs_edit_enter(&K.S, &a);
    apply(&a);
}

// ---- paginas ----------------------------------------------------------------------------------------------------------------------------------------------------
static void anim_exec(void *var, int32_t v)
{
    (void)var;
    if (!K.page) {
        return;
    }
    int style = flex_look()->anim_style;
    float p = (float)v / (float)KBS_ANIM_MS;
    if (style == 2) {   // deslizar desde la derecha
        lv_obj_set_x(K.page, (int)((1.0f - p) * 480));
    } else if (style == 1) {   // fundido
        lv_obj_set_style_opa(K.page, (lv_opa_t)(p * 255 < 1 ? 1 : p * 255), 0);
    } else {   // zoom del 88 % al 100 %
        lv_obj_set_style_transform_pivot_x(K.page, 240, 0);
        lv_obj_set_style_transform_pivot_y(K.page, 400, 0);
        lv_obj_set_style_transform_scale(K.page, (int32_t)(256.0f * (0.88f + 0.12f * p)), 0);
    }
}

static void anim_done(lv_anim_t *a)
{
    (void)a;
    K.animating = false;
    if (K.page) {   // fotograma final EXACTO
        lv_obj_set_x(K.page, 0);
        lv_obj_set_style_opa(K.page, LV_OPA_COVER, 0);
        lv_obj_set_style_transform_scale(K.page, 256, 0);
    }
}

static void page_build(bool animate)
{
    const flex_palette_t *t = flex_th();
    int keep = (K.view && !animate) ? (int)lv_obj_get_scroll_y(K.view) : K.scroll;
    if (K.kb) {
        sync_lang();
        lv_obj_delete(K.kb);
        K.kb = NULL;
    }
    if (K.page) {
        lv_obj_delete(K.page);
    }
    K.view = K.content = K.ed = NULL;
    K.nrows = 0;
    K.page = flex_set_box(K.root, 0, 0, 480, 800, 0);
    lv_obj_set_style_bg_color(K.page, t->page, 0);
    lv_obj_set_style_bg_opa(K.page, LV_OPA_COVER, 0);
    lv_obj_t *bz = flex_set_box(K.page, 0, 0, FLEX_KBS_BACK_X, FLEX_KBS_BACK_Y, 0);   // volver: x < 48, y < 52
    lv_obj_set_clickable(bz, true);
    lv_obj_add_event_cb(bz, back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *g = flex_glyph_create(bz, FLEX_GLYPH_CHEVRON_LEFT, 16, 28);
    lv_obj_set_pos(g, 16, 18);
    lv_obj_set_style_text_color(g, t->txt, 0);
    lv_obj_t *tt = flex_kit_text(K.page, flex_kbs_title(K.S.page), 3, t->txt, 52, 20, false);
    flex_label_one_line(tt, 480 - 52 - 8);
    if (K.S.page == FLEX_KBS_SCEDIT) {
        ed_render();
        ed_zone(FLEX_KBS_E_FIELD0, 0, FLEX_KBS_TOP + 18, 480, 43);
        ed_zone(FLEX_KBS_E_FIELD1, 0, FLEX_KBS_TOP + 76 + 18, 480, 43);
        int hw = (480 - 36) / 2;
        ed_zone(FLEX_KBS_E_SAVE, 12, FLEX_KBS_TOP + 160, hw + 1, 47);
        ed_zone(FLEX_KBS_E_DELETE, 24 + hw, FLEX_KBS_TOP + 160, 480 - 24 - hw, 47);
        flex_kb_cfg_t kc = {
            .flags = FLEX_KB_F_PAGE | FLEX_KB_F_NO_FAST,   // sin barra ni chips (kbExtrasOn = false)
            .backdrop = FLEX_BD_FLAT,
            .bottom_reserve = (int16_t)(flex_navbar_ctx() != FLEX_NAV_HIDDEN ? FLEX_NAV_H : 0),   // la barra de navegacion del sistema sigue a la vista: el teclado se apoya sobre ella
            .enter_label = "OK",
            .on_text = kb_text_cb,
            .on_backspace = kb_back_cb,
            .on_enter = kb_enter_cb,
        };
        K.kb = flex_kb_create(K.page, &kc);
        flex_kb_set_lang_es(K.kb, K.S.lang_es);
        ed_render();   // vuelve a poner el teclado por encima
    } else {
        flex_kbs_build(&PG, &K.S);
        K.view = flex_set_box(K.page, 0, KBS_VIEW_TOP, 480, FLEX_KBS_BOT - KBS_VIEW_TOP, 0);
        lv_obj_set_clickable(K.view, true);
        lv_obj_set_scrollable(K.view, true);
        lv_obj_set_scroll_dir(K.view, LV_DIR_VER);
        lv_obj_set_scroll_elastic(K.view, false);
        lv_obj_set_scroll_momentum(K.view, false);
        lv_obj_set_scrollbar_mode(K.view, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_event_cb(K.view, scroll_end_cb, LV_EVENT_SCROLL_END, NULL);
        int h = PG.content_h + 8;
        if (h < FLEX_KBS_BOT - KBS_VIEW_TOP) {
            h = FLEX_KBS_BOT - KBS_VIEW_TOP;
        }
        K.content = flex_set_box(K.view, 0, 0, 480, h, 0);
        K.nrows = PG.rows;
        flex_set_ops_render(K.content, &PG, 8, K.rowcard, FLEX_SET_ROW_MAX, row_click_cb);   // el contenido empieza 8 px dentro de la banda
        lv_obj_update_layout(K.view);
        lv_obj_scroll_to_y(K.view, keep, LV_ANIM_OFF);
    }
    if (animate) {
        K.animating = true;
        anim_exec(NULL, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, &K);
        lv_anim_set_exec_cb(&a, anim_exec);
        lv_anim_set_values(&a, 0, KBS_ANIM_MS);
        lv_anim_set_duration(&a, KBS_ANIM_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_set_completed_cb(&a, anim_done);
        lv_anim_start(&a);
    }
}

// ---- API ------------------------------------------------------------------------------------------------------------------------------------------------------------------
void flex_kbsettings_open(void (*on_back)(void))
{
    if (K.open) {
        return;
    }
    flex_kb_prefs_t pr;
    flex_kb_prefs_load(&pr);
    memset(&K, 0, sizeof(K));
    flex_kbs_init(&K.S, &pr, s_lang_es, 1);   // el indev de LVGL da UN punto: la escritura rapida lo dice con ese "1 dedo"
    K.on_back = on_back;
    K.open = true;
    flex_touch_drop_all();   // el toque que abrio la pantalla no se filtra
    K.root = flex_set_box(flex_shell_screen(), 0, 0, 480, 800, 0);
    lv_obj_set_clickable(K.root, true);   // modal: nada de lo de debajo recibe toques
    lv_obj_move_foreground(K.root);
    page_build(false);
}

bool flex_kbs_active(void)
{
    return K.open;
}

void flex_kbs_close_now(void)
{
    exit_screen();
}

// Atras: de una pagina a la que la abrio; de la principal, sale
void flex_kbs_back(void)
{
    if (!K.open || K.animating) {
        return;
    }
    int tg = flex_kbs_back_target(K.S.page);
    if (tg < 0) {
        exit_screen();
    } else {
        go_page(tg);
    }
}

// ---- pruebas ---------------------------------------------------------------------------------------------------------------------------------------------------------
int flex_kbs_page(void) { return K.open ? K.S.page : -1; }
bool flex_kbs_animating(void) { return K.animating; }
int flex_kbs_rows(void) { return K.nrows; }
int flex_kbs_scroll(void) { return K.view ? (int)lv_obj_get_scroll_y(K.view) : K.scroll; }
lv_obj_t *flex_kbs_kb(void) { return K.kb; }
const flex_kbs_t *flex_kbs_state(void) { return K.open ? &K.S : NULL; }
bool flex_kbs_row_xy(int i, int *x, int *y)
{
    if (i < 0 || i >= K.nrows || !K.rowcard[i]) return false;
    lv_area_t a;
    lv_obj_get_coords(K.rowcard[i], &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}
static bool find_text(lv_obj_t *o, const char *txt)
{
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), txt)) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        if (find_text(lv_obj_get_child(o, (int32_t)i), txt)) {
            return true;
        }
    }
    return false;
}
bool flex_kbs_shown(const char *txt)
{
    return K.page && find_text(K.page, txt);
}
