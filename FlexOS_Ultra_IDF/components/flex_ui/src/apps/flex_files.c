// Flex OS Ultra · Archivos: el explorador local (docs/spec/03 §8; FlexOS_Ultra_AppFiles.h; inventario A04).
//
// Pantalla modal a todo el alto POR ENCIMA de Almacenamiento (en Arduino era ST_FILES: no una app, sino una pantalla que cuelga de ella), como
// Optimizar y Ajustes del teclado: Atras sube de carpeta o sale, Inicio / Recientes / el bloqueo la cierran del todo. Toda la decision
// (que significa un toque, la pulsacion larga, los resultados de los dialogos, las filas) es del modelo flex_files_model, comparado con Arduino;
// aqui solo hay LVGL: la lista con scroll nativo y FILAS POR VENTANA (hasta 256 entradas sin 256 x 8 objetos), el selector, la barra de
// seleccion y la ejecucion ASINCRONA de lo que el modelo pide (leer carpeta, borrar, renombrar, tirar a la papelera) en la tarea de almacenamiento.
//
// Flex Cloud: la pestana existe, pero el servicio de nube no (inventario A04c): dice que no esta disponible en vez de fingir una cuenta.
// Abrir un medio: Galeria y Multimedia aun no estan migradas (A02/A03), asi que se avisa en vez de abrir un visor que no existe.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_files_model.h"
#include "flex_filekit.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_shell.h"
#include "flex_stor_ui.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define ROW_ABOVE 18                                  // FLEX_FILES_TOP - FLEX_FILES_VP_TOP: la primera fila empieza 18 px dentro de la lista
#define WIN_MARGIN 2                                  // filas de colchon por arriba y por abajo de lo visible

static struct {
    lv_obj_t *root, *header, *seg, *path, *view, *content, *empty, *bar, *cloud;
    lv_obj_t *rowobj[FLEX_FILES_MAX + 1];             // por fila dibujada (0 = ".." si lo hay)
    flex_files_t F;
    flex_fsx_ent_t *ent;                              // la lista de la carpeta (en el heap: 256 x 56 B)
    bool open, loaded, cloud_mode, nofs;
    uint32_t gen;                                     // las respuestas de la tarea de almacenamiento de una carpeta anterior se ignoran
    int vp_bottom;
    int busy;                                         // operaciones de escritura en vuelo
    // pulsacion larga
    bool pressed, fired;
    int sx, sy;
    uint32_t down_ms;
    // mover lo marcado a la papelera, de uno en uno
    int q[FLEX_FILES_MAX];
    int nq, qpos;
    void (*on_back)(void);
} FL;

static void list_build(bool keep_scroll);
static void chrome_build(void);
static void reload(bool keep_scroll);
static void apply(const flex_files_out_t *o);
static void finish_exit(void);

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void indev_xy(int *x, int *y)
{
    lv_point_t p = {0, 0};
    lv_indev_t *d = lv_indev_active();
    if (d) {
        lv_indev_get_point(d, &p);
    }
    *x = (int)p.x;
    *y = (int)p.y;
}

static int scroll_y(void)
{
    return FL.view ? (int)lv_obj_get_scroll_y(FL.view) : 0;
}

static int view_h(void)
{
    return FL.vp_bottom - FLEX_FILES_VP_TOP;
}

// ---- filas por ventana --------------------------------------------------------------------------------------------------------------------------------
static void row_press_cb(lv_event_t *e);

static void row_destroy(int r)
{
    if (r >= 0 && r <= FLEX_FILES_MAX && FL.rowobj[r]) {
        lv_obj_delete(FL.rowobj[r]);
        FL.rowobj[r] = NULL;
    }
}

static void row_make(int r)
{
    if (!FL.content || r < 0 || r >= flex_files_rows(&FL.F) || FL.rowobj[r]) {
        return;
    }
    lv_obj_t *o = box(FL.content, 0, ROW_ABOVE + r * FLEX_FILES_RH, 480, FLEX_FILES_RH - 8, 0);
    lv_obj_set_clickable(o, true);
    lv_obj_set_user_data(o, (void *)(intptr_t)r);
    lv_obj_add_event_cb(o, row_press_cb, LV_EVENT_ALL, NULL);
    static flex_st_page_t pg;   // una sola tarea de UI
    flex_files_row_ops(&FL.F, r, 0, &pg);
    flex_st_ui_render(o, &pg, 0, 0);
    FL.rowobj[r] = o;
}

static void window_update(void)
{
    if (!FL.view || !FL.content) {
        return;
    }
    int rows = flex_files_rows(&FL.F);
    int sy = scroll_y(), vh = view_h();
    int r0 = (sy - ROW_ABOVE) / FLEX_FILES_RH - WIN_MARGIN;
    int r1 = (sy + vh - ROW_ABOVE) / FLEX_FILES_RH + WIN_MARGIN;
    if (r0 < 0) {
        r0 = 0;
    }
    if (r1 > rows - 1) {
        r1 = rows - 1;
    }
    for (int r = 0; r <= FLEX_FILES_MAX; r++) {
        if (FL.rowobj[r] && (r < r0 || r > r1)) {
            row_destroy(r);
        }
    }
    for (int r = r0; r <= r1; r++) {
        row_make(r);
    }
}

static void view_scroll_cb(lv_event_t *e)
{
    (void)e;
    window_update();
}

static void bar_build(void)
{
    if (FL.bar) {
        lv_obj_delete(FL.bar);
        FL.bar = NULL;
    }
    if (!FL.F.multi || FL.cloud_mode || !FL.root) {
        return;
    }
    FL.bar = box(FL.root, 0, FLEX_FILES_BAR_Y, 480, FLEX_FILES_BAR_H, 0);
    lv_obj_set_clickable(FL.bar, true);
    lv_obj_add_event_cb(FL.bar, row_press_cb, LV_EVENT_ALL, NULL);   // el PRESSED reinicia la pulsacion larga de la fila que abrio el menu
    static flex_st_page_t pg;
    flex_files_bar_ops(&pg);
    flex_st_ui_render(FL.bar, &pg, 0, -FLEX_FILES_BAR_Y);
}

static void list_build(bool keep_scroll)
{
    if (!FL.root) {
        return;
    }
    int keep = keep_scroll && FL.view ? scroll_y() : 0;
    if (FL.view) {
        lv_obj_delete(FL.view);
    }
    if (FL.empty) {
        lv_obj_delete(FL.empty);
        FL.empty = NULL;
    }
    memset(FL.rowobj, 0, sizeof(FL.rowobj));
    FL.view = FL.content = NULL;
    if (FL.cloud_mode || FL.nofs) {
        bar_build();
        return;
    }
    FL.view = box(FL.root, 0, FLEX_FILES_VP_TOP, 480, view_h(), 0);
    lv_obj_set_clickable(FL.view, true);
    lv_obj_set_scrollable(FL.view, true);
    lv_obj_set_scroll_dir(FL.view, LV_DIR_VER);
    lv_obj_set_scroll_elastic(FL.view, false);
    lv_obj_set_scroll_momentum(FL.view, false);
    lv_obj_set_scrollbar_mode(FL.view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(FL.view, view_scroll_cb, LV_EVENT_SCROLL, NULL);
    int ch = ROW_ABOVE + flex_files_rows(&FL.F) * FLEX_FILES_RH + 30;
    if (ch < view_h()) {
        ch = view_h();
    }
    FL.content = box(FL.view, 0, 0, 480, ch, 0);
    lv_obj_update_layout(FL.view);
    int mx = ch - view_h();
    lv_obj_scroll_to_y(FL.view, keep > mx ? mx : keep, LV_ANIM_OFF);
    window_update();
    if (FL.loaded && FL.F.n == 0) {   // no se dice "vacia" mientras la carpeta aun se esta leyendo
        FL.empty = flex_kit_text(FL.root, FLEX_FILES_EMPTY_TEXT, 3, flex_th()->mute, 240, 320, true);
    }
    bar_build();
}

// ---- cabecera, selector y ruta --------------------------------------------------------------------------------------------------------------------
static void hdr_menu_cb(lv_event_t *e)
{
    (void)e;
    if (FL.cloud_mode || FL.nofs) {
        return;   // el menu de la nube es del servicio de Flex Cloud (A04c)
    }
    int x, y;
    indev_xy(&x, &y);
    flex_files_out_t o;
    flex_files_tap(&FL.F, scroll_y(), FL.vp_bottom, x, y, &o);
    apply(&o);
}

static void seg_cb(lv_obj_t *seg, int index, void *user)
{
    (void)seg;
    (void)user;
    flex_files_out_t o;
    memset(&o, 0, sizeof(o));
    o.idx = -1;
    if (index == 1 && !FL.cloud_mode) {
        FL.F.multi = false;
        memset(FL.F.mark, 0, sizeof(FL.F.mark));
        o.act = FILES_A_CLOUD_TAB;
    } else if (index == 0 && FL.cloud_mode) {
        o.act = FILES_A_LOCAL_TAB;
    }
    apply(&o);
}

static void path_update(void)
{
    if (!FL.path) {
        return;
    }
    lv_label_set_text(FL.path, FL.F.dir);
}

static void cloud_build(void)
{
    if (FL.cloud) {
        lv_obj_delete(FL.cloud);
        FL.cloud = NULL;
    }
    if (!FL.cloud_mode || !FL.root) {
        return;
    }
    const flex_palette_t *t = flex_th();
    FL.cloud = box(FL.root, 0, FLEX_FILES_VP_TOP, 480, view_h(), 0);
    lv_obj_t *card = box(FL.cloud, 36, 130, 408, 188, 26);
    flex_surface(card, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    lv_obj_t *g = flex_glyph_create(FL.cloud, FLEX_GLYPH_CLOUD, 90, 90);
    flex_glyph_set(g, FLEX_GLYPH_CLOUD, 26);
    lv_obj_set_pos(g, 195, 148);
    lv_obj_set_style_text_color(g, t->mute, 0);
    flex_kit_text(FL.cloud, "Flex Cloud no disponible", 3, t->txt, 240, 224, true);
    lv_obj_t *l = flex_label(FL.cloud, "Esta versi\xC3\xB3n a\xC3\xBAn no tiene el servicio de Flex Cloud. Tus archivos locales siguen en \"Este dispositivo\".",
                             flex_ui_font(1), t->txt2);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 340);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, 70, 258);
}

static void chrome_build(void)
{
    if (!FL.root) {
        return;
    }
    const flex_palette_t *t = flex_th();
    lv_obj_clean(FL.root);
    FL.header = FL.seg = FL.path = FL.view = FL.content = FL.empty = FL.bar = FL.cloud = NULL;
    memset(FL.rowobj, 0, sizeof(FL.rowobj));
    lv_obj_set_style_bg_color(FL.root, t->page, 0);
    lv_obj_set_style_bg_opa(FL.root, LV_OPA_COVER, 0);
    if (FL.nofs) {
        flex_fk_nofs_screen(FL.root, "Archivos", (flex_fk_exit_cb_t)finish_exit, NULL);
        return;
    }
    FL.header = flex_app_header(FL.root, "Archivos:", true, hdr_menu_cb);
    static const char *const labels[2] = {"Este dispositivo", "Flex Cloud"};
    FL.seg = flex_kit_segmented_at(FL.root, 16, FLEX_FILES_SEG_Y, 448, labels, 2, FL.cloud_mode ? 1 : 0, seg_cb, NULL);
    FL.path = flex_kit_text(FL.root, FL.cloud_mode ? "" : FL.F.dir, 2, t->txt2, 16, FLEX_FILES_HDR_H + 8 + 40, false);
    flex_label_one_line(FL.path, 480 - 60 - 16);   // drawTextClip(16, ..., SCR_W - 60): recorta sin puntos suspensivos
    lv_label_set_long_mode(FL.path, LV_LABEL_LONG_CLIP);
    cloud_build();
    list_build(false);
}

// ---- lectura de la carpeta ------------------------------------------------------------------------------------------------------------------------
static void explore_cb(esp_err_t err, const flex_fsx_ent_t *ents, size_t n, size_t total, void *user)
{
    if (!FL.open || (uint32_t)(uintptr_t)user != FL.gen) {
        return;   // respuesta de una carpeta anterior, o la pantalla ya se cerro
    }
    if (err != ESP_OK) {
        n = 0;   // "Carpeta vacia" (Arduino: mediaList devolvia -1 y filesN = 0)
        total = 0;
    }
    FL.F.ent = FL.ent;
    flex_files_set_list(&FL.F, ents, (int)n, total);
    FL.loaded = true;
    path_update();
    list_build(true);
}

static void reload(bool keep_scroll)
{
    if (!FL.open || FL.cloud_mode) {
        return;
    }
    (void)keep_scroll;
    const char *dir = flex_files_reload_dir(&FL.F);   // la biblioteca por dentro se redirige a /System
    FL.gen++;
    FL.loaded = false;
    FL.F.n = 0;
    path_update();
    list_build(false);
    if (flex_fs_explore_async(dir, FLEX_FILES_MAX, explore_cb, (void *)(uintptr_t)FL.gen) != ESP_OK) {
        FL.loaded = true;   // no se pudo ni encolar: queda "Carpeta vacia"
        list_build(false);
    }
}

// ---- acciones de escritura (tarea de almacenamiento) ----------------------------------------------------------------------------------------------
static void write_done_cb(esp_err_t err, void *user)
{
    (void)err;
    if (FL.busy > 0) {
        FL.busy--;
    }
    if (!FL.open || (uint32_t)(uintptr_t)user != FL.gen) {
        return;
    }
    reload(false);
}

static void trash_next(void);
static void trash_done_cb(esp_err_t err, void *user)
{
    (void)err;
    if (!FL.open || (uint32_t)(uintptr_t)user != FL.gen) {
        if (FL.busy > 0) {
            FL.busy--;
        }
        return;
    }
    FL.qpos++;
    trash_next();
}

static void trash_next(void)
{
    if (FL.qpos >= FL.nq) {   // todo lo marcado ya esta en la papelera
        if (FL.busy > 0) {
            FL.busy--;
        }
        flex_files_end_multi(&FL.F);
        FL.F.sel = -1;
        FL.nq = FL.qpos = 0;
        reload(false);
        return;
    }
    char p[FLEX_FSX_PATH_MAX];
    flex_files_path_of(&FL.F, FL.q[FL.qpos], p, sizeof(p));
    if (flex_fs_trash_async(p, trash_done_cb, (void *)(uintptr_t)FL.gen) != ESP_OK) {
        FL.qpos++;
        trash_next();
    }
}

static void path_op(int idx, int what, const char *name)
{
    if (idx < 0 || idx >= FL.F.n) {
        return;
    }
    char p[FLEX_FSX_PATH_MAX];
    flex_files_path_of(&FL.F, idx, p, sizeof(p));
    FL.busy++;
    esp_err_t r;
    void *u = (void *)(uintptr_t)FL.gen;
    if (what == 0) {
        r = flex_fs_delete_async(p, write_done_cb, u);
    } else if (what == 1) {
        r = flex_fs_trash_async(p, write_done_cb, u);
    } else {
        r = flex_fs_rename_name_async(p, name, write_done_cb, u);
    }
    if (r != ESP_OK) {
        write_done_cb(r, u);
    }
}

// ---- dialogos del kit --------------------------------------------------------------------------------------------------------------------------------
static void menu_cb(int act, void *user)
{
    (void)user;
    if (!FL.open || act < 0) {
        list_build(true);   // se cerro sin accion: la fila vuelve a su aspecto
        return;
    }
    flex_files_out_t o;
    flex_files_menu_act(&FL.F, act, &o);
    if (o.act == FILES_A_NONE) {
        list_build(true);
        bar_build();
        return;
    }
    apply(&o);
}

static void ask_cb(bool ok, void *user)
{
    (void)user;
    if (!FL.open) {
        return;
    }
    flex_files_out_t o;
    flex_files_ask_done(&FL.F, ok, &o);
    if (o.act == FILES_A_NONE) {
        list_build(true);
        return;
    }
    apply(&o);
}

static void name_cb(const char *name, void *user)
{
    (void)user;
    if (!FL.open) {
        return;
    }
    flex_files_out_t o;
    flex_files_name_done(&FL.F, name, &o);
    if (o.act == FILES_A_NONE) {
        list_build(true);
        return;
    }
    apply(&o);
}

static void trash_exit_cb(void *user)
{
    (void)user;
    reload(false);   // la papelera pudo restaurar o vaciar: la lista se vuelve a leer
}

// ---- lo que pide el modelo --------------------------------------------------------------------------------------------------------------------------
static void apply(const flex_files_out_t *o)
{
    switch (o->act) {
    case FILES_A_MENU:
        flex_fk_menu_open(FL.root, o->x, o->y, o->cloud, menu_cb, NULL);
        break;
    case FILES_A_EXIT:
        finish_exit();
        break;
    case FILES_A_CLOUD_TAB:
        FL.cloud_mode = true;
        FL.gen++;   // lo que llegue de la carpeta ya no se pinta bajo la nube
        chrome_build();
        break;
    case FILES_A_LOCAL_TAB:
        FL.cloud_mode = false;
        chrome_build();
        reload(false);
        break;
    case FILES_A_MULTI_EXIT:
        list_build(true);
        break;
    case FILES_A_MULTI_TRASH: {
        FL.nq = 0;
        for (int i = 0; i < FL.F.n && i < FLEX_FILES_MAX; i++) {
            if (FL.F.mark[i]) {
                FL.q[FL.nq++] = i;
            }
        }
        FL.qpos = 0;
        FL.busy++;
        trash_next();
        break;
    }
    case FILES_A_ENTER:
    case FILES_A_UP:
        path_update();
        reload(false);
        break;
    case FILES_A_TOGGLE:
        if (o->idx >= 0) {
            int r = o->idx + (flex_files_has_up(&FL.F) ? 1 : 0);
            row_destroy(r);
            row_make(r);
        }
        break;
    case FILES_A_OPEN_MEDIA:
        // Galeria y Multimedia (A02/A03) aun no estan migradas: no hay visor al que ir
        if (o->idx >= 0 && o->idx < FL.F.n) {
            flex_notify_media(FL.F.ent[o->idx].name, "El visor a\xC3\xBAn no est\xC3\xA1 disponible en esta versi\xC3\xB3n");
        }
        break;
    case FILES_A_NOTIFY:
        if (o->idx >= 0 && o->idx < FL.F.n) {
            flex_notify_media(FL.F.ent[o->idx].name, o->text);
        }
        break;
    case FILES_A_UPLOAD:
        // sin el servicio de Flex Cloud (A04c) no hay a donde subir: aviso honesto, nada se encola
        if (o->idx >= 0 && o->idx < FL.F.n) {
            flex_notify("Flex Cloud", "No disponible en esta versi\xC3\xB3n");
        }
        list_build(true);
        break;
    case FILES_A_ASK_DELETE:
        if (o->idx >= 0 && o->idx < FL.F.n) {
            flex_fk_ask_open(FL.root, "\xC2\xBF" "Borrar definitivamente?", FL.F.ent[o->idx].name, ask_cb, NULL);
        }
        break;
    case FILES_A_NAME:
        flex_fk_name_open(FL.root, "Renombrar", o->text, NULL, name_cb, NULL);
        break;
    case FILES_A_TRASH_ONE:
        path_op(o->idx, 1, NULL);
        break;
    case FILES_A_OPEN_TRASH:
        flex_fk_trash_open(FL.root, trash_exit_cb, NULL);
        break;
    case FILES_A_DELETE:
        path_op(o->idx, 0, NULL);
        break;
    case FILES_A_RENAME:
        path_op(o->idx, 2, o->text);
        break;
    default:
        break;
    }
}

// ---- toques de filas y barra ----------------------------------------------------------------------------------------------------------------------------
static void row_press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int x, y;
    if (code == LV_EVENT_PRESSED) {
        indev_xy(&x, &y);
        FL.pressed = true;
        FL.fired = false;
        FL.sx = x;
        FL.sy = y;
        FL.down_ms = lv_tick_get();
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        if (!FL.pressed || FL.fired || FL.F.multi) {
            return;   // en modo seleccion un toque marca; no hay menu
        }
        indev_xy(&x, &y);
        flex_files_out_t o;
        bool scrollable = flex_files_max_scroll(&FL.F, FL.vp_bottom) > 0;
        if (flex_files_long(&FL.F, scroll_y(), FL.vp_bottom, scrollable, FL.sx, FL.sy, x, y, lv_tick_elaps(FL.down_ms), &o)) {
            FL.fired = true;
            lv_indev_wait_release(lv_indev_active());   // el toque que abrio el menu no genera ademas un clic
            apply(&o);
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        FL.pressed = false;
        return;
    }
    if (code != LV_EVENT_CLICKED || FL.fired) {
        return;
    }
    indev_xy(&x, &y);
    flex_files_out_t o;
    flex_files_tap(&FL.F, scroll_y(), FL.vp_bottom, x, y, &o);
    apply(&o);
}

// ---- API ------------------------------------------------------------------------------------------------------------------------------------------------------
static void finish_exit(void)
{
    if (!FL.open) {
        return;
    }
    void (*cb)(void) = FL.on_back;
    lv_obj_t *root = FL.root;
    flex_fk_close_all(root);
    free(FL.ent);
    FL.gen++;   // lo que aun este en vuelo se ignora
    uint32_t gen = FL.gen;
    int busy = FL.busy;
    memset(&FL, 0, sizeof(FL));
    FL.gen = gen;
    FL.busy = busy;   // las escrituras en vuelo terminan solas; sus respuestas se descartan por `gen`
    lv_obj_delete_async(root);   // se llega aqui desde el toque de un hijo suyo
    if (cb) {
        cb();   // Almacenamiento vuelve a medir: si aqui se borro o renombro algo, sus tamanos ya no valen
    }
}

void flex_files_open(void (*on_back)(void))
{
    if (FL.open) {
        return;
    }
    uint32_t gen = FL.gen;
    memset(&FL, 0, sizeof(FL));
    FL.gen = gen + 1;
    FL.on_back = on_back;
    FL.open = true;
    FL.vp_bottom = flex_look()->nav_mode == 1 ? 800 : 800 - FLEX_NAV_H;
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    FL.nofs = !st.fs_mounted;   // flexFsReady(): sin la particion interna no hay explorador
    FL.ent = calloc(FLEX_FILES_MAX, sizeof(*FL.ent));
    if (!FL.ent) {
        FL.nofs = true;
    }
    flex_files_enter_at(&FL.F, "/");
    FL.F.ent = FL.ent;
    flex_touch_drop_all();   // el toque que abrio la pantalla no se filtra
    FL.root = box(flex_shell_screen(), 0, 0, 480, 800, 0);
    lv_obj_set_clickable(FL.root, true);   // modal: nada de lo de debajo recibe toques
    lv_obj_move_foreground(FL.root);
    chrome_build();
    if (!FL.nofs) {
        reload(false);
    }
}

bool flex_files_active(void)
{
    return FL.open;
}

void flex_files_close_now(void)
{
    finish_exit();
}

// Atras (la flecha de la cabecera y el gesto del sistema): una capa del kit abierta (menu, dialogo, papelera) se cierra; en la nube vuelve a
// "Este dispositivo"; en lo demas sale a Almacenamiento (en Arduino el gesto de atras no hacia nada dentro del explorador: solo la flecha salia)
void flex_files_back(void)
{
    if (!FL.open) {
        return;
    }
    if (FL.nofs) {   // la pantalla "sin almacenamiento" es una capa del kit, pero su salida es la del explorador
        finish_exit();
        return;
    }
    if (flex_fk_any(FL.root)) {
        flex_fk_close_all(FL.root);
        reload(false);   // la papelera pudo cambiar la carpeta
        return;
    }
    if (FL.cloud_mode) {
        flex_files_out_t o = {.act = FILES_A_LOCAL_TAB, .idx = -1};
        apply(&o);
        return;
    }
    finish_exit();   // Arduino: la flecha de la cabecera vuelve a Almacenamiento (no sube de carpeta; ".." es la fila de arriba), tambien en modo seleccion
}

// ---- pruebas ---------------------------------------------------------------------------------------------------------------------------------------------------------
const char *flex_files_dir(void) { return FL.open ? FL.F.dir : ""; }
int flex_files_count(void) { return FL.open ? FL.F.n : -1; }
bool flex_files_loaded(void) { return FL.open && FL.loaded; }
bool flex_files_in_cloud(void) { return FL.open && FL.cloud_mode; }
bool flex_files_multi(void) { return FL.open && FL.F.multi; }
int flex_files_marked(void)
{
    int n = 0;
    for (int i = 0; FL.open && i < FL.F.n; i++) {
        n += FL.F.mark[i] ? 1 : 0;
    }
    return n;
}
int flex_files_busy(void) { return FL.busy; }
int flex_files_layers(void) { return FL.open ? flex_fk_layers(FL.root) : 0; }
bool flex_files_nofs(void) { return FL.open && FL.nofs; }
int flex_files_scroll(void) { return scroll_y(); }
int flex_files_total_rows(void) { return FL.open ? flex_files_rows(&FL.F) : 0; }
int flex_files_live_rows(void)
{
    int n = 0;
    for (int r = 0; r <= FLEX_FILES_MAX; r++) {
        n += FL.rowobj[r] ? 1 : 0;
    }
    return n;
}
const char *flex_files_name_at(int i) { return FL.open && i >= 0 && i < FL.F.n ? FL.F.ent[i].name : ""; }
bool flex_files_row_xy(int i, int *x, int *y)
{
    int r = i + (FL.open && flex_files_has_up(&FL.F) ? 1 : 0);
    if (!FL.open || r < 0 || r > FLEX_FILES_MAX || !FL.rowobj[r]) {
        return false;
    }
    lv_area_t a;
    lv_obj_get_coords(FL.rowobj[r], &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}
// la fila i se ve marcada: su primer hijo es el rectangulo del color de seleccion
bool flex_files_row_is_sel(int i)
{
    int r = i + (FL.open && flex_files_has_up(&FL.F) ? 1 : 0);
    if (!FL.open || r < 0 || r > FLEX_FILES_MAX || !FL.rowobj[r] || lv_obj_get_child_count(FL.rowobj[r]) == 0) {
        return false;
    }
    lv_obj_t *bg = lv_obj_get_child(FL.rowobj[r], 0);
    return lv_color_to_u32(lv_obj_get_style_bg_color(bg, 0)) == lv_color_to_u32(flex_th()->sel);
}
bool flex_files_up_xy(int *x, int *y)
{
    if (!FL.open || !flex_files_has_up(&FL.F) || !FL.rowobj[0]) {
        return false;
    }
    lv_area_t a;
    lv_obj_get_coords(FL.rowobj[0], &a);
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
bool flex_files_shown(const char *txt)
{
    return FL.root && find_text(FL.root, txt);
}
