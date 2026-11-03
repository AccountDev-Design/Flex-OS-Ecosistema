// Kit de archivos (docs/spec/03 §5): menu contextual, confirmacion, dialogo de nombre con el teclado del sistema,
// papelera (restaurar, borrar, vaciar, lista larga con ventana de filas) y "sin almacenamiento", sobre un DISCO REAL
// (directorio temporal con las mismas operaciones que la tarea de almacenamiento) y con el dedo.
#include <stdio.h>
#include <string.h>
#include "flex_filekit.h"
#include "flex_frame.h"
#include "flex_fk_model.h"
#include "flex_fs_names.h"
#include "flex_i18n.h"
#include "flex_kb.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void sim_disk_enable(bool on);
bool sim_disk_put(const char *rel, const void *d, size_t l);
size_t sim_disk_get(const char *rel, void *out, size_t cap);
bool sim_disk_exists(const char *rel);
bool sim_disk_is_dir(const char *rel);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("filekit: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

static lv_obj_t *g_root;
static int g_act, g_menu_calls, g_ask, g_ask_calls, g_exit_calls, g_name_calls;
static char g_name[64];
static bool g_name_null;

static void on_menu(int act, void *u) { (void)u; g_act = act; g_menu_calls++; }
static void on_ask(bool v, void *u) { (void)u; g_ask = v; g_ask_calls++; }
static void on_exit_cb(void *u) { (void)u; g_exit_calls++; }
static void on_name(const char *n, void *u)
{
    (void)u;
    g_name_calls++;
    g_name_null = n == NULL;
    snprintf(g_name, sizeof(g_name), "%s", n ? n : "");
}

static void type_text(const char *txt)
{
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 64, 0);   // sobre la barra de navegacion
    flex_kb_state_t st;
    flex_kb_state_init(&st, true);
    for (; *txt; txt++) {
        for (int c = 0; c < FLEX_KB_CELLS; c++) {
            const char *b = flex_kb_key_base(&st, c);
            if (b[0] == *txt && b[1] == 0) {
                int x, y;
                flex_kb_cell_xy(&g, c, &x, &y);
                sim_tap(x + g.kw / 2, y + g.kh / 2);
                break;
            }
        }
    }
}
static void fkey(int i)
{
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 64, 0);   // sobre la barra de navegacion
    sim_tap(flex_kb_fkey_x(&g, i) + flex_kb_fkey_w(&g, i) / 2, flex_kb_func_y(&g) + g.kh / 2);
}

static bool wait_trash(void)
{
    for (int i = 0; i < 60; i++) {
        if (flex_fk_trash_ready()) return true;
        sim_run(20);
    }
    return false;
}

static int label_lines(lv_obj_t *l)
{
    const char *t = l ? lv_label_get_text(l) : "";
    int n = t[0] ? 1 : 0;
    for (; *t; t++) n += *t == '\n';
    return n;
}

bool scene_filekit_run(void)
{
    bool ok = true;
    flex_lock_clear();
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    }
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
    sim_disk_enable(true);
    g_root = flex_box(flex_shell_screen());
    lv_obj_set_size(g_root, 480, 800);
    lv_obj_set_style_bg_color(g_root, flex_th()->page, 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_move_foreground(g_root);

    // ---- menu contextual: filas, ajuste a la pantalla, toque fuera
    flex_fk_rect_t r;
    g_menu_calls = 0;
    flex_fk_menu_open(g_root, 300, 400, false, on_menu, NULL);
    sim_run(60);
    CHK(flex_fk_any(g_root));
    flex_fk_menu_geom(300, 400, false, &r);
    sim_tap(r.x + 50, r.y + FLEX_FK_MENU_PAD + 2 * FLEX_FK_MENU_RH + 20);   // fila 2: Renombrar
    sim_run(60);
    CHK(g_menu_calls == 1 && g_act == FLEX_FK_ACT_REN && !flex_fk_any(g_root));
    flex_fk_menu_open(g_root, 300, 400, false, on_menu, NULL);
    sim_run(60);
    sim_tap(100, 300);                                         // fuera del panel: cierra sin accion
    sim_run(60);
    CHK(g_menu_calls == 2 && g_act == -1 && !flex_fk_any(g_root));
    flex_fk_menu_open(g_root, 440, 56, true, on_menu, NULL);    // desde los tres puntos, con la quinta fila
    sim_run(60);
    flex_fk_menu_geom(440, 56, true, &r);
    CHK(r.x == 200 && r.y == 56 && r.h == 250);
    sim_shot("filekit_menu");
    sim_tap(r.x + 100, r.y + FLEX_FK_MENU_PAD + 4 * FLEX_FK_MENU_RH + 10);
    sim_run(60);
    CHK(g_act == FLEX_FK_ACT_CLOUD && !flex_fk_any(g_root));

    // ---- confirmacion: Cancelar, Borrar, fuera, y dentro sin boton
    flex_fk_rect_t card, no, yes;
    flex_fk_ask_geom(&card, &no, &yes);
    flex_fk_ask_open(g_root, "\xC2\xBF" "Borrar definitivamente?", "Nota 1", on_ask, NULL);
    sim_run(60);
    sim_tap(card.x + card.w / 2, card.y + 100);                 // dentro de la tarjeta, sin boton
    sim_run(60);
    CHK(g_ask_calls == 0 && flex_fk_any(g_root));
    sim_tap(yes.x + yes.w / 2, yes.y + 28);
    sim_run(60);
    CHK(g_ask_calls == 1 && g_ask == 1 && !flex_fk_any(g_root));
    flex_fk_ask_open(g_root, "\xC2\xBF" "Vaciar la papelera?", NULL, on_ask, NULL);
    sim_run(60);
    sim_tap(no.x + no.w / 2, no.y + 28);
    sim_run(60);
    CHK(g_ask_calls == 2 && g_ask == 0);
    flex_fk_ask_open(g_root, "x", NULL, on_ask, NULL);
    sim_run(60);
    sim_tap(30, 200);                                           // fuera de la tarjeta = cancelar
    sim_run(60);
    CHK(g_ask_calls == 3 && g_ask == 0 && !flex_fk_any(g_root));
    flex_fk_ask_open(g_root, "\xC2\xBF" "Borrar definitivamente?", "Nota 1", on_ask, NULL);
    sim_run(60);
    sim_shot("filekit_confirmar");
    flex_fk_close_all(g_root);
    CHK(!flex_fk_any(g_root));

    // ---- dialogo de nombre: teclear, borrar, Guardar, vacio = cancelar, esquina = cancelar, tope de 47
    g_name_calls = 0;
    flex_fk_name_open(g_root, "Renombrar nota", "Nota", NULL, on_name, NULL);
    sim_run(80);
    CHK(flex_fk_any(g_root));
    type_text("xyz");
    fkey(FLEX_KB_FN_BACKSPACE);                                 // borra la z
    sim_run(60);
    CHK(g_name_calls == 0);
    sim_shot("filekit_nombre");
    fkey(FLEX_KB_FKEYS - 1);                                    // Guardar
    sim_run(60);
    CHK(g_name_calls == 1 && !g_name_null && !strcmp(g_name, "Notaxy") && !flex_fk_any(g_root));
    flex_fk_name_open(g_root, "Renombrar", "a/b", "Otro texto", on_name, NULL);   // el nombre inicial no puede partir la ruta
    sim_run(80);
    fkey(FLEX_KB_FKEYS - 1);
    sim_run(60);
    CHK(g_name_calls == 2 && !strcmp(g_name, "a b"));
    flex_fk_name_open(g_root, "Renombrar", "", NULL, on_name, NULL);
    sim_run(80);
    fkey(FLEX_KB_FKEYS - 1);                                    // vacio: cancela
    sim_run(60);
    CHK(g_name_calls == 3 && g_name_null);
    flex_fk_name_open(g_root, "Renombrar", "Hola", NULL, on_name, NULL);
    sim_run(80);
    sim_tap(30, 60);                                            // esquina superior izquierda
    sim_run(60);
    CHK(g_name_calls == 4 && g_name_null && !flex_fk_any(g_root));
    flex_fk_name_open(g_root, "Renombrar", "", NULL, on_name, NULL);
    sim_run(80);
    for (int i = 0; i < 55; i++) type_text("a");
    fkey(FLEX_KB_FKEYS - 1);
    sim_run(60);
    CHK(g_name_calls == 5 && strlen(g_name) == 46);             // el tope del nombre (FileKit.h: L + sl < NAME_MAX - 1)

    // ---- texto en caja: corta por caracteres, respeta saltos y para cuando no cabe otra linea
    {
        lv_obj_t *l = flex_fk_text_box(g_root, 10, 10, 196, 100, "uno\ndos dos dos dos dos dos dos dos dos dos dos dos dos dos dos", 2, flex_th()->txt);
        CHK(l != NULL && label_lines(l) >= 2 && label_lines(l) <= 100 / (flex_ui_line_h(2) + 4));
        CHK(flex_fk_text_box(g_root, 0, 0, 100, 100, "", 2, flex_th()->txt) == NULL);
        lv_obj_delete(l);
    }

    // ---- papelera sobre el disco: tirar dos ficheros y verlos, restaurar, borrar y vaciar
    sim_disk_put("/Notas/a.txt", "AAA", 3);
    sim_disk_put("/Paint/d.fxp", "DDDD", 4);
    flex_fs_trash_async("/Notas/a.txt", NULL, NULL);
    flex_fs_trash_async("/Paint/d.fxp", NULL, NULL);
    sim_run(60);
    CHK(!sim_disk_exists("/Notas/a.txt") && sim_disk_exists("/Papelera/@Notas@a.txt") && sim_disk_exists("/Papelera/@Paint@d.fxp"));
    g_exit_calls = 0;
    lv_obj_t *tl = flex_fk_trash_open(g_root, on_exit_cb, NULL);
    CHK(tl != NULL && flex_fk_trash_open(g_root, on_exit_cb, NULL) == tl);   // una a la vez
    CHK(wait_trash() && flex_fk_trash_count() == 2 && flex_fk_trash_selected() == -1);
    sim_shot("filekit_papelera");
    sim_tap(240, 108 + 12 + 25);                                // fila 0 (orden: "@Notas..." antes que "@Paint...")
    sim_run(60);
    CHK(flex_fk_trash_selected() == 0);
    sim_tap(240, 108 + 12 + 25);                                // otra vez: deselecciona
    sim_run(60);
    CHK(flex_fk_trash_selected() == -1);
    sim_tap(240, 108 + 12 + 25);
    sim_run(60);
    sim_shot("filekit_papelera_sel");
    sim_tap(124, 706);                                          // Restaurar
    CHK(wait_trash());
    CHK(sim_disk_exists("/Notas/a.txt") && !sim_disk_exists("/Papelera/@Notas@a.txt") && flex_fk_trash_count() == 1);
    sim_tap(240, 108 + 12 + 25);                                // queda la de Paint
    sim_run(60);
    CHK(flex_fk_trash_selected() == 0);
    sim_tap(356, 706);                                          // Borrar -> confirmacion
    sim_run(60);
    CHK(flex_fk_layers(g_root) == 2 && sim_disk_exists("/Papelera/@Paint@d.fxp"));   // papelera + confirmacion
    flex_fk_ask_geom(&card, &no, &yes);
    sim_tap(no.x + no.w / 2, no.y + 28);                        // Cancelar: sigue ahi
    sim_run(60);
    CHK(sim_disk_exists("/Papelera/@Paint@d.fxp") && flex_fk_trash_count() == 1);
    sim_tap(356, 706);
    sim_run(60);
    sim_tap(yes.x + yes.w / 2, yes.y + 28);                     // Borrar definitivamente
    CHK(wait_trash());
    CHK(!sim_disk_exists("/Papelera/@Paint@d.fxp") && flex_fk_trash_count() == 0);
    sim_disk_put("/Papelera/@X@1.txt", "1", 1);
    sim_disk_put("/Papelera/@X@2.txt", "22", 2);
    flex_fk_close_all(g_root);                                  // cerrar con la lista en vuelo no deja nada colgando
    CHK(flex_fk_trash_count() == -1 && !flex_fk_any(g_root));
    sim_run(60);
    flex_fk_trash_open(g_root, on_exit_cb, NULL);
    CHK(wait_trash() && flex_fk_trash_count() == 2);
    sim_tap(240, 706);                                          // Vaciar papelera -> confirmacion
    sim_run(60);
    CHK(flex_fk_layers(g_root) == 2 && sim_disk_exists("/Papelera/@X@1.txt") && sim_disk_exists("/Papelera/@X@2.txt"));
    flex_fk_ask_geom(&card, &no, &yes);
    sim_tap(yes.x + yes.w / 2, yes.y + 28);
    CHK(wait_trash());
    CHK(flex_fk_trash_count() == 0 && !sim_disk_exists("/Papelera/@X@1.txt") && sim_disk_is_dir("/Papelera"));
    sim_run(100);
    sim_tap(24, 40);                                            // chevron: salir (y avisa para recargar); y < 30 es el borde del sistema
    sim_run(80);
    CHK(g_exit_calls == 1 && flex_fk_trash_count() == -1 && !flex_fk_any(g_root));

    // ---- lista larga: solo viven las filas de la vista, el desplazamiento llega al final y elegir sigue valiendo
    for (int i = 0; i < 60; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/Papelera/@Doc@f%02d.txt", i);
        sim_disk_put(p, "x", 1);
    }
    flex_fk_trash_open(g_root, on_exit_cb, NULL);
    CHK(wait_trash() && flex_fk_trash_count() == 60);
    {
        lv_obj_t *view = lv_obj_get_child(lv_obj_get_child(g_root, lv_obj_get_child_count(g_root) - 1), 4);
        lv_obj_t *content = lv_obj_get_child(view, 0);
        CHK(lv_obj_get_child_count(content) > 0 && lv_obj_get_child_count(content) <= 10 + 2 * 4);   // ventana, no 60 filas
        sim_drag(240, 600, 240, 150, 300);
        sim_drag(240, 600, 240, 100, 300);
        sim_drag(240, 600, 240, 100, 300);
        sim_run(80);
        int sy = (int)lv_obj_get_scroll_y(view);
        CHK(sy > 0 && sy <= flex_fk_trash_max_scroll(60));
        CHK(lv_obj_get_child_count(content) > 0 && lv_obj_get_child_count(content) <= 10 + 2 * 4);
        sim_tap(240, 300);                                      // una fila de mitad de lista
        sim_run(60);
        CHK(flex_fk_trash_selected() > 0);
    }
    flex_fk_close_all(g_root);

    // ---- "sin almacenamiento"
    g_exit_calls = 0;
    flex_fk_nofs_screen(g_root, "Notas", on_exit_cb, NULL);
    sim_run(60);
    sim_shot("filekit_sin_almacenamiento");
    CHK(flex_fk_any(g_root));
    sim_tap(24, 40);
    sim_run(60);
    CHK(g_exit_calls == 1 && !flex_fk_any(g_root));

    // ---- cerrar todo de una vez
    flex_fk_menu_open(g_root, 100, 100, false, on_menu, NULL);
    flex_fk_ask_open(g_root, "x", NULL, on_ask, NULL);
    flex_fk_name_open(g_root, "t", "n", NULL, on_name, NULL);
    CHK(flex_fk_any(g_root));
    flex_fk_close_all(g_root);
    CHK(!flex_fk_any(g_root));
    sim_run(100);

    lv_obj_delete(g_root);
    g_root = NULL;
    sim_disk_enable(false);
    flex_shell_lock();
    sim_run(200);
    printf("kit de archivos: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
