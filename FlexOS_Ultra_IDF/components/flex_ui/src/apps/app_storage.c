// Flex OS Ultra · app Almacenamiento (docs/spec/03 §9; Arduino: FlexOS_Ultra_AppStorage.h; inventario A04).
//
// Dos pantallas dentro del marco de la app: la principal (memoria interna, PSRAM, "detalles de memoria y sistema", el telefono, las cuatro
// categorias, los archivos mayores y la fila "Todos los archivos") y los detalles (cinco secciones con scroll nativo: acceso a Optimizar,
// PSRAM, SRAM, flash y estado del sistema). Que se dibuja y donde lo decide el modelo (flex_stor_model.c, comparado con Arduino sobre 260 000
// casos); aqui solo esta LVGL y los servicios REALES: la medida de las categorias corre en la tarea de almacenamiento (asincrona: hasta que
// llega se lee "..." / "Calculando...", nunca una cifra inventada), la memoria sale de flex_mem, el firmware de la cabecera de la imagen y el
// ritmo del sistema de las vueltas de la tarea de UI.
//
// Lo que NO existe todavia (inventario A04c): la tarjeta "Flex Cloud en tu telefono" depende del servicio de telefono emparejado (Flex Storage)
// y la pestana Flex Cloud del explorador del servicio de nube: dicen "No disponible" y no fingen un estado. Optimizar es el panel modal del
// sistema (flex_optimize); el explorador (Ver...) es modal sobre esta app (flex_files.c) y al salir esta pantalla vuelve a medir.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_files_model.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_metrics.h"
#include "flex_set_model.h"
#include "flex_shell.h"
#include "flex_stor_model.h"
#include "flex_stor_ui.h"
#include "flex_storage.h"
#include "flex_system.h"
#include "flex_theme.h"

#ifdef FLEX_SIM
#define LOCAL_VERSION "1.0.0"
#else
#include "flex_core.h"
#define LOCAL_VERSION (flex_core_boot_info()->app_version)
#endif

void flex_files_open(void (*on_back)(void));   // flex_files.c
bool flex_files_active(void);
void flex_mem_sample_now(void);
const FlexMemSnap *flex_mem_snap(void);
bool flex_net_online(void);   // flex_home_widgets.c (con el servicio de red real, el suyo)

enum { SCR_MAIN = 0, SCR_DETAIL };

static uint32_t s_gen;   // sobrevive al cierre: una medida que llega despues de cerrar y volver a abrir no es de esta pantalla

static struct {
    lv_obj_t *root, *page, *view, *content;
    lv_obj_t *sec[FLEX_ST_SEC_N];
    int w, h;
    int screen;
    flex_st_main_in_t in;
    flex_st_zones_t zones;
    flex_fsx_scan_t scan;
    bool scanned, scan_err, scan_pending, susp;
    uint32_t sig[FLEX_ST_SEC_N];
    lv_timer_t *tmr;
    int builds;   // para las pruebas: cuantas veces se pinto la pantalla principal
} S;

static void main_build(void);
static void detail_build(void);
static void scan_start(void);

// ---- datos reales ---------------------------------------------------------------------------------------------------------------------------------
static void gather_main(flex_st_main_in_t *in)
{
    memset(in, 0, sizeof(*in));
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    in->fs_ready = st.fs_mounted;
    snprintf(in->fs_err, sizeof(in->fs_err), "%s", st.fs_mounted ? "" : esp_err_to_name(st.fs_err));
    in->fs_total = (uint32_t)st.fs_total;
    in->fs_used = (uint32_t)st.fs_used;
    const FlexMemSnap *m = flex_mem_snap();
    in->ps_total = m->psTotal;
    in->ps_free = m->psFree;
    in->health = flexMemHealth(m);
    in->scanned = S.scanned;
    in->scan_err = S.scan_err;
    if (S.scanned && !S.scan_err) {
        memcpy(in->cat, S.scan.cat, sizeof(in->cat));
        in->nbig = S.scan.nbig;
        memcpy(in->big, S.scan.big, sizeof(in->big));
    }
    in->phone.state = FST_PH_UNAVAILABLE;   // sin el servicio de telefono emparejado (A04c)
}

static void gather_sys(flex_st_sys_t *s)
{
    memset(s, 0, sizeof(*s));
    s->m = *flex_mem_snap();
    s->fw_size = flex_system_fw_size();
    if (S.scanned && !S.scan_err) {
        s->cat_sys = S.scan.cat[FLEX_FSX_CAT_SYS];
        s->cat_apps = S.scan.cat[FLEX_FSX_CAT_APPS];
        s->cat_docs = S.scan.cat[FLEX_FSX_CAT_DOCS];
        s->cache = S.scan.cache;
    }
    s->reset_reason = flex_system_reset_reason();
    flex_set_uptime((unsigned long)lv_tick_get(), s->uptime, sizeof(s->uptime));
    s->uptime_min = (unsigned)(lv_tick_get() / 60000u);
    snprintf(s->version, sizeof(s->version), "%s", LOCAL_VERSION);
    s->net_online = flex_net_online();
    flex_metrics_t mt;
    flex_metrics_get(&mt);
    s->loop_rate = (unsigned)(mt.ui_loops + 0.5f);
    s->napps = FLEX_ST_APPS;
    for (int i = 0; i < FLEX_ST_APPS; i++) {
        s->app_state[i] = (uint8_t)flex_app_life(i);
        s->app_shed[i] = flex_mem_app_shed(i);
        s->app_mem_has[i] = flex_mem_app_known(i);
        s->app_mem[i] = flex_mem_app_bytes(i);
        snprintf(s->app_name[i], sizeof(s->app_name[i]), "%s", flex_app_name(i));
    }
}

// ---- medida de las categorias (tarea de almacenamiento) --------------------------------------------------------------------------------------------
static void refresh_after_scan(void)
{
    if (!S.root) {
        return;
    }
    if (S.screen == SCR_MAIN) {
        main_build();
    } else if (S.sec[3]) {
        S.sig[3] = 0xFFFFFFFFu;   // las cifras de la seccion de flash dependen de esta medida: se repinta aunque la firma no cambie
    }
}

static void scan_cb(esp_err_t err, const flex_fsx_scan_t *sc, void *user)
{
    if (!S.root || (uint32_t)(uintptr_t)user != s_gen) {
        return;
    }
    S.scan_pending = false;
    if (err == ESP_OK && sc) {
        S.scan = *sc;
        S.scanned = true;
        S.scan_err = false;
    } else {
        S.scanned = true;
        S.scan_err = true;   // "--" y "No se pudo leer": no se inventan ceros
    }
    refresh_after_scan();
}

static void scan_start(void)
{
    if (!S.root || S.scan_pending) {
        return;
    }
    S.scan_pending = true;
    if (flex_fs_scan_async("/System/Media/", scan_cb, (void *)(uintptr_t)s_gen) != ESP_OK) {
        S.scan_pending = false;
        S.scanned = true;
        S.scan_err = true;
    }
}

// ---- pantalla principal --------------------------------------------------------------------------------------------------------------------------------
static void optimize_done(void)
{
    if (!S.root) {
        return;
    }
    S.scanned = false;   // Optimizar vacia la cache: lo medido ya no vale
    S.scan_err = false;
    scan_start();
    if (S.screen == SCR_MAIN) {
        main_build();
    } else {
        detail_build();
    }
}

static void files_back(void)
{
    if (!S.root) {
        return;
    }
    S.scanned = false;   // desde el explorador se pudo borrar o renombrar: los tamanos de esta pantalla ya no valen
    S.scan_err = false;
    scan_start();
    main_build();
}

static void main_click_cb(lv_event_t *e)
{
    (void)e;
    if (!S.page) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(S.page, &a);
    int x = (int)p.x - a.x1, y = (int)p.y - a.y1;
    switch (flex_st_main_hit(&S.zones, x, y)) {
    case FST_HIT_PHONE:
        // la tarjeta abre la pantalla del telefono emparejado: sin el servicio de Flex Storage (A04c) no hay nada que mostrar ni emparejar
        flex_notify("Flex Cloud en tu tel\xC3\xA9" "fono", "No disponible todav\xC3\xAD" "a en esta versi\xC3\xB3n");
        break;
    case FST_HIT_DETAIL:
        flex_mem_sample_now();   // almDetailEnter: la primera lectura es inmediata
        S.screen = SCR_DETAIL;
        scan_start();
        detail_build();
        break;
    case FST_HIT_OPTIMIZE:
        flex_optimize_open(optimize_done);
        break;
    case FST_HIT_FILES:
        flex_files_open(files_back);
        break;
    default:
        break;
    }
}

static void clear_screens(void)
{
    if (S.page) {
        lv_obj_delete(S.page);
    }
    if (S.view) {
        lv_obj_delete(S.view);
    }
    S.page = S.view = S.content = NULL;
    memset(S.sec, 0, sizeof(S.sec));
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, false);
    return o;
}

static void main_build(void)
{
    if (!S.root) {
        return;
    }
    clear_screens();
    S.screen = SCR_MAIN;
    S.builds++;
    gather_main(&S.in);
    static flex_st_page_t pg;
    flex_st_main_build(&pg, &S.zones, &S.in, 0, 0, S.w, S.h, flex_ui_text_w, NULL);
    S.page = box(S.root, 0, 0, S.w, S.h);
    lv_obj_set_clickable(S.page, true);
    lv_obj_add_event_cb(S.page, main_click_cb, LV_EVENT_CLICKED, NULL);
    flex_st_ui_render(S.page, &pg, 0, 0);
}

// ---- detalles de memoria y sistema --------------------------------------------------------------------------------------------------------------------
static void opt_click_cb(lv_event_t *e)
{
    (void)e;
    flex_optimize_open(optimize_done);
}

static void sec_render(int i, const flex_st_sys_t *s)
{
    if (!S.sec[i]) {
        return;
    }
    lv_obj_clean(S.sec[i]);
    static flex_st_page_t pg;
    flex_st_detail_section(&pg, i, s);
    flex_st_ui_render(S.sec[i], &pg, 0, 0);
    if (i == 0) {   // el boton "Optimizar Flex OS": su zona es la misma con la que se dibuja
        lv_obj_t *z = box(S.sec[i], FLEX_ST_OPT_X, FLEX_ST_OPT_Y, FLEX_ST_OPT_W, FLEX_ST_OPT_H);
        lv_obj_set_clickable(z, true);
        lv_obj_add_event_cb(z, opt_click_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void detail_build(void)
{
    if (!S.root) {
        return;
    }
    int keep = (S.view && S.screen == SCR_DETAIL) ? (int)lv_obj_get_scroll_y(S.view) : 0;
    clear_screens();
    S.screen = SCR_DETAIL;
    S.view = box(S.root, 0, 0, S.w, S.h);
    lv_obj_set_clickable(S.view, true);
    lv_obj_set_scrollable(S.view, true);
    lv_obj_set_scroll_dir(S.view, LV_DIR_VER);
    lv_obj_set_scroll_elastic(S.view, false);
    lv_obj_set_scroll_momentum(S.view, false);
    lv_obj_set_scrollbar_mode(S.view, LV_SCROLLBAR_MODE_OFF);
    int ch = flex_st_content_h() + 8;
    if (ch < S.h) {
        ch = S.h;
    }
    S.content = box(S.view, 0, 0, S.w, ch);
    static flex_st_sys_t sys;
    gather_sys(&sys);
    for (int i = 0; i < FLEX_ST_SEC_N; i++) {
        S.sec[i] = box(S.content, 0, flex_st_sec_top(i), 480, flex_st_sec_h[i]);
        sec_render(i, &sys);
        S.sig[i] = flex_st_detail_sig(i, &sys);
    }
    lv_obj_update_layout(S.view);
    lv_obj_scroll_to_y(S.view, keep, LV_ANIM_OFF);
}

// Refresco de cifras: una vez por segundo, por firma y por seccion, con el dedo fuera (almDetailTick)
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!S.root || S.susp || S.screen != SCR_DETAIL || !S.view || flex_files_active()) {
        return;
    }
    if (lv_indev_active() && lv_indev_get_state(lv_indev_active()) == LV_INDEV_STATE_PRESSED) {
        return;
    }
    static flex_st_sys_t sys;
    gather_sys(&sys);
    for (int i = 1; i < FLEX_ST_SEC_N; i++) {
        uint32_t sg = flex_st_detail_sig(i, &sys);
        if (sg == S.sig[i]) {
            continue;   // esta seccion no ha cambiado: no se toca
        }
        S.sig[i] = sg;
        sec_render(i, &sys);
    }
}

static void timer_sync(void)
{
    if (S.susp && S.tmr) {
        lv_timer_delete(S.tmr);
        S.tmr = NULL;
    } else if (!S.susp && !S.tmr) {
        S.tmr = lv_timer_create(tick_cb, 1000, NULL);
    }
}

// ---- ciclo de vida ----------------------------------------------------------------------------------------------------------------------------------------
static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    memset(&S, 0, sizeof(S));
    s_gen++;
    S.root = root;
    S.w = lv_area_get_width(area);
    S.h = lv_area_get_height(area);
    S.screen = SCR_MAIN;
    main_build();
    scan_start();
    timer_sync();
}

static void on_resume(void)
{
    S.susp = false;
    if (!S.root) {
        return;
    }
    S.scanned = false;   // almEnter / almResume miden de nuevo
    S.scan_err = false;
    if (S.screen == SCR_DETAIL) {
        flex_mem_sample_now();
        detail_build();
    } else {
        main_build();
    }
    scan_start();
    timer_sync();
}

static void on_suspend(void)
{
    S.susp = true;
    timer_sync();
}

static void on_close(void)
{
    if (S.tmr) {
        lv_timer_delete(S.tmr);
    }
    memset(&S, 0, sizeof(S));
    s_gen++;   // lo que aun este en vuelo ya no es de nadie
}

// Atras desde los detalles vuelve a la lista, no expulsa la app (misma regla que Galeria, Notas y Multimedia)
static bool on_back(void)
{
    if (!S.root || S.screen != SCR_DETAIL) {
        return false;
    }
    main_build();
    return true;
}

// ---- pruebas ---------------------------------------------------------------------------------------------------------------------------------------------------
int flex_app_storage_screen(void) { return S.root ? S.screen : -1; }
bool flex_app_storage_scanned(void) { return S.root && S.scanned; }
bool flex_app_storage_scan_failed(void) { return S.root && S.scan_err; }
int flex_app_storage_builds(void) { return S.builds; }
const flex_st_zones_t *flex_app_storage_zones(void) { return S.root ? &S.zones : NULL; }
// Punto de pantalla del centro de una zona de la pantalla principal (0 telefono, 1 detalles, 2 Optimizar, 3 Ver...)
bool flex_app_storage_zone_xy(int zone, int *x, int *y)
{
    if (!S.root || !S.page || S.screen != SCR_MAIN) {
        return false;
    }
    lv_area_t a;
    lv_obj_get_coords(S.page, &a);
    const flex_st_zones_t *z = &S.zones;
    int y0 = 0, y1 = 0, px = 20;
    switch (zone) {
    case 0: y0 = z->phone_y0; y1 = z->phone_y1; break;
    case 1: y0 = z->det_y0; y1 = z->det_y1; px = 40; break;
    case 2: y0 = z->det_y0; y1 = z->det_y1; px = (z->opt_x0 + z->opt_x1) / 2; break;
    default: y0 = z->ver_y0; y1 = z->ver_y1; break;
    }
    if (y1 <= y0) {
        return false;
    }
    *x = a.x1 + px;
    *y = a.y1 + (y0 + y1) / 2;
    return true;
}
bool flex_app_storage_opt_xy(int *x, int *y)
{
    if (!S.root || S.screen != SCR_DETAIL || !S.sec[0]) {
        return false;
    }
    lv_area_t a;
    lv_obj_get_coords(S.sec[0], &a);
    *x = a.x1 + FLEX_ST_OPT_X + FLEX_ST_OPT_W / 2;
    *y = a.y1 + FLEX_ST_OPT_Y + FLEX_ST_OPT_H / 2;
    return true;
}
int flex_app_storage_scroll(void) { return S.view ? (int)lv_obj_get_scroll_y(S.view) : 0; }
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
bool flex_app_storage_shown(const char *txt) { return S.root && find_text(S.root, txt); }

const flex_app_ops_t flex_app_almacen_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_suspend = on_suspend,
    .on_close = on_close,
    .on_back = on_back,
};
