// Flex OS Ultra · App Paint (docs/spec/03 §7; Arduino: FlexOS_Ultra_AppPaint.h + FlexOS_FS.cpp:686-839).
//
// Cada dibujo es un fichero REAL .fxp en /Paint: la lista de trazos serializada (flex_paint_model.c). La galeria (tarjetas en 2
// columnas con la MINIATURA real: los mismos trazos reproducidos a escala, boton flotante, seleccion multiple, menu, papelera) y el lienzo
// (el dibujo con su proporcion, trazo en vivo con bordes suaves, colores, grosor, deshacer y limpiar) comparten el rasterizador de
// flex_raster.c, que es el de Arduino pixel a pixel. Toda la logica sin graficos esta en flex_paint_model.c y flex_raster.c y se prueba
// en el PC frente a Arduino; aqui solo hay LVGL y el pegamento.
//
// La UI nunca toca la flash: el dibujo vive en memoria mientras el lienzo esta abierto y, tras cada trazo, la tarea de almacenamiento
// lo escribe entero y de forma atomica (flex_fs_write_async). Si hay una escritura en curso, la siguiente espera y se hace con el dibujo
// ya actualizado: nunca hay mas de una en vuelo. Las respuestas tardias (una lista, una miniatura o un dibujo que ya no se espera)
// se descartan por su generacion.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "flex_app.h"
#include "flex_filekit.h"
#include "flex_frame.h"
#include "flex_fs_names.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_paint_model.h"
#include "flex_raster.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "widgets/flex_app_ids.h"

#define PAINT_DIR "/Paint"
#define PAINT_EXT ".fxp"
#define FLEX_PG_VIEW_Y 60   // el viewport con scroll de la galeria empieza aqui (UIHDR_ZONE + 4)
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

typedef struct {
    lv_obj_t *root;
    bool created;
    int view;                          // 0 galeria, 1 lienzo
    uint32_t gen;                      // generacion: una respuesta con otra generacion se descarta
    uint32_t wgen;                     // generacion de las escrituras (cambia al cerrar la app)
    // ---- galeria
    lv_obj_t *groot, *gview, *gcontent, *fabwrap, *bar;
    lv_obj_t *card[FLEX_PG_MAX];
    flex_fs_ent_t ents[FLEX_PG_MAX];
    int n;
    bool overflow, overflow_told, list_ready;
    int sel_idx;
    bool multi;
    uint32_t mask;
    int scroll;                        // el de la sesion o el ultimo conocido
    bool long_fired;
    int trash_i;
    bool opening, creating;            // abriendo un dibujo / creando uno: no se aceptan otros toques
    lv_draw_buf_t *thumb[FLEX_PG_MAX];
    bool thumb_done[FLEX_PG_MAX];
    // ---- lienzo
    lv_obj_t *croot, *cbox, *cimg, *ctools, *csub;
    lv_draw_buf_t *cbuf;
    flex_raster_t ras;
    lv_area_t dirty;                   // lo pintado desde el ultimo aviso a LVGL (coordenadas de pantalla)
    bool dirty_valid;
    char path[96];
    bool wr_busy, wr_again, need_write, leave_pending;
    bool suspended;                    // suspendida: LVGL sigue entregando el dedo apoyado al lienzo oculto y NO debe dibujar (Arduino no ticaba la app)
    // ---- herramientas (viven fuera del dibujo: sobreviven a cerrarlo y a la sesion)
    uint16_t color;
    int size_ix;
    // ---- sesion pendiente de aplicar al abrir
    flex_paint_sess_t sess;
    bool sess_valid;
} paint_t;

static paint_t P;
static flex_pcv_t CV;   // el dibujo abierto: 2 KB de trazo + el .fxp en memoria

static const flex_palette_t *TH(void)
{
    return flex_th();
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void fill(lv_obj_t *o, lv_color_t c, lv_opa_t a)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
}

static void tap_point(lv_obj_t *o, int *x, int *y)
{
    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(o, &a);
    *x = (int)p.x - (int)a.x1;
    *y = (int)p.y - (int)a.y1;
}

static int nav_reserve(void)
{
    return flex_navbar_ctx() != FLEX_NAV_HIDDEN ? FLEX_NAV_H : 0;
}

static void mark_dirty(void)
{
    flex_session_mark_dirty(IC_PAINT);
}

static void gallery_show(void);
static void list_reload(void);
static void list_render(void);

// ===== miniaturas =============================================================================================================
static void thumbs_free(void)
{
    for (int i = 0; i < FLEX_PG_MAX; i++) {
        if (P.thumb[i]) {
            lv_draw_buf_destroy(P.thumb[i]);
            P.thumb[i] = NULL;
        }
        P.thumb_done[i] = false;
    }
}

// El mismo callback sirve para el lienzo y para las miniaturas (paintSegCb): un punto es un circulo, lo demas un segmento grueso.
static void seg_to_raster(int x0, int y0, int x1, int y1, uint16_t color, int radius, void *user)
{
    const flex_raster_t *r = user;
    if (x0 == x1 && y0 == y1) {
        flex_raster_fill_circle_aa(r, (float)x0, (float)y0, (float)radius, color);
    } else {
        flex_raster_stroke_seg_aa(r, (float)x0, (float)y0, (float)x1, (float)y1, (float)radius, color);
    }
}

static void thumb_render(int i, const uint8_t *data, size_t len)
{
    uint16_t dw = 0, dh = 0, strokes = 0;
    if (!flex_fxp_header(data, len, &dw, &dh, &strokes) || dw == 0 || dh == 0) {
        dw = FLEX_PAINT_CW;   // sin cabecera legible la miniatura queda en blanco, con el tamano por defecto
        dh = (uint16_t)flex_paint_ch(nav_reserve());
        strokes = 0;
    }
    int cx, cy, cw, ch;
    flex_pg_card_rect(i, 0, nav_reserve(), &cx, &cy, &cw, &ch);
    int iw = cw - 8, ih = ch - 8;
    lv_draw_buf_t *b = lv_draw_buf_create((uint32_t)iw, (uint32_t)ih, LV_COLOR_FORMAT_RGB565, 0);
    if (!b) {
        return;   // sin memoria: la tarjeta se queda en blanco
    }
    flex_raster_t r;
    flex_raster_init(&r, (uint16_t *)b->data, iw, ih, (int)(b->header.stride / 2), 4, 4);   // coordenadas de la tarjeta
    flex_raster_fill(&r, RGB565(255, 255, 255));
    if (strokes > 0) {
        float sc;
        int ox, oy;
        flex_pg_thumb(0, 0, cw, ch, dw, dh, &sc, &ox, &oy);
        flex_fxp_replay(data, len, sc, ox, oy, seg_to_raster, &r);
    }
    P.thumb[i] = b;
}

static void card_attach_thumb(int i)
{
    if (i < 0 || i >= P.n || !P.card[i] || !P.thumb[i]) {
        return;
    }
    lv_obj_t *img = lv_image_create(P.card[i]);
    lv_obj_set_pos(img, 4, 4);
    lv_image_set_src(img, P.thumb[i]);
    lv_obj_set_clickable(img, false);
}

static void thumb_next(int i);

static void thumb_cb(esp_err_t err, uint8_t *data, size_t len, void *user)
{
    uint32_t tok = (uint32_t)(uintptr_t)user;
    int i = (int)(tok & 0xFFFF);
    if ((tok >> 16) != (P.gen & 0xFFFF) || !P.created || P.view != 0 || i >= P.n) {
        free(data);
        return;   // otra lista o la vista ya no es esta
    }
    if (err == ESP_OK && data) {
        thumb_render(i, data, len);
    }
    free(data);
    P.thumb_done[i] = true;
    card_attach_thumb(i);
    thumb_next(i + 1);
}

static void path_of(int i, char *out, size_t n)
{
    snprintf(out, n, PAINT_DIR "/%.47s", P.ents[i].name);
}

static void thumb_next(int i)
{
    if (i >= P.n) {
        return;
    }
    char p[FLEX_FS_PATH_MAX];
    path_of(i, p, sizeof(p));
    // de una en una: la cola del almacenamiento es corta y 24 lecturas seguidas la desbordarian
    if (flex_fs_read_async(p, FLEX_FXP_MAX_BYTES, thumb_cb, (void *)(uintptr_t)(((P.gen & 0xFFFF) << 16) | (uint32_t)i)) != ESP_OK) {
        P.thumb_done[i] = true;
        thumb_next(i + 1);
    }
}

// ===== galeria ================================================================================================================
static int ent_cmp(const void *a, const void *b)
{
    return strcasecmp(((const flex_fs_ent_t *)a)->name, ((const flex_fs_ent_t *)b)->name);
}

static bool is_drawing(const flex_fs_ent_t *e)
{
    size_t l = strlen(e->name);
    return !e->dir && l > 4 && strcasecmp(e->name + l - 4, PAINT_EXT) == 0;   // ni carpetas ni restos (".tmp~") se muestran
}

static void list_cb(esp_err_t err, const flex_fs_ent_t *ents, size_t n, void *user)
{
    if ((uint32_t)(uintptr_t)user != P.gen || !P.created || P.view != 0) {
        return;
    }
    P.n = 0;
    P.overflow = false;
    if (err == ESP_OK) {
        // todos los dibujos, ordenados ANTES de recortar (Arduino recortaba a 16 antes de ordenar y perdia dibujos)
        static flex_fs_ent_t all[256];
        size_t k = 0;
        for (size_t i = 0; i < n && k < 256; i++) {
            if (is_drawing(&ents[i])) {
                all[k++] = ents[i];
            }
        }
        qsort(all, k, sizeof(all[0]), ent_cmp);
        P.overflow = k > FLEX_PG_MAX;
        P.n = (int)(k > FLEX_PG_MAX ? FLEX_PG_MAX : k);
        memcpy(P.ents, all, (size_t)P.n * sizeof(P.ents[0]));
    }
    if (P.n < 32 && (P.mask >> P.n)) {
        P.mask &= (1u << P.n) - 1u;   // el modo seleccion no deja marcas fuera de la lista
    }
    if (P.sel_idx >= P.n) {
        P.sel_idx = -1;
    }
    int maxs = flex_pg_max_scroll(P.n, nav_reserve());
    if (P.scroll > maxs) {
        P.scroll = maxs;
    }
    P.list_ready = true;
    thumbs_free();
    list_render();
    if (P.overflow && !P.overflow_told) {
        P.overflow_told = true;
        flex_kit_toast(P.root, "Solo se muestran los primeros 24 dibujos");
    }
    thumb_next(0);
}

static void list_reload(void)
{
    P.gen++;
    if (flex_fs_list_async(PAINT_DIR, 256, list_cb, (void *)(uintptr_t)P.gen) != ESP_OK) {
        list_cb(ESP_FAIL, NULL, 0, (void *)(uintptr_t)P.gen);
    }
}

static void done_reload_cb(esp_err_t err, void *user)
{
    (void)user;
    (void)err;   // si algo fallo el elemento sigue en la lista: se relee del disco, que es la verdad
    if (P.created && P.view == 0) {
        list_reload();
    }
}

// ---- abrir y crear ---------------------------------------------------------------------------------------------------------
static void canvas_show(const char *path, bool restored);

static void open_cb(esp_err_t err, uint8_t *data, size_t len, void *user)
{
    uint32_t tok = (uint32_t)(uintptr_t)user;
    bool restored = tok & 1u;
    P.opening = false;
    if ((tok >> 1) != (P.gen & 0x7FFFFFFFu) || !P.created || P.view != 0) {
        free(data);
        if (P.created) {
            P.path[0] = '\0';
        }
        return;
    }
    char path[96];
    snprintf(path, sizeof(path), "%.95s", P.path);
    P.path[0] = '\0';
    bool ok = err == ESP_OK && data && flex_pcv_load(&CV, data, len);
    free(data);
    if (!ok) {
        if (!restored) {
            flex_kit_toast(P.root, err == ESP_ERR_INVALID_SIZE ? "El dibujo es demasiado grande"
                                                               : (err == ESP_OK ? "No es un dibujo de Paint" : "No se pudo abrir el dibujo"));
        } else {
            P.color = flex_paint_pal[0];   // el fichero ya no esta: la galeria, como si se entrara de nuevo
        }
        if (restored) {
            gallery_show();
        }
        return;
    }
    snprintf(P.path, sizeof(P.path), "%.95s", path);
    canvas_show(path, restored);
}

static void canvas_open(const char *path, bool restored)
{
    if (P.opening) {
        return;
    }
    P.opening = true;
    snprintf(P.path, sizeof(P.path), "%.95s", path);   // en curso: open_cb la recoge
    if (flex_fs_read_async(path, FLEX_FXP_MAX_BYTES, open_cb, (void *)(uintptr_t)(((P.gen & 0x7FFFFFFFu) << 1) | (restored ? 1u : 0u))) != ESP_OK) {
        P.opening = false;
        P.path[0] = '\0';
        if (restored) {
            gallery_show();
        } else {
            flex_kit_toast(P.root, "No se pudo abrir el dibujo");
        }
    }
}

static void write_kick(void);

static void new_cb(esp_err_t err, const char *path, void *user)
{
    (void)user;
    P.creating = false;
    if (!P.created || P.view != 0) {
        return;
    }
    if (err != ESP_OK || !path[0]) {
        flex_kit_toast(P.root, "No se pudo crear el dibujo");   // disco lleno: no aparece nada, porque no existe
        return;
    }
    flex_pcv_init(&CV, nav_reserve(), P.color, P.size_ix);
    if (!flex_pcv_new(&CV)) {
        flex_fs_remove_async(path, NULL, NULL);
        flex_kit_toast(P.root, "Sin memoria para el lienzo");
        return;
    }
    snprintf(P.path, sizeof(P.path), "%.95s", path);
    P.need_write = true;   // la cabecera con 0 trazos: el fichero nuevo no es un dibujo hasta que se escribe
    canvas_show(path, false);
    write_kick();
}

static void paint_new(void)
{
    if (P.creating || P.opening) {
        return;
    }
    P.creating = true;
    if (flex_fs_new_file_async(PAINT_DIR, "Dibujo", PAINT_EXT, new_cb, NULL) != ESP_OK) {
        P.creating = false;
        flex_kit_toast(P.root, "No se pudo crear el dibujo");
    }
}

// ---- acciones --------------------------------------------------------------------------------------------------------------
static void trash_next_cb(esp_err_t err, void *user)
{
    (void)err;
    (void)user;
    if (!P.created || P.view != 0) {
        return;
    }
    while (P.trash_i < P.n && !(P.mask & (1u << P.trash_i))) {
        P.trash_i++;
    }
    if (P.trash_i >= P.n) {
        P.multi = false;
        P.mask = 0;
        list_reload();
        return;
    }
    char p[FLEX_FS_PATH_MAX];
    path_of(P.trash_i++, p, sizeof(p));
    if (flex_fs_trash_async(p, trash_next_cb, NULL) != ESP_OK) {
        trash_next_cb(ESP_FAIL, NULL);
    }
}

static void ask_del_cb(bool ok, void *user)
{
    (void)user;
    if (!P.created || P.view != 0) {
        return;
    }
    if (ok && P.sel_idx >= 0 && P.sel_idx < P.n) {
        char p[FLEX_FS_PATH_MAX];
        path_of(P.sel_idx, p, sizeof(p));
        P.sel_idx = -1;
        if (flex_fs_delete_async(p, done_reload_cb, NULL) == ESP_OK) {   // borrado DEFINITIVO real
            return;
        }
    }
    P.sel_idx = -1;
    list_render();
}

static void rename_done_cb(esp_err_t err, void *user)
{
    (void)user;
    if (!P.created || P.view != 0) {
        return;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        flex_kit_toast(P.root, "Ya hay un dibujo con ese nombre");   // Arduino no decia nada
    } else if (err == ESP_ERR_INVALID_ARG) {
        flex_kit_toast(P.root, "Nombre demasiado largo");
    } else if (err != ESP_OK) {
        flex_kit_toast(P.root, "No se pudo renombrar");
    }
    list_reload();
}

static void name_cb(const char *name, void *user)
{
    (void)user;
    if (!P.created || P.view != 0) {
        return;
    }
    if (name && P.sel_idx >= 0 && P.sel_idx < P.n) {
        char p[FLEX_FS_PATH_MAX];
        path_of(P.sel_idx, p, sizeof(p));
        P.sel_idx = -1;
        if (flex_fs_rename_name_async(p, name, rename_done_cb, NULL) == ESP_OK) {
            return;
        }
    }
    P.sel_idx = -1;
    list_render();
}

static void trash_exit_cb(void *user)
{
    (void)user;
    if (P.created && P.view == 0) {
        list_reload();   // la papelera pudo restaurar dibujos
    }
}

static void menu_cb(int act, void *user)
{
    (void)user;
    if (!P.created || P.view != 0) {
        return;
    }
    bool have = P.sel_idx >= 0 && P.sel_idx < P.n;
    char stem[FLEX_FS_NAME_MAX] = "";
    if (have) {
        flex_fsn_stem(P.ents[P.sel_idx].name, stem, sizeof(stem));
    }
    switch (act) {
    case FLEX_FK_ACT_SEL:
        P.multi = true;
        P.mask = have ? (1u << P.sel_idx) : 0;
        break;
    case FLEX_FK_ACT_DEL:
        if (have) {
            flex_fk_ask_open(P.root, "\xC2\xBF" "Borrar definitivamente?", stem, ask_del_cb, NULL);
            return;   // el borrado ocurre al confirmar
        }
        break;
    case FLEX_FK_ACT_REN:
        if (have) {
            flex_fk_name_open(P.root, "Renombrar dibujo", stem, NULL, name_cb, NULL);
            return;
        }
        break;
    case FLEX_FK_ACT_TRASH:
        if (have) {
            char p[FLEX_FS_PATH_MAX];
            path_of(P.sel_idx, p, sizeof(p));
            P.sel_idx = -1;
            if (flex_fs_trash_async(p, done_reload_cb, NULL) == ESP_OK) {   // a /Papelera de verdad
                return;
            }
        } else {
            flex_fk_trash_open(P.root, trash_exit_cb, NULL);   // sin seleccion (tres puntos): abre la papelera
            return;
        }
        break;
    default:
        break;
    }
    list_render();
}

static void header_menu_cb(lv_event_t *e)
{
    (void)e;
    if (flex_fk_any(P.root)) {
        return;
    }
    P.sel_idx = -1;
    flex_fk_menu_open(P.root, 440, 56, false, menu_cb, NULL);   // desde los tres puntos, sin seleccion
}

static void card_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (P.long_fired) {
        P.long_fired = false;   // soltar tras la pulsacion larga no es un toque
        return;
    }
    if (flex_fk_any(P.root) || i < 0 || i >= P.n || P.opening || P.creating) {
        return;
    }
    if (P.multi) {
        P.mask ^= 1u << i;
        list_render();
    } else {
        char p[FLEX_FS_PATH_MAX];
        path_of(i, p, sizeof(p));
        canvas_open(p, false);
    }
}

static void card_long_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (flex_fk_any(P.root) || i < 0 || i >= P.n || P.opening || P.creating) {
        return;
    }
    P.long_fired = true;
    P.sel_idx = i;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    flex_fk_menu_open(P.root, (int)p.x, (int)p.y - 40, false, menu_cb, NULL);
}

static void card_press_cb(lv_event_t *e)
{
    (void)e;
    P.long_fired = false;
}

static void fab_click_cb(lv_event_t *e)
{
    (void)e;
    if (!flex_fk_any(P.root) && !P.multi) {
        paint_new();
    }
}

static void bar_click_cb(lv_event_t *e)
{
    int x, y;
    tap_point(lv_event_get_current_target_obj(e), &x, &y);
    x += 12;   // la barra esta en (12, 672): el toque llega en coordenadas de la barra
    if (flex_fk_any(P.root)) {
        return;
    }
    if (x > 380) {   // Salir
        P.multi = false;
        P.mask = 0;
        list_render();
    } else if (x > 250) {   // Papelera: todos los marcados, sin confirmacion
        P.trash_i = 0;
        trash_next_cb(ESP_OK, NULL);
    }
}

static void card_make(int i)
{
    const flex_palette_t *t = TH();
    int x, y, w, h;
    flex_pg_card_rect(i, 0, nav_reserve(), &x, &y, &w, &h);
    y -= FLEX_PG_VIEW_Y;   // el contenido empieza en la y del viewport
    char title[FLEX_FS_NAME_MAX];
    flex_fsn_stem(P.ents[i].name, title, sizeof(title));
    lv_obj_t *tl = flex_kit_text(P.gcontent, title, 2, t->txt, x + w / 2, y - 34, true);   // el titulo, 34 px POR ENCIMA de la tarjeta
    flex_label_one_line(tl, w + 12);
    lv_obj_set_x(tl, x + w / 2 - (w + 12) / 2);
    lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *c = box(P.gcontent, x, y, w, h, 12);
    fill(c, lv_color_white(), LV_OPA_COVER);
    lv_obj_set_clickable(c, true);
    lv_obj_set_user_data(c, (void *)(intptr_t)i);
    lv_obj_add_event_cb(c, card_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(c, card_long_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(c, card_press_cb, LV_EVENT_PRESSED, NULL);
    P.card[i] = c;
    card_attach_thumb(i);
    if (P.multi && (P.mask & (1u << i))) {
        lv_obj_set_style_border_width(c, 2, 0);   // doble contorno PRIM
        lv_obj_set_style_border_color(c, t->primary, 0);
        lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
        lv_obj_t *ck = box(c, w - 18 - 10 - 2, 18 - 10 - 2, 20, 20, LV_RADIUS_CIRCLE);   // -2: el borde de la tarjeta
        fill(ck, t->primary, LV_OPA_COVER);
        lv_obj_t *g = flex_glyph_create(ck, FLEX_GLYPH_CHECK, 20, 20);
        lv_obj_set_style_text_color(g, t->on_acc, 0);
    }
}

// Reconstruye las tarjetas y la barra / el boton flotante con lo que hay en memoria.
static void list_render(void)
{
    if (!P.groot || P.view != 0) {
        return;
    }
    const flex_palette_t *t = TH();
    int keep = P.list_ready ? (int)lv_obj_get_scroll_y(P.gview) : P.scroll;
    lv_obj_clean(P.gcontent);
    memset(P.card, 0, sizeof(P.card));
    lv_obj_set_size(P.gcontent, 480, 800 - FLEX_PG_VIEW_Y + flex_pg_max_scroll(P.n, nav_reserve()));
    if (P.list_ready && P.n == 0) {
        flex_kit_text(P.gcontent, "No hay dibujos todav\xC3\xAD" "a", 3, t->txt2, 240, 320 - FLEX_PG_VIEW_Y, true);
        flex_kit_text(P.gcontent, "Pulsa + para crear uno", 1, t->mute, 240, 364 - FLEX_PG_VIEW_Y, true);
    }
    for (int i = 0; i < P.n; i++) {
        card_make(i);
    }
    lv_obj_update_layout(P.gview);
    lv_obj_scroll_to_y(P.gview, keep, LV_ANIM_OFF);
    lv_obj_set_hidden(P.fabwrap, P.multi);
    lv_obj_set_hidden(P.bar, !P.multi);
}

static void list_scroll_cb(lv_event_t *e)
{
    (void)e;
    P.scroll = (int)lv_obj_get_scroll_y(P.gview);
    mark_dirty();   // el desplazamiento de la galeria es estado de sesion
}

static void gallery_build(void)
{
    const flex_palette_t *t = TH();
    P.groot = box(P.root, 0, 0, 480, 800, 0);
    fill(P.groot, t->page, LV_OPA_COVER);
    flex_app_header(P.groot, "Paint", true, header_menu_cb);
    P.gview = box(P.groot, 0, FLEX_PG_VIEW_Y, 480, 800 - FLEX_PG_VIEW_Y, 0);
    lv_obj_set_clickable(P.gview, true);
    lv_obj_set_scrollable(P.gview, true);
    lv_obj_set_scroll_dir(P.gview, LV_DIR_VER);
    lv_obj_set_scroll_elastic(P.gview, false);
    lv_obj_set_scroll_momentum(P.gview, false);
    lv_obj_set_scrollbar_mode(P.gview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(P.gview, list_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    P.gcontent = box(P.gview, 0, 0, 480, 800 - FLEX_PG_VIEW_Y, 0);
    // boton flotante: el rotulo "Nuevo dibujo" a su izquierda y el circulo r = 52 con borde de 5 px y el signo +
    int fx, fy, fr;
    flex_pg_fab(&fx, &fy, &fr);
    P.fabwrap = box(P.groot, 0, 0, 480, 800, 0);
    lv_obj_t *lb = flex_kit_text(P.fabwrap, "Nuevo dibujo", 2, t->txt, 0, fy - 34, false);
    lv_obj_set_width(lb, fx - fr - 6);   // alineado a la derecha en x = fx - fr - 6
    lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *fab = box(P.fabwrap, fx - fr, fy - fr, 2 * fr, 2 * fr, LV_RADIUS_CIRCLE);
    fill(fab, t->surf, LV_OPA_COVER);
    lv_obj_set_style_border_width(fab, 5, 0);
    lv_obj_set_style_border_color(fab, t->border, 0);
    lv_obj_set_style_border_opa(fab, LV_OPA_COVER, 0);
    lv_obj_set_clickable(fab, true);
    lv_obj_add_event_cb(fab, fab_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *h = box(fab, fr - 5 - 22, fr - 5 - 5, 44, 10, 4);   // el + : 44x10 y 10x44, r4 (menos el borde de 5 px)
    fill(h, t->txt, LV_OPA_COVER);
    lv_obj_t *v = box(fab, fr - 5 - 5, fr - 5 - 22, 10, 44, 4);
    fill(v, t->txt, LV_OPA_COVER);
    // barra de seleccion (12, 672, 456, 60)
    P.bar = box(P.groot, 12, 800 - 128, 456, 60, 16);
    flex_surface(P.bar, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    lv_obj_set_clickable(P.bar, true);
    lv_obj_add_event_cb(P.bar, bar_click_cb, LV_EVENT_CLICKED, NULL);
    flex_kit_text(P.bar, "Selecci\xC3\xB3n", 2, t->txt, 28 - 12, 692 - (800 - 128), false);
    lv_obj_t *p = flex_kit_text(P.bar, "Papelera", 2, t->warn, 0, 692 - (800 - 128), false);
    lv_obj_set_width(p, 340 - 12);   // alineado a la derecha en x = 340
    lv_obj_set_style_text_align(p, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *s = flex_kit_text(P.bar, "Salir", 2, t->txt2, 0, 692 - (800 - 128), false);
    lv_obj_set_width(s, 452 - 12);   // alineado a la derecha en x = 452
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_hidden(P.bar, true);
}

static void gallery_free(void)
{
    if (P.groot) {
        lv_obj_delete(P.groot);   // primero los objetos: las miniaturas siguen apuntando a sus buffers hasta aqui
    }
    P.groot = P.gview = P.gcontent = P.fabwrap = P.bar = NULL;
    memset(P.card, 0, sizeof(P.card));
    thumbs_free();
}

static void gallery_show(void)
{
    flex_fk_close_all(P.root);
    P.view = 0;
    if (!P.groot) {
        gallery_build();
    }
    P.list_ready = false;
    list_reload();
}

// ===== lienzo ===================================================================================================================
static void canvas_replay_all(void)
{
    if (!P.cbuf) {
        return;
    }
    flex_raster_fill(&P.ras, RGB565(250, 250, 252));
    flex_fxp_replay(CV.doc.d, CV.doc.len, CV.lay.scale, CV.lay.x, CV.lay.y, seg_to_raster, &P.ras);
    if (P.cimg) {
        lv_obj_invalidate(P.cimg);
    }
}

static void dirty_add(int x0, int y0, int x1, int y1)
{
    if (!P.dirty_valid) {
        P.dirty.x1 = x0;
        P.dirty.y1 = y0;
        P.dirty.x2 = x1;
        P.dirty.y2 = y1;
        P.dirty_valid = true;
        return;
    }
    if (x0 < P.dirty.x1) P.dirty.x1 = x0;
    if (y0 < P.dirty.y1) P.dirty.y1 = y0;
    if (x1 > P.dirty.x2) P.dirty.x2 = x1;
    if (y1 > P.dirty.y2) P.dirty.y2 = y1;
}

static void live_dot(int x, int y, int rad, uint16_t c, void *u)
{
    (void)u;
    flex_raster_fill_circle_aa(&P.ras, (float)x, (float)y, (float)rad, c);
    dirty_add(x - rad - 1, y - rad - 1, x + rad + 1, y + rad + 1);
}

static void live_seg(int x0, int y0, int x1, int y1, int rad, uint16_t c, void *u)
{
    (void)u;
    flex_raster_stroke_seg_aa(&P.ras, (float)x0, (float)y0, (float)x1, (float)y1, (float)rad, c);
    dirty_add((x0 < x1 ? x0 : x1) - rad - 1, (y0 < y1 ? y0 : y1) - rad - 1, (x0 > x1 ? x0 : x1) + rad + 1, (y0 > y1 ? y0 : y1) + rad + 1);
}

static const flex_pcv_draw_t LIVE = {live_dot, live_seg};

static void sub_build(void)
{
    if (!P.croot) {
        return;
    }
    if (P.csub) {
        lv_obj_delete(P.csub);
    }
    int n = flex_pcv_strokes(&CV);
    char sub[48];
    snprintf(sub, sizeof(sub), n == 1 ? "%d trazo guardado" : "%d trazos guardados", n);
    P.csub = flex_kit_text(P.croot, sub, 1, TH()->txt2, 240, 66, true);
}

static void tools_build(void)
{
    const flex_palette_t *t = TH();
    if (P.ctools) {
        lv_obj_clean(P.ctools);
    }
    int y = 8, sw = 34, gap = 6, x0 = 10;
    for (int i = 0; i < 6; i++) {
        int cx = x0 + i * (sw + gap) + sw / 2;
        lv_obj_t *d = box(P.ctools, cx - (sw / 2 - 2), y + 2, sw - 4, sw - 4, LV_RADIUS_CIRCLE);
        fill(d, flex_c565(flex_paint_pal[i]), LV_OPA_COVER);
        if (flex_paint_pal[i] == CV.color) {   // el elegido lleva un aro doble
            lv_obj_t *ring = box(P.ctools, cx - sw / 2, y, sw, sw, LV_RADIUS_CIRCLE);
            lv_obj_set_style_border_width(ring, 2, 0);
            lv_obj_set_style_border_color(ring, t->txt, 0);
            lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
        }
    }
    // Grosor: un punto del tamano real (lo que se ve es lo que se pinta)
    int gx = x0 + 6 * (sw + gap) + 8;
    lv_obj_t *gs = box(P.ctools, gx, y, 44, sw, 8);
    flex_surface(gs, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    int rad = flex_paint_sizes[CV.size_ix];
    lv_obj_t *dot = box(P.ctools, gx + 22 - rad, y + sw / 2 - rad, 2 * rad, 2 * rad, LV_RADIUS_CIRCLE);
    fill(dot, t->txt, LV_OPA_COVER);
    lv_obj_t *ub = box(P.ctools, gx + 52, y, 76, sw, 8);
    flex_surface(ub, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    flex_kit_text(P.ctools, "Deshacer", 1, t->txt, gx + 90, y + 9, true);
    lv_obj_t *cb = box(P.ctools, gx + 134, y, 66, sw, 8);
    fill(cb, t->danger, LV_OPA_COVER);
    flex_kit_text(P.ctools, "Limpiar", 1, t->on_acc, gx + 167, y + 9, true);
}

static void write_cb(esp_err_t err, void *user);
static void leave_done(void);

static void write_kick(void)
{
    if (!flex_pcv_is_open(&CV) || !P.path[0] || !P.need_write) {
        return;
    }
    if (P.wr_busy) {
        P.wr_again = true;   // la que esta en vuelo no lleva lo ultimo: se repite al terminar, con el dibujo ya actualizado
        return;
    }
    if (flex_fs_write_async(P.path, CV.doc.d, CV.doc.len, write_cb, (void *)(uintptr_t)P.wgen) == ESP_OK) {
        P.wr_busy = true;
        P.need_write = false;
    } else {
        flex_kit_toast(P.root, "No se pudo guardar el dibujo");   // sin sitio en la cola o sin memoria: se reintenta con el siguiente trazo
    }
}

static void write_cb(esp_err_t err, void *user)
{
    if ((uint32_t)(uintptr_t)user != P.wgen || !P.created) {
        return;   // la app se cerro entre medias
    }
    P.wr_busy = false;
    if (err != ESP_OK) {
        P.need_write = true;   // se reintenta con el siguiente trazo (o al salir)
        flex_kit_toast(P.root, "No se pudo guardar el dibujo");
    }
    if (P.wr_again) {
        P.wr_again = false;
        if (P.need_write || err == ESP_OK) {
            P.need_write = true;
            write_kick();
        }
    }
    if (P.leave_pending && !P.wr_busy) {
        leave_done();
    }
}

static void apply_flags(void)
{
    if (CV.full) {
        flex_kit_toast(P.root, "El dibujo es demasiado grande");   // Arduino no tenia tope y se quedaba sin sitio sin avisar
    }
    if (CV.modified) {
        P.need_write = true;
        sub_build();
    }
    if (CV.redraw) {
        canvas_replay_all();
        sub_build();
    }
    if (CV.tools) {
        P.color = CV.color;
        P.size_ix = CV.size_ix;
        tools_build();
    }
    if (CV.sess) {
        mark_dirty();
    }
    flex_pcv_ack(&CV);
    write_kick();
}

static void canvas_pointer_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (!P.cbuf || P.leave_pending || P.suspended) {
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        flex_pcv_touch(&CV, true, (int)p.x, (int)p.y, &LIVE, NULL);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        flex_pcv_touch(&CV, false, (int)p.x, (int)p.y, &LIVE, NULL);   // dedo levantado: el trazo se pasa al dibujo y se guarda
    } else {
        return;
    }
    if (P.dirty_valid) {
        lv_area_t a = P.dirty;   // solo lo que se ha pintado, no el lienzo entero
        P.dirty_valid = false;
        lv_obj_invalidate_area(P.cimg, &a);
    }
    apply_flags();
}

static void tools_click_cb(lv_event_t *e)
{
    int x, y;
    tap_point(lv_event_get_current_target_obj(e), &x, &y);
    y += flex_paint_bot(nav_reserve());
    if (flex_fk_any(P.root) || P.leave_pending) {
        return;
    }
    flex_pcv_tap(&CV, x, y);   // color, grosor, deshacer o limpiar
    apply_flags();
}

static void canvas_back_click_cb(lv_event_t *e)
{
    (void)e;
    flex_sys_back();   // misma logica que el boton de la barra del sistema: capa -> pantalla -> suspender y salir
}

// Rasterizador sobre el buffer del lienzo: la esquina del buffer es la del dibujo en pantalla.
static bool canvas_buf_make(void)
{
    if (P.cbuf) {
        return true;
    }
    P.cbuf = lv_draw_buf_create((uint32_t)CV.lay.disp_w, (uint32_t)CV.lay.disp_h, LV_COLOR_FORMAT_RGB565, 0);
    if (!P.cbuf) {
        return false;
    }
    flex_raster_init(&P.ras, (uint16_t *)P.cbuf->data, CV.lay.disp_w, CV.lay.disp_h, (int)(P.cbuf->header.stride / 2), CV.lay.x, CV.lay.y);
    return true;
}

static void canvas_view_make(void)
{
    const flex_palette_t *t = TH();
    int nav = nav_reserve();
    P.croot = box(P.root, 0, 0, 480, 800, 0);
    fill(P.croot, t->page, LV_OPA_COVER);
    lv_obj_t *bz = box(P.croot, 0, 0, 48, 48, 0);   // chevron "atras" de la cabecera: x < 48, y < 48
    lv_obj_set_clickable(bz, true);
    lv_obj_add_event_cb(bz, canvas_back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *g = flex_glyph_create(bz, FLEX_GLYPH_CHEVRON_LEFT, 28, 28);
    lv_obj_set_pos(g, 10, 4);
    lv_obj_set_style_text_color(g, t->nav, 0);
    char title[FLEX_FS_NAME_MAX];
    flex_fsn_stem(flex_fsn_base(P.path), title, sizeof(title));   // nombre REAL del fichero abierto
    lv_obj_t *tt = flex_kit_text(P.croot, title, 3, t->txt, 240, 30, true);
    flex_label_one_line(tt, 360);
    lv_obj_set_x(tt, 240 - 180);
    lv_obj_set_style_text_align(tt, LV_TEXT_ALIGN_CENTER, 0);
    sub_build();
    // el lienzo: gris alrededor y el dibujo con su proporcion encima
    lv_obj_t *bg = box(P.croot, FLEX_PAINT_CX, FLEX_PAINT_TOP, FLEX_PAINT_CW, flex_paint_ch(nav), 0);
    fill(bg, flex_c565(RGB565(218, 222, 230)), LV_OPA_COVER);
    P.cbox = box(P.croot, FLEX_PAINT_CX, FLEX_PAINT_TOP, FLEX_PAINT_CW, flex_paint_ch(nav), 0);   // recoge el dedo en todo el lienzo
    lv_obj_set_clickable(P.cbox, true);
    lv_obj_set_press_lock(P.cbox, true);
    lv_obj_add_event_cb(P.cbox, canvas_pointer_cb, LV_EVENT_ALL, NULL);
    P.cimg = lv_image_create(P.croot);
    lv_obj_set_pos(P.cimg, CV.lay.x, CV.lay.y);
    lv_obj_set_clickable(P.cimg, false);
    lv_image_set_src(P.cimg, P.cbuf);
    lv_obj_move_background(bg);
    lv_obj_move_foreground(P.cbox);
    // barra de herramientas: debajo del lienzo, del color de la tarjeta
    P.ctools = box(P.croot, 0, flex_paint_bot(nav), 480, 800 - nav - flex_paint_bot(nav), 0);
    flex_surface(P.ctools, FLEX_SURF_CARD, FLEX_BD_FLAT);
    lv_obj_set_clickable(P.ctools, true);
    lv_obj_add_event_cb(P.ctools, tools_click_cb, LV_EVENT_CLICKED, NULL);
    tools_build();
}

static void canvas_free_view(void)
{
    if (P.croot) {
        lv_obj_delete(P.croot);   // primero los objetos: la imagen sigue apuntando al buffer hasta aqui
    }
    P.croot = P.cbox = P.cimg = P.ctools = P.csub = NULL;
    if (P.cbuf) {
        lv_draw_buf_destroy(P.cbuf);
        P.cbuf = NULL;
    }
    memset(&P.ras, 0, sizeof(P.ras));
    P.dirty_valid = false;
}

static void canvas_show(const char *path, bool restored)
{
    (void)path;
    (void)restored;
    flex_pcv_ack(&CV);
    CV.color = P.color;
    CV.size_ix = (uint8_t)P.size_ix;
    if (!canvas_buf_make()) {
        flex_pcv_free(&CV);
        P.path[0] = '\0';
        flex_kit_toast(P.root, "Sin memoria para el lienzo");
        if (!P.groot) {
            gallery_show();
        }
        return;
    }
    flex_fk_close_all(P.root);
    gallery_free();
    P.view = 1;
    P.leave_pending = false;
    canvas_replay_all();
    canvas_view_make();
    mark_dirty();
}

// Sale del lienzo: el trazo en curso al dibujo, el dibujo al disco, y la galeria se relee cuando la escritura haya terminado
// (la cola del almacenamiento es en orden, pero la repeticion pendiente aun no esta en ella).
static void leave_done(void)
{
    P.leave_pending = false;
    canvas_free_view();
    flex_pcv_free(&CV);
    P.path[0] = '\0';
    P.need_write = P.wr_again = false;
    gallery_show();
    mark_dirty();
}

static void canvas_leave(void)
{
    flex_pcv_flush(&CV);
    if (CV.modified) {
        P.need_write = true;
    }
    flex_pcv_ack(&CV);
    write_kick();
    if (P.wr_busy || P.wr_again) {
        P.leave_pending = true;
        return;
    }
    leave_done();
}

// ===== ciclo de vida y sesion (03 §7.5) ===========================================================================================
static void nofs_back_cb(void *u)
{
    (void)u;
    flex_app_close();
}

static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    (void)area;
    bool had = P.sess_valid;
    flex_paint_sess_t sess = P.sess;
    uint16_t color = P.color;
    int six = P.size_ix;
    memset(&P, 0, sizeof(P));
    P.sess = sess;
    P.sess_valid = had;
    P.root = root;
    P.created = true;
    P.view = 0;
    P.sel_idx = -1;
    P.color = flex_paint_pal[0];
    P.size_ix = had ? sess.size_ix : (six >= 0 && six < 3 && color ? six : 1);
    flex_pcv_init(&CV, nav_reserve(), P.color, P.size_ix);
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    if (!st.fs_mounted) {   // sin almacenamiento no hay nada real que ensenar
        flex_fk_nofs_screen(root, "Paint", nofs_back_cb, NULL);
        return;
    }
    if (had) {
        P.scroll = sess.scroll;
    }
    if (had && sess.view == 1 && sess.path[0]) {
        P.color = (uint16_t)sess.color;   // al restaurar el lienzo vuelve el color; al entrar en la galeria no (paintEnter)
        gallery_build();
        P.view = 0;
        canvas_open(sess.path, true);   // el fichero ya no existe -> la galeria (lo dice la lectura)
    } else {
        gallery_show();
    }
}

static void on_close(void)
{
    if (P.created && P.view == 1 && flex_pcv_is_open(&CV) && P.path[0]) {
        flex_pcv_flush(&CV);
        if (CV.modified || P.need_write || P.wr_busy) {
            flex_fs_write_async(P.path, CV.doc.d, CV.doc.len, NULL, NULL);   // lo ultimo, antes de soltar el dibujo
        }
    }
    gallery_free();
    canvas_free_view();
    flex_pcv_free(&CV);
    uint32_t wg = P.wgen + 1;
    flex_paint_sess_t sess = P.sess;
    memset(&P, 0, sizeof(P));
    P.wgen = wg;   // una escritura que vuelva despues no es de la app que se abre luego
    P.sess = sess;
    P.sess_valid = false;   // "paintCloseApp": la sesion se olvida
    P.sess.view = 0;
}

static void on_suspend(void)
{
    P.suspended = true;
    if (P.view == 1 && flex_pcv_is_open(&CV)) {
        flex_pcv_flush(&CV);   // paintSuspend: el trazo en curso al dibujo y a disco
        if (CV.modified) {
            P.need_write = true;
        }
        flex_pcv_ack(&CV);
        write_kick();
        CV.px = CV.py = -1;
    }
    P.long_fired = false;
    flex_fk_close_all(P.root);   // la superficie de dialogos es compartida con Notas, Galeria y Archivos
}

static void on_resume(void)
{
    P.suspended = false;
    if (P.view == 1) {
        if (!P.cbuf && flex_pcv_is_open(&CV)) {   // el lienzo se solto con shed: se reconstruye del dibujo en memoria
            if (canvas_buf_make()) {
                canvas_replay_all();
                if (P.croot) {
                    lv_obj_delete(P.croot);
                    P.croot = P.cbox = P.cimg = P.ctools = P.csub = NULL;
                }
                canvas_view_make();
            } else {
                flex_kit_toast(P.root, "Sin memoria para el lienzo");
            }
        }
    } else if (P.view == 0 && P.groot) {
        list_reload();
    }
}

// "atras": capa -> pantalla -> (false) salir de la app
static bool on_back(void)
{
    if (flex_fk_any(P.root)) {   // dialogos del kit: cerrar el de arriba (la papelera avisa para recargar la lista)
        flex_fk_close_all(P.root);
        if (P.view == 0) {
            list_reload();
        }
        return true;
    }
    if (P.view == 1) {
        if (!P.leave_pending) {
            canvas_leave();
        }
        return true;
    }
    return false;
}

static bool dirty_cb(void)
{
    // CAMBIOS SIN GUARDAR: un trazo que todavia no se ha pasado al dibujo, o un dibujo que aun no esta en el disco
    return P.view == 1 && (flex_pcv_dirty(&CV) || P.wr_busy || P.need_write);
}

static size_t shed(void)
{
    size_t freed = 0;
    if (P.view == 0 && P.groot) {
        lv_obj_clean(P.gcontent);   // las tarjetas llevan las miniaturas: fuera ellas y luego sus buffers
        memset(P.card, 0, sizeof(P.card));
        for (int i = 0; i < FLEX_PG_MAX; i++) {
            if (P.thumb[i]) {
                freed += P.thumb[i]->data_size;
            }
        }
        thumbs_free();
        P.list_ready = false;   // al reanudar se vuelve a listar y a dibujar
        P.scroll = (int)lv_obj_get_scroll_y(P.gview);
    } else if (P.view == 1 && P.cbuf && !flex_pcv_dirty(&CV)) {
        freed = P.cbuf->data_size;
        P.scroll = P.scroll;   // sin cambios: el dibujo sigue en memoria y el lienzo se reconstruye de el
        canvas_free_view();
    }
    return freed;
}

static bool save_session(void *out, size_t cap, size_t *len)
{
    if (cap < sizeof(flex_paint_sess_t)) {
        return false;
    }
    flex_paint_sess_t s;
    if (P.view == 1 && flex_pcv_is_open(&CV) && P.path[0]) {
        flex_pcv_flush(&CV);   // paintSaveSess vuelca el trazo en curso
        if (CV.modified) {
            P.need_write = true;
        }
        flex_pcv_ack(&CV);
        write_kick();
        flex_paint_sess_pack(&s, true, P.path, CV.size_ix, CV.color, P.scroll);
    } else {
        flex_paint_sess_pack(&s, false, "", P.size_ix, P.color, P.groot && P.gview ? (int)lv_obj_get_scroll_y(P.gview) : P.scroll);
    }
    memcpy(out, &s, sizeof(s));
    *len = sizeof(s);
    return true;
}

static void load_session(const void *data, size_t len)
{
    P.sess_valid = false;
    if (data && len == sizeof(flex_paint_sess_t)) {
        memcpy(&P.sess, data, sizeof(P.sess));
        flex_paint_sess_normalize(&P.sess);
        P.sess_valid = true;
    }
}

const flex_app_ops_t flex_app_paint_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_suspend = on_suspend,
    .on_close = on_close,
    .on_back = on_back,
    .dirty = dirty_cb,
    .save_session = save_session,
    .load_session = load_session,
    .shed = shed,
};

// ---- pruebas ---------------------------------------------------------------------------------------------------------------------------------
int flex_app_paint_view(void) { return P.created ? P.view : -1; }
int flex_app_paint_count(void) { return P.n; }
bool flex_app_paint_ready(void)
{
    if (!P.list_ready) {
        return false;
    }
    for (int i = 0; i < P.n; i++) {
        if (!P.thumb_done[i]) {
            return false;
        }
    }
    return true;
}
const char *flex_app_paint_title(int i) { return (i >= 0 && i < P.n) ? P.ents[i].name : ""; }
bool flex_app_paint_multi(int *marked)
{
    if (marked) {
        int c = 0;
        for (int i = 0; i < P.n; i++) c += (P.mask >> i) & 1u;
        *marked = c;
    }
    return P.multi;
}
const char *flex_app_paint_path(void) { return P.path; }
lv_obj_t *flex_app_paint_root(void) { return P.root; }
int flex_app_paint_strokes(void) { return flex_pcv_is_open(&CV) ? flex_pcv_strokes(&CV) : -1; }
int flex_app_paint_pending(void) { return CV.n; }
unsigned flex_app_paint_color(void) { return P.view == 1 ? CV.color : P.color; }
int flex_app_paint_size(void) { return P.view == 1 ? CV.size_ix : P.size_ix; }
bool flex_app_paint_writing(void) { return P.wr_busy || P.need_write || P.wr_again; }
int flex_app_paint_scroll(void) { return P.view == 0 && P.gview ? (int)lv_obj_get_scroll_y(P.gview) : P.scroll; }
// Un pixel del lienzo (coordenadas de pantalla) y de la miniatura i (coordenadas de la tarjeta); -1 si no hay
int flex_app_paint_canvas_px(int x, int y)
{
    if (!P.cbuf) {
        return -1;
    }
    int bx = x - P.ras.ox, by = y - P.ras.oy;
    if (bx < 0 || by < 0 || bx >= P.ras.w || by >= P.ras.h) {
        return -1;
    }
    return P.ras.buf[(size_t)by * (size_t)P.ras.stride + (size_t)bx];
}
int flex_app_paint_thumb_ink(int i)
{
    if (i < 0 || i >= FLEX_PG_MAX || !P.thumb[i]) {
        return -1;
    }
    const uint16_t *d = (const uint16_t *)P.thumb[i]->data;
    int stride = (int)(P.thumb[i]->header.stride / 2), w = (int)P.thumb[i]->header.w, h = (int)P.thumb[i]->header.h, ink = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            ink += d[y * stride + x] != RGB565(255, 255, 255);
        }
    }
    return ink;
}
// Hijos de la tarjeta i que son una imagen (la miniatura puesta de verdad en la pantalla)
int flex_app_paint_card_images(int i)
{
    if (i < 0 || i >= FLEX_PG_MAX || !P.card[i]) {
        return -1;
    }
    int n = 0;
    for (uint32_t k = 0; k < lv_obj_get_child_count(P.card[i]); k++) {
        n += lv_obj_check_type(lv_obj_get_child(P.card[i], (int32_t)k), &lv_image_class) ? 1 : 0;
    }
    return n;
}
bool flex_app_paint_thumb_present(int i) { return i >= 0 && i < FLEX_PG_MAX && P.thumb[i] != NULL; }
void flex_app_paint_doc_rect(int *x, int *y, int *w, int *h)
{
    *x = CV.lay.x;
    *y = CV.lay.y;
    *w = CV.lay.disp_w;
    *h = CV.lay.disp_h;
}
