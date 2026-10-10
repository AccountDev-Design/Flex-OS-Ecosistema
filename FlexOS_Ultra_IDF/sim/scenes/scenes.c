// Lista de escenas del simulador de la interfaz.
#include <stddef.h>
#include "ui_sim.h"

bool scene_gallery_run(void);
bool scene_glass_profile_run(void);
bool scene_shell_run(void);
bool scene_kb_run(void);
bool scene_auth_run(void);
bool scene_power_run(void);
bool scene_drawer_run(void);
bool scene_recents_run(void);
bool scene_qs_run(void);
bool scene_notif_run(void);
bool scene_poweroff_run(void);
bool scene_safe_run(void);
bool scene_regress_run(void);
bool scene_factory_run(void);
bool scene_oobe_run(void);
bool scene_widgets_run(void);
bool scene_kiosk_run(void);

const sim_scene_t sim_scenes[] = {
    {"galeria", scene_gallery_run},
    {"perfil_vidrio", scene_glass_profile_run},
    {"teclado", scene_kb_run},
    {"shell", scene_shell_run},
    {"clave", scene_auth_run},
    {"energia", scene_power_run},
    {"caja", scene_drawer_run},
    {"recientes", scene_recents_run},
    {"panel", scene_qs_run},
    {"avisos", scene_notif_run},
    {"seguro", scene_safe_run},
    {"regresiones", scene_regress_run},
    {"fabrica", scene_factory_run},
    {"oobe", scene_oobe_run},
    {"widgets", scene_widgets_run},
    {"kiosco", scene_kiosk_run},
    {"apagado", scene_poweroff_run},
    {NULL, NULL},
};
