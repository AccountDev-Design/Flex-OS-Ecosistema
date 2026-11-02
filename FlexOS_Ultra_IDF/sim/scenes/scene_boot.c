// Arranque (01a §2.3-2.5): banda forense solo tras un reinicio anormal (2200 ms), splash de 2600 ms con la
// curva de opacidad de Arduino, toques tragados mientras dura y destino al terminar: bloqueo, Modo seguro
// (tras la banda, sin splash) o primera configuracion.
#include <stdio.h>
#include <string.h>
#include "flex_boot_model.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_safeboot.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void flex_boot_begin(void (*done)(void));
void flex_boot_sim_set(bool enabled, int reason);
void sim_safe_set(int reason, int saved_fails);
void flex_oobe_finish_for_test(void);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("arranque: FALLO linea %d: %s\n", __LINE__, #c);  \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void reset_shell(void)
{
    flex_shell_lock();
    sim_run(200);
}

bool scene_boot_run(void)
{
    bool ok = true;
    flex_lock_clear();
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    }
    flex_cfg_set_bool("oobe", true);
    sim_safe_set(FLEX_RST_POWERON, 0);
    reset_shell();

    // 1) encendido normal: sin banda; splash de 2600 ms y luego el bloqueo
    flex_boot_sim_set(true, FLEX_RST_POWERON);
    flex_boot_begin(flex_shell_boot_destination);
    sim_run(100);
    CHK(flex_boot_active());
    sim_run(500);
    sim_shot("arranque_splash_subiendo");
    sim_run(500);                               // 1100 ms: opacidad al maximo
    sim_shot("arranque_splash");
    sim_tap(240, 400);                          // los toques no llegan a nada
    CHK(flex_boot_active());
    sim_run(1100);                              // 2480 ms (con el toque): aun
    CHK(flex_boot_active());
    sim_run(300);
    CHK(!flex_boot_active() && flex_shell_state() == FLEX_SH_LOCK && flex_shell_lock_visible());

    // 2) tras un fallo (PANIC): banda forense 2200 ms, luego el splash entero
    flex_boot_sim_set(true, FLEX_RST_PANIC);
    flex_boot_begin(flex_shell_boot_destination);
    sim_run(300);
    sim_shot("arranque_banda");
    sim_run(1700);                              // 2000 ms: sigue la banda
    CHK(flex_boot_active());
    sim_run(400);                               // 2400: ya en el splash (empezo a los 2200)
    sim_shot("arranque_tras_banda");
    sim_run(2200);                              // 4600 ms < 2200 + 2600
    CHK(flex_boot_active());
    sim_run(400);
    CHK(!flex_boot_active() && flex_shell_lock_visible());

    // 3) Modo seguro: tras la banda va directo a su pantalla (sin splash ni bloqueo)
    sim_safe_set(FLEX_RST_PANIC, 2);
    CHK(flex_safe_mode());
    flex_boot_sim_set(true, FLEX_RST_PANIC);
    flex_boot_begin(flex_shell_boot_destination);
    sim_run(2000);
    CHK(flex_boot_active() && !flex_safe_screen_active());
    sim_run(400);                               // 2400 > 2200
    CHK(!flex_boot_active() && flex_safe_screen_active());
    flex_safe_close_now();
    sim_safe_set(FLEX_RST_POWERON, 0);
    reset_shell();

    // 4) placa sin configurar (OOBE) y despertar de deep sleep (sin banda): splash y luego el idioma
    flex_cfg_set_bool("oobe", false);
    flex_boot_sim_set(true, FLEX_RST_DEEPSLEEP);
    flex_boot_begin(flex_shell_boot_destination);
    sim_run(300);
    CHK(flex_boot_active());
    sim_run(2500);
    CHK(!flex_boot_active() && flex_oobe_active());
    flex_oobe_finish_for_test();                // sale del OOBE como si se hubiera completado
    flex_cfg_set_bool("oobe", true);

    flex_boot_sim_set(false, FLEX_RST_POWERON);
    reset_shell();
    printf("arranque: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
