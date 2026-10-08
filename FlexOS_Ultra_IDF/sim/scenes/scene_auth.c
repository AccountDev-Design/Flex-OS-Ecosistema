// Clave del sistema: verificar desde el bloqueo (fallo, sacudida, espera
// progresiva, acierto con revelado), cancelar, cortar al bloquear y crear un PIN.
#include <stdio.h>
#include "flex_auth.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("auth: FALLO linea %d: %s\n", __LINE__, #c);      \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void key(char d)
{
    int i = d == '<' ? 9 : d == '0' ? 10 : d == 'K' ? 11 : d - '1';
    int c = i % 3, r = i / 3;
    sim_tap(30 + c * 144 + 66, 300 + r * 94 + 41);
}

// Pulsa y suelta una tecla y deja correr solo ms (para capturar a mitad de animacion)
static void key_quick(char d, uint32_t ms)
{
    int i = d == '<' ? 9 : d == '0' ? 10 : d == 'K' ? 11 : d - '1';
    int c = i % 3, r = i / 3;
    sim_touch(30 + c * 144 + 66, 300 + r * 94 + 41, true);
    sim_run(40);
    sim_touch(30 + c * 144 + 66, 300 + r * 94 + 41, false);
    sim_run(ms);
}

static void type(const char *s)
{
    for (; *s; s++) {
        key(*s);
    }
}

static void open_verify(void)
{
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 400, 200);
    sim_run(600);   // fundidos 190 + 230 ms
}

static bool s_setup_done;
static void setup_done(void *ctx)
{
    (void)ctx;
    s_setup_done = true;
}

bool scene_auth_run(void)
{
    bool ok = true;
    flex_cfg_set_bool("glass", true);
    flex_cfg_set_bool("dark", true);
    flex_cfg_set_i32("lockwidgets", 1);
    flex_lock_set_fails(0);
    CHK(flex_lock_set("2580", FLEX_LOCK_PIN));
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    } else {
        flex_theme_set_glass(true);
        flex_theme_set_dark(true);
    }

    // 1) desde el bloqueo, el desliz pide la clave y NO revela el escritorio
    open_verify();
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    CHK(flex_auth_active());
    sim_shot("au_01_pin_vidrio");

    // 2) PIN incorrecto (autoconfirma a los 4 digitos): sacudida, sigue bloqueado
    type("123");
    sim_shot("au_02_tres_digitos");
    key_quick('4', 70);
    sim_shot("au_03_sacudida");
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_AUTH);
    CHK(flex_lock_fails() == 1);

    // 3) PIN correcto: revelado de 400 ms y escritorio
    type("258");
    key_quick('0', 200);
    sim_shot("au_04_revelado");
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    CHK(!flex_auth_active());
    CHK(flex_lock_fails() == 0);

    // 4) cuatro fallos -> espera de 30 s; el teclado queda inerte
    open_verify();
    for (int i = 0; i < 4; i++) {
        type("0000");
        sim_run(400);
    }
    CHK(flex_lock_fails() == 4);
    CHK(flex_auth_wait_left_ms() > 25000);
    sim_shot("au_05_espera");
    type("2580");
    sim_run(400);
    CHK(flex_shell_state() == FLEX_SH_AUTH);   // durante la espera no se puede probar
    sim_run(31000);
    CHK(flex_auth_wait_left_ms() == 0);
    type("2580");
    sim_run(800);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    CHK(flex_lock_fails() == 0);

    // 5) la flecha atras vuelve SIEMPRE al bloqueo
    open_verify();
    type("25");
    sim_tap(24, 20);
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_LOCK);
    CHK(!flex_auth_active());

    // 6) bloquear con una verificacion abierta la corta
    open_verify();
    CHK(flex_auth_active());
    flex_shell_lock();
    sim_run(100);
    CHK(!flex_auth_active());
    CHK(flex_shell_state() == FLEX_SH_LOCK);

    // 7) Plano y claro (los colores sobre el wallpaper no cambian con el tema)
    flex_theme_set_glass(false);
    flex_theme_set_dark(false);
    open_verify();
    sim_shot("au_06_pin_plano_claro");
    sim_tap(24, 20);
    sim_run(300);

    // 8) crear PIN desde Ajustes (fondo de pagina, paleta del tema)
    s_setup_done = false;
    flex_auth_setup(setup_done, NULL);
    sim_run(100);
    sim_shot("au_07_selector");
    sim_tap(240, 280);
    sim_run(200);
    sim_shot("au_08_crear_pin");
    type("135");
    key('K');   // con 3 digitos no guarda
    sim_run(100);
    CHK(!s_setup_done);
    type("7");
    key('K');
    sim_run(200);
    CHK(s_setup_done);
    CHK(flex_lock_verify_alone("1357"));
    CHK(!flex_lock_verify_alone("2580"));
    flex_theme_set_glass(true);
    flex_theme_set_dark(true);
    flex_shell_lock();
    sim_run(100);

    printf("auth: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
