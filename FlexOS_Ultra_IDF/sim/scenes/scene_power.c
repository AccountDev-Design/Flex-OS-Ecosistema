// Energia: suspension por doble toque de dos dedos, despertar con uno (con clave:
// bloqueo compuesto a oscuras y vuelta a la app tras acertar) y bloqueo por
// inactividad.
#include <stdio.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

uint8_t sim_display_backlight(void);
bool sim_display_panel_on(void);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("energia: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void two_finger_tap(void)
{
    sim_touch_n(200, 400, 2);
    sim_run(80);
    sim_touch_n(200, 400, 0);
    sim_run(60);
}

static void one_finger_tap(void)
{
    sim_touch_n(240, 400, 1);
    sim_run(60);
    sim_touch_n(240, 400, 0);
    sim_run(60);
}

static void pin(const char *s)
{
    for (; *s; s++) {
        int i = *s == '0' ? 10 : *s - '1';
        sim_tap(30 + (i % 3) * 144 + 66, 300 + (i / 3) * 94 + 41);
    }
}

bool scene_power_run(void)
{
    bool ok = true;
    flex_cfg_set_i32("lockwidgets", 1);
    flex_cfg_set_i32("autolockms", 60000);
    flex_lock_set_fails(0);
    flex_lock_clear();
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    }
    flex_shell_lock();
    sim_run(100);
    // sin clave: desliz -> escritorio, abrir una app
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    flex_app_open(IC_NOTAS, NULL);
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_APP);

    // 1) doble toque de dos dedos: fundido a 0 y DISPOFF; el estado no cambia
    two_finger_tap();
    sim_run(60);
    two_finger_tap();
    CHK(flex_power_suspended());
    sim_run(300);
    CHK(sim_display_backlight() == 0);
    CHK(!sim_display_panel_on());
    CHK(flex_shell_state() == FLEX_SH_APP);
    // suspendido, un toque suelto no hace nada (ni llega a la app)
    one_finger_tap();
    sim_run(600);
    CHK(flex_power_suspended() && !sim_display_panel_on());

    // 2) doble toque de un dedo sin clave: vuelve donde estaba
    one_finger_tap();
    one_finger_tap();
    sim_run(400);
    CHK(!flex_power_suspended());
    CHK(sim_display_panel_on() && sim_display_backlight() > 0);
    CHK(flex_shell_state() == FLEX_SH_APP);

    // 3) con clave: al despertar, bloqueo; al acertar, de vuelta a la app
    CHK(flex_lock_set("1470", FLEX_LOCK_PIN));
    two_finger_tap();
    sim_run(60);
    two_finger_tap();
    sim_run(300);
    CHK(flex_power_suspended());
    one_finger_tap();
    one_finger_tap();
    CHK(flex_shell_state() == FLEX_SH_LOCK);   // compuesto antes de encender
    sim_run(400);
    sim_shot("en_01_despertar_bloqueo");
    sim_drag(240, 700, 240, 400, 200);
    sim_run(600);
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    pin("1470");
    sim_run(600);
    CHK(flex_shell_state() == FLEX_SH_APP);
    CHK(flex_app_current() == IC_NOTAS);
    sim_shot("en_02_vuelta_app");

    // 4) bloqueo por inactividad (1 min): cierra la app y cae el bloqueo
    sim_run(59000);
    CHK(flex_shell_state() == FLEX_SH_APP);
    sim_run(2500);
    CHK(flex_shell_state() == FLEX_SH_LOCK);
    sim_shot("en_03_autobloqueo");
    // "Nunca"
    flex_cfg_set_i32("autolockms", 0);
    flex_lock_clear();
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    sim_run(130000);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    flex_cfg_set_i32("autolockms", 60000);
    flex_shell_lock();
    sim_run(100);

    printf("energia: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
