// Flex OS Ultra · App Ajustes (docs/spec/03b §11; Arduino: FlexOS_Ultra_AppSettings.h).
//
// Dos pantallas: la lista de 12 categorias y la de una categoria (filas con su valor y, al tocarlas, su accion: ciclar un valor, abrir una
// pantalla del sistema). Toda la logica sin graficos (que filas hay, donde, que valor muestran, que hace cada toque, la sesion y las curvas de la
// transicion) esta en flex_set_model.c y se prueba frente a Arduino; aqui solo hay LVGL: cada operacion de la lista que entrega el modelo se
// convierte en un objeto, el desplazamiento es el de LVGL (sin inercia, como el original) y la transicion mueve las dos paginas con el paralaje del 28 %.
//
// Cada fila escribe SOLO su clave (flex_theme_set_*, flex_set_lang, flex_cfg_set_*), no las 30 de cfgSavePrefs. Los servicios de otras areas aun sin
// migrar (hora por red, actualizaciones, cuenta, Wi-Fi, proteccion contra robo, audio) son simbolos debiles: sin ellos la fila dice que no esta
// disponible y tocarla no abre nada.
#include <stdio.h>
#include <string.h>
#ifndef FLEX_SIM
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif
#include "flex_app.h"
#include "flex_auth.h"
#include "flex_clock.h"
#include "flex_display.h"
#include "flex_frame.h"
#include "flex_fs_names.h"
#include "flex_glass.h"
#include "flex_i18n.h"
#include "flex_icons.h"
#include "flex_kb_layout.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_set_model.h"
#include "flex_set_ui.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "widgets/flex_app_ids.h"

// ---- servicios de otras areas (simbolos debiles) -------------------------------------------------------------------------------
extern void flex_ntp_texts(char *state, size_t ns, char *last, size_t nl, bool *busy) __attribute__((weak));
extern void flex_ntp_request_sync(bool force) __attribute__((weak));
extern const char *flex_ota_status_text(void) __attribute__((weak));
extern void flex_ota_open_settings(void) __attribute__((weak));
extern void flex_account_settings_text(char *out, size_t n) __attribute__((weak));
extern void flex_account_settings_enter(void) __attribute__((weak));
extern void flex_conn_wifi_sub(char *out, size_t n) __attribute__((weak));
extern void flex_conn_ble_sub(char *out, size_t n) __attribute__((weak));
extern void flex_conn_enter(void) __attribute__((weak));
extern void flex_wifi_settings_enter(void) __attribute__((weak));
extern void flex_ble_toggle(void) __attribute__((weak));
extern const char *flex_theft_row_value(void) __attribute__((weak));
extern void flex_theft_enter(void) __attribute__((weak));
extern bool flex_audio_available(void) __attribute__((weak));
extern int flex_audio_volume(void) __attribute__((weak));
extern bool flex_audio_muted(void) __attribute__((weak));
extern const char *flex_audio_error(void) __attribute__((weak));
extern void flex_kbsettings_open(void (*on_back)(void)) __attribute__((weak));

#define PAGE_Y 30   // la banda que se desplaza (Arduino: desde y = 40): 10 px mas arriba para que el titulo de tamano 4 no quede cortado por su linea de caja
#define PAGE_H (FLEX_SET_ANIM_Y1 - PAGE_Y)

typedef struct {
    lv_obj_t *root;
    bool created;
    int view, sel;                    // 0 lista, 1 categoria
    int list_scroll, scroll;          // posiciones guardadas (la pagina viva manda mientras existe)
    lv_obj_t *sbar;
    lv_obj_t *pl, *pd;                // paginas: lista y categoria (480 x 700 en y = 40)
    lv_obj_t *lview, *lcontent, *dview, *dcontent;
    lv_obj_t *catcard[FLEX_SET_NCAT];
    lv_obj_t *rowcard[FLEX_SET_ROW_MAX];
    int nrows;
    flex_set_sys_t sys;
    bool animating;
    int anim_dir;
    bool swipe;
    int sx, sy;
    char trash[16];
    flex_set_sess_t sess;
    bool sess_valid;
    bool suspended;
} set_t;

static set_t S;
static uint32_t s_gen;   // generacion de las respuestas de la tarea de almacenamiento
static flex_set_page_t PG;   // lo bastante grande para ir fuera de la pila

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

static void mark_dirty(void)
{
    flex_session_mark_dirty(IC_AJUSTES);
}

// ===== lo que las filas leen del sistema =======================================================================================================
static void sys_fill(void)
{
    flex_set_sys_t *s = &S.sys;
    memset(s, 0, sizeof(*s));
    s->lang = flex_lang();
    const flex_look_prefs_t *lk = flex_look();
    s->h24 = lk->h24;
    s->glass = lk->glass;
    s->dark = lk->dark;
    s->navmode = lk->nav_mode;
    s->iconstyle = lk->icon_style;
    s->animstyle = lk->anim_style;
    s->bright = (int)flex_cfg_get_i32("bright", 80);
    s->lockwidgets = (int)flex_cfg_get_i32("lockwidgets", FLEX_LW_CLOCK);
    int kb = (int)flex_cfg_get_i32("kbsize", FLEX_KB_SIZE_NORMAL);
    s->kbsize = (kb < 0 || kb > 2) ? FLEX_KB_SIZE_NORMAL : kb;
    int lt = (int)flex_cfg_get_i32("locktype", 0);
    s->locktype = (lt < 0 || lt > 2) ? 0 : lt;
    s->autolock_ms = flex_set_autolock_norm((uint32_t)flex_cfg_get_i32("autolockms", 60000));
    s->poffpin = flex_cfg_get_bool("poffpin", false);
    s->airplane = flex_cfg_get_bool("airpl", false);
    s->ble_enabled = false;   // el ESP32-P4 no tiene radio BLE (FLEXOS_BLE_HW = 0)
    flex_cfg_get_str("name", s->name, sizeof(s->name), "FlexOS Ultra");
    struct tm tm;
    flex_clock_now(&tm);
    s->rtc_d = tm.tm_mday;
    s->rtc_mo = tm.tm_mon + 1;
    s->rtc_y = tm.tm_year + 1900;
    s->rtc_h = tm.tm_hour;
    s->rtc_min = tm.tm_min;
    s->time_nvs_ok = true;
    // hora por red
    s->ntp_available = flex_ntp_texts != NULL && flex_ntp_request_sync != NULL;
    if (flex_ntp_texts) {
        flex_ntp_texts(s->ntp_state, sizeof(s->ntp_state), s->ntp_last, sizeof(s->ntp_last), &s->ntp_busy);
    } else {
        snprintf(s->ntp_state, sizeof(s->ntp_state), "No disponible");
        snprintf(s->ntp_last, sizeof(s->ntp_last), "Nunca");
    }
    snprintf(s->ota_text, sizeof(s->ota_text), "%s", flex_ota_status_text ? flex_ota_status_text() : "No disponible");
    if (flex_account_settings_text) {
        flex_account_settings_text(s->account_text, sizeof(s->account_text));
    } else {
        snprintf(s->account_text, sizeof(s->account_text), "Sin cuenta vinculada");
    }
    if (flex_conn_wifi_sub) {
        flex_conn_wifi_sub(s->wifi_sub, sizeof(s->wifi_sub));
    } else {
        snprintf(s->wifi_sub, sizeof(s->wifi_sub), "(No disponible)");
    }
    if (flex_conn_ble_sub) {
        flex_conn_ble_sub(s->ble_sub, sizeof(s->ble_sub));
    } else {
        snprintf(s->ble_sub, sizeof(s->ble_sub), "(No disponible)");   // el SoC no tiene BLE: es lo que dice Arduino
    }
    snprintf(s->theft, sizeof(s->theft), "%s", flex_theft_row_value ? flex_theft_row_value() : "No disponible");
    s->audio_avail = flex_audio_available && flex_audio_available();
    s->volume = flex_audio_volume ? flex_audio_volume() : 0;
    s->muted = flex_audio_muted && flex_audio_muted();
    snprintf(s->audio_error, sizeof(s->audio_error), "%s", flex_audio_error ? flex_audio_error() : "Sin codec de audio");
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    s->fs_ready = st.fs_mounted;
    flex_fsn_fmt_size((uint32_t)st.fs_used, s->fs_used, sizeof(s->fs_used));
    flex_fsn_fmt_size((uint32_t)st.fs_total, s->fs_total, sizeof(s->fs_total));
    snprintf(s->trash_size, sizeof(s->trash_size), "%s", S.trash[0] ? S.trash : "--");
    flex_set_uptime((unsigned long)lv_tick_get(), s->uptime, sizeof(s->uptime));
#ifdef FLEX_SIM
    s->free_kb = 2048;
    s->psram_free_mb = 20;
    s->psram_total_mb = 32;
#else
    s->free_kb = (unsigned)(esp_get_free_heap_size() / 1024);
    s->psram_free_mb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576);
    s->psram_total_mb = (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1048576);
#endif
}

static void detail_rebuild(void);
static void trash_cb(esp_err_t err, uint32_t count, uint32_t bytes, void *user)
{
    (void)count;
    if ((uint32_t)(uintptr_t)user != s_gen || !S.created) {
        return;
    }
    if (err == ESP_OK) {
        flex_fsn_fmt_size(bytes, S.trash, sizeof(S.trash));
    }
    if (S.pd && S.sel == 8 && S.dcontent) {   // S.view aun puede ser 0: la medida suele llegar durante la transicion
        detail_rebuild();   // el tamano real de la papelera llego
    }
}

// ===== operaciones -> objetos ===============================================================================================================
static void card_click_cb(lv_event_t *e);
static void detail_rebuild(void);

static int ops_render(lv_obj_t *parent, const flex_set_page_t *pg, int y_shift, lv_obj_t **cards, int ncards, bool clickable)
{
    return flex_set_ops_render(parent, pg, y_shift, cards, ncards, clickable ? card_click_cb : NULL);
}

// ===== paginas ===================================================================================================================================
static lv_obj_t *page_new(void)
{
    lv_obj_t *p = box(S.root, 0, PAGE_Y, 480, PAGE_H, 0);
    lv_obj_set_style_bg_color(p, TH()->page, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_clickable(p, true);   // recoge el gesto de volver que burbujea de sus hijos
    return p;
}

static lv_obj_t *view_new(lv_obj_t *page, int top, int bot, lv_event_cb_t scroll_cb)
{
    lv_obj_t *v = box(page, 0, top - PAGE_Y, 480, bot - top, 0);
    lv_obj_set_clickable(v, true);
    lv_obj_set_scrollable(v, true);
    lv_obj_set_scroll_dir(v, LV_DIR_VER);
    lv_obj_set_scroll_elastic(v, false);        // sin rebote ni inercia, como el original
    lv_obj_set_scroll_momentum(v, false);
    lv_obj_set_scrollbar_mode(v, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(v, scroll_cb, LV_EVENT_SCROLL_END, NULL);
    return v;
}

static void list_scroll_cb(lv_event_t *e)
{
    (void)e;
    S.list_scroll = (int)lv_obj_get_scroll_y(S.lview);
    mark_dirty();   // el scroll es estado de sesion
}

static void detail_scroll_cb(lv_event_t *e)
{
    (void)e;
    S.scroll = (int)lv_obj_get_scroll_y(S.dview);
    mark_dirty();
}

static void list_build(void)
{
    if (S.pl) {
        lv_obj_delete(S.pl);
    }
    S.pl = page_new();
    flex_set_list_head(&PG);
    ops_render(S.pl, &PG, -PAGE_Y, NULL, 0, false);   // el titulo "Ajustes"
    S.lview = view_new(S.pl, FLEX_SET_LIST_TOP, FLEX_SET_LIST_BOT, list_scroll_cb);
    flex_set_list_build(&PG);
    S.lcontent = box(S.lview, 0, 0, 480, PG.content_h > FLEX_SET_LIST_BOT - FLEX_SET_LIST_TOP ? PG.content_h : FLEX_SET_LIST_BOT - FLEX_SET_LIST_TOP, 0);
    memset(S.catcard, 0, sizeof(S.catcard));
    ops_render(S.lcontent, &PG, 0, S.catcard, FLEX_SET_NCAT, true);
    lv_obj_update_layout(S.lview);
    lv_obj_scroll_to_y(S.lview, S.list_scroll, LV_ANIM_OFF);
}

static void detail_content(void)
{
    sys_fill();
    flex_set_detail_build(&PG, S.sel, &S.sys);
    int h = PG.content_h > FLEX_SET_DLIST_BOT - FLEX_SET_DLIST_TOP ? PG.content_h : FLEX_SET_DLIST_BOT - FLEX_SET_DLIST_TOP;
    S.dcontent = box(S.dview, 0, 0, 480, h, 0);
    memset(S.rowcard, 0, sizeof(S.rowcard));
    S.nrows = PG.rows;
    int made = ops_render(S.dcontent, &PG, 0, S.rowcard, FLEX_SET_ROW_MAX, true);
    (void)made;
}

static void back_zone_cb(lv_event_t *e);

static void detail_build(void)
{
    if (S.pd) {
        lv_obj_delete(S.pd);
    }
    S.pd = page_new();
    flex_set_detail_head(&PG, S.sel);
    ops_render(S.pd, &PG, -PAGE_Y, NULL, 0, false);   // chevron, titulo y descripcion de la categoria
    lv_obj_t *bz = box(S.pd, 0, 0, FLEX_SET_BACK_W, 84 - PAGE_Y, 0);   // zona tactil del chevron: x < 52, hasta y = 84
    lv_obj_set_clickable(bz, true);
    lv_obj_add_event_cb(bz, back_zone_cb, LV_EVENT_CLICKED, NULL);
    S.dview = view_new(S.pd, FLEX_SET_DLIST_TOP, FLEX_SET_DLIST_BOT, detail_scroll_cb);
    detail_content();
    lv_obj_update_layout(S.dview);
    lv_obj_scroll_to_y(S.dview, S.scroll, LV_ANIM_OFF);
    if (S.sel == 8) {   // el tamano de la papelera se mide en la tarea de almacenamiento
        s_gen++;
        flex_fs_dirstat_async("/Papelera", trash_cb, (void *)(uintptr_t)s_gen);
    }
}

// Reconstruye solo la banda con scroll de la categoria (lo que hace settingsRenderDetailOnly), conservando la posicion.
static void detail_rebuild(void)
{
    if (!S.pd || !S.dview) {
        return;
    }
    int keep = (int)lv_obj_get_scroll_y(S.dview);
    if (S.dcontent) {
        lv_obj_delete(S.dcontent);
        S.dcontent = NULL;
    }
    detail_content();
    lv_obj_update_layout(S.dview);
    lv_obj_scroll_to_y(S.dview, keep, LV_ANIM_OFF);
}

static void rebuild_all(void)
{
    int ls = S.lview ? (int)lv_obj_get_scroll_y(S.lview) : S.list_scroll;
    int ds = S.dview ? (int)lv_obj_get_scroll_y(S.dview) : S.scroll;
    S.list_scroll = ls;
    S.scroll = ds;
    list_build();
    if (S.view == 1) {
        detail_build();
    } else if (S.pd) {
        lv_obj_delete(S.pd);
        S.pd = S.dview = S.dcontent = NULL;
    }
    if (S.sbar) {
        lv_obj_move_foreground(S.sbar);
    }
    lv_obj_set_hidden(S.pl, S.view == 1);
}

// ===== transicion ===================================================================================================================================
static void anim_exec(void *var, int32_t ms)
{
    (void)var;
    int o, i;
    flex_set_anim_offsets(S.anim_dir, (uint32_t)ms, &o, &i);
    if (!S.pl || !S.pd) {
        return;
    }
    lv_obj_set_x(S.pl, S.anim_dir > 0 ? o : i);   // abrir: la lista sale despacio; volver: la lista vuelve a su sitio
    lv_obj_set_x(S.pd, S.anim_dir > 0 ? i : o);   // abrir: la categoria entra; volver: la categoria se va por la derecha
}

static void anim_done(lv_anim_t *a)
{
    (void)a;
    S.animating = false;
    if (!S.pl) {
        return;
    }
    lv_obj_set_x(S.pl, 0);
    if (S.pd) {
        lv_obj_set_x(S.pd, 0);
        lv_obj_set_style_shadow_width(S.pd, 0, 0);
    }
    if (S.anim_dir > 0) {
        lv_obj_set_hidden(S.pl, true);
    } else if (S.pd) {
        lv_obj_delete(S.pd);
        S.pd = S.dview = S.dcontent = NULL;
    }
    S.view = S.anim_dir > 0 ? 1 : 0;
    mark_dirty();
}

static void anim_start(int dir)
{
    S.anim_dir = dir;
    S.animating = true;
    lv_obj_set_hidden(S.pl, false);
    if (S.pd) {
        lv_obj_move_foreground(S.pd);
        lv_obj_set_style_shadow_width(S.pd, 12, 0);   // sombra a la izquierda de la pagina de encima
        lv_obj_set_style_shadow_ofs_x(S.pd, -6, 0);
        lv_obj_set_style_shadow_color(S.pd, TH()->shadow, 0);
        lv_obj_set_style_shadow_opa(S.pd, LV_OPA_40, 0);
    }
    if (S.sbar) {
        lv_obj_move_foreground(S.sbar);   // la barra de estado se queda quieta
    }
    anim_exec(NULL, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &S);
    lv_anim_set_exec_cb(&a, anim_exec);
    lv_anim_set_values(&a, 0, FLEX_SET_NAV_MS);
    lv_anim_set_duration(&a, FLEX_SET_NAV_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, anim_done);
    lv_anim_start(&a);
}

static void open_cat(int cat, bool animate)
{
    if (cat < 0 || cat >= FLEX_SET_NCAT || S.animating) {
        return;
    }
    S.sel = cat;
    S.scroll = 0;
    detail_build();
    mark_dirty();
    if (animate) {
        anim_start(+1);
    } else {
        S.view = 1;
        lv_obj_set_hidden(S.pl, true);
    }
}

static bool go_back(void)
{
    if (S.view != 1 || S.animating) {
        return S.view == 1;   // durante la transicion "atras" se traga
    }
    S.swipe = false;
    S.list_scroll = (int)lv_obj_get_scroll_y(S.lview);
    mark_dirty();
    anim_start(-1);
    return true;
}

static void back_zone_cb(lv_event_t *e)
{
    (void)e;
    go_back();
}

// Arrastre desde el borde izquierdo para volver: se arma al apoyar y se decide al soltar (settingsTick :706-711). Se sigue el tactil del arbitro
// (el "T" de Arduino) con un temporizador y no los eventos del objeto: en cuanto el dedo se mueve LVGL puede dar la pulsacion por perdida
// (empieza un desplazamiento) y dejar de dar el punto, asi que no habria un "soltar" que decidir.
static lv_timer_t *s_swipe_timer;
static bool s_track;
static int s_lx, s_ly;

static void swipe_tick(lv_timer_t *t)
{
    (void)t;
    const flex_arb_touch_t *T = &flex_touch_arb()->t;
    if (T->down) {
        if (!s_track) {
            s_track = true;
            S.sx = T->start_x;
            S.sy = T->start_y;
            S.swipe = S.view == 1 && !S.animating && flex_set_back_swipe_armed(S.sx);
        }
        s_lx = T->x;
        s_ly = T->y;
    } else if (s_track) {
        s_track = false;
        bool armed = S.swipe;
        S.swipe = false;
        if (armed && S.view == 1 && !S.animating && !S.suspended && flex_set_back_swipe_done(S.sx, S.sy, s_lx, s_ly)) {
            go_back();
        }
    }
}

// ===== acciones =======================================================================================================================================
static void toast_soon(void)
{
    flex_kit_toast(S.root, "Proximamente");
}

static void lock_setup_done_void(void)
{
    if (S.created && S.pd) {
        detail_rebuild();   // el tamano del teclado pudo cambiar
    }
}

static void lock_setup_done(void *ctx)
{
    (void)ctx;
    if (S.created && S.pd) {
        detail_rebuild();
    }
}

static void apply_act(const flex_set_act_t *a)
{
    const flex_set_sys_t *s = &S.sys;
    uint32_t f = a->flags;
    if (f & FLEX_SF_SAVE_LANG) {
        flex_set_lang(s->lang);
    }
    if (f & FLEX_SF_SAVE_H24) {
        flex_theme_set_h24(s->h24);
    }
    if (f & FLEX_SF_BACKLIGHT) {
        flex_display_set_brightness((uint8_t)s->bright);
    }
    if (f & FLEX_SF_SAVE_BRIGHT) {
        flex_cfg_set_i32("bright", s->bright);
    }
    if (f & FLEX_SF_SAVE_LOCKW) {
        flex_cfg_set_i32("lockwidgets", s->lockwidgets);
    }
    if (f & FLEX_SF_SAVE_NAV) {
        flex_theme_set_nav_mode((uint8_t)s->navmode);
    }
    if (f & FLEX_SF_SAVE_ICON) {
        flex_theme_set_icon_style((uint8_t)s->iconstyle);
    }
    if (f & FLEX_SF_SAVE_ANIM) {
        flex_theme_set_anim_style((uint8_t)s->animstyle);
    }
    if (f & FLEX_SF_SAVE_AUTOLOCK) {
        flex_cfg_set_i32("autolockms", (int32_t)s->autolock_ms);
        lv_display_trigger_activity(NULL);   // el temporizador nuevo cuenta desde ahora (gLastTouchMs = millis())
    }
    if (f & FLEX_SF_SAVE_POFFPIN) {
        flex_cfg_set_bool("poffpin", s->poffpin);
    }
    if (f & FLEX_SF_THEME) {
        flex_theme_set_glass(s->glass);   // Estilo y Modo de apariencia: guardan, invalidan cachés y repintan
        flex_theme_set_dark(s->dark);
    }
    if (f & (FLEX_SF_SAVE_LANG | FLEX_SF_SAVE_H24 | FLEX_SF_SAVE_NAV | FLEX_SF_SAVE_ICON)) {
        flex_home_refresh();   // gHomeDirty: el escritorio se recompone antes de volver a mostrarse
    }
    // pantallas y servicios
    switch (a->open) {
    case FLEX_SX_NTP_SYNC:
        if (flex_ntp_request_sync) {
            flex_ntp_request_sync(true);
        }
        break;
    case FLEX_SX_OTA:
        if (flex_ota_open_settings) {
            flex_ota_open_settings();
        } else {
            toast_soon();
        }
        break;
    case FLEX_SX_FACTORY:
        if (flex_factory_reset_available()) {
            flex_factory_reset_open(false);
        } else {
            toast_soon();
        }
        break;
    case FLEX_SX_ACCOUNT:
        if (flex_account_settings_enter) {
            flex_account_settings_enter();
        } else {
            toast_soon();
        }
        break;
    case FLEX_SX_CONN:
        if (flex_conn_enter) {
            flex_conn_enter();
        } else {
            toast_soon();
        }
        break;
    case FLEX_SX_WIFI:
        if (flex_wifi_settings_enter) {
            flex_wifi_settings_enter();
        } else {
            toast_soon();
        }
        break;
    case FLEX_SX_BLE_TOGGLE:
        if (flex_ble_toggle) {
            flex_ble_toggle();
        }
        break;
    case FLEX_SX_KEYBOARD:
        if (flex_kbsettings_open) {
            flex_kbsettings_open(lock_setup_done_void);
        } else {
            toast_soon();   // los ajustes del teclado llegan con su propio bloque
        }
        break;
    case FLEX_SX_LOCKSETUP:
        flex_auth_setup(lock_setup_done, NULL);
        break;
    case FLEX_SX_THEFT:
        if (flex_theft_enter) {
            flex_theft_enter();
        } else {
            toast_soon();
        }
        break;
    default:
        break;
    }
    if (f & FLEX_SF_THEME) {
        rebuild_all();
    } else if (a->render == FLEX_SRD_FULL) {
        flex_statusbar_refresh_all();
        rebuild_all();
    } else if (a->render == FLEX_SRD_BAND) {
        detail_rebuild();
    }
}

static void card_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    if (S.animating || !S.created) {
        return;
    }
    if (S.view == 0) {
        open_cat(i, true);
        return;
    }
    sys_fill();
    flex_set_act_t a;
    flex_set_row_action(&S.sys, S.sel, i, &a);
    apply_act(&a);
}

// ===== ciclo de vida y sesion (03b §11.8) ============================================================================================================
static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    (void)area;
    bool had = S.sess_valid;
    flex_set_sess_t sess = S.sess;
    memset(&S, 0, sizeof(S));
    S.sess = sess;
    S.sess_valid = had;
    S.root = root;
    S.created = true;
    S.sel = 0;
    if (had) {
        S.sel = sess.sel;
        S.scroll = sess.scroll;
        S.list_scroll = sess.list_scroll;
    }
    S.sbar = flex_statusbar_create(root, FLEX_SB_APP);
    if (S.sbar) {
        lv_obj_set_style_bg_opa(S.sbar, LV_OPA_TRANSP, 0);   // Ajustes pinta la pagina entera: la barra de estado solo pone sus glifos encima (como en Arduino)
    }
    s_track = false;
    s_swipe_timer = lv_timer_create(swipe_tick, 16, NULL);
    list_build();
    if (had && sess.view == 1) {
        detail_build();
        S.view = 1;
        lv_obj_set_hidden(S.pl, true);
    }
    if (S.sbar) {
        lv_obj_move_foreground(S.sbar);
    }
}

static void on_close(void)
{
    lv_anim_delete(&S, anim_exec);
    if (s_swipe_timer) {
        lv_timer_delete(s_swipe_timer);
        s_swipe_timer = NULL;
    }
    flex_set_sess_t sess = S.sess;
    memset(&S, 0, sizeof(S));
    S.sess = sess;
    S.sess_valid = false;   // la sesion se olvida al cerrar de verdad
}

static void on_suspend(void)
{
    S.suspended = true;
    S.swipe = false;
    if (S.animating) {   // una transicion a medias se cierra en el acto: al volver no debe quedar nada movido
        lv_anim_delete(&S, anim_exec);
        anim_done(NULL);
    }
}

static void on_resume(void)
{
    S.suspended = false;
    S.swipe = false;
    if (S.created) {
        rebuild_all();   // misma categoria, mismo scroll: los valores vivos (hora, brillo, tema) se leen otra vez
    }
}

// "atras": categoria -> lista; en la lista devuelve false y el sistema cierra la app
static bool on_back(void)
{
    if (S.view == 1 || S.animating) {
        return go_back() || S.animating;
    }
    return false;
}

static bool save_session(void *out, size_t cap, size_t *len)
{
    if (cap < sizeof(flex_set_sess_t)) {
        return false;
    }
    flex_set_sess_t s;
    int ls = S.lview ? (int)lv_obj_get_scroll_y(S.lview) : S.list_scroll;
    int ds = S.dview ? (int)lv_obj_get_scroll_y(S.dview) : S.scroll;
    flex_set_sess_pack(&s, S.view, S.sel, ds, ls);
    memcpy(out, &s, sizeof(s));
    *len = sizeof(s);
    return true;
}

static void load_session(const void *data, size_t len)
{
    S.sess_valid = false;
    if (data && len == sizeof(flex_set_sess_t)) {
        memcpy(&S.sess, data, sizeof(S.sess));
        S.sess_valid = flex_set_sess_normalize(&S.sess);   // incoherente: se ignora entera
    }
}

const flex_app_ops_t flex_app_ajustes_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_suspend = on_suspend,
    .on_close = on_close,
    .on_back = on_back,
    .save_session = save_session,
    .load_session = load_session,
};

// ---- pruebas ---------------------------------------------------------------------------------------------------------------------------------
int flex_app_settings_view(void) { return S.created ? S.view : -1; }
int flex_app_settings_sel(void) { return S.sel; }
bool flex_app_settings_animating(void) { return S.animating; }
int flex_app_settings_scroll(void) { return S.view == 1 && S.dview ? (int)lv_obj_get_scroll_y(S.dview) : S.scroll; }
int flex_app_settings_list_scroll(void) { return S.lview ? (int)lv_obj_get_scroll_y(S.lview) : S.list_scroll; }
int flex_app_settings_rows(void) { return S.nrows; }
lv_obj_t *flex_app_settings_root(void) { return S.root; }
// Centro en pantalla de la tarjeta de categoria i / de la fila i de la categoria abierta
bool flex_app_settings_card_xy(int i, int *x, int *y)
{
    if (i < 0 || i >= FLEX_SET_NCAT || !S.catcard[i]) return false;
    lv_area_t a;
    lv_obj_get_coords(S.catcard[i], &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}
bool flex_app_settings_row_xy(int i, int *x, int *y)
{
    if (i < 0 || i >= S.nrows || !S.rowcard[i]) return false;
    lv_area_t a;
    lv_obj_get_coords(S.rowcard[i], &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}
const flex_set_sys_t *flex_app_settings_sys(void)
{
    sys_fill();
    return &S.sys;
}
int flex_app_settings_page_x(bool detail)
{
    lv_obj_t *o = detail ? S.pd : S.pl;
    return o ? (int)lv_obj_get_x(o) : 0;
}
// Hay una etiqueta con ese texto en la pagina de categoria (lo que se ve de verdad, no lo que el modelo calcularia)
static bool find_text(lv_obj_t *o, const char *txt)
{
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), txt)) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        if (find_text(lv_obj_get_child(o, (int32_t)i), txt)) {
            return true;
        }
    }
    return false;
}
bool flex_app_settings_shown(const char *txt)
{
    return S.pd && find_text(S.pd, txt);
}
bool flex_app_settings_list_hidden(void)
{
    return S.pl && lv_obj_is_hidden(S.pl);
}
