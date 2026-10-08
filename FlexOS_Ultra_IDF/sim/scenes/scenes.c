// Lista de escenas del simulador de la interfaz.
#include <stddef.h>
#include "ui_sim.h"

bool scene_gallery_run(void);
bool scene_glass_profile_run(void);
bool scene_shell_run(void);
bool scene_auth_run(void);

const sim_scene_t sim_scenes[] = {
    {"galeria", scene_gallery_run},
    {"perfil_vidrio", scene_glass_profile_run},
    {"shell", scene_shell_run},
    {"clave", scene_auth_run},
    {NULL, NULL},
};
