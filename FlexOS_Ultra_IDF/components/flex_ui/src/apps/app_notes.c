// Flex OS Ultra · App Notas (docs/spec/03 §6; FlexOS_Ultra_AppNotes.h + el editor de Keyboard.h:700-1564).
//
// Cada nota es un fichero REAL .txt en /Notas: el titulo es el nombre sin extension y la vista previa sus primeros
// bytes. Dos vistas: la lista (tarjetas en 2 columnas, boton flotante, modo seleccion, menu contextual, papelera) y el
// editor (hoja con texto, cursor, seleccion con manijas, menu Cortar/Copiar/Pegar/Todo, teclado del sistema con barra y
// chips, portapapeles de 12 ranuras, autoguardado a los 2 s). Toda la logica sin graficos esta en flex_ned.c,
// flex_clip.c, flex_note_model.c y flex_fk_model.c y se prueba frente a Arduino; aqui solo hay LVGL y el pegamento.
//
// La UI nunca toca la flash: leer, escribir, listar, borrar, renombrar y tirar a la papelera son trabajos de la tarea de
// almacenamiento (flex_fs_*_async) y la lista se vuelve a leer del disco al volver. Una respuesta tardia (de una lista o
// una lectura que ya no se espera) se descarta por su generacion.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifndef FLEX_SIM
#include "esp_heap_caps.h"
#endif
#include "flex_app.h"
#include "flex_clip.h"
#include "flex_clock.h"
#include "flex_filekit.h"
#include "flex_frame.h"
#include "flex_fs_names.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_kb.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_ned.h"
#include "flex_note_model.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "widgets/flex_app_ids.h"

#define NOTES_DIR       "/Notas"
#define NOTE_EXT        ".txt"
#define NOTE_BUF_MAX    16384   // buffer de trabajo del editor: una nota mayor NO se abre (Arduino la truncaba y la guardaba truncada)
#define NOTE_AUTOSAVE_MS 2000
#define NOTE_LONG_MS    500     // pulsacion larga en el texto -> seleccionar palabra
#define NOTE_TAP_MOVE   12      // umbral de arrastre en el texto

typedef struct {
    lv_obj_t *root;
    int view;                          // 0 lista, 1 editor
    bool created;
    uint32_t gen;                      // generacion: una respuesta con otra generacion se descarta
    // ---- lista
    lv_obj_t *lroot, *lview, *lcontent, *fab, *bar;
    flex_fs_ent_t ents[FLEX_NL_MAX];
    char prev[FLEX_NL_MAX][FLEX_NL_PREV_LEN];
    bool prev_ok[FLEX_NL_MAX];
    int n;
    bool overflow, overflow_told;
    bool list_ready;
    int sel_idx;                       // elemento sobre el que actua el menu
    bool multi;
    uint64_t mask;
    int list_scroll;                   // el de la sesion o el ultimo conocido
    bool long_fired;
    int trash_i;                       // cola de "tirar los marcados"
    // ---- editor
    lv_obj_t *eroot, *tv, *kb, *menu_ov, *chip_ov, *clip_ov, *more_ov, *back_zone;
    lv_timer_t *timer;
    char path[96];
    char title[FLEX_FS_NAME_MAX];
    char *buf;
    flex_ned_t E;
    uint32_t saved_rev;
    uint32_t last_change;              // lv_tick del ultimo cambio del texto (autoguardado)
    uint32_t seen_rev;                 // ultima revision del texto vista por editor_changed
    bool save_failed_told;
    uint8_t kb_layout, kb_flags;       // estado del teclado capturado al suspender
    bool kb_state_valid;
    // gestos en el texto
    bool g_down, g_drag, g_long;
    int g_handle;                      // 0 no, 1 manija izquierda, 2 derecha
    int g_x0, g_y0, g_scroll0;
    uint32_t g_t0;
    // ---- portapapeles
    flex_clip_t clip;
    bool clip_ready, clip_filter_pin, clip_ask_clear;
    // ---- sesion pendiente de aplicar al abrir
    flex_note_sess_t sess;
    bool sess_valid;
    bool restore;                      // reabrir el editor de la sesion
} notes_t;

static notes_t N;

static const flex_palette_t *TH(void)
{
    return flex_th();
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void fill(lv_obj_t *o, lv_color_t c, lv_opa_t a)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
}

static void *big_alloc(size_t n)
{
#ifndef FLEX_SIM
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // el texto no es para la RAM interna
    if (p) {
        return p;
    }
#endif
    return calloc(1, n);
}

static void tap_point(lv_obj_t *o, int *x, int *y)
{
    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(o, &a);
    *x = (int)p.x - (int)a.x1;
    *y = (int)p.y - (int)a.y1;
}

static int nav_reserve(void)
{
    return flex_navbar_ctx() != FLEX_NAV_HIDDEN ? FLEX_NAV_H : 0;
}

static void mark_dirty(void)
{
    flex_session_mark_dirty(IC_NOTAS);
}

// ===== lista ===============================================================================================================
static void show_list(void);
static void list_reload(void);
static void editor_open(const char *path, bool restore);
static void editor_to_list(void);
static void list_render(void);

static void stem_of(const flex_fs_ent_t *e, char *out, size_t n)
{
    flex_fsn_stem(e->name, out, n);
}

static void path_of(int i, char *out, size_t n)
{
    snprintf(out, n, NOTES_DIR "/%.47s", N.ents[i].name);
}

static int ent_cmp(const void *a, const void *b)
{
    return strcasecmp(((const flex_fs_ent_t *)a)->name, ((const flex_fs_ent_t *)b)->name);
}

static void prev_next(int i);

static void prev_cb(esp_err_t err, uint8_t *data, size_t len, void *user)
{
    uint32_t tok = (uint32_t)(uintptr_t)user;
    int i = (int)(tok & 0xFFFF);
    if ((tok >> 16) != (N.gen & 0xFFFF) || !N.created || N.view != 0 || i >= N.n) {
        free(data);
        return;   // otra lista o la vista ya no es esta
    }
    N.prev[i][0] = '\0';
    if (err == ESP_OK && data) {
        size_t n = len < FLEX_NL_PREV_LEN - 1 ? len : FLEX_NL_PREV_LEN - 1;
        memcpy(N.prev[i], data, n);
        N.prev[i][n] = '\0';
    }
    free(data);
    N.prev_ok[i] = true;
    list_render();   // la tarjeta i ya tiene su vista previa real
    prev_next(i + 1);
}

static void prev_next(int i)
{
    if (i >= N.n) {
        return;
    }
    char p[FLEX_FS_PATH_MAX];
    path_of(i, p, sizeof(p));
    // de una en una: la cola del almacenamiento es corta y 64 lecturas seguidas la desbordarian
    if (flex_fs_read_head_async(p, FLEX_NL_PREV_LEN - 1, prev_cb, (void *)(uintptr_t)(((N.gen & 0xFFFF) << 16) | (uint32_t)i)) != ESP_OK) {
        N.prev[i][0] = '\0';
        N.prev_ok[i] = true;
        prev_next(i + 1);
    }
}

static void list_cb(esp_err_t err, const flex_fs_ent_t *ents, size_t n, void *user)
{
    if ((uint32_t)(uintptr_t)user != N.gen || !N.created || N.view != 0) {
        return;
    }
    N.n = 0;
    N.overflow = false;
    if (err == ESP_OK) {
        // todas las notas, ordenadas ANTES de recortar (Arduino recortaba a 16 antes de ordenar y perdia notas)
        static flex_fs_ent_t all[256];
        size_t k = 0;
        for (size_t i = 0; i < n && k < 256; i++) {
            if (!ents[i].dir) {   // una carpeta no es una nota (Arduino abria un editor sobre ella)
                all[k++] = ents[i];
            }
        }
        qsort(all, k, sizeof(all[0]), ent_cmp);
        N.overflow = k > FLEX_NL_MAX;
        N.n = (int)(k > FLEX_NL_MAX ? FLEX_NL_MAX : k);
        memcpy(N.ents, all, (size_t)N.n * sizeof(N.ents[0]));
    }
    memset(N.prev_ok, 0, sizeof(N.prev_ok));
    for (int i = 0; i < N.n; i++) {
        N.prev[i][0] = '\0';
    }
    if (N.mask >> N.n) {
        N.mask &= (N.n >= 64) ? ~0ull : ((1ull << N.n) - 1);   // el modo seleccion no deja marcas fuera de la lista
    }
    if (N.sel_idx >= N.n) {
        N.sel_idx = -1;
    }
    N.list_ready = true;
    list_render();
    if (N.overflow && !N.overflow_told) {
        N.overflow_told = true;
        flex_kit_toast(N.root, "Solo se muestran las primeras 64 notas");
    }
    prev_next(0);
}

static void list_reload(void)
{
    N.gen++;
    if (flex_fs_list_async(NOTES_DIR, 256, list_cb, (void *)(uintptr_t)N.gen) != ESP_OK) {
        list_cb(ESP_FAIL, NULL, 0, (void *)(uintptr_t)N.gen);
    }
}

// ---- acciones -----------------------------------------------------------------------------------------------------------
static void done_reload_cb(esp_err_t err, void *user)
{
    (void)user;
    if (!N.created || N.view != 0) {
        return;
    }
    (void)err;   // si algo fallo el elemento sigue en la lista: se relee del disco, que es la verdad
    list_reload();
}

static void note_created_cb(esp_err_t err, const char *path, void *user)
{
    (void)user;
    if (!N.created || N.view != 0) {
        return;
    }
    if (err != ESP_OK || !path[0]) {
        flex_kit_toast(N.root, "No se pudo crear la nota");   // disco lleno: no aparece nada, porque no existe
        return;
    }
    editor_open(path, false);
}

static void note_new(void)
{
    flex_fs_new_file_async(NOTES_DIR, "Sin t\xC3\xAD" "tulo", NOTE_EXT, note_created_cb, NULL);
}

static void trash_next_cb(esp_err_t err, void *user)
{
    (void)err;
    (void)user;
    if (!N.created || N.view != 0) {
        return;
    }
    while (N.trash_i < N.n && !(N.mask & (1ull << N.trash_i))) {
        N.trash_i++;
    }
    if (N.trash_i >= N.n) {
        N.multi = false;
        N.mask = 0;
        list_reload();
        return;
    }
    char p[FLEX_FS_PATH_MAX];
    path_of(N.trash_i++, p, sizeof(p));
    if (flex_fs_trash_async(p, trash_next_cb, NULL) != ESP_OK) {
        trash_next_cb(ESP_FAIL, NULL);
    }
}

static void ask_del_cb(bool ok, void *user)
{
    (void)user;
    if (!N.created || N.view != 0) {
        return;
    }
    if (ok && N.sel_idx >= 0 && N.sel_idx < N.n) {
        char p[FLEX_FS_PATH_MAX];
        path_of(N.sel_idx, p, sizeof(p));
        N.sel_idx = -1;
        if (flex_fs_delete_async(p, done_reload_cb, NULL) == ESP_OK) {   // borrado DEFINITIVO real
            return;
        }
    }
    N.sel_idx = -1;
    list_render();
}

static void rename_done_cb(esp_err_t err, void *user)
{
    (void)user;
    if (!N.created || N.view != 0) {
        return;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        flex_kit_toast(N.root, "Ya hay una nota con ese nombre");   // Arduino no decia nada
    } else if (err == ESP_ERR_INVALID_ARG) {
        flex_kit_toast(N.root, "Nombre demasiado largo");
    } else if (err != ESP_OK) {
        flex_kit_toast(N.root, "No se pudo renombrar");
    }
    list_reload();
}

static void name_cb(const char *name, void *user)
{
    (void)user;
    if (!N.created || N.view != 0) {
        return;
    }
    if (name && N.sel_idx >= 0 && N.sel_idx < N.n) {
        char p[FLEX_FS_PATH_MAX];
        path_of(N.sel_idx, p, sizeof(p));
        N.sel_idx = -1;
        if (flex_fs_rename_name_async(p, name, rename_done_cb, NULL) == ESP_OK) {
            return;
        }
    }
    N.sel_idx = -1;
    list_render();
}

static void trash_exit_cb(void *user)
{
    (void)user;
    if (N.created && N.view == 0) {
        list_reload();   // la papelera pudo restaurar notas
    }
}

static void menu_cb(int act, void *user)
{
    (void)user;
    if (!N.created || N.view != 0) {
        return;
    }
    bool have = N.sel_idx >= 0 && N.sel_idx < N.n;
    char stem[FLEX_FS_NAME_MAX] = "";
    if (have) {
        stem_of(&N.ents[N.sel_idx], stem, sizeof(stem));
    }
    switch (act) {
    case FLEX_FK_ACT_SEL:
        N.multi = true;
        N.mask = have ? (1ull << N.sel_idx) : 0;
        break;
    case FLEX_FK_ACT_DEL:
        if (have) {
            flex_fk_ask_open(N.root, "\xC2\xBF" "Borrar definitivamente?", stem, ask_del_cb, NULL);
            return;   // el borrado ocurre al confirmar
        }
        break;
    case FLEX_FK_ACT_REN:
        if (have) {
            flex_fk_name_open(N.root, "Renombrar nota", stem, NULL, name_cb, NULL);
            return;
        }
        break;
    case FLEX_FK_ACT_TRASH:
        if (have) {
            char p[FLEX_FS_PATH_MAX];
            path_of(N.sel_idx, p, sizeof(p));
            N.sel_idx = -1;
            if (flex_fs_trash_async(p, done_reload_cb, NULL) == ESP_OK) {   // a /Papelera de verdad
                return;
            }
        } else {
            flex_fk_trash_open(N.root, trash_exit_cb, NULL);   // sin seleccion (tres puntos): abre la papelera
            return;
        }
        break;
    default:
        break;
    }
    list_render();
}

static void header_menu_cb(lv_event_t *e)
{
    (void)e;
    if (flex_fk_any(N.root)) {
        return;
    }
    N.sel_idx = -1;
    flex_fk_menu_open(N.root, 440, 56, false, menu_cb, NULL);   // desde los tres puntos, sin seleccion
}

static void card_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (N.long_fired) {
        N.long_fired = false;   // soltar tras la pulsacion larga no es un toque
        return;
    }
    if (flex_fk_any(N.root) || i < 0 || i >= N.n) {
        return;
    }
    if (N.multi) {
        N.mask ^= 1ull << i;
        list_render();
    } else {
        char p[FLEX_FS_PATH_MAX];
        path_of(i, p, sizeof(p));
        editor_open(p, false);
    }
}

static void card_long_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (flex_fk_any(N.root) || i < 0 || i >= N.n) {
        return;
    }
    N.long_fired = true;
    N.sel_idx = i;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    flex_fk_menu_open(N.root, (int)p.x, (int)p.y - 40, false, menu_cb, NULL);
}

static void card_press_cb(lv_event_t *e)
{
    (void)e;
    N.long_fired = false;
}

static void fab_click_cb(lv_event_t *e)
{
    (void)e;
    if (!flex_fk_any(N.root) && !N.multi) {
        note_new();
    }
}

static void bar_click_cb(lv_event_t *e)
{
    int x, y;
    tap_point(lv_event_get_current_target_obj(e), &x, &y);
    // la barra esta en (12, 672): el toque llega en coordenadas de la barra
    int a = flex_nl_bar_hit(x + 12, y + (800 - 128));
    if (flex_fk_any(N.root)) {
        return;
    }
    if (a == 1) {   // Salir
        N.multi = false;
        N.mask = 0;
        list_render();
    } else if (a == 2) {   // Papelera: todas las marcadas, sin confirmacion
        N.trash_i = 0;
        trash_next_cb(ESP_OK, NULL);
    }
}

static void card_make(int i)
{
    const flex_palette_t *t = TH();
    int x, y, w, h;
    flex_nl_card_rect(i, 0, &x, &y, &w, &h);
    y -= FLEX_NL_VIEW_Y;   // el contenido empieza en la y del viewport
    char title[FLEX_FS_NAME_MAX];
    stem_of(&N.ents[i], title, sizeof(title));
    lv_obj_t *tl = flex_kit_text(N.lcontent, title, 2, t->txt, x + w / 2, y - 34, true);   // el titulo, 34 px POR ENCIMA de la tarjeta
    flex_label_one_line(tl, w + 12);
    lv_obj_set_x(tl, x + w / 2 - (w + 12) / 2);
    lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *c = box(N.lcontent, x, y, w, h, 14);
    flex_surface(c, FLEX_SURF_CARD, FLEX_BD_FLAT);
    lv_obj_set_clickable(c, true);
    lv_obj_set_user_data(c, (void *)(intptr_t)i);
    lv_obj_add_event_cb(c, card_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(c, card_long_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(c, card_press_cb, LV_EVENT_PRESSED, NULL);
    if (N.multi && (N.mask & (1ull << i))) {
        lv_obj_set_style_border_width(c, 2, 0);   // doble contorno PRIM
        lv_obj_set_style_border_color(c, t->primary, 0);
        lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
        lv_obj_t *ck = box(c, w - 20 - 11, 20 - 11, 22, 22, LV_RADIUS_CIRCLE);
        fill(ck, t->primary, LV_OPA_COVER);
        lv_obj_t *g = flex_glyph_create(ck, FLEX_GLYPH_CHECK, 22, 22);
        lv_obj_set_style_text_color(g, t->on_acc, 0);
    }
    if (N.prev_ok[i]) {
        if (N.prev[i][0]) {
            flex_fk_text_box(c, 10, 10, w - 20, h - 20, N.prev[i], 2, t->txt);   // vista previa REAL, por caracteres
        } else {
            flex_kit_text(c, "(vac\xC3\xAD" "a)", 2, t->mute, 10, 10, false);
        }
    }
}

// Reconstruye las tarjetas y la barra / el boton flotante con lo que hay en memoria.
static void list_render(void)
{
    if (!N.lroot || N.view != 0) {
        return;
    }
    const flex_palette_t *t = TH();
    int keep = N.list_ready ? (int)lv_obj_get_scroll_y(N.lview) : N.list_scroll;
    lv_obj_clean(N.lcontent);
    lv_obj_set_size(N.lcontent, 480, 740 + flex_nl_max_scroll(N.n));
    if (N.list_ready && N.n == 0) {
        flex_kit_text(N.lcontent, "No hay notas todav\xC3\xAD" "a", 3, t->txt2, 240, 320 - FLEX_NL_VIEW_Y, true);
        flex_kit_text(N.lcontent, "Pulsa el boton de abajo para crear una", 1, t->mute, 240, 364 - FLEX_NL_VIEW_Y, true);
    }
    for (int i = 0; i < N.n; i++) {
        card_make(i);
    }
    lv_obj_update_layout(N.lview);
    lv_obj_scroll_to_y(N.lview, keep, LV_ANIM_OFF);
    // el boton flotante o la barra de seleccion
    lv_obj_set_hidden(N.fab, N.multi);
    lv_obj_set_hidden(N.bar, !N.multi);
}

static void list_scroll_cb(lv_event_t *e)
{
    (void)e;
    N.list_scroll = (int)lv_obj_get_scroll_y(N.lview);
    mark_dirty();   // el desplazamiento de la lista es estado de sesion
}

static void list_build(void)
{
    const flex_palette_t *t = TH();
    N.lroot = box(N.root, 0, 0, 480, 800, 0);
    fill(N.lroot, t->page, LV_OPA_COVER);
    flex_app_header(N.lroot, "Notas:", true, header_menu_cb);
    N.lview = box(N.lroot, 0, FLEX_NL_VIEW_Y, 480, 800 - FLEX_NL_VIEW_Y, 0);
    lv_obj_set_clickable(N.lview, true);
    lv_obj_set_scrollable(N.lview, true);
    lv_obj_set_scroll_dir(N.lview, LV_DIR_VER);
    lv_obj_set_scroll_elastic(N.lview, false);
    lv_obj_set_scroll_momentum(N.lview, false);
    lv_obj_set_scrollbar_mode(N.lview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(N.lview, list_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    N.lcontent = box(N.lview, 0, 0, 480, 740, 0);
    // boton flotante: circulo r = 56 en (396, 650) con borde de 5 px y la hoja con el lapiz
    int fx, fy, fr;
    flex_nl_fab(&fx, &fy, &fr);
    N.fab = box(N.lroot, fx - fr, fy - fr, 2 * fr, 2 * fr, LV_RADIUS_CIRCLE);
    fill(N.fab, t->surf, LV_OPA_COVER);
    lv_obj_set_style_border_width(N.fab, 5, 0);
    lv_obj_set_style_border_color(N.fab, t->border, 0);
    lv_obj_set_style_border_opa(N.fab, LV_OPA_COVER, 0);
    lv_obj_set_clickable(N.fab, true);
    lv_obj_add_event_cb(N.fab, fab_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *g = flex_glyph_create(N.fab, FLEX_GLYPH_NOTES_FAB, 2 * fr - 10, 2 * fr - 10);
    lv_obj_set_pos(g, 0, 0);
    lv_obj_set_style_text_color(g, t->txt, 0);
    lv_obj_set_style_bg_color(g, t->surf, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    // barra de seleccion (12, 672, 456, 60)
    N.bar = box(N.lroot, 12, 800 - 128, 456, 60, 16);
    flex_surface(N.bar, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    lv_obj_set_clickable(N.bar, true);
    lv_obj_add_event_cb(N.bar, bar_click_cb, LV_EVENT_CLICKED, NULL);
    flex_kit_text(N.bar, "Selecci\xC3\xB3n", 2, t->txt, 28 - 12, 692 - (800 - 128), false);
    lv_obj_t *p = flex_kit_text(N.bar, "Papelera", 2, t->warn, 0, 692 - (800 - 128), false);
    lv_obj_set_width(p, 340 - 12);   // alineado a la derecha en x = 340
    lv_obj_set_style_text_align(p, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *s = flex_kit_text(N.bar, "Salir", 2, t->txt2, 0, 692 - (800 - 128), false);
    lv_obj_set_width(s, 452 - 12);   // alineado a la derecha en x = 452
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_hidden(N.bar, true);
}

// ===== editor ================================================================================================================
static const lv_font_t *F2(void)
{
    return FLEX_FONT_S2;
}

// Avance del glifo (como (int)(FG[..].adv * sc + 0.5f) de Arduino): lo que mide la fuente de LVGL; un caracter que la fuente no
// tiene se dibuja y se mide como '?'.
static uint32_t glyph_cp(uint32_t cp)
{
    lv_font_glyph_dsc_t g;
    return lv_font_get_glyph_dsc(F2(), &g, cp, 0) ? cp : '?';
}

static int adv_cb(uint32_t cp, void *ctx)
{
    (void)ctx;
    int w = (int)lv_font_get_glyph_width(F2(), glyph_cp(cp), 0);
    return w > 0 ? w : 1;
}

static int txt_bot(void)   // noteTxtBot() = kbPanelTop() - 8
{
    return (N.kb ? (int)flex_kb_top(N.kb) : 800 - 64 - 240) - 8;
}

static void editor_changed(void);
static void overlay_update(void);
static void clip_close(void);

static bool dirty_now(void)
{
    return N.view == 1 && N.buf && N.E.rev != N.saved_rev;
}

static void save_done_cb(esp_err_t err, void *user)
{
    uint32_t rev = (uint32_t)(uintptr_t)user;
    if (err == ESP_OK) {
        if ((int32_t)(rev - N.saved_rev) > 0) {
            N.saved_rev = rev;   // lo que se grabo; si hubo cambios despues, el autoguardado los vuelve a coger
        }
        N.save_failed_told = false;
    } else if (N.created && !N.save_failed_told) {
        N.save_failed_told = true;
        flex_kit_toast(N.root, "No se pudo guardar");   // el texto sigue en memoria y se reintenta
    }
}

// Guardado REAL de la nota: reescritura completa y atomica en la tarea de almacenamiento.
static void editor_save(void)
{
    if (!dirty_now() || !N.path[0]) {
        return;
    }
    uint32_t rev = N.E.rev;
    if (flex_fs_write_async(N.path, N.buf, strlen(N.buf), save_done_cb, (void *)(uintptr_t)rev) != ESP_OK) {
        return;   // no cabia en la cola: sigue sucia y el autoguardado lo reintenta
    }
}

static void autosave_tick(lv_timer_t *t)
{
    (void)t;
    // 2 s despues de la ultima modificacion real del texto
    if (dirty_now() && lv_tick_elaps(N.last_change) > NOTE_AUTOSAVE_MS) {
        N.last_change = lv_tick_get() - NOTE_AUTOSAVE_MS / 2;   // si la cola esta llena, reintenta en 1 s
        editor_save();
    }
}

typedef struct {
    lv_layer_t *L;
    int ox, oy, cap;
    const flex_palette_t *t;
    const lv_font_t *f;
    const flex_kb_prefs_t *pr;
    bool es;
} draw_ctx_t;

static void draw_glyph_cb(const flex_ned_glyph_t *g, void *u)
{
    draw_ctx_t *x = u;
    if (g->selected) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = x->t->sel;
        d.bg_opa = LV_OPA_COVER;
        lv_area_t a = {x->ox + g->x - 1, x->oy + g->y - 2, x->ox + g->x - 1 + g->w, x->oy + g->y - 2 + FLEX_NED_LH - 5};
        lv_draw_rect(x->L, &d, &a);
    }
    // un glifo por llamada: el avance de Arduino es el de cada glifo, sin el kerning que aplicaria una linea entera
    char ch[5];
    uint32_t cp = glyph_cp(g->cp);
    int n = 0;
    if (cp < 0x80) {
        ch[n++] = (char)cp;
    } else if (cp < 0x800) {
        ch[n++] = (char)(0xC0 | (cp >> 6));
        ch[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        ch[n++] = (char)(0xE0 | (cp >> 12));
        ch[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        ch[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        ch[n++] = (char)(0xF0 | (cp >> 18));
        ch[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        ch[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        ch[n++] = (char)(0x80 | (cp & 0x3F));
    }
    ch[n] = '\0';
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.text = ch;
    ld.text_local = 1;   // LVGL guarda su propia copia: ch vive en esta pila
    ld.font = x->f;
    ld.color = x->t->txt;
    ld.opa = LV_OPA_COVER;
    int top = x->oy + g->y - x->cap;   // la caja de linea empieza x->cap por encima de las mayusculas
    lv_area_t a = {x->ox + g->x, top, x->ox + g->x + 40, top + lv_font_get_line_height(x->f)};
    lv_draw_label(x->L, &ld, &a);
}

static bool draw_has_cb(const char *w, void *u)
{
    draw_ctx_t *x = u;
    return flex_kb_dict_has(x->pr, x->es, w);
}

static void draw_miss_cb(int x0, int x1, int y, void *u)
{
    draw_ctx_t *x = u;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = x->t->err;
    d.bg_opa = LV_OPA_COVER;
    for (int px = x0; px < x1; px += 3) {
        lv_area_t a = {x->ox + px, x->oy + y + FLEX_NED_LH - 7, x->ox + px + 1, x->oy + y + FLEX_NED_LH - 6};
        lv_draw_rect(x->L, &d, &a);
    }
}

static void tv_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *L = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    const flex_palette_t *t = TH();
    int ox = c.x1, oy = c.y1 - FLEX_NED_SHEET_TOP;   // coordenadas de pantalla del modelo -> las del objeto
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = t->surf;
    rd.bg_opa = LV_OPA_COVER;
    lv_area_t sheet = {c.x1 + 8, c.y1, c.x2 - 8, c.y2};   // la hoja (8, 48, 464, txtBot - 48)
    lv_draw_rect(L, &rd, &sheet);
    if (!N.buf) {
        return;
    }
    flex_kb_prefs_t pr;
    flex_kb_prefs_load(&pr);
    const lv_font_t *f = F2();
    draw_ctx_t cx = {L, ox, oy, (int)flex_font_cap_offset(f), t, f, &pr, !N.kb || flex_kb_get_lang_es(N.kb)};
    flex_ned_scan_t sc = {.adv = adv_cb, .y_break = txt_bot() - 22, .glyph = draw_glyph_cb, .user = &cx};
    if (pr.spell) {   // KB_AUTOCOMPLETE_ON && gKbSpell: comparacion con el diccionario local, nada mas
        sc.has_word = draw_has_cb;
        sc.misspelled = draw_miss_cb;
    }
    flex_ned_marks_t m;
    flex_ned_scan(&N.E, &sc, &m);
    // el cursor, o las manijas (gotas arrastrables) con una seleccion
    lv_draw_rect_dsc_t pd;
    lv_draw_rect_dsc_init(&pd);
    pd.bg_color = t->primary;
    pd.bg_opa = LV_OPA_COVER;
    if (flex_ned_has_sel(&N.E)) {
        int xs[2] = {m.a_x, m.b_x}, ys[2] = {m.a_y, m.b_y};
        for (int k = 0; k < 2; k++) {
            lv_area_t v = {ox + xs[k], oy + ys[k] - 2, ox + xs[k], oy + ys[k] - 2 + 23};
            lv_draw_rect(L, &pd, &v);
            lv_draw_rect_dsc_t cd = pd;
            cd.radius = LV_RADIUS_CIRCLE;
            lv_area_t ca = {ox + xs[k] - 7, oy + ys[k] + 24 - 7, ox + xs[k] + 7, oy + ys[k] + 24 + 7};
            lv_draw_rect(L, &cd, &ca);
        }
    } else {
        lv_area_t cu = {ox + m.cur_x + 1, oy + m.cur_y - 2, ox + m.cur_x + 2, oy + m.cur_y - 2 + 21};
        lv_draw_rect(L, &pd, &cu);
    }
}

static void tv_relayout(void)
{
    int bot = txt_bot();
    lv_obj_set_size(N.tv, 480, bot - FLEX_NED_SHEET_TOP);
    flex_ned_clamp_scroll(&N.E, adv_cb, NULL, bot);
    lv_obj_invalidate(N.tv);
}

static void editor_clear_sel(void)
{
    flex_ned_clear_sel(&N.E);
}

// ---- portapapeles -------------------------------------------------------------------------------------------------------
static void clip_put_cb(int slot, const char *text, void *u)
{
    (void)u;
    char key[10];
    snprintf(key, sizeof(key), "clip%d", slot);
    if (text) {
        flex_cfg_set_str(key, text);
    } else {
        flex_cfg_erase(key);
    }
}

static void clip_get_cb(int slot, char *out, size_t cap, void *u)
{
    (void)u;
    char key[10];
    snprintf(key, sizeof(key), "clip%d", slot);
    flex_cfg_get_str(key, out, cap, "");
}

static void clip_ensure(void)
{
    if (!N.clip_ready) {
        flex_clip_store_t st = {clip_put_cb, clip_get_cb, NULL};
        flex_clip_init(&N.clip, &st);   // las fijadas sobreviven al reinicio
        N.clip_ready = true;
    }
}

static void chip_fade_exec(void *var, int32_t v)
{
    lv_obj_set_style_opa(var, (lv_opa_t)v, 0);
}

static void chip_toast(const char *msg)   // el chip "Copiado": 1200 ms, fundido en los ultimos 400
{
    if (N.chip_ov) {
        lv_obj_delete(N.chip_ov);
        N.chip_ov = NULL;
    }
    int tw = flex_ui_text_w(msg, 2, NULL) + 34;
    N.chip_ov = box(N.eroot, (480 - tw) / 2, txt_bot() - 46, tw, 32, 16);
    fill(N.chip_ov, TH()->surf2, 230);
    lv_obj_t *l = flex_kit_text(N.chip_ov, msg, 2, TH()->txt, tw / 2, 9, true);
    (void)l;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, N.chip_ov);
    lv_anim_set_exec_cb(&a, chip_fade_exec);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_delay(&a, 800);
    lv_anim_set_duration(&a, 400);
    lv_anim_start(&a);
}

static void editor_copy(void)
{
    char tmp[FLEX_CLIP_CLASSIC];
    if (flex_ned_copy_sel(&N.E, tmp, sizeof(tmp)) > 0) {
        clip_ensure();
        flex_clip_push(&N.clip, tmp, lv_tick_get());
        chip_toast("Copiado");
    }
}

static void editor_insert(const char *s)
{
    if (!flex_ned_insert(&N.E, s)) {
        flex_kit_toast(N.root, "La nota est\xC3\xA1 llena");   // Arduino no decia nada y se perdia lo escrito
        return;
    }
    editor_changed();
}

// ---- menu flotante y "mas" ------------------------------------------------------------------------------------------------------
static void overlay_update(void)
{
    if (N.menu_ov) {
        lv_obj_delete(N.menu_ov);
        N.menu_ov = NULL;
    }
    if (!N.E.menu || !flex_ned_has_sel(&N.E) || !N.buf) {
        return;
    }
    flex_ned_scan_t sc = {.adv = adv_cb, .y_break = txt_bot() - 22};
    flex_ned_marks_t m;
    flex_ned_scan(&N.E, &sc, &m);
    flex_ned_menu_t mn;
    flex_ned_menu_geom(m.a_y, &mn);
    const flex_palette_t *t = TH();
    static const char *const IT[4] = {"Cortar", "Copiar", "Pegar", "Todo"};
    N.menu_ov = box(N.eroot, mn.panel_x, mn.panel_y, mn.panel_w, mn.panel_h, 8);
    flex_surface(N.menu_ov, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = box(N.menu_ov, mn.x + i * (mn.bw + mn.gap) - mn.panel_x, mn.y - mn.panel_y, mn.bw, 28, 6);
        flex_surface(b, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
        flex_kit_text(b, IT[i], 2, t->txt, mn.bw / 2, 7, true);
    }
}

static void editor_changed(void)
{
    if (N.E.rev != N.seen_rev) {   // solo un cambio real del texto arma el autoguardado (no mover el cursor)
        N.seen_rev = N.E.rev;
        N.last_change = lv_tick_get();
    }
    char w[40];
    flex_ned_current_word(N.buf, N.E.cur, w, sizeof(w));
    flex_kb_set_context(N.kb, w);   // de aqui salen los chips
    if (N.E.follow) {
        N.E.follow = false;
        flex_ned_ensure_cursor(&N.E, adv_cb, NULL, txt_bot());   // el cursor siempre a la vista
    } else {
        flex_ned_clamp_scroll(&N.E, adv_cb, NULL, txt_bot());
    }
    overlay_update();
    lv_obj_invalidate(N.tv);
    mark_dirty();
}

static void more_close(void)
{
    if (N.more_ov) {
        lv_obj_delete(N.more_ov);
        N.more_ov = NULL;
    }
}

static void more_click_cb(lv_event_t *e)
{
    int x, y;
    tap_point(lv_event_get_current_target_obj(e), &x, &y);
    int w = 250, h = 2 * 38 + 12;
    const flex_kb_geom_t *g = flex_kb_geom(N.kb);
    int mx = 480 - w - 10, my = flex_kb_toolbar_y(g) - h - 6;
    if (my < 60) {
        my = 60;
    }
    int hit = -1;
    if (x >= mx && x <= mx + w) {
        for (int i = 0; i < 2; i++) {
            int yy = my + 10 + i * 38;
            if (y >= yy && y < yy + 38) {
                hit = i;
            }
        }
    }
    more_close();
    if (hit == 0) {
        flex_ned_select_all(&N.E);
        editor_changed();
    } else if (hit == 1) {
        char d[48], hm[12], ln[64];
        struct tm tm;
        flex_clock_now(&tm);
        flex_date_short(d, sizeof(d), tm.tm_wday, tm.tm_mday, tm.tm_mon);
        flex_clock_str_bar(hm, sizeof(hm));
        snprintf(ln, sizeof(ln), "%s %s", d, hm);
        editor_insert(ln);
    }
}

static void more_open(void)
{
    more_close();
    int w = 250, h = 2 * 38 + 12;
    const flex_kb_geom_t *g = flex_kb_geom(N.kb);
    int mx = 480 - w - 10, my = flex_kb_toolbar_y(g) - h - 6;
    if (my < 60) {
        my = 60;
    }
    N.more_ov = box(N.eroot, 0, 0, 480, 800, 0);   // cualquier toque lo cierra
    lv_obj_set_clickable(N.more_ov, true);
    lv_obj_add_event_cb(N.more_ov, more_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *p = box(N.more_ov, mx, my, w, h, 12);
    flex_surface(p, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    static const char *const LBL[2] = {"Seleccionar todo", "Insertar fecha y hora"};
    for (int i = 0; i < 2; i++) {
        flex_kit_text(p, LBL[i], 2, TH()->txt, 14, 10 + i * 38 + 8, false);
    }
}

// ---- panel del portapapeles (FASE D) ----------------------------------------------------------------------------------------------
static void clip_render(void);

static void clip_card_rect(int k, int *x, int *y, int *w, int *h)
{
    int col = k % 2, row = k / 2;
    *w = (480 - 3 * 12) / 2;
    *h = 116;
    *x = 12 + col * (*w + 12);
    *y = 108 + row * (*h + 12);
}

static void clip_click_cb(lv_event_t *e)
{
    int x, y;
    tap_point(lv_event_get_current_target_obj(e), &x, &y);
    int vis[FLEX_CLIP_SLOTS];
    int n = flex_clip_visible(&N.clip, N.clip_filter_pin, vis);
    if (y < 64) {
        if (x < 50) {   // volver
            clip_close();
            overlay_update();
            lv_obj_invalidate(N.tv);
            return;
        }
        if (abs(x - (480 - 96)) <= 22) {   // filtro de fijadas
            N.clip_filter_pin = !N.clip_filter_pin;
            N.clip_ask_clear = false;
        } else if (abs(x - (480 - 42)) <= 22) {   // vaciar: dos toques
            if (N.clip_ask_clear) {
                flex_clip_clear_unpinned(&N.clip);
                N.clip_ask_clear = false;
            } else {
                N.clip_ask_clear = true;
            }
        }
        clip_render();
        return;
    }
    N.clip_ask_clear = false;
    for (int k = 0; k < n && k < 8; k++) {
        int cx, cy, cw, ch;
        clip_card_rect(k, &cx, &cy, &cw, &ch);
        if (x < cx || x > cx + cw || y < cy || y > cy + ch) {
            continue;
        }
        int i = vis[k];
        if (x > cx + cw - 34 && y < cy + 34) {
            flex_clip_toggle_pin(&N.clip, i);   // fijar / soltar
        } else if (x > cx + cw - 34 && y > cy + ch - 34) {
            flex_clip_del(&N.clip, i);          // borrar esta ficha
        } else {
            char t[FLEX_CLIP_TXT_MAX];
            snprintf(t, sizeof(t), "%s", N.clip.slot[i].text);
            clip_close();
            editor_insert(t);                   // pegar en el cursor
            return;
        }
        clip_render();
        return;
    }
    clip_render();
}

static void clip_render(void)
{
    if (!N.clip_ov) {
        return;
    }
    const flex_palette_t *t = TH();
    lv_obj_clean(N.clip_ov);
    lv_obj_t *hd = box(N.clip_ov, 0, 0, 480, 64, 0);
    fill(hd, t->surf, LV_OPA_COVER);
    lv_obj_t *kbi = box(hd, 14, 22, 26, 20, 4);   // icono de teclado
    lv_obj_set_style_border_width(kbi, 1, 0);
    lv_obj_set_style_border_color(kbi, t->nav, 0);
    flex_kit_text(hd, "Portapapeles", 3, t->txt, 54, 20, false);
    for (int k = 0; k < 2; k++) {   // pin (filtra fijados) y papelera (vaciar no fijados)
        int cx = k == 0 ? 480 - 96 : 480 - 42;
        bool on = k == 0 ? N.clip_filter_pin : N.clip_ask_clear;
        lv_obj_t *ci = box(hd, cx - 20, 12, 40, 40, LV_RADIUS_CIRCLE);
        fill(ci, on ? (k == 0 ? t->primary : t->danger) : t->surf2, LV_OPA_COVER);
        lv_obj_t *g = flex_glyph_create(ci, k == 0 ? FLEX_GLYPH_KB_PIN : FLEX_GLYPH_KB_BIN, 40, 40);
        if (k == 0) {
            flex_glyph_set(g, FLEX_GLYPH_KB_PIN, 100);
        }
        lv_obj_set_style_text_color(g, on ? t->on_acc : t->txt, 0);
    }
    int vis[FLEX_CLIP_SLOTS];
    int n = flex_clip_visible(&N.clip, N.clip_filter_pin, vis);
    if (N.clip_ask_clear) {
        flex_kit_text(N.clip_ov, "Toca otra vez la papelera para vaciar (los fijados se quedan)", 1, t->warn, 240, 74, true);
    } else {
        char sub[96];
        snprintf(sub, sizeof(sub), "%d visibles - %d de %d ranuras en uso%s", n, flex_clip_count(&N.clip), FLEX_CLIP_SLOTS,
                 N.clip_filter_pin ? " - filtro: fijadas" : "");
        flex_kit_text(N.clip_ov, sub, 1, t->txt2, 240, 76, true);
    }
    if (n == 0) {
        flex_kit_text(N.clip_ov, "Nada copiado todav\xC3\xAD" "a", 2, t->txt2, 240, 300, true);
        flex_kit_text(N.clip_ov, "Selecciona texto y toca Copiar", 1, t->mute, 240, 330, true);
    }
    for (int k = 0; k < n && k < 8; k++) {
        int x, y, w, h;
        clip_card_rect(k, &x, &y, &w, &h);
        int i = vis[k];
        lv_obj_t *c = box(N.clip_ov, x, y, w, h, 12);
        flex_surface(c, FLEX_SURF_CARD, FLEX_BD_FLAT);
        lv_obj_t *l = flex_kit_text(c, N.clip.slot[i].text, 1, t->txt, 10, 10, false);   // la ficha no crece: 3 lineas con elipsis
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(l, w - 34 - 10 - 6, 3 * 16);
        bool pinned = N.clip.slot[i].pinned;
        lv_obj_t *pin = flex_glyph_create(c, FLEX_GLYPH_KB_PIN, 20, 20);   // pin de la ficha
        lv_obj_set_pos(pin, w - 30, 6);
        lv_obj_set_style_text_color(pin, pinned ? t->primary : t->mute, 0);
        flex_glyph_set(pin, FLEX_GLYPH_KB_PIN, 1);
        lv_obj_t *xg = flex_glyph_create(c, FLEX_GLYPH_CLOSE, 20, 20);   // borrar esta ficha
        lv_obj_set_pos(xg, w - 30, h - 28);
        lv_obj_set_style_text_color(xg, t->danger, 0);
    }
    flex_kit_text(N.clip_ov, "Toca una ficha para pegarla en el cursor", 1, t->mute, 240, 800 - 44, true);
}

static void clip_close(void)
{
    if (N.clip_ov) {
        lv_obj_delete(N.clip_ov);
        N.clip_ov = NULL;
    }
    N.clip_ask_clear = false;
}

static void clip_open(void)
{
    clip_ensure();
    clip_close();
    N.clip_ask_clear = false;
    N.clip_ov = box(N.eroot, 0, 0, 480, 800, 0);
    fill(N.clip_ov, TH()->page, LV_OPA_COVER);
    lv_obj_set_clickable(N.clip_ov, true);
    lv_obj_add_event_cb(N.clip_ov, clip_click_cb, LV_EVENT_CLICKED, NULL);
    clip_render();
}

// ---- gestos en el texto (noteEditorTick) -----------------------------------------------------------------------------------------------
static flex_ned_marks_t cur_marks(void)
{
    flex_ned_scan_t sc = {.adv = adv_cb, .y_break = txt_bot() - 22};
    flex_ned_marks_t m;
    flex_ned_scan(&N.E, &sc, &m);
    return m;
}

static void menu_action(int mi)
{
    if (mi == 0) {   // Cortar
        editor_copy();
        flex_ned_delete_sel(&N.E);
    } else if (mi == 1) {   // Copiar
        editor_copy();
    } else if (mi == 2) {   // Pegar: el buffer clasico, el ultimo texto copiado
        clip_ensure();
        if (N.clip.classic[0]) {
            char t[FLEX_CLIP_CLASSIC];
            snprintf(t, sizeof(t), "%s", N.clip.classic);
            editor_insert(t);
        }
    } else {   // Todo
        flex_ned_select_all(&N.E);
    }
    if (mi != 3) {
        N.E.menu = false;
    }
    editor_changed();
}

static void tv_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!N.buf || N.clip_ov || N.more_ov) {
        return;
    }
    int x, y;
    tap_point(N.eroot, &x, &y);
    int bot = txt_bot();
    if (code == LV_EVENT_PRESSED) {
        N.g_down = true;
        N.g_drag = N.g_long = false;
        N.g_handle = 0;
        N.g_x0 = x;
        N.g_y0 = y;
        N.g_scroll0 = N.E.scroll;
        N.g_t0 = lv_tick_get();
        if (flex_ned_has_sel(&N.E) && y >= FLEX_NED_SHEET_TOP && y < bot + 30) {
            flex_ned_marks_t m = cur_marks();
            if (abs(x - m.a_x) < 24 && abs(y - (m.a_y + 20)) < 28) {
                N.g_handle = 1;
            } else if (abs(x - m.b_x) < 24 && abs(y - (m.b_y + 20)) < 28) {
                N.g_handle = 2;
            }
        }
        return;
    }
    if (!N.g_down) {
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        if (N.g_handle) {   // arrastre de manija -> extender la seleccion
            int bi = flex_ned_hit(&N.E, adv_cb, NULL, x, y);
            if (N.g_handle == 1) {
                if (bi >= 0 && bi < N.E.sel_b) {
                    N.E.sel_a = bi;
                }
            } else if (bi > N.E.sel_a) {
                N.E.sel_b = bi;
            }
            N.E.cur = N.g_handle == 1 ? N.E.sel_a : N.E.sel_b;
            editor_changed();
            return;
        }
        int dy = y - N.g_y0;
        // arrastre vertical: solo si de verdad hay contenido fuera de pantalla (en una nota corta el gesto sigue sirviendo
        // para seleccionar una palabra)
        if (!N.g_drag && !N.g_long && abs(dy) > NOTE_TAP_MOVE && flex_ned_max_scroll(&N.E, adv_cb, NULL, bot) > 0) {
            N.g_drag = true;
        }
        if (N.g_drag) {
            N.E.scroll = N.g_scroll0 - dy;
            flex_ned_clamp_scroll(&N.E, adv_cb, NULL, bot);
            overlay_update();
            lv_obj_invalidate(N.tv);
            return;
        }
        if (!N.g_long && !flex_ned_has_sel(&N.E) && lv_tick_elaps(N.g_t0) > NOTE_LONG_MS && abs(x - N.g_x0) < NOTE_TAP_MOVE &&
            abs(y - N.g_y0) < NOTE_TAP_MOVE) {
            N.g_long = true;   // pulsacion larga -> selecciona la palabra y abre el menu
            flex_ned_select_word_at(&N.E, flex_ned_hit(&N.E, adv_cb, NULL, N.g_x0, N.g_y0));
            editor_changed();
        }
        return;
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }
    N.g_down = false;
    bool tap = code == LV_EVENT_RELEASED && !N.g_drag && !N.g_long && flex_kb_is_tap(x - N.g_x0, y - N.g_y0, lv_tick_elaps(N.g_t0));
    if (N.E.menu && tap && y < (int)flex_kb_top(N.kb)) {   // el menu intercepta los toques ANTES que las manijas
        N.g_handle = 0;
        flex_ned_marks_t m = cur_marks();
        flex_ned_menu_t mn;
        flex_ned_menu_geom(m.a_y, &mn);
        int mi = flex_ned_menu_hit(&mn, x, y);
        if (mi >= 0) {
            menu_action(mi);
            return;
        }
        N.E.menu = false;
        editor_clear_sel();
        N.E.cur = flex_ned_hit(&N.E, adv_cb, NULL, x, y);
        editor_changed();
        return;
    }
    if (N.g_drag) {
        N.g_drag = false;
        mark_dirty();   // el desplazamiento es estado de sesion
        return;
    }
    if (N.g_handle) {
        N.g_handle = 0;
        N.E.menu = true;
        editor_changed();
        return;
    }
    if (N.g_long || !tap) {
        return;
    }
    editor_clear_sel();   // un toque en el texto coloca el cursor
    N.E.cur = flex_ned_hit(&N.E, adv_cb, NULL, x, y);
    editor_changed();
}

// ---- teclado ---------------------------------------------------------------------------------------------------------------------------
static void kb_text_cb(lv_obj_t *kb, const char *utf8, void *user)
{
    (void)kb;
    (void)user;
    if (!N.buf || N.clip_ov || N.more_ov) {
        return;
    }
    editor_insert(utf8);
}

static void kb_back_cb(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    if (N.buf) {
        flex_ned_backspace(&N.E);
        editor_changed();
    }
}

static void kb_enter_cb(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    if (N.buf) {
        editor_insert("\n");
    }
}

static void kb_tool_cb(lv_obj_t *kb, int tool, void *user)
{
    (void)kb;
    (void)user;
    if (!N.buf) {
        return;
    }
    if (tool == FLEX_KB_TB_CLIP) {
        clip_open();
    } else if (tool == FLEX_KB_TB_MORE) {
        more_open();
    } else if (tool == FLEX_KB_TB_SET) {
        // Ajustes del teclado: pertenecen a la app Ajustes (03b §11); mientras no exista no hay a donde ir
        extern void flex_kbsettings_open(void (*on_back)(void)) __attribute__((weak));
        if (flex_kbsettings_open) {
            flex_kbsettings_open(NULL);
        } else {
            flex_kit_toast(N.root, "Ajustes del teclado: con la app Ajustes");
        }
    }
}

static void kb_chip_cb(lv_obj_t *kb, const char *text, bool symbol, void *user)
{
    (void)kb;
    (void)user;
    if (!N.buf) {
        return;
    }
    if (symbol) {
        editor_insert(text);   // en la capa numerica el chip es un simbolo
        return;
    }
    char w[40];
    while (flex_ned_current_word(N.buf, N.E.cur, w, sizeof(w)) > 0) {   // SUSTITUYE la palabra en curso y deja un espacio
        flex_ned_backspace(&N.E);
    }
    char t[48];
    snprintf(t, sizeof(t), "%s", text);
    editor_insert(t);
    editor_insert(" ");
}

static void kb_resize_cb(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    if (N.tv) {
        tv_relayout();
        overlay_update();
    }
}

static void capture_kb_state(void)
{
    if (!N.kb) {
        return;
    }
    N.kb_layout = (uint8_t)flex_kb_get_layout(N.kb);
    N.kb_flags = (uint8_t)((flex_kb_get_lang_es(N.kb) ? 1u : 0u) | (flex_kb_get_shift(N.kb) ? 2u : 0u) | (N.clip_ov ? 4u : 0u) |
                           (N.more_ov ? 8u : 0u) | (N.E.menu ? 16u : 0u));
    N.kb_state_valid = true;
}

static void apply_kb_state(void)   // noteRestoreKbState
{
    if (!N.kb_state_valid) {
        N.kb_layout = 0;
        N.kb_flags = 1;
    }
    flex_kb_set_layout(N.kb, (flex_kb_layout_t)(N.kb_layout < 4 ? N.kb_layout : 0));
    flex_kb_set_lang_es(N.kb, (N.kb_flags & 1u) != 0);
    flex_kb_set_shift(N.kb, (N.kb_flags & 2u) != 0);
    if (N.kb_flags & 4u) {
        clip_open();
    }
    if (N.kb_flags & 8u) {
        more_open();
    }
    N.E.menu = (N.kb_flags & 16u) && flex_ned_has_sel(&N.E);
    N.kb_state_valid = true;
}

// ---- vista del editor ----------------------------------------------------------------------------------------------------------------------
static void editor_back_click_cb(lv_event_t *e)
{
    (void)e;
    flex_sys_back();   // misma logica que el boton de la barra del sistema: capa -> pantalla -> suspender y salir
}

static void editor_free_view(void)
{
    if (N.timer) {
        lv_timer_delete(N.timer);
        N.timer = NULL;
    }
    if (N.eroot) {
        lv_obj_delete(N.eroot);
    }
    N.eroot = N.tv = N.kb = N.menu_ov = N.chip_ov = N.clip_ov = N.more_ov = N.back_zone = NULL;
    N.g_down = N.g_drag = N.g_long = false;
    N.g_handle = 0;
}

static void editor_show(bool restore)
{
    const flex_palette_t *t = TH();
    if (N.lroot) {   // la lista se vuelve a construir al volver
        lv_obj_delete(N.lroot);
        N.lroot = N.lview = N.lcontent = N.fab = N.bar = NULL;
    }
    flex_fk_close_all(N.root);
    N.view = 1;
    N.eroot = box(N.root, 0, 0, 480, 800, 0);
    fill(N.eroot, t->page, LV_OPA_COVER);
    N.back_zone = box(N.eroot, 0, 0, 52, 44, 0);   // chevron "atras" de la cabecera: x < 52, y < 44
    lv_obj_set_clickable(N.back_zone, true);
    lv_obj_add_event_cb(N.back_zone, editor_back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *g = flex_glyph_create(N.back_zone, FLEX_GLYPH_CHEVRON_LEFT, 28, 28);
    lv_obj_set_pos(g, 10, 4);
    lv_obj_set_style_text_color(g, t->nav, 0);
    lv_obj_t *tt = flex_kit_text(N.eroot, N.title, 3, t->txt, 240, 14, true);   // nombre REAL del fichero abierto
    flex_label_one_line(tt, 360);
    lv_obj_set_x(tt, 240 - 180);
    lv_obj_set_style_text_align(tt, LV_TEXT_ALIGN_CENTER, 0);
    N.tv = box(N.eroot, 0, FLEX_NED_SHEET_TOP, 480, 300, 0);
    lv_obj_set_clickable(N.tv, true);
    lv_obj_set_press_lock(N.tv, true);
    lv_obj_add_event_cb(N.tv, tv_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(N.tv, tv_event_cb, LV_EVENT_ALL, NULL);
    flex_kb_cfg_t kc = {
        .flags = FLEX_KB_F_EXTRAS | FLEX_KB_F_FN_ON_PRESS | FLEX_KB_F_ACCENTS,   // Notas SI muestra barra y chips, y acentos
        .backdrop = FLEX_BD_FLAT,
        .bottom_reserve = (int16_t)nav_reserve(),   // el teclado se apoya SOBRE la barra del sistema
        .enter_label = "ent",
        .on_text = kb_text_cb,
        .on_backspace = kb_back_cb,
        .on_enter = kb_enter_cb,
        .on_tool = kb_tool_cb,
        .on_chip = kb_chip_cb,
        .on_resize = kb_resize_cb,
    };
    N.kb = flex_kb_create(N.eroot, &kc);
    if (!restore) {
        N.kb_state_valid = false;   // al abrir: capa ES, sin mayusculas (noteEditorEnter)
    }
    apply_kb_state();
    if (!restore) {
        flex_kb_slide_in(N.kb);   // 0,3 s al abrir; al volver a una nota abierta, sin animar
    }
    N.timer = lv_timer_create(autosave_tick, 100, NULL);
    tv_relayout();
    editor_changed();
    mark_dirty();
}

static void editor_read_cb(esp_err_t err, uint8_t *data, size_t len, void *user)
{
    uint32_t tok = (uint32_t)(uintptr_t)user;
    bool restore = (tok & 1u) != 0;
    if ((tok >> 1) != (N.gen & 0x7FFFFFFF) || !N.created || N.view != 0) {
        free(data);
        return;
    }
    if (err == ESP_ERR_INVALID_SIZE) {
        flex_kit_toast(N.root, "La nota es demasiado grande para editarla");   // no se abre: abrirla recortada y guardarla perderia texto
        N.restore = false;
        list_reload();
        return;
    }
    if (err != ESP_OK || !data) {
        if (!restore) {
            flex_kit_toast(N.root, "No se pudo abrir la nota");
        }
        N.restore = false;
        list_reload();   // si ya no existe, la lista se pone al dia
        return;
    }
    if (memchr(data, 0, len)) {   // un byte 0 cortaria el texto al guardarlo: no es una nota de texto
        free(data);
        flex_kit_toast(N.root, "No es un archivo de texto");
        N.restore = false;
        list_reload();
        return;
    }
    if (!N.buf) {
        N.buf = big_alloc(NOTE_BUF_MAX);
    }
    if (!N.buf) {
        free(data);
        flex_kit_toast(N.root, "Sin memoria para abrir la nota");
        list_reload();
        return;
    }
    flex_ned_init(&N.E, N.buf, NOTE_BUF_MAX);   // (vacia el buffer: primero se inicia y despues se copia el texto)
    memcpy(N.buf, data, len);
    N.buf[len] = '\0';
    free(data);
    N.saved_rev = N.seen_rev = N.E.rev;
    N.save_failed_told = false;
    N.E.cur = (int)strlen(N.buf);   // cursor al final
    N.E.follow = true;
    if (restore && N.sess_valid) {   // cursor, seleccion y scroll de la sesion, si caben en el texto
        int L = (int)strlen(N.buf);
        N.E.cur = N.sess.cursor <= L ? N.sess.cursor : L;
        if (flex_note_sel_valid(N.sess.sel_a, N.sess.sel_b, L)) {
            N.E.sel_a = N.sess.sel_a;
            N.E.sel_b = N.sess.sel_b;
        }
        N.E.scroll = N.sess.editor_scroll;
        N.E.follow = false;
    }
    char stem[FLEX_FS_NAME_MAX];
    flex_fsn_stem(N.path, stem, sizeof(stem));
    snprintf(N.title, sizeof(N.title), "%s", stem);
    N.restore = false;
    editor_show(restore);
}

static void editor_open(const char *path, bool restore)
{
    snprintf(N.path, sizeof(N.path), "%.95s", path);
    N.gen++;
    if (flex_fs_read_async(path, NOTE_BUF_MAX - 2, editor_read_cb, (void *)(uintptr_t)((N.gen << 1) | (restore ? 1u : 0u))) != ESP_OK) {
        editor_read_cb(ESP_FAIL, NULL, 0, (void *)(uintptr_t)((N.gen << 1) | (restore ? 1u : 0u)));
    }
}

static void show_list(void)
{
    N.view = 0;
    if (!N.lroot) {
        list_build();
    }
    N.list_ready = false;
    list_reload();
}

// noteBackToList: guarda y vuelve a la lista, que se relee del disco.
static void editor_to_list(void)
{
    if (dirty_now()) {
        uint32_t rev = N.E.rev;
        if (flex_fs_write_async(N.path, N.buf, strlen(N.buf), save_done_cb, (void *)(uintptr_t)rev) != ESP_OK) {
            flex_kit_toast(N.root, "No se pudo guardar");   // sin sitio en la cola: no se sale y se pierde nada
            return;
        }
    }
    clip_close();
    more_close();
    editor_free_view();
    free(N.buf);
    N.buf = NULL;
    N.path[0] = '\0';
    N.sess_valid = false;
    show_list();
    mark_dirty();
}

// ===== ciclo de vida y sesion (03 §6.5) ================================================================================================================
static void nofs_back_cb(void *u)
{
    (void)u;
    flex_app_close();
}

static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    (void)area;
    bool had_sess = N.sess_valid;
    flex_note_sess_t sess = N.sess;
    uint8_t kl = N.kb_layout, kf = N.kb_flags;
    bool kv = N.kb_state_valid;
    memset(&N.root, 0, offsetof(notes_t, clip) - offsetof(notes_t, root));   // todo menos el portapapeles y la sesion
    N.sess = sess;
    N.sess_valid = had_sess;
    N.kb_layout = kl;
    N.kb_flags = kf;
    N.kb_state_valid = kv;
    N.root = root;
    N.created = true;
    N.view = 0;
    N.sel_idx = -1;
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    if (!st.fs_mounted) {   // sin almacenamiento no hay nada real que ensenar
        flex_fk_nofs_screen(root, "Notas", nofs_back_cb, NULL);
        return;
    }
    list_build();
    if (N.sess_valid) {
        N.list_scroll = N.sess.list_scroll;
    }
    if (N.sess_valid && N.sess.view == 1 && N.sess.path[0]) {
        N.restore = true;
        N.list_ready = false;
        editor_open(N.sess.path, true);   // el fichero ya no existe -> la lista (lo dice la lectura)
    } else {
        show_list();
    }
}

static void on_close(void)
{
    if (N.view == 1 && dirty_now() && N.path[0]) {
        flex_fs_write_async(N.path, N.buf, strlen(N.buf), NULL, NULL);   // lo ultimo escrito, antes de soltar el buffer
    }
    if (N.timer) {
        lv_timer_delete(N.timer);
    }
    free(N.buf);
    // el arbol de objetos se borra despues: aqui solo se olvida el estado (y la sesion: "noteCloseApp")
    memset(&N.root, 0, offsetof(notes_t, clip) - offsetof(notes_t, root));
    N.sess_valid = false;
    N.kb_state_valid = false;
    N.kb_layout = N.kb_flags = 0;
    N.gen++;
}

static void on_suspend(void)
{
    if (N.view == 1 && N.buf) {
        capture_kb_state();   // capa, idioma, mayusculas y que capas estaban abiertas (portapapeles, "mas", menu de seleccion)
        editor_save();        // las capas siguen como estan: la sesion se escribe despues y debe verlas igual
    }
    flex_fk_close_all(N.root);   // la superficie de dialogos es compartida con Paint, Galeria y Archivos
    N.g_down = N.g_drag = N.g_long = false;
    N.g_handle = 0;
}

static void on_resume(void)
{
    if (N.view == 1 && N.buf && N.kb) {
        apply_kb_state();   // la capa, el idioma, las mayusculas y las capas que estaban abiertas
        if (!flex_note_sel_valid(N.E.sel_a, N.E.sel_b, (int)strlen(N.buf))) {
            flex_ned_clear_sel(&N.E);
        }
        if (N.E.cur < 0 || N.E.cur > (int)strlen(N.buf)) {
            N.E.cur = (int)strlen(N.buf);
        }
        tv_relayout();
        editor_changed();
    } else if (N.view == 0) {
        list_reload();
    }
}

// "atras": capa -> pantalla -> (false) salir de la app
static bool on_back(void)
{
    if (flex_fk_any(N.root)) {   // dialogos del kit: cerrar el de arriba (la papelera avisa para recargar la lista)
        flex_fk_close_all(N.root);
        if (N.view == 0) {
            list_reload();
        }
        return true;
    }
    if (N.view == 1) {
        if (N.more_ov) {
            more_close();
            return true;
        }
        if (N.clip_ov) {
            clip_close();
            overlay_update();
            lv_obj_invalidate(N.tv);
            return true;
        }
        if (N.E.menu || flex_ned_has_sel(&N.E)) {
            flex_ned_clear_sel(&N.E);
            editor_changed();
            return true;
        }
        editor_to_list();
        return true;
    }
    return false;
}

static bool dirty_cb(void)
{
    return dirty_now();
}

static bool save_session(void *out, size_t cap, size_t *len)
{
    if (cap < sizeof(flex_note_sess_t)) {
        return false;
    }
    flex_note_sess_t s;
    if (N.view == 1 && N.buf) {
        editor_save();   // noteSaveSess guarda tambien la nota
        capture_kb_state();
        flex_note_sess_pack(&s, true, N.path, N.kb_layout, N.kb_flags, N.E.cur, (int)strlen(N.buf), N.E.sel_a, N.E.sel_b, N.E.scroll,
                            N.list_scroll);
    } else {
        flex_note_sess_pack(&s, false, "", N.kb_layout, N.kb_flags, 0, 0, -1, -1, 0, N.list_scroll);
    }
    memcpy(out, &s, sizeof(s));
    *len = sizeof(s);
    return true;
}

static void load_session(const void *data, size_t len)
{
    N.sess_valid = false;
    if (data && len == sizeof(flex_note_sess_t)) {
        memcpy(&N.sess, data, sizeof(N.sess));
        flex_note_sess_normalize(&N.sess);
        N.sess_valid = true;
        N.kb_layout = N.sess.kb_layout;
        N.kb_flags = N.sess.kb_flags;
        N.kb_state_valid = true;
        N.list_scroll = N.sess.list_scroll;
    }
}

const flex_app_ops_t flex_app_notas_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_suspend = on_suspend,
    .on_close = on_close,
    .on_back = on_back,
    .dirty = dirty_cb,
    .save_session = save_session,
    .load_session = load_session,
};

// ---- pruebas ---------------------------------------------------------------------------------------------------------------------------------
int flex_app_notes_view(void) { return N.created ? N.view : -1; }
int flex_app_notes_count(void) { return N.n; }
bool flex_app_notes_ready(void)
{
    if (!N.list_ready) {
        return false;
    }
    for (int i = 0; i < N.n; i++) {
        if (!N.prev_ok[i]) {
            return false;
        }
    }
    return true;
}
const char *flex_app_notes_text(void) { return N.buf ? N.buf : ""; }
int flex_app_notes_cursor(void) { return N.E.cur; }
int flex_app_notes_scroll(void) { return N.E.scroll; }
bool flex_app_notes_selection(int *a, int *b)
{
    if (a) *a = N.E.sel_a;
    if (b) *b = N.E.sel_b;
    return flex_ned_has_sel(&N.E);
}
bool flex_app_notes_menu_open(void) { return N.menu_ov != NULL; }
bool flex_app_notes_clip_open(void) { return N.clip_ov != NULL; }
bool flex_app_notes_more_open(void) { return N.more_ov != NULL; }
lv_obj_t *flex_app_notes_kb(void) { return N.kb; }
lv_obj_t *flex_app_notes_root(void) { return N.root; }
const char *flex_app_notes_title(int i) { return (i >= 0 && i < N.n) ? N.ents[i].name : ""; }
const char *flex_app_notes_preview(int i) { return (i >= 0 && i < N.n) ? N.prev[i] : ""; }
bool flex_app_notes_multi(int *marked)
{
    if (marked) {
        int c = 0;
        for (int i = 0; i < N.n; i++) c += (N.mask >> i) & 1u;
        *marked = c;
    }
    return N.multi;
}
const char *flex_app_notes_path(void) { return N.path; }
// El portapapeles: ranuras en uso y una ranura (texto, fijada)
int flex_app_notes_clip_count(void)
{
    clip_ensure();
    return flex_clip_count(&N.clip);
}
bool flex_app_notes_clip_slot(int i, const char **text, bool *pinned)
{
    clip_ensure();
    if (i < 0 || i >= FLEX_CLIP_SLOTS || !N.clip.slot[i].used) return false;
    if (text) *text = N.clip.slot[i].text;
    if (pinned) *pinned = N.clip.slot[i].pinned;
    return true;
}
int flex_app_notes_list_scroll(void) { return N.view == 0 && N.lview ? (int)lv_obj_get_scroll_y(N.lview) : N.list_scroll; }
// Pantalla (x, y) del byte bi del texto del editor (para que una escena toque donde esta)
void flex_app_notes_xy_of(int bi, int *x, int *y)
{
    flex_ned_t e = N.E;
    e.cur = bi;
    e.sel_a = e.sel_b = -1;
    flex_ned_scan_t sc = {.adv = adv_cb, .y_break = 100000};
    flex_ned_marks_t m;
    flex_ned_scan(&e, &sc, &m);
    *x = m.cur_x;
    *y = m.cur_y;
}
// Ajusta el reloj del autoguardado en una prueba (el reloj del simulador esta congelado)
uint32_t flex_app_notes_rev(void) { return N.E.rev; }
uint32_t flex_app_notes_saved_rev(void) { return N.saved_rev; }
