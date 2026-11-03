// Notas (03 §6): la app real en el simulador, sobre un disco real: crear, escribir con el teclado (chips, acentos, barra),
// mover el cursor, seleccionar con pulsacion larga, cortar / copiar / pegar, portapapeles de 12 ranuras, "mas", autoguardado,
// volver a la lista, renombrar, tirar a la papelera, borrar, notas que no se pueden abrir, sesion y suspension.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_clip.h"
#include "flex_filekit.h"
#include "flex_i18n.h"
#include "flex_kb.h"
#include "flex_kit.h"
#include "flex_kb_layout.h"
#include "flex_fk_model.h"
#include "flex_ned.h"
#include "flex_note_model.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

esp_err_t flex_fs_delete_async(const char *path, void (*cb)(esp_err_t, void *), void *user);
void sim_fs_fail_writes(bool queue_fail, bool disk_fail);

void sim_disk_enable(bool on);
bool sim_disk_put(const char *rel, const void *d, size_t l);
size_t sim_disk_get(const char *rel, void *out, size_t cap);
bool sim_disk_exists(const char *rel);
void flex_session_forget_loaded(int id);
void flex_session_test_preload(int id, const uint8_t *file, size_t len);
bool sim_disk_mkdir(const char *rel);

int flex_app_notes_view(void);
int flex_app_notes_count(void);
bool flex_app_notes_ready(void);
const char *flex_app_notes_text(void);
int flex_app_notes_cursor(void);
int flex_app_notes_scroll(void);
bool flex_app_notes_selection(int *a, int *b);
bool flex_app_notes_menu_open(void);
bool flex_app_notes_clip_open(void);
bool flex_app_notes_more_open(void);
lv_obj_t *flex_app_notes_kb(void);
const char *flex_app_notes_title(int i);
const char *flex_app_notes_preview(int i);
bool flex_app_notes_multi(int *marked);
const char *flex_app_notes_path(void);
lv_obj_t *flex_app_notes_root(void);
int flex_app_notes_clip_count(void);
bool flex_app_notes_clip_slot(int i, const char **text, bool *pinned);
void flex_app_notes_xy_of(int bi, int *x, int *y);
uint32_t flex_app_notes_rev(void);
uint32_t flex_app_notes_saved_rev(void);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("notas: FALLO linea %d: %s\n", __LINE__, #c);     \
            ok = false;                                              \
        }                                                            \
    } while (0)

// ---- ayudas ---------------------------------------------------------------------------------------------------------------
static bool wait_list(void)
{
    for (int i = 0; i < 80; i++) {
        if (flex_app_notes_view() == 0 && flex_app_notes_ready()) return true;
        sim_run(20);
    }
    return false;
}
static bool wait_editor(void)
{
    for (int i = 0; i < 80; i++) {
        if (flex_app_notes_view() == 1) return true;
        sim_run(20);
    }
    return false;
}
static const flex_kb_geom_t *kg(void)
{
    return flex_kb_geom(flex_app_notes_kb());
}
// toca una letra / signo por su etiqueta base en la capa activa
static void key(const char *base)
{
    flex_kb_state_t st;
    flex_kb_state_init(&st, true);
    flex_kb_set_layout_state(&st, flex_kb_get_layout(flex_app_notes_kb()));
    for (int c = 0; c < FLEX_KB_CELLS; c++) {
        const char *b = flex_kb_key_base(&st, c);
        if (!strcmp(b, base)) {
            int x, y;
            flex_kb_cell_xy(kg(), c, &x, &y);
            sim_tap(x + kg()->kw / 2, y + kg()->kh / 2);
            sim_run(30);
            return;
        }
    }
}
static void type(const char *s)
{
    for (; *s; s++) {
        char one[2] = {*s, 0};
        if (*s == ' ') {
            sim_tap(flex_kb_fkey_x(kg(), FLEX_KB_FN_SPACE) + flex_kb_fkey_w(kg(), FLEX_KB_FN_SPACE) / 2, flex_kb_func_y(kg()) + kg()->kh / 2);
            sim_run(30);
        } else {
            key(one);
        }
    }
}
static void fn(int i)
{
    sim_tap(flex_kb_fkey_x(kg(), i) + flex_kb_fkey_w(kg(), i) / 2, flex_kb_func_y(kg()) + kg()->kh / 2);
    sim_run(30);
}
static void tool(int i)
{
    sim_tap(48 + 96 * i, flex_kb_toolbar_y(kg()) + 26);
    sim_run(60);
}
static void tap_char(int bi)
{
    int x, y;
    flex_app_notes_xy_of(bi, &x, &y);
    sim_tap(x + 1, y + 8);
    sim_run(60);
}
static void hold_at(int bi, int ms)
{
    int x, y;
    flex_app_notes_xy_of(bi, &x, &y);
    sim_touch(x + 3, y + 8, true);
    sim_run((uint32_t)ms);
    sim_touch(x + 3, y + 8, false);
    sim_run(60);
}
static void back(void)
{
    sim_tap(24, 40);
    sim_run(80);
}
static void copy_word_at(int bi)   // pulsacion larga en la palabra + Copiar + un toque que suelta la seleccion
{
    hold_at(bi + 1, 700);
    int a, b, ax, ay;
    flex_ned_menu_t mn;
    flex_app_notes_selection(&a, &b);
    flex_app_notes_xy_of(a, &ax, &ay);
    flex_ned_menu_geom(ay, &mn);
    sim_tap(mn.x + 1 * (mn.bw + mn.gap) + mn.bw / 2, mn.y + 14);
    sim_run(80);
    sim_tap(450, 100);                                         // lejos de las manijas: suelta la seleccion
    sim_run(80);
    (void)bi;
}
static bool clip_has(const char *txt, bool *pinned)
{
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        const char *t;
        bool p;
        if (flex_app_notes_clip_slot(i, &t, &p) && !strcmp(t, txt)) {
            if (pinned) *pinned = p;
            return true;
        }
    }
    return false;
}
// tarjeta i de la lista: centro en pantalla
static void card_xy(int i, int *x, int *y)
{
    int cx, cy, cw, ch;
    extern int flex_app_notes_list_scroll(void);
    flex_nl_card_rect(i, flex_app_notes_list_scroll(), &cx, &cy, &cw, &ch);
    *x = cx + cw / 2;
    *y = cy + ch / 2;
}
static void hold_xy(int x, int y, int ms)
{
    sim_touch(x, y, true);
    sim_run((uint32_t)ms);
    sim_touch(x, y, false);
    sim_run(100);
}
// una fila del menu contextual abierto en (px, py)
static void menu_row(int px, int py, int row)
{
    flex_fk_rect_t r;
    flex_fk_menu_geom(px, py, false, &r);
    sim_tap(r.x + 60, r.y + FLEX_FK_MENU_PAD + row * FLEX_FK_MENU_RH + FLEX_FK_MENU_RH / 2);
    sim_run(100);
}
// el teclado de los dialogos del kit: Normal, sobre la barra de navegacion, sin barra ni chips
static void dlg_key(const char *base)
{
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 64, 0);
    flex_kb_state_t st;
    flex_kb_state_init(&st, true);
    for (int c = 0; c < FLEX_KB_CELLS; c++) {
        if (!strcmp(flex_kb_key_base(&st, c), base)) {
            int x, y;
            flex_kb_cell_xy(&g, c, &x, &y);
            sim_tap(x + g.kw / 2, y + g.kh / 2);
            sim_run(30);
            return;
        }
    }
}
static void dlg_fn(int i)
{
    flex_kb_geom_t g;
    flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 64, 0);
    sim_tap(flex_kb_fkey_x(&g, i) + flex_kb_fkey_w(&g, i) / 2, flex_kb_func_y(&g) + g.kh / 2);
    sim_run(30);
}
static bool file_is(const char *rel, const char *txt)
{
    char b[8192];
    size_t n = sim_disk_get(rel, b, sizeof(b) - 1);
    if (n == (size_t)-1) return false;
    b[n] = 0;
    return !strcmp(b, txt);
}

bool scene_notes_run(void)
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
    flex_session_forget_loaded(IC_NOTAS);

    // ---- 1) lista vacia y boton flotante: crea "Sin titulo 1.txt" y abre el editor
    flex_app_open(IC_NOTAS, NULL);
    sim_run(500);
    CHK(wait_list() && flex_app_notes_count() == 0);
    sim_shot("notas_vacia");
    sim_tap(396, 650);
    CHK(wait_editor());
    sim_run(400);   // entrada del teclado, 0,3 s
    CHK(!strcmp(flex_app_notes_path(), "/Notas/Sin t\xC3\xADtulo 1.txt") && sim_disk_exists("/Notas/Sin t\xC3\xADtulo 1.txt"));
    CHK(!strcmp(flex_app_notes_text(), "") && flex_app_notes_cursor() == 0);
    CHK(flex_kb_chip_count(flex_app_notes_kb()) == 0);

    // ---- 2) escribir con el teclado; el texto sale en el buffer y NO en el disco hasta el autoguardado
    type("hola mundo");
    CHK(!strcmp(flex_app_notes_text(), "hola mundo") && flex_app_notes_cursor() == 10);
    CHK(flex_app_notes_rev() != flex_app_notes_saved_rev());   // cambios sin guardar
    CHK(file_is("/Notas/Sin t\xC3\xADtulo 1.txt", ""));
    sim_shot("notas_editor");
    sim_run(900);
    CHK(file_is("/Notas/Sin t\xC3\xADtulo 1.txt", ""));                 // menos de 2 s: aun no
    sim_run(2000);
    CHK(file_is("/Notas/Sin t\xC3\xADtulo 1.txt", "hola mundo"));      // 2 s despues de la ultima tecla
    CHK(flex_app_notes_rev() == flex_app_notes_saved_rev());

    // ---- 3) enter, retroceso y mover el cursor tocando el texto
    fn(FLEX_KB_FKEYS - 1);                                     // "ent"
    type("dos");
    CHK(!strcmp(flex_app_notes_text(), "hola mundo\ndos"));
    fn(FLEX_KB_FN_BACKSPACE);
    CHK(!strcmp(flex_app_notes_text(), "hola mundo\ndo"));
    tap_char(5);                                               // antes de "mundo"
    CHK(flex_app_notes_cursor() == 5);
    type("x");
    CHK(!strcmp(flex_app_notes_text(), "hola xmundo\ndo") && flex_app_notes_cursor() == 6);

    // ---- 4) chips: la palabra en curso sugiere del diccionario; aceptar un chip SUSTITUYE la palabra y deja un espacio
    {
        int L = (int)strlen(flex_app_notes_text());
        tap_char(L);                                           // el cursor al final: la palabra en curso es "do"
        CHK(flex_app_notes_cursor() == L);
        lv_obj_t *kb = flex_app_notes_kb();
        CHK(flex_kb_chip_count(kb) > 0);
        char first[48];
        snprintf(first, sizeof(first), "%s", flex_kb_chip_text(kb, 0));
        int top0 = (int)flex_kb_top(kb);
        const flex_kb_geom_t *g = kg();
        int cw = (480 - 12) / flex_kb_chip_count(kb);
        sim_tap(6 + cw / 2, flex_kb_chips_y(g, 56) + 16);       // el primer chip
        sim_run(80);
        char want[96];
        snprintf(want, sizeof(want), "hola xmundo\n%s ", first);
        CHK(!strcmp(flex_app_notes_text(), want));
        CHK((int)flex_kb_top(kb) == top0);                     // el panel no se movio (los chips ya estaban)
        // capa ?123: la franja ofrece los 4 simbolos personalizados y se inserta tal cual
        fn(FLEX_KB_FN_LAYER);
        CHK(flex_kb_get_layout(kb) == FLEX_KB_LAYOUT_NUM && flex_kb_chip_count(kb) == 4);
        int before = (int)strlen(flex_app_notes_text());
        sim_tap(6 + (480 - 12) / 4 / 2, flex_kb_chips_y(kg(), 56) + 16);
        sim_run(60);
        CHK((int)strlen(flex_app_notes_text()) > before);
        // de vuelta a las letras (?123 -> emoji -> ABC): la franja vuelve a ser de sugerencias
        fn(FLEX_KB_FN_LAYER);
        fn(FLEX_KB_FN_LAYER);
        CHK(flex_kb_get_layout(kb) == FLEX_KB_LAYOUT_ES);
        tool(0);                                               // carita: capa de emoticonos
        CHK(flex_kb_get_layout(kb) == FLEX_KB_LAYOUT_EMOJI);
        fn(FLEX_KB_FN_LAYER);
        CHK(flex_kb_get_layout(kb) == FLEX_KB_LAYOUT_ES);
        tool(1);                                               // globo: idioma
        CHK(!flex_kb_get_lang_es(kb));
        tool(1);
        CHK(flex_kb_get_lang_es(kb));
    }

    // ---- 5) pulsacion larga: selecciona la palabra y abre el menu; Copiar / Cortar / Pegar / Todo
    {
        const char *t = flex_app_notes_text();
        int bi = (int)(strstr(t, "xmundo") - t);
        hold_at(bi + 2, 700);
        int a, b;
        CHK(flex_app_notes_selection(&a, &b) && a == bi && b == bi + 6 && flex_app_notes_menu_open());
        sim_shot("notas_seleccion");
        flex_ned_menu_t mn;
        int ax, ay;
        flex_app_notes_xy_of(a, &ax, &ay);
        flex_ned_menu_geom(ay, &mn);
        sim_tap(mn.x + 1 * (mn.bw + mn.gap) + mn.bw / 2, mn.y + 14);        // Copiar
        sim_run(80);
        CHK(flex_app_notes_selection(NULL, NULL) && !flex_app_notes_menu_open());   // el menu se cierra, la seleccion queda
        int bx, by;
        flex_app_notes_xy_of(b, &bx, &by);
        sim_tap(bx, by + 24);                                              // la manija derecha: vuelve el menu
        sim_run(80);
        CHK(flex_app_notes_menu_open());
        flex_app_notes_xy_of(a, &ax, &ay);
        flex_ned_menu_geom(ay, &mn);
        sim_tap(mn.x + 0 * (mn.bw + mn.gap) + mn.bw / 2, mn.y + 14);        // Cortar
        sim_run(80);
        CHK(strstr(flex_app_notes_text(), "xmundo") == NULL && !flex_app_notes_selection(NULL, NULL));
        // Pegar: el ultimo texto copiado ("xmundo") en el cursor. El menu solo sale con una seleccion: se selecciona una letra.
        hold_at(0, 700);
        CHK(flex_app_notes_selection(&a, &b) && flex_app_notes_menu_open());
        flex_app_notes_xy_of(a, &ax, &ay);
        flex_ned_menu_geom(ay, &mn);
        sim_tap(mn.x + 2 * (mn.bw + mn.gap) + mn.bw / 2, mn.y + 14);        // Pegar: reemplaza la seleccion
        sim_run(80);
        CHK(!strncmp(flex_app_notes_text(), "xmundo", 6));
        // Todo
        hold_at(0, 700);
        CHK(flex_app_notes_selection(&a, &b));
        flex_app_notes_xy_of(a, &ax, &ay);
        flex_ned_menu_geom(ay, &mn);
        sim_tap(mn.x + 3 * (mn.bw + mn.gap) + mn.bw / 2, mn.y + 14);        // Todo
        sim_run(80);
        CHK(flex_app_notes_selection(&a, &b) && a == 0 && b == (int)strlen(flex_app_notes_text()) && flex_app_notes_menu_open());
        tap_char(0);                                                       // un toque suelta la seleccion
        CHK(!flex_app_notes_selection(NULL, NULL) && !flex_app_notes_menu_open());
    }

    // ---- 6) portapapeles de 12 ranuras: copiar alimenta las ranuras; el panel pega, fija, borra, filtra y vacia
    {
        type(" uno dos tres");
        const char *t = flex_app_notes_text();
        int iu = (int)(strstr(t, "uno") - t), id = (int)(strstr(t, "dos") - t), it = (int)(strstr(t, "tres") - t);
        copy_word_at(iu);
        copy_word_at(id);
        copy_word_at(it);
        CHK(clip_has("uno", NULL) && clip_has("dos", NULL) && clip_has("tresxmundo", NULL) && flex_app_notes_clip_count() >= 3);
        tool(2);                                               // portapapeles
        CHK(flex_app_notes_clip_open());
        sim_shot("notas_portapapeles");
        // la ficha de "dos": se fija con su pin (arriba a la derecha de la tarjeta)
        int k = -1, kk = 0;
        for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
            const char *tx;
            if (flex_app_notes_clip_slot(i, &tx, NULL)) {
                if (!strcmp(tx, "dos")) k = kk;
                kk++;
            }
        }
        CHK(k >= 0);
        int cx = 12 + (k % 2) * (222 + 12), cy = 108 + (k / 2) * (116 + 12);
        sim_tap(cx + 222 - 15, cy + 15);
        sim_run(80);
        bool pinned = false;
        CHK(clip_has("dos", &pinned) && pinned);
        {
            char nvs[64];
            int found = 0;
            for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
                char key[10];
                snprintf(key, sizeof(key), "clip%d", i);
                flex_cfg_get_str(key, nvs, sizeof(nvs), "");
                found += !strcmp(nvs, "dos");
            }
            CHK(found == 1);                                   // las fijadas sobreviven al reinicio: estan en la NVS
        }
        // filtro de fijadas y vaciar (dos toques): solo quedan las fijadas
        sim_tap(480 - 96, 32);
        sim_run(80);
        sim_tap(480 - 42, 32);                                 // primer toque: pide confirmacion, no borra
        sim_run(80);
        CHK(clip_has("uno", NULL));
        sim_tap(480 - 42, 32);                                 // segundo: vacia las no fijadas
        sim_run(80);
        CHK(!clip_has("uno", NULL) && !clip_has("tresxmundo", NULL) && clip_has("dos", NULL) && flex_app_notes_clip_count() == 1);
        // tocar la ficha la pega en el cursor y cierra el panel
        int before = (int)strlen(flex_app_notes_text());
        sim_tap(12 + 100, 108 + 50);
        sim_run(80);
        CHK(!flex_app_notes_clip_open() && (int)strlen(flex_app_notes_text()) == before + 3);
        // borrar una ficha fijada la quita tambien de la NVS
        tool(2);
        sim_tap(12 + 222 - 15, 108 + 116 - 15);
        sim_run(80);
        CHK(flex_app_notes_clip_count() == 0);
        {
            char nvs[64];
            int found = 0;
            for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
                char key[10];
                snprintf(key, sizeof(key), "clip%d", i);
                flex_cfg_get_str(key, nvs, sizeof(nvs), "");
                found += nvs[0] != 0;
            }
            CHK(found == 0);
        }
        sim_tap(25, 32);                                       // volver
        sim_run(80);
        CHK(!flex_app_notes_clip_open());
    }

    // ---- 7) menu "mas": seleccionar todo e insertar fecha y hora
    {
        tool(4);
        CHK(flex_app_notes_more_open());
        const flex_kb_geom_t *g = kg();
        int mx = 480 - 250 - 10, my = flex_kb_toolbar_y(g) - (2 * 38 + 12) - 6;
        if (my < 60) my = 60;
        int before = (int)strlen(flex_app_notes_text());
        sim_tap(mx + 100, my + 10 + 38 + 19);                  // "Insertar fecha y hora"
        sim_run(80);
        CHK(!flex_app_notes_more_open() && (int)strlen(flex_app_notes_text()) > before && strchr(flex_app_notes_text() + before, ':') != NULL);
        tool(4);
        sim_tap(mx + 100, my + 10 + 19);                       // "Seleccionar todo"
        sim_run(80);
        int a, b;
        CHK(flex_app_notes_selection(&a, &b) && a == 0 && b == (int)strlen(flex_app_notes_text()));
        tool(4);
        sim_tap(20, 300);                                      // fuera del menu: lo cierra sin hacer nada
        sim_run(80);
        CHK(!flex_app_notes_more_open());
        tap_char(0);
    }

    // ---- 8) volver a la lista: guarda, relee del disco y enseña la vista previa REAL
    char saved[512];
    {
        sim_run(3000);                                         // todo lo anterior ya esta guardado
        type("z");                                             // y esto NO: hay que guardarlo al volver, sin esperar
        snprintf(saved, sizeof(saved), "%s", flex_app_notes_text());
        CHK(!file_is("/Notas/Sin t\xC3\xADtulo 1.txt", saved));
        back();
        CHK(wait_list() && flex_app_notes_count() == 1);
        CHK(file_is("/Notas/Sin t\xC3\xADtulo 1.txt", saved));                    // lo ultimo escrito, aunque no hubieran pasado 2 s
        CHK(!strncmp(flex_app_notes_preview(0), saved, strlen(saved) < 95 ? strlen(saved) : 95));
        sim_shot("notas_lista");
    }

    // ---- 9) menu contextual de una tarjeta: renombrar (conserva la extension), nueva nota, seleccionar y tirar a la papelera
    {
        int x, y;
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);                                    // pulsacion larga sobre la tarjeta
        CHK(flex_fk_any(flex_app_notes_root()));
        menu_row(x, y - 40, FLEX_FK_ACT_REN);
        sim_run(200);
        for (int i = 0; i < 14; i++) dlg_fn(FLEX_KB_FN_BACKSPACE);   // borra "Sin titulo 1"
        const char *w = "nota uno";
        for (; *w; w++) {
            char one[2] = {*w, 0};
            if (*w == ' ') {
                flex_kb_geom_t g;
                flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 64, 0);
                sim_tap(flex_kb_fkey_x(&g, FLEX_KB_FN_SPACE) + flex_kb_fkey_w(&g, FLEX_KB_FN_SPACE) / 2, flex_kb_func_y(&g) + g.kh / 2);
                sim_run(30);
            } else {
                dlg_key(one);
            }
        }
        dlg_fn(FLEX_KB_FKEYS - 1);                             // Guardar
        CHK(wait_list());
        CHK(sim_disk_exists("/Notas/nota uno.txt") && !sim_disk_exists("/Notas/Sin t\xC3\xADtulo 1.txt"));   // la extension se conserva
        CHK(!strcmp(flex_app_notes_title(0), "nota uno.txt"));
        // nueva nota: el primer "Sin titulo N" libre
        sim_tap(396, 650);
        CHK(wait_editor());
        sim_run(400);
        CHK(!strcmp(flex_app_notes_path(), "/Notas/Sin t\xC3\xADtulo 1.txt"));
        type("segunda");
        back();
        CHK(wait_list() && flex_app_notes_count() == 2);
        CHK(!strcmp(flex_app_notes_title(0), "nota uno.txt") && !strcmp(flex_app_notes_title(1), "Sin t\xC3\xADtulo 1.txt"));   // orden sin mayusculas
        // modo seleccion: marcar las dos y tirarlas a la papelera de una vez
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_SEL);
        int marked = 0;
        CHK(flex_app_notes_multi(&marked) && marked == 1);
        sim_shot("notas_seleccion_lista");
        card_xy(1, &x, &y);
        sim_tap(x, y);
        sim_run(100);
        CHK(flex_app_notes_multi(&marked) && marked == 2);
        sim_tap(300, 702);                                     // "Papelera" de la barra: sin confirmacion
        CHK(wait_list() && flex_app_notes_count() == 0);
        CHK(sim_disk_exists("/Papelera/@Notas@nota uno.txt") && sim_disk_exists("/Papelera/@Notas@Sin t\xC3\xADtulo 1.txt"));
        CHK(!sim_disk_exists("/Notas/nota uno.txt") && !flex_app_notes_multi(NULL));
        // la papelera desde los tres puntos: restaurar una
        sim_tap(450, 40);
        sim_run(100);
        menu_row(440, 56, FLEX_FK_ACT_TRASH);
        for (int i = 0; i < 60 && flex_fk_trash_count() < 2; i++) sim_run(20);
        CHK(flex_fk_trash_count() == 2);
        sim_tap(240, 108 + 12 + 25);
        sim_run(80);
        sim_tap(124, 706);                                     // Restaurar
        for (int i = 0; i < 60 && flex_fk_trash_count() != 1; i++) sim_run(20);
        CHK(sim_disk_exists("/Notas/Sin t\xC3\xADtulo 1.txt") || sim_disk_exists("/Notas/nota uno.txt"));
        sim_tap(24, 40);                                       // sale de la papelera: la lista se relee
        CHK(wait_list() && flex_app_notes_count() == 1);
        // eliminar de verdad: confirmacion y borrado
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_DEL);
        flex_fk_rect_t card, no, yes;
        flex_fk_ask_geom(&card, &no, &yes);
        sim_tap(no.x + no.w / 2, no.y + 28);                   // cancelar: sigue ahi
        sim_run(100);
        CHK(wait_list() && flex_app_notes_count() == 1);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_DEL);
        sim_tap(yes.x + yes.w / 2, yes.y + 28);
        CHK(wait_list() && flex_app_notes_count() == 0);
    }

    // ---- 10) lo que NO se puede abrir no se abre ni se toca: nota mayor que el buffer, archivo con bytes 0, carpetas, mas de 64 notas
    {
        static char big[20000];
        memset(big, 'a', sizeof(big));
        sim_disk_put("/Notas/grande.txt", big, sizeof(big));
        sim_disk_put("/Notas/binario.txt", "ab\0cd", 5);
        sim_disk_mkdir("/Notas/carpeta");
        sim_disk_put("/Notas/corta.txt", "hola", 4);
        flex_app_close();                                      // a Inicio y de vuelta: la lista se relee
        sim_run(400);
        flex_app_open(IC_NOTAS, NULL);
        sim_run(500);
        CHK(wait_list() && flex_app_notes_count() == 3);       // la carpeta no es una nota
        int gi = -1, bi2 = -1, ci = -1;
        for (int i = 0; i < 3; i++) {
            const char *t = flex_app_notes_title(i);
            if (!strcmp(t, "grande.txt")) gi = i;
            if (!strcmp(t, "binario.txt")) bi2 = i;
            if (!strcmp(t, "corta.txt")) ci = i;
        }
        CHK(gi >= 0 && bi2 >= 0 && ci >= 0);
        int x, y;
        card_xy(gi, &x, &y);
        sim_tap(x, y);
        sim_run(300);
        CHK(flex_app_notes_view() == 0 && flex_kit_toast_count(flex_app_notes_root()) == 1);   // "demasiado grande": no se abre
        CHK(sim_disk_get("/Notas/grande.txt", big, 1) != (size_t)-1);
        sim_run(2000);
        card_xy(bi2, &x, &y);
        sim_tap(x, y);
        sim_run(300);
        CHK(flex_app_notes_view() == 0);                       // un byte 0 cortaria el texto al guardarlo: no se abre
        {
            static char chk[20001];
            size_t n = sim_disk_get("/Notas/grande.txt", chk, sizeof(chk));
            CHK(n == 20000 && chk[0] == 'a' && chk[19999] == 'a');     // intacta: Arduino la abria recortada a 4 KB y la guardaba asi
            size_t m = sim_disk_get("/Notas/binario.txt", chk, 10);
            CHK(m == 5 && chk[2] == 0 && chk[3] == 'c');
        }
        // mas de 64 notas: se ordenan ANTES de recortar y se avisa
        for (int i = 0; i < 70; i++) {
            char path[64];
            snprintf(path, sizeof(path), "/Notas/n%02d.txt", i);
            sim_disk_put(path, "x", 1);
        }
        flex_app_close();
        sim_run(400);
        flex_app_open(IC_NOTAS, NULL);
        sim_run(500);
        CHK(wait_list() && flex_app_notes_count() == 64);
        CHK(!strcmp(flex_app_notes_title(0), "binario.txt") && !strcmp(flex_app_notes_title(1), "corta.txt") && !strcmp(flex_app_notes_title(2), "grande.txt") &&
            !strcmp(flex_app_notes_title(3), "n00.txt"));      // orden sin mayusculas, sobre TODAS las notas
        CHK(flex_kit_toast_count(flex_app_notes_root()) >= 0);
        sim_shot("notas_lista_larga");
        // limpiar para lo que sigue
        for (int i = 0; i < 70; i++) {
            char path[64];
            snprintf(path, sizeof(path), "/Notas/n%02d.txt", i);
            flex_fs_delete_async(path, NULL, NULL);
        }
        flex_fs_delete_async("/Notas/grande.txt", NULL, NULL);
        flex_fs_delete_async("/Notas/binario.txt", NULL, NULL);
        flex_fs_delete_async("/Notas/carpeta", NULL, NULL);
        sim_run(200);
        flex_app_close();
        sim_run(400);
        flex_app_open(IC_NOTAS, NULL);
        sim_run(500);
        CHK(wait_list() && flex_app_notes_count() == 1);
    }

    // ---- 11) suspender guarda YA; reabrir sin recrear; cerrar la app guarda; la sesion restaura vista, cursor, seleccion y teclado
    {
        int x, y;
        card_xy(0, &x, &y);
        sim_tap(x, y);                                         // abre "corta" ("hola")
        CHK(wait_editor());
        sim_run(400);
        CHK(!strcmp(flex_app_notes_text(), "hola") && flex_app_notes_cursor() == 4);
        type("x");
        CHK(sim_disk_exists("/Notas/corta.txt") && file_is("/Notas/corta.txt", "hola"));   // sin pasar los 2 s: aun no esta
        flex_app_close();                                      // suspender: guarda sin esperar
        sim_run(300);
        CHK(file_is("/Notas/corta.txt", "holax"));
        flex_app_open(IC_NOTAS, NULL);                         // vuelve a la nota abierta, sin releer ni recrear
        sim_run(500);
        CHK(flex_app_notes_view() == 1 && !strcmp(flex_app_notes_text(), "holax") && flex_app_notes_cursor() == 5);
        // estado del teclado y de las capas: se conserva al suspender
        fn(FLEX_KB_FN_LAYER);                                  // capa ?123
        tool(4);                                               // y el menu "mas" abierto
        flex_app_close();
        sim_run(300);
        flex_app_open(IC_NOTAS, NULL);
        sim_run(500);
        CHK(flex_kb_get_layout(flex_app_notes_kb()) == FLEX_KB_LAYOUT_NUM && flex_app_notes_more_open());
        sim_tap(20, 300);                                      // cierra "mas"
        sim_run(80);
        fn(FLEX_KB_FN_LAYER);
        fn(FLEX_KB_FN_LAYER);
        // sesion: se escribe en disco con el formato de Arduino (cabecera de 16 B + 116 B) y restaura al abrir tras cerrar
        hold_at(1, 700);                                       // selecciona "holax"
        int a, b;
        CHK(flex_app_notes_selection(&a, &b) && a == 0 && b == 5);
        flex_session_flush_now();
        sim_run(200);
        static uint8_t file[512];
        size_t n = sim_disk_get("/System/Sessions/notas.bin", file, sizeof(file));
        CHK(n == 16 + 116);
        flex_app_terminate_ex(IC_NOTAS, true);                 // cerrar de verdad
        sim_run(300);
        CHK(file_is("/Notas/corta.txt", "holax"));
        flex_session_forget_loaded(IC_NOTAS);
        flex_session_test_preload(IC_NOTAS, file, n);
        flex_app_open(IC_NOTAS, NULL);
        sim_run(600);
        CHK(wait_editor() && !strcmp(flex_app_notes_text(), "holax"));
        CHK(flex_app_notes_selection(&a, &b) && a == 0 && b == 5 && flex_app_notes_cursor() == 5);
        // otra vuelta con el cursor en mitad del texto y sin seleccion: vuelve donde estaba
        sim_tap(450, 100);                                     // lejos de las manijas: suelta la seleccion
        sim_run(80);
        tap_char(2);
        CHK(flex_app_notes_cursor() == 2 && !flex_app_notes_selection(NULL, NULL));
        flex_session_flush_now();
        sim_run(200);
        n = sim_disk_get("/System/Sessions/notas.bin", file, sizeof(file));
        CHK(n == 16 + 116);
        flex_app_terminate_ex(IC_NOTAS, true);
        sim_run(300);
        flex_session_forget_loaded(IC_NOTAS);
        flex_session_test_preload(IC_NOTAS, file, n);
        flex_app_open(IC_NOTAS, NULL);
        sim_run(600);
        CHK(wait_editor() && flex_app_notes_cursor() == 2 && !flex_app_notes_selection(NULL, NULL));
        // una sesion de una nota que ya no existe vuelve a la lista (el editor no se abre sobre nada)
        flex_fs_delete_async("/Notas/corta.txt", NULL, NULL);
        sim_run(100);
        flex_app_terminate_ex(IC_NOTAS, true);
        sim_run(300);
        flex_session_forget_loaded(IC_NOTAS);
        flex_session_test_preload(IC_NOTAS, file, n);
        flex_app_open(IC_NOTAS, NULL);
        sim_run(600);
        CHK(wait_list() && flex_app_notes_count() == 0);
    }

    // ---- 12) cambios que dejan el texto con la MISMA longitud tambien se guardan (Arduino comparaba solo la longitud)
    {
        sim_tap(396, 650);
        CHK(wait_editor());
        sim_run(400);
        type("abc");
        sim_run(2600);
        CHK(file_is(flex_app_notes_path(), "abc"));
        hold_at(1, 700);                                       // selecciona "abc"
        int a, b;
        CHK(flex_app_notes_selection(&a, &b) && a == 0 && b == 3);
        type("xyz");                                           // reemplaza por otra de la misma longitud
        CHK(!strcmp(flex_app_notes_text(), "xyzabc") || !strcmp(flex_app_notes_text(), "xyz"));
        sim_run(2600);
        CHK(file_is(flex_app_notes_path(), flex_app_notes_text()) && flex_app_notes_rev() == flex_app_notes_saved_rev());
    }

    // ---- 13) nota llena: lo que no cabe se rechaza con aviso y NO borra nada; un guardado que falla se reintenta
    {
        static char full[16381];
        memset(full, 'a', sizeof(full) - 1);
        full[sizeof(full) - 1] = 0;
        flex_fs_delete_async("/Notas/abc.txt", NULL, NULL);
        sim_disk_put("/Notas/llena.txt", full, 16380);
        back();                                                // de la nota "abc" a la lista, que se relee
        CHK(wait_list());
        int idx = -1;
        for (int i = 0; i < flex_app_notes_count(); i++) if (!strcmp(flex_app_notes_title(i), "llena.txt")) idx = i;
        CHK(idx >= 0);
        int x, y;
        card_xy(idx, &x, &y);
        sim_tap(x, y);
        CHK(wait_editor());
        sim_run(400);
        CHK(strlen(flex_app_notes_text()) == 16380);
        sim_fs_fail_writes(false, true);                       // el disco falla al guardar
        type("bcdef");
        CHK(strlen(flex_app_notes_text()) == 16382);           // solo cupieron 2; el resto se rechazo
        CHK(flex_kit_toast_count(flex_app_notes_root()) == 1);
        // el guardado falla: el texto sigue en memoria, se avisa y se reintenta solo
        sim_run(2600);
        CHK(flex_app_notes_rev() != flex_app_notes_saved_rev());
        sim_fs_fail_writes(false, false);
        sim_run(1800);
        CHK(flex_app_notes_rev() == flex_app_notes_saved_rev());
        {
            static char chk[16400];
            size_t n = sim_disk_get("/Notas/llena.txt", chk, sizeof(chk));
            CHK(n == 16382);                                   // ni recortada ni vacia
        }
    }

    // ---- 14) "atras": cierra capas de dentro afuera y solo al final sale; cancelar un dialogo no cambia nada
    {
        sim_disk_put("/Notas/otra.txt", "otra", 4);
        back();
        CHK(wait_list());
        int oi = -1;
        for (int i = 0; i < flex_app_notes_count(); i++) if (!strcmp(flex_app_notes_title(i), "otra.txt")) oi = i;
        CHK(oi >= 0);
        int ox, oy;
        card_xy(oi, &ox, &oy);
        sim_tap(ox, oy);
        CHK(wait_editor());
        sim_run(400);
        hold_at(1, 700);
        int a, b;
        CHK(flex_app_notes_selection(&a, &b) && flex_app_notes_menu_open());
        flex_sys_back();                                       // 1.o: suelta la seleccion y el menu
        sim_run(100);
        CHK(!flex_app_notes_selection(NULL, NULL) && !flex_app_notes_menu_open() && flex_app_notes_view() == 1);
        tool(4);
        CHK(flex_app_notes_more_open());
        flex_sys_back();                                       // el menu "mas"
        sim_run(100);
        CHK(!flex_app_notes_more_open() && flex_app_notes_view() == 1);
        tool(2);
        CHK(flex_app_notes_clip_open());
        flex_sys_back();                                       // el panel del portapapeles
        sim_run(100);
        CHK(!flex_app_notes_clip_open() && flex_app_notes_view() == 1);
        flex_sys_back();                                       // la pantalla: editor -> lista
        CHK(wait_list());
        // modo seleccion: "Salir" lo cierra sin tirar nada
        {
            int cx, cy;
            card_xy(0, &cx, &cy);
            hold_xy(cx, cy, 700);
            menu_row(cx, cy - 40, FLEX_FK_ACT_SEL);
            CHK(flex_app_notes_multi(NULL));
            sim_tap(440, 702);                                 // "Salir"
            sim_run(100);
            CHK(!flex_app_notes_multi(NULL) && flex_app_notes_count() >= 1);
            // renombrar y cancelar con la esquina: no cambia el nombre
            hold_xy(cx, cy, 700);
            menu_row(cx, cy - 40, FLEX_FK_ACT_REN);
            sim_run(200);
            sim_tap(30, 60);
            sim_run(200);
            CHK(wait_list() && sim_disk_exists("/Notas/llena.txt"));
            CHK(wait_list());
        }
    }

    printf("notas: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    flex_app_terminate_ex(IC_NOTAS, true);
    sim_disk_enable(false);
    flex_shell_lock();
    sim_run(200);
    return ok;
}
