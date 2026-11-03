// Flex OS Ultra · kit de archivos compartido (ver flex_filekit.h).
#include "flex_filekit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifndef FLEX_SIM
#include "esp_heap_caps.h"
#endif
#include "flex_fk_model.h"
#include "flex_app.h"
#include "flex_fs_names.h"
#include "flex_frame.h"
#include "flex_icons.h"
#include "flex_kb.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_storage.h"
#include "flex_theme.h"

#define SCR_W 480
#define SCR_H 800

// ===== capas ===============================================================================================
typedef enum { K_MENU = 1, K_NAME, K_ASK, K_TRASH, K_NOFS } kind_t;

// Cabecera comun (primer miembro de cada contexto): reconoce una capa del kit entre los hijos de la raiz y
// recuerda que ya se esta cerrando (el borrado puede ser asincrono: no se cierra dos veces).
typedef struct {
    kind_t kind;
    bool closing;
    lv_obj_t *layer;
} hdr_t;

static void marker_cb(lv_event_t *e)
{
    (void)e;   // solo marca: hdr_of() la busca por identidad
}

static hdr_t *hdr_of(lv_obj_t *o)
{
    uint32_t n = lv_obj_get_event_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(o, i);
        if (lv_event_dsc_get_cb(d) == marker_cb) {
            return lv_event_dsc_get_user_data(d);
        }
    }
    return NULL;
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

static void fill(lv_obj_t *o, lv_color_t c)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

// Capa de pantalla completa que se traga los toques. ctx_size >= sizeof(hdr_t).
static lv_obj_t *layer_new(lv_obj_t *root, kind_t kind, size_t ctx_size, void **ctx_out)
{
    hdr_t *c = calloc(1, ctx_size);
    if (!c) {
        return NULL;
    }
    lv_obj_t *l = box(root, 0, 0, SCR_W, SCR_H, 0);
    lv_obj_set_clickable(l, true);
    lv_obj_move_foreground(l);
    c->kind = kind;
    c->layer = l;
    lv_obj_add_event_cb(l, marker_cb, LV_EVENT_DELETE, c);
    *ctx_out = c;
    return l;
}

static void ctx_free_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

// Cierra la capa YA (una sola vez); el contexto sigue valido hasta que LVGL la borre.
static void layer_close(hdr_t *h)
{
    if (!h->closing) {
        h->closing = true;
        lv_obj_delete_async(h->layer);
    }
}

// Toque de la capa, en coordenadas de la capa (la raiz puede estar desplazada por una animacion).
static void tap_point(lv_obj_t *layer, int *x, int *y)
{
    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(layer, &a);
    *x = (int)p.x - (int)a.x1;
    *y = (int)p.y - (int)a.y1;
}

// Texto centrado en cx que no pasa de w: una linea, con "..." si no cabe (flex_label_one_line deja el texto a la izquierda).
static void center_clip(lv_obj_t *l, int cx, int w)
{
    flex_label_one_line(l, w);
    lv_obj_set_x(l, cx - w / 2);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

static void chevron(lv_obj_t *layer, int cx, int cy)
{
    lv_obj_t *g = flex_glyph_create(layer, FLEX_GLYPH_CHEVRON_LEFT, 28, 28);
    lv_obj_set_pos(g, cx - 14, cy - 14);
    lv_obj_set_style_text_color(g, flex_th()->txt, 0);
}

void flex_fk_close_all(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    for (int32_t i = (int32_t)lv_obj_get_child_count(root) - 1; i >= 0; i--) {
        lv_obj_t *c = lv_obj_get_child(root, i);
        hdr_t *h = hdr_of(c);
        if (h && !h->closing) {
            h->closing = true;
            lv_obj_delete(c);
        }
    }
}

int flex_fk_layers(lv_obj_t *root)
{
    int k = 0;
    uint32_t n = root ? lv_obj_get_child_count(root) : 0;
    for (uint32_t i = 0; i < n; i++) {
        hdr_t *h = hdr_of(lv_obj_get_child(root, (int32_t)i));
        k += h && !h->closing;
    }
    return k;
}

bool flex_fk_any(lv_obj_t *root)
{
    return flex_fk_layers(root) > 0;
}

// ===== menu contextual =======================================================================================
typedef struct {
    hdr_t h;
    flex_fk_menu_cb_t cb;
    void *user;
    bool cloud;
    flex_fk_rect_t r;
} menu_t;

static const char *const MENU_LBL[5] = {"Seleccionar", "Eliminar", "Renombrar", "Papelera", "Subir a Flex Cloud"};
static const flex_glyph_t MENU_GLYPH[5] = {FLEX_GLYPH_FK_SELECT, FLEX_GLYPH_FK_DELETE, FLEX_GLYPH_FK_RENAME,
                                           FLEX_GLYPH_FK_TRASH, FLEX_GLYPH_FK_CLOUD};

static void menu_click(lv_event_t *e)
{
    menu_t *c = lv_event_get_user_data(e);
    if (c->h.closing) {
        return;
    }
    int x, y;
    tap_point(c->h.layer, &x, &y);
    int a = flex_fk_menu_hit(&c->r, c->cloud, x, y);   // -1: fuera del panel
    flex_fk_menu_cb_t cb = c->cb;
    void *u = c->user;
    layer_close(&c->h);   // el menu se cierra con cualquier toque; el callback ya puede abrir otra capa
    if (cb) {
        cb(a, u);
    }
}

lv_obj_t *flex_fk_menu_open(lv_obj_t *root, int px, int py, bool cloud, flex_fk_menu_cb_t cb, void *user)
{
    menu_t *c;
    lv_obj_t *l = layer_new(root, K_MENU, sizeof(*c), (void **)&c);
    if (!l) {
        return NULL;
    }
    lv_obj_add_event_cb(l, ctx_free_cb, LV_EVENT_DELETE, c);
    c->cb = cb;
    c->user = user;
    c->cloud = cloud;
    flex_fk_menu_geom(px, py, cloud, &c->r);
    lv_obj_add_event_cb(l, menu_click, LV_EVENT_CLICKED, c);
    const flex_palette_t *t = flex_th();
    lv_obj_t *p = box(l, c->r.x, c->r.y, c->r.w, c->r.h, 18);
    flex_surface(p, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    for (int i = 0; i < flex_fk_menu_rows(cloud); i++) {
        int ry = FLEX_FK_MENU_PAD + i * FLEX_FK_MENU_RH;
        lv_obj_t *lb = flex_kit_text(p, MENU_LBL[i], 3, i == FLEX_FK_ACT_DEL ? t->danger : t->txt, 16, ry + 10, false);
        flex_label_one_line(lb, c->r.w - 42 - 16);
        lv_obj_t *g = flex_glyph_create(p, MENU_GLYPH[i], 40, 40);
        lv_obj_set_pos(g, c->r.w - 32 - 20, ry + FLEX_FK_MENU_RH / 2 - 20);
        lv_color_t col = i == FLEX_FK_ACT_DEL ? t->danger : i == FLEX_FK_ACT_TRASH ? t->txt : t->txt2;
        lv_obj_set_style_text_color(g, col, 0);
        lv_obj_set_style_bg_color(g, i == FLEX_FK_ACT_DEL ? t->on_acc : t->surf2, 0);   // detalle del glifo
        lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    }
    return l;
}

// ===== confirmacion ============================================================================================
typedef struct {
    hdr_t h;
    flex_fk_ask_cb_t cb;
    void *user;
} ask_t;

static void ask_click(lv_event_t *e)
{
    ask_t *c = lv_event_get_user_data(e);
    if (c->h.closing) {
        return;
    }
    int x, y;
    tap_point(c->h.layer, &x, &y);
    int r = flex_fk_ask_hit(x, y);
    if (r == 0) {
        return;   // dentro de la tarjeta, sin boton: nada
    }
    flex_fk_ask_cb_t cb = c->cb;
    void *u = c->user;
    layer_close(&c->h);
    if (cb) {
        cb(r == 1, u);
    }
}

lv_obj_t *flex_fk_ask_open(lv_obj_t *root, const char *msg, const char *sub, flex_fk_ask_cb_t cb, void *user)
{
    ask_t *c;
    lv_obj_t *l = layer_new(root, K_ASK, sizeof(*c), (void **)&c);
    if (!l) {
        return NULL;
    }
    lv_obj_add_event_cb(l, ctx_free_cb, LV_EVENT_DELETE, c);
    c->cb = cb;
    c->user = user;
    lv_obj_add_event_cb(l, ask_click, LV_EVENT_CLICKED, c);
    flex_fk_rect_t card, no, yes;
    flex_fk_ask_geom(&card, &no, &yes);
    const flex_palette_t *t = flex_th();
    lv_obj_t *cd = box(l, card.x, card.y, card.w, card.h, 24);
    flex_surface(cd, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    lv_obj_t *m = flex_kit_text(cd, msg ? msg : "", 2, t->txt, card.w / 2, 26, true);
    center_clip(m, card.w / 2, card.w - 24);
    if (sub && sub[0]) {
        lv_obj_t *s = flex_kit_text(cd, sub, 1, t->txt2, card.w / 2, 62, true);
        center_clip(s, card.w / 2, card.w - 24);
    }
    lv_obj_t *b1 = box(l, no.x, no.y, no.w, no.h, 16);
    fill(b1, t->surf);
    flex_kit_text(b1, "Cancelar", 2, t->txt, no.w / 2, 18, true);
    lv_obj_t *b2 = box(l, yes.x, yes.y, yes.w, yes.h, 16);
    fill(b2, t->danger);
    flex_kit_text(b2, "Borrar", 2, t->on_acc, yes.w / 2, 18, true);
    return l;
}

// ===== dialogo de nombre =========================================================================================
typedef struct {
    hdr_t h;
    flex_fk_name_cb_t cb;
    void *user;
    char buf[FLEX_FS_NAME_MAX];
    lv_obj_t *text, *cursor;
} name_t;

static void name_refresh(name_t *c)
{
    lv_label_set_text(c->text, c->buf);
    flex_label_one_line(c->text, 440 - 38);
    int cw = flex_ui_text_w(c->buf, 2, NULL);
    int cx = 38 + cw + 2;
    lv_obj_set_x(c->cursor, cx > 438 ? 438 : cx);
}

static void name_finish(name_t *c, bool accept)
{
    if (c->h.closing) {
        return;
    }
    flex_fk_name_cb_t cb = c->cb;
    void *u = c->user;
    char out[FLEX_FS_NAME_MAX];
    snprintf(out, sizeof(out), "%s", c->buf);
    layer_close(&c->h);
    if (cb) {
        cb(accept ? out : NULL, u);
    }
}

static void name_on_text(lv_obj_t *kb, const char *utf8, void *user)
{
    (void)kb;
    name_t *c = user;
    if (!c->h.closing && flex_fk_name_append(c->buf, sizeof(c->buf), utf8)) {
        name_refresh(c);
    }
}

static void name_on_back(lv_obj_t *kb, void *user)
{
    (void)kb;
    name_t *c = user;
    if (!c->h.closing) {
        flex_fk_name_back(c->buf);
        name_refresh(c);
    }
}

static void name_on_enter(lv_obj_t *kb, void *user)
{
    (void)kb;
    name_t *c = user;
    name_finish(c, c->buf[0] != '\0');   // "Guardar" con el campo vacio cancela
}

static void name_click(lv_event_t *e)
{
    name_t *c = lv_event_get_user_data(e);
    int x, y;
    tap_point(c->h.layer, &x, &y);
    if (x < 60 && y < 90) {
        name_finish(c, false);   // esquina superior izquierda = cancelar
    }
}

lv_obj_t *flex_fk_name_open(lv_obj_t *root, const char *title, const char *initial, const char *hint,
                            flex_fk_name_cb_t cb, void *user)
{
    name_t *c;
    lv_obj_t *l = layer_new(root, K_NAME, sizeof(*c), (void **)&c);
    if (!l) {
        return NULL;
    }
    lv_obj_add_event_cb(l, ctx_free_cb, LV_EVENT_DELETE, c);
    c->cb = cb;
    c->user = user;
    snprintf(c->buf, sizeof(c->buf), "%s", initial ? initial : "");
    for (char *s = c->buf; *s; s++) {
        if (*s == '/') {
            *s = ' ';   // el nombre inicial tampoco puede partir la ruta
        }
    }
    const flex_palette_t *t = flex_th();
    fill(l, t->page);
    lv_obj_add_event_cb(l, name_click, LV_EVENT_CLICKED, c);
    chevron(l, 24, 38);
    flex_kit_text(l, title ? title : "", 3, t->txt, SCR_W / 2, 40, true);
    lv_obj_t *f = box(l, 24, 130, SCR_W - 48, 56, 14);
    flex_surface(f, FLEX_SURF_CARD, FLEX_BD_FLAT);
    c->text = flex_kit_text(l, c->buf, 2, t->txt, 38, 146, false);
    c->cursor = box(l, 38, 144, 2, 28, 0);
    fill(c->cursor, flex_accent());
    flex_kit_text(l, hint ? hint : "Escribe el nombre y pulsa Guardar", 1, t->txt2, SCR_W / 2, 206, true);
    flex_kb_cfg_t kc = {
        .flags = FLEX_KB_F_PAGE | FLEX_KB_F_NO_FAST,   // sobre la pagina; escribe al soltar, como el resto del dialogo
        .backdrop = FLEX_BD_FLAT,
        .bottom_reserve = flex_navbar_ctx() != FLEX_NAV_HIDDEN ? FLEX_NAV_H : 0,   // el teclado se apoya sobre la barra de navegacion
        .enter_label = "Guardar",
        .on_text = name_on_text,
        .on_backspace = name_on_back,
        .on_enter = name_on_enter,
        .user = c,
    };
    flex_kb_create(l, &kc);
    name_refresh(c);
    return l;
}

// ===== texto en caja ============================================================================================
typedef struct {
    char *buf;
    size_t len, cap;
} lines_t;

static void lines_emit(int x, int y, const char *line, void *ctx)
{
    (void)x;
    (void)y;
    lines_t *L = ctx;
    size_t n = strlen(line);
    if (L->len + n + 2 > L->cap) {
        return;
    }
    memcpy(L->buf + L->len, line, n);
    L->len += n;
    L->buf[L->len++] = '\n';
    L->buf[L->len] = '\0';
}

lv_obj_t *flex_fk_text_box(lv_obj_t *parent, int x, int y, int w, int h, const char *s, int fs, lv_color_t color)
{
    if (!s || !*s || w <= 0 || h <= 0) {
        return NULL;
    }
    size_t cap = strlen(s) + 8 * (size_t)(h / 8 + 1) + 16;   // las lineas caben en el propio texto + saltos
    char *buf = calloc(1, cap);
    if (!buf) {
        return NULL;
    }
    lines_t L = {buf, 0, cap};
    int lh = flex_ui_line_h(fs) + 4;
    flex_fk_textbox_layout(x, y, w, h, s, fs, flex_ui_line_h(fs), flex_ui_text_w, NULL, lines_emit, &L);
    lv_obj_t *lb = NULL;
    if (L.len > 0) {
        buf[L.len - 1] = '\0';   // sin el ultimo salto
        const lv_font_t *f = flex_ui_font(fs);
        lb = flex_kit_text(parent, buf, fs, color, x, y, false);
        lv_label_set_long_mode(lb, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(lb, w);
        lv_obj_set_style_text_line_space(lb, lh - (int)f->line_height, 0);
    }
    free(buf);
    return lb;
}

// ===== pantalla "sin almacenamiento" =====================================================================================
typedef struct {
    hdr_t h;
    flex_fk_exit_cb_t cb;
    void *user;
} nofs_t;

static void nofs_click(lv_event_t *e)
{
    nofs_t *c = lv_event_get_user_data(e);
    int x, y;
    tap_point(c->h.layer, &x, &y);
    if (x < 60 && y < 60 && !c->h.closing) {
        flex_fk_exit_cb_t cb = c->cb;
        void *u = c->user;
        layer_close(&c->h);
        if (cb) {
            cb(u);
        }
    }
}

lv_obj_t *flex_fk_nofs_screen(lv_obj_t *root, const char *title, flex_fk_exit_cb_t on_back, void *user)
{
    nofs_t *c;
    lv_obj_t *l = layer_new(root, K_NOFS, sizeof(*c), (void **)&c);
    if (!l) {
        return NULL;
    }
    lv_obj_add_event_cb(l, ctx_free_cb, LV_EVENT_DELETE, c);
    c->cb = on_back;
    c->user = user;
    const flex_palette_t *t = flex_th();
    fill(l, t->page);
    lv_obj_add_event_cb(l, nofs_click, LV_EVENT_CLICKED, c);
    chevron(l, 24, 18);
    flex_kit_text(l, title ? title : "", 3, t->txt, SCR_W / 2, 40, true);
    flex_kit_text(l, "Sin almacenamiento", 3, t->err, SCR_W / 2, 300, true);
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    char why[96];
    snprintf(why, sizeof(why), "no se pudo montar la particion de datos (%s)", st.fs_err ? esp_err_to_name(st.fs_err) : "sin detalle");
    lv_obj_t *w = flex_kit_text(l, why, 1, t->txt2, SCR_W / 2, 344, true);
    center_clip(w, SCR_W / 2, SCR_W - 32);
    // Arduino aconsejaba su Partition Scheme; aqui la tabla es fija (docs/PARTICIONES.md): la reparacion se pide en Ajustes
    flex_kit_text(l, "Reinstala el firmware o repara el almacenamiento en Ajustes", 1, t->mute, SCR_W / 2, 380, true);
    return l;
}

// ===== papelera ==========================================================================================================
#define TR_VIEW_Y 108
#define TR_VIEW_H 562
#define TR_ROW0   (FLEX_FK_TRASH_TOP - TR_VIEW_Y)   // y de la primera fila dentro de la lista
#define TR_BAR_Y  (SCR_H - 122)
#define TR_WIN    4                                  // filas fuera de la vista que se mantienen creadas

typedef struct {
    hdr_t h;
    uint32_t gen;
    flex_fk_exit_cb_t on_exit;
    void *user;
    lv_obj_t *root;
    flex_fs_ent_t *ents;
    int n;
    uint32_t total, bytes;
    int sel;
    bool ready, busy;
    int ask_mode;   // 0 ninguno, 1 borrar el elegido, 2 vaciar
    lv_obj_t *view, *content, *sub, *empty, *bar;
    int first, last;   // ventana de filas creadas [first, last)
} trash_t;

static trash_t *s_trash;
static uint32_t s_gen;

static void *big_alloc(size_t n)
{
#ifndef FLEX_SIM
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // la lista no es para la RAM interna
    if (p) {
        return p;
    }
#endif
    return malloc(n);
}

static void trash_free_cb(lv_event_t *e)
{
    trash_t *c = lv_event_get_user_data(e);
    if (s_trash == c) {
        s_trash = NULL;
    }
    free(c->ents);
    free(c);
}

static int ent_cmp(const void *a, const void *b)
{
    const flex_fs_ent_t *x = a, *y = b;
    if (x->dir != y->dir) {
        return x->dir ? -1 : 1;   // carpetas primero, luego por nombre sin distinguir mayusculas (cmpEntry)
    }
    return strcasecmp(x->name, y->name);
}

// Nombre original (ultimo segmento de la ruta decodificada) y ruta original de una entrada.
static void entry_origin(const flex_fs_ent_t *e, char *origin, size_t on, char *name, size_t nn)
{
    if (!flex_fsn_trash_origin(e->name, origin, on)) {
        snprintf(origin, on, "%s", e->name);
    }
    snprintf(name, nn, "%s", flex_fsn_base(origin));
}

static void trash_rows_sync(trash_t *c);
static void trash_bar(trash_t *c);

static void trash_row_click(lv_event_t *e)
{
    trash_t *c = lv_event_get_user_data(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (c->h.closing || !c->ready || c->busy || c->ask_mode || i < 0 || i >= c->n) {
        return;
    }
    c->sel = c->sel == i ? -1 : i;
    c->first = c->last = 0;
    lv_obj_clean(c->content);   // el color de seleccion cambia: se rehace la ventana
    trash_rows_sync(c);
    trash_bar(c);
}

static void trash_row_make(trash_t *c, int i)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *card = box(c->content, 16, TR_ROW0 + i * FLEX_FK_TRASH_RH, SCR_W - 32, FLEX_FK_TRASH_RH - 8, 14);
    if (i == c->sel) {
        fill(card, t->sel);
    } else {
        flex_surface(card, FLEX_SURF_CARD, FLEX_BD_FLAT);
    }
    lv_obj_set_clickable(card, true);
    lv_obj_set_user_data(card, (void *)(intptr_t)i);
    lv_obj_add_event_cb(card, trash_row_click, LV_EVENT_CLICKED, c);
    char origin[FLEX_FS_PATH_MAX], nm[FLEX_FS_NAME_MAX], sz[24], sub[FLEX_FS_PATH_MAX + 32];
    entry_origin(&c->ents[i], origin, sizeof(origin), nm, sizeof(nm));
    lv_obj_t *l1 = flex_kit_text(card, nm, 2, t->txt, 14, 8, false);
    flex_label_one_line(l1, SCR_W - 40 - 14);
    flex_fsn_fmt_size(c->ents[i].size, sz, sizeof(sz));
    snprintf(sub, sizeof(sub), "%s  \xC2\xB7  %s", origin, sz);
    lv_obj_t *l2 = flex_kit_text(card, sub, 1, t->txt2, 14, 34, false);
    flex_label_one_line(l2, SCR_W - 40 - 14);
}

// Mantiene creadas solo las filas de la vista y unas pocas mas: con 256 elementos no hay 700 objetos vivos.
static void trash_rows_sync(trash_t *c)
{
    if (!c->ready || c->h.closing) {
        return;
    }
    int sy = (int)lv_obj_get_scroll_y(c->view);
    int a = (sy - TR_ROW0) / FLEX_FK_TRASH_RH - TR_WIN;
    int b = (sy + TR_VIEW_H - TR_ROW0) / FLEX_FK_TRASH_RH + 1 + TR_WIN;
    if (a < 0) {
        a = 0;
    }
    if (b > c->n) {
        b = c->n;
    }
    if (c->last > c->first && a == c->first && b == c->last) {
        return;
    }
    lv_obj_clean(c->content);
    for (int i = a; i < b; i++) {
        trash_row_make(c, i);
    }
    c->first = a;
    c->last = b > a ? b : a;
}

static void trash_scroll_cb(lv_event_t *e)
{
    trash_rows_sync(lv_event_get_user_data(e));
}

static void trash_reload(trash_t *c);

static void trash_done_cb(esp_err_t err, void *user)
{
    (void)err;   // si falla, el elemento sigue en la lista: se vuelve a leer del disco
    if (!s_trash || s_trash->gen != (uint32_t)(uintptr_t)user) {
        return;
    }
    s_trash->busy = false;
    s_trash->sel = -1;
    trash_reload(s_trash);
}

static void trash_ask_cb(bool ok, void *user)
{
    if (!s_trash || s_trash->gen != (uint32_t)(uintptr_t)user) {
        return;
    }
    trash_t *c = s_trash;
    int mode = c->ask_mode;
    c->ask_mode = 0;
    if (!ok) {
        return;
    }
    c->busy = true;
    esp_err_t r = ESP_ERR_INVALID_STATE;
    if (mode == 1 && c->sel >= 0 && c->sel < c->n) {
        char p[FLEX_FS_PATH_MAX];
        snprintf(p, sizeof(p), FLEX_FSN_DIR_TRASH "/%s", c->ents[c->sel].name);
        r = flex_fs_delete_async(p, trash_done_cb, (void *)(uintptr_t)c->gen);   // definitivo
    } else if (mode == 2) {
        r = flex_fs_empty_trash_async(trash_done_cb, (void *)(uintptr_t)c->gen);
    }
    if (r != ESP_OK) {
        trash_done_cb(r, (void *)(uintptr_t)c->gen);   // no se pudo poner en cola: se relee y ya
    }
}

static void bar_restore_cb(lv_obj_t *b, void *user)
{
    (void)b;
    trash_t *c = user;
    if (c->busy || c->ask_mode || c->sel < 0 || c->sel >= c->n) {
        return;
    }
    c->busy = true;
    esp_err_t r = flex_fs_restore_async(c->ents[c->sel].name, trash_done_cb, (void *)(uintptr_t)c->gen);   // a su carpeta de verdad
    if (r != ESP_OK) {
        trash_done_cb(r, (void *)(uintptr_t)c->gen);
    }
}

static void ask_open_for(trash_t *c, int mode)
{
    if (c->busy || c->ask_mode) {
        return;
    }
    c->ask_mode = mode;
    if (mode == 1) {
        char origin[FLEX_FS_PATH_MAX], nm[FLEX_FS_NAME_MAX], stem[FLEX_FS_NAME_MAX];
        entry_origin(&c->ents[c->sel], origin, sizeof(origin), nm, sizeof(nm));
        flex_fsn_stem(nm, stem, sizeof(stem));
        flex_fk_ask_open(c->root, "\xC2\xBF" "Borrar definitivamente?", stem, trash_ask_cb, (void *)(uintptr_t)c->gen);
    } else {
        flex_fk_ask_open(c->root, "\xC2\xBF" "Vaciar la papelera?", "Se borrar\xC3\xA1 todo su contenido", trash_ask_cb,
                         (void *)(uintptr_t)c->gen);
    }
}

static void bar_delete_cb(lv_obj_t *b, void *user)
{
    (void)b;
    trash_t *c = user;
    if (c->sel >= 0 && c->sel < c->n) {
        ask_open_for(c, 1);
    }
}

static void bar_empty_cb(lv_obj_t *b, void *user)
{
    (void)b;
    trash_t *c = user;
    if (c->n > 0) {
        ask_open_for(c, 2);
    }
}

// Barra inferior: Restaurar / Borrar con una fila elegida; "Vaciar papelera" sin eleccion y con elementos.
static void trash_bar(trash_t *c)
{
    const flex_palette_t *t = flex_th();
    lv_obj_clean(c->bar);
    if (c->sel >= 0) {
        int bw = (SCR_W - 48) / 2;
        flex_kit_button(c->bar, 16, 0, bw, 56, "Restaurar", FLEX_BTN_NORMAL, bar_restore_cb, c);
        flex_kit_button(c->bar, 32 + bw, 0, bw, 56, "Borrar", FLEX_BTN_DANGER, bar_delete_cb, c);
        // colores propios del kit de archivos: Restaurar verde con texto sobre acento, Borrar rojo relleno
        lv_obj_t *b1 = lv_obj_get_child(c->bar, 0), *b2 = lv_obj_get_child(c->bar, 1);
        lv_obj_set_style_bg_color(b1, t->ok, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b1, 0), t->on_acc, 0);
        lv_obj_set_style_bg_color(b2, t->danger, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b2, 0), t->on_acc, 0);
    } else if (c->n > 0) {
        flex_kit_button(c->bar, SCR_W / 2 - 120, 0, 240, 56, "Vaciar papelera", FLEX_BTN_DANGER, bar_empty_cb, c);
    }
}

// Pinta cabecera, lista y barra inferior con lo que hay cargado.
static void trash_render(trash_t *c)
{
    char sz[24], hdr[96];
    flex_fsn_fmt_size(c->bytes, sz, sizeof(sz));
    if (c->total > (uint32_t)c->n) {
        snprintf(hdr, sizeof(hdr), "%u elementos (se ven %d)  \xC2\xB7  %s", (unsigned)c->total, c->n, sz);
    } else {
        snprintf(hdr, sizeof(hdr), "%d elementos  \xC2\xB7  %s", c->n, sz);
    }
    lv_label_set_text(c->sub, hdr);
    flex_label_cap_center(c->sub, SCR_W / 2, 86);
    lv_obj_set_hidden(c->empty, c->n != 0);
    lv_obj_set_size(c->content, SCR_W, TR_VIEW_H + flex_fk_trash_max_scroll(c->n));
    c->first = c->last = 0;
    lv_obj_clean(c->content);
    c->ready = true;
    c->busy = false;   // el recuento ya llego: se puede actuar
    trash_rows_sync(c);

    trash_bar(c);
}

static void trash_stat_cb(esp_err_t err, uint32_t count, uint32_t bytes, void *user)
{
    if (!s_trash || s_trash->gen != (uint32_t)(uintptr_t)user) {
        return;
    }
    trash_t *c = s_trash;
    c->total = err == ESP_OK ? count : (uint32_t)c->n;
    if (c->total < (uint32_t)c->n) {
        c->total = (uint32_t)c->n;
    }
    c->bytes = err == ESP_OK ? bytes : 0;
    trash_render(c);
}

static void trash_list_cb(esp_err_t err, const flex_fs_ent_t *ents, size_t n, void *user)
{
    if (!s_trash || s_trash->gen != (uint32_t)(uintptr_t)user) {
        return;   // la papelera se cerro (o se abrio otra) mientras el disco contestaba
    }
    trash_t *c = s_trash;
    free(c->ents);
    c->ents = NULL;
    c->n = 0;
    if (err == ESP_OK && n > 0) {
        c->ents = big_alloc(n * sizeof(*c->ents));
        if (c->ents) {
            memcpy(c->ents, ents, n * sizeof(*c->ents));
            c->n = (int)n;
            qsort(c->ents, n, sizeof(*c->ents), ent_cmp);
        }
    }
    if (c->sel >= c->n) {
        c->sel = -1;
    }
    if (flex_fs_dirstat_async(FLEX_FSN_DIR_TRASH, trash_stat_cb, (void *)(uintptr_t)c->gen) != ESP_OK) {
        trash_stat_cb(ESP_FAIL, 0, 0, (void *)(uintptr_t)c->gen);
    }
}

static void trash_reload(trash_t *c)
{
    c->busy = true;   // hasta que llegue el recuento
    if (flex_fs_list_async(FLEX_FSN_DIR_TRASH, FLEX_FK_TRASH_MAX, trash_list_cb, (void *)(uintptr_t)c->gen) != ESP_OK) {
        trash_list_cb(ESP_FAIL, NULL, 0, (void *)(uintptr_t)c->gen);
    }
}

static void trash_click(lv_event_t *e)
{
    trash_t *c = lv_event_get_user_data(e);
    int x, y;
    tap_point(c->h.layer, &x, &y);
    if (x < 60 && y < 60 && !c->h.closing && !c->ask_mode) {   // volver (y suelta la lista)
        flex_fk_exit_cb_t cb = c->on_exit;
        void *u = c->user;
        layer_close(&c->h);
        if (cb) {
            cb(u);
        }
    }
}

lv_obj_t *flex_fk_trash_open(lv_obj_t *root, flex_fk_exit_cb_t on_exit, void *user)
{
    if (s_trash) {
        return s_trash->h.layer;   // una a la vez
    }
    trash_t *c;
    lv_obj_t *l = layer_new(root, K_TRASH, sizeof(*c), (void **)&c);
    if (!l) {
        return NULL;
    }
    lv_obj_add_event_cb(l, trash_free_cb, LV_EVENT_DELETE, c);
    c->on_exit = on_exit;
    c->user = user;
    c->root = root;
    c->sel = -1;
    c->gen = ++s_gen;
    s_trash = c;
    const flex_palette_t *t = flex_th();
    fill(l, t->page);
    lv_obj_add_event_cb(l, trash_click, LV_EVENT_CLICKED, c);
    chevron(l, 24, 18);
    flex_kit_text(l, "Papelera", 4, t->txt, SCR_W / 2, 34, true);
    c->sub = flex_kit_text(l, "", 1, t->txt2, SCR_W / 2, 86, true);
    c->empty = flex_kit_text(l, "La papelera est\xC3\xA1 vac\xC3\xAD" "a", 2, t->txt2, SCR_W / 2, 320, true);
    lv_obj_set_hidden(c->empty, true);
    c->view = box(l, 0, TR_VIEW_Y, SCR_W, TR_VIEW_H, 0);
    lv_obj_set_clickable(c->view, true);
    lv_obj_set_scrollable(c->view, true);
    lv_obj_set_scroll_dir(c->view, LV_DIR_VER);
    lv_obj_set_scroll_elastic(c->view, false);
    lv_obj_set_scroll_momentum(c->view, false);
    lv_obj_set_scrollbar_mode(c->view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(c->view, trash_scroll_cb, LV_EVENT_SCROLL, c);
    c->content = box(c->view, 0, 0, SCR_W, TR_VIEW_H, 0);
    c->bar = box(l, 0, TR_BAR_Y, SCR_W, 56, 0);
    trash_reload(c);
    return l;
}

int flex_fk_trash_count(void)
{
    return s_trash ? s_trash->n : -1;
}

int flex_fk_trash_selected(void)
{
    return s_trash ? s_trash->sel : -1;
}

bool flex_fk_trash_ready(void)
{
    return s_trash && s_trash->ready && !s_trash->busy;
}
