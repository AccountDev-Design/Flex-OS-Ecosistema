// Calculadora (03 §3): la app real en el simulador. Teclas con el dedo, linea de operacion, encadenado,
// "Error", sesion de disco (la primera apertura tras el arranque la recupera; las demas empiezan en 0) y
// su estado de interfaz en Recientes.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_calc_model.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void flex_app_calc_line(char *out, size_t n);
const char *flex_app_calc_display_text(void);
static bool g_disp_ok = true;
void flex_session_forget_loaded(int id);
void flex_session_test_preload(int id, const uint8_t *file, size_t len);
void sim_fs_enable(bool on);
size_t sim_fs_get(const char *p, uint8_t *out, size_t cap);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("calculadora: FALLO linea %d: %s\n", __LINE__, #c); \
            ok = false;                                              \
        }                                                            \
    } while (0)

// centro de la tecla (fila r, columna c) a 480x800 con la barra de botones: caja (0,96)-(480,736)
static void tap_key(int r, int c)
{
    int m = 16, gap = 12, dh = 120, kw = (480 - 2 * m - 3 * gap) / 4, kh = ((736 - 96 - 2 * m - dh - m) - 4 * gap) / 5;
    int x = m + c * (kw + gap) + kw / 2, y = 96 + m + dh + m + r * (kh + gap) + kh / 2;
    sim_tap(x, y);
    sim_run(60);
}
static const char *line(void)
{
    static char l[56];
    flex_app_calc_line(l, sizeof(l));
    if (strcmp(l, flex_app_calc_display_text()) != 0) {   // el display de verdad dice lo mismo que el estado
        g_disp_ok = false;
    }
    return l;
}
// posiciones de las teclas por su etiqueta
static void press(const char *lbl)
{
    static const char *const L[5][4] = {{"C", "+/-", "%", "/"}, {"7", "8", "9", "x"}, {"4", "5", "6", "-"}, {"1", "2", "3", "+"}, {"0", ".", "=", "DEL"}};
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++)
            if (!strcmp(L[r][c], lbl)) { tap_key(r, c); return; }
}

bool scene_calc_run(void)
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
    sim_fs_enable(true);
    flex_session_forget_loaded(IC_CALC);

    // 1) abre en "0" (sin sesion); 25 + 10 se ve como se escribe y = da 35
    flex_app_open(IC_CALC, NULL);
    sim_run(600);
    CHK(strcmp(line(), "0") == 0);
    press("2"); press("5");
    press("+");
    CHK(strcmp(line(), "25 +") == 0);
    press("1"); press("0");
    CHK(strcmp(line(), "25 + 10") == 0);
    sim_shot("calc_25_mas_10");
    press("=");
    CHK(strcmp(line(), "35") == 0);

    // 2) encadenado: 2 + 3 + 4 = 9; cambio de operador; porcentaje; signo; DEL
    press("C");
    press("2"); press("+"); press("3"); press("+"); press("4"); press("=");
    CHK(strcmp(line(), "9") == 0);
    press("C");
    press("8"); press("+"); press("-"); press("2");
    CHK(strcmp(line(), "8 - 2") == 0);
    press("="); press("%");
    CHK(strcmp(line(), "0.06") == 0);
    press("C"); press("7"); press("+/-");
    CHK(strcmp(line(), "-7") == 0);
    press("4"); press("DEL");
    CHK(strcmp(line(), "-7") == 0);

    // 3) division por cero: "Error" y solo C, un digito o el punto lo limpian
    press("C");
    press("5"); press("/"); press("0"); press("=");
    CHK(strcmp(line(), "Error") == 0);
    sim_shot("calc_error");
    press("+");
    CHK(strcmp(line(), "Error") == 0);
    press("9");
    CHK(strcmp(line(), "9") == 0);

    // 4) la operacion visible se guarda 1200 ms despues y la primera apertura tras el "arranque" la recupera
    press("C");
    press("1"); press("2"); press("x"); press("3");
    sim_run(1500);
    uint8_t file[64];
    size_t n = sim_fs_get("/System/Sessions/calc.bin", file, sizeof(file));
    CHK(n == 16 + sizeof(flex_calc_sess_t));
    flex_sys_home();
    sim_run(300);
    flex_app_terminate_ex(IC_CALC, true);
    // otra apertura de la misma ejecucion: empieza en "0" (solo la primera tras el arranque recupera)
    flex_app_open(IC_CALC, NULL);
    sim_run(500);
    CHK(strcmp(line(), "0") == 0);
    flex_sys_home();
    sim_run(300);
    flex_app_terminate_ex(IC_CALC, true);
    // "arranque": se vuelve a leer el archivo y la primera apertura recupera "12 x 3"
    flex_session_forget_loaded(IC_CALC);
    flex_session_test_preload(IC_CALC, file, n);
    flex_app_open(IC_CALC, NULL);
    sim_run(500);
    CHK(strcmp(line(), "12 x 3") == 0);
    press("="); 
    CHK(strcmp(line(), "36") == 0);

    flex_sys_home();
    sim_run(300);
    for (int i = 0; i < FLEX_APP_N; i++) flex_app_terminate_ex(i, true);
    sim_fs_enable(false);
    flex_shell_lock();
    sim_run(200);
    CHK(g_disp_ok);
    printf("calculadora: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
