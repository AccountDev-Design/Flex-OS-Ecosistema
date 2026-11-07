// Ajustes del teclado (A11k): la pantalla real en el simulador. Se llega desde Ajustes > Personalizacion > Teclado y desde el engranaje del teclado de
// Notas; las 8 paginas, cada fila guarda SU clave, los atajos se editan con el teclado del sistema, "atras" va de pagina en pagina, Inicio la cierra y
// quien la abrio se entera al salir.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_i18n.h"
#include "flex_kb.h"
#include "flex_kb_layout.h"
#include "flex_kbs_model.h"
#include "flex_kit.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void sim_disk_enable(bool on);
void flex_session_forget_loaded(int id);

int flex_app_settings_view(void);
bool flex_app_settings_animating(void);
bool flex_app_settings_card_xy(int i, int *x, int *y);
bool flex_app_settings_row_xy(int i, int *x, int *y);
bool flex_app_settings_shown(const char *txt);
lv_obj_t *flex_app_settings_root(void);

int flex_kbs_page(void);
bool flex_kbs_active(void);
bool flex_kbs_animating(void);
int flex_kbs_rows(void);
int flex_kbs_scroll(void);
lv_obj_t *flex_kbs_kb(void);
const flex_kbs_t *flex_kbs_state(void);
bool flex_kbs_row_xy(int i, int *x, int *y);
bool flex_kbs_shown(const char *txt);

int flex_app_notes_view(void);
bool flex_app_notes_ready(void);
lv_obj_t *flex_app_notes_kb(void);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("teclado: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)

static void tap_card(int cat)
{
    int x, y;
    for (int k = 0; k < 4; k++) {
        if (!flex_app_settings_card_xy(cat, &x, &y)) break;
        if (y < 135) sim_drag(240, 200, 240, 600, 200);
        else if (y > 700) sim_drag(240, 600, 240, 200, 200);
        else break;
        sim_run(100);
    }
    flex_app_settings_card_xy(cat, &x, &y);
    sim_touch(x, y, true);
    sim_run(30);
    sim_touch(x, y, false);
}
static void settings_row(int i)
{
    int x, y;
    if (flex_app_settings_row_xy(i, &x, &y)) {
        sim_tap(x, y);
        sim_run(150);
    }
}
static void krow(int i)
{
    int x, y;
    for (int k = 0; k < 4; k++) {   // lleva la fila a la banda visible con un arrastre
        if (!flex_kbs_row_xy(i, &x, &y)) break;
        if (y < 125) sim_drag(240, 250, 240, 650, 200);
        else if (y > 750) sim_drag(240, 650, 240, 250, 200);
        else break;
        sim_run(100);
    }
    if (flex_kbs_row_xy(i, &x, &y)) {
        sim_tap(x, y);
        sim_run(200);   // el repintado o los 90 ms de la transicion
    }
}
static void kback(void)
{
    sim_tap(24, 40);
    sim_run(200);
}
static bool shown(const char *t)
{
    return flex_kbs_shown(t);
}
// una tecla del teclado del editor por su etiqueta base en la capa activa
static void kkey(const char *base)
{
    lv_obj_t *kb = flex_kbs_kb();
    flex_kb_state_t st;
    flex_kb_state_init(&st, true);
    flex_kb_set_layout_state(&st, flex_kb_get_layout(kb));
    const flex_kb_geom_t *g = flex_kb_geom(kb);
    for (int c = 0; c < FLEX_KB_CELLS; c++) {
        if (!strcmp(flex_kb_key_base(&st, c), base)) {
            int x, y;
            flex_kb_cell_xy(g, c, &x, &y);
            sim_tap(x + g->kw / 2, y + g->kh / 2);
            sim_run(40);
            return;
        }
    }
}
static void ktype(const char *s)
{
    lv_obj_t *kb = flex_kbs_kb();
    const flex_kb_geom_t *g = flex_kb_geom(kb);
    for (; *s; s++) {
        if (*s == ' ') {
            sim_tap(flex_kb_fkey_x(g, FLEX_KB_FN_SPACE) + flex_kb_fkey_w(g, FLEX_KB_FN_SPACE) / 2, flex_kb_func_y(g) + g->kh / 2);
            sim_run(40);
        } else {
            char one[2] = {*s, 0};
            kkey(one);
        }
    }
}
static int kb_pref_i32(const char *k, int d)
{
    return (int)flex_cfg_get_i32(k, d);
}

bool scene_kbsettings_run(void)
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
    flex_theme_set_anim_style(0);
    {
        flex_kb_prefs_t d;
        flex_kb_prefs_defaults(&d);
        flex_kb_prefs_save(&d);
    }

    // ---- 1) Ajustes > Personalizacion > Teclado abre la pantalla (la fila ya no avisa "Proximamente")
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(500);
    tap_card(5);
    sim_run(400);
    CHK(flex_app_settings_view() == 1 && flex_app_settings_shown("Normal"));
    settings_row(3);
    sim_run(300);
    CHK(flex_kbs_active() && flex_kbs_page() == FLEX_KBS_MAIN && flex_kit_toast_count(flex_app_settings_root()) == 0);
    sim_shot("kbs_principal");
    CHK(shown("Idiomas y tipos") && shown("Espa\xC3\xB1ol (ES) - activo") && shown("Texto predictivo"));
    CHK(shown("Activado - el panel ha dado 1 dedo a la vez"));
    CHK(shown("Entrada de voz") && shown("Necesita reconocimiento en la nube"));

    // ---- 2) las filas de interruptor guardan SU clave y cambian su valor en pantalla
    CHK(flex_cfg_get_bool("kbpred", false));
    krow(1);
    CHK(!flex_cfg_get_bool("kbpred", true) && flex_cfg_get_bool("kbspell", true) == false);
    krow(2);
    CHK(flex_cfg_get_bool("kbspell", false));
    krow(3);
    CHK(flex_cfg_get_bool("kbemoji", false));
    krow(5);
    CHK(!flex_cfg_get_bool("kbtool", true));
    krow(6);
    CHK(flex_cfg_get_bool("kbhicon", false));
    krow(7);
    CHK(!flex_cfg_get_bool("kbfast", true) && shown("Desactivado - se escribe al soltar"));
    krow(7);
    CHK(flex_cfg_get_bool("kbfast", false));
    // las dos filas apagadas por hardware no hacen nada
    {
        const flex_kbs_t *st = flex_kbs_state();
        flex_kbs_t before = *st;
        krow(10);
        krow(11);
        CHK(flex_kbs_page() == FLEX_KBS_MAIN && !memcmp(&before.pr, &flex_kbs_state()->pr, sizeof(before.pr)));
    }
    sim_shot("kbs_principal_cambios");

    // ---- 3) Idiomas y tipos: el idioma activo
    krow(0);
    CHK(flex_kbs_page() == FLEX_KBS_LANG && shown("Activo") && shown("Toca para activar"));
    krow(1);
    CHK(flex_kbs_state()->lang_es == false);
    kback();
    CHK(flex_kbs_page() == FLEX_KBS_MAIN && shown("English (EN) - activo"));
    krow(0);
    krow(0);                                                       // "Espanol" otra vez
    CHK(flex_kbs_state()->lang_es == true);
    kback();

    // ---- 4) Diseno y tamano: tamano, opacidad, diseno, fuente y simbolos
    krow(8);
    CHK(flex_kbs_page() == FLEX_KBS_DESIGN && shown("100% de opacidad") && shown("Normal") && shown("Redondeada"));
    krow(0);
    CHK(kb_pref_i32("kbsize", -1) == 2 && shown("Grande"));       // normal -> grande
    krow(0);
    CHK(kb_pref_i32("kbsize", -1) == 0 && shown("Compacto"));     // -> compacto
    krow(0);
    CHK(kb_pref_i32("kbsize", -1) == 1);
    krow(1);
    CHK(kb_pref_i32("kbopa", -1) == 85 && shown("85% de opacidad"));
    krow(1);
    krow(1);
    krow(1);
    CHK(kb_pref_i32("kbopa", -1) == 40);
    krow(1);
    CHK(kb_pref_i32("kbopa", -1) == 100);                          // 40 - 15 < 40: vuelve a 100
    krow(2);
    CHK(kb_pref_i32("kbstyle", -1) == 1 && shown("Cuadrada"));
    krow(2);
    CHK(shown("Contorno"));
    krow(2);
    CHK(kb_pref_i32("kbstyle", -1) == 0);
    krow(3);
    CHK(kb_pref_i32("kbfont", -1) == 2 && shown("Grande"));
    krow(3);
    krow(3);
    CHK(kb_pref_i32("kbfont", -1) == 1);
    sim_shot("kbs_diseno");
    krow(4);                                                       // simbolos personalizados
    CHK(flex_kbs_page() == FLEX_KBS_SYMS && shown("S\xC3\xADmbolo 1") && shown("@"));
    krow(0);
    CHK(shown("#") && flex_kbs_state()->pr.sym[0] == 1);
    {
        uint8_t sb[4];
        CHK(flex_cfg_get_blob("kbsyms", sb, sizeof(sb)) == 4 && sb[0] == 1 && sb[1] == 1 && sb[2] == 2 && sb[3] == 3);
    }
    krow(3);
    krow(3);
    CHK(flex_kbs_state()->pr.sym[3] == 5);
    kback();                                                       // de simbolos a DISENO, no a la principal
    CHK(flex_kbs_page() == FLEX_KBS_DESIGN);
    kback();
    CHK(flex_kbs_page() == FLEX_KBS_MAIN);

    // ---- 5) Deslizar y tocar: destello y pulsacion larga
    krow(9);
    CHK(flex_kbs_page() == FLEX_KBS_TOUCH && shown("Normal") && shown("500 ms"));
    krow(0);
    CHK(kb_pref_i32("kbfx", -1) == 160 && shown("Larga"));
    krow(0);
    CHK(kb_pref_i32("kbfx", -1) == 60 && shown("Corta"));
    krow(0);
    CHK(kb_pref_i32("kbfx", -1) == 100);
    krow(1);
    CHK(kb_pref_i32("kblp", -1) == 700 && shown("700 ms"));
    krow(1);
    CHK(kb_pref_i32("kblp", -1) == 350);
    krow(1);
    CHK(kb_pref_i32("kblp", -1) == 500);
    krow(2);                                                       // respuesta haptica: no hay motor
    CHK(flex_kbs_page() == FLEX_KBS_TOUCH && shown("La placa no tiene motor vibrador"));
    kback();

    // ---- 6) Atajos de texto: la lista, el editor con el teclado del sistema, Guardar, OK y Borrar
    krow(4);
    CHK(flex_kbs_page() == FLEX_KBS_SHORT && shown("xq -> porque") && shown("Vacio - toca para crear"));
    krow(4);                                                       // "Atajo 5": vacio
    CHK(flex_kbs_page() == FLEX_KBS_SCEDIT && flex_kbs_kb() != NULL && flex_kbs_state()->sc_sel == 4 && flex_kbs_state()->sc_field == 0);
    sim_shot("kbs_editor");
    ktype("hola");
    CHK(!strcmp(flex_kbs_state()->sc_a, "hola") && shown("hola"));
    sim_tap(240, FLEX_KBS_TOP + 76 + 40);                          // el campo de la expansion
    sim_run(100);
    CHK(flex_kbs_state()->sc_field == 1);
    ktype("buenos dias");
    CHK(!strcmp(flex_kbs_state()->sc_e, "buenos dias"));
    {
        lv_obj_t *kb = flex_kbs_kb();
        const flex_kb_geom_t *g = flex_kb_geom(kb);
        sim_tap(flex_kb_fkey_x(g, FLEX_KB_FN_BACKSPACE) + flex_kb_fkey_w(g, FLEX_KB_FN_BACKSPACE) / 2, flex_kb_func_y(g) + g->kh / 2);
        sim_run(60);
    }
    CHK(!strcmp(flex_kbs_state()->sc_e, "buenos dia"));            // "<-" borra un caracter
    ktype("s");
    sim_tap(120, FLEX_KBS_TOP + 160 + 20);                         // Guardar
    sim_run(300);
    CHK(flex_kbs_page() == FLEX_KBS_SHORT && shown("hola -> buenos dias"));
    {
        char abr[80], exp[200];
        CHK(flex_cfg_get_blob("kbscabr", abr, sizeof(abr)) == 80 && !strcmp(abr + 4 * 10, "hola"));
        CHK(flex_cfg_get_blob("kbscexp", exp, sizeof(exp)) == 192 && !strcmp(exp + 4 * 24, "buenos dias"));
    }
    // el idioma que se deja en la tecla ES/EN del teclado del editor es el activo al volver (kbLangEs es global en Arduino)
    krow(4);
    {
        lv_obj_t *kb = flex_kbs_kb();
        const flex_kb_geom_t *g = flex_kb_geom(kb);
        sim_tap(flex_kb_fkey_x(g, FLEX_KB_FN_LANG) + flex_kb_fkey_w(g, FLEX_KB_FN_LANG) / 2, flex_kb_func_y(g) + g->kh / 2);
        sim_run(100);
        CHK(!flex_kb_get_lang_es(flex_kbs_kb()));
    }
    kback();                                                       // de vuelta a la lista de atajos
    kback();                                                       // y a la principal
    CHK(flex_kbs_page() == FLEX_KBS_MAIN && shown("English (EN) - activo"));
    krow(0);
    krow(0);                                                       // de nuevo espanol
    kback();
    krow(4);
    // se puede volver a editar y Borrar lo deja vacio
    krow(4);
    CHK(flex_kbs_page() == FLEX_KBS_SCEDIT && !strcmp(flex_kbs_state()->sc_a, "hola"));
    sim_tap(360, FLEX_KBS_TOP + 160 + 20);                         // Borrar
    sim_run(300);
    CHK(flex_kbs_page() == FLEX_KBS_SHORT && !shown("hola -> buenos dias") && shown("Vacio - toca para crear"));
    // la tecla OK con los dos textos guarda y vuelve; con uno solo no hace nada
    krow(4);
    ktype("a");
    {
        lv_obj_t *kb = flex_kbs_kb();
        const flex_kb_geom_t *g = flex_kb_geom(kb);
        int ok_x = flex_kb_fkey_x(g, FLEX_KB_FKEYS - 1) + flex_kb_fkey_w(g, FLEX_KB_FKEYS - 1) / 2, ok_y = flex_kb_func_y(g) + g->kh / 2;
        sim_tap(ok_x, ok_y);
        sim_run(200);
        CHK(flex_kbs_page() == FLEX_KBS_SCEDIT);                   // falta la expansion
        sim_tap(240, FLEX_KBS_TOP + 76 + 40);
        sim_run(100);
        ktype("b");
        sim_tap(ok_x, ok_y);
        sim_run(300);
        CHK(flex_kbs_page() == FLEX_KBS_SHORT && shown("a -> b"));
    }
    kback();

    // ---- 7) Sobre teclado: lo que el teclado realmente tiene
    krow(13);
    CHK(flex_kbs_page() == FLEX_KBS_ABOUT && shown("Version 1.0"));
    {
        char want[48];
        snprintf(want, sizeof(want), "%d palabras ES / %d EN", flex_kb_dict_n(true), flex_kb_dict_n(false));
        CHK(shown(want));
    }
    kback();

    // ---- 8) las tres transiciones (zoom, fundido, deslizar) duran 90 ms y dejan la pagina en su sitio
    for (int style = 0; style < 3; style++) {
        flex_theme_set_anim_style((uint8_t)style);
        krow(8);
        CHK(flex_kbs_page() == FLEX_KBS_DESIGN);
        kback();
        sim_run(100);
        CHK(!flex_kbs_animating() && flex_kbs_page() == FLEX_KBS_MAIN);
    }
    {
        flex_theme_set_anim_style(2);
        int x, y;
        CHK(flex_kbs_row_xy(8, &x, &y));
        sim_touch(x, y, true);
        sim_run(30);
        sim_touch(x, y, false);
        sim_run(20);
        CHK(flex_kbs_animating());                                 // 90 ms: aun en marcha
        flex_sys_back();                                           // "atras" durante la transicion se descarta
        CHK(flex_kbs_page() == FLEX_KBS_DESIGN);
        sim_run(200);
        CHK(!flex_kbs_animating());
        kback();
        flex_theme_set_anim_style(0);
    }

    // un toque en una fila mientras dura la transicion se descarta (en Arduino el bucle de la transicion es bloqueante)
    {
        flex_theme_set_anim_style(1);                              // fundido: la pagina nueva no se mueve y el toque cae donde esta la fila
        krow(8);                                                   // a Diseno y tamano: 90 ms de transicion
        CHK(flex_kbs_page() == FLEX_KBS_DESIGN);
        int sz = kb_pref_i32("kbsize", -1);
        int x, y;
        krow(8);
        kback();
        CHK(flex_kbs_row_xy(8, &x, &y));
        sim_touch(x, y, true);
        sim_run(25);
        sim_touch(x, y, false);                                    // la transicion arranca al soltar
        sim_run(20);
        CHK(flex_kbs_animating() && flex_kbs_page() == FLEX_KBS_DESIGN);
        CHK(flex_kbs_row_xy(0, &x, &y));
        sim_touch(x, y, true);
        sim_run(15);
        sim_touch(x, y, false);                                    // ...y este toque en la fila del tamano llega a mitad de ella
        sim_run(250);
        CHK(kb_pref_i32("kbsize", -1) == sz);
        kback();
        flex_theme_set_anim_style(0);
    }

    // ---- 9) Restablecer: todo vuelve a su valor de fabrica, atajos incluidos
    krow(1);
    krow(7);
    krow(8);
    krow(0);
    kback();
    CHK(kb_pref_i32("kbsize", -1) == 2 && flex_cfg_get_bool("kbpred", false) && !flex_cfg_get_bool("kbfast", true));
    krow(12);
    CHK(flex_kbs_scroll() > 0);                                    // repintar la pagina no devuelve el scroll arriba
    CHK(kb_pref_i32("kbsize", -1) == 1 && !flex_cfg_get_bool("kbpred", false) == false && flex_cfg_get_bool("kbfast", false) && flex_cfg_get_bool("kbtool", false));
    CHK(!flex_cfg_get_bool("kbspell", true) && !flex_cfg_get_bool("kbemoji", true) && !flex_cfg_get_bool("kbhicon", true) && kb_pref_i32("kbopa", -1) == 100);
    {
        char abr[80];
        CHK(flex_cfg_get_blob("kbscabr", abr, sizeof(abr)) == 80 && !strcmp(abr, "xq") && !abr[4 * 10]);   // los 4 de fabrica y los demas vacios
    }
    CHK(flex_kbs_page() == FLEX_KBS_MAIN);

    // ---- 10) "atras" desde la principal sale de la pantalla y Ajustes se entera: el valor del tamano ya es otro
    krow(8);
    krow(0);                                                       // normal -> grande
    kback();
    CHK(flex_kbs_page() == FLEX_KBS_MAIN);
    flex_sys_back();
    sim_run(300);
    CHK(!flex_kbs_active() && flex_app_settings_view() == 1);
    CHK(flex_app_settings_shown("Grande") && !flex_app_settings_shown("Normal"));
    // Inicio la cierra del todo (con la app Ajustes debajo)
    settings_row(3);
    sim_run(300);
    CHK(flex_kbs_active());
    flex_sys_home();
    sim_run(400);
    CHK(!flex_kbs_active() && flex_app_current() == -1);
    flex_cfg_set_i32("kbsize", 1);

    // ---- 11) desde el engranaje del teclado de Notas: al salir el teclado de la nota lee sus ajustes otra vez
    {
        flex_app_open(IC_NOTAS, NULL);
        sim_run(500);
        for (int i = 0; i < 80 && !(flex_app_notes_view() == 0 && flex_app_notes_ready()); i++) sim_run(20);
        sim_tap(396, 650);                                         // nota nueva
        for (int i = 0; i < 80 && flex_app_notes_view() != 1; i++) sim_run(20);
        sim_run(500);
        lv_obj_t *kb = flex_app_notes_kb();
        CHK(kb != NULL);
        int h0 = (int)flex_kb_height(kb);
        const flex_kb_geom_t *g = flex_kb_geom(kb);
        sim_tap(48 + 96 * FLEX_KB_TB_SET, flex_kb_toolbar_y(g) + 26);   // el engranaje
        sim_run(300);
        CHK(flex_kbs_active() && flex_kbs_page() == FLEX_KBS_MAIN);
        krow(8);
        krow(0);                                                   // grande
        kback();
        flex_sys_back();
        sim_run(300);
        CHK(!flex_kbs_active() && flex_app_notes_view() == 1);
        CHK((int)flex_kb_height(flex_app_notes_kb()) > h0);        // el teclado de la nota es ahora grande
        flex_cfg_set_i32("kbsize", 1);
        flex_app_terminate_ex(IC_NOTAS, true);
        sim_run(300);
    }

    flex_kb_prefs_t d;
    flex_kb_prefs_defaults(&d);
    flex_kb_prefs_save(&d);
    return ok;
}
