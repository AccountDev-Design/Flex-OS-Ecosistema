// Restablecimiento de fabrica: aviso, cancelar sin tocar nada (a Inicio, a la
// app o al Modo seguro), escribir RESTABLECER sin clave, la clave si la hay,
// deslizar (a medias no hace nada), progreso por etapas (>= 260 ms cada una),
// "Listo" y reinicio, fallo con Reintentar, marcador que no se puede armar,
// reanudacion tras un corte y nada que interrumpa el borrado.
// El motor es el modelo real (flex_reset_model.c) sobre un disco simulado.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_kb_layout.h"
#include "flex_passcode.h"
#include "flex_reset.h"
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
            printf("fabrica: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

void sim_reset_clear(void);
void sim_reset_fail(int stage, bool arm);
int sim_reset_runs(int stage);
int sim_reset_reboots(void);
const flex_fr_marker_t *sim_reset_disk(void);
void sim_reset_set_disk(const flex_fr_marker_t *m);
void flex_factory_sim_reset(void);
void sim_safe_set(int reason, int saved_fails);
void sim_safe_clear(void);

enum { V_INTRO = 1, V_TYPE, V_SLIDE, V_RUN, V_FAIL };
#define KNOB_Y (600 + 36)

static int runs_total(void)
{
    int n = 0;
    for (int s = FLEX_FR_ARMED; s <= FLEX_FR_DEFAULTS; s++) {
        n += sim_reset_runs(s);
    }
    return n;
}

static void type_word(const char *w)
{
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 0, 0);
    flex_kb_state_t st;
    flex_kb_state_init(&st, true);
    for (; *w; w++) {
        char lc = (char)(*w | 0x20);
        for (int c = 0; c < FLEX_KB_CELLS; c++) {
            const char *b = flex_kb_key_base(&st, c);
            if ((b[0] | 0x20) == lc && b[1] == 0) {
                int x, y;
                flex_kb_cell_xy(&g, c, &x, &y);
                sim_tap(x + g.kw / 2, y + g.kh / 2);
                break;
            }
        }
    }
    sim_run(100);
}

static void slide(int to_x)
{
    int x0 = 46 + 30;
    sim_touch(x0, KNOB_Y, true);
    sim_run(40);
    for (int x = x0; x <= to_x; x += 12) {
        sim_touch(x, KNOB_Y, true);
        sim_run(16);
    }
    sim_touch(to_x, KNOB_Y, true);
    sim_run(16);
    sim_touch(to_x, KNOB_Y, false);
    sim_run(100);
}

static void pin(const char *s)
{
    for (; *s; s++) {
        int i = *s == '0' ? 10 : *s - '1';
        sim_tap(30 + (i % 3) * 144 + 66, 300 + (i / 3) * 94 + 41);
    }
    sim_run(1500);
}

// Hasta "Listo" (o fallo); devuelve el tiempo que tardo en pantalla
static uint32_t run_until_end(void)
{
    uint32_t t = 0;
    while (t < 10000 && flex_factory_view() == V_RUN && flex_factory_shown_stage() != FLEX_FR_DONE) {
        sim_run(50);
        t += 50;
    }
    return t;
}

bool scene_factory_run(void)
{
    bool ok = true;
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_lock_clear();
    sim_reset_clear();
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
    uint32_t n_top = lv_obj_get_child_count(lv_layer_top());

    // 1) aviso; Cancelar vuelve a Inicio sin tocar nada
    flex_factory_reset_open(false);
    sim_run(200);
    CHK(flex_factory_view() == V_INTRO && flex_shell_state() == FLEX_SH_FACTORY);
    CHK(flex_navbar_ctx() == FLEX_NAV_HIDDEN);
    sim_shot("fr_01_aviso");
    sim_drag(240, 6, 240, 560, 260);   // ni panel rapido
    CHK(!flex_qs_is_open());
    flex_sys_home();
    flex_sys_recents();
    CHK(flex_shell_state() == FLEX_SH_FACTORY);
    sim_tap(129, 733);   // Cancelar
    sim_run(300);
    CHK(!flex_factory_active() && flex_shell_state() == FLEX_SH_HOME);
    CHK(runs_total() == 0 && !sim_reset_disk()->pending);

    // 2) desde una app: Cancelar vuelve a la app
    flex_app_open(IC_NOTAS, NULL);
    sim_run(500);
    flex_factory_reset_open(false);
    sim_run(200);
    sim_tap(129, 733);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_app_current() == IC_NOTAS);

    // 3) sin clave: escribir RESTABLECER (solo esa palabra habilita Continuar)
    flex_factory_reset_open(false);
    sim_run(200);
    sim_tap(351, 733);   // Continuar
    sim_run(300);
    CHK(flex_factory_view() == V_TYPE);
    type_word("RESTA");
    sim_tap(240, 259);   // Continuar con la palabra a medias: nada
    sim_run(200);
    CHK(flex_factory_view() == V_TYPE);
    type_word("BLECER");
    sim_shot("fr_02_escribir");
    sim_tap(240, 259);
    sim_run(300);
    CHK(flex_factory_view() == V_SLIDE);
    sim_shot("fr_03_deslizar");

    // 4) a medias el pomo vuelve de golpe y no se borra nada; Cancelar vuelve a la app
    slide(46 + 30 + 180);
    CHK(flex_factory_knob() == 0);
    CHK(flex_factory_view() == V_SLIDE && runs_total() == 0);
    sim_tap(240, 733);
    sim_run(300);
    CHK(!flex_factory_active() && flex_app_current() == IC_NOTAS);
    CHK(!sim_reset_disk()->pending);

    // 5) todo el camino: progreso por etapas, apps cerradas, "Listo" y reinicio
    flex_factory_reset_open(false);
    sim_run(200);
    sim_tap(351, 733);
    sim_run(300);
    type_word("RESTABLECER");
    sim_tap(240, 259);
    sim_run(300);
    slide(46 + 30 + 330);
    uint32_t t0 = lv_tick_get();
    sim_run(100);
    CHK(flex_factory_view() == V_RUN);
    CHK(!flex_app_is_open(IC_NOTAS));   // ARMED: ninguna app sigue abierta
    sim_shot("fr_04_progreso");
    // nada lo interrumpe: tocar, dos dedos, bloquear
    sim_tap(240, 733);
    for (int k = 0; k < 2; k++) {
        sim_touch_n(240, 400, 2);
        sim_run(60);
        sim_touch_n(240, 400, 0);
        sim_run(120);
    }
    flex_shell_lock();
    CHK(!flex_power_suspended() && flex_shell_state() == FLEX_SH_FACTORY);
    run_until_end();
    CHK(flex_factory_shown_stage() == FLEX_FR_DONE);
    CHK(lv_tick_get() - t0 >= FLEX_FR_STEPS * 260);   // cada etapa se ve al menos 260 ms
    for (int s = FLEX_FR_ARMED; s <= FLEX_FR_DEFAULTS; s++) {
        CHK(sim_reset_runs(s) == 1);
    }
    CHK(sim_reset_disk()->stage == FLEX_FR_DONE);
    sim_shot("fr_05_listo");
    int rb = sim_reset_reboots();
    sim_run(1400);
    CHK(sim_reset_reboots() == rb + 1);
    flex_factory_sim_reset();
    sim_reset_clear();

    // 6) falla una etapa: pantalla de fallo con su nombre; Reintentar retoma y termina
    sim_reset_fail(FLEX_FR_FILES, false);
    flex_factory_reset_open(false);
    sim_run(200);
    sim_tap(351, 733);
    sim_run(300);
    type_word("RESTABLECER");
    sim_tap(240, 259);
    sim_run(300);
    slide(46 + 30 + 330);
    run_until_end();
    sim_run(400);
    CHK(flex_factory_view() == V_FAIL);
    CHK(sim_reset_disk()->stage == FLEX_FR_FAIL && sim_reset_disk()->err == FLEX_FR_FILES);
    CHK(sim_reset_runs(FLEX_FR_NVS) == 0);   // lo de despues no se toco
    sim_shot("fr_06_fallo");
    sim_reset_fail(0, false);
    sim_tap(240, 657);   // Reintentar
    sim_run(100);
    run_until_end();
    CHK(flex_factory_shown_stage() == FLEX_FR_DONE);
    CHK(sim_reset_runs(FLEX_FR_APPDATA) == 1 && sim_reset_runs(FLEX_FR_FILES) == 2);
    sim_run(1400);
    flex_factory_sim_reset();
    sim_reset_clear();

    // 7) el marcador no se puede armar: nada borrado, fallo; Reintentar arma y termina
    sim_reset_fail(0, true);
    flex_factory_reset_open(false);
    sim_run(200);
    sim_tap(351, 733);
    sim_run(300);
    type_word("RESTABLECER");
    sim_tap(240, 259);
    sim_run(300);
    slide(46 + 30 + 330);
    sim_run(800);
    CHK(flex_factory_view() == V_FAIL);
    CHK(runs_total() == 0);
    sim_reset_fail(0, false);
    sim_tap(240, 657);
    sim_run(100);
    run_until_end();
    CHK(flex_factory_shown_stage() == FLEX_FR_DONE && runs_total() == FLEX_FR_STEPS);
    sim_run(1400);
    flex_factory_sim_reset();
    sim_reset_clear();
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // 8) con clave: la pide; cancelarla sale del asistente; acertarla lleva a deslizar
    CHK(flex_lock_set("1470", FLEX_LOCK_PIN));
    flex_factory_reset_open(false);
    sim_run(200);
    sim_tap(351, 733);
    sim_run(500);
    CHK(flex_auth_active());
    sim_tap(24, 20);
    sim_run(400);
    CHK(!flex_auth_active() && !flex_factory_active());
    CHK(flex_shell_state() == FLEX_SH_HOME);
    flex_factory_reset_open(false);
    sim_run(200);
    sim_tap(351, 733);
    sim_run(500);
    pin("1470");
    CHK(flex_factory_view() == V_SLIDE && flex_shell_state() == FLEX_SH_FACTORY);
    sim_tap(240, 733);
    sim_run(300);
    CHK(!flex_factory_active());
    flex_lock_clear();

    // 9) un corte a mitad: el arranque retoma en la etapa anotada y termina
    flex_fr_marker_t cut = {true, FLEX_FR_VER, FLEX_FR_NVS, 0};
    sim_reset_set_disk(&cut);
    CHK(flex_reset_boot_check());
    flex_factory_resume_boot();   // lo que hace flex_shell_start
    sim_run(100);
    CHK(flex_factory_view() == V_RUN && flex_shell_state() == FLEX_SH_FACTORY);
    run_until_end();
    CHK(flex_factory_shown_stage() == FLEX_FR_DONE);
    CHK(sim_reset_runs(FLEX_FR_FILES) == 0 && sim_reset_runs(FLEX_FR_NVS) == 1 && sim_reset_runs(FLEX_FR_DEFAULTS) == 1);
    sim_run(1400);
    flex_factory_sim_reset();
    sim_reset_clear();

    // 10) desde el Modo seguro (fila 7) y Cancelar vuelve al Modo seguro
    sim_safe_set(FLEX_RST_PANIC, 2);
    flex_safe_open();
    sim_run(200);
    sim_tap(240, 292 + 6 * 66 + 29);
    sim_run(300);
    CHK(flex_factory_view() == V_INTRO);
    sim_tap(129, 733);
    sim_run(300);
    CHK(!flex_factory_active() && flex_safe_screen_active());
    flex_safe_close_now();
    sim_safe_clear();

    flex_shell_lock();
    sim_run(200);
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);
    printf("fabrica: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
