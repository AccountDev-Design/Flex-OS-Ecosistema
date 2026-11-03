// Kit de componentes compartidos (02 §7): botones, interruptor, fila-tarjeta, segmentadas, barra, deslizador,
// dialogo y aviso local, con el dedo y con lo que NO debe pasar (un boton deshabilitado no registra toque, un
// dialogo sin "Cancelar" no se cierra tocando fuera, un aviso no sobrevive a su padre).
#include <stdio.h>
#include <string.h>
#include "flex_i18n.h"
#include "flex_kit.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                    \
    do {                                                          \
        if (!(c)) {                                               \
            printf("kit: FALLO linea %d: %s\n", __LINE__, #c);    \
            ok = false;                                           \
        }                                                         \
    } while (0)

static int g_btn, g_btn_dis, g_sw_cb, g_sw_val, g_row, g_seg_cb, g_seg_val, g_sl, g_ok, g_cancel, g_dlg2;
static void on_btn(lv_obj_t *b, void *u) { (void)b; (*(int *)u)++; }
static void on_sw(lv_obj_t *s, bool on, void *u) { (void)s; (void)u; g_sw_cb++; g_sw_val = on; }
static void on_seg(lv_obj_t *s, int i, void *u) { (void)s; (void)u; g_seg_cb++; g_seg_val = i; }
static void on_sl(lv_obj_t *s, int v, void *u) { (void)s; (void)u; g_sl = v; }
static void on_ok(void *u) { (void)u; g_ok++; }
static void on_cancel(void *u) { (void)u; g_cancel++; }
static lv_obj_t *g_root;
static void on_ok_open_another(void *u)
{
    (void)u;
    g_ok++;
    flex_kit_dialog_t d = {.title = "Otro", .ok = "Listo", .on_ok = on_ok};
    flex_kit_dialog(g_root, &d);   // el callback abre un dialogo nuevo: el viejo ya esta cerrado
    g_dlg2++;
}

bool scene_kit_run(void)
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
    g_root = flex_box(flex_shell_screen());
    lv_obj_set_size(g_root, 480, 800);
    lv_obj_set_style_bg_color(g_root, flex_th()->page, 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_move_foreground(g_root);

    // ---- botones
    lv_obj_t *b1 = flex_kit_button(g_root, 16, 20, 200, 44, "Normal", FLEX_BTN_NORMAL, on_btn, &g_btn);
    lv_obj_t *b2 = flex_kit_button(g_root, 240, 20, 200, 44, "Primario", FLEX_BTN_PRIMARY, on_btn, &g_btn);
    lv_obj_t *b3 = flex_kit_button(g_root, 16, 74, 200, 44, "Peligro", FLEX_BTN_DANGER, on_btn, &g_btn);
    lv_obj_t *b4 = flex_kit_button(g_root, 240, 74, 200, 44, "Deshabilitado", FLEX_BTN_DISABLED, on_btn, &g_btn_dis);
    (void)b1; (void)b2; (void)b3;
    sim_tap(100, 42);
    CHK(g_btn == 1);
    sim_tap(300, 42);
    sim_tap(100, 96);
    CHK(g_btn == 3);
    sim_tap(300, 96);                            // deshabilitado: ni zona tactil
    CHK(g_btn_dis == 0 && !lv_obj_is_clickable(b4));
    flex_kit_button_set(b4, FLEX_BTN_NORMAL, "Habilitado");
    sim_tap(300, 96);
    CHK(g_btn_dis == 1);
    flex_kit_button_set(b4, FLEX_BTN_DISABLED, NULL);
    sim_tap(300, 96);
    CHK(g_btn_dis == 1);

    // ---- interruptor suelto
    lv_obj_t *sw = flex_kit_switch(g_root, 400, 140, 46, 26, false, on_sw, NULL);
    sim_tap(423, 153);
    CHK(g_sw_cb == 1 && g_sw_val == 1 && flex_kit_switch_get(sw));
    flex_kit_switch_set(sw, false);              // programatico: no llama al callback
    CHK(g_sw_cb == 1 && !flex_kit_switch_get(sw));
    flex_kit_switch_enable(sw, false);
    sim_tap(423, 153);
    CHK(g_sw_cb == 1 && !flex_kit_switch_get(sw));
    flex_kit_switch_enable(sw, true);

    // ---- filas-tarjeta: con valor y chevron, con interruptor, informativa
    flex_kit_row_t r1 = {.title = "Brillo", .value = "75%", .accent = lv_color_make(240, 170, 50), .tile = true, .chevron = true,
                         .sw = -1, .cb = on_btn, .user = &g_row};
    lv_obj_t *row1 = flex_kit_row(g_root, 190, &r1);
    flex_kit_row_t r2 = {.title = "Modo avion", .value = "Desactivado", .accent = lv_color_make(60, 150, 235), .tile = true, .sw = 0,
                         .sw_cb = on_sw};
    lv_obj_t *row2 = flex_kit_row(g_root, 190 + FLEX_KIT_ROW_STEP, &r2);
    flex_kit_row_t r3 = {.title = "Solo informacion", .sw = -1};
    flex_kit_row(g_root, 190 + 2 * FLEX_KIT_ROW_STEP, &r3);
    flex_kit_info_line(g_root, 190 + 3 * FLEX_KIT_ROW_STEP, "Version", "1.0");
    CHK(flex_kit_row_tile(row1) != NULL && flex_kit_row_switch(row1) == NULL && flex_kit_row_switch(row2) != NULL);
    sim_tap(240, 190 + 28);
    CHK(g_row == 1);
    flex_kit_row_set_value(row1, "100%");
    int swc = g_sw_cb;
    sim_tap(240, 190 + FLEX_KIT_ROW_STEP + 28);  // tocar la FILA conmuta su interruptor
    CHK(g_sw_cb == swc + 1 && g_sw_val == 1 && flex_kit_switch_get(flex_kit_row_switch(row2)));
    sim_tap(240, 190 + 2 * FLEX_KIT_ROW_STEP + 28);   // informativa: nada
    CHK(g_sw_cb == swc + 1);
    sim_shot("kit_componentes");

    // ---- segmentadas
    static const char *const tabs[3] = {"Hora", "Cronometro", "Temporizador"};
    lv_obj_t *seg = flex_kit_segmented(g_root, 480, tabs, 3, 0, on_seg, NULL);
    sim_run(50);
    CHK(flex_kit_segmented_get(seg) == 0);
    sim_tap(240, 498);                           // el segmento del medio
    CHK(g_seg_cb == 1 && g_seg_val == 1 && flex_kit_segmented_get(seg) == 1);
    sim_tap(240, 498);                           // el mismo: no vuelve a avisar
    CHK(g_seg_cb == 1);
    flex_kit_segmented_set(seg, 2);
    CHK(g_seg_cb == 1 && flex_kit_segmented_get(seg) == 2);
    sim_tap(60, 498);
    CHK(g_seg_cb == 2 && g_seg_val == 0);

    // ---- barra y deslizador
    lv_obj_t *bar = flex_kit_bar(g_root, 24, 540, 432, 50);
    lv_obj_update_layout(bar);
    CHK(lv_obj_get_width(lv_obj_get_child(bar, 0)) == 216);
    flex_kit_bar_set(bar, 0);
    CHK(lv_obj_is_hidden(lv_obj_get_child(bar, 0)));
    flex_kit_bar_set(bar, 130);                  // acotada
    lv_obj_update_layout(bar);
    CHK(lv_obj_get_width(lv_obj_get_child(bar, 0)) == 432);
    flex_kit_slider(g_root, 24, 570, 432, 0, 100, 10, on_sl, NULL);
    sim_drag(40, 583, 240, 583, 200);
    CHK(g_sl > 30 && g_sl < 70);
    sim_shot("kit_barras");

    // ---- dialogo de dos botones: Cancelar, Aceptar, y tocar fuera cierra como Cancelar
    flex_kit_dialog_t d = {.title = "Eliminar", .line = {"Se borrara la nota", "No se puede deshacer"}, .ok = "Eliminar",
                           .cancel = "Cancelar", .danger = true, .on_ok = on_ok, .on_cancel = on_cancel};
    lv_obj_t *dlg = flex_kit_dialog(g_root, &d);
    sim_run(50);
    sim_shot("kit_dialogo");
    int bt = g_btn;
    sim_tap(100, 96);                            // un boton DEBAJO del velo: no recibe el toque
    CHK(g_btn == bt);
    sim_tap(100, 800 / 2 + 100 - 62 + 23);       // Cancelar
    CHK(g_cancel == 1 && g_ok == 0);
    dlg = flex_kit_dialog(g_root, &d);
    sim_run(50);
    sim_tap(300, 800 / 2 + 100 - 62 + 23);       // Eliminar
    CHK(g_ok == 1 && g_cancel == 1);
    dlg = flex_kit_dialog(g_root, &d);
    sim_run(50);
    sim_tap(240, 150);                           // fuera de la tarjeta: como Cancelar
    CHK(g_cancel == 2);
    dlg = flex_kit_dialog(g_root, &d);
    sim_run(50);
    sim_tap(240, 400);                           // dentro de la tarjeta, sin boton: no cierra
    CHK(g_cancel == 2 && g_ok == 1);
    flex_kit_dialog_close(dlg);

    // ---- un dialogo sin "Cancelar" no se cierra tocando fuera; el callback puede abrir otro
    flex_kit_dialog_t d2 = {.title = "Aviso", .line = {"Hecho"}, .ok = "Entendido", .on_ok = on_ok_open_another};
    flex_kit_dialog(g_root, &d2);
    sim_run(50);
    sim_tap(240, 150);
    CHK(g_ok == 1 && g_dlg2 == 0);
    sim_tap(240, 800 / 2 + 100 - 62 + 23);
    CHK(g_ok == 2 && g_dlg2 == 1);               // abrio el segundo (el primero ya estaba cerrado)
    sim_tap(240, 800 / 2 + 100 - 62 + 23);
    CHK(g_ok == 3);

    // ---- aviso local: uno a la vez, 1800 ms, y no sobrevive a su padre
    flex_kit_toast(g_root, "Guardado");
    flex_kit_toast(g_root, "Sin espacio para guardar");
    int toasts = flex_kit_toast_count(g_root);
    CHK(toasts == 1);
    sim_run(1000);
    CHK(lv_obj_get_child_count(g_root) > 0);
    sim_run(1000);
    toasts = flex_kit_toast_count(g_root);
    CHK(toasts == 0);
    flex_kit_toast(g_root, "Se borra con su padre");
    lv_obj_delete(g_root);                       // el aviso (y su temporizador) se van con el padre
    g_root = NULL;
    sim_run(2500);                               // sin objeto colgante: no hay fallo al vencer el temporizador

    flex_shell_lock();
    sim_run(200);
    printf("kit de componentes: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
