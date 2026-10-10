// Menu contextual del escritorio (01a §9) y Modo kiosco (01a §5.14, 01c §12):
// filas atenuadas sin clave, candado de app con la clave, pantalla de zona
// excluida, la tabla de vetos (§12.4) como lista de pruebas, candado de la
// esquina, salida con la clave (cancelar vuelve a la app), persistencia y
// arranque directo a la app clavada.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_frame.h"
#include "flex_home_model.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("kiosco: FALLO linea %d: %s\n", __LINE__, #c);    \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void key(char d)
{
    int i = d == '0' ? 10 : d - '1';
    sim_tap(30 + (i % 3) * 144 + 66, 300 + (i / 3) * 94 + 41);
}

static void type(const char *s)
{
    for (; *s; s++) {
        key(*s);
    }
    sim_run(600);
}

static void to_home(void)
{
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
}

// Icono de la rejilla de la pagina visible (no del dock)
static int grid_icon(lv_area_t *a)
{
    for (int id = 0; id < FLEX_APP_N; id++) {
        if (flex_home_icon_area(id, a) && a->y2 < 600 && a->x1 >= 0 && a->x2 < 480) {
            return id;
        }
    }
    return -1;
}

static void long_press(const lv_area_t *a)
{
    int x = (a->x1 + a->x2) / 2, y = (a->y1 + a->y2) / 2;
    sim_touch(x, y, true);
    sim_run(1150);
    sim_touch(x, y, false);
    sim_run(300);   // apertura de 150 ms
}

// Centro de la fila del menu (misma colocacion que ctxOpen)
static void row_xy(const lv_area_t *a, int rows, int row, int *x, int *y)
{
    int S = lv_area_get_width(a), ph = rows * 58;
    int rr = (480 - 8) - (a->x2 + 1 + 12), rl = (a->x1 - 12) - 8;
    bool right = rr >= 244 ? true : rl >= 244 ? false : rr >= rl;
    int px = right ? a->x1 + S + 12 : a->x1 - 12 - 244, py = a->y1;
    px = px < 8 ? 8 : px > 480 - 8 - 244 ? 480 - 8 - 244 : px;
    py = py < 8 ? 8 : py > 800 - 8 - ph ? 800 - 8 - ph : py;
    *x = px + 100;
    *y = py + row * 58 + 29;
}

static lv_obj_t *find_text(lv_obj_t *o, const char *txt)
{
    if (lv_obj_is_hidden(o)) {
        return NULL;
    }
    if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), txt) == 0) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_text(lv_obj_get_child(o, (int32_t)i), txt);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static bool shown(const char *txt)
{
    return find_text(lv_layer_top(), txt) != NULL;
}

// Mantener el candado de la esquina
static void hold_badge(uint32_t ms)
{
    sim_touch(460, 56, true);
    sim_run(ms);
}

bool scene_kiosk_run(void)
{
    bool ok = true;
    flex_lock_clear();
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_cfg_set_bool("kioskon", false);
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    } else {
        flex_theme_set_glass(true);
        flex_theme_set_dark(true);
    }
    flex_kiosk_load();
    to_home();
    CHK(flex_shell_state() == FLEX_SH_HOME);
    uint32_t lock0 = g_home.lock;

    // 1) sin clave: menu con candado, edicion y kiosco atenuados; inertes; fuera cierra
    lv_area_t a;
    int app = grid_icon(&a);
    CHK(app >= 0);
    long_press(&a);
    CHK(flex_home_ctx_open() && flex_shell_state() == FLEX_SH_OVERLAY);
    CHK(shown("Bloquear app") && shown("Modo edici\xC3\xB3n") && shown("Modo kiosko"));
    sim_shot("ki_01_menu_sin_clave");
    int x, y;
    row_xy(&a, 3, 2, &x, &y);
    sim_tap(x, y);   // "Modo kiosko" inactiva: ni siquiera cierra
    sim_run(300);
    CHK(flex_home_ctx_open() && !flex_kiosk_set_active());
    sim_tap(20, 760);   // fuera
    sim_run(300);
    CHK(!flex_home_ctx_open() && flex_shell_state() == FLEX_SH_HOME);
    // el dock no abre el menu
    lv_area_t dock;
    CHK(flex_home_icon_area(IC_CALEND, &dock));
    long_press(&dock);
    CHK(!flex_home_ctx_open());
    sim_run(800);
    if (flex_shell_state() == FLEX_SH_APP) {   // el toque largo del dock no abre el menu (ni la app)
        flex_sys_home();
        sim_run(800);
    }

    // 2) con clave: "Bloquear app" pide la clave y guarda el candado (32 bits)
    CHK(flex_lock_set("2468", FLEX_LOCK_PIN));
    to_home();
    sim_drag(240, 700, 240, 400, 200);
    sim_run(600);
    type("2468");
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    app = grid_icon(&a);
    long_press(&a);
    row_xy(&a, 3, 0, &x, &y);
    sim_tap(x, y);
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    type("2468");
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    CHK((g_home.lock >> app) & 1u);
    CHK((uint32_t)flex_cfg_get_i32("applockm", 0) == g_home.lock);
    app = grid_icon(&a);
    long_press(&a);
    CHK(shown("Desbloquear app"));
    sim_tap(20, 760);
    sim_run(300);

    // 3) "Modo kiosko": pantalla de zona excluida; arrastrar marca la zona
    int kapp = app;
    long_press(&a);
    row_xy(&a, 3, 2, &x, &y);
    sim_tap(x, y);
    sim_run(400);
    CHK(flex_kiosk_set_active() && flex_shell_state() == FLEX_SH_OVERLAY);
    sim_drag(40, 420, 300, 560, 304);   // multiplo de 8: el ultimo punto apoyado es el final
    sim_run(100);
    CHK(shown("260 x 140"));
    sim_shot("ki_02_zona_excluida");
    // Iniciar: se guarda ANTES de abrir y la app se abre sin pedir su candado
    sim_tap(270 + 90, 704 + 32);
    sim_run(900);
    CHK(flex_kiosk_active() && flex_kiosk_app() == kapp);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_app_current() == kapp);
    CHK(flex_cfg_get_bool("kioskon", false) && flex_cfg_get_i32("kioskapp", -1) == kapp);
    CHK(flex_cfg_get_i32("kioskx", 0) == 40 && flex_cfg_get_i32("kiosky", 0) == 420);
    CHK(flex_cfg_get_i32("kioskw", 0) == 260 && flex_cfg_get_i32("kioskh", 0) == 140);
    CHK(flex_kiosk_badge_visible());
    CHK(flex_navbar_ctx() == FLEX_NAV_HIDDEN);
    sim_shot("ki_03_app_clavada");

    // 4) la tabla de vetos (01c §12.4)
    flex_sys_back();
    flex_sys_home();
    flex_sys_recents();
    flex_app_close();
    sim_run(600);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_app_current() == kapp);
    int other = kapp == IC_RELOJ ? IC_CALC : IC_RELOJ;
    flex_app_open(other, NULL);
    flex_app_launch(other, NULL);
    sim_run(600);
    CHK(flex_app_current() == kapp);
    flex_qs_open();
    sim_drag(400, 4, 400, 500, 250);   // cortina desde arriba
    sim_run(500);
    CHK(!flex_qs_is_open());
    flex_notif_center_show();
    sim_run(400);
    CHK(!flex_notif_center_open());
    flex_drawer_open();
    flex_recents_open();
    flex_auth_setup(NULL, NULL);
    sim_run(300);
    CHK(!flex_drawer_is_open() && !flex_recents_is_open() && !flex_auth_active());
    // suspension: doble toque de dos dedos vetado
    for (int k = 0; k < 2; k++) {
        sim_touch_n(240, 300, 2);
        sim_run(60);
        sim_touch_n(240, 300, 0);
        sim_run(120);
    }
    sim_run(300);
    CHK(!flex_power_suspended());
    // zona excluida: un toque dentro no llega ("sin dato"); fuera si
    sim_touch(150, 480, true);
    sim_run(50);
    CHK(!flex_touch_arb()->t.down);
    sim_touch(150, 480, false);
    sim_run(50);
    sim_touch(150, 300, true);
    sim_run(50);
    CHK(flex_touch_arb()->t.down);
    sim_touch(150, 300, false);
    sim_run(50);
    // bloqueo automatico desactivado
    flex_cfg_set_i32("autolockms", 30000);
    sim_run(31000);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_app_current() == kapp);
    flex_cfg_set_i32("autolockms", 60000);

    // 5) salida: > 1 s en el candado pide la clave; cancelar vuelve a la app clavada
    hold_badge(700);
    CHK(flex_shell_state() == FLEX_SH_APP);   // aun no
    sim_run(500);
    sim_touch(460, 56, false);
    sim_run(600);
    CHK(flex_shell_state() == FLEX_SH_AUTH && !flex_kiosk_badge_visible());
    sim_shot("ki_04_salida_clave");
    sim_tap(24, 20);   // flecha atras: cancelar
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_app_current() == kapp && flex_kiosk_active());
    CHK(flex_kiosk_badge_visible());
    // moverse con el dedo no dispara
    sim_touch(460, 56, true);
    sim_run(300);
    sim_touch(475, 75, true);
    sim_run(1000);
    sim_touch(475, 75, false);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_APP);

    // 6) persistencia: reiniciar vuelve a la app clavada sin pasar por el bloqueo
    //    (al arrancar la vista del bloqueo esta puesta, encima de las apps)
    flex_kiosk_exit_now();
    sim_run(900);
    flex_cfg_set_bool("kioskon", true);
    flex_cfg_set_i32("kioskapp", kapp);
    flex_shell_lock();
    sim_run(200);
    CHK(flex_shell_lock_visible());
    flex_kiosk_load();
    flex_kiosk_boot();
    sim_run(900);
    CHK(flex_kiosk_active() && flex_shell_state() == FLEX_SH_APP && flex_app_current() == kapp);
    CHK(!flex_shell_lock_visible() && flex_kiosk_badge_visible());
    sim_shot("ki_05_arranque_kiosco");

    // 7) acertar la clave: fuera del kiosco, NVS borrada y a Inicio
    hold_badge(1200);
    sim_touch(460, 56, false);
    sim_run(600);
    type("2468");
    sim_run(900);
    CHK(!flex_kiosk_active() && flex_shell_state() == FLEX_SH_HOME);
    CHK(!flex_cfg_get_bool("kioskon", true) && flex_cfg_get_i32("kioskapp", 0) == -1);
    CHK(!flex_kiosk_badge_visible());
    flex_sys_recents();   // los vetos ya no aplican
    sim_run(400);
    CHK(flex_recents_is_open());
    flex_sys_back();
    sim_run(400);

    // 8) sin clave el kiosco guardado no se aplica (la NVS no se toca)
    flex_cfg_set_bool("kioskon", true);
    flex_cfg_set_i32("kioskapp", kapp);
    flex_lock_clear();
    flex_kiosk_load();
    CHK(!flex_kiosk_active() && flex_cfg_get_bool("kioskon", false));
    flex_cfg_set_bool("kioskon", false);
    flex_cfg_set_i32("kioskapp", -1);
    flex_kiosk_load();

    g_home.lock = lock0;
    flex_home_save();
    flex_home_rebuild();
    flex_shell_lock();
    sim_run(200);
    printf("kiosco: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
