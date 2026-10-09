// Recientes: tarjetas con miniatura por uso, carrusel, levantar para cerrar,
// ficha con pulsacion larga, reanudar la central y "Cerrar todas".
#include <stdio.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                         \
    do {                                                               \
        if (!(c)) {                                                    \
            printf("recientes: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                                \
        }                                                              \
    } while (0)

static void use_app(int id)
{
    flex_app_open(id, NULL);
    sim_run(500);
    flex_sys_home();
    sim_run(400);
}

bool scene_recents_run(void)
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
    sim_run(300);
    // limpiar lo que dejaron otras escenas
    for (int i = flex_recents_count() - 1; i >= 0; i--) {
        flex_app_terminate(flex_recents_app_at(i));
    }
    CHK(flex_recents_count() == 0);
    use_app(IC_NOTAS);
    use_app(IC_CALC);
    use_app(IC_RELOJ);
    CHK(flex_recents_count() == 3);
    CHK(flex_recents_app_at(0) == IC_RELOJ && flex_recents_app_at(2) == IC_NOTAS);
    CHK(flex_recents_has_thumb(0) && flex_recents_has_thumb(2));
    use_app(IC_NOTAS);   // vuelve al frente
    CHK(flex_recents_app_at(0) == IC_NOTAS && flex_recents_count() == 3);

    // abrir desde una app con el boton Recientes: la app se suspende
    flex_app_open(IC_CALEND, NULL);
    sim_run(500);
    flex_sys_recents();
    sim_run(60);
    sim_shot("rc_01_entrando");
    sim_run(300);
    CHK(flex_recents_is_open());
    CHK(flex_shell_state() == FLEX_SH_OVERLAY);
    CHK(flex_recents_count() == 4 && flex_recents_app_at(0) == IC_CALEND);
    sim_shot("rc_02_recientes");

    // carrusel: deslizar a la izquierda -> la segunda queda centrada
    sim_drag(400, 330, 100, 330, 200);
    sim_run(700);
    sim_shot("rc_03_desplazado");
    // ficha: mantener 480 ms la central
    sim_touch(240, 330, true);
    sim_run(600);
    sim_touch(240, 330, false);
    sim_run(200);
    sim_shot("rc_04_ficha");
    sim_tap(240, 150);   // fuera de la hoja: la cierra
    sim_run(200);
    // levantar la central 130 px: se cierra esa app
    int before = flex_recents_count();
    int apps[32];
    for (int i = 0; i < before; i++) {
        apps[i] = flex_recents_app_at(i);
    }
    sim_drag(240, 400, 240, 260, 250);
    sim_run(400);
    CHK(flex_recents_count() == before - 1);
    int victim = -1;
    for (int i = 0; i < before; i++) {
        bool still = false;
        for (int k = 0; k < flex_recents_count(); k++) {
            still |= flex_recents_app_at(k) == apps[i];
        }
        if (!still) {
            victim = apps[i];
        }
    }
    CHK(victim >= 0 && !flex_app_is_open(victim));
    // toque en la central: reanuda la app
    int resume = -1;
    sim_run(300);
    resume = flex_recents_app_at(1 < flex_recents_count() ? 1 : 0);
    (void)resume;
    sim_tap(240, 330);
    sim_run(600);
    CHK(!flex_recents_is_open());
    CHK(flex_shell_state() == FLEX_SH_APP);
    sim_shot("rc_05_reanudada");
    // "Cerrar todas"
    flex_sys_recents();
    sim_run(400);
    sim_tap(240, 623);
    sim_run(400);
    CHK(flex_recents_count() == 0);
    sim_shot("rc_06_vacia");
    // fuera de las tarjetas -> Inicio
    sim_tap(240, 760);
    sim_run(300);
    CHK(!flex_recents_is_open());
    CHK(flex_shell_state() == FLEX_SH_HOME);

    printf("recientes: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
