#include "flex_shell.h"

#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_auth.h"
#include "flex_clock.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_home_model.h"
#include "flex_storage.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"

// Transiciones (AppFramework.h:821-961)
#define ATR_OPEN_MS   210
#define ATR_CLOSE_MS  190
#define ATR_MIN_MS    60
#define ATR_RAD_SMALL 26
#define ATR_RAD_FULL  4

typedef enum { APP_CLOSED = 0, APP_RUNNING, APP_SUSPENDED } app_life_t;

typedef struct {
    lv_obj_t *root;
    app_life_t life;
    uint32_t last_used;
} app_inst_t;

static lv_obj_t *s_scr, *s_home, *s_apps, *s_lock;
// Bajo el bloqueo con clave: nada de lo de debajo se puede tocar aunque el
// bloqueo no lo tape entero (durante su caida, por ejemplo).
static lv_obj_t *s_shield;
static app_inst_t s_inst[FLEX_APP_N];
static int s_fg = -1;                     // app en primer plano (estado logico)
static flex_shell_state_t s_state = FLEX_SH_LOCK;
static int32_t s_last_minute = -1;

// ---- tarjeta de transicion ------------------------------------------------------
static struct {
    lv_obj_t *card;
    lv_area_t from, to;
    bool opening;
    int app;
    uint32_t gen;
    int32_t prog;          // 0..1024 del recorrido visual actual
} s_tr;

static int32_t ease_out_cubic(const lv_anim_t *a)
{
    // 1 - (1 - u)^3 sobre el tramo restante, por tiempo
    uint32_t t = a->act_time < 0 ? 0 : (uint32_t)a->act_time;
    uint32_t d = a->duration ? a->duration : 1;
    float p = (float)(t > d ? d : t) / (float)d;
    float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
    return a->start_value + (int32_t)((float)(a->end_value - a->start_value) * e);
}

static lv_color_t app_bg(int id)
{
    (void)id;
    return flex_th()->win;
}

static void card_apply(int32_t prog)
{
    s_tr.prog = prog;
    float p = prog / 1024.0f;
    uint8_t style = flex_look()->anim_style;
    lv_area_t a;
    if (style == 1) {   // fundido: siempre a pantalla completa, la opacidad es la animacion
        a = (lv_area_t){0, 0, 479, 799};
        lv_obj_set_style_bg_opa(s_tr.card, (lv_opa_t)(255 * p), 0);
        lv_obj_set_style_radius(s_tr.card, 0, 0);
    } else if (style == 2) {   // deslizar: sube desde abajo, sin radio
        int32_t y0 = (int32_t)(800 * (1.0f - p));
        a = (lv_area_t){0, y0, 479, y0 + 799};
        lv_obj_set_style_radius(s_tr.card, 0, 0);
        lv_obj_set_style_bg_opa(s_tr.card, LV_OPA_COVER, 0);
    } else {   // zoom: del rectangulo del icono a la pantalla entera
        a.x1 = s_tr.from.x1 + (int32_t)((s_tr.to.x1 - s_tr.from.x1) * p);
        a.y1 = s_tr.from.y1 + (int32_t)((s_tr.to.y1 - s_tr.from.y1) * p);
        a.x2 = s_tr.from.x2 + (int32_t)((s_tr.to.x2 - s_tr.from.x2) * p);
        a.y2 = s_tr.from.y2 + (int32_t)((s_tr.to.y2 - s_tr.from.y2) * p);
        lv_obj_set_style_radius(s_tr.card, ATR_RAD_SMALL + (int32_t)((ATR_RAD_FULL - ATR_RAD_SMALL) * p), 0);
        lv_obj_set_style_bg_opa(s_tr.card, LV_OPA_COVER, 0);
    }
    lv_obj_set_pos(s_tr.card, a.x1, a.y1);
    lv_obj_set_size(s_tr.card, lv_area_get_width(&a), lv_area_get_height(&a));
}

static void card_exec(void *obj, int32_t v)
{
    (void)obj;
    // apertura: 0 -> 1024; cierre: 1024 -> 0 (el rectangulo se interpreta igual)
    card_apply(v);
}

static void show_app_root(int id);

static void card_done(lv_anim_t *a)
{
    uint32_t gen = (uint32_t)(uintptr_t)lv_anim_get_user_data(a);
    if (gen != s_tr.gen) {
        return;   // una intencion mas nueva ya mando sobre esta capa
    }
    if (s_tr.opening && s_fg == s_tr.app && s_state == FLEX_SH_APP) {
        show_app_root(s_tr.app);   // enter()/resume() en el ultimo cuadro de la apertura
    }
    lv_obj_set_hidden(s_tr.card, true);
}

static void card_run(bool opening, int app, const lv_area_t *icon)
{
    s_tr.gen++;
    lv_anim_delete(s_tr.card, card_exec);
    int32_t from_prog = lv_obj_is_hidden(s_tr.card) ? (opening ? 0 : 1024) : s_tr.prog;
    s_tr.opening = opening;
    s_tr.app = app;
    if (icon) {
        s_tr.from = *icon;
    } else {
        s_tr.from = (lv_area_t){208, 368, 271, 431};   // sin icono visible: el centro
    }
    s_tr.to = (lv_area_t){0, 0, 479, 799};
    lv_obj_set_style_bg_color(s_tr.card, app_bg(app), 0);
    lv_obj_set_hidden(s_tr.card, false);
    lv_obj_move_foreground(s_tr.card);
    int32_t target = opening ? 1024 : 0;
    uint32_t base = opening ? ATR_OPEN_MS : ATR_CLOSE_MS;
    // re-dirigida: parte del progreso visual actual y dura lo que le queda
    uint32_t dur = (uint32_t)(base * (float)LV_ABS(target - from_prog) / 1024.0f);
    if (dur < ATR_MIN_MS) {
        dur = ATR_MIN_MS;
    }
    card_apply(from_prog);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_tr.card);
    lv_anim_set_exec_cb(&a, card_exec);
    lv_anim_set_values(&a, from_prog, target);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, ease_out_cubic);
    lv_anim_set_completed_cb(&a, card_done);
    lv_anim_set_user_data(&a, (void *)(uintptr_t)s_tr.gen);
    lv_anim_start(&a);
}

// ---- apps --------------------------------------------------------------------------
static void header_back_cb(lv_event_t *e)
{
    (void)e;
    flex_sys_back();
}

lv_obj_t *flex_app_header(lv_obj_t *parent, const char *title, bool menu, lv_event_cb_t on_menu)
{
    // uiHdr (AppFramework.h:378): zonas 56x56, titulo desde x=64 que se reduce
    // y si aun asi no cabe se recorta; nunca invade un boton.
    const flex_palette_t *t = flex_th();
    lv_obj_t *h = flex_box(parent);
    lv_obj_set_size(h, 480, 76);
    lv_obj_t *back = flex_box(h);
    lv_obj_set_size(back, 56, 56);
    lv_obj_set_clickable(back, true);
    lv_obj_add_event_cb(back, header_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *chev = flex_glyph_create(back, FLEX_GLYPH_CHEVRON_LEFT, 56, 56);
    lv_obj_set_style_text_color(chev, t->nav, 0);
    if (menu) {
        lv_obj_t *mz = flex_box(h);
        lv_obj_set_size(mz, 56, 56);
        lv_obj_set_x(mz, 480 - 56);
        lv_obj_set_clickable(mz, true);
        if (on_menu) {
            lv_obj_add_event_cb(mz, on_menu, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_t *dots = flex_glyph_create(mz, FLEX_GLYPH_MENU, 56, 56);
        lv_obj_set_style_text_color(dots, t->nav, 0);
    }
    if (title && title[0]) {
        int32_t avail = (menu ? 480 - 56 - 8 : 480 - 8) - 64;
        static const lv_font_t *const sizes[] = {FLEX_FONT_S3, FLEX_FONT_S2, FLEX_FONT_S1};
        const lv_font_t *f = sizes[0];
        for (int i = 0; i < 3; i++) {
            f = sizes[i];
            lv_point_t sz;
            lv_text_get_size(&sz, title, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (sz.x <= avail) {
                break;
            }
        }
        lv_obj_t *l = flex_label(h, title, f, t->txt);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(l, avail);
        lv_obj_set_x(l, 64);
        lv_obj_set_y(l, 28 - lv_font_get_line_height(f) / 2);
    }
    return h;
}

void flex_app_placeholder(lv_obj_t *root, const lv_area_t *area, int id)
{
    // appPlaceholderEnter (AppFramework.h:480)
    const flex_palette_t *t = flex_th();
    lv_obj_t *panel = flex_box(root);
    lv_obj_set_pos(panel, 36, 248);
    lv_obj_set_size(panel, 408, 268);
    lv_obj_set_style_radius(panel, 26, 0);
    flex_surface(panel, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    lv_obj_t *ic = flex_app_icon_create(root, id, 88, FLEX_BD_FLAT);
    lv_obj_set_pos(ic, 196, 286);
    lv_obj_t *l1 = flex_label(root, flex_t(FLEX_S_SOON), FLEX_FONT_S3, t->txt);
    flex_label_cap_center(l1, 240, 422);
    lv_obj_t *l2 = flex_label(root, "Pendiente de migrar a ESP-IDF", FLEX_FONT_S2, t->txt2);
    flex_label_cap_center(l2, 240, 464);
    (void)area;
}

static void std_header_back_cb(lv_event_t *e)
{
    (void)e;
    flex_sys_back();
}

static void build_app(int id)
{
    const flex_app_def_t *d = flex_app_def(id);
    app_inst_t *in = &s_inst[id];
    lv_obj_t *r = flex_box(s_apps);
    lv_obj_set_size(r, 480, 800);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, flex_th()->win, 0);
    in->root = r;
    bool custom = d->flags & FLEX_APP_CUSTOM_HEADER;
    int32_t bot = flex_look()->nav_mode == 1 ? 800 : FLEX_WIN_BOT;
    lv_area_t area = {0, custom ? 0 : FLEX_WIN_TOP, 479, bot - 1};
    if (!custom) {
        flex_statusbar_create(r, FLEX_SB_APP);
        // cabecera estandar: chevron (18..30, 50..66) + nombre centrado, tamano 3
        lv_obj_t *hz = flex_box(r);
        lv_obj_set_pos(hz, 0, 46);
        lv_obj_set_size(hz, 72, 50);
        lv_obj_set_clickable(hz, true);
        lv_obj_add_event_cb(hz, std_header_back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *chev = flex_glyph_create(hz, FLEX_GLYPH_CHEVRON_LEFT, 24, 24);
        lv_obj_set_pos(chev, 12, 46);
        lv_obj_set_y(chev, 58 - 46 - 12);
        lv_obj_set_style_text_color(chev, flex_th()->txt, 0);
        lv_obj_t *title = flex_label(r, flex_app_name(id), FLEX_FONT_S3, flex_th()->txt);
        flex_label_cap_center(title, 240, 53);
    }
    lv_obj_t *content = flex_box(r);
    lv_obj_set_pos(content, 0, area.y1);
    lv_obj_set_size(content, 480, lv_area_get_height(&area));
    if (d->ops && d->ops->on_create) {
        lv_area_t local = {0, 0, 479, lv_area_get_height(&area) - 1};
        d->ops->on_create(content, &local);
    } else {
        lv_obj_set_pos(content, 0, 0);
        lv_obj_set_size(content, 480, 800);
        flex_app_placeholder(content, &area, id);
    }
    lv_obj_set_hidden(r, true);
    in->life = APP_SUSPENDED;
}

static void show_app_root(int id)
{
    app_inst_t *in = &s_inst[id];
    const flex_app_def_t *d = flex_app_def(id);
    bool was_closed = in->root == NULL;
    if (was_closed) {
        build_app(id);
    }
    for (int i = 0; i < FLEX_APP_N; i++) {
        if (s_inst[i].root) {
            lv_obj_set_hidden(s_inst[i].root, i != id);
        }
    }
    lv_obj_set_hidden(s_apps, false);
    lv_obj_set_hidden(s_home, true);
    in->life = APP_RUNNING;
    in->last_used = lv_tick_get();
    if (!was_closed && d->ops && d->ops->on_resume) {
        d->ops->on_resume();
    }
}

static void suspend_fg(void)
{
    if (s_fg < 0) {
        return;
    }
    app_inst_t *in = &s_inst[s_fg];
    const flex_app_def_t *d = flex_app_def(s_fg);
    if (in->root) {
        // tarjeta de Recientes + miniatura, ANTES de ocultarla (las horizontales sin
        // miniatura: saldria girada)
        flex_recents_note_suspend(s_fg, in->root, !(d->flags & FLEX_APP_LAND));
        lv_obj_set_hidden(in->root, true);
        in->life = APP_SUSPENDED;
        if (d->ops && d->ops->on_suspend) {
            d->ops->on_suspend();
        }
    }
    s_fg = -1;
}

void flex_app_open(int id, const lv_area_t *from_icon)
{
    if (!flex_app_def(id)) {
        return;
    }
    lv_area_t icon;
    if (!from_icon && flex_home_icon_area(id, &icon)) {
        from_icon = &icon;
    }
    if (s_fg >= 0 && s_fg != id) {
        suspend_fg();
    }
    flex_qs_close_now();
    flex_notif_center_close_now();
    s_fg = id;
    s_state = FLEX_SH_APP;
    flex_navbar_set_ctx(FLEX_NAV_APP);
    flex_touch_drop_all();   // touchDropAll: el dedo que abrio no toca la app
    card_run(true, id, from_icon);
}

void flex_app_close(void)
{
    flex_qs_close_now();
    flex_notif_center_close_now();
    int id = s_fg;
    if (id < 0) {
        flex_shell_show_home();
        return;
    }
    lv_area_t icon;
    bool has_icon = flex_home_icon_area(id, &icon);
    suspend_fg();
    flex_shell_show_home();
    card_run(false, id, has_icon ? &icon : NULL);
}

static const flex_overlay_ops_t *s_overlay;

void flex_shell_overlay_begin(const flex_overlay_ops_t *ops)
{
    s_overlay = ops;
    s_state = FLEX_SH_OVERLAY;
}

void flex_shell_overlay_end(void)
{
    s_overlay = NULL;
    if (s_state == FLEX_SH_OVERLAY) {
        flex_shell_show_home();
    }
}

// ---- abrir con candado ----------------------------------------------------------------
static lv_area_t s_launch_from;
static bool s_launch_has_from;

static void launch_ok(void *ctx)
{
    // lsuFinishAfter(LSU_AFTER_OPENAPP): escritorio y la app con su animacion
    flex_shell_show_home();
    flex_app_open((int)(intptr_t)ctx, s_launch_has_from ? &s_launch_from : NULL);
}

static void launch_cancel(void *ctx)
{
    (void)ctx;
    flex_shell_show_home();   // la verificacion no salio del bloqueo: se queda en Inicio
}

void flex_app_launch(int id, const lv_area_t *from_icon)
{
    if (!flex_app_def(id) || s_state == FLEX_SH_LOCK || s_state == FLEX_SH_AUTH || s_state == FLEX_SH_POWEROFF) {
        return;   // bloqueado, tecleando una clave o apagando: no se abre nada
    }
    bool locked = id >= 0 && id < 32 && ((g_home.lock >> id) & 1u);
    if (locked && flex_auth_required()) {
        s_launch_has_from = from_icon != NULL;
        if (from_icon) {
            s_launch_from = *from_icon;
        }
        flex_auth_req_t r = {.on_ok = launch_ok, .on_cancel = launch_cancel, .ctx = (void *)(intptr_t)id};
        flex_auth_verify(&r);
        return;
    }
    flex_app_open(id, from_icon);
}

void flex_sys_back(void)
{
    if (s_state == FLEX_SH_OVERLAY && s_overlay && s_overlay->on_back) {
        s_overlay->on_back();
        return;
    }
    if (s_state != FLEX_SH_APP || s_fg < 0) {
        return;   // POWEROFF: sin gesto de atras (Cancelar es la salida)
    }
    const flex_app_def_t *d = flex_app_def(s_fg);
    if (d->ops && d->ops->on_back && d->ops->on_back()) {
        return;   // la app cerro una capa o retrocedio una pantalla
    }
    flex_app_close();
}

void flex_sys_home(void)
{
    if (s_state == FLEX_SH_OVERLAY && s_overlay && s_overlay->on_home) {
        s_overlay->on_home();
        return;
    }
    if (s_state == FLEX_SH_APP) {
        flex_app_close();
    } else if (s_state != FLEX_SH_LOCK && s_state != FLEX_SH_AUTH && s_state != FLEX_SH_POWEROFF) {
        flex_shell_show_home();
    }
}

void flex_sys_recents(void)
{
    if (s_state == FLEX_SH_LOCK || s_state == FLEX_SH_AUTH || s_state == FLEX_SH_POWEROFF) {
        return;
    }
    if (s_state == FLEX_SH_OVERLAY && s_overlay && s_overlay->on_recents) {
        s_overlay->on_recents();
        return;
    }
    flex_qs_close_now();
    flex_notif_center_close_now();
    if (s_state == FLEX_SH_APP) {
        suspend_fg();
        flex_shell_show_home();
    }
    flex_recents_open();
}

int flex_app_current(void)
{
    return s_state == FLEX_SH_APP ? s_fg : -1;
}

bool flex_app_is_open(int id)
{
    return id >= 0 && id < FLEX_APP_N && s_inst[id].root != NULL;
}

void flex_app_terminate(int id)
{
    if (!flex_app_is_open(id)) {
        return;
    }
    const flex_app_def_t *d = flex_app_def(id);
    if (id == s_fg) {
        suspend_fg();
        flex_shell_show_home();
    }
    if (d->ops && d->ops->on_close) {
        d->ops->on_close();
    }
    lv_obj_delete(s_inst[id].root);
    s_inst[id].root = NULL;
    s_inst[id].life = APP_CLOSED;
    flex_recents_note_closed(id);
}

// ---- estados del shell -------------------------------------------------------------
flex_shell_state_t flex_shell_state(void)
{
    return s_state;
}

lv_obj_t *flex_shell_screen(void)
{
    return s_scr;
}

void flex_shell_show_home(void)
{
    flex_qs_close_now();
    flex_notif_center_close_now();
    lv_obj_set_hidden(s_shield, true);
    s_state = FLEX_SH_HOME;
    lv_obj_set_hidden(s_home, false);
    lv_obj_set_hidden(s_apps, true);
    flex_navbar_set_ctx(FLEX_NAV_HOME);
}

void flex_shell_lock(void)
{
    if (flex_poweroff_running()) {
        return;   // la animacion de apagado no tiene vuelta (y nada deberia pedirlo)
    }
    flex_auth_abort();   // una clave a medias no sobrevive a bloquear
    flex_poweroff_close_now();
    flex_qs_close_now();
    flex_notif_center_close_now();
    flex_drawer_close_now();
    flex_recents_close_now();
    flex_lock_set_return_app(-1);
    suspend_fg();
    flex_lock_reset();
    lv_obj_set_hidden(s_shield, !flex_auth_required());
    lv_obj_move_foreground(s_shield);
    lv_obj_set_hidden(s_lock, false);
    lv_obj_move_foreground(s_lock);
    s_state = FLEX_SH_LOCK;
    flex_navbar_set_ctx(FLEX_NAV_HIDDEN);
}

void flex_shell_unlocked(void)
{
    lv_obj_set_hidden(s_shield, true);
    lv_obj_set_hidden(s_lock, true);
    flex_shell_show_home();
}

void flex_shell_auth_begin(void)
{
    s_state = FLEX_SH_AUTH;
    flex_navbar_set_ctx(FLEX_NAV_HIDDEN);
}

static flex_shell_state_t s_poff_prev = FLEX_SH_HOME;

void flex_shell_poweroff_begin(void)
{
    if (s_state == FLEX_SH_HOME || s_state == FLEX_SH_APP) {
        s_poff_prev = s_state;   // AUTH (la clave del apagado) no: se vuelve a lo de antes
    }
    s_state = FLEX_SH_POWEROFF;
    flex_navbar_set_ctx(FLEX_NAV_HIDDEN);
}

void flex_shell_poweroff_end(void)
{
    if (s_state != FLEX_SH_POWEROFF && s_state != FLEX_SH_AUTH) {
        return;
    }
    if (s_poff_prev == FLEX_SH_APP && s_fg >= 0) {
        s_state = FLEX_SH_APP;   // la app sigue delante (Arduino volvia a Inicio con la app viva)
        flex_navbar_set_ctx(FLEX_NAV_APP);
    } else {
        flex_shell_show_home();
    }
}

void flex_shell_reveal_prepare(void)
{
    lv_obj_set_hidden(s_shield, true);
    lv_obj_set_hidden(s_lock, true);
    lv_obj_set_hidden(s_apps, true);
    lv_obj_set_hidden(s_home, false);
}

void flex_shell_home_shift(int32_t dx)
{
    lv_obj_set_style_translate_x(s_home, dx, 0);
}

static void minute_tick(lv_timer_t *t)
{
    (void)t;
    int32_t m = flex_clock_minute();
    if (m == s_last_minute) {
        return;
    }
    s_last_minute = m;
    flex_statusbar_refresh_all();
    flex_lock_refresh();
    flex_home_refresh();
    if (s_fg >= 0) {
        const flex_app_def_t *d = flex_app_def(s_fg);
        if (d->ops && d->ops->on_minute) {
            d->ops->on_minute();
        }
    }
}

static void theme_cb(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < FLEX_APP_N; i++) {
        if (s_inst[i].root) {
            lv_obj_set_style_bg_color(s_inst[i].root, flex_th()->win, 0);
        }
    }
    flex_statusbar_refresh_all();
}

void flex_shell_start(void)
{
    flex_clock_init();
    flex_home_set_registry(FLEX_APP_N, flex_app_factory_fav());
    flex_home_load();

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_scrollable(s_scr, false);

    s_home = flex_home_create(s_scr);
    s_apps = flex_box(s_scr);
    lv_obj_set_size(s_apps, 480, 800);
    lv_obj_set_hidden(s_apps, true);
    s_shield = flex_box(s_scr);
    lv_obj_set_size(s_shield, 480, 800);
    lv_obj_set_clickable(s_shield, true);   // se traga los toques, no hace nada con ellos
    lv_obj_set_hidden(s_shield, true);
    s_lock = flex_lock_create(s_scr);

    flex_navbar_init();
    s_tr.card = flex_box(lv_layer_top());
    lv_obj_set_hidden(s_tr.card, true);
    lv_obj_set_clickable(s_tr.card, false);

    flex_theme_listen(theme_cb, NULL);
    s_last_minute = flex_clock_minute();
    lv_timer_create(minute_tick, 1000, NULL);
    lv_screen_load(s_scr);
    flex_power_init();
    flex_qs_init();
    flex_notif_init();
    flex_shell_lock();
}

bool flex_shell_transition_active(void)
{
    return s_tr.card && !lv_obj_is_hidden(s_tr.card);
}

uint32_t flex_app_last_used(int id)
{
    return id >= 0 && id < FLEX_APP_N ? s_inst[id].last_used : 0;
}
