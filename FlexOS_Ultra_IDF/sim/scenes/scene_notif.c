// Avisos: banner (entra, caduca, cola con "+N", descartar deslizando o tocando,
// vuelve al frente si el panel rapido le quita la pantalla, espera en el
// bloqueo), Centro de notificaciones (borde izquierdo siguiendo al dedo, quitar
// una fila, No molestar, Borrar todas, cerrar arrastrando a la derecha, sobre
// una app), tarjeta del bloqueo con el aviso mas reciente y el control No
// molestar del panel rapido.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
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
            printf("avisos: FALLO linea %d: %s\n", __LINE__, #c);    \
            ok = false;                                              \
        }                                                            \
    } while (0)

#define CARD_Y 54   // centro vertical del banner (FPB_Y 18 + 72 / 2)

static bool title_is(const char *s)
{
    const char *t = flex_notif_banner_title();
    return t && strcmp(t, s) == 0;
}

// Un icono del escritorio (no del dock): si LVGL viera un arrastre, la pagina se moveria.
static int page_icon(lv_area_t *a)
{
    for (int id = 0; id < FLEX_APP_N; id++) {
        if (flex_home_icon_area(id, a) && a->y2 < 600) {
            return id;
        }
    }
    return -1;
}

static bool same_area(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 == b->x1 && a->y1 == b->y1 && a->x2 == b->x2 && a->y2 == b->y2;
}

// Hasta que el banner se vaya (y la cola se vacie) o se acabe el tiempo.
static void run_until_quiet(uint32_t max_ms)
{
    for (uint32_t t = 0; t < max_ms && (flex_notif_banner_visible() || flex_notif_queue_len()); t += 100) {
        sim_run(100);
    }
}

// Abre el Centro desde el borde izquierdo, con el dedo apoyado en x.
static void edge_to(int x)
{
    sim_touch(4, 400, true);
    sim_run(30);
    for (int px = 4; px <= x; px += 24) {
        sim_touch(px, 402, true);
        sim_run(16);
    }
}

// Ninguna etiqueta del banner pasa de una linea (titulos largos: "...").
static bool banner_one_line(void)
{
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        if (lv_obj_get_width(c) != 452 || lv_obj_get_height(c) != 72) {
            continue;
        }
        bool any = false;
        for (uint32_t k = 0; k < lv_obj_get_child_count(c); k++) {
            lv_obj_t *l = lv_obj_get_child(c, (int32_t)k);
            if (lv_obj_check_type(l, &lv_label_class)) {
                any = true;
                const lv_font_t *f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
                if (lv_obj_get_height(l) > lv_font_get_line_height(f)) {
                    printf("avisos: etiqueta de %d px (linea %d)\n", (int)lv_obj_get_height(l),
                           (int)lv_font_get_line_height(f));
                    return false;
                }
            }
        }
        return any;
    }
    return false;
}

bool scene_notif_run(void)
{
    bool ok = true;
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_cfg_set_i32("lockwidgets", 0x01 | 0x08);   // reloj + notificaciones
    flex_notif_set_dnd(false);
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
    run_until_quiet(20000);   // lo que dejaran escenas anteriores
    uint32_t n_top = lv_obj_get_child_count(lv_layer_top());

    // 1) un aviso del sistema: baja, se queda 4,2 s y sube. Queda en el historial.
    int n0 = flex_notif_count();
    flex_notify("Copia de seguridad completada", "Ajustes guardados");
    sim_run(60);
    CHK(flex_notif_banner_visible());
    CHK(title_is("Copia de seguridad completada"));
    CHK(flex_notif_count() == (n0 < 3 ? n0 + 1 : 3));
    sim_run(300);
    sim_shot("ntf_01_banner");
    sim_run(3700);
    CHK(flex_notif_banner_visible());   // 4,0 s: aun a la vista
    sim_run(800);
    CHK(!flex_notif_banner_visible());   // 4,2 s + salida
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);   // no deja objetos

    // 2) cola: uno a la vista, el resto espera ("+2"); el mismo titulo no se repite
    flex_notify("Aviso A", "uno");
    flex_notify("Aviso B", "dos");
    flex_notify("Aviso C", "tres");
    flex_notify("Aviso C", "tres bis");   // mismo aviso: se actualiza en la cola
    sim_run(300);
    CHK(title_is("Aviso A"));
    CHK(flex_notif_queue_len() == 2);
    sim_shot("ntf_02_cola");
    CHK(flex_notif_count() == 3);   // historial de 3: salio el mas antiguo
    CHK(strcmp(flex_notif_latest_title(), "Aviso C") == 0);
    run_until_quiet(20000);
    CHK(!flex_notif_banner_visible() && flex_notif_queue_len() == 0);

    // 2b) un titulo y un texto largos se cortan con "..." en una linea
    flex_notify("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM \xC3\x91" "and\xC3\xBA",
                "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");
    sim_run(400);
    CHK(flex_notif_banner_visible());
    CHK(banner_one_line());
    sim_shot("ntf_02b_largo");
    run_until_quiet(8000);

    // 3) deslizar el banner lo descarta; el escritorio no ve el arrastre
    lv_area_t ic0, ic1;
    int icon = page_icon(&ic0);
    CHK(icon >= 0);
    flex_notify("Deslizame", NULL);
    sim_run(400);
    CHK(title_is("Deslizame"));
    sim_drag(200, CARD_Y, 420, CARD_Y + 6, 160);
    CHK(!flex_notif_banner_visible());
    CHK(icon < 0 || (flex_home_icon_area(icon, &ic1) && same_area(&ic0, &ic1)));
    // un arrastre corto vuelve a su sitio (muelle) y sigue a la vista
    flex_notify("Muelle", NULL);
    sim_run(400);
    sim_touch(200, CARD_Y, true);
    sim_run(40);
    for (int x = 200; x <= 260; x += 6) {
        sim_touch(x, CARD_Y, true);
        sim_run(16);
    }
    sim_run(200);   // quieto: sin lanzamiento
    sim_touch(260, CARD_Y, false);
    sim_run(300);
    CHK(title_is("Muelle"));
    // tocarlo lo cierra (un aviso del sistema no abre nada)
    sim_tap(240, CARD_Y);
    sim_run(300);
    CHK(!flex_notif_banner_visible());
    CHK(flex_app_current() < 0 && flex_shell_state() == FLEX_SH_HOME);

    // 4) un toque que empieza FUERA del banner es de la pantalla de debajo
    flex_notify("No me toques", NULL);
    sim_run(400);
    sim_drag(240, 300, 240, 310, 60);
    CHK(title_is("No me toques"));

    // 5) el panel rapido le quita la pantalla: vuelve al frente al cerrarlo
    sim_drag(240, 120, 240, 600, 260);   // desde debajo del banner no se abre
    // borde superior: el apoyo quieto en y=6 (si el primer cuadro leido cayera
    // ya dentro de la tarjeta, el gesto seria del banner, como en Arduino)
    sim_touch(240, 6, true);
    sim_run(60);
    sim_drag(240, 6, 240, 560, 260);
    sim_run(300);
    CHK(flex_qs_is_open());
    CHK(!flex_notif_banner_visible());
    CHK(flex_notif_queue_len() == 1);
    flex_qs_close_now();
    sim_run(300);
    CHK(title_is("No me toques"));
    run_until_quiet(8000);

    // 6) control No molestar del panel rapido (politica real, flexphone/dnd)
    flex_qs_open();
    sim_run(500);
    uint8_t ids[32];
    int nv = flex_qs_visible_ids(ids, 32);
    bool dnd_vis = false;
    for (int i = 0; i < nv; i++) {
        dnd_vis |= ids[i] == FLEX_QS_DND;
    }
    CHK(dnd_vis || flex_qs_catalog_index(FLEX_QS_DND) >= 0);   // en el panel o en "Anadir un control"
    if (dnd_vis) {
        lv_area_t a;
        CHK(flex_qs_ctl_rect(FLEX_QS_DND, &a));
        sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
        sim_run(250);
        CHK(flex_notif_dnd());
        CHK(flex_kvs_get_bool("flexphone", "dnd", false));
        sim_shot("ntf_03_panel_dnd");
        sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
        sim_run(250);
        CHK(!flex_notif_dnd());
    }
    flex_qs_close_now();
    sim_run(100);

    // 7) en el bloqueo el banner espera y la tarjeta muestra el mas reciente
    flex_shell_lock();
    sim_run(100);
    flex_notify("Llega bloqueado", "espera");
    sim_run(600);
    CHK(!flex_notif_banner_visible());
    CHK(flex_notif_queue_len() == 1);
    CHK(strcmp(flex_notif_latest_title(), "Llega bloqueado") == 0);
    sim_shot("ntf_04_bloqueo");
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    CHK(title_is("Llega bloqueado"));
    run_until_quiet(8000);

    // 8) Centro: un roce del borde (toque) no abre nada
    sim_tap(6, 400);
    sim_run(300);
    CHK(!flex_notif_center_open());
    // un deslizamiento vertical junto al borde tampoco
    sim_drag(8, 300, 14, 600, 200);
    CHK(!flex_notif_center_open());
    // borde izquierdo: sigue al dedo y, pasada la mitad, abre del todo
    edge_to(180);
    CHK(flex_notif_center_open());
    sim_shot("ntf_05_centro_arrastre");
    sim_touch(180, 402, false);
    sim_run(400);   // < mitad: se cierra
    CHK(!flex_notif_center_open());
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);
    edge_to(320);
    sim_touch(320, 402, false);
    sim_run(400);
    CHK(flex_notif_center_open());
    CHK(flex_shell_state() == FLEX_SH_HOME);   // capa global
    sim_shot("ntf_06_centro");
    // mientras el Centro esta abierto el banner espera
    flex_notify("Con el Centro abierto", NULL);
    sim_run(400);
    CHK(!flex_notif_banner_visible() && flex_notif_queue_len() == 1);
    // tocar una fila la descarta (la primera es la mas reciente)
    int before = flex_notif_count();
    sim_run(100);
    CHK(before == 3);
    sim_tap(240, 96 + 35);
    sim_run(200);
    CHK(flex_notif_count() == before - 1);
    CHK(strcmp(flex_notif_latest_title(), "Con el Centro abierto") != 0);
    // pildora No molestar
    sim_tap(418, 47);
    sim_run(200);
    CHK(flex_notif_dnd());
    sim_shot("ntf_07_centro_dnd");
    sim_tap(418, 47);
    sim_run(200);
    CHK(!flex_notif_dnd());
    // arrastrar a la derecha desde el contenido lo cierra (como Arduino)
    sim_drag(100, 500, 260, 505, 150);
    sim_run(400);
    CHK(!flex_notif_center_open());
    sim_run(200);
    CHK(title_is("Con el Centro abierto"));   // ya puede salir
    run_until_quiet(8000);

    // 9) Borrar todas y el estado vacio
    flex_notif_center_show();
    sim_run(400);
    CHK(flex_notif_center_open());
    int n = flex_notif_count();
    CHK(n > 0);
    sim_tap(240, 96 + n * 78 + 6 + 21);
    sim_run(200);
    CHK(flex_notif_count() == 0);
    CHK(flex_notif_latest_title() == NULL);
    sim_shot("ntf_08_centro_vacio");
    // bloquear con el Centro abierto lo cierra sin dejar objetos
    flex_shell_lock();
    sim_run(100);
    CHK(!flex_notif_center_open());
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);
    sim_shot("ntf_09_bloqueo_sin_avisos");
    sim_drag(240, 700, 240, 300, 300);
    sim_run(300);

    // 10) dentro de una app: sysSay solo saca el banner; el Centro abre encima
    flex_app_open(IC_NOTAS, NULL);
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_APP);
    flex_say("Notas", "Guardado", "Nota 3");
    sim_run(400);
    CHK(title_is("Guardado"));
    CHK(flex_notif_count() == 0);   // no va al historial
    sim_shot("ntf_10_banner_app");
    run_until_quiet(8000);
    edge_to(330);
    sim_touch(330, 402, false);
    sim_run(400);
    CHK(flex_notif_center_open());
    CHK(flex_shell_state() == FLEX_SH_APP);
    flex_app_close();   // cerrar la app cierra el Centro
    sim_run(500);
    CHK(!flex_notif_center_open());
    CHK(flex_shell_state() == FLEX_SH_HOME);
    // fuera de una app sysSay si va al historial (como sysNotify)
    flex_say("Flex OS", "Desde Inicio", NULL);
    sim_run(400);
    CHK(flex_notif_count() == 1);
    run_until_quiet(8000);
    CHK(lv_obj_get_child_count(lv_layer_top()) == n_top);

    flex_shell_lock();
    sim_run(100);
    printf("avisos: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
