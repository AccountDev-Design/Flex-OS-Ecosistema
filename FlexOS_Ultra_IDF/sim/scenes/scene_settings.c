// Ajustes (03b §11): la app real en el simulador: lista de categorias con scroll, abrir una categoria con la transicion de 270 ms, filas que ciclan
// valores REALES (brillo, estilo, apariencia, barra de navegacion, widgets del bloqueo, idioma, formato de hora, transiciones, bloqueo de inactividad,
// apagado seguro), servicios aun sin migrar que lo dicen, volver (chevron, borde izquierdo, boton atras), sesion y suspension a medias.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_i18n.h"
#include "flex_kit.h"
#include "flex_passcode.h"
#include "flex_set_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

uint8_t flex_display_get_brightness(void);
void sim_disk_enable(bool on);
bool sim_disk_put(const char *rel, const void *d, size_t l);
size_t sim_disk_get(const char *rel, void *out, size_t cap);
void flex_session_forget_loaded(int id);
void flex_session_test_preload(int id, const uint8_t *file, size_t len);

int flex_app_settings_view(void);
int flex_app_settings_sel(void);
bool flex_app_settings_animating(void);
int flex_app_settings_scroll(void);
int flex_app_settings_list_scroll(void);
int flex_app_settings_rows(void);
lv_obj_t *flex_app_settings_root(void);
bool flex_app_settings_card_xy(int i, int *x, int *y);
bool flex_app_settings_row_xy(int i, int *x, int *y);
const flex_set_sys_t *flex_app_settings_sys(void);
int flex_app_settings_page_x(bool detail);
bool flex_app_settings_shown(const char *txt);
bool flex_kbs_active(void);
bool flex_app_settings_list_hidden(void);
bool flex_session_pending(int id);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("ajustes: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

static bool wait_view(int v)
{
    for (int i = 0; i < 80; i++) {
        if (flex_app_settings_view() == v && !flex_app_settings_animating()) return true;
        sim_run(20);
    }
    return false;
}
static void tap_card(int cat)
{
    int x, y;
    for (int k = 0; k < 4; k++) {   // lleva la tarjeta a la banda visible con un arrastre y toca
        if (!flex_app_settings_card_xy(cat, &x, &y)) break;
        if (y < 135) {
            sim_drag(240, 200, 240, 600, 200);
        } else if (y > 700) {
            sim_drag(240, 600, 240, 200, 200);
        } else {
            break;
        }
        sim_run(100);
    }
    flex_app_settings_card_xy(cat, &x, &y);
    sim_touch(x, y, true);   // apoyar y soltar sin esperar (sim_tap tarda ~200 ms y la transicion dura 270)
    sim_run(30);
    sim_touch(x, y, false);
}
static void open_cat(int cat)
{
    tap_card(cat);
    sim_run(400);   // los 270 ms de la transicion
}
static void row(int i)
{
    int x, y;
    if (!flex_app_settings_row_xy(i, &x, &y) || y > 700 || y < 140) {
        sim_drag(240, 600, 240, 250, 200);
        sim_run(100);
    }
    if (flex_app_settings_row_xy(i, &x, &y)) {
        sim_tap(x, y);
        sim_run(150);
    }
}
static bool has_text(const char *txt)
{
    return flex_app_settings_shown(txt);   // lo que hay en pantalla: una etiqueta con ese texto en la pagina abierta
}
static void back_chevron(void)
{
    sim_tap(24, 60);
    sim_run(400);
}

bool scene_settings_run(void)
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
    sim_disk_enable(true);
    flex_session_forget_loaded(IC_AJUSTES);
    flex_set_lang(0);
    flex_cfg_set_i32("bright", 80);
    flex_cfg_set_i32("lockwidgets", 1);
    flex_cfg_set_i32("autolockms", 60000);
    flex_theme_set_glass(false);
    flex_theme_set_dark(true);
    flex_theme_set_nav_mode(0);

    // ---- 1) la lista de categorias, con su scroll (898 de alto en 626 de banda: 272 de recorrido)
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(500);
    CHK(wait_view(0) && flex_app_settings_list_scroll() == 0);
    sim_shot("ajustes_lista");
    flex_session_flush_now();
    sim_run(200);
    CHK(!flex_session_pending(IC_AJUSTES));
    sim_drag(240, 600, 240, 100, 300);
    sim_run(200);
    CHK(flex_session_pending(IC_AJUSTES));                    // el scroll de la lista es estado de sesion
    CHK(flex_app_settings_list_scroll() > 100 && flex_app_settings_list_scroll() <= 272);
    int lsc = flex_app_settings_list_scroll();
    sim_drag(240, 200, 240, 700, 300);
    sim_run(200);
    CHK(flex_app_settings_list_scroll() < lsc);
    sim_drag(240, 200, 240, 700, 300);
    sim_run(200);
    CHK(flex_app_settings_list_scroll() == 0);

    // ---- 2) abrir "Pantalla": la transicion dura 270 ms y deja la categoria a la vista
    {
        int x, y;
        CHK(flex_app_settings_card_xy(1, &x, &y));
        sim_tap(x, y);
        sim_run(60);
        CHK(flex_app_settings_animating());                       // en marcha
        CHK(flex_app_settings_page_x(false) < 0 && flex_app_settings_page_x(true) > 0);   // la lista sale hacia la izquierda y la categoria entra por la derecha
        sim_run(300);
        CHK(wait_view(1) && flex_app_settings_sel() == 1 && !flex_app_settings_animating());
        CHK(flex_app_settings_list_hidden());                    // la lista no se sigue pintando debajo
        sim_shot("ajustes_pantalla");
    }

    // ---- 3) Pantalla: brillo (de 25 en 25, de 100 vuelve a 25), estilo, apariencia y barra de navegacion
    CHK(flex_cfg_get_i32("bright", 0) == 80);
    row(0);
    CHK(flex_cfg_get_i32("bright", 0) == 25 && has_text("25%") && flex_display_get_brightness() == 25);   // se guarda Y se aplica al panel
    row(0);
    CHK(flex_cfg_get_i32("bright", 0) == 50);
    row(0);
    row(0);
    CHK(flex_cfg_get_i32("bright", 0) == 100);
    row(0);
    CHK(flex_cfg_get_i32("bright", 0) == 25);                     // 100 -> 25
    CHK(!flex_look()->glass && has_text("Plano"));
    row(1);
    CHK(flex_look()->glass && flex_cfg_get_bool("glass", false) && has_text("Liquid Glass"));
    sim_shot("ajustes_pantalla_vidrio");
    row(1);
    CHK(!flex_look()->glass);
    CHK(flex_look()->dark && has_text("Oscuro"));
    row(2);
    CHK(!flex_look()->dark && !flex_cfg_get_bool("dark", true) && has_text("Claro"));
    sim_shot("ajustes_pantalla_claro");
    row(2);
    CHK(flex_look()->dark);
    row(3);
    CHK(flex_look()->nav_mode == 1 && has_text("Gestos iOS"));
    row(3);
    CHK(flex_look()->nav_mode == 0 && has_text("Botones"));
    // los widgets del bloqueo: cada fila conmuta SU bit y guarda solo esa clave
    row(4);
    CHK(flex_cfg_get_i32("lockwidgets", -1) == 0);                // 1 -> 0 (reloj fuera)
    row(5);
    CHK(flex_cfg_get_i32("lockwidgets", -1) == FLEX_LW_WEATHER);
    row(6);
    row(7);
    CHK(flex_cfg_get_i32("lockwidgets", -1) == (FLEX_LW_WEATHER | FLEX_LW_CAL | FLEX_LW_NOTIF));
    row(5);
    row(6);
    row(7);
    row(4);
    CHK(flex_cfg_get_i32("lockwidgets", -1) == FLEX_LW_CLOCK);
    CHK(flex_app_settings_scroll() >= 0);

    // ---- 4) volver: el chevron de la cabecera, el boton atras del sistema y el arrastre desde el borde izquierdo
    back_chevron();
    CHK(wait_view(0) && flex_app_settings_list_scroll() == 0 && !flex_app_settings_list_hidden());
    open_cat(1);
    CHK(wait_view(1));
    flex_sys_back();                                             // el boton atras: de la categoria a la lista
    sim_run(400);
    CHK(wait_view(0) && flex_app_settings_view() == 0);
    open_cat(1);
    CHK(wait_view(1));
    sim_drag(10, 400, 140, 405, 200);                            // desde el borde izquierdo, a la derecha (el primer punto que ve el arbitro puede estar unos px dentro)
    sim_run(400);
    CHK(wait_view(0));
    open_cat(1);
    CHK(wait_view(1));
    sim_drag(10, 400, 60, 405, 200);                             // se queda corto (50 px): no vuelve
    sim_run(400);
    CHK(wait_view(1));
    sim_drag(200, 400, 330, 405, 200);                           // lejos del borde: tampoco
    sim_run(400);
    CHK(wait_view(1));
    sim_drag(10, 400, 140, 560, 200);                            // demasiado inclinado (160 px de desvio): tampoco
    sim_run(400);
    CHK(wait_view(1));
    back_chevron();
    CHK(wait_view(0));

    // ---- 5) General: idioma (cicla los 6 y vuelve), formato de hora, servicios sin migrar
    open_cat(0);
    CHK(wait_view(1) && flex_app_settings_sel() == 0);
    CHK(flex_lang() == 0);
    row(0);
    CHK(flex_lang() == 1 && flex_cfg_get_i32("lang", -1) == 1);
    for (int i = 0; i < 5; i++) row(0);
    CHK(flex_lang() == 0);                                       // ES -> EN -> FR -> PT -> IT -> ZH -> ES
    CHK(!flex_look()->h24);
    row(2);
    CHK(flex_look()->h24 && flex_cfg_get_bool("h24", false) && has_text("24 horas"));
    row(2);
    CHK(!flex_look()->h24 && has_text("12 horas"));
    CHK(has_text("No disponible"));                              // la hora por red no esta migrada: lo dice
    row(6);                                                      // "Sincronizar ahora": no hay servicio, no hace nada
    CHK(flex_app_settings_view() == 1);
    row(7);                                                      // "Actualizaciones": no hay OTA, avisa
    CHK(flex_kit_toast_count(flex_app_settings_root()) == 1);
    sim_run(2000);
    row(3);                                                      // zona horaria: informativa
    row(1);                                                      // fecha y hora: informativa (Arduino tampoco hace nada)
    CHK(flex_app_settings_view() == 1 && flex_lang() == 0 && flex_kit_toast_count(flex_app_settings_root()) == 0);
    sim_shot("ajustes_general");
    back_chevron();
    CHK(wait_view(0));

    // ---- 6) Personalizacion: iconos, transiciones (zoom -> fundido -> deslizar -> zoom) y teclado
    open_cat(5);
    CHK(wait_view(1));
    CHK(flex_look()->icon_style == 0 && flex_look()->anim_style == 0);
    row(1);
    CHK(flex_look()->icon_style == 1 && flex_cfg_get_i32("iconstyle", -1) == 1 && has_text("Vidrio"));
    row(1);
    CHK(flex_look()->icon_style == 0);
    row(2);
    CHK(flex_look()->anim_style == 1 && has_text("Fundido"));
    row(2);
    CHK(flex_look()->anim_style == 2 && has_text("Deslizar"));
    row(2);
    CHK(flex_look()->anim_style == 0 && has_text("Zoom"));
    row(0);                                                      // "Personalizar UI" = el mismo material que Pantalla > Estilo
    CHK(flex_look()->glass);
    row(0);
    CHK(!flex_look()->glass);
    row(3);                                                      // Teclado: abre los Ajustes del teclado (A11k)
    sim_run(300);
    CHK(flex_kbs_active() && flex_kit_toast_count(flex_app_settings_root()) == 0);
    flex_sys_back();                                             // atras: sale de esa pantalla y Ajustes sigue en su categoria
    sim_run(300);
    CHK(!flex_kbs_active() && flex_app_settings_view() == 1 && flex_app_settings_sel() == 5);
    back_chevron();
    CHK(wait_view(0));

    // ---- 7) Seguridad: bloqueo de inactividad (6 opciones y vuelta), apagado seguro solo con clave
    open_cat(6);
    CHK(wait_view(1));
    CHK(flex_cfg_get_i32("autolockms", 0) == 60000 && has_text("1 minuto"));
    row(1);
    CHK(flex_cfg_get_i32("autolockms", 0) == 300000 && has_text("5 minutos"));
    row(1);
    row(1);
    CHK(flex_cfg_get_i32("autolockms", 0) == 1800000 && has_text("30 minutos"));
    row(1);
    CHK(flex_cfg_get_i32("autolockms", 0) == 0 && has_text("Nunca"));
    row(1);
    CHK(flex_cfg_get_i32("autolockms", 0) == 30000 && has_text("30 segundos"));
    row(1);
    CHK(flex_cfg_get_i32("autolockms", 0) == 60000);
    CHK(!flex_cfg_get_bool("poffpin", false) && has_text("Configura antes un PIN"));
    row(3);                                                      // sin clave no hay nada que pedir
    CHK(!flex_cfg_get_bool("poffpin", false));
    flex_cfg_set_i32("locktype", 1);                             // con una clave puesta (la escribe la pantalla de clave)
    back_chevron();
    open_cat(6);
    CHK(wait_view(1) && has_text("Desactivado") && has_text("PIN configurado"));
    row(3);
    CHK(flex_cfg_get_bool("poffpin", false) && has_text("Activado"));
    row(3);
    CHK(!flex_cfg_get_bool("poffpin", false));
    row(2);                                                      // proteccion contra robo: servicio aun no migrado, avisa
    CHK(flex_kit_toast_count(flex_app_settings_root()) == 1);
    sim_run(2000);
    flex_cfg_set_i32("locktype", 0);
    sim_shot("ajustes_seguridad");
    back_chevron();
    CHK(wait_view(0));

    // ---- 8) Almacenamiento: el tamano de la papelera se mide en la tarea de almacenamiento y la fila se actualiza
    sim_disk_put("/Papelera/@Notas@a.txt", "0123456789", 10);
    open_cat(8);
    CHK(wait_view(1));
    sim_run(300);
    CHK(has_text("10 B"));
    back_chevron();
    CHK(wait_view(0));

    // ---- 9) sesion: categoria abierta y las dos posiciones de scroll, con el formato de Arduino (cabecera de 16 B + 8 B)
    {
        sim_drag(240, 600, 240, 300, 200);
        sim_run(100);
        CHK(flex_app_settings_list_scroll() > 0);
        open_cat(0);                                             // lleva la tarjeta a la vista (mueve la lista) y entra
        CHK(wait_view(1));
        int ls = flex_app_settings_list_scroll();
        sim_drag(240, 600, 240, 300, 200);
        sim_run(100);
        int ds = flex_app_settings_scroll();
        CHK(ds > 0);
        flex_session_flush_now();
        sim_run(200);
        static uint8_t file[256];
        size_t n = sim_disk_get("/System/Sessions/ajustes.bin", file, sizeof(file));
        CHK(n == 16 + 8);
        flex_app_terminate_ex(IC_AJUSTES, true);
        sim_run(300);
        flex_session_forget_loaded(IC_AJUSTES);
        flex_session_test_preload(IC_AJUSTES, file, n);
        flex_app_open(IC_AJUSTES, NULL);
        sim_run(600);
        CHK(flex_app_settings_view() == 1 && flex_app_settings_sel() == 0 && !flex_app_settings_animating());
        CHK(flex_app_settings_scroll() == ds);
        back_chevron();
        CHK(wait_view(0) && flex_app_settings_list_scroll() == ls);
        // una sesion con una categoria que no existe se ignora entera
        flex_set_sess_t bad = {1, 99, 50, 50};
        uint8_t raw[256];
        memcpy(raw, file, 16);
        memcpy(raw + 16, &bad, sizeof(bad));
        flex_app_terminate_ex(IC_AJUSTES, true);
        sim_run(300);
        flex_session_forget_loaded(IC_AJUSTES);
        flex_session_test_preload(IC_AJUSTES, raw, n);   // el CRC de la cabecera ya no vale: tambien se ignora
        flex_app_open(IC_AJUSTES, NULL);
        sim_run(600);
        CHK(flex_app_settings_view() == 0 && flex_app_settings_list_scroll() == 0);
    }

    // ---- 10) suspender con la transicion a medias: al volver no queda nada movido
    {
        tap_card(1);
        sim_run(60);
        CHK(flex_app_settings_animating());
        flex_sys_home();
        CHK(!flex_app_settings_animating());                      // se cierra en el acto, sin esperar a los 270 ms
        sim_run(400);
        flex_app_open(IC_AJUSTES, NULL);
        sim_run(500);
        CHK(!flex_app_settings_animating() && flex_app_settings_view() == 1 && flex_app_settings_sel() == 1);
        CHK(flex_app_settings_page_x(false) == 0 && flex_app_settings_page_x(true) == 0);
        back_chevron();
        CHK(wait_view(0));
    }

    // ---- 10b) los valores vivos se leen otra vez al reanudar: un cambio hecho desde fuera (panel rapido) aparece
    {
        open_cat(1);
        CHK(wait_view(1) && has_text("25%"));
        flex_sys_home();
        sim_run(400);
        flex_cfg_set_i32("bright", 50);
        flex_app_open(IC_AJUSTES, NULL);
        sim_run(500);
        CHK(flex_app_settings_view() == 1 && has_text("50%") && !has_text("25%"));
        flex_cfg_set_i32("bright", 25);
        back_chevron();
        CHK(wait_view(0));
    }

    // ---- 11) "atras" desde la lista cierra la app; dos toques rapidos durante la transicion no abren dos categorias
    {
        int x, y;
        tap_card(2);                                             // empieza la transicion al soltar
        sim_run(20);
        CHK(flex_app_settings_animating());
        CHK(flex_app_settings_card_xy(3, &x, &y));
        sim_touch(x, y, true);                                   // la lista ya se mueve: este toque se descarta
        sim_run(20);
        sim_touch(x, y, false);
        sim_run(500);
        CHK(wait_view(1) && flex_app_settings_sel() == 2);
        flex_sys_back();
        sim_run(400);
        CHK(wait_view(0));
        flex_sys_back();                                         // en la lista: sale de la app
        sim_run(400);
        CHK(flex_app_current() == -1);
    }

    flex_set_lang(0);
    return ok;
}
