// Panel rapido: abrir desde el borde superior (sigue al dedo) y el derecho,
// tocar sin mover, controles reales (tema, vidrio, modo avion, brillo), el asa
// de la tarjeta, el editor (quitar, redimensionar, Cancelar/Listo,
// Restablecer), el catalogo, abrir una app desde el panel y los cierres forzados.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_display.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_qs_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("panel: FALLO linea %d: %s\n", __LINE__, #c);     \
            ok = false;                                              \
        }                                                            \
    } while (0)

static bool visible(int id)
{
    uint8_t ids[32];
    int n = flex_qs_visible_ids(ids, 32);
    for (int i = 0; i < n; i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static void tap_ctl(int id)
{
    lv_area_t a;
    if (flex_qs_ctl_rect(id, &a)) {
        sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
        sim_run(250);
    }
}

static void open_top(void)
{
    sim_drag(240, 10, 240, 520, 260);   // >= 40 % del recorrido: abre del todo
    sim_run(400);
}

static int cfg_count(void)
{
    uint8_t b[FLEX_QS_BLOB_N];
    if (flex_kvs_get_blob(FLEX_QS_NVS_NS, FLEX_QS_NVS_KEY, b, sizeof(b)) != FLEX_QS_BLOB_N) {
        return -1;
    }
    return b[2];
}

static bool cfg_has(int id)
{
    uint8_t b[FLEX_QS_BLOB_N];
    if (flex_kvs_get_blob(FLEX_QS_NVS_NS, FLEX_QS_NVS_KEY, b, sizeof(b)) != FLEX_QS_BLOB_N) {
        return false;
    }
    for (int i = 0; i < b[2]; i++) {
        if (b[4 + i * 5] == id) {
            return true;
        }
    }
    return false;
}

static uint32_t obj_count(lv_obj_t *o)
{
    uint32_t n = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        n += obj_count(lv_obj_get_child(o, (int32_t)i));
    }
    return n;
}

// Controles que se ven en el modo actual del panel (en el editor, los de la edicion).
static int shown_now(int *first)
{
    int n = 0;
    lv_area_t a;
    *first = -1;
    for (int id = 0; id < FLEX_QS_COUNT; id++) {
        if (flex_qs_ctl_rect(id, &a)) {
            if (*first < 0) {
                *first = id;
            }
            n++;
        }
    }
    return n;
}

bool scene_qs_run(void)
{
    bool ok = true;
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_cfg_set_bool("airpl", false);
    flex_lock_clear();
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
    sim_drag(240, 700, 240, 300, 300);   // sin clave: al escritorio
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // 1) borde superior: la cortina sigue al dedo y, soltando a mas del 40 %, abre
    sim_touch(240, 8, true);
    sim_run(40);
    for (int y = 8; y <= 300; y += 20) {
        sim_touch(240, y, true);
        sim_run(16);
    }
    CHK(flex_qs_is_open());
    CHK(flex_qs_panel_y() > 250 && flex_qs_panel_y() < 320);   // 1:1 con el dedo
    sim_shot("qs_01_arrastre");
    sim_run(300);   // quieto: sin velocidad de lanzamiento
    sim_touch(240, 300, false);
    sim_run(400);
    CHK(flex_qs_panel_y() == 0);   // 300 < 320 (40 %) sin lanzamiento: se cierra
    open_top();
    CHK(flex_qs_panel_y() == 800);
    CHK(flex_shell_state() == FLEX_SH_HOME);   // capa global: el estado no cambia
    sim_shot("qs_02_panel_vidrio");

    // 2) NADA FALSO: lo que se ve tiene backend; lo pendiente no aparece
    CHK(visible(FLEX_QS_AIRPLANE) && visible(FLEX_QS_BRIGHT) && visible(FLEX_QS_THEME));
    CHK(visible(FLEX_QS_GLASS) && visible(FLEX_QS_SETTINGS) && visible(FLEX_QS_CAMERA));
    CHK(!visible(FLEX_QS_WIFI) && !visible(FLEX_QS_BLE) && !visible(FLEX_QS_VOLUME) && !visible(FLEX_QS_OTA));
    CHK(!visible(FLEX_QS_LOCK));   // sin clave no hay "Bloquear"

    // 3) controles reales
    tap_ctl(FLEX_QS_THEME);
    CHK(!flex_look()->dark);
    sim_shot("qs_03_claro");
    tap_ctl(FLEX_QS_THEME);
    CHK(flex_look()->dark);
    tap_ctl(FLEX_QS_AIRPLANE);
    CHK(flex_cfg_get_bool("airpl", false));
    tap_ctl(FLEX_QS_AIRPLANE);
    CHK(!flex_cfg_get_bool("airpl", true));
    lv_area_t br;
    CHK(flex_qs_ctl_rect(FLEX_QS_BRIGHT, &br));
    sim_drag(br.x1 + 30, (br.y1 + br.y2) / 2, br.x1 + 40 + (br.x2 - br.x1) / 4, (br.y1 + br.y2) / 2, 200);
    sim_run(100);
    CHK(flex_display_get_brightness() < 40);
    CHK(flex_cfg_get_i32("bright", 0) == flex_display_get_brightness());
    sim_drag(br.x1 + 60, (br.y1 + br.y2) / 2, br.x2 - 5, (br.y1 + br.y2) / 2, 200);
    sim_run(100);
    CHK(flex_display_get_brightness() == 100);
    CHK(flex_qs_panel_y() == 800);   // el deslizador no mueve la cortina

    // 4) Plano: la cortina es la superficie solida de la paleta
    tap_ctl(FLEX_QS_GLASS);
    CHK(!flex_look()->glass);
    CHK(!visible(FLEX_QS_GLASSFX));
    sim_shot("qs_04_plano");
    tap_ctl(FLEX_QS_GLASS);
    CHK(flex_look()->glass);

    // 5) un toque en el vacio no cierra; el asa inferior si
    sim_tap(240, 700);
    sim_run(200);
    CHK(flex_qs_panel_y() == 800);
    sim_tap(240, 785);
    sim_run(500);
    CHK(!flex_qs_is_open());

    // 6) toque sin mover en el borde superior: se traga y no abre
    sim_tap(240, 10);
    sim_run(300);
    CHK(!flex_qs_is_open());

    // 6b) dos dedos sobre un control NO es un toque (el arbitraje se traga el
    // episodio para el gesto de suspender; Arduino ejecutaba el control)
    open_top();
    {
        lv_area_t th;
        CHK(flex_qs_ctl_rect(FLEX_QS_THEME, &th));
        int cx = (th.x1 + th.x2) / 2, cy = (th.y1 + th.y2) / 2;
        bool dark0 = flex_look()->dark;
        sim_touch(cx, cy, true);
        sim_run(40);
        for (int k = 0; k < 8; k++) {
            sim_touch_n(cx, cy, 2);
            sim_run(16);
        }
        sim_touch_n(cx, cy, 0);
        sim_run(1200);   // pasa la ventana del doble toque: no suspende
        CHK(flex_look()->dark == dark0);
        CHK(flex_qs_panel_y() == 800);
        CHK(!flex_power_suspended());
    }
    flex_qs_close_now();
    sim_run(100);

    // 7) editor: Cancelar no guarda nada
    open_top();
    int n0 = cfg_count();
    sim_tap(338, 50);   // lapiz
    sim_run(200);
    CHK(flex_qs_mode() == 1);
    sim_shot("qs_05_editor");
    lv_area_t a;
    CHK(flex_qs_ctl_rect(FLEX_QS_CAMERA, &a));
    sim_tap(a.x1 + 4, a.y1 + 4);   // "-" del circulo
    sim_run(200);
    CHK(!flex_qs_ctl_rect(FLEX_QS_CAMERA, &a));
    sim_tap(60, 28);   // Cancelar
    sim_run(200);
    CHK(flex_qs_mode() == 0);
    CHK(visible(FLEX_QS_CAMERA));
    CHK(cfg_count() == n0);

    // 7b) mover un control (mantener y arrastrar) no deja fantasmas al cancelar
    {
        uint32_t objs0 = obj_count(lv_layer_top());
        sim_tap(338, 50);
        sim_run(200);
        CHK(flex_qs_ctl_rect(FLEX_QS_CAMERA, &a));
        int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
        sim_touch(cx, cy, true);
        sim_run(450);   // > 320 ms: se levanta
        int ex = cx - 240 < 12 ? 12 : cx - 240;
        for (int x = cx; x >= ex; x -= 10) {
            sim_touch(x, cy, true);   // pasa por encima de otros controles
            sim_run(16);
        }
        sim_touch(ex, cy, false);
        sim_run(300);
        sim_tap(60, 28);   // Cancelar
        sim_run(300);
        CHK(flex_qs_mode() == 0);
        CHK(obj_count(lv_layer_top()) == objs0);
    }

    // 7c) el ultimo control que SE VE no se puede quitar (el panel nunca queda vacio)
    sim_tap(338, 50);
    sim_run(200);
    {
        int first, n = shown_now(&first), guard = 0;
        while (n > 1 && guard++ < 40) {
            CHK(flex_qs_ctl_rect(first, &a));
            sim_tap(a.x1 + 4, a.y1 + 4);
            sim_run(150);
            int n2 = shown_now(&first);
            if (n2 >= n) {
                break;   // no se pudo quitar: el fallo lo dice la comprobacion de abajo
            }
            n = n2;
        }
        CHK(n == 1);
        CHK(flex_qs_ctl_rect(first, &a));
        sim_tap(a.x1 + 4, a.y1 + 4);
        sim_run(300);
        CHK(shown_now(&first) == 1);
    }
    sim_tap(60, 28);   // Cancelar
    sim_run(300);
    CHK(cfg_count() == n0);

    // 7d) el bloqueo por inactividad no cae con la cortina abierta (Lock.h:392)
    flex_cfg_set_i32("autolockms", 60000);
    sim_tap(338, 50);
    sim_run(200);
    CHK(flex_qs_mode() == 1);
    sim_run(65000);
    CHK(flex_qs_mode() == 1 && flex_qs_is_open());
    CHK(flex_shell_state() == FLEX_SH_HOME);
    sim_tap(60, 28);   // Cancelar
    sim_run(300);

    // 8) editor: Modo PC de capsula a circulo (asa derecha) y quitar Camara; Listo guarda
    sim_tap(338, 50);
    sim_run(200);
    CHK(flex_qs_ctl_rect(FLEX_QS_DEX, &a));
    int ax = a.x2 - 8, ay = (a.y1 + a.y2) / 2;
    sim_drag(ax, ay, ax - 60, ay, 200);
    sim_run(200);
    lv_area_t g;
    CHK(flex_qs_group_rect(&g));
    CHK(flex_qs_ctl_rect(FLEX_QS_DEX, &a) && a.x2 - a.x1 + 1 == FLEX_QP_TCIRC);   // ya es un circulo
    CHK(flex_qs_ctl_rect(FLEX_QS_CAMERA, &a));
    sim_tap(a.x1 + 4, a.y1 + 4);
    sim_run(200);
    sim_tap(440, 28);   // Listo
    sim_run(300);
    CHK(flex_qs_mode() == 0);
    CHK(!cfg_has(FLEX_QS_CAMERA) && cfg_has(FLEX_QS_DEX));
    CHK(!visible(FLEX_QS_CAMERA));
    sim_shot("qs_06_tras_editar");

    // 9) catalogo: Camara vuelve con "Anadir un control"
    sim_tap(338, 50);
    sim_run(200);
    CHK(flex_qs_mode() == 1);
    int idx = -1;
    for (int y = 790; y > 120; y -= 10) {   // el bloque "Anadir": el ultimo
        sim_tap(240, y);
        sim_run(150);
        if (flex_qs_mode() == 2) {
            break;
        }
        if (flex_qs_mode() != 1) {
            break;
        }
    }
    CHK(flex_qs_mode() == 2);
    sim_shot("qs_07_catalogo");
    idx = flex_qs_catalog_index(FLEX_QS_CAMERA);
    CHK(idx >= 0);
    if (idx >= 0) {
        int cx = flex_qp_col_x(idx % 4) + FLEX_QP_CW / 2, cy = 96 + 12 + (idx / 4) * 112 + 31;
        sim_tap(cx, cy);
        sim_run(300);
    }
    CHK(flex_qs_mode() == 1);
    sim_tap(440, 28);   // Listo
    sim_run(300);
    CHK(cfg_has(FLEX_QS_CAMERA));
    CHK(visible(FLEX_QS_CAMERA));

    // 10) Restablecer diseno + Listo = fabrica (Modo PC vuelve a capsula)
    sim_tap(338, 50);
    sim_run(200);
    sim_tap(240, 70);
    sim_run(200);
    sim_tap(440, 28);
    sim_run(300);
    CHK(flex_qs_ctl_rect(FLEX_QS_DEX, &a) && a.x2 - a.x1 + 1 == flex_qp_span_w(2));

    // 11) asa de la tarjeta: con 9 circulos (3 filas), encoger a 2 filas y se guarda
    sim_tap(338, 50);
    sim_run(200);
    static const int TO_TILE[3] = {FLEX_QS_AIRPLANE, FLEX_QS_DEX, FLEX_QS_CONN};
    for (int i = 0; i < 3; i++) {
        if (flex_qs_ctl_rect(TO_TILE[i], &a)) {
            int x = a.x2 - 8, y = (a.y1 + a.y2) / 2;
            sim_drag(x, y, x - 60, y, 200);
            sim_run(200);
        }
        CHK(flex_qs_ctl_rect(TO_TILE[i], &a) && a.x2 - a.x1 + 1 == FLEX_QP_TCIRC);
    }
    sim_tap(440, 28);   // Listo
    sim_run(300);
    CHK(flex_qs_group_rect(&g));
    CHK(g.y2 - g.y1 + 1 == flex_qp_group_h(3));
    sim_shot("qs_08_tarjeta");
    sim_drag(240, g.y2 - 11, 240, g.y2 - 110, 300);
    sim_run(700);
    lv_area_t g2;
    CHK(flex_qs_group_rect(&g2));
    CHK(g2.y2 - g2.y1 + 1 == flex_qp_group_h(2));
    CHK(flex_qs_grows() == 2);
    {
        uint8_t b[FLEX_QS_BLOB_N];
        CHK(flex_kvs_get_blob(FLEX_QS_NVS_NS, FLEX_QS_NVS_KEY, b, sizeof(b)) == FLEX_QS_BLOB_N && b[3] == 2);
    }
    // la tarjeta con mas filas de las que ensena se desplaza por dentro
    CHK(flex_qs_ctl_rect(FLEX_QS_CONN, &a));
    int conn_y = a.y1;
    sim_drag(240, g2.y1 + 150, 240, g2.y1 + 40, 200);
    sim_run(600);
    CHK(flex_qs_ctl_rect(FLEX_QS_CONN, &a) && a.y1 < conn_y);
    CHK(flex_qs_group_rect(&g) && g.y1 == g2.y1);   // el panel no se movio

    // 12) sobre una app: el panel abre encima y "Ajustes" deja en la app
    flex_qs_close_now();
    flex_app_open(IC_NOTAS, NULL);
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_APP);
    open_top();
    CHK(flex_qs_panel_y() == 800);
    CHK(flex_shell_state() == FLEX_SH_APP);
    sim_shot("qs_09_sobre_app");
    sim_tap(442, 50);   // engranaje
    sim_run(500);
    CHK(!flex_qs_is_open());
    CHK(flex_app_current() == IC_AJUSTES);

    // 13) borde derecho: hacia dentro con intencion abre (animado)
    flex_app_close();
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    sim_drag(472, 400, 380, 410, 150);
    sim_run(500);
    CHK(flex_qs_panel_y() == 800);
    // cerrar arrastrando hacia arriba desde la cabecera
    sim_drag(200, 100, 200, 0, 120);
    sim_run(500);
    CHK(!flex_qs_is_open());
    sim_shot("qs_10_inicio_tras_borde");   // el escritorio queda como estaba
    // abrir desde el borde derecho EMPEZANDO en un boton de la barra: el boton no
    // se queda pulsado (LVGL solo avisaba con INDEV_RESET, no con PRESS_LOST)
    sim_touch(470, 770, true);
    sim_run(40);
    for (int x = 470; x >= 398; x -= 8) {
        sim_touch(x, 770, true);
        sim_run(16);
    }
    sim_touch(398, 770, false);
    sim_run(600);
    CHK(flex_qs_panel_y() == 800);
    flex_qs_close_now();
    sim_run(400);
    CHK(!flex_navbar_flash_visible());
    // un desplazamiento vertical junto al borde derecho NO abre
    sim_drag(470, 300, 466, 600, 200);
    sim_run(300);
    CHK(!flex_qs_is_open());

    // 14) con clave aparece "Bloquear"; bloquear con el panel abierto lo cierra
    CHK(flex_lock_set("1470", FLEX_LOCK_PIN));
    uint32_t n_top = lv_obj_get_child_count(lv_layer_top());
    open_top();
    CHK(visible(FLEX_QS_LOCK));
    flex_shell_lock();
    sim_run(100);
    CHK(!flex_qs_is_open());
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);   // la cortina no deja objetos
    flex_lock_clear();
    flex_shell_lock();
    sim_run(100);

    printf("panel: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
