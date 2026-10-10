// Widgets del escritorio como wgDrawCell (Widgets.h:186): esfera con agujas,
// rotulos, barra de almacenamiento, antena sin red, Clima de una fila con su
// material; datos que cambian EN SU SITIO cada 2 s; solo Camara, Clima y
// Calendario responden a un toque (el cronometro es informativo).
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "flex_clock.h"
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
            printf("widgets: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

void sim_crono_set(uint32_t ms, bool running);

// Etiqueta visible cuyo texto contiene txt (exacto si exact)
static lv_obj_t *find_text(lv_obj_t *o, const char *txt, bool exact)
{
    if (lv_obj_is_hidden(o)) {
        return NULL;
    }
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *s = lv_label_get_text(o);
        if (exact ? strcmp(s, txt) == 0 : strstr(s, txt) != NULL) {
            return o;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_text(lv_obj_get_child(o, (int32_t)i), txt, exact);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static int count_lines(lv_obj_t *o)
{
    int n = lv_obj_check_type(o, &lv_line_class) ? 1 : 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        n += count_lines(lv_obj_get_child(o, (int32_t)i));
    }
    return n;
}

static void put(int p, int k, int type, int c, int r, int w, int h)
{
    g_home.wg[p][k] = (flex_home_wg_t){(uint8_t)type, (uint8_t)c, (uint8_t)r, (uint8_t)w, (uint8_t)h};
    if (g_home.wg_n[p] < k + 1) {
        g_home.wg_n[p] = (uint8_t)(k + 1);
    }
}

static void show_page(int p)
{
    g_home.page = p;
    flex_home_rebuild();
    sim_run(300);
}

// Centro en pantalla del widget k de la pagina p
static void wg_center(int p, int k, int *x, int *y)
{
    int wx, wy, ww, wh;
    flex_home_wg_rect(&g_home.wg[p][k], &wx, &wy, &ww, &wh);
    *x = wx + ww / 2;
    *y = wy + wh / 2;
}

bool scene_widgets_run(void)
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

    flex_home_t home0 = g_home;
    memset(g_home.wg, 0, sizeof(g_home.wg));
    memset(g_home.wg_n, 0, sizeof(g_home.wg_n));
    g_home.page_n = 3;
    put(0, 0, FLEX_WG_CLOCK_A, 0, 0, 2, 1);
    put(0, 1, FLEX_WG_DATE, 2, 0, 2, 1);
    put(0, 2, FLEX_WG_MEM, 0, 1, 2, 1);
    put(0, 3, FLEX_WG_STORAGE, 2, 1, 2, 1);
    put(0, 4, FLEX_WG_WIFI, 0, 2, 1, 1);
    put(0, 5, FLEX_WG_CAM, 1, 2, 1, 1);
    put(1, 0, FLEX_WG_CLIMA, 0, 0, 2, 1);   // una fila: material propio de Clima
    put(1, 1, FLEX_WG_CLOCK, 2, 0, 2, 1);
    put(1, 2, FLEX_WG_CRONO, 0, 1, 2, 1);
    put(1, 3, FLEX_WG_CALEND, 2, 1, 2, 2);
    put(2, 0, FLEX_WG_CLIMA, 0, 1, 2, 2);   // dos filas: tarjeta
    flex_home_normalize();
    CHK(g_home.wg_n[0] == 6 && g_home.wg_n[1] == 4 && g_home.wg_n[2] == 1);
    struct tm t0;
    flex_clock_now(&t0);
    uint32_t utc0 = (uint32_t)(timegm(&t0) - FLEX_TZ_OFFSET_SEC), tick0 = lv_tick_get();
    flex_clock_set_utc(1783177680u);   // 4 jul 2026, 10:08 en Lima (la semilla menos 3 h 15 min)
    sim_crono_set(0, false);
    show_page(0);
    sim_run(1200);   // el cambio de minuto (hora puesta) rehace las paginas una vez
    lv_obj_t *scr = lv_screen_active();

    // 1) composicion de Arduino: rotulos, esfera con dos agujas, antena sin red
    CHK(find_text(scr, "Fecha", true) && find_text(scr, "Memoria", true));
    CHK(find_text(scr, "Almacenamiento", true) && find_text(scr, "Sin red", true));
    CHK(find_text(scr, "C\xC3\xA1mara", true) && find_text(scr, "KB libres", false));
    CHK(find_text(scr, "Reloj", true) && find_text(scr, "Cron\xC3\xB3metro", true));
    CHK(find_text(scr, "A\xC3\xB1" "adir ubicaci\xC3\xB3n", true));   // Clima de una fila, sin ubicaciones
    CHK(!find_text(scr, "Sin conexi\xC3\xB3n", true));
    CHK(count_lines(scr) == 2);   // agujas del reloj analogico (sin red: la antena es solo un punto)
    sim_shot("wg_01_pagina1");

    // 2) los datos cambian en su sitio (sin rehacer la pagina): el cronometro corre
    lv_obj_t *cro = find_text(scr, "00:00", true);
    CHK(cro != NULL);
    sim_crono_set(65000, true);
    sim_run(2100);
    CHK(find_text(scr, "01:05", true) == cro);   // el mismo objeto: no se rehizo
    CHK(find_text(scr, "Cron\xC3\xB3metro en marcha", true));

    // 3) el cronometro no es un boton; Camara si
    show_page(1);
    sim_shot("wg_02_pagina2");
    int x, y;
    wg_center(1, 2, &x, &y);
    sim_tap(x, y);
    sim_run(600);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    show_page(0);
    wg_center(0, 5, &x, &y);
    sim_tap(x, y);
    sim_run(800);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_app_current() == IC_CAMARA);
    flex_sys_home();
    sim_run(800);

    show_page(2);
    sim_shot("wg_03_pagina3");
    flex_theme_set_glass(false);
    flex_theme_set_dark(false);
    show_page(0);
    sim_shot("wg_04_plano_claro");
    flex_theme_set_glass(true);
    flex_theme_set_dark(true);

    sim_crono_set(0, false);
    flex_clock_set_utc(utc0 + (lv_tick_get() - tick0) / 1000u);
    g_home = home0;
    flex_home_save();
    flex_home_rebuild();
    flex_shell_lock();
    sim_run(200);
    printf("widgets: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
