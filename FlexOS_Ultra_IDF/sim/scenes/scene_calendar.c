// Calendario (03 §4): el mes de la semilla de fabrica (sabado 4 de julio de 2026), panel lateral "Hoy", cambio
// de dia por el servicio de hora, reanudar, idioma del mes y limites de la rejilla (31 dias, 5 filas).
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_clock.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

int flex_app_calendar_today(void);
const char *flex_app_calendar_header(void);
int flex_app_calendar_dots(void);
void flex_app_calendar_stats(int *cells, int *side, int *first, int *rows);

#define CHK(c)                                                         \
    do {                                                               \
        if (!(c)) {                                                    \
            printf("calendario: FALLO linea %d: %s\n", __LINE__, #c);  \
            ok = false;                                                \
        }                                                              \
    } while (0)

bool scene_calendar_run(void)
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
    sim_run(400);
    for (int i = 0; i < FLEX_APP_N; i++) flex_app_terminate_ex(i, true);
    int lang0 = flex_lang();
    flex_set_lang(0);
    flex_clock_set_utc(1783177680u);   // sabado 4 de julio de 2026, 10:08 en Lima
    sim_run(100);

    // 1) julio de 2026: el dia 1 es miercoles (3), 31 dias, 5 filas y panel lateral a pantalla completa
    flex_app_open(IC_CALEND, NULL);
    sim_run(600);
    int cells, side, first, rows;
    flex_app_calendar_stats(&cells, &side, &first, &rows);
    CHK(strcmp(flex_app_calendar_header(), "julio 2026") == 0);
    CHK(flex_app_calendar_today() == 4 && cells == 31 && first == 3 && rows == 5 && side == 1);
    CHK(flex_app_calendar_dots() == 1);                // un solo dia de hoy resaltado
    sim_shot("calendario_julio");

    // 2) cambia el idioma: el mes sale en el idioma (al reanudar se repinta)
    flex_sys_home();
    sim_run(300);
    flex_set_lang(1);
    flex_app_open(IC_CALEND, NULL);
    sim_run(500);
    CHK(strcmp(flex_app_calendar_header(), "July 2026") == 0);
    sim_shot("calendario_july");
    flex_set_lang(0);

    // 3) cambio de dia con la app delante: 31 de julio 23:59 -> 1 de agosto (el servicio de hora avisa por minuto)
    // 31 jul 2026 23:59:30 hora de Lima (UTC-5) = 1 ago 04:59:30 UTC
    flex_clock_set_utc(1785560370);   // 1 ago 2026 04:59:30 UTC
    sim_run(1500);
    CHK(flex_app_calendar_today() == 31);              // aun es 31 de julio
    flex_clock_set_utc(1785560370 + 40);               // el reloj pasa de las 00:00 de Lima: 1 de agosto
    sim_run(1500);                                     // el minuto cambia dentro de la app
    CHK(flex_app_calendar_today() == 1);
    CHK(strcmp(flex_app_calendar_header(), "agosto 2026") == 0);
    flex_app_calendar_stats(&cells, &side, &first, &rows);
    CHK(cells == 31 && first == 6 && rows == 6);   // el 1 de agosto de 2026 es sabado: seis filas
    sim_shot("calendario_agosto_seis_filas");

    // 4) febrero bisiesto de 2028: 29 dias, empieza en martes (2) -> 5 filas
    // 29 feb 2028 12:00 Lima = 17:00 UTC
    flex_clock_set_utc(1835456400);
    flex_sys_home();
    sim_run(200);
    flex_app_open(IC_CALEND, NULL);
    sim_run(500);
    flex_app_calendar_stats(&cells, &side, &first, &rows);
    CHK(strcmp(flex_app_calendar_header(), "febrero 2028") == 0 && cells == 29 && flex_app_calendar_today() == 29);
    CHK(first == 2 && rows == 5);

    flex_set_lang(lang0);
    flex_sys_home();
    sim_run(300);
    for (int i = 0; i < FLEX_APP_N; i++) flex_app_terminate_ex(i, true);
    flex_shell_lock();
    sim_run(200);
    printf("calendario: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
