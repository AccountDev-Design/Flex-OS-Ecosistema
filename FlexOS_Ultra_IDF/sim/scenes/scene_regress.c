// Regresiones de la revision adversarial 4: una comprobacion por hallazgo, cada
// una falla si se revierte su correccion.
//  F0 el Centro no devuelve el resto del toque a LVGL al cerrarse
//  F1 el desliz para desbloquear puede empezar sobre una tarjeta del bloqueo
//  F2 un banner que ya sale no se queda encima del bloqueo
//  F3 flex_say decide "dentro de una app" al llamarlo
//  F4 cancelar un candado pedido desde una app vuelve a esa app
//  F5 teclear con dos pulgares no suspende la pantalla
//  F6 el escritorio no acepta toques mientras abre una app
//  F7 una pulsacion de 450 ms sobre un icono es un toque (umbral 550 ms)
//  F8 el refresco del minuto no deja las paginas entre dos encajes
//  F9 un toque durante la caida del bloqueo (sin clave) no desbloquea
// Revision 5 (fidelidad a Arduino):
//  R1 sin clave, un desliz mas horizontal que vertical no desbloquea (swipeUp)
//  R2 la caja no se abre con un desliz que empieza en la barra de estado (y <= 96)
//  R3 el menu de la caja dice "Anadir a inicio" si la fila anade (crea pagina)
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_home_model.h"
#include "flex_i18n.h"
#include "flex_kb_layout.h"
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
            printf("regresiones: FALLO linea %d: %s\n", __LINE__, #c); \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void to_home(void)
{
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);
}

static void quiet(uint32_t max_ms)
{
    for (uint32_t t = 0; t < max_ms && (flex_notif_banner_visible() || flex_notif_queue_len()); t += 100) {
        sim_run(100);
    }
}

// Una etiqueta visible con ese texto exacto en la pantalla o en la capa superior
static bool text_visible(lv_obj_t *o, const char *txt)
{
    if (lv_obj_is_hidden(o)) {
        return false;
    }
    if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), txt) == 0) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        if (text_visible(lv_obj_get_child(o, (int32_t)i), txt)) {
            return true;
        }
    }
    return false;
}

static bool on_screen(const char *txt)
{
    return text_visible(lv_screen_active(), txt) || text_visible(lv_layer_top(), txt);
}

// Centro de la fila "row" del menu contextual de la celda i de la caja (sin scroll)
static void ctx_row_xy(int i, int row, int *x, int *y)
{
    int cx = 24 + (i % 4) * 120, iy = 152 + (i / 4) * 116, h = 4 * 56;
    int px = cx + 72 + 12;
    if (px + 262 > 480 - 8) {
        px = cx - 12 - 262;
    }
    px = px < 8 ? 8 : px > 480 - 8 - 262 ? 480 - 8 - 262 : px;
    int py = iy < 152 ? 152 : iy > 800 - 8 - h ? 800 - 8 - h : iy;
    *x = px + 100;
    *y = py + row * 56 + 28;
}

// Centro de un icono de la pagina del escritorio a la vista (no del dock)
static int page_icon(lv_area_t *a, int skip)
{
    for (int id = 0; id < FLEX_APP_N; id++) {
        if (flex_home_icon_area(id, a) && a->y2 < 600 && a->x1 >= 140 && a->x2 < 480 && skip-- <= 0) {
            return id;
        }
    }
    return -1;
}

bool scene_regress_run(void)
{
    bool ok = true;
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_cfg_set_i32("lockwidgets", 0x01 | 0x08);
    flex_lock_clear();
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    }
    to_home();
    CHK(flex_shell_state() == FLEX_SH_HOME);
    quiet(20000);

    // F0: cerrar el Centro deslizando a la derecha con el dedo quieto encima de un icono
    lv_area_t ic;
    int icon = page_icon(&ic, 0);
    CHK(icon >= 0);
    flex_notif_center_show();
    sim_run(400);
    CHK(flex_notif_center_open());
    int cx = (ic.x1 + ic.x2) / 2, cy = (ic.y1 + ic.y2) / 2;
    sim_touch(cx - 90, cy, true);
    sim_run(40);
    for (int x = cx - 90; x <= cx; x += 10) {
        sim_touch(x, cy, true);
        sim_run(16);
    }
    sim_run(400);   // el Centro termina de irse con el dedo aun apoyado
    CHK(!flex_notif_center_open());
    sim_touch(cx, cy, false);
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_HOME && flex_shell_fg_app() < 0);

    // F6: un segundo toque en otro icono mientras se abre el primero no cambia de app
    lv_area_t ic2;
    int icon2 = page_icon(&ic2, 1);
    CHK(icon2 >= 0 && icon2 != icon);
    sim_tap(cx, cy);
    sim_run(30);
    sim_tap((ic2.x1 + ic2.x2) / 2, (ic2.y1 + ic2.y2) / 2);
    sim_run(500);
    CHK(flex_app_current() == icon);
    flex_app_close();
    sim_run(500);

    // F7: 450 ms quieto sobre un icono abre la app (Arduino: < 550 ms es un toque)
    sim_touch(cx, cy, true);
    sim_run(450);
    sim_touch(cx, cy, false);
    sim_run(500);
    CHK(flex_app_current() == icon);
    flex_app_close();
    sim_run(500);

    // F3: una app avisa y se cierra en la misma vuelta: sigue siendo "dentro de la app"
    int n0 = flex_notif_count();
    flex_app_open(IC_NOTAS, NULL);
    sim_run(500);
    flex_say("Notas", "Guardado al salir", NULL);
    flex_app_close();
    sim_run(500);
    CHK(flex_notif_count() == n0);
    quiet(8000);

    // F2: un banner que ya sale no queda encima del bloqueo
    flex_notify("Saliendo", NULL);
    sim_run(400);
    CHK(flex_notif_banner_visible());
    sim_touch(240, 54, true);
    sim_run(60);
    sim_touch(240, 54, false);   // tocarlo: empieza a salir (180 ms)
    sim_run(20);
    flex_shell_lock();
    sim_run(30);
    CHK(flex_shell_state() == FLEX_SH_LOCK);
    CHK(!flex_notif_banner_visible());

    // F1: el desliz empieza sobre la tarjeta de notificaciones (y = 462..512)
    sim_run(300);
    sim_drag(240, 487, 240, 100, 300);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // F9: sin clave, un toque quieto durante la caida del bloqueo no desbloquea
    flex_shell_lock();
    sim_run(50);
    flex_lock_drop_in();
    sim_run(60);
    sim_touch(240, 100, true);
    sim_run(50);
    sim_touch(240, 100, false);
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_LOCK);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // F8: el minuto cambia a mitad de un desliz de paginas (pagina con un widget:
    // el refresco la rehace entera, icono pulsado incluido)
    flex_home_wg_t wg0 = g_home.wg[0][0];
    uint8_t wgn0 = g_home.wg_n[0];
    g_home.wg[0][0] = (flex_home_wg_t){FLEX_WG_CLOCK, 0, 0, 4, 1};
    g_home.wg_n[0] = wgn0 ? wgn0 : 1;
    flex_home_rebuild();
    sim_run(200);
    icon = page_icon(&ic, 0);
    CHK(icon >= 0 && flex_home_icon_area(icon, &ic));
    int x0 = ic.x1;
    int sx = (ic.x1 + ic.x2) / 2, sy = (ic.y1 + ic.y2) / 2;   // el desliz empieza SOBRE un icono
    sim_touch(sx, sy, true);
    sim_run(40);
    for (int x = sx; x >= sx - 100; x -= 10) {
        sim_touch(x, sy, true);
        sim_run(16);
        if (x == sx - 60) {
            flex_home_refresh();   // lo que hace el reloj del shell al cambiar el minuto
        }
    }
    sim_touch(sx - 100, sy, false);
    sim_run(1200);
    CHK(flex_home_scroll_x() % 480 == 0);   // en un encaje, nunca a medias
    // vuelve a la primera pagina y deja el escritorio como estaba
    (void)x0;
    for (int k = 0; k < 4 && flex_home_scroll_x() != 0; k++) {
        sim_drag(100, 450, 420, 452, 200);
        sim_run(600);
    }
    g_home.wg[0][0] = wg0;
    g_home.wg_n[0] = wgn0;
    flex_home_rebuild();
    sim_run(200);

    // F4: candado en Ajustes pedido desde Notas (rueda del panel rapido); atras vuelve a Notas
    CHK(flex_lock_set("1470", FLEX_LOCK_PIN));
    uint32_t lock0 = g_home.lock;
    g_home.lock |= 1u << IC_AJUSTES;
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(600);
    const char *pin = "1470";
    for (const char *p = pin; *p; p++) {
        int i = *p == '0' ? 10 : *p - '1';
        sim_tap(30 + (i % 3) * 144 + 66, 300 + (i / 3) * 94 + 41);
    }
    sim_run(1500);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    flex_app_open(IC_NOTAS, NULL);
    sim_run(500);
    flex_app_launch(IC_AJUSTES, NULL);   // lo que hace la rueda del panel rapido
    sim_run(500);
    CHK(flex_auth_active());
    sim_tap(24, 20);   // atras
    sim_run(400);
    CHK(!flex_auth_active());
    CHK(flex_shell_state() == FLEX_SH_APP && flex_app_current() == IC_NOTAS);
    CHK(flex_shell_fg_app() == IC_NOTAS);
    flex_app_close();
    sim_run(500);
    g_home.lock = lock0;

    // F5: dos pulgares que se solapan al teclear la contrasena no suspenden
    CHK(flex_lock_set("qwer", FLEX_LOCK_PASS));
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 400, 200);
    sim_run(900);
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 0, 0);
    int qx, qy, wx, wy;
    flex_kb_cell_xy(&g, 0, &qx, &qy);
    flex_kb_cell_xy(&g, 1, &wx, &wy);
    qx += g.kw / 2;
    qy += g.kh / 2;
    wx += g.kw / 2;
    wy += g.kh / 2;
    for (int k = 0; k < 2; k++) {   // dos solapes seguidos (< 450 ms entre ellos)
        sim_touch(qx, qy, true);
        sim_run(40);
        for (int f = 0; f < 3; f++) {
            sim_touch_n(wx, wy, 2);
            sim_run(16);
        }
        sim_touch(wx, wy, true);
        sim_run(40);
        sim_touch(wx, wy, false);
        sim_run(60);
    }
    sim_run(300);
    CHK(!flex_power_suspended());
    CHK(flex_shell_state() == FLEX_SH_AUTH);

    // R1: sin clave, un roce en diagonal (dx -220, dy 70) no desbloquea; uno vertical si
    flex_lock_clear();
    flex_shell_lock();
    sim_run(200);
    sim_drag(400, 500, 180, 430, 200);
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_LOCK);
    sim_drag(240, 500, 240, 430, 200);
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_HOME);

    // R2: desde la barra de estado (y 90) no se abre la caja; desde abajo si
    sim_drag(240, 90, 240, 10, 150);
    sim_run(400);
    CHK(!flex_drawer_is_open());
    sim_drag(240, 600, 240, 250, 200);
    sim_run(400);
    CHK(flex_drawer_is_open());
    flex_sys_back();
    sim_run(400);

    // R3: paginas llenas pero sin llegar al maximo: la fila dice "Anadir a inicio"
    // y anade (crea la pagina); nunca "Inicio completo" activa
    flex_home_t home0 = g_home;
    g_home.page_n = 1;   // una sola pagina, sin widgets, que se llena con apps
    g_home.page = 0;
    g_home.fav = 0;
    memset(g_home.order, FLEX_HOME_EMPTY, sizeof(g_home.order));
    memset(g_home.wg_n, 0, sizeof(g_home.wg_n));
    for (int id = 0; id < FLEX_APP_N && flex_home_first_free() >= 0; id++) {
        if (!flex_app_is_fav(id) && !flex_app_is_hidden(id)) {
            flex_home_fav_toggle(id);
        }
    }
    flex_home_rebuild();
    CHK(flex_home_first_free() < 0 && g_home.page_n < FLEX_HOME_PAGES_MAX);
    int pages0 = g_home.page_n;
    sim_drag(240, 600, 240, 250, 200);
    sim_run(400);
    CHK(flex_drawer_is_open());
    bool menu = false;
    for (int c = 0; c < 16 && !menu; c++) {   // la primera app de la caja que no esta en Inicio
        int cx = 24 + (c % 4) * 120 + 36, cy = 152 + (c / 4) * 116 + 36;
        sim_touch(cx, cy, true);
        sim_run(700);
        sim_touch(cx, cy, false);
        sim_run(200);
        if (on_screen("Quitar de inicio")) {
            flex_sys_back();   // esta ya esta: cerrar el menu y probar la siguiente
            sim_run(200);
            continue;
        }
        menu = true;
        CHK(!on_screen("Inicio completo"));
        CHK(on_screen("A\xC3\xB1" "adir a inicio"));
        sim_shot("rg_r3_menu_anadir");
        uint32_t fav0 = g_home.fav;
        int mx, my;
        ctx_row_xy(c, 1, &mx, &my);
        sim_tap(mx, my);
        sim_run(300);
        CHK(g_home.fav != fav0 && g_home.page_n == pages0 + 1);
    }
    CHK(menu);
    while (flex_drawer_is_open()) {
        flex_sys_back();
        sim_run(400);
    }
    g_home = home0;
    flex_home_save();
    flex_home_rebuild();

    flex_lock_clear();
    flex_shell_lock();
    sim_run(200);
    printf("regresiones: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
