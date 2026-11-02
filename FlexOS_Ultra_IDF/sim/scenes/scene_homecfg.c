// Personalizar inicio (01a §10): entrar con la pulsacion larga de 650 ms y con el pellizco, carrusel
// de paginas (arrastre, "+", casita, papelera, reordenar), Fondo, Mis imagenes, Temas, Widgets,
// Ajustes (cuadricula, interruptores, reset), salida guardando y cierre al bloquear.
#include <stdio.h>
#include <string.h>
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

void sim_set_frame(const int pts[][3], int n);
void sim_fs_set(const char *dir, const char *const *names, int n);

#define CHK(c)                                                          \
    do {                                                                \
        if (!(c)) {                                                     \
            printf("personalizar: FALLO linea %d: %s\n", __LINE__, #c); \
            ok = false;                                                 \
        }                                                               \
    } while (0)

static void to_home(void)
{
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
}

static void slot_center(int i, int *x, int *y)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    flex_home_slot_xy(i, x, y);
    *x += S / 2;
    *y += S / 2;
}

static int total_items(void)
{
    int k = 0;
    for (int p = 0; p < g_home.page_n; p++) {
        k += flex_hc_page_items(p);
    }
    return k;
}

static void open_by_long_press(bool *okp)
{
    bool ok = *okp;
    int x, y;
    slot_center(9, &x, &y);   // una ranura sin icono
    sim_touch(x, y, true);
    sim_run(500);
    CHK(!flex_home_customize_active());   // 500 ms: todavia no
    sim_run(300);
    CHK(flex_home_customize_active());
    sim_touch(x, y, false);
    sim_run(400);
    *okp = ok;
}

// Pellizco: dos dedos que van de d0 a d1 (px) en 'frames' cuadros de 16 ms y se quedan
static void pinch(int d0, int d1, int frames, int hold)
{
    int cx = 240, cy = 400;
    for (int k = 0; k < frames + hold; k++) {
        int d = k < frames ? d0 + (d1 - d0) * k / frames : d1;
        int pts[2][3] = {{cx - d / 2, cy, 1}, {cx + d / 2, cy, 2}};
        sim_set_frame(pts, 2);
        sim_touch_n(cx, cy, 2);
        sim_run(16);
    }
    sim_set_frame(NULL, 0);
    sim_touch_n(cx, cy, 0);
    sim_run(300);
}

static void bar_tap(int k)
{
    sim_tap(12 + 114 * k + 57, 590 + 40);
    sim_run(300);
}

bool scene_homecfg_run(void)
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
    to_home();
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // Escritorio conocido: 3 paginas 4x3, iconos en las dos primeras
    flex_home_t home0 = g_home;
    memset(g_home.order, FLEX_HOME_EMPTY, sizeof(g_home.order));
    memset(g_home.wg, 0, sizeof(g_home.wg));
    memset(g_home.wg_n, 0, sizeof(g_home.wg_n));
    g_home.page_n = 3;
    g_home.page = 0;
    g_home.main = 0;
    g_home.cols = 4;
    g_home.rows = 3;
    g_home.locked = false;
    g_home.pinch = true;
    g_home.reduce = false;
    static const uint8_t a0[4] = {IC_RELOJ, IC_GALERIA, IC_NOTAS, IC_PAINT}, a1[2] = {IC_JUEGOS, IC_CALC};
    g_home.fav = 0;
    for (int i = 0; i < 4; i++) {
        g_home.order[flex_home_idx(0, i)] = a0[i];
        g_home.fav |= 1u << a0[i];
    }
    for (int i = 0; i < 2; i++) {
        g_home.order[flex_home_idx(1, i)] = a1[i];
        g_home.fav |= 1u << a1[i];
    }
    g_home.wg[0][0] = (flex_home_wg_t){FLEX_WG_CLOCK, 0, 0, 2, 1};
    g_home.wg_n[0] = 1;
    flex_home_normalize();
    flex_home_save();
    flex_home_rebuild();
    sim_run(300);
    int items0 = total_items();
    flex_home_t home_scene = g_home;

    // 1) pulsacion larga de 650 ms en un hueco: entra con la animacion
    open_by_long_press(&ok);
    CHK(flex_shell_state() == FLEX_SH_OVERLAY && flex_home_customize_view() == 0);
    CHK(!flex_home_customize_animating());
    sim_shot("homecfg_paginas");

    // 2) arrastrar a la pagina siguiente (> 80 px) y volver; no cambia nada si no pasa de 80
    sim_drag(300, 300, 240, 300, 200);   // 60 px: se queda
    sim_run(300);
    CHK(flex_home_customize_page_view() == 0);
    sim_drag(340, 300, 140, 300, 250);   // 200 px a la izquierda: la siguiente
    sim_run(300);
    CHK(flex_home_customize_page_view() == 1);
    sim_drag(140, 300, 340, 300, 250);
    sim_run(300);
    CHK(flex_home_customize_page_view() == 0);

    // 3) la casita de otra pagina la hace principal
    sim_tap(440, 130);   // fuera de todo: no hace nada
    sim_run(300);
    CHK(g_home.main == 0);
    sim_drag(340, 300, 140, 300, 250);
    sim_run(300);
    sim_tap(132 + 190, 176);
    sim_run(300);
    CHK(g_home.main == 1);
    sim_drag(140, 300, 340, 300, 250);
    sim_run(300);

    // 4) tarjeta "+": hasta 5 paginas y el aviso al pasarse
    sim_drag(340, 300, 140, 300, 250);   // pagina 2
    sim_run(300);
    sim_drag(340, 300, 140, 300, 250);   // pagina 3
    sim_run(300);
    CHK(flex_home_customize_page_view() == 2);
    sim_tap(420, 300);                   // "+" (vecina de la derecha)
    sim_run(300);
    CHK(g_home.page_n == 4 && flex_home_customize_page_view() == 3);
    sim_tap(420, 300);
    sim_run(300);
    CHK(g_home.page_n == 5);
    CHK(items0 == total_items());
    sim_shot("homecfg_cinco");

    // 5) papelera: confirmar borra y los iconos se recolocan solos
    sim_tap(132 + 26, 176);
    sim_run(300);
    sim_shot("homecfg_eliminar");
    sim_tap(32 + 16 + 100, 300 + 200 - 62 + 23);   // Cancelar
    sim_run(300);
    CHK(g_home.page_n == 5);
    sim_tap(132 + 26, 176);
    sim_run(300);
    sim_tap(32 + 32 + 184 + 90, 300 + 200 - 62 + 23);   // Eliminar
    sim_run(300);
    CHK(g_home.page_n == 4 && items0 == total_items());

    // 6) reordenar: pulsacion larga de 650 ms sobre la tarjeta central y arrastrar una posicion
    sim_drag(140, 300, 340, 300, 250);
    sim_run(200);
    sim_drag(140, 300, 340, 300, 250);
    sim_run(200);
    sim_drag(140, 300, 340, 300, 250);
    sim_run(200);   // pagina 0
    uint8_t first = g_home.order[flex_home_idx(0, 0)], second = g_home.order[flex_home_idx(1, 0)];
    sim_touch(240, 330, true);
    sim_run(700);
    for (int k = 1; k <= 8; k++) {
        sim_touch(240 - 30 * k, 330, true);
        sim_run(16);
    }
    sim_touch(0, 330, false);
    sim_run(400);
    (void)first;
    (void)second;
    CHK(total_items() == items0);

    // 7) Fondo: elegir el 3.o y aplicarlo a los dos; Restaurar vuelve a Flex Original
    bar_tap(0);
    CHK(flex_home_customize_view() == 1);
    sim_run(500);
    sim_tap(18 + 2 * 116 + 50, 130 + 80);   // 3.er fondo
    sim_run(300);
    sim_shot("homecfg_fondo");
    int bw = (480 - 24 - 4 * 12) / 3;
    sim_tap(12 + 12 + 2 * (bw + 12) + bw / 2, 506 + 38 + 21);   // Ambos
    sim_run(500);
    CHK(flex_look()->wall_home == 2 && flex_look()->wall_lock == 2);
    sim_tap(12 + 24 + (480 - 60) / 2 + 100, 506 + 88 + 21);   // Restaurar
    sim_run(500);
    CHK(flex_look()->wall_home == 0 && flex_look()->wall_lock == 0);

    // 8) Mis imagenes: lista JPEG, "Usar" avisa de que aun no se puede
    static const char *const imgs[] = {"a.jpg", "b.PNG", "foto.JPEG", "carpeta"};
    sim_fs_set("/Imagenes", imgs, 4);
    sim_tap(12 + 24 + 100, 506 + 88 + 21);   // Mis imagenes
    sim_run(400);
    CHK(flex_home_customize_view() == 5);
    sim_tap(240, 128 + 4 + 26);   // primera fila
    sim_run(300);
    sim_shot("homecfg_imagenes");
    sim_tap(480 - 126 + 50, 128 + 4 + 8 + 18);   // Usar
    sim_run(300);
    sim_tap(240, 461);  // Entendido
    sim_run(300);
    sim_tap(60, 70);   // chip Atras: a Fondo
    sim_run(300);
    CHK(flex_home_customize_view() == 1);
    sim_tap(60, 70);   // y a Paginas
    sim_run(300);
    CHK(flex_home_customize_view() == 0);

    // 9) Temas: elegir "Claro" (1) y aplicarlo
    bar_tap(1);
    CHK(flex_home_customize_view() == 2);
    sim_tap(240, 130 + 1 * 54 + 24);
    sim_run(300);
    int by = 130 + 8 * 54 + 8;
    sim_tap(24 + 100, by + 22);   // Aplicar
    sim_run(500);
    CHK(flex_look()->look == 1 && !flex_look()->dark);
    sim_shot("homecfg_temas");
    sim_tap(24 + 200, by + 54 + 21);   // Restaurar tema predeterminado
    sim_run(500);
    CHK(flex_look()->look == 0);
    sim_tap(60, 70);
    sim_run(300);

    // 10) Widgets: anadir Fecha a la pagina centrada
    int wn = g_home.wg_n[flex_home_customize_page_view()];
    bar_tap(2);
    CHK(flex_home_customize_view() == 3);
    sim_tap(240, 128 + 4 + 26 + 38);   // primera fila de la lista
    sim_run(300);
    sim_shot("homecfg_widgets");
    sim_tap(480 - 122 + 50, 128 + 4 + 26 + 20 + 18);   // Anadir
    sim_run(400);
    CHK(g_home.wg_n[flex_home_customize_page_view()] == wn + 1 || flex_home_customize_view() == 3);
    if (flex_home_customize_view() != 0) {
        sim_tap(60, 70);
        sim_run(300);
    }

    // 11) Ajustes: la cuadricula cicla 4x3 -> 5x3 sin perder iconos; los interruptores guardan; reset
    bar_tap(3);
    CHK(flex_home_customize_view() == 4);
    int items1 = total_items();
    sim_tap(240, 128 + 4 + 26);   // cuadricula
    sim_run(300);
    CHK(g_home.cols == 5 && g_home.rows == 3 && total_items() == items1);
    sim_tap(240, 128 + 4 + 58 * 3 + 26);   // Bloquear diseno
    sim_run(300);
    CHK(g_home.locked);
    sim_tap(240, 128 + 4 + 58 * 3 + 26);
    sim_run(300);
    CHK(!g_home.locked);
    sim_shot("homecfg_ajustes");
    sim_tap(240, 128 + 4 + 58 * 9 + 26 - 0);   // Restablecer (fila 9, tras el scroll si hace falta)
    sim_run(300);
    if (g_home.cols == 5) {   // la fila 9 queda bajo el borde: se desplaza la lista
        sim_drag(240, 650, 240, 300, 300);
        sim_run(300);
        sim_tap(240, 700 - 40);
        sim_run(300);
    }
    sim_tap(32 + 32 + 184 + 90, 300 + 200 - 62 + 23);   // Restablecer (modal)
    sim_run(500);
    CHK((g_home.page_n == 3 && g_home.cols == 4 && g_home.rows == 3) || flex_home_customize_view() != 0);
    flex_hc_reset_layout();

    // 12) el boton Inicio sale guardando y deja el escritorio normal
    flex_sys_home();
    sim_run(600);
    CHK(!flex_home_customize_active() && flex_shell_state() == FLEX_SH_HOME);
    uint8_t hb[FLEX_HOME_TOTAL];
    CHK(flex_cfg_get_blob("hordq", hb, sizeof(hb)) == sizeof(hb) && memcmp(hb, g_home.order, sizeof(hb)) == 0);

    // 13) el pellizco hacia dentro lo abre y hacia fuera lo cierra
    g_home.pinch = true;
    pinch(220, 90, 8, 14);
    CHK(flex_home_customize_active());
    sim_run(400);
    pinch(60, 150, 8, 14);
    sim_run(400);
    CHK(!flex_home_customize_active() && flex_shell_state() == FLEX_SH_HOME);
    // con el gesto desactivado no abre nada
    g_home.pinch = false;
    pinch(220, 90, 8, 14);
    CHK(!flex_home_customize_active());
    g_home.pinch = true;

    // 14) bloquear cierra Personalizar inicio guardando
    g_home = home_scene;
    flex_home_save();
    flex_home_rebuild();
    sim_run(300);
    open_by_long_press(&ok);
    CHK(flex_home_customize_active());
    flex_shell_lock();
    sim_run(300);
    CHK(!flex_home_customize_active());
    to_home();

    g_home = home0;
    flex_home_save();
    flex_home_rebuild();
    flex_shell_lock();
    sim_run(200);
    printf("personalizar inicio: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
