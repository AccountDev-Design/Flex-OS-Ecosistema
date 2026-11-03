// Paint (03 §7): la app real en el simulador, sobre un disco real: galeria vacia, dibujo nuevo, trazos con el dedo (en vivo y
// guardados al levantarlo), colores, grosor, deshacer, limpiar, volver a la galeria con la miniatura real, abrir, renombrar,
// seleccion, papelera, borrar, ficheros que no son dibujos, mas de 24 dibujos, fallos de disco, sesion y suspension.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_filekit.h"
#include "flex_fk_model.h"
#include "flex_i18n.h"
#include "flex_kb.h"
#include "flex_kit.h"
#include "flex_paint_model.h"
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
bool sim_disk_mkdir(const char *rel);
void flex_session_forget_loaded(int id);
void flex_session_test_preload(int id, const uint8_t *file, size_t len);

int flex_app_paint_view(void);
int flex_app_paint_count(void);
bool flex_app_paint_ready(void);
const char *flex_app_paint_title(int i);
bool flex_app_paint_multi(int *marked);
const char *flex_app_paint_path(void);
lv_obj_t *flex_app_paint_root(void);
int flex_app_paint_strokes(void);
int flex_app_paint_pending(void);
unsigned flex_app_paint_color(void);
int flex_app_paint_size(void);
bool flex_app_paint_writing(void);
int flex_app_paint_scroll(void);
int flex_app_paint_canvas_px(int x, int y);
int flex_app_paint_thumb_ink(int i);
bool flex_app_paint_thumb_present(int i);
int flex_app_paint_card_images(int i);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("paint: FALLO linea %d: %s\n", __LINE__, #c);     \
            ok = false;                                              \
        }                                                            \
    } while (0)

#define NAV 64
#define BG565 ((uint16_t)((250 & 0xF8) << 8 | (250 & 0xFC) << 3 | (252 >> 3)))

// ---- ayudas ---------------------------------------------------------------------------------------------------------------
static bool wait_gallery(void)
{
    for (int i = 0; i < 120; i++) {
        if (flex_app_paint_view() == 0 && flex_app_paint_ready()) return true;
        sim_run(20);
    }
    return false;
}
static bool wait_canvas(void)
{
    for (int i = 0; i < 120; i++) {
        if (flex_app_paint_view() == 1) return true;
        sim_run(20);
    }
    return false;
}
static bool wait_idle(void)
{
    for (int i = 0; i < 80; i++) {
        if (!flex_app_paint_writing()) return true;
        sim_run(20);
    }
    return false;
}
static void card_xy(int i, int *x, int *y)
{
    int cx, cy, cw, ch;
    flex_pg_card_rect(i, flex_app_paint_scroll(), NAV, &cx, &cy, &cw, &ch);
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
static void menu_row(int px, int py, int row)
{
    flex_fk_rect_t r;
    flex_fk_menu_geom(px, py, false, &r);
    sim_tap(r.x + 60, r.y + FLEX_FK_MENU_PAD + row * FLEX_FK_MENU_RH + FLEX_FK_MENU_RH / 2);
    sim_run(100);
}
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
// herramientas del lienzo (y = P_BOT + 25 = 695)
static void tool_color(int i)
{
    sim_tap(27 + 40 * i, flex_paint_bot(NAV) + 25);
    sim_run(60);
}
static void tool_size(void)
{
    sim_tap(280, flex_paint_bot(NAV) + 25);
    sim_run(60);
}
static void tool_undo(void)
{
    sim_tap(350, flex_paint_bot(NAV) + 25);
    sim_run(100);
}
static void tool_clear(void)
{
    sim_tap(425, flex_paint_bot(NAV) + 25);
    sim_run(100);
}
static void stroke(int x0, int y0, int x1, int y1)
{
    sim_drag(x0, y0, x1, y1, 300);
    sim_run(150);
}
static void back(void)
{
    sim_tap(24, 40);
    sim_run(80);
}
// cabecera del fichero en disco: w, h y numero de trazos; false si no es un .fxp
static bool disk_hdr(const char *rel, int *w, int *h, int *strokes)
{
    uint8_t b[64];
    size_t n = sim_disk_get(rel, b, sizeof(b));
    if (n == (size_t)-1 || n < 12 || memcmp(b, "FXP1", 4) != 0) return false;
    if (w) *w = b[4] | (b[5] << 8);
    if (h) *h = b[6] | (b[7] << 8);
    if (strokes) *strokes = b[8] | (b[9] << 8);
    return true;
}
static bool roto_intact(void)
{
    char b[64];
    size_t n = sim_disk_get("/Paint/roto.fxp", b, sizeof(b));
    return n == 20 && !memcmp(b, "esto no es un dibujo", 20);
}
static int disk_strokes(const char *rel)
{
    int s = -1;
    return disk_hdr(rel, NULL, NULL, &s) ? s : -1;
}

bool scene_paint_run(void)
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
    flex_session_forget_loaded(IC_PAINT);

    // ---- 1) galeria vacia; el boton flotante crea "Dibujo 1.fxp" con su cabecera y abre el lienzo
    flex_app_open(IC_PAINT, NULL);
    sim_run(500);
    CHK(wait_gallery() && flex_app_paint_count() == 0);
    sim_shot("paint_vacia");
    sim_tap(404, 654);
    CHK(wait_canvas());
    sim_run(200);
    CHK(wait_idle());
    CHK(!strcmp(flex_app_paint_path(), "/Paint/Dibujo 1.fxp") && sim_disk_exists("/Paint/Dibujo 1.fxp"));
    {
        int w, h, s;
        CHK(disk_hdr("/Paint/Dibujo 1.fxp", &w, &h, &s) && w == 464 && h == flex_paint_ch(NAV) && s == 0);
    }
    CHK(flex_app_paint_strokes() == 0 && flex_app_paint_color() == flex_paint_pal[0] && flex_app_paint_size() == 1);
    CHK(flex_app_paint_canvas_px(200, 300) == BG565);
    sim_shot("paint_lienzo_vacio");

    // ---- 2) un trazo con el dedo: sale EN VIVO en el lienzo y al levantar el dedo se guarda en el fichero
    sim_touch(100, 200, true);
    sim_run(60);
    sim_touch(200, 300, true);
    sim_run(60);
    sim_touch(300, 400, true);
    sim_run(60);
    CHK(flex_app_paint_pending() == 3);                                   // el trazo aun esta a medias
    CHK(flex_app_def(IC_PAINT)->ops->dirty());                              // y eso son cambios sin guardar
    CHK(flex_app_paint_canvas_px(200, 300) == flex_paint_pal[0]);          // y ya se ve
    CHK(disk_strokes("/Paint/Dibujo 1.fxp") == 0);                         // pero no esta en el disco
    sim_touch(300, 400, false);
    sim_run(100);
    CHK(wait_idle());
    CHK(flex_app_paint_pending() == 0 && flex_app_paint_strokes() == 1 && !flex_app_def(IC_PAINT)->ops->dirty());
    CHK(disk_strokes("/Paint/Dibujo 1.fxp") == 1);                         // al levantar el dedo
    CHK(flex_app_paint_canvas_px(150, 250) == flex_paint_pal[0] && flex_app_paint_canvas_px(400, 200) == BG565);
    sim_shot("paint_un_trazo");

    // ---- 3) color y grosor; el trazo guarda el color y el radio en unidades del dibujo
    tool_color(1);
    CHK(flex_app_paint_color() == flex_paint_pal[1]);
    stroke(60, 500, 400, 520);
    CHK(wait_idle() && disk_strokes("/Paint/Dibujo 1.fxp") == 2);
    CHK(flex_app_paint_canvas_px(230, 510) == flex_paint_pal[1]);
    tool_size();
    CHK(flex_app_paint_size() == 2);
    stroke(60, 580, 400, 590);
    CHK(wait_idle() && disk_strokes("/Paint/Dibujo 1.fxp") == 3);
    {
        uint8_t f[2048];
        size_t n = sim_disk_get("/Paint/Dibujo 1.fxp", f, sizeof(f));
        CHK(n != (size_t)-1);
        // recorre los trazos: colores 0, 1, 1 y radios 6, 6, 12
        size_t off = 12;
        const int want_col[3] = {0, 1, 1}, want_rad[3] = {6, 6, 12};
        for (int s = 0; s < 3 && off + 6 <= n; s++) {
            int col = f[off] | (f[off + 1] << 8), rad = f[off + 2], pts = f[off + 4] | (f[off + 5] << 8);
            CHK(col == flex_paint_pal[want_col[s]] && rad == want_rad[s] && pts >= 2);
            off += 6 + (size_t)pts * 4;
        }
        CHK(off == n);
    }
    CHK(flex_app_paint_canvas_px(230, 585) == flex_paint_pal[1] && flex_app_paint_canvas_px(230, 595) == flex_paint_pal[1]);   // el grueso
    sim_shot("paint_tres_trazos");

    // ---- 4) deshacer quita el ultimo trazo del fichero y repinta; limpiar deja el dibujo a cero
    tool_undo();
    CHK(wait_idle() && flex_app_paint_strokes() == 2 && disk_strokes("/Paint/Dibujo 1.fxp") == 2);
    CHK(flex_app_paint_canvas_px(230, 585) == BG565 && flex_app_paint_canvas_px(230, 510) == flex_paint_pal[1]);
    tool_undo();
    tool_undo();
    CHK(wait_idle() && flex_app_paint_strokes() == 0 && disk_strokes("/Paint/Dibujo 1.fxp") == 0);
    tool_undo();                                                           // sin trazos: no hace nada
    CHK(flex_app_paint_strokes() == 0 && !flex_app_paint_writing());
    CHK(flex_app_paint_canvas_px(150, 250) == BG565);
    stroke(80, 200, 380, 210);
    stroke(80, 260, 380, 270);
    CHK(wait_idle() && disk_strokes("/Paint/Dibujo 1.fxp") == 2);
    tool_clear();
    CHK(wait_idle() && flex_app_paint_strokes() == 0 && disk_strokes("/Paint/Dibujo 1.fxp") == 0);
    CHK(flex_app_paint_canvas_px(230, 205) == BG565);

    // ---- 5) volver a la galeria: la miniatura es el dibujo real; un dibujo vacio tiene la miniatura en blanco
    stroke(80, 200, 380, 400);
    stroke(80, 400, 380, 200);
    CHK(wait_idle() && disk_strokes("/Paint/Dibujo 1.fxp") == 2);
    back();
    CHK(wait_gallery() && flex_app_paint_count() == 1);
    CHK(!strcmp(flex_app_paint_title(0), "Dibujo 1.fxp"));
    CHK(flex_app_paint_thumb_present(0) && flex_app_paint_thumb_ink(0) > 200 && flex_app_paint_card_images(0) == 1);
    sim_shot("paint_galeria");
    sim_tap(404, 654);                                                     // otro dibujo, vacio
    CHK(wait_canvas());
    sim_run(200);
    CHK(!strcmp(flex_app_paint_path(), "/Paint/Dibujo 2.fxp"));
    back();
    CHK(wait_gallery() && flex_app_paint_count() == 2);
    CHK(!strcmp(flex_app_paint_title(1), "Dibujo 2.fxp") && flex_app_paint_thumb_ink(1) == 0 && flex_app_paint_thumb_ink(0) > 200);

    // ---- 6) abrir un dibujo guardado: se reconstruye del fichero
    {
        int x, y;
        card_xy(0, &x, &y);
        sim_tap(x, y);
        CHK(wait_canvas());
        sim_run(200);
        CHK(flex_app_paint_strokes() == 2 && !strcmp(flex_app_paint_path(), "/Paint/Dibujo 1.fxp"));
        CHK(flex_app_paint_canvas_px(230, 300) == flex_paint_pal[1]);   // el color de las herramientas cuando se dibujo (rojo)
        CHK(flex_app_paint_color() == flex_paint_pal[0] || flex_app_paint_color() == flex_paint_pal[1]);
        CHK(flex_app_paint_canvas_px(400, 600) == BG565);
        back();
        CHK(wait_gallery());
    }

    // ---- 7) menu de una tarjeta: renombrar (conserva la extension), seleccion y papelera, borrar
    {
        int x, y;
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);
        CHK(flex_fk_any(flex_app_paint_root()));
        menu_row(x, y - 40, FLEX_FK_ACT_REN);
        sim_run(200);
        for (int i = 0; i < 9; i++) dlg_fn(FLEX_KB_FN_BACKSPACE);          // borra "Dibujo 1"
        for (const char *w = "casa"; *w; w++) {
            char one[2] = {*w, 0};
            dlg_key(one);
        }
        dlg_fn(FLEX_KB_FKEYS - 1);                                         // Guardar
        CHK(wait_gallery());
        CHK(sim_disk_exists("/Paint/casa.fxp") && !sim_disk_exists("/Paint/Dibujo 1.fxp"));
        CHK(!strcmp(flex_app_paint_title(0), "casa.fxp") && !strcmp(flex_app_paint_title(1), "Dibujo 2.fxp"));
        CHK(flex_app_paint_thumb_ink(0) > 200);                            // la miniatura sigue siendo la suya
        // seleccion: marcar las dos y tirarlas a la papelera de una vez
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_SEL);
        int marked = 0;
        CHK(flex_app_paint_multi(&marked) && marked == 1);
        sim_shot("paint_seleccion");
        card_xy(1, &x, &y);
        sim_tap(x, y);
        sim_run(100);
        CHK(flex_app_paint_multi(&marked) && marked == 2);
        card_xy(1, &x, &y);
        sim_tap(x, y);                                                     // desmarcar
        sim_run(100);
        CHK(flex_app_paint_multi(&marked) && marked == 1);
        sim_tap(430, 702);                                                 // "Salir"
        sim_run(100);
        CHK(!flex_app_paint_multi(&marked) && marked == 0 && flex_app_paint_count() == 2);
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_SEL);
        card_xy(1, &x, &y);
        sim_tap(x, y);
        sim_run(100);
        sim_tap(300, 702);                                                 // "Papelera": sin confirmacion
        CHK(wait_gallery() && flex_app_paint_count() == 0);
        CHK(sim_disk_exists("/Papelera/@Paint@casa.fxp") && sim_disk_exists("/Papelera/@Paint@Dibujo 2.fxp"));
        CHK(!sim_disk_exists("/Paint/casa.fxp") && !flex_app_paint_multi(NULL));
        // la papelera desde los tres puntos: restaurar uno; al salir la galeria se relee y lo muestra
        sim_tap(450, 40);
        sim_run(100);
        menu_row(440, 56, FLEX_FK_ACT_TRASH);
        for (int i = 0; i < 60 && flex_fk_trash_count() < 2; i++) sim_run(20);
        CHK(flex_fk_trash_count() == 2);
        sim_tap(240, 108 + 12 + 25);
        sim_run(80);
        sim_tap(124, 706);                                                 // Restaurar
        for (int i = 0; i < 60 && flex_fk_trash_count() != 1; i++) sim_run(20);
        CHK(sim_disk_exists("/Paint/casa.fxp") || sim_disk_exists("/Paint/Dibujo 2.fxp"));
        sim_tap(24, 40);                                                   // sale de la papelera: la galeria se relee
        CHK(wait_gallery() && flex_app_paint_count() == 1);
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_DEL);
        flex_fk_rect_t rcard, rno, ryes;
        flex_fk_ask_geom(&rcard, &rno, &ryes);
        sim_tap(ryes.x + ryes.w / 2, ryes.y + 28);
        CHK(wait_gallery() && flex_app_paint_count() == 0);
        // nuevo dibujo y eliminarlo de verdad
        sim_tap(404, 654);
        CHK(wait_canvas());
        sim_run(200);
        back();
        CHK(wait_gallery() && flex_app_paint_count() == 1);
        card_xy(0, &x, &y);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_DEL);
        flex_fk_rect_t card, no, yes;
        flex_fk_ask_geom(&card, &no, &yes);
        sim_tap(no.x + no.w / 2, no.y + 28);                               // cancelar: sigue ahi
        sim_run(100);
        CHK(wait_gallery() && flex_app_paint_count() == 1);
        hold_xy(x, y, 700);
        menu_row(x, y - 40, FLEX_FK_ACT_DEL);
        sim_tap(yes.x + yes.w / 2, yes.y + 28);
        CHK(wait_gallery() && flex_app_paint_count() == 0);
    }

    // ---- 8) lo que NO es un dibujo: carpetas y ficheros ajenos no se listan; un .fxp roto no se abre y se dice
    {
        sim_disk_put("/Paint/roto.fxp", "esto no es un dibujo", 20);
        sim_disk_put("/Paint/notas.txt", "hola", 4);
        sim_disk_put("/Paint/Dibujo 7.fxp.tmp~", "FXP1", 4);                // resto de una escritura cortada
        sim_disk_mkdir("/Paint/carpeta.fxp");
        flex_app_close();                                                   // a Inicio y de vuelta: la lista se relee
        sim_run(400);
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(wait_gallery() && flex_app_paint_count() == 1 && !strcmp(flex_app_paint_title(0), "roto.fxp"));
        CHK(flex_app_paint_thumb_present(0) && flex_app_paint_thumb_ink(0) == 0);
        int x, y;
        card_xy(0, &x, &y);
        sim_tap(x, y);
        sim_run(300);
        CHK(flex_app_paint_view() == 0 && flex_kit_toast_count(flex_app_paint_root()) == 1);   // "No es un dibujo de Paint"
        CHK(roto_intact());
        sim_run(2000);
        flex_fs_delete_async("/Paint/roto.fxp", NULL, NULL);
        sim_run(100);
    }

    // ---- 9) un trazo a medias al suspender se guarda; al volver el lienzo sigue como estaba
    {
        sim_disk_put("/Paint/roto.fxp", "x", 0);   // sin restos de la prueba anterior
        flex_fs_delete_async("/Paint/roto.fxp", NULL, NULL);
        flex_fs_delete_async("/Paint/notas.txt", NULL, NULL);
        flex_fs_delete_async("/Paint/carpeta.fxp", NULL, NULL);
        flex_fs_delete_async("/Paint/Dibujo 7.fxp.tmp~", NULL, NULL);
        flex_app_close();
        sim_run(400);
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(wait_gallery() && flex_app_paint_count() == 0);   // el apartado 7 termino sin dibujos
        sim_tap(404, 654);
        CHK(wait_canvas());
        sim_run(200);
        const char *path = flex_app_paint_path();
        char saved[96];
        snprintf(saved, sizeof(saved), "%s", path);
        tool_color(2);
        tool_size();
        stroke(80, 200, 380, 300);
        CHK(wait_idle() && flex_app_paint_strokes() == 1);
        sim_touch(100, 450, true);
        sim_run(60);
        sim_touch(240, 520, true);
        sim_run(60);
        CHK(flex_app_paint_pending() == 2 && flex_app_paint_strokes() == 1);
        flex_sys_home();                                                   // suspende con el dedo apoyado
        sim_run(500);
        CHK(wait_idle() && disk_strokes(saved) == 2);                      // el trazo a medias se guardo ANTES de soltar el dedo
        sim_touch(240, 520, false);
        sim_run(200);
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(flex_app_paint_view() == 1 && flex_app_paint_strokes() == 2 && flex_app_paint_pending() == 0);
        CHK(flex_app_paint_canvas_px(170, 485) == flex_paint_pal[2]);      // lo pintado sigue ahi
        CHK(flex_app_paint_color() == flex_paint_pal[2] && flex_app_paint_size() == 0);
    }

    // ---- 10) sesion: vista, ruta, color y grosor, con el formato de Arduino (cabecera de 16 B + 104 B)
    {
        char saved[96];
        snprintf(saved, sizeof(saved), "%s", flex_app_paint_path());
        flex_session_flush_now();
        sim_run(200);
        static uint8_t file[512];
        size_t n = sim_disk_get("/System/Sessions/paint.bin", file, sizeof(file));
        CHK(n == 16 + 104);
        flex_app_terminate_ex(IC_PAINT, true);                             // cerrar de verdad
        sim_run(300);
        flex_session_forget_loaded(IC_PAINT);
        flex_session_test_preload(IC_PAINT, file, n);
        flex_app_open(IC_PAINT, NULL);
        sim_run(600);
        CHK(wait_canvas() && flex_app_paint_strokes() == 2 && !strcmp(flex_app_paint_path(), saved));
        CHK(flex_app_paint_color() == flex_paint_pal[2] && flex_app_paint_size() == 0);
        CHK(flex_app_paint_canvas_px(170, 485) == flex_paint_pal[2]);
        // una sesion de un dibujo que ya no existe vuelve a la galeria
        flex_fs_delete_async(saved, NULL, NULL);
        sim_run(100);
        flex_app_terminate_ex(IC_PAINT, true);
        sim_run(300);
        flex_session_forget_loaded(IC_PAINT);
        flex_session_test_preload(IC_PAINT, file, n);
        flex_app_open(IC_PAINT, NULL);
        sim_run(600);
        CHK(wait_gallery() && flex_app_paint_count() == 0);
        flex_app_terminate_ex(IC_PAINT, true);
        sim_run(300);
        flex_session_forget_loaded(IC_PAINT);
    }

    // ---- 11) el disco falla al guardar: el dibujo sigue en memoria, se avisa y el siguiente trazo lo guarda todo
    {
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(wait_gallery());
        sim_tap(404, 654);
        CHK(wait_canvas());
        sim_run(200);
        CHK(wait_idle());
        char saved[96];
        snprintf(saved, sizeof(saved), "%s", flex_app_paint_path());
        stroke(80, 200, 380, 260);
        CHK(wait_idle() && disk_strokes(saved) == 1);
        sim_fs_fail_writes(false, true);
        stroke(80, 300, 380, 360);
        sim_run(200);
        CHK(flex_app_paint_strokes() == 2 && disk_strokes(saved) == 1);     // en memoria si, en el disco no
        CHK(flex_app_paint_writing() && flex_kit_toast_count(flex_app_paint_root()) == 1);
        sim_run(2000);
        sim_fs_fail_writes(false, false);
        stroke(80, 400, 380, 460);
        CHK(wait_idle() && flex_app_paint_strokes() == 3 && disk_strokes(saved) == 3);   // el tercero lleva tambien el segundo
        // un deshacer que no se puede guardar tampoco pierde nada: al salir se reintenta
        sim_fs_fail_writes(false, true);
        tool_undo();
        sim_run(200);
        CHK(flex_app_paint_strokes() == 2 && disk_strokes(saved) == 3 && flex_app_paint_writing());
        sim_fs_fail_writes(false, false);
        back();
        CHK(wait_gallery() && flex_app_paint_count() == 1 && flex_app_paint_thumb_ink(0) > 100);
        CHK(disk_strokes(saved) == 2);
        sim_run(2500);
        CHK(flex_kit_toast_count(flex_app_paint_root()) == 0);               // el aviso de guardado ya se fue
    }

    // ---- 12) mas de 24 dibujos: se listan 24 ordenados y se avisa; la galeria se desplaza y se abre uno del final
    {
        static const uint8_t hdr[12] = {'F', 'X', 'P', '1', 0xD0, 0x01, 0x3E, 0x02, 0, 0, 0, 0};
        for (int i = 0; i < 30; i++) {
            char nm[32];
            snprintf(nm, sizeof(nm), "/Paint/d%02d.fxp", i);
            sim_disk_put(nm, hdr, sizeof(hdr));
        }
        flex_app_close();
        sim_run(400);
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(wait_gallery() && flex_app_paint_count() == 24);
        CHK(flex_kit_toast_count(flex_app_paint_root()) == 1);
        CHK(!strcmp(flex_app_paint_title(0), "Dibujo 1.fxp") || !strcmp(flex_app_paint_title(0), "d00.fxp"));
        int sc0 = flex_app_paint_scroll();
        sim_drag(240, 600, 240, 200, 300);
        sim_run(300);
        CHK(flex_app_paint_scroll() > sc0);
        int x, y;
        card_xy(4, &x, &y);                                                // el 24.o no esta a la vista con un solo gesto
        sim_tap(x, y);
        CHK(wait_canvas());
        sim_run(200);
        CHK(flex_app_paint_strokes() == 0 && !strcmp(flex_app_paint_path(), "/Paint/d04.fxp"));
        back();
        CHK(wait_gallery());
        sim_shot("paint_galeria_llena");
    }

    // ---- 13) soltar memoria (shed): la galeria suelta las miniaturas y el lienzo su bufer; al volver se reconstruyen
    {
        flex_app_close();
        sim_run(400);
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(wait_gallery() && flex_app_paint_thumb_present(4) && flex_app_paint_card_images(4) == 1);
        flex_sys_home();                                                    // suspendida
        sim_run(400);
        size_t freed = flex_app_def(IC_PAINT)->ops->shed();
        CHK(freed > 100000 && !flex_app_paint_thumb_present(4) && flex_app_paint_card_images(4) == -1);
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(wait_gallery() && flex_app_paint_thumb_present(4) && flex_app_paint_card_images(4) == 1);
        // el lienzo: el dibujo "d04" se llena de un trazo, se suspende, se suelta el bufer y vuelve igual
        int x, y;
        sim_drag(240, 600, 240, 200, 300);                                  // la tarjeta 4 queda a la vista
        sim_run(300);
        card_xy(4, &x, &y);
        sim_tap(x, y);
        CHK(wait_canvas());
        sim_run(200);
        stroke(100, 200, 360, 400);
        CHK(wait_idle() && flex_app_paint_canvas_px(230, 300) == (int)flex_app_paint_color());
        int col = (int)flex_app_paint_color();
        flex_sys_home();
        sim_run(400);
        CHK(flex_app_def(IC_PAINT)->ops->shed() > 100000 && flex_app_paint_canvas_px(230, 300) == -1);   // el bufer ya no esta
        flex_app_open(IC_PAINT, NULL);
        sim_run(500);
        CHK(flex_app_paint_view() == 1 && flex_app_paint_strokes() == 1 && flex_app_paint_canvas_px(230, 300) == col);
        sim_shot("paint_tras_shed");
    }

    return ok;
}
