// Modo edicion del escritorio (01a §8): entrar desde el menu contextual,
// reordenar con 400 ms sobre otra celda, llevar un icono a la pagina vecina
// (700 ms en el borde), seleccionar / redimensionar / mover / quitar un widget,
// tocar en vacio (suelta y luego sale guardando), diseno bloqueado, y salir
// al bloquear o con Inicio.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_home_model.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("edicion: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void to_home(void)
{
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
}

static void slot_center(int i, int *x, int *y)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    flex_home_slot_xy(i, x, y);
    *x += S / 2;
    *y += S / 2;
}

// Apoyar, ir hasta (x1, y1) y quedarse ms con el dedo quieto; luego soltar
static void drag_hold(int x0, int y0, int x1, int y1, uint32_t hold)
{
    sim_touch(x0, y0, true);
    sim_run(60);
    for (int k = 1; k <= 10; k++) {
        sim_touch(x0 + (x1 - x0) * k / 10, y0 + (y1 - y0) * k / 10, true);
        sim_run(16);
    }
    sim_run(hold);
    sim_touch(x1, y1, false);
    sim_run(200);
}

static void enter_edit(bool *okp)
{
    bool ok = *okp;
    lv_area_t a;
    int app = -1;
    for (int id = 0; id < FLEX_APP_N && app < 0; id++) {
        if (flex_home_icon_area(id, &a) && a.y2 < 600) {
            app = id;
        }
    }
    CHK(app >= 0);
    int x = (a.x1 + a.x2) / 2, y = (a.y1 + a.y2) / 2;
    sim_touch(x, y, true);
    sim_run(1150);
    sim_touch(x, y, false);
    sim_run(300);
    CHK(flex_home_ctx_open());
    // fila "Modo edicion" (la segunda), misma colocacion que ctxOpen
    int S = lv_area_get_width(&a), rr = (480 - 8) - (a.x2 + 1 + 12), rl = (a.x1 - 12) - 8;
    bool right = rr >= 244 ? true : rl >= 244 ? false : rr >= rl;
    int px = right ? a.x1 + S + 12 : a.x1 - 12 - 244;
    px = px < 8 ? 8 : px > 480 - 8 - 244 ? 480 - 8 - 244 : px;
    sim_tap(px + 100, a.y1 + 58 + 29);
    sim_run(400);
    CHK(flex_home_edit_active() && flex_shell_state() == FLEX_SH_OVERLAY);
    *okp = ok;
}

bool scene_edit_run(void)
{
    bool ok = true;
    flex_lock_clear();
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    } else {
        flex_theme_set_glass(true);
        flex_theme_set_dark(true);
    }
    to_home();
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // Escritorio conocido: 2 paginas 4x3, seis apps y dos widgets de cabecera en la 1
    flex_home_t home0 = g_home;
    memset(g_home.order, FLEX_HOME_EMPTY, sizeof(g_home.order));
    memset(g_home.wg, 0, sizeof(g_home.wg));
    memset(g_home.wg_n, 0, sizeof(g_home.wg_n));
    g_home.page_n = 2;
    g_home.page = 0;
    g_home.cols = 4;
    g_home.rows = 3;
    g_home.locked = false;
    static const uint8_t apps[6] = {IC_RELOJ, IC_GALERIA, IC_NOTAS, IC_PAINT, IC_JUEGOS, IC_CALC};
    g_home.fav = 0;
    for (int i = 0; i < 6; i++) {
        g_home.order[flex_home_idx(0, i)] = apps[i];
        g_home.fav |= 1u << apps[i];
    }
    g_home.wg[0][0] = (flex_home_wg_t){FLEX_WG_CLOCK, 0, 0, 2, 1};
    g_home.wg[0][1] = (flex_home_wg_t){FLEX_WG_DATE, 2, 0, 2, 1};
    g_home.wg_n[0] = 2;
    flex_home_normalize();
    flex_home_save();
    flex_home_rebuild();
    sim_run(300);

    // 1) entrar desde el menu contextual
    enter_edit(&ok);
    CHK(strstr(flex_home_edit_hint(), "Arrastra iconos y widgets") != NULL);
    sim_run(200);
    sim_shot("ed_01_temblor");

    // 2) reordenar: el icono de la celda 0, 500 ms sobre la celda 2 -> se reinserta
    uint8_t o0 = g_home.order[0], o1 = g_home.order[1], o2 = g_home.order[2];
    int x0, y0, x2, y2;
    slot_center(0, &x0, &y0);
    slot_center(2, &x2, &y2);
    sim_touch(x0, y0, true);
    sim_run(60);
    CHK(flex_home_edit_drag() == 0);
    for (int k = 1; k <= 10; k++) {
        sim_touch(x0 + (x2 - x0) * k / 10, y0 + (y2 - y0) * k / 10, true);
        sim_run(16);
    }
    sim_run(200);
    sim_shot("ed_02_arrastrando");
    sim_run(400);
    sim_touch(x2, y2, false);
    sim_run(200);
    CHK(g_home.order[0] == o1 && g_home.order[1] == o2 && g_home.order[2] == o0);
    uint8_t nv[FLEX_HOME_TOTAL];
    CHK(flex_cfg_get_blob("hordq", nv, sizeof(nv)) == sizeof(nv) && memcmp(nv, g_home.order, sizeof(nv)) == 0);
    CHK(flex_home_edit_drag() == -1 && flex_home_edit_active());

    // 3) al borde derecho 800 ms: a la primera celda libre de la pagina 2
    uint8_t moved = g_home.order[flex_home_idx(0, 3)];
    int x3, y3;
    slot_center(3, &x3, &y3);
    drag_hold(x3, y3, 470, y3, 900);
    CHK(g_home.page == 1 && g_home.order[flex_home_idx(1, 0)] == moved);
    CHK(g_home.order[flex_home_idx(0, 3)] != moved);
    // y de vuelta a la primera (ahi hay hueco)
    int xb, yb;
    slot_center(0, &xb, &yb);
    drag_hold(xb, yb, 8, yb, 900);
    CHK(g_home.page == 0);

    // 4) widget: tocarlo lo selecciona (resalte y asa); el asa cambia el tamano por celdas
    int wx, wy, ww, wh;
    flex_home_wg_rect(&g_home.wg[0][0], &wx, &wy, &ww, &wh);
    sim_tap(wx + ww / 2, wy + wh / 2);
    sim_run(200);
    CHK(flex_home_edit_wsel() == 0);
    sim_shot("ed_03_widget_seleccionado");
    // DATE ocupa las columnas 2-3: crecer el reloj no cabe ("Ese tamano no cabe ahi")
    drag_hold(wx + ww - 12, wy + wh - 12, 2 * 120 + 60, wy + wh - 12, 200);
    CHK(g_home.wg[0][0].w == 2);
    CHK(strstr(flex_home_edit_hint(), "no cabe") != NULL);
    // quitar la fecha con su insignia; entonces si crece
    int dx, dy, dw, dh;
    flex_home_wg_rect(&g_home.wg[0][1], &dx, &dy, &dw, &dh);
    sim_tap(dx + 12, dy + 12);
    sim_run(200);
    CHK(g_home.wg_n[0] == 1);
    sim_tap(wx + ww / 2, wy + wh / 2);
    sim_run(200);
    drag_hold(wx + ww - 12, wy + wh - 12, 2 * 120 + 60, wy + wh - 12, 200);
    CHK(g_home.wg[0][0].w == 3);
    // moverlo una columna (agarrado por su primera celda)
    sim_tap(240, 700);   // suelta la seleccion
    sim_run(200);
    CHK(flex_home_edit_wsel() == -1 && flex_home_edit_active());
    drag_hold(wx + 60, wy + wh / 2, wx + 60 + 120, wy + wh / 2, 200);
    CHK(g_home.wg[0][0].col == 1);

    // 5) tocar en vacio: suelta la seleccion; otra vez: sale guardando
    sim_tap(240, 700);
    sim_run(200);
    CHK(flex_home_edit_active());
    sim_tap(240, 700);
    sim_run(400);
    CHK(!flex_home_edit_active() && flex_shell_state() == FLEX_SH_HOME);
    uint8_t wb[FLEX_HOME_WG_BLOB];
    CHK(flex_cfg_get_blob("hwg2", wb, sizeof(wb)) == sizeof(wb));
    CHK(wb[2] == 1 && wb[3] == FLEX_WG_CLOCK && wb[4] == 1 && wb[6] == 3);   // pagina 1: 1 widget, col 1, ancho 3

    // 6) diseno bloqueado: no se arrastra ni se quita; si se selecciona
    g_home.locked = true;
    enter_edit(&ok);
    uint8_t before[FLEX_HOME_TOTAL];
    memcpy(before, g_home.order, sizeof(before));
    slot_center(4, &x0, &y0);
    drag_hold(x0, y0, x2, y2, 600);
    CHK(memcmp(before, g_home.order, sizeof(before)) == 0);
    flex_home_wg_rect(&g_home.wg[0][0], &wx, &wy, &ww, &wh);
    sim_tap(wx + 12, wy + 12);   // donde iria la insignia
    sim_run(200);
    CHK(g_home.wg_n[0] == 1 && flex_home_edit_wsel() == 0);
    g_home.locked = false;

    // 7) bloquear saca del Modo edicion (guardando); Inicio tambien
    flex_shell_lock();
    sim_run(200);
    CHK(!flex_home_edit_active());
    to_home();
    enter_edit(&ok);
    flex_sys_home();
    sim_run(300);
    CHK(!flex_home_edit_active() && flex_shell_state() == FLEX_SH_HOME);

    g_home = home0;
    flex_home_save();
    flex_home_rebuild();
    flex_shell_lock();
    sim_run(200);
    printf("edicion: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
