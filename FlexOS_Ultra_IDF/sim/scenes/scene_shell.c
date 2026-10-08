// Shell completo: bloqueo -> desbloqueo -> escritorio -> abrir app -> atras.
#include <stdio.h>
#include "flex_i18n.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

static bool s_built;

static void boot(bool glass, bool dark, int icon_style, int lockw)
{
    flex_cfg_set_bool("glass", glass);
    flex_cfg_set_bool("dark", dark);
    flex_cfg_set_i32("iconstyle", icon_style);
    flex_cfg_set_i32("lockwidgets", lockw);
    if (!s_built) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
        s_built = true;
    } else {
        flex_theme_set_glass(glass);
        flex_theme_set_dark(dark);
        flex_theme_set_icon_style((uint8_t)icon_style);
        flex_shell_lock();
    }
}

bool scene_shell_run(void)
{
    bool ok = true;
    boot(true, true, 1, 0x0F);
    sim_run(200);
    sim_shot("sh_01_bloqueo");
    // desbloqueo: arrastre hacia arriba desde el asa
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
    ok &= flex_shell_state() == FLEX_SH_HOME;
    sim_shot("sh_02_escritorio");
    // pagina siguiente
    sim_drag(400, 400, 60, 400, 250);
    sim_run(400);
    sim_shot("sh_03_pagina2");
    sim_drag(60, 400, 420, 400, 250);
    sim_run(400);
    // abrir Notas (ranura 5: fila 1, columna 1)
    sim_tap(144 + 36, 212 + 112 + 36);
    sim_run(100);
    sim_shot("sh_04_abriendo");
    sim_run(400);
    ok &= flex_shell_state() == FLEX_SH_APP;
    sim_shot("sh_05_app");
    // atras (barra de navegacion: tercio izquierdo)
    sim_tap(80, 768);
    sim_run(80);
    sim_shot("sh_06_cerrando");
    sim_run(400);
    ok &= flex_shell_state() == FLEX_SH_HOME;
    sim_shot("sh_07_vuelta");
    // tema claro + plano
    boot(false, false, 0, 0x01);
    sim_run(200);
    sim_shot("sh_08_bloqueo_plano");
    printf("shell: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
