// Primera configuracion: idioma (cambia en vivo, preselecciona el guardado),
// nombre del equipo con su teclado (solo mayusculas, maximo 20, vacio -> "FlexOS
// Ultra"), fin en el bloqueo con "oobe", "lang" y "name" guardados, y el
// marcador de un restablecimiento terminado borrado al entrar.
#include <stdio.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_reset.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("oobe: FALLO linea %d: %s\n", __LINE__, #c);      \
            ok = false;                                              \
        }                                                            \
    } while (0)

void sim_reset_clear(void);
void sim_reset_set_disk(const flex_fr_marker_t *m);
const flex_fr_marker_t *sim_reset_disk(void);

static void key(char k)
{
    static const char *const R[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const int X0[3] = {13, 36, 36}, Y[3] = {544, 604, 664};
    if (k == '\b') {
        sim_tap(358 + 43, 664 + 26);
        return;
    }
    if (k == ' ') {
        sim_tap(48 + 149, 724 + 26);
        return;
    }
    if (k == '\n') {
        sim_tap(352 + 60, 724 + 26);
        return;
    }
    for (int r = 0; r < 3; r++) {
        const char *p = strchr(R[r], k);
        if (p) {
            sim_tap(X0[r] + (int)(p - R[r]) * 46 + 20, Y[r] + 26);
            return;
        }
    }
}

static void type(const char *s)
{
    for (; *s; s++) {
        key(*s);
    }
    sim_run(100);
}

bool scene_oobe_run(void)
{
    bool ok = true;
    flex_lock_clear();
    sim_reset_clear();
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    }
    int lang0 = flex_lang();
    // Tras un restablecimiento terminado: el marcador se borra al entrar
    flex_fr_marker_t done = {true, FLEX_FR_VER, FLEX_FR_DONE, 0};
    sim_reset_set_disk(&done);
    flex_reset_boot_check();
    flex_cfg_set_bool("oobe", false);
    flex_set_lang(1);   // un idioma guardado de antes: se preselecciona
    flex_navbar_set_ctx(FLEX_NAV_HOME);   // el OOBE tiene que ocultarla el mismo
    flex_oobe_start();
    sim_run(200);
    CHK(!sim_reset_disk()->pending);
    CHK(flex_oobe_active() && flex_oobe_view() == 1 && flex_shell_state() == FLEX_SH_OOBE);
    CHK(flex_navbar_ctx() == FLEX_NAV_HIDDEN);
    CHK(flex_lang() == 1);
    sim_shot("oobe_01_idioma_en");
    sim_drag(240, 6, 240, 560, 260);   // ni panel rapido
    CHK(!flex_qs_is_open());
    // fila "Espanol": el idioma cambia en vivo
    sim_tap(240, 158 + 37);
    sim_run(200);
    CHK(flex_lang() == 0);
    sim_shot("oobe_02_idioma_es");
    sim_tap(240, 158 + 2 * 88 + 37);   // Francais
    sim_run(200);
    CHK(flex_lang() == 2);
    sim_tap(240, 158 + 37);
    sim_run(200);
    sim_tap(240, 734);   // Continuar
    sim_run(300);
    CHK(flex_oobe_view() == 2);

    // nombre: no se empieza por espacio; solo mayusculas; maximo 20
    key(' ');
    type("SALON");
    CHK(strcmp(flex_oobe_name(), "SALON") == 0);
    key(' ');
    type("CASA");
    CHK(strcmp(flex_oobe_name(), "SALON CASA") == 0);
    sim_shot("oobe_03_nombre");
    // un aviso durante la configuracion espera: nunca encima del asistente
    flex_notify("Aviso en la configuracion", NULL);
    sim_run(600);
    CHK(!flex_notif_banner_visible());
    type("QWERTYUIOPASDF");
    CHK(strlen(flex_oobe_name()) == 20);
    for (int i = 0; i < 25; i++) {
        key('\b');
    }
    sim_run(100);
    CHK(flex_oobe_name()[0] == 0);
    // OK con el nombre vacio: "FlexOS Ultra"; fin en el bloqueo
    key('\n');
    sim_run(400);
    CHK(!flex_oobe_active());
    CHK(flex_shell_state() == FLEX_SH_LOCK);
    CHK(flex_cfg_get_bool("oobe", false));
    char nm[32];
    flex_cfg_get_str("name", nm, sizeof(nm), "");
    CHK(strcmp(nm, "FlexOS Ultra") == 0);
    CHK(flex_cfg_get_i32("lang", -1) == 0);

    // otra vez con nombre
    flex_cfg_set_bool("oobe", false);
    flex_oobe_start();
    sim_run(200);
    sim_tap(240, 734);
    sim_run(300);
    type("TALLER");
    key('\n');
    sim_run(400);
    flex_cfg_get_str("name", nm, sizeof(nm), "");
    CHK(strcmp(nm, "TALLER") == 0);
    CHK(flex_shell_state() == FLEX_SH_LOCK);

    // el aviso sigue en cola en el bloqueo y sale al llegar al escritorio
    CHK(!flex_notif_banner_visible() && flex_notif_queue_len() == 1);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(600);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    CHK(flex_notif_banner_visible());
    sim_run(9000);
    CHK(!flex_notif_banner_visible() && flex_notif_queue_len() == 0);

    flex_set_lang(lang0);
    flex_cfg_set_bool("oobe", true);
    sim_reset_clear();
    printf("oobe: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
