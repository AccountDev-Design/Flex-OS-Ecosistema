// Almacenamiento y Archivos (03 §8-§9): la app real sobre un DISCO REAL (directorio temporal con las mismas operaciones que la tarea de
// almacenamiento) y con el dedo. Pantalla principal (categorias medidas de verdad, "..." mientras la medida no llega, archivos grandes sin la
// biblioteca), tarjeta del telefono (aviso honesto: no hay servicio), detalles de memoria y sistema (cinco secciones, scroll, refresco por
// firma, Optimizar), explorador (carpetas, "..", pulsacion larga, menu, borrar, renombrar, papelera, seleccion multiple, nube no disponible,
// lista de 256 con filas por ventana, biblioteca oculta, sin almacenamiento) y su cierre modal (Atras, Inicio, bloqueo).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "FlexOS_Mem.h"
#include "flex_filekit.h"
#include "flex_files_model.h"
#include "flex_fk_model.h"
#include "flex_frame.h"
#include "flex_fs_names.h"
#include "flex_i18n.h"
#include "flex_kb.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_stor_model.h"
#include "flex_stor_ui.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void sim_disk_enable(bool on);
bool sim_disk_put(const char *rel, const void *d, size_t l);
bool sim_disk_exists(const char *rel);
bool sim_disk_is_dir(const char *rel);
bool sim_disk_mkdir(const char *rel);
void sim_system_set(uint32_t fw_size, int reset_reason);
void sim_metrics_set_loops(float n);
void sim_fs_mounted(bool on);
void sim_net_online(bool on);
void sim_explore_hold(bool on);
void sim_explore_release(void);
void sim_explore_release_n(int n);
bool flex_files_row_is_sel(int i);
int sim_fs_wipes(const char **last);
void flex_mem_sim_set(const FlexMemSnap *s);
int flex_optimize_stage(void);

int flex_app_storage_screen(void);
bool flex_app_storage_scanned(void);
bool flex_app_storage_scan_failed(void);
int flex_app_storage_builds(void);
bool flex_app_storage_zone_xy(int zone, int *x, int *y);
bool flex_app_storage_opt_xy(int *x, int *y);
int flex_app_storage_scroll(void);
bool flex_app_storage_shown(const char *txt);

bool flex_files_active(void);
const char *flex_files_dir(void);
int flex_files_count(void);
bool flex_files_loaded(void);
bool flex_files_in_cloud(void);
bool flex_files_multi(void);
int flex_files_marked(void);
int flex_files_busy(void);
int flex_files_scroll(void);
int flex_files_total_rows(void);
int flex_files_live_rows(void);
int flex_files_layers(void);
bool flex_files_nofs(void);
const char *flex_files_name_at(int i);
bool flex_files_row_xy(int i, int *x, int *y);
bool flex_files_up_xy(int *x, int *y);
bool flex_files_shown(const char *txt);

#define CHK(c)                                                         \
    do {                                                               \
        if (!(c)) {                                                    \
            printf("almacen: FALLO linea %d: %s\n", __LINE__, #c);     \
            ok = false;                                                \
        }                                                              \
    } while (0)

static void put_n(const char *rel, size_t n)
{
    char *b = calloc(1, n ? n : 1);
    if (b) {
        memset(b, 'x', n);
        sim_disk_put(rel, b, n);
        free(b);
    }
}

// el historial de avisos tiene tope (con las demas escenas en el mismo proceso ya esta lleno): se pone un centinela delante y se mira el titulo
// del mas reciente
static void sentinel(void)
{
    flex_notify("-", "-");
    sim_run(100);
}
static bool notified(const char *title)
{
    const char *t = flex_notif_latest_title();
    return t && !strcmp(t, title);
}

// el banner de un aviso cubre la parte de arriba y se queda con los toques: se espera a que se vaya antes de tocar la cabecera
static void calm(int y)
{
    for (int i = 0; y < 110 && i < 80 && flex_notif_banner_visible(); i++) {
        sim_run(100);
    }
}

static void tap(int x, int y)
{
    calm(y);
    sim_touch(x, y, true);
    sim_run(60);
    sim_touch(x, y, false);
    sim_run(160);
}

static void hold(int x, int y, int ms)
{
    sim_touch(x, y, true);
    sim_run(ms);
    sim_touch(x, y, false);
    sim_run(160);
}

static void unlock_and_clean(void)
{
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
    for (int i = 0; i < FLEX_APP_N; i++) {
        flex_app_terminate_ex(i, true);
    }
    sim_run(200);
}

// las cinco etapas de Optimizar y el boton "Hecho"
static bool finish_optimize_ok;
static void finish_optimize(void)
{
    for (int i = 0; i < 80 && flex_optimize_stage() != 5; i++) {
        sim_run(100);
    }
    finish_optimize_ok = flex_optimize_stage() == 5;
    sim_tap(240, 545);   // "Hecho"
    sim_run(400);
}

static void seed(void)
{
    sim_disk_enable(true);   // arranca vacio
    put_n("/Documentos/a.txt", 409600);
    put_n("/Documentos/big.bin", 2 * 1024 * 1024 + 17);
    sim_disk_mkdir("/Documentos/Cartas");
    put_n("/Documentos/Cartas/c1.txt", 1500);
    put_n("/Notas/n1.txt", 400);
    put_n("/Paint/d.fxp", 1000);
    put_n("/System/Cache/c1.bin", 5000);
    put_n("/System/Media/lib.dat", 3000000);   // la biblioteca por dentro: cuenta como Sistema, pero no es un "archivo grande" ni se ve en el explorador
    put_n("/System/config.bin", 700);
    put_n("/Papelera/@Notas@old.txt", 250);
}

static bool open_storage(void)
{
    flex_app_open(IC_ALMACEN, NULL);
    sim_run(600);
    return flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_ALMACEN;
}

static bool wait_files(void)
{
    for (int i = 0; i < 60; i++) {
        if (flex_files_loaded()) {
            return true;
        }
        sim_run(20);
    }
    return false;
}

static bool wait_scan(void)
{
    for (int i = 0; i < 60; i++) {
        if (flex_app_storage_scanned()) {
            return true;
        }
        sim_run(20);
    }
    return false;
}

static bool wait_idle(void)
{
    for (int i = 0; i < 60; i++) {
        if (flex_files_busy() == 0 && flex_files_loaded()) {
            return true;
        }
        sim_run(20);
    }
    return false;
}

static void sz(uint32_t b, char *out, size_t n)
{
    flex_fsn_fmt_size(b, out, n);
}

static int row_of(const char *name)
{
    for (int i = 0; i < flex_files_count(); i++) {
        if (!strcmp(flex_files_name_at(i), name)) {
            return i;
        }
    }
    return -1;
}

static void tap_row(const char *name)
{
    int i = row_of(name), x, y;
    if (i >= 0 && flex_files_row_xy(i, &x, &y)) {
        tap(x, y);
    }
}

static void menu_item(int px, int py, bool cloud, int item)
{
    flex_fk_rect_t r;
    flex_fk_menu_geom(px, py, cloud, &r);
    tap(r.x + 100, r.y + FLEX_FK_MENU_PAD + item * FLEX_FK_MENU_RH + 14);
}


// ---- el pintor de operaciones: cada tipo de operacion deja el objeto LVGL con la geometria y el color que pide -------------------------------------
static void add_op(flex_st_page_t *pg, int kind, int x, int y, int w, int h, int r, int role, uint16_t rgb, int size, int max_right, const char *text)
{
    flex_st_op_t *o = &pg->op[pg->n++];
    memset(o, 0, sizeof(*o));
    o->kind = (uint8_t)kind;
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->w = (int16_t)w;
    o->h = (int16_t)h;
    o->r = (int16_t)r;
    o->role = (uint8_t)role;
    o->rgb = rgb;
    o->size = (uint8_t)size;
    o->max_right = (int16_t)max_right;
    if (text) {
        snprintf(o->text, sizeof(o->text), "%s", text);
    }
}
static bool same(lv_color_t a, lv_color_t b)
{
    return lv_color_to_u32(a) == lv_color_to_u32(b);
}
static bool render_ops(void)
{
    bool ok = true;
    const flex_palette_t *t = flex_th();
    // los roles de color resuelven a la paleta del tema
    CHK(same(flex_st_ui_color(FSC_TXT, 0), t->txt) && same(flex_st_ui_color(FSC_TXT2, 0), t->txt2) && same(flex_st_ui_color(FSC_MUTE, 0), t->mute));
    CHK(same(flex_st_ui_color(FSC_OK, 0), t->ok) && same(flex_st_ui_color(FSC_WARN, 0), t->warn) && same(flex_st_ui_color(FSC_ERR, 0), t->err));
    CHK(same(flex_st_ui_color(FSC_PRIM, 0), t->primary) && same(flex_st_ui_color(FSC_ONACC, 0), t->on_acc) && same(flex_st_ui_color(FSC_ACCS, 0), t->acc_soft));
    CHK(same(flex_st_ui_color(FSC_TRACK, 0), t->track) && same(flex_st_ui_color(FSC_BORDER, 0), t->border) && same(flex_st_ui_color(FSC_SURF, 0), t->surf));
    CHK(same(flex_st_ui_color(FSC_ACCENT, 0), flex_accent()) && same(flex_st_ui_color(FSC_SEL, 0), t->sel) && same(flex_st_ui_color(FSC_SURF2, 0), t->surf2));
    CHK(same(flex_st_ui_color(FSC_RGB, 0xF800), lv_color_make(255, 0, 0)));
    CHK(!same(flex_st_ui_color(FSC_WARN, 0), flex_st_ui_color(FSC_OK, 0)) && !same(flex_st_ui_color(FSC_ACCENT, 0), t->txt));

    lv_obj_t *par = flex_box(flex_shell_screen());
    lv_obj_set_pos(par, 0, 0);
    lv_obj_set_size(par, 480, 800);
    lv_obj_move_foreground(par);
    static flex_st_page_t pg;
    memset(&pg, 0, sizeof(pg));
    add_op(&pg, FST_RRECT, 10, 20, 100, 30, 8, FSC_RGB, 0xF800, 0, 0, NULL);                       // 0
    add_op(&pg, FST_OUTLINE, 10, 60, 100, 30, 8, FSC_BORDER, 0, 0, 0, NULL);                        // 1
    add_op(&pg, FST_DOT, 50, 120, 0, 0, 4, FSC_RGB, 0x07E0, 0, 0, NULL);                            // 2
    add_op(&pg, FST_TEXT, 12, 140, 0, 0, 0, FSC_TXT2, 0, 2, 0, "Hola");                             // 3
    add_op(&pg, FST_TEXT, 12, 170, 0, 0, 0, FSC_TXT, 0, 2, 100, "Un texto bastante largo para recortar");   // 4
    add_op(&pg, FST_TEXT_R, 300, 200, 0, 0, 0, FSC_TXT, 0, 2, 0, "12 KB");                          // 5
    add_op(&pg, FST_TEXT_C, 240, 240, 0, 0, 0, FSC_TXT, 0, 2, 0, "Centro");                         // 6
    add_op(&pg, FST_FOLDER, 20, 280, 38, 0, 0, FSC_TXT, 0, 0, 0, NULL);                             // 7, 8, 9
    add_op(&pg, FST_CHEV, 400, 300, 0, 0, 0, FSC_TXT2, 0, 0, 0, NULL);                              // 10
    add_op(&pg, FST_CLOUD, 60, 400, 0, 0, 13, FSC_ACCENT, 0, 0, 0, NULL);                           // 11
    add_op(&pg, FST_SURF, 10, 450, 200, 50, 12, FSC_SURF_CARD, 0, 0, 0, NULL);                      // 12
    add_op(&pg, FST_SURF, 10, 520, 200, 50, 12, FSC_SURF_ELEV, 0, 0, 0, NULL);                      // 13
    int made = flex_st_ui_render(par, &pg, 0, 0);
    CHK(made == pg.n);
    lv_obj_update_layout(par);
    CHK(lv_obj_get_child_count(par) == 14);   // la carpeta son tres rectangulos
    lv_area_t a;
    lv_obj_t *o = lv_obj_get_child(par, 0);
    lv_obj_get_coords(o, &a);
    CHK(a.x1 == 10 && a.y1 == 20 && lv_area_get_width(&a) == 100 && lv_area_get_height(&a) == 30);
    CHK(lv_obj_get_style_radius(o, 0) == 8 && same(lv_obj_get_style_bg_color(o, 0), lv_color_make(255, 0, 0)) && lv_obj_get_style_bg_opa(o, 0) == LV_OPA_COVER);
    o = lv_obj_get_child(par, 1);
    lv_obj_get_coords(o, &a);
    CHK(a.x1 == 10 && a.y1 == 60 && lv_area_get_width(&a) == 100 && lv_area_get_height(&a) == 30);
    CHK(lv_obj_get_style_border_width(o, 0) == 1 && same(lv_obj_get_style_border_color(o, 0), t->border) && lv_obj_get_style_bg_opa(o, 0) == LV_OPA_TRANSP);
    o = lv_obj_get_child(par, 2);   // el punto: centro (50,120), radio 4
    lv_obj_get_coords(o, &a);
    CHK(a.x1 == 46 && a.y1 == 116 && lv_area_get_width(&a) == 9 && lv_area_get_height(&a) == 9 && lv_obj_get_style_radius(o, 0) == LV_RADIUS_CIRCLE);
    CHK(same(lv_obj_get_style_bg_color(o, 0), lv_color_make(0, 255, 0)));
    o = lv_obj_get_child(par, 3);
    CHK(!strcmp(lv_label_get_text(o), "Hola") && same(lv_obj_get_style_text_color(o, 0), t->txt2));
    lv_obj_get_coords(o, &a);
    CHK(a.x1 == 12);
    o = lv_obj_get_child(par, 4);   // recortado a la derecha en x = 100 SIN puntos suspensivos
    CHK(lv_label_get_long_mode(o) == LV_LABEL_LONG_CLIP && lv_obj_get_width(o) == 100 - 12);
    o = lv_obj_get_child(par, 5);   // alineado a la derecha en x = 300
    lv_obj_get_coords(o, &a);
    CHK(lv_obj_get_style_text_align(o, 0) == LV_TEXT_ALIGN_RIGHT && a.x2 == 299 && a.x1 == 0);
    o = lv_obj_get_child(par, 6);   // centrado en x = 240
    lv_obj_get_coords(o, &a);
    CHK(abs((a.x1 + a.x2) / 2 - 240) <= 1);
    flex_st_rect_t fr[3];
    flex_st_folder_rects(20, 280, 38, fr);
    for (int k = 0; k < 3; k++) {
        o = lv_obj_get_child(par, 7 + k);
        lv_obj_get_coords(o, &a);
        CHK(a.x1 == fr[k].x && a.y1 == fr[k].y && lv_area_get_width(&a) == fr[k].w && lv_area_get_height(&a) == fr[k].h);
        CHK(lv_obj_get_style_radius(o, 0) == fr[k].r && same(lv_obj_get_style_bg_color(o, 0), flex_c565(fr[k].rgb)));
    }
    o = lv_obj_get_child(par, 10);   // el chevron ">" ocupa 8 x 15 desde (400,300)
    lv_obj_get_coords(o, &a);
    CHK(a.x1 <= 400 && a.x2 >= 407 && a.y1 <= 300 && a.y2 >= 314 && same(lv_obj_get_style_text_color(o, 0), t->txt2));
    o = lv_obj_get_child(par, 11);   // la nube de r = 13 centrada en (60,400)
    lv_obj_get_coords(o, &a);
    CHK(lv_area_get_width(&a) == 39 && lv_area_get_height(&a) == 39 && (a.x1 + a.x2) / 2 == 60 && (a.y1 + a.y2) / 2 == 400);
    CHK(same(lv_obj_get_style_text_color(o, 0), flex_accent()));
    lv_obj_t *card = lv_obj_get_child(par, 12), *elev = lv_obj_get_child(par, 13);
    lv_obj_get_coords(card, &a);
    CHK(a.x1 == 10 && a.y1 == 450 && lv_area_get_width(&a) == 200 && lv_area_get_height(&a) == 50 && lv_obj_get_style_radius(card, 0) == 12);
    (void)elev;   // el material (tarjeta / elevada) lo pinta flex_surface: se ve en las capturas, no en un estilo que leer
    // el desplazamiento de las coordenadas del modelo al padre
    lv_obj_delete(par);
    par = flex_box(flex_shell_screen());
    lv_obj_set_pos(par, 0, 0);
    lv_obj_set_size(par, 480, 800);
    made = flex_st_ui_render(par, &pg, 7, 11);
    lv_obj_update_layout(par);
    lv_obj_get_coords(lv_obj_get_child(par, 0), &a);
    CHK(a.x1 == 17 && a.y1 == 31);
    lv_obj_delete(par);
    return ok;
}

bool scene_almacen_run(void)
{
    bool ok = true;
    unlock_and_clean();
    ok = render_ops() && ok;
    seed();
    {
        FlexMemSnap m = {.psTotal = 32u << 20, .psFree = 20u << 20, .psLargest = 16u << 20, .psPeakUsed = 12u << 20,
                         .inTotal = 400u << 10, .inFree = 250u << 10, .inMin = 200u << 10, .fsTotal = 0xAE0000, .fsUsed = 3u << 20, .fsValid = 1};
        flex_mem_sim_set(&m);
    }
    sim_system_set(1564656, 1);
    sim_metrics_set_loops(58.0f);
    char a[40], b[40], c[40];

    // ---- 1) pantalla principal: "..." mientras la medida no llega, y despues las cifras REALES del disco ---------------------------
    CHK(open_storage());
    sim_shot("almacen_principal");
    CHK(flex_app_storage_screen() == 0);
    CHK(flex_app_storage_shown("Almacenamiento") && flex_app_storage_shown("Memoria interna"));
    CHK(flex_app_storage_shown("PSRAM") && flex_app_storage_shown("12 / 32 MB  (38%)") && !flex_app_storage_shown("No disponible en esta placa"));
    CHK(flex_app_storage_shown("Documentos") && flex_app_storage_shown("Sistema") && flex_app_storage_shown("Aplicaciones") &&
        flex_app_storage_shown("Papelera"));
    CHK(wait_scan() && !flex_app_storage_scan_failed());
    uint32_t docs = 409600 + (2 * 1024 * 1024 + 17) + 1500 + 400;
    uint32_t sys = 5000 + 3000000 + 700;
    sz(docs, a, sizeof(a));
    sz(sys, b, sizeof(b));
    sz(1000, c, sizeof(c));
    CHK(flex_app_storage_shown(a) && flex_app_storage_shown(b) && flex_app_storage_shown(c));
    sz(250, a, sizeof(a));
    CHK(flex_app_storage_shown(a));   // la papelera
    CHK(!flex_app_storage_shown("..."));   // ya no hay marcadores de medida pendiente
    CHK(!flex_app_storage_shown("Calculando..."));
    CHK(flex_app_storage_shown("Detalles de memoria y sistema") && flex_app_storage_shown("Optimizar"));
    CHK(flex_app_storage_shown("Flex Cloud en tu tel\xC3\xA9" "fono") && flex_app_storage_shown("No disponible todav\xC3\xAD" "a"));
    CHK(flex_app_storage_shown("Todos los archivos") && flex_app_storage_shown("Ver..."));
    // el archivo grande es el de la carpeta de usuario; el de la biblioteca (3 MB) no aparece
    CHK(flex_app_storage_shown("big.bin") || flex_app_storage_shown("/Documentos/big.bin"));
    CHK(!flex_app_storage_shown("lib.dat") && !flex_app_storage_shown("/System/Media/lib.dat"));

    // ---- 2) tarjeta del telefono: aviso honesto, la pantalla no cambia -----------------------------------------------------------
    {
        int x, y;
        sentinel();
        CHK(flex_app_storage_zone_xy(0, &x, &y));
        tap(x, y);
        CHK(flex_app_storage_screen() == 0 && !flex_files_active() && !flex_optimize_active());
        CHK(notified("Flex Cloud en tu tel\xC3\xA9" "fono"));
    }

    // ---- 3) detalles: cinco secciones, cifras reales, scroll, atras ----------------------------------------------------------------
    {
        int x, y;
        CHK(flex_app_storage_zone_xy(1, &x, &y));
        tap(x, y);
        CHK(flex_app_storage_screen() == 1);
        sim_shot("almacen_detalles");
        CHK(flex_app_storage_shown("Detalles de memoria y sistema"));
        CHK(flex_app_storage_shown("PSRAM (memoria de trabajo)") && flex_app_storage_shown("Memoria interna del chip (SRAM)"));
        CHK(flex_app_storage_shown("Almacenamiento interno (flash)") && flex_app_storage_shown("Estado del sistema"));
        CHK(flex_app_storage_shown("Optimizar Flex OS"));
        flexMemFmt(1564656, a, sizeof(a));
        CHK(flex_app_storage_shown("Tama\xC3\xB1o del firmware") && flex_app_storage_shown(a));
        CHK(flex_app_storage_shown("58 vueltas/s"));
        CHK(flex_app_storage_shown("Flex OS 1.0.0"));
        CHK(flex_app_storage_shown("Desconectado") && !flex_app_storage_shown("Conectado"));
        sim_net_online(true);   // el estado de la red entra en la firma de la seccion: se repinta solo
        sim_run(1400);
        CHK(flex_app_storage_shown("Conectado") && !flex_app_storage_shown("Desconectado"));
        sim_net_online(false);
        sim_run(1400);
        CHK(flex_app_storage_shown("Desconectado"));
        CHK(flex_app_storage_shown(flex_st_boot_cause(1)));   // el motivo del ultimo arranque
        flexMemFmt(5000, a, sizeof(a));
        CHK(flex_app_storage_shown(a));   // la cache temporal medida
        flexMemFmt(3000000 + 700, a, sizeof(a));
        CHK(flex_app_storage_shown(a));   // recursos del sistema (todo /System menos la cache)
        // desplazamiento nativo: llega al final de las cinco secciones y no mas
        int mx0 = flex_st_content_h() + 8;
        sim_drag(240, 650, 240, 150, 400);
        sim_run(200);
        int sc = flex_app_storage_scroll();
        CHK(sc > 0 && sc <= mx0);
        sim_drag(240, 650, 240, 100, 400);
        sim_run(200);
        sim_drag(240, 650, 240, 100, 400);
        sim_run(200);
        CHK(flex_app_storage_scroll() <= mx0 - 640 + 8 + 64);
        sim_shot("almacen_detalles_fin");
        // el refresco de cifras: cambia el ritmo y el firmware no (se mide una vez); se repinta la seccion de estado con el dedo fuera
        sim_metrics_set_loops(31.0f);
        sim_run(1400);
        CHK(flex_app_storage_shown("31 vueltas/s") && !flex_app_storage_shown("58 vueltas/s"));
        sim_metrics_set_loops(58.0f);
        sim_run(1400);
        CHK(flex_app_storage_shown("58 vueltas/s"));
        // Atras vuelve a la lista (no expulsa la app) y el segundo cierra la app
        flex_sys_back();
        sim_run(200);
        CHK(flex_app_storage_screen() == 0 && flex_shell_state() == FLEX_SH_APP);
        CHK(!flex_app_storage_shown("PSRAM (memoria de trabajo)"));
    }

    // ---- 4) Optimizar desde la tarjeta de la lista y desde los detalles; al terminar vuelve a medir ----------------------------------
    {
        int x, y;
        put_n("/System/Cache/c2.bin", 7000);
        CHK(flex_app_storage_zone_xy(2, &x, &y));
        tap(x, y);
        CHK(flex_optimize_active());
        finish_optimize();
        CHK(!flex_optimize_active());
        CHK(wait_scan());
        CHK(flex_app_storage_screen() == 0);
        // la cache temporal se vacio de verdad
        {
            const char *last = NULL;
            CHK(sim_fs_wipes(&last) > 0 && last && !strcmp(last, "/System/Cache"));   // la etapa de la cache pidio vaciar exactamente esa carpeta
        }
        // ...y desde los detalles
        CHK(flex_app_storage_zone_xy(1, &x, &y));
        tap(x, y);
        CHK(flex_app_storage_screen() == 1);
        CHK(flex_app_storage_opt_xy(&x, &y));
        tap(x, y);
        CHK(flex_optimize_active());
        finish_optimize();
        CHK(!flex_optimize_active() && flex_app_storage_screen() == 1);
        flex_sys_back();
        sim_run(200);
        CHK(flex_app_storage_screen() == 0);
    }

    // ---- 5) explorador: raiz, carpetas, "..", biblioteca oculta --------------------------------------------------------------------
    {
        int x, y;
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(flex_files_active() && wait_files());
        sim_shot("archivos_raiz");
        CHK(!strcmp(flex_files_dir(), "/") && flex_files_count() == 5);   // Documentos, Notas, Paint, Papelera y System
        CHK(flex_files_shown("Archivos:") && flex_files_shown("Este dispositivo") && flex_files_shown("Flex Cloud"));
        tap_row("Documentos");
        CHK(wait_files() && !strcmp(flex_files_dir(), "/Documentos"));
        CHK(flex_files_count() == 3);
        CHK(flex_files_shown("/Documentos") && !flex_files_shown("/"));   // la ruta de arriba sigue a la carpeta
        CHK(!strcmp(flex_files_name_at(0), "Cartas"));   // las carpetas primero
        sim_shot("archivos_documentos");
        CHK(flex_files_up_xy(&x, &y));
        tap(x, y);   // ".."
        CHK(wait_files() && !strcmp(flex_files_dir(), "/"));
        // /System: la biblioteca no se ve ni deja entrar
        tap_row("System");
        CHK(wait_files() && !strcmp(flex_files_dir(), "/System"));
        CHK(row_of("Media") < 0 && row_of("Cache") >= 0 && row_of("config.bin") >= 0);
        flex_sys_back();   // el gesto de Atras sale del explorador (en Arduino solo salia la flecha)
        sim_run(300);
        CHK(!flex_files_active() && flex_app_storage_screen() == 0);
    }

    // ---- 6) pulsacion larga -> menu; borrar con confirmacion; renombrar; la accion se hace en el disco --------------------------------
    {
        int x, y;
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        tap_row("Documentos");
        CHK(wait_files());
        int i = row_of("a.txt");
        CHK(i >= 0 && flex_files_row_xy(i, &x, &y));
        hold(x, y, 800);   // pulsacion larga
        CHK(flex_files_layers() == 1);
        CHK(flex_files_dir() && !strcmp(flex_files_dir(), "/Documentos"));   // el menu no abrio el archivo ni cambio de carpeta
        sim_shot("archivos_menu");
        menu_item(x, y - 40, true, 1);   // Eliminar
        sim_run(100);
        CHK(flex_files_layers() == 1);
        flex_fk_rect_t card, no, yes;
        flex_fk_ask_geom(&card, &no, &yes);
        tap(no.x + no.w / 2, no.y + 28);   // Cancelar: no se borra nada
        sim_run(100);
        CHK(sim_disk_exists("/Documentos/a.txt") && flex_files_layers() == 0);
        hold(x, y, 800);
        menu_item(x, y - 40, true, 1);
        sim_run(100);
        tap(yes.x + yes.w / 2, yes.y + 28);   // Borrar
        CHK(wait_idle());
        sim_run(200);
        CHK(!sim_disk_exists("/Documentos/a.txt") && row_of("a.txt") < 0 && flex_files_count() == 2);
        // renombrar con el teclado del sistema
        i = row_of("big.bin");
        CHK(i >= 0 && flex_files_row_xy(i, &x, &y));
        hold(x, y, 800);
        menu_item(x, y - 40, true, 2);   // Renombrar
        sim_run(100);
        CHK(flex_files_layers() == 1);
        {
            flex_kb_geom_t g;
            flex_kb_geom_init(&g, FLEX_KB_SIZE_NORMAL, 64, 0);
            flex_kb_state_t st;
            flex_kb_state_init(&st, true);
            const char *txt = "z";
            for (int c = 0; c < FLEX_KB_CELLS; c++) {
                const char *k = flex_kb_key_base(&st, c);
                if (k[0] == txt[0] && k[1] == 0) {
                    int kx, ky;
                    flex_kb_cell_xy(&g, c, &kx, &ky);
                    sim_tap(kx + g.kw / 2, ky + g.kh / 2);
                    break;
                }
            }
            sim_tap(flex_kb_fkey_x(&g, FLEX_KB_FKEYS - 1) + flex_kb_fkey_w(&g, FLEX_KB_FKEYS - 1) / 2, flex_kb_func_y(&g) + g.kh / 2);
        }
        CHK(wait_idle());
        sim_run(200);
        CHK(!sim_disk_exists("/Documentos/big.bin") && sim_disk_exists("/Documentos/bigz.bin"));   // la extension se conserva
        CHK(row_of("bigz.bin") >= 0 && row_of("big.bin") < 0);
        flex_sys_back();
        sim_run(300);
        // al volver, Almacenamiento mide de nuevo: lo borrado (400 KB) ya no cuenta
        CHK(wait_scan());
        sz(docs - 409600, a, sizeof(a));
        sz(docs, b, sizeof(b));
        CHK(strcmp(a, b) != 0 && flex_app_storage_shown(a) && !flex_app_storage_shown(b));
    }

    // ---- 7) subir a Flex Cloud y la pestana de nube: no existe el servicio, y se dice ------------------------------------------------
    {
        int x, y;
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        tap_row("Documentos");
        CHK(wait_files());
        int i = row_of("bigz.bin");
        CHK(i >= 0 && flex_files_row_xy(i, &x, &y));
        sentinel();
        hold(x, y, 800);
        menu_item(x, y - 40, true, 4);   // Subir a Flex Cloud
        sim_run(200);
        CHK(notified("Flex Cloud") && sim_disk_exists("/Documentos/bigz.bin") && flex_files_layers() == 0);
        tap(350, 92);   // selector: Flex Cloud
        sim_run(200);
        CHK(flex_files_in_cloud() && flex_files_shown("Flex Cloud no disponible"));
        sim_shot("archivos_nube");
        tap(130, 92);   // "Este dispositivo"
        CHK(wait_files() && !flex_files_in_cloud() && !strcmp(flex_files_dir(), "/Documentos"));
        tap(350, 92);
        sim_run(200);
        flex_sys_back();   // Atras desde la nube vuelve a "Este dispositivo"
        sim_run(200);
        CHK(wait_files() && flex_files_active() && !flex_files_in_cloud());
        flex_sys_back();
        sim_run(300);
        CHK(!flex_files_active());
    }

    // ---- 8) seleccion multiple y papelera ---------------------------------------------------------------------------------------------
    {
        int x, y;
        put_n("/Paint/p1.fxp", 10);
        put_n("/Paint/p2.fxp", 20);
        put_n("/Paint/p3.fxp", 30);
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        tap_row("Paint");
        CHK(wait_files() && flex_files_count() == 4);
        int i = row_of("p1.fxp");
        CHK(i >= 0 && flex_files_row_xy(i, &x, &y));
        hold(x, y, 800);
        menu_item(x, y - 40, true, 0);   // Seleccionar
        sim_run(200);
        CHK(flex_files_multi() && flex_files_marked() == 1);
        sim_shot("archivos_seleccion");
        CHK(flex_files_row_is_sel(row_of("p1.fxp")) && !flex_files_row_is_sel(row_of("p2.fxp")));
        tap_row("p2.fxp");
        CHK(flex_files_marked() == 2 && flex_files_row_is_sel(row_of("p2.fxp")));
        tap_row("p2.fxp");   // desmarca
        CHK(flex_files_marked() == 1 && !flex_files_row_is_sel(row_of("p2.fxp")));
        tap_row("p3.fxp");
        CHK(flex_files_marked() == 2);
        tap(480 - 140 - 30, FLEX_FILES_BAR_Y + 30);   // "Papelera" de la barra
        CHK(wait_idle());
        sim_run(300);
        CHK(!flex_files_multi() && flex_files_marked() == 0);
        CHK(!sim_disk_exists("/Paint/p1.fxp") && !sim_disk_exists("/Paint/p3.fxp") && sim_disk_exists("/Paint/p2.fxp"));
        CHK(sim_disk_exists("/Papelera/@Paint@p1.fxp") && sim_disk_exists("/Papelera/@Paint@p3.fxp"));
        CHK(flex_files_count() == 2);
        // seleccion y "Salir": no se toca nada
        i = row_of("p2.fxp");
        CHK(i >= 0 && flex_files_row_xy(i, &x, &y));
        hold(x, y, 800);
        menu_item(x, y - 40, true, 0);
        sim_run(200);
        CHK(flex_files_multi());
        tap(480 - 40, FLEX_FILES_BAR_Y + 30);   // "Salir"
        sim_run(200);
        CHK(!flex_files_multi() && sim_disk_exists("/Paint/p2.fxp"));
        // la papelera del menu de los tres puntos
        tap(440, 45);   // los tres puntos (la franja de arriba, y < 30, es del sistema)
        sim_run(100);
        CHK(flex_files_layers() == 1);
        menu_item(440, FLEX_FILES_ZONE, false, 3);   // Papelera
        sim_run(300);
        CHK(flex_files_layers() >= 1);
        sim_shot("archivos_papelera");
        flex_sys_back();   // cierra la capa de la papelera
        sim_run(300);
        CHK(flex_files_active() && flex_files_layers() == 0);
        flex_sys_back();
        sim_run(300);
    }

    // ---- 9) carpeta vacia, toque en un fichero desconocido y en uno que no se reproduce -------------------------------------------------
    {
        int x, y;
        sim_disk_mkdir("/Documentos/Vacia");
        put_n("/Documentos/x.xyz", 5);
        put_n("/Documentos/v.mkv", 5);
        put_n("/Documentos/foto.jpg", 5);
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        tap_row("Documentos");
        CHK(wait_files());
        tap_row("Vacia");
        CHK(wait_files() && flex_files_count() == 0 && flex_files_shown("Carpeta vac\xC3\xAD" "a"));
        sim_shot("archivos_vacia");
        CHK(flex_files_up_xy(&x, &y));
        tap(x, y);
        CHK(wait_files());
        sentinel();
        tap_row("v.mkv");   // un formato reconocido que no se reproduce: se avisa con el motivo
        sim_run(200);
        CHK(notified("v.mkv") && flex_files_layers() == 0);
        sentinel();
        tap_row("foto.jpg");   // un medio: iria a su visor, que aun no esta migrado (Galeria / Multimedia): se dice
        sim_run(200);
        CHK(notified("foto.jpg") && flex_files_layers() == 0 && flex_files_active());
        tap_row("x.xyz");   // ni medio ni conocido: las acciones de siempre
        sim_run(200);
        CHK(flex_files_layers() == 1);
        flex_sys_back();   // cierra el menu
        sim_run(200);
        CHK(flex_files_layers() == 0);
        {
            int i = row_of("x.xyz");
            CHK(i >= 0 && flex_files_row_xy(i, &x, &y));
            hold(x, y, 800);
            menu_item(x, y - 40, true, 3);   // Papelera: un fichero suelto va a la papelera
            CHK(wait_idle());
            sim_run(200);
            CHK(!sim_disk_exists("/Documentos/x.xyz") && sim_disk_exists("/Papelera/@Documentos@x.xyz") && row_of("x.xyz") < 0);
        }
        flex_sys_back();
        sim_run(300);
    }

    // ---- 10) una carpeta enorme: 256 entradas, filas por ventana (no 256 x 8 objetos), scroll hasta el final ------------------------------
    {
        int x, y;
        sim_disk_mkdir("/Documentos/Mucho");
        for (int i = 0; i < FLEX_FSX_LIST_SCAN + 20; i++) {
            char p[96];
            snprintf(p, sizeof(p), "/Documentos/Mucho/f%04d.txt", i);
            put_n(p, 1);
        }
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        tap_row("Documentos");
        CHK(wait_files());
        tap_row("Mucho");
        for (int i = 0; i < 100 && flex_files_count() != FLEX_FSX_LIST_SCAN; i++) {
            sim_run(50);
        }
        CHK(flex_files_count() == FLEX_FSX_LIST_SCAN);
        CHK(flex_files_total_rows() == FLEX_FSX_LIST_SCAN + 1);   // ".." + 256
        CHK(flex_files_live_rows() > 0 && flex_files_live_rows() < 24);   // solo lo que se ve (mas el colchon)
        sim_shot("archivos_larga");
        for (int k = 0; k < 40; k++) {
            sim_drag(240, 700, 240, 160, 200);
            sim_run(60);
        }
        sim_run(300);
        CHK(flex_files_scroll() > 10000 && flex_files_live_rows() < 24);
        CHK(flex_files_shown(flex_files_name_at(flex_files_count() - 1)));   // el final de la lista esta a la vista
        sim_shot("archivos_larga_fin");
        flex_sys_back();
        sim_run(300);
    }

    // ---- 10b) una respuesta tardia de una carpeta que ya no se mira no se pinta; el explorador abierto sin particion lo dice ---------------------------
    {
        int x, y;
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        sim_explore_hold(true);
        tap_row("Documentos");   // la lectura queda en vuelo...
        sim_run(200);
        CHK(flex_files_count() == 0 && !flex_files_loaded());
        tap(350, 92);            // ...y se pasa a la nube antes de que llegue
        sim_run(200);
        CHK(flex_files_in_cloud());
        sim_explore_release();   // la respuesta de /Documentos llega ahora
        sim_run(300);
        CHK(flex_files_in_cloud() && flex_files_count() == 0 && !flex_files_loaded());
        sim_explore_hold(false);
        tap(130, 92);
        CHK(wait_files() && !flex_files_in_cloud() && flex_files_count() > 0);
        flex_sys_back();
        sim_run(300);
        // la particion deja de montar con la pantalla de Almacenamiento ya pintada: el explorador no finge una lista vacia
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        sim_fs_mounted(false);
        tap(x, y);
        sim_run(300);
        CHK(flex_files_active() && flex_files_nofs() && !flex_files_in_cloud());
        sim_shot("archivos_sin_fs");
        flex_sys_back();   // la flecha / el gesto salen
        sim_run(300);
        CHK(!flex_files_active());
        CHK(flex_app_storage_shown("Sin almacenamiento"));   // al volver, la pantalla de Almacenamiento lo vuelve a medir y lo dice
        sim_fs_mounted(true);
        flex_app_terminate_ex(IC_ALMACEN, true);
        sim_run(200);
        CHK(open_storage() && wait_scan() && flex_app_storage_zone_xy(3, &x, &y));
    }

    // ---- 11) cierre modal: Inicio y el bloqueo cierran el explorador; sin almacenamiento --------------------------------------------------
    {
        int x, y;
        CHK(flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        flex_sys_home();
        sim_run(500);
        CHK(!flex_files_active() && flex_shell_state() == FLEX_SH_HOME);
        CHK(open_storage() && flex_app_storage_zone_xy(3, &x, &y));
        tap(x, y);
        CHK(wait_files());
        flex_shell_lock();
        sim_run(300);
        CHK(!flex_files_active());
        sim_drag(240, 700, 240, 300, 300);
        sim_run(400);
        // sin la particion de datos: "Sin almacenamiento" y el explorador dice lo mismo
        sim_disk_enable(false);
        sim_fs_mounted(false);
        CHK(open_storage());
        sim_run(200);
        CHK(flex_app_storage_shown("Sin almacenamiento") && !flex_app_storage_shown("Memoria interna"));
        CHK(flex_app_storage_shown("Reinstala el firmware o repara el almacenamiento en Ajustes"));
        sim_shot("almacen_sin_fs");
        flex_app_terminate_ex(IC_ALMACEN, true);
        sim_run(200);
        sim_fs_mounted(true);
    }

    // ---- 11b) la medida falla: "--" y "No se pudo leer", sin ceros inventados ------------------------------------------------------------
    sim_disk_enable(false);   // el sistema de archivos "monta" pero la lectura de la tarea de almacenamiento falla
    CHK(open_storage() && wait_scan() && flex_app_storage_scan_failed());
    CHK(flex_app_storage_shown("--") && flex_app_storage_shown("No se pudo leer") && !flex_app_storage_shown("0 B"));
    sim_shot("almacen_medida_fallida");
    flex_app_terminate_ex(IC_ALMACEN, true);
    sim_run(200);

    // ---- 12) cerrar la app con una medida en vuelo no deja nada colgando ----------------------------------------------------------------
    seed();
    sim_explore_hold(true);
    CHK(open_storage() && !flex_app_storage_scanned());   // la medida queda en vuelo...
    flex_app_terminate_ex(IC_ALMACEN, true);               // ...y la app se cierra y se vuelve a abrir antes de que llegue
    sim_run(200);
    CHK(open_storage() && !flex_app_storage_scanned());
    sim_explore_release_n(1);   // llega la medida de la primera vez: no es de esta pantalla
    sim_run(300);
    CHK(!flex_app_storage_scanned() && flex_app_storage_shown("Calculando..."));
    sim_explore_release_n(1);   // la de esta pantalla si
    sim_run(300);
    CHK(flex_app_storage_scanned() && !flex_app_storage_shown("Calculando..."));
    sim_explore_hold(false);
    flex_app_terminate_ex(IC_ALMACEN, true);
    sim_run(300);
    CHK(open_storage() && wait_scan() && flex_app_storage_screen() == 0);
    flex_app_terminate_ex(IC_ALMACEN, true);
    sim_run(200);
    sim_disk_enable(false);
    printf("almacen: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
