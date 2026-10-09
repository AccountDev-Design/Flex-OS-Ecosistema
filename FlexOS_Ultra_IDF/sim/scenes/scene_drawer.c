// Caja de aplicaciones: abrir con el desliz, buscar, ver ocultas, menu de
// pulsacion larga (ocultar, anadir a inicio), ficha, abrir una app con candado
// (pide la clave) y cerrar.
#include <stdio.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
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
            printf("caja: FALLO linea %d: %s\n", __LINE__, #c);      \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void open_drawer(void)
{
    sim_drag(240, 600, 240, 250, 200);
    sim_run(400);
}

static void cell_xy(int i, int *x, int *y)
{
    *x = 24 + (i % 4) * 120 + 36;
    *y = 152 + (i / 4) * 116 + 36;
}

// Centro de la fila "row" del menu contextual de la celda i (sin scroll)
static void ctx_row_xy(int i, int row, int *x, int *y)
{
    int cx = 24 + (i % 4) * 120, iy = 152 + (i / 4) * 116, h = 4 * 56;
    int px = cx + 72 + 12;
    if (px + 262 > 480 - 8) {
        px = cx - 12 - 262;
    }
    px = px < 8 ? 8 : px > 480 - 8 - 262 ? 480 - 8 - 262 : px;
    int py = iy < 152 ? 152 : iy > 800 - 8 - h ? 800 - 8 - h : iy;
    *x = px + 100;
    *y = py + row * 56 + 28;
}

bool scene_drawer_run(void)
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
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // 1) abrir: la hoja sube (captura a mitad) y queda en OVERLAY
    sim_touch(240, 600, true);
    for (int y = 600; y >= 250; y -= 25) {
        sim_touch(240, y, true);
        sim_run(16);
    }
    sim_touch(240, 250, false);
    sim_run(120);
    sim_shot("dr_01_subiendo");
    sim_run(300);
    CHK(flex_drawer_is_open());
    CHK(flex_shell_state() == FLEX_SH_OVERLAY);
    sim_shot("dr_02_caja");

    // 2) buscador: teclado propio, "no" -> Notas; borrar; cerrar teclado
    sim_tap(200, 103);
    sim_run(100);
    sim_tap(12 + 8 * 46 + 21, 636 + 23);   // n (fila 2: x0 = 35)... ver abajo
    sim_shot("dr_03_buscando");
    // cerrar teclado
    sim_tap(12 + 25, 740 + 23);
    sim_run(100);
    CHK(flex_shell_state() == FLEX_SH_OVERLAY);

    // 3) Atras cierra la caja
    flex_sys_back();
    sim_run(400);
    CHK(!flex_drawer_is_open());
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // 4) pulsacion larga -> menu; Ocultar; "ver ocultas" la ensena atenuada
    open_drawer();
    CHK(flex_drawer_is_open());
    int x, y;
    cell_xy(1, &x, &y);   // segunda app por orden de nombre
    sim_touch(x, y, true);
    sim_run(700);
    sim_touch(x, y, false);
    sim_run(200);
    sim_shot("dr_04_menu");
    uint32_t hidden0 = g_home.hidden;
    int mx, my;
    ctx_row_xy(1, 2, &mx, &my);
    sim_tap(mx, my);   // fila "Ocultar"
    sim_run(200);
    CHK(g_home.hidden != hidden0);
    sim_tap(24 + 432 - 30, 103);   // ojo: ver ocultas
    sim_run(200);
    sim_shot("dr_05_ver_ocultas");
    // deshacer: menu sobre la misma celda -> Mostrar
    sim_touch(x, y, true);
    sim_run(700);
    sim_touch(x, y, false);
    sim_run(200);
    sim_tap(mx, my);
    sim_run(200);
    CHK(g_home.hidden == hidden0);
    // ficha
    sim_touch(x, y, true);
    sim_run(700);
    sim_touch(x, y, false);
    sim_run(200);
    ctx_row_xy(1, 3, &mx, &my);
    sim_tap(mx, my);
    sim_run(200);
    sim_shot("dr_06_ficha");
    sim_tap(240, 700);   // cualquier toque la cierra
    sim_run(200);

    // 5) toque en una app: la caja baja y la app abre (zoom desde el icono)
    cell_xy(0, &x, &y);
    sim_tap(x, y);
    sim_run(700);
    CHK(!flex_drawer_is_open());
    CHK(flex_shell_state() == FLEX_SH_APP);
    flex_sys_home();
    sim_run(400);

    // 6) app con candado y clave puesta: pide la clave antes de abrir
    CHK(flex_lock_set("2468", FLEX_LOCK_PIN));
    g_home.lock |= 1u << IC_AJUSTES;   // Ajustes va primera por orden de nombre
    open_drawer();
    cell_xy(0, &x, &y);
    sim_tap(x, y);
    sim_run(1000);
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    sim_shot("dr_07_candado");
    const char *pin = "2468";
    for (const char *p = pin; *p; p++) {
        int i = *p - '1';
        sim_tap(30 + (i % 3) * 144 + 66, 300 + (i / 3) * 94 + 41);
    }
    sim_run(800);
    CHK(flex_shell_state() == FLEX_SH_APP);
    CHK(flex_app_current() == IC_AJUSTES);
    g_home.lock = 0;
    flex_lock_clear();
    flex_sys_home();
    sim_run(400);

    // 7) bloquear con la caja abierta la cierra
    open_drawer();
    flex_shell_lock();
    sim_run(100);
    CHK(!flex_drawer_is_open());
    CHK(flex_shell_state() == FLEX_SH_LOCK);

    printf("caja: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
