// Apagado completo: "Apagar FlexOS?" desde el boton del panel rapido, el
// deslizador (vuelve solo si no llega al 92 %), Cancelar vuelve a donde se
// estaba (Inicio o la app), nada de panel ni barra mientras esta abierto, el
// apagado seguro pide la clave (cancelarla vuelve al deslizador) y la animacion
// final (fundido, "Flex OS", retroiluminacion a 0, SLPIN y deep sleep) no se
// interrumpe con nada.
#include <stdio.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_display.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_poweroff.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("apagado: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

int sim_deep_sleeps(void);
bool sim_display_sleep_in(void);
uint8_t sim_display_backlight(void);
void flex_poweroff_sim_reset(void);

#define KNOB_Y (FLEX_POFF_TRACK_Y + FLEX_POFF_TRACK_H / 2)

static void open_power(void)
{
    sim_touch(240, 6, true);
    sim_run(60);
    sim_drag(240, 6, 240, 560, 260);   // panel rapido
    sim_run(300);
    sim_tap(390, 50);                  // boton de apagado de la cabecera
    sim_run(200);
}

// Arrastra el pomo desde el reposo hasta x y suelta.
static void slide_to(int x)
{
    int x0 = FLEX_POFF_TRACK_X + FLEX_POFF_KNOB_PAD + FLEX_POFF_KNOB_D / 2;
    sim_touch(x0, KNOB_Y, true);
    sim_run(40);
    for (int px = x0; px <= x; px += 12) {
        sim_touch(px, KNOB_Y, true);
        sim_run(16);
    }
    sim_touch(x, KNOB_Y, true);
    sim_run(16);
    sim_touch(x, KNOB_Y, false);
    sim_run(16);
}

static void key(char d)
{
    int i = d == '<' ? 9 : d == '0' ? 10 : d == 'K' ? 11 : d - '1';
    int c = i % 3, r = i / 3;
    sim_tap(30 + c * 144 + 66, 300 + r * 94 + 41);
}

bool scene_poweroff_run(void)
{
    bool ok = true;
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_cfg_set_bool("poffpin", false);
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
    CHK(flex_shell_state() == FLEX_SH_HOME);
    uint32_t n_top = lv_obj_get_child_count(lv_layer_top());

    // 1) desde el panel rapido: pantalla de confirmacion, sin panel ni barra
    open_power();
    CHK(flex_poweroff_active());
    CHK(flex_shell_state() == FLEX_SH_POWEROFF);
    CHK(!flex_qs_is_open());
    CHK(flex_navbar_ctx() == FLEX_NAV_HIDDEN);
    sim_shot("poff_01_confirmar");
    sim_drag(240, 6, 240, 560, 260);   // el panel rapido no abre aqui
    CHK(!flex_qs_is_open());
    sim_drag(4, 400, 300, 402, 200);   // ni el Centro
    CHK(!flex_notif_center_open());
    flex_sys_back();
    flex_sys_home();
    flex_sys_recents();
    sim_run(100);
    CHK(flex_shell_state() == FLEX_SH_POWEROFF);

    // 2) a medias (< 92 %) el pomo vuelve solo; tocar fuera del pomo no lo agarra
    slide_to(FLEX_POFF_TRACK_X + 200);
    CHK(flex_poweroff_knob() > 100);
    sim_shot("poff_02_a_medias");
    sim_run(400);
    CHK(flex_poweroff_knob() == 0);
    sim_drag(400, KNOB_Y, 460, KNOB_Y, 100);   // agarre lejos del pomo
    CHK(flex_poweroff_knob() == 0);
    CHK(flex_shell_state() == FLEX_SH_POWEROFF);

    // 3) el bloqueo por inactividad no cae con la pantalla de apagado abierta
    flex_cfg_set_i32("autolockms", 60000);
    sim_run(65000);
    CHK(flex_shell_state() == FLEX_SH_POWEROFF);

    // 4) Cancelar vuelve a Inicio sin dejar nada
    sim_tap(240, 596);
    sim_run(200);
    CHK(!flex_poweroff_active());
    CHK(flex_shell_state() == FLEX_SH_HOME);
    CHK(flex_navbar_ctx() == FLEX_NAV_HOME);
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);

    // 5) desde una app: Cancelar vuelve a la app (Arduino volvia a Inicio con la app viva)
    flex_app_open(IC_NOTAS, NULL);
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_APP);
    open_power();
    CHK(flex_shell_state() == FLEX_SH_POWEROFF);
    sim_tap(240, 596);
    sim_run(200);
    CHK(flex_shell_state() == FLEX_SH_APP);
    CHK(flex_app_current() == IC_NOTAS);
    CHK(flex_navbar_ctx() == FLEX_NAV_APP);
    flex_app_close();
    sim_run(500);

    // 6) apagado seguro: la clave va antes; cancelarla vuelve al deslizador
    CHK(flex_lock_set("1470", FLEX_LOCK_PIN));
    flex_cfg_set_bool("poffpin", true);
    open_power();
    slide_to(FLEX_POFF_TRACK_X + FLEX_POFF_TRACK_W);
    sim_run(500);
    CHK(flex_auth_active());
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    CHK(sim_deep_sleeps() == 0);
    sim_shot("poff_03_clave");
    sim_tap(24, 20);   // atras
    sim_run(400);
    CHK(!flex_auth_active());
    CHK(flex_poweroff_active() && flex_shell_state() == FLEX_SH_POWEROFF);
    CHK(flex_poweroff_knob() == 0);
    CHK(sim_deep_sleeps() == 0);
    // una clave equivocada no apaga
    slide_to(FLEX_POFF_TRACK_X + FLEX_POFF_TRACK_W);
    sim_run(500);
    key('1'); key('1'); key('1'); key('1');
    sim_run(1500);
    CHK(sim_deep_sleeps() == 0);
    CHK(flex_auth_active());
    // la buena: animacion final
    key('1'); key('4'); key('7'); key('0');
    sim_run(600);
    CHK(!flex_auth_active());
    CHK(flex_poweroff_running());
    CHK(flex_shell_state() == FLEX_SH_POWEROFF);
    sim_shot("poff_04_fundido");
    // nada la interrumpe: Cancelar, dos dedos (suspender), bloquear
    sim_tap(240, 596);
    for (int k = 0; k < 2; k++) {
        sim_touch_n(240, 400, 2);
        sim_run(60);
        sim_touch_n(240, 400, 0);
        sim_run(120);
    }
    flex_shell_lock();
    CHK(!flex_power_suspended());
    CHK(flex_shell_state() == FLEX_SH_POWEROFF);
    sim_run(500);
    sim_shot("poff_05_flex_os");
    CHK(sim_deep_sleeps() == 0);
    sim_run(1600);   // 520 + 260 + 700 + 620 + 150 ms en total
    CHK(sim_deep_sleeps() == 1);
    CHK(sim_display_sleep_in());
    CHK(sim_display_backlight() == 0);
    CHK(flex_cfg_get_bool("cleanoff", false));
    sim_run(500);
    CHK(sim_deep_sleeps() == 1);   // una sola vez

    // Simulador: "despertar" para las escenas que vengan detras
    flex_poweroff_sim_reset();
    flex_cfg_set_bool("cleanoff", false);
    flex_cfg_set_bool("poffpin", false);
    flex_lock_clear();
    flex_display_panel_on(true);
    flex_display_backlight_enable(true);
    flex_shell_lock();
    sim_run(200);
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);
    printf("apagado: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
