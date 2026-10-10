// Modo seguro: la pantalla (motivo, contador, filas), la clave antes de lo que da
// acceso (riesgo de Arduino §11.6), cancelar vuelve al Modo seguro, escritorio
// limitado con su pildora, lista blanca de apps con su aviso, limpiar cachés,
// apps de terceros, sin banner de avisos y "Reiniciar normalmente".
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_safeboot.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("seguro: FALLO linea %d: %s\n", __LINE__, #c);    \
            ok = false;                                              \
        }                                                            \
    } while (0)

void sim_safe_set(int reason, int saved_fails);
void sim_safe_clear(void);
int sim_reboots(void);
int sim_fs_wipes(const char **last);

#define ROW_Y(i) (292 + (i) * 66 + 29)

static void key(char d)
{
    int i = d == '<' ? 9 : d == '0' ? 10 : d == 'K' ? 11 : d - '1';
    int c = i % 3, r = i / 3;
    sim_tap(30 + c * 144 + 66, 300 + r * 94 + 41);
}

static void pin(const char *s)
{
    for (; *s; s++) {
        key(*s);
    }
    sim_run(1500);
}

bool scene_safe_run(void)
{
    bool ok = true;
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_cfg_set_i32("apps3rd", 0);
    flex_lock_clear();
    sim_safe_clear();
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
    CHK(flex_shell_state() == FLEX_SH_HOME);
    // sin Modo seguro no hay pildora ni pantalla
    sim_tap(240, 75);
    sim_run(200);
    CHK(!flex_safe_screen_active());
    flex_safe_open();
    CHK(!flex_safe_screen_active());

    // 1) arranque con el tercer crash seguido: la pantalla, sin bloqueo
    sim_safe_set(FLEX_RST_PANIC, 2);
    CHK(flex_safe_mode() && flex_safe_fails() == 3);
    CHK(flex_lock_set("1470", FLEX_LOCK_PIN));
    uint32_t n_top = lv_obj_get_child_count(lv_layer_top());
    flex_safe_open();   // lo que hace flex_shell_start en Modo seguro
    sim_run(200);
    CHK(flex_safe_screen_active());
    CHK(flex_shell_state() == FLEX_SH_SAFE);
    CHK(flex_navbar_ctx() == FLEX_NAV_HIDDEN);
    sim_shot("safe_01_pantalla");
    sim_drag(240, 6, 240, 560, 260);   // ni panel rapido
    CHK(!flex_qs_is_open());
    flex_sys_home();
    flex_sys_recents();
    CHK(flex_shell_state() == FLEX_SH_SAFE);

    // 2) "Ir al escritorio" pide la clave (Arduino entraba sin ella); cancelar vuelve aqui
    sim_tap(240, ROW_Y(5));
    sim_run(500);
    CHK(flex_auth_active());
    CHK(!flex_safe_screen_active());
    sim_shot("safe_02_clave");
    sim_tap(24, 20);
    sim_run(400);
    CHK(!flex_auth_active());
    CHK(flex_safe_screen_active() && flex_shell_state() == FLEX_SH_SAFE);
    // cancelar la clave de Ajustes no deja Ajustes abierto detras
    sim_tap(240, ROW_Y(3));
    sim_run(500);
    CHK(flex_auth_active());
    sim_tap(24, 20);
    sim_run(400);
    CHK(flex_shell_fg_app() < 0);   // ni abierta ni tapada
    CHK(flex_safe_screen_active() && flex_shell_state() == FLEX_SH_SAFE);
    // una clave mala no entra
    sim_tap(240, ROW_Y(5));
    sim_run(500);
    pin("1111");
    CHK(flex_auth_active());
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    pin("1470");
    CHK(!flex_auth_active());
    CHK(flex_shell_state() == FLEX_SH_HOME);
    sim_shot("safe_03_escritorio");

    // 3) lista blanca: Notas no, Calculadora si
    flex_app_launch(IC_NOTAS, NULL);
    sim_run(300);
    CHK(flex_app_current() < 0 && flex_shell_state() == FLEX_SH_HOME);
    CHK(flex_safe_toast_visible());
    sim_shot("safe_04_no_disponible");
    sim_run(1700);
    CHK(!flex_safe_toast_visible());
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);
    flex_app_launch(IC_CALC, NULL);
    sim_run(500);
    CHK(flex_app_current() == IC_CALC);
    flex_app_close();
    sim_run(500);

    // 4) la pildora vuelve al Modo seguro; con la clave ya acertada Ajustes no la pide
    sim_tap(240, 75);
    sim_run(200);
    CHK(flex_safe_screen_active() && flex_shell_state() == FLEX_SH_SAFE);
    sim_tap(240, ROW_Y(3));
    sim_run(600);
    CHK(!flex_auth_active());
    CHK(flex_app_current() == IC_AJUSTES);
    flex_app_close();
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // 5) limpiar cachés: solo /System/Cache
    sim_tap(240, 75);
    sim_run(200);
    const char *last = NULL;
    int w0 = sim_fs_wipes(NULL);
    sim_tap(240, ROW_Y(2));
    sim_run(200);
    CHK(sim_fs_wipes(&last) == w0 + 1 && strcmp(last, "/System/Cache") == 0);
    CHK(flex_safe_screen_active());

    // 6) apps de terceros: inerte sin ninguna; con dos, las desactiva
    sim_tap(240, ROW_Y(1));
    sim_run(200);
    CHK(flex_safe_screen_active() && flex_cfg_get_i32("apps3rd", -1) == 0);
    flex_cfg_set_i32("apps3rd", 5);
    flex_safe_open();
    sim_run(100);
    sim_shot("safe_05_terceros");
    sim_tap(240, ROW_Y(1));
    sim_run(200);
    CHK(flex_cfg_get_i32("apps3rd", -1) == 0);

    // 7) avisos: al historial, sin banner
    int nn = flex_notif_count();
    flex_notify("Aviso en Modo seguro", NULL);
    sim_run(400);
    CHK(!flex_notif_banner_visible());
    CHK(flex_notif_count() == (nn < 3 ? nn + 1 : 3));

    // 8) bloquear (p. ej. al despertar) cierra la pantalla; desbloquear da el escritorio limitado
    flex_shell_lock();
    sim_run(100);
    CHK(!flex_safe_screen_active());
    CHK(flex_shell_state() == FLEX_SH_LOCK);

    // 9) "Reiniciar normalmente": contador a 0 y reinicio (no pide clave: no da acceso)
    flex_safe_open();
    sim_run(100);
    int rb = sim_reboots();
    sim_tap(240, ROW_Y(0));
    sim_run(100);
    CHK(sim_reboots() == rb + 1);
    CHK(flex_kvs_get_i32("flexsafe", "fails", -1) == 0);

    // fuera del Modo seguro para las escenas de detras
    flex_safe_close_now();
    sim_safe_clear();
    flex_lock_clear();
    flex_shell_lock();
    sim_run(200);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);
    flex_home_refresh();
    sim_tap(240, 75);
    sim_run(200);
    CHK(!flex_safe_screen_active());   // la pildora ya no esta
    flex_shell_lock();
    sim_run(100);
    printf("seguro: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
