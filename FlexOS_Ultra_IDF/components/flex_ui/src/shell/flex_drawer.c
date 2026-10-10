// Flex OS Ultra · Caja de aplicaciones (docs/spec/01a §11; Arduino:
// FlexOS_Ultra_AppDrawer.h:293-1492).
//
// Hoja que sube desde abajo (240 ms, ease-out cubica) con las esquinas de
// arriba redondeadas (r28). Su fondo es el del escritorio velado y FIJO en la
// pantalla (la hoja lo va descubriendo, como en Arduino); encima, la barra de
// estado, el buscador con su ojo de "ver ocultas", la rejilla de 4 columnas con
// scroll y, al abrir el buscador, su teclado compacto. Que apps salen y en que
// orden lo decide flex_drawer_model (probado contra drwFilter).
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_drawer_model.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_home_model.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"
#include "flex_wallpaper.h"

#define ANIM_MS     240
#define ICON_S      72
#define COL_X0      24
#define COL_STEP    120
#define ROW_STEP    116
#define GRID_TOP    152
#define NAV_H       76
#define SEARCH_Y    74
#define SEARCH_H    58
#define SEARCH_R    29
#define SHEET_RAD   28
#define KB_H        178
#define LP_MS       600
#define CTX_ROW_H   56
#define CTX_W       262
#define CTX_RAD     20
#define INFO_W      344
#define INFO_H      244
#define CELLS_MAX   (FLEX_APP_N + 24)

// Velos: blurBgVeil rgb(8,10,18) a70 y el extra de la caja rgb(6,8,16) a96
#define VEIL1_C lv_color_make(8, 10, 18)
#define VEIL1_A 70
#define VEIL2_C lv_color_make(6, 8, 16)
#define VEIL2_A 96
// Los dos juntos, para el vidrio que se apoya encima (1 - (1-a1)(1-a2) = 140)
#define VEIL_SUM_C lv_color_make(7, 9, 17)
#define VEIL_SUM_A 140

static const char *const KB_ROW[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};

static struct {
    lv_obj_t *sheet, *grid, *pill, *q_lbl, *eye, *kb, *ctx, *info;
    lv_image_dsc_t view;
    flex_drw_cell_t cells[CELLS_MAX];
    int n;
    char q[FLEX_DRW_QMAX + 1];
    int qn;
    bool show_hid, kb_on, open, closing;
    int pend_app;
    bool pend_recents;
    lv_area_t pend_from;
    int32_t slide;
    // pulsacion larga
    int lp_cell;
    uint32_t lp_t0;
    lv_point_t lp_p0;
    bool lp_done;
    int ctx_app;
} D = {.pend_app = -1, .lp_cell = -1, .ctx_app = -1};

static const char *app_name(int id)
{
    return flex_app_name(id);
}
static bool app_hidden(int id)
{
    return flex_app_is_hidden(id);
}
static const char *no_pkg_name(int i)
{
    (void)i;
    return "";
}
static bool no_pkg_hidden(int i)
{
    (void)i;
    return false;
}

static int32_t grid_bot(void)
{
    return D.kb_on ? 800 - KB_H - 8 : 800 - NAV_H;
}

// ---- fondo de la hoja ---------------------------------------------------------------
static void sheet_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius = SHEET_RAD;
    const lv_image_dsc_t *wall = flex_wallmgr_image(FLEX_WALL_HOME);
    if (!wall || c.y1 < 0 || c.y1 >= 800) {
        rd.bg_color = flex_th()->page;
        rd.bg_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &rd, &c);
        return;
    }
    // El fondo esta fijo en la pantalla: el pixel (x, y) es wall[y][x]. La vista
    // empieza en la fila y1 de la hoja; LVGL solo lee la parte visible (la hoja
    // se alarga SHEET_RAD px por debajo del borde para esconder sus esquinas).
    memset(&D.view, 0, sizeof(D.view));
    D.view.header.magic = LV_IMAGE_HEADER_MAGIC;
    D.view.header.cf = LV_COLOR_FORMAT_RGB565;
    D.view.header.w = (uint32_t)lv_area_get_width(&c);
    D.view.header.h = (uint32_t)lv_area_get_height(&c);
    D.view.header.stride = FLEX_WALL_W * 2;
    D.view.data = (const uint8_t *)((uintptr_t)wall->data + (uintptr_t)c.y1 * FLEX_WALL_W * 2);
    D.view.data_size = (uint32_t)(FLEX_WALL_W * 2 * lv_area_get_height(&c));
    lv_draw_image_dsc_t id;
    lv_draw_image_dsc_init(&id);
    id.src = &D.view;
    id.clip_radius = SHEET_RAD;
    id.image_area = c;
    lv_draw_image(layer, &id, &c);
    rd.bg_color = VEIL1_C;
    rd.bg_opa = VEIL1_A;
    lv_draw_rect(layer, &rd, &c);
    rd.bg_color = VEIL2_C;
    rd.bg_opa = VEIL2_A;
    lv_draw_rect(layer, &rd, &c);
}

static lv_obj_t *wall_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                            flex_surf_role_t role, lv_color_t flat, lv_opa_t flat_a)
{
    lv_obj_t *o = flex_box(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    flex_surface(o, role, FLEX_BD_HOME);
    flex_surface_set_veil(o, VEIL_SUM_C, VEIL_SUM_A);
    flex_surface_set_flat(o, flat, flat_a);
    return o;
}

// ---- contenido ----------------------------------------------------------------------
static void build_grid(void);
static void close_ctx(void);
static void close_info(void);
static void start_close(int app, const lv_area_t *from, bool recents);

static void refilter(void)
{
    flex_drw_src_t src = {FLEX_APP_N, app_name, app_hidden, 0, no_pkg_name, no_pkg_hidden};
    D.n = flex_drw_filter(&src, D.show_hid, D.q, D.qn, D.cells, CELLS_MAX);
}

static void search_refresh(void)
{
    if (!D.q_lbl) {
        return;
    }
    if (D.qn > 0) {
        lv_label_set_text(D.q_lbl, D.q);
        lv_obj_set_style_text_color(D.q_lbl, FLEX_ONWALL, 0);
    } else {
        lv_label_set_text(D.q_lbl, "Buscar aplicaciones");
        lv_obj_set_style_text_color(D.q_lbl, FLEX_ONWALL2, 0);
    }
    flex_glyph_set(D.eye, FLEX_GLYPH_EYE, D.show_hid ? 1 : 0);
    lv_obj_set_style_text_color(D.eye, D.show_hid ? flex_accent() : FLEX_ONWALL2, 0);
}

static void kb_key_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    if (code == -1) {   // cerrar teclado
        D.kb_on = false;
        lv_obj_set_hidden(D.kb, true);
        lv_obj_set_height(D.grid, grid_bot() - GRID_TOP);
        flex_navbar_set_ctx(FLEX_NAV_HOME);
        return;
    }
    if (code == -2) {   // borrar
        if (D.qn > 0) {
            D.q[--D.qn] = 0;
        }
    } else if (D.qn < FLEX_DRW_QMAX) {
        D.q[D.qn++] = (char)code;
        D.q[D.qn] = 0;
    }
    refilter();
    search_refresh();
    build_grid();   // cada cambio refiltra y vuelve arriba
}

static void build_kb(void)
{
    // Teclado compacto propio (minusculas): panel negro a120 desde y = 622
    D.kb = flex_box(D.sheet);
    lv_obj_set_pos(D.kb, 0, 800 - KB_H);
    lv_obj_set_size(D.kb, 480, KB_H);
    lv_obj_set_style_bg_opa(D.kb, 120, 0);
    lv_obj_set_style_bg_color(D.kb, lv_color_black(), 0);
    for (int r = 0; r < 3; r++) {
        int n = (int)strlen(KB_ROW[r]);
        int total = n * 42 + (n - 1) * 4;
        for (int c = 0; c < n; c++) {
            int32_t x = (480 - total) / 2 + c * 46 + (r == 2 ? 54 : 0);
            int32_t y = 14 + r * 52;
            lv_obj_t *k = flex_box(D.kb);
            lv_obj_set_pos(k, x, y);
            lv_obj_set_size(k, 42, 46);
            lv_obj_set_style_radius(k, 10, 0);
            lv_obj_set_style_bg_opa(k, 170, 0);
            lv_obj_set_style_bg_color(k, flex_th()->surf, 0);
            lv_obj_set_clickable(k, true);
            lv_obj_add_event_cb(k, kb_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)KB_ROW[r][c]);
            char t[2] = {KB_ROW[r][c], 0};
            lv_obj_t *l = flex_label(k, t, FLEX_FONT_S2, FLEX_ONWALL);
            flex_label_cap_center(l, 21, 23 - 8);
        }
    }
    for (int side = 0; side < 2; side++) {
        lv_obj_t *k = flex_box(D.kb);
        lv_obj_set_pos(k, side ? 480 - 12 - 50 : 12, 14 + 2 * 52);
        lv_obj_set_size(k, 50, 46);
        lv_obj_set_style_radius(k, 10, 0);
        lv_obj_set_style_bg_opa(k, 120, 0);
        lv_obj_set_style_bg_color(k, flex_th()->surf, 0);
        lv_obj_set_clickable(k, true);
        lv_obj_add_event_cb(k, kb_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(side ? -2 : -1));
        if (side) {
            lv_obj_t *dash = flex_box(k);   // borrar: un guion
            lv_obj_set_pos(dash, 14, 22);
            lv_obj_set_size(dash, 50 - 28, 2);
            lv_obj_set_style_bg_opa(dash, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dash, FLEX_ONWALL, 0);
            lv_obj_set_clickable(dash, false);
        } else {
            lv_obj_t *g = flex_glyph_create(k, FLEX_GLYPH_CHEVRON_DOWN, 50, 46);
            lv_obj_set_style_text_color(g, FLEX_ONWALL2, 0);
        }
    }
    lv_obj_set_hidden(D.kb, !D.kb_on);
}

static void pill_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    (void)e;
    if (p.x >= 24 + 432 - 54) {   // el ojo: ver / esconder las ocultas
        D.show_hid = !D.show_hid;
        refilter();
        search_refresh();
        build_grid();
        return;
    }
    D.kb_on = !D.kb_on;   // el resto del buscador abre/cierra el teclado
    lv_obj_set_hidden(D.kb, !D.kb_on);
    lv_obj_set_height(D.grid, grid_bot() - GRID_TOP);
    flex_navbar_set_ctx(D.kb_on ? FLEX_NAV_HIDDEN : FLEX_NAV_HOME);
}

// ---- celdas ---------------------------------------------------------------------------
static int cell_app(int i)
{
    return (i >= 0 && i < D.n && D.cells[i].kind == FLEX_DRW_NATIVE) ? D.cells[i].idx : -1;
}

static void open_ctx(int cell);

static void cell_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *o = lv_event_get_current_target_obj(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (D.closing || D.ctx || D.info) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        D.lp_cell = i;
        D.lp_t0 = lv_tick_get();
        D.lp_p0 = p;
        D.lp_done = false;
    } else if (code == LV_EVENT_PRESSING) {
        // pulsacion larga 600 ms, sin moverse 12 px y sin scroll -> menu
        if (!D.lp_done && D.lp_cell == i && lv_tick_elaps(D.lp_t0) > LP_MS && LV_ABS(p.x - D.lp_p0.x) < 12 &&
            LV_ABS(p.y - D.lp_p0.y) < 12 && !lv_obj_is_scrolling(D.grid)) {
            D.lp_done = true;
            lv_indev_wait_release(lv_indev_active());
            open_ctx(i);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED && !D.lp_done) {
        int app = cell_app(i);
        if (app >= 0) {
            lv_area_t a;
            lv_obj_get_coords(lv_obj_get_child(o, 0), &a);   // el icono: origen del zoom
            start_close(app, &a, false);
        }
    }
}

static void build_grid(void)
{
    lv_obj_clean(D.grid);
    lv_obj_scroll_to_y(D.grid, 0, LV_ANIM_OFF);
    const flex_palette_t *t = flex_th();
    (void)t;
    for (int i = 0; i < D.n; i++) {
        int app = cell_app(i);
        if (app < 0) {
            continue;
        }
        int r = i / 4, c = i % 4;
        // zona: 84 px de ancho desde el icono (icono + 12) y la fila entera
        lv_obj_t *cell = flex_box(D.grid);
        lv_obj_set_pos(cell, COL_X0 + c * COL_STEP, r * ROW_STEP);
        lv_obj_set_size(cell, ICON_S + 12, ROW_STEP);
        lv_obj_set_clickable(cell, true);
        lv_obj_set_scroll_chain(cell, true);
        lv_obj_add_event_cb(cell, cell_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cell, cell_cb, LV_EVENT_PRESSING, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cell, cell_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        // Iconos SIEMPRE planos en la caja (Arduino: gIconStyle = 0 en la rejilla)
        lv_obj_t *ic = flex_app_icon_create(cell, app, ICON_S, FLEX_BD_HOME);
        flex_surface_force_material(ic, 0);
        lv_obj_set_clickable(ic, false);
        bool dim = flex_app_is_hidden(app);
        if (dim) {
            lv_obj_t *v = flex_box(cell);   // velo negro a130 sobre la oculta
            lv_obj_set_size(v, ICON_S, ICON_S);
            lv_obj_set_style_radius(v, ICON_S * 22 / 100, 0);
            lv_obj_set_style_bg_opa(v, 130, 0);
            lv_obj_set_style_bg_color(v, lv_color_black(), 0);
            lv_obj_set_clickable(v, false);
        }
        if ((g_home.lock >> app) & 1u) {
            lv_obj_t *b = flex_box(cell);   // candado: cuadrito rojo
            lv_obj_set_pos(b, ICON_S - 18, ICON_S - 18);
            lv_obj_set_size(b, 16, 16);
            lv_obj_set_style_radius(b, 5, 0);
            lv_obj_set_style_bg_opa(b, 230, 0);
            lv_obj_set_style_bg_color(b, flex_th()->danger, 0);
            lv_obj_set_clickable(b, false);
        }
        // etiqueta: tamano 2 si cabe en 106 px, si no 1, y corte con "..."
        const char *nm = flex_app_name(app);
        const lv_font_t *f = FLEX_FONT_S2;
        lv_point_t sz;
        lv_text_get_size(&sz, nm, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > COL_STEP - 14) {
            f = FLEX_FONT_S1;
        }
        // en la rejilla (no en la celda, de 84 px: la recortaria)
        lv_obj_t *l = flex_label(D.grid, nm, f, dim ? FLEX_ONWALL2 : FLEX_ONWALL);
        flex_label_one_line(l, COL_STEP - 14);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_x(l, COL_X0 + c * COL_STEP + ICON_S / 2 - (COL_STEP - 14) / 2);
        flex_label_cap_y(l, r * ROW_STEP + ICON_S + 8);
        lv_obj_set_clickable(l, false);
    }
    if (D.n == 0) {
        lv_obj_t *l = flex_label(D.grid, "Sin resultados", FLEX_FONT_S3, FLEX_ONWALL2);
        flex_label_cap_center(l, 240, 60);
    }
}

// ---- menu contextual (pulsacion larga) ---------------------------------------------
enum { CTX_OPEN = 0, CTX_HOME, CTX_HIDE, CTX_INFO, CTX_ROWS };

// Hay sitio en Inicio: un hueco o una pagina por crear (drwFavToggle crea la
// pagina: "anadir a Inicio no puede fallar mientras queden paginas"). La misma
// condicion para el texto de la fila y para activarla.
static bool home_room(void)
{
    return flex_home_first_free() >= 0 || g_home.page_n < FLEX_HOME_PAGES_MAX;
}

static const char *ctx_label(int i)
{
    int id = D.ctx_app;
    switch (i) {
    case CTX_OPEN: return "Abrir";
    case CTX_HOME:
        if (flex_app_is_fav(id)) {
            return "Quitar de inicio";
        }
        return home_room() ? "A\xC3\xB1" "adir a inicio" : "Inicio completo";
    case CTX_HIDE: return flex_app_is_hidden(id) ? "Mostrar" : "Ocultar";
    default: return "Informaci\xC3\xB3n";
    }
}

static bool ctx_enabled(int i)
{
    int id = D.ctx_app;
    if (i == CTX_HOME) {
        // "Inicio completo": sin hueco Y sin paginas por crear
        return flex_app_is_fav(id) || (home_room() && !flex_app_is_hidden(id));
    }
    if (i == CTX_HIDE) {
        return flex_app_can_hide(id);   // Ajustes no se puede ocultar (fila atenuada)
    }
    return true;
}

static void info_open(int id);

static void ctx_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int id = D.ctx_app;
    if (!ctx_enabled(i) || id < 0) {
        return;   // fila inactiva: nada
    }
    if (i == CTX_OPEN) {
        close_ctx();
        start_close(id, NULL, false);
        return;
    }
    if (i == CTX_INFO) {
        close_ctx();
        info_open(id);
        return;
    }
    if (i == CTX_HOME) {
        flex_home_fav_toggle(id);
    } else {
        flex_home_hide_toggle(id);
    }
    flex_home_save();
    flex_home_rebuild();
    close_ctx();
    refilter();
    build_grid();
}

static void ctx_scrim_cb(lv_event_t *e)
{
    (void)e;
    close_ctx();   // toque fuera o deslizamiento: cierra el menu (no la caja)
}

static void close_ctx(void)
{
    if (D.ctx) {
        lv_obj_delete_async(D.ctx);
        D.ctx = NULL;
    }
    D.ctx_app = -1;
}

static void open_ctx(int cell)
{
    int id = cell_app(cell);
    if (id < 0) {
        return;
    }
    D.ctx_app = id;
    int r = cell / 4, c = cell % 4;
    int32_t cx = COL_X0 + c * COL_STEP;
    int32_t iy = GRID_TOP + r * ROW_STEP - lv_obj_get_scroll_y(D.grid);
    int32_t h = CTX_ROWS * CTX_ROW_H;
    int32_t px = cx + ICON_S + 12;
    if (px + CTX_W > 480 - 8) {
        px = cx - 12 - CTX_W;   // no cabe a la derecha
    }
    px = px < 8 ? 8 : px > 480 - 8 - CTX_W ? 480 - 8 - CTX_W : px;
    int32_t py = iy < GRID_TOP ? GRID_TOP : iy > 800 - 8 - h ? 800 - 8 - h : iy;

    D.ctx = flex_box(D.sheet);   // capa que recoge el toque de fuera
    lv_obj_set_size(D.ctx, 480, 800 + SHEET_RAD);
    lv_obj_set_clickable(D.ctx, true);
    lv_obj_add_event_cb(D.ctx, ctx_scrim_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_gesture_bubble(D.ctx, false);
    lv_obj_add_event_cb(D.ctx, ctx_scrim_cb, LV_EVENT_GESTURE, NULL);
    const flex_palette_t *t = flex_th();
    lv_obj_t *m = wall_panel(D.ctx, px, py, CTX_W, h, CTX_RAD, FLEX_SURF_CARD, t->surf, 242);
    lv_obj_set_clickable(m, true);   // un toque en el panel fuera de las filas no cierra
    for (int i = 0; i < CTX_ROWS; i++) {
        lv_obj_t *row = flex_box(m);
        lv_obj_set_pos(row, 0, i * CTX_ROW_H);
        lv_obj_set_size(row, CTX_W, CTX_ROW_H);
        lv_obj_set_clickable(row, true);
        lv_obj_add_event_cb(row, ctx_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (i > 0) {
            lv_obj_t *sep = flex_box(row);
            lv_obj_set_pos(sep, 14, 0);
            lv_obj_set_size(sep, CTX_W - 28, 1);
            lv_obj_set_style_bg_opa(sep, 80, 0);
            lv_obj_set_style_bg_color(sep, t->txt2, 0);
            lv_obj_set_clickable(sep, false);
        }
        lv_color_t col = ctx_enabled(i) ? t->txt : t->txt2;
        lv_obj_t *l = flex_label(row, ctx_label(i), FLEX_FONT_S3, col);
        flex_label_one_line(l, CTX_W - 76);
        lv_obj_set_x(l, 18);
        lv_obj_set_y(l, CTX_ROW_H / 2 - lv_font_get_line_height(FLEX_FONT_S3) / 2);
        static const flex_glyph_t gl[CTX_ROWS] = {FLEX_GLYPH_OPEN, FLEX_GLYPH_RING, FLEX_GLYPH_EYE, FLEX_GLYPH_INFO};
        lv_obj_t *g = flex_glyph_create(row, gl[i], 26, 26);
        lv_obj_set_pos(g, CTX_W - 44, CTX_ROW_H / 2 - 13);
        lv_obj_set_style_text_color(g, col, 0);
        if (i == CTX_HIDE) {
            flex_glyph_set(g, FLEX_GLYPH_EYE, flex_app_is_hidden(D.ctx_app) ? 0 : 1);
        }
    }
}

// ---- ficha de informacion ------------------------------------------------------------
static void info_close_cb(lv_event_t *e)
{
    (void)e;
    close_info();
}

static void close_info(void)
{
    if (D.info) {
        lv_obj_delete_async(D.info);
        D.info = NULL;
    }
}

static void info_open(int id)
{
    if (id < 0 || id >= FLEX_APP_N) {
        return;
    }
    const flex_palette_t *t = flex_th();
    D.info = flex_box(D.sheet);   // cualquier toque o deslizamiento la cierra
    lv_obj_set_size(D.info, 480, 800 + SHEET_RAD);
    lv_obj_set_clickable(D.info, true);
    lv_obj_add_event_cb(D.info, info_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_gesture_bubble(D.info, false);
    lv_obj_add_event_cb(D.info, info_close_cb, LV_EVENT_GESTURE, NULL);
    int32_t x = (480 - INFO_W) / 2, y = (800 - INFO_H) / 2;
    lv_obj_t *card = wall_panel(D.info, x, y, INFO_W, INFO_H, 24, FLEX_SURF_CARD, t->surf, 245);
    lv_obj_set_clickable(card, false);
    lv_obj_t *ic = flex_app_icon_create(card, id, 56, FLEX_BD_HOME);
    flex_surface_force_material(ic, 0);
    lv_obj_set_pos(ic, 20, 20);
    lv_obj_set_clickable(ic, false);
    lv_obj_t *nm = flex_label(card, flex_app_name(id), FLEX_FONT_S3, t->txt);
    flex_label_one_line(nm, INFO_W - 108);
    lv_obj_set_x(nm, 88);
    flex_label_cap_y(nm, 28);
    const flex_app_def_t *d = flex_app_def(id);
    lv_obj_t *cat = flex_label(card, flex_app_cat_name(d ? d->cat : FLEX_CAT_SYSTEM), FLEX_FONT_S2, t->txt2);
    lv_obj_set_x(cat, 88);
    flex_label_cap_y(cat, 54);
    char ln[48];
    const char *si = "s\xC3\xAD";
    for (int k = 0; k < 4; k++) {
        if (k == 0) {
            snprintf(ln, sizeof(ln), "Id de registro: %d", id);
        } else if (k == 1) {
            snprintf(ln, sizeof(ln), "En inicio: %s", flex_app_is_fav(id) ? si : "no");
        } else if (k == 2) {
            snprintf(ln, sizeof(ln), "Visible: %s", flex_app_is_hidden(id) ? "no" : si);
        } else {
            snprintf(ln, sizeof(ln), "Bloqueada: %s", ((g_home.lock >> id) & 1u) ? si : "no");
        }
        lv_obj_t *l = flex_label(card, ln, FLEX_FONT_S2, t->txt2);
        lv_obj_set_x(l, 20);
        flex_label_cap_y(l, 96 + k * 26);
    }
    lv_obj_t *hint = flex_label(card, "Toca para cerrar", FLEX_FONT_S1, t->txt2);
    flex_label_cap_center(hint, INFO_W / 2, INFO_H - 30);
}

// ---- abrir / cerrar --------------------------------------------------------------------
static int32_t ease_out_cubic(const lv_anim_t *a)
{
    float p = a->duration ? (float)(a->act_time < 0 ? 0 : a->act_time) / (float)a->duration : 1.0f;
    p = p > 1.0f ? 1.0f : p;
    float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
    return a->start_value + (int32_t)((float)(a->end_value - a->start_value) * e);
}

static void slide_exec(void *obj, int32_t v)
{
    D.slide = v;
    lv_obj_set_y(obj, v);
}

static void destroy(void)
{
    if (D.sheet) {
        lv_anim_delete(D.sheet, slide_exec);
        lv_obj_delete(D.sheet);
    }
    D.sheet = D.grid = D.pill = D.q_lbl = D.eye = D.kb = D.ctx = D.info = NULL;
    D.open = D.closing = false;
}

static void close_done(lv_anim_t *a)
{
    (void)a;
    int app = D.pend_app;
    bool rec = D.pend_recents;
    lv_area_t from = D.pend_from;
    D.pend_app = -1;
    D.pend_recents = false;
    destroy();
    flex_shell_overlay_end();   // estado Inicio; la accion pendiente, ahora que la hoja salio
    flex_touch_drop_all();
    if (app >= 0) {
        flex_app_launch(app, from.x2 > from.x1 ? &from : NULL);
    } else if (rec) {
        flex_recents_open();
    }
}

static void start_close(int app, const lv_area_t *from, bool recents)
{
    if (!D.open || D.closing) {
        return;
    }
    D.closing = true;
    D.pend_app = app;
    D.pend_recents = recents;
    D.pend_from = from ? *from : (lv_area_t){0, 0, -1, -1};
    close_ctx();
    close_info();
    if (D.kb_on) {
        D.kb_on = false;
        lv_obj_set_hidden(D.kb, true);
        flex_navbar_set_ctx(FLEX_NAV_HOME);
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, D.sheet);
    lv_anim_set_exec_cb(&a, slide_exec);
    lv_anim_set_values(&a, D.slide, 800);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, ease_out_cubic);
    lv_anim_set_completed_cb(&a, close_done);
    lv_anim_start(&a);
}

static void ov_back(void)
{
    if (D.info) {
        close_info();
    } else if (D.ctx) {
        close_ctx();
    } else {
        start_close(-1, NULL, false);
    }
}

static void ov_home(void)
{
    start_close(-1, NULL, false);
}

static void ov_recents(void)
{
    start_close(-1, NULL, true);   // cierra y abre Recientes al terminar
}

static const flex_overlay_ops_t s_ops = {ov_back, ov_home, ov_recents};

static void sheet_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *in = lv_indev_active();
    if (!in || D.closing || D.ctx || D.info) {
        return;
    }
    // deslizar hacia abajo con la lista arriba del todo (si no, desplaza la lista)
    if (lv_indev_get_gesture_dir(in) == LV_DIR_BOTTOM && lv_obj_get_scroll_y(D.grid) <= 0) {
        lv_indev_wait_release(in);
        start_close(-1, NULL, false);
    }
}

void flex_drawer_open(void)
{
    if (D.open || flex_shell_state() != FLEX_SH_HOME) {
        return;
    }
    D.qn = 0;
    D.q[0] = 0;
    D.show_hid = false;
    D.kb_on = false;
    D.pend_app = -1;
    D.pend_recents = false;
    refilter();

    D.sheet = flex_box(flex_shell_screen());
    lv_obj_set_size(D.sheet, 480, 800 + SHEET_RAD);
    lv_obj_set_clickable(D.sheet, true);
    lv_obj_add_event_cb(D.sheet, sheet_draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_set_gesture_bubble(D.sheet, false);   // el gesto se queda en la hoja
    lv_obj_add_event_cb(D.sheet, sheet_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_move_foreground(D.sheet);
    flex_statusbar_create(D.sheet, FLEX_SB_WALL);

    const flex_palette_t *t = flex_th();
    D.pill = wall_panel(D.sheet, 24, SEARCH_Y, 480 - 48, SEARCH_H, SEARCH_R, FLEX_SURF_ELEVATED, t->surf, 150);
    lv_obj_set_clickable(D.pill, true);
    lv_obj_add_event_cb(D.pill, pill_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lupa = flex_glyph_create(D.pill, FLEX_GLYPH_SEARCH, 28, 28);
    lv_obj_set_pos(lupa, 30 - 14, SEARCH_H / 2 - 14);
    lv_obj_set_style_text_color(lupa, FLEX_ONWALL, 0);
    D.q_lbl = flex_label(D.pill, "", FLEX_FONT_S2, FLEX_ONWALL2);
    lv_obj_set_x(D.q_lbl, 54);
    flex_label_cap_y(D.q_lbl, SEARCH_H / 2 - 8);
    D.eye = flex_glyph_create(D.pill, FLEX_GLYPH_EYE, 22, 22);
    lv_obj_set_pos(D.eye, 432 - 32 - 11, SEARCH_H / 2 - 11);

    D.grid = flex_box(D.sheet);
    lv_obj_set_pos(D.grid, 0, GRID_TOP);
    lv_obj_set_size(D.grid, 480, grid_bot() - GRID_TOP);
    lv_obj_set_clickable(D.grid, false);
    lv_obj_set_scrollable(D.grid, true);
    lv_obj_set_scroll_dir(D.grid, LV_DIR_VER);
    lv_obj_set_scroll_elastic(D.grid, false);   // sin rebote, acotado
    lv_obj_set_scroll_momentum(D.grid, true);
    lv_obj_set_scrollbar_mode(D.grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(D.grid, 16, 0);
    build_kb();
    search_refresh();
    build_grid();

    D.open = true;
    D.closing = false;
    D.slide = 800;
    lv_obj_set_y(D.sheet, 800);
    flex_shell_overlay_begin(&s_ops);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, D.sheet);
    lv_anim_set_exec_cb(&a, slide_exec);
    lv_anim_set_values(&a, 800, 0);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, ease_out_cubic);
    lv_anim_start(&a);
}

void flex_drawer_close_now(void)
{
    if (D.open) {
        destroy();
        flex_shell_overlay_end();
    }
}

bool flex_drawer_is_open(void)
{
    return D.open;
}
