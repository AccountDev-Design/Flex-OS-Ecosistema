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

const sim_scene_t sim_scenes[] = {
    {"galeria", scene_gallery_run},
    {"perfil_vidrio", scene_glass_profile_run},
    {"teclado", scene_kb_run},
    {"shell", scene_shell_run},
    {"clave", scene_auth_run},
    {"energia", scene_power_run},
    {"caja", scene_drawer_run},
    {"recientes", scene_recents_run},
    {NULL, NULL},
};
