// Juegos · Jumper (03 §14): la app real en el simulador. El juego es el de Arduino sobre el motor de dibujo de Arduino (components/flex_games);
// aqui se prueba lo que pone el marco: pantalla completa y horizontal (sin barra de navegacion ni gestos del sistema), el lienzo vivo a 60 Hz, el
// tactil fisico girado, la salida por la flecha del selector, pausa al suspender, records que sobreviven y la memoria que se devuelve al cerrar.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_jumper.h"
#include "flex_passcode.h"
#include "flex_qs_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

uint32_t flex_app_games_frames(void);
uint32_t flex_app_games_laps(void);
bool flex_app_games_open(void);
uint32_t flex_app_games_presses(void);
bool flex_app_games_down(void);
const uint16_t *flex_app_games_canvas(void);
uint16_t *sim_fb(void);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("juegos: FALLO linea %d: %s\n", __LINE__, #c);    \
            ok = false;                                              \
        }                                                            \
    } while (0)

// un toque en el lienzo LOGICO 800 x 480 (el panel lo da girado: x = 479 - ly, y = lx)
static void tap_l(int lx, int ly)
{
    int px = 479 - ly, py = lx;
    sim_touch(px, py, true);
    sim_run(60);
    sim_touch(px, py, false);
    sim_run(100);
}

static void unlock_and_clean(void)
{
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
    for (int i = 0; i < FLEX_APP_N; i++) {
        flex_app_terminate_ex(i, true);
    }
    sim_run(200);
}

static int nonblank(void)
{
    const uint16_t *c = flex_app_games_canvas();
    int n = 0;
    for (size_t i = 0; c && i < FLEX_JMP_BYTES / 2; i++) {
        n += c[i] != 0;
    }
    return n;
}

static bool open_games(void)
{
    flex_app_open(IC_JUEGOS, NULL);
    sim_run(700);
    return flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_JUEGOS;
}

static bool wait_screen(int s, int ms)
{
    for (int t = 0; t < ms && flex_jmp_screen() != s; t += 20) {
        sim_run(20);
    }
    return flex_jmp_screen() == s;
}

bool scene_juegos_run(void)
{
    bool ok = true;
    unlock_and_clean();
    flex_cfg_set_u32("jmp_bn", 0);
    flex_cfg_set_u32("jmp_att", 0);

    // ---- abrir: pantalla completa y horizontal, el juego en marcha ----------------------------------------------------------------------
    CHK(open_games());
    CHK(flex_app_games_open() && flex_shell_land());
    CHK(flex_navbar_ctx() == FLEX_NAV_HIDDEN);   // sin barra de navegacion
    CHK(flex_jmp_screen() == FLEX_JMP_S_INTRO);
    sim_run(1000);
    CHK(flex_app_games_frames() > 40 && flex_app_games_laps() > 100);   // ~60 cuadros por segundo
    CHK(nonblank() > 100000);
    sim_shot("juegos_intro");
    CHK(wait_screen(FLEX_JMP_S_SELECT, 3000));
    sim_run(800);
    sim_shot("juegos_selector");

    // ---- sin gestos del sistema: ni la cortina del panel rapido ni el Centro de avisos, ni Inicio por el borde de abajo ------------------------------
    sim_drag(240, 4, 240, 400, 300);
    sim_run(400);
    CHK(!flex_qs_is_open() && flex_shell_state() == FLEX_SH_APP);
    flex_notif_center_show();
    sim_run(300);
    CHK(!flex_notif_center_open());
    sim_drag(240, 796, 240, 400, 300);
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_JUEGOS && !flex_notif_center_open());

    // ---- el flanco de apoyo llega al juego UNA vez por toque y el dedo se suelta al levantarlo ------------------------------------------------------
    {
        uint32_t p0 = flex_app_games_presses();
        sim_touch(100, 100, true);
        sim_run(120);
        CHK(flex_app_games_down() && flex_app_games_presses() == p0 + 1);
        sim_touch(100, 100, false);
        sim_run(120);
        CHK(!flex_app_games_down() && flex_app_games_presses() == p0 + 1);
    }

    // ---- el tactil girado: "Normal" arranca la partida; el nivel corre solo y el cubo muere en el primer pico y reaparece ---------------------
    tap_l(400, 287);
    CHK(flex_jmp_screen() == FLEX_JMP_S_PLAY);
    uint32_t f0 = flex_app_games_frames();
    sim_run(500);
    CHK(flex_app_games_frames() > f0 + 20);
    CHK(flex_jmp_player_x() > 100.0f);
    sim_shot("juegos_partida");
    CHK(wait_screen(FLEX_JMP_S_DEAD, 4000));
    // sin tocar, el cubo muere en el primer pico (x = 616 del nivel): un flanco que se repite o un dedo que no se suelta lo harian saltar por encima
    CHK(flex_jmp_player_x() > 550.0f && flex_jmp_player_x() < 700.0f);
    sim_shot("juegos_muerte");
    CHK(wait_screen(FLEX_JMP_S_PLAY, 3000));
    CHK(flex_jmp_attempts() >= 2 && flex_jmp_best_normal() >= 1);

    // ---- pausa por el HUD; Inicio con el juego en marcha suspende (queda en pausa) y al volver sigue donde estaba ---------------------------------
    tap_l(30, 30);
    CHK(flex_jmp_screen() == FLEX_JMP_S_PAUSE);
    sim_shot("juegos_pausa");
    // lo que se ve en el panel es el lienzo del juego (la pausa no cambia entre cuadros)
    sim_run(200);
    CHK(memcmp(sim_fb(), flex_app_games_canvas(), FLEX_JMP_BYTES) == 0);
    tap_l(400, 212);                      // CONTINUAR
    CHK(flex_jmp_screen() == FLEX_JMP_S_PLAY);
    sim_run(300);
    flex_sys_home();
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_HOME && !flex_shell_land());
    CHK(flex_jmp_screen() == FLEX_JMP_S_PAUSE);
    uint32_t laps = flex_app_games_laps();
    sim_run(400);
    CHK(flex_app_games_laps() == laps);   // suspendido: no corre
    CHK(flex_navbar_ctx() != FLEX_NAV_APP || flex_shell_state() == FLEX_SH_HOME);
    CHK(open_games());
    sim_run(300);
    CHK(flex_jmp_screen() == FLEX_JMP_S_PAUSE && flex_app_games_laps() > laps);
    tap_l(400, 212);
    CHK(flex_jmp_screen() == FLEX_JMP_S_PLAY);

    // ---- con el panel rapido o el bloqueo encima, el juego no se queda corriendo en segundo plano --------------------------------------------------
    flex_shell_lock();
    sim_run(400);
    CHK(!flex_shell_land());
    laps = flex_app_games_laps();
    sim_run(300);
    CHK(flex_app_games_laps() == laps);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(500);

    // ---- records: se guardan solos y sobreviven a cerrar la app (la memoria del lienzo se devuelve) ---------------------------------------------------
    int best = flex_jmp_best_normal();
    flex_app_terminate_ex(IC_JUEGOS, true);
    sim_run(300);
    CHK(!flex_app_games_open() && flex_app_games_canvas() == NULL);
    CHK((int)flex_cfg_get_u32("jmp_bn", 0) == best && best >= 1);
    CHK(open_games());
    CHK(flex_jmp_best_normal() == best);
    // cerrar justo tras morir, con el record nuevo SIN guardar aun (el juego lo guarda al reaparecer): cerrar la app lo guarda
    flex_app_terminate_ex(IC_JUEGOS, true);
    sim_run(200);
    flex_cfg_set_u32("jmp_bn", 0);
    flex_cfg_set_u32("jmp_att", 0);
    CHK(open_games());
    CHK(wait_screen(FLEX_JMP_S_SELECT, 3500));
    sim_run(800);
    tap_l(400, 287);
    CHK(wait_screen(FLEX_JMP_S_DEAD, 4000));
    best = flex_jmp_best_normal();
    CHK(best >= 1 && flex_cfg_get_u32("jmp_bn", 0) == 0);   // aun sin guardar
    flex_app_terminate_ex(IC_JUEGOS, true);
    sim_run(300);
    CHK((int)flex_cfg_get_u32("jmp_bn", 0) == best);
    CHK(open_games());

    // ---- la flecha del selector sale de la app --------------------------------------------------------------------------------------------------
    CHK(wait_screen(FLEX_JMP_S_SELECT, 3500));
    sim_run(800);
    tap_l(40, 40);
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_HOME && !flex_shell_land());
    CHK(flex_navbar_ctx() == FLEX_NAV_HOME);
    flex_app_terminate_ex(IC_JUEGOS, true);
    sim_run(200);
    printf("juegos: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
