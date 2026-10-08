// Comprueba el perfil vertical del material de vidrio sobre un color liso
// contra la formula de la version Arduino (especular hasta el 45 %, sombreado
// desde el 45 %): no debe haber saltos de brillo entre filas vecinas.
#include <stdio.h>
#include <stdlib.h>
#include "flex_glass.h"
#include "flex_glass_math.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "lvgl.h"
#include "ui_sim.h"

extern uint16_t *sim_fb(void);

bool scene_glass_profile_run(void)
{
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_theme_init();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, flex_th()->page, 0);
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 400, 300);
    lv_obj_set_pos(p, 40, 200);
    lv_obj_set_style_radius(p, 20, 0);
    flex_surface(p, FLEX_SURF_CARD, FLEX_BD_FLAT);
    lv_screen_load(scr);
    sim_run(50);
    sim_shot("glass_profile");
    // Entre filas vecinas solo puede cambiar 1 escalon de RGB565 por canal (el
    // material es una rampa lineal; un salto mayor seria una costura).
    const uint16_t *fb = sim_fb();
    int bad = 0;
    for (int y = 206; y < 494; y++) {
        uint16_t a = fb[(y - 1) * 480 + 240], b = fb[y * 480 + 240];
        int dr = abs(((a >> 11) & 31) - ((b >> 11) & 31)), dg = abs(((a >> 5) & 63) - ((b >> 5) & 63));
        int db = abs((a & 31) - (b & 31));
        if (dr > 1 || dg > 2 || db > 1) {   // verde: LVGL mezcla con 8 bits y puede saltar 2 escalones de 6 bits
            printf("costura en la fila %d\n", y);
            bad++;
        }
    }
    printf("perfil del vidrio: %d costuras\n", bad);
    return bad == 0;
}
