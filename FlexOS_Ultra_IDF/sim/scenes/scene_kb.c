// Teclado del sistema (flex_kb): capturas en cada material/apariencia/capa y
// una prueba de escritura real con toques simulados ("Contrasena 1" con n con
// tilde, mayuscula, espacio y capa ?123) comprobando el texto que llega por los
// callbacks.
#include <stdio.h>
#include <string.h>
#include "flex_kb.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

static char s_text[64];
static int s_enter, s_back;
static lv_obj_t *s_scr, *s_kb, *s_lbl;

static void show_text(void)
{
    if (s_lbl) {
        char t[96];
        snprintf(t, sizeof(t), "Texto: %s|", s_text);
        lv_label_set_text(s_lbl, t);
    }
}

static void on_text(lv_obj_t *kb, const char *utf8, void *user)
{
    (void)kb;
    (void)user;
    flex_kb_buf_append(s_text, sizeof(s_text), utf8);
    show_text();
}

static void on_back(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    s_back++;
    flex_kb_buf_backspace(s_text);
    show_text();
}

static void on_enter(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    s_enter++;
}

typedef struct {
    bool glass, dark, wall;
    uint32_t flags;
    int size, style, font;
    bool hicon, fast;
    int opacity;
    const char *enter;
} look_t;

static void build(const look_t *l, const char *title)
{
    flex_cfg_set_bool("glass", l->glass);
    flex_cfg_set_bool("dark", l->dark);
    flex_cfg_set_i32("kbsize", l->size);
    flex_cfg_set_i32("kbstyle", l->style);
    flex_cfg_set_i32("kbfont", l->font);
    flex_cfg_set_bool("kbhicon", l->hicon);
    flex_cfg_set_bool("kbfast", l->fast);
    flex_cfg_set_i32("kbopa", l->opacity ? l->opacity : 100);
    flex_theme_init();
    flex_wallmgr_init();

    lv_obj_t *old = s_scr;
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_scrollable(s_scr, false);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scr, flex_th()->page, 0);
    lv_color_t fg = flex_th()->txt;
    if (l->wall) {
        lv_obj_t *w = lv_image_create(s_scr);
        lv_image_set_src(w, flex_wallmgr_image(FLEX_WALL_LOCK));
        lv_obj_set_pos(w, 0, 0);
        fg = FLEX_ONWALL;
    }
    lv_obj_t *t = flex_label(s_scr, title, FLEX_FONT_S3, fg);
    lv_obj_set_pos(t, 20, 40);
    s_lbl = flex_label(s_scr, "", FLEX_FONT_S4, fg);
    lv_obj_set_pos(s_lbl, 20, 110);
    s_text[0] = 0;
    s_enter = s_back = 0;
    show_text();

    flex_kb_cfg_t c = {
        .flags = l->flags | (l->wall ? FLEX_KB_F_WALLPAPER : 0),
        .backdrop = l->wall ? FLEX_BD_LOCK : FLEX_BD_FLAT,
        .enter_label = l->enter,
        .on_text = on_text,
        .on_backspace = on_back,
        .on_enter = on_enter,
    };
    s_kb = flex_kb_create(s_scr, &c);
    lv_screen_load(s_scr);
    if (old) {
        lv_obj_delete(old);
    }
    sim_run(60);
}

// Centro de una tecla de letra / de funcion en la pantalla
static void key_center(int cell, int *x, int *y)
{
    const flex_kb_geom_t *g = flex_kb_geom(s_kb);
    flex_kb_cell_xy(g, cell, x, y);
    *x += g->kw / 2;
    *y += g->kh / 2;
}

static void tap_fn(int i)
{
    const flex_kb_geom_t *g = flex_kb_geom(s_kb);
    sim_tap(flex_kb_fkey_x(g, i) + flex_kb_fkey_w(g, i) / 2, flex_kb_func_y(g) + g->kh / 2);
}

// Teclea un caracter buscandolo en la capa activa como lo haria una persona:
// mayuscula = shift + letra; si no esta en la capa, cambia de capa.
static bool type_char(const char *ch)
{
    char low[8];
    snprintf(low, sizeof(low), "%s", ch);
    bool upper = false;
    if (ch[1] == 0 && ch[0] >= 'A' && ch[0] <= 'Z') {
        low[0] = (char)(ch[0] + 32);
        upper = true;
    } else if (!strcmp(ch, "\xC3\x91")) {
        strcpy(low, "\xC3\xB1");
        upper = true;
    }
    if (!strcmp(low, " ")) {
        tap_fn(FLEX_KB_FN_SPACE);
        return true;
    }
    for (int tries = 0; tries < 4; tries++) {
        flex_kb_state_t st = {.layout = (uint8_t)flex_kb_get_layout(s_kb), .shift = false,
                              .lang_es = flex_kb_get_lang_es(s_kb)};
        for (int cell = 0; cell < FLEX_KB_CELLS; cell++) {
            if (!strcmp(flex_kb_key_base(&st, cell), low)) {
                if (upper && !flex_kb_get_shift(s_kb)) {
                    tap_fn(FLEX_KB_FN_SHIFT);
                }
                int x, y;
                key_center(cell, &x, &y);
                sim_tap(x, y);
                return true;
            }
        }
        tap_fn(FLEX_KB_FN_LAYER);
    }
    return false;
}

static bool type_str(const char *s)
{
    while (*s) {
        char ch[5] = {0};
        int n = ((unsigned char)*s & 0x80) == 0 ? 1 : ((unsigned char)*s & 0xE0) == 0xC0 ? 2 : 3;
        memcpy(ch, s, (size_t)n);
        if (!type_char(ch)) {
            return false;
        }
        s += n;
    }
    return true;
}

static bool expect(const char *what, bool cond)
{
    printf("  %-58s %s\n", what, cond ? "ok" : "FALLO");
    return cond;
}

bool scene_kb_run(void)
{
    bool ok = true;
    look_t l = {.glass = false, .dark = true, .size = 1, .font = 1, .fast = true};

    // 1) Plano oscuro, capa de letras ES
    build(&l, "Teclado - Plano oscuro");
    const flex_kb_geom_t *g = flex_kb_geom(s_kb);
    ok &= expect("geometria normal: KB_Y 546, panel 542, alto 258",
                 flex_kb_keys_y(s_kb) == 546 && flex_kb_top(s_kb) == 542 && flex_kb_height(s_kb) == 258 &&
                     g->x == 6 && flex_kb_height(NULL) == 258);
    sim_shot("kb_01_plano_oscuro");
    // 2) Shift encendido (etiquetas en mayuscula, tecla shift en acento)
    tap_fn(FLEX_KB_FN_SHIFT);
    ok &= expect("shift encendido", flex_kb_get_shift(s_kb));
    sim_shot("kb_02_shift");
    tap_fn(FLEX_KB_FN_SHIFT);
    // 3) Capa ?123 y capa de emoticonos
    tap_fn(FLEX_KB_FN_LAYER);
    ok &= expect("capa ?123", flex_kb_get_layout(s_kb) == FLEX_KB_LAYOUT_NUM);
    sim_shot("kb_03_num");
    tap_fn(FLEX_KB_FN_SHIFT);
    sim_shot("kb_03b_num_shift_llaves");
    tap_fn(FLEX_KB_FN_SHIFT);
    tap_fn(FLEX_KB_FN_LAYER);
    ok &= expect("capa emoji", flex_kb_get_layout(s_kb) == FLEX_KB_LAYOUT_EMOJI);
    sim_shot("kb_04_emoji");
    tap_fn(FLEX_KB_FN_LAYER);
    ok &= expect("vuelve a letras ES", flex_kb_get_layout(s_kb) == FLEX_KB_LAYOUT_ES);
    tap_fn(FLEX_KB_FN_LANG);
    ok &= expect("ES -> EN", flex_kb_get_layout(s_kb) == FLEX_KB_LAYOUT_EN && !flex_kb_get_lang_es(s_kb));
    sim_shot("kb_05_ingles");

    // 4) Liquid Glass oscuro y claro, plano claro
    l.glass = true;
    build(&l, "Teclado - Liquid Glass oscuro");
    sim_shot("kb_06_vidrio_oscuro");
    l.dark = false;
    build(&l, "Teclado - Liquid Glass claro");
    sim_shot("kb_07_vidrio_claro");
    l.glass = false;
    build(&l, "Teclado - Plano claro");
    sim_shot("kb_08_plano_claro");

    // 5) Sobre el fondo de pantalla (clave al verificar), Plano y Liquid Glass
    l.dark = true;
    l.wall = true;
    build(&l, "Introduce contrase\xC3\xB1" "a");
    sim_shot("kb_09_wallpaper_plano");
    l.glass = true;
    build(&l, "Introduce contrase\xC3\xB1" "a");
    sim_shot("kb_10_wallpaper_vidrio");

    // 6) Escritura real: "Contrasena 1" (con n con tilde) por toques, sobre el fondo
    ok &= expect("teclea \"Contrase\xC3\xB1" "a 1\"", type_str("Contrase\xC3\xB1" "a 1"));
    ok &= expect("texto recibido por on_text == \"Contrase\xC3\xB1" "a 1\"", !strcmp(s_text, "Contrase\xC3\xB1" "a 1"));
    ok &= expect("el shift se apago tras la C", !flex_kb_get_shift(s_kb));
    sim_shot("kb_11_escrito");
    // "<-" borra un caracter UTF-8 entero: "1", " ", "a" y la n con tilde (2 bytes)
    for (int i = 0; i < 4; i++) {
        tap_fn(FLEX_KB_FN_BACKSPACE);
    }
    ok &= expect("4 x \"<-\" -> \"Contrase\"", !strcmp(s_text, "Contrase") && s_back == 4);
    tap_fn(FLEX_KB_FN_ENTER);
    ok &= expect("OK llama a on_enter una vez", s_enter == 1);
    // Mayuscula de la n con tilde
    ok &= expect("shift + n con tilde -> N con tilde", type_str("\xC3\x91") && !strcmp(s_text, "Contrase\xC3\x91"));

    // 7) Entrada deslizando (0.3 s lineal): a mitad, el panel va por la mitad
    flex_kb_slide_in(s_kb);
    sim_run(148);
    lv_area_t a;
    lv_obj_update_layout(s_kb);
    lv_obj_get_coords(s_kb, &a);
    int kbh = 800 - flex_kb_keys_y(s_kb);
    printf("  (a 148 ms: panel en y=%d, reposo %d, recorrido %d)\n", (int)a.y1, (int)flex_kb_top(s_kb), kbh);
    ok &= expect("entrando: animacion activa", flex_kb_is_animating(s_kb));
    ok &= expect("a ~150 ms el panel esta a mitad de camino",
                 a.y1 >= flex_kb_top(s_kb) + kbh / 2 - 6 && a.y1 <= flex_kb_top(s_kb) + kbh / 2 + 6);
    sim_shot("kb_12_entrando");
    // Un toque durante la entrada no escribe nada
    size_t before = strlen(s_text);
    int x, y;
    key_center(0, &x, &y);
    sim_touch(x, 790, true);
    sim_run(20);
    sim_touch(x, 790, false);
    sim_run(200);
    lv_obj_get_coords(s_kb, &a);
    ok &= expect("termina en su sitio y no escribio durante la entrada",
                 !flex_kb_is_animating(s_kb) && a.y1 == flex_kb_top(s_kb) && strlen(s_text) == before);

    // 8) Sacudida: solo se mueven las teclas, no el panel
    flex_kb_set_shift_x(s_kb, 14);
    sim_run(20);
    sim_shot("kb_13_sacudida");
    lv_obj_get_coords(s_kb, &a);
    lv_area_t ka;
    lv_obj_get_coords(flex_kb_keys_obj(s_kb), &ka);
    ok &= expect("sacudida: teclas +14 px, panel quieto", ka.x1 == 14 && a.x1 == 0);
    flex_kb_set_shift_x(s_kb, 0);

    // 9) Tamanos compacto y grande, estilo contorno, contraste alto, letra grande
    l = (look_t){.glass = false, .dark = true, .size = 0, .font = 1, .fast = true};
    build(&l, "Compacto");
    ok &= expect("compacto: KB_Y 578, KB_X 7", flex_kb_keys_y(s_kb) == 578 && flex_kb_geom(s_kb)->x == 7);
    sim_shot("kb_14_compacto");
    l.size = 2;
    l.font = 2;
    build(&l, "Grande + letra grande");
    ok &= expect("grande: KB_Y 498", flex_kb_keys_y(s_kb) == 498);
    sim_shot("kb_15_grande");
    l = (look_t){.glass = false, .dark = false, .size = 1, .font = 1, .style = 2, .fast = true};
    build(&l, "Contorno (claro)");
    sim_shot("kb_16_contorno");
    l = (look_t){.glass = true, .dark = true, .size = 1, .font = 1, .hicon = true, .fast = true};
    build(&l, "Contraste alto");
    sim_shot("kb_17_contraste_alto");
    l = (look_t){.glass = true, .dark = true, .size = 1, .font = 1, .fast = true, .opacity = 70};
    build(&l, "Opacidad 70 %");
    sim_shot("kb_18_opacidad70");

    // 10) Sin escritura rapida: la tecla se escribe al SOLTAR y destella al apoyar
    l = (look_t){.glass = false, .dark = true, .size = 1, .font = 1, .fast = false};
    build(&l, "Escritura al soltar");
    key_center(12, &x, &y);   // "d"
    sim_touch(x, y, true);
    sim_run(40);
    ok &= expect("al soltar: apoyado todavia no escribe", s_text[0] == 0);
    sim_shot("kb_19_destello");
    sim_touch(x, y, false);
    sim_run(200);
    ok &= expect("al soltar: escribe \"d\"", !strcmp(s_text, "d"));
    // Un arrastre largo no es un toque: no escribe
    sim_drag(x, y, x + 60, y, 120);
    ok &= expect("al soltar: un arrastre no escribe", !strcmp(s_text, "d"));

    // 11) Pulsacion larga en vocal -> acentos (Notas), con escritura rapida
    l = (look_t){.glass = true, .dark = true, .size = 1, .font = 1, .fast = true};
    build(&l, "Acentos");
    lv_obj_delete(s_kb);
    flex_kb_cfg_t c = {.flags = FLEX_KB_F_ACCENTS | FLEX_KB_F_FN_ON_PRESS, .enter_label = "ent", .on_text = on_text,
                       .on_backspace = on_back, .on_enter = on_enter};
    s_kb = flex_kb_create(s_scr, &c);
    sim_run(40);
    key_center(10, &x, &y);   // "a"
    sim_touch(x, y, true);
    sim_run(560);
    sim_shot("kb_20_acentos");
    ok &= expect("acentos: la via rapida ya escribio la \"a\"", !strcmp(s_text, "a"));
    // soltar sobre la segunda variante (a con grave)
    int totw = 4 * 40 + 3 * 4, px0 = x - totw / 2;
    if (px0 < 4) {
        px0 = 4;
    }
    int vx = px0 + 1 * 44 + 20, vy = flex_kb_keys_y(s_kb) + 62 - 46 - 10 + 23;
    sim_touch(vx, vy, true);
    sim_run(20);
    sim_touch(vx, vy, false);
    sim_run(200);
    ok &= expect("acentos: soltar en la variante -> \"\xC3\xA0\"", !strcmp(s_text, "\xC3\xA0"));
    // Teclas de funcion al tocar (Notas): el shift cambia ya al apoyar
    const flex_kb_geom_t *g2 = flex_kb_geom(s_kb);
    sim_touch(flex_kb_fkey_x(g2, 0) + 10, flex_kb_func_y(g2) + 10, true);
    sim_run(20);
    ok &= expect("FN_ON_PRESS: shift al apoyar", flex_kb_get_shift(s_kb));
    sim_touch(flex_kb_fkey_x(g2, 0) + 10, flex_kb_func_y(g2) + 10, false);
    sim_run(200);
    ok &= expect("FN_ON_PRESS: soltar no lo vuelve a cambiar", flex_kb_get_shift(s_kb));

    // Preferencias de fabrica para las escenas siguientes
    flex_cfg_set_i32("kbsize", 1);
    flex_cfg_set_i32("kbstyle", 0);
    flex_cfg_set_i32("kbfont", 1);
    flex_cfg_set_bool("kbhicon", false);
    flex_cfg_set_bool("kbfast", true);
    flex_cfg_set_i32("kbopa", 100);
    lv_obj_t *blank = lv_obj_create(NULL);
    lv_screen_load(blank);
    lv_obj_delete(s_scr);
    s_scr = NULL;
    s_kb = NULL;
    s_lbl = NULL;
    printf("teclado: %s\n", ok ? "todo correcto" : "HAY FALLOS");
    return ok;
}
