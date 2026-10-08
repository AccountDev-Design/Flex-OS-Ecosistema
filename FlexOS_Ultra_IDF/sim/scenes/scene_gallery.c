// Escena de prueba del sistema de diseno: tipografia con tildes, superficies
// (plano y Liquid Glass sobre el fondo y sobre color liso) e iconos de app.
#include <stdio.h>
#include "flex_glass.h"
#include "flex_icons.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

static void build(bool glass, bool dark, int wall, int icon_style)
{
    flex_cfg_set_bool("glass", glass);
    flex_cfg_set_bool("dark", dark);
    flex_cfg_set_i32("wallh", wall);
    flex_cfg_set_i32("iconstyle", icon_style);
    flex_theme_init();
    flex_wallmgr_init();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, flex_th()->page, 0);
    lv_obj_t *wall_img = lv_image_create(scr);
    lv_image_set_src(wall_img, flex_wallmgr_image(FLEX_WALL_HOME));
    lv_obj_set_pos(wall_img, 0, 0);

    lv_obj_t *t = flex_label(scr, "Liquid Glass · Ñandú, camión, ¿qué tal? ¡Sí!", FLEX_FONT_S2, FLEX_ONWALL);
    lv_obj_set_pos(t, 16, 16);
    lv_obj_t *t2 = flex_label(scr, "12:45", FLEX_FONT_S6, FLEX_ONWALL);
    lv_obj_set_pos(t2, 16, 40);
    lv_obj_t *t3 = flex_label(scr, "BNO085 ● Conectado   AHRS ● Activo", FLEX_FONT_S2, FLEX_ONWALL);
    lv_obj_set_pos(t3, 16, 92);

    // Iconos: 4 columnas como el escritorio
    for (int id = 0; id < FLEX_APP_N; id++) {
        int col = id % 4, row = id / 4;
        lv_obj_t *ic = flex_app_icon_create(scr, id, 72, FLEX_BD_HOME);
        lv_obj_set_pos(ic, 28 + col * 112, 130 + row * 104);
    }
    // Panel de vidrio sobre el fondo (widget)
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 448, 120);
    lv_obj_set_pos(card, 16, 660);
    lv_obj_set_style_radius(card, 24, 0);
    flex_surface(card, FLEX_SURF_WALL, FLEX_BD_HOME);
    lv_obj_t *ct = flex_label(card, "Tarjeta sobre el fondo", FLEX_FONT_S3, FLEX_ONWALL);
    lv_obj_set_pos(ct, 20, 16);
    lv_obj_t *cs = flex_label(card, "Material: el fondo desenfocado + tinte + luz", FLEX_FONT_S1, FLEX_ONWALL2);
    lv_obj_set_pos(cs, 20, 52);

    lv_screen_load(scr);
}

bool scene_gallery_run(void)
{
    build(false, true, 0, 0);
    sim_run(100);
    sim_shot("gal_01_plano_oscuro");
    build(true, true, 0, 1);
    sim_run(100);
    sim_shot("gal_02_vidrio_oscuro");
    build(true, false, 1, 1);
    sim_run(100);
    sim_shot("gal_03_vidrio_aurora");
    build(true, true, 3, 0);
    sim_run(100);
    sim_shot("gal_04_halo_iconos_planos");
    return true;
}

