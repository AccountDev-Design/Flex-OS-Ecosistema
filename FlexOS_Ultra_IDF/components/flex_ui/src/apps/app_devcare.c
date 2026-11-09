// Flex OS Ultra · app Device Care (docs/spec/03b §15; Arduino: FlexOS_Ultra_DeviceCare.h y FlexOS_Ultra_DeviceTests.h; inventario A17).
//
// Seis accesos (estado, diagnostico completo, deteccion de caidas, salud, optimizacion, historial) y, dentro del diagnostico, el motor de etapas
// (sistema, IMU, pantalla, tactil, resultado). Que se dibuja y donde lo deciden flex_dc_model.c y flex_dc_screens.c: las operaciones de dibujo con
// las coordenadas de Arduino, comparadas primitiva a primitiva con las funciones originales compiladas tal cual. Aqui esta LVGL (flex_dc_ui.c las
// pinta), el reloj de las animaciones, los toques y los servicios REALES: memoria de flex_mem, reinicios y firmware de flex_system, bus I2C de
// flex_i2c, almacenamiento de flex_storage. Ni una cifra esta inventada: lo que no se puede medir dice "No disponible".
//
// Lo que NO existe todavia (inventario A17h): el GY-BNO085 (IMU) y la deteccion de caidas dependen de su controlador (A08 / N6). La pantalla de
// caidas lo dice, el diagnostico anota "IMU no disponible" y no puntua ni a favor ni en contra, y el historial no recibe eventos de caida.
//
// El historial (16 registros, /System/devcare.bin) se lee y se escribe por la tarea de almacenamiento (nunca en la tarea de UI) y sobrevive al
// cierre de la app, como en Arduino. Una escritura a la vez; si llega otro registro mientras se escribe, se vuelve a escribir al terminar.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_clock.h"
#include "flex_dc_model.h"
#include "flex_dc_screens.h"
#include "flex_dc_ui.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_i2c.h"
#include "flex_icons.h"
#include "flex_kit.h"
#include "flex_layout.h"
#include "flex_metrics.h"
#include "flex_storage.h"
#include "flex_system.h"
#include "flex_theme.h"

#ifdef FLEX_SIM
bool sim_task_wdt_ok(void);   // sim/stubs/flex_services_stub.c
#else
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "sdkconfig.h"
#endif

void flex_mem_sample_now(void);
const FlexMemSnap *flex_mem_snap(void);

#define HIST_PATH "/System/devcare.bin"

// pantallas (DC_* de Arduino; sin la prueba del modulo IMU, que es de A17h)
enum { DC_HOME = 0, DC_STATUS, DC_FALL, DC_HIST, DC_HEALTH, DC_OPTIM, DC_DIAG, DC_POST, DC_SCRTEST, DC_TOUCHTEST, DC_RESULT };
// pasadas del test de pantalla
enum { DCP_BLACK = 0, DCP_WHITE, DCP_RED, DCP_GREEN, DCP_BLUE, DCP_GRAY, DCP_GRAD, DCP_PATTERN, DCP_ASK, DCP_N };

#define BOX_X 0
#define BOX_Y 96    // WIN_TOP
#define BOX_W 480
#define BOX_H 640   // WIN_BOT - WIN_TOP

#define TICK_MS 33

// ---- historial: sobrevive al cierre de la app (en Arduino son estaticos) ------------------------------------------------------------------------
static flex_dc_hist_t H;
static bool s_loaded, s_loading, s_wr_busy;
static uint32_t s_hver, s_wr_ver;   // version del historial en RAM y la que se mando a escribir
static int s_wr_ok_count;           // para las pruebas: escrituras terminadas bien
static int s_wr_fail_count;
static struct {
    uint32_t utc;
    uint8_t kind, score, flags, extra;
} s_pend[4];                        // registros que llegaron mientras se leia el archivo
static int s_npend;
static uint32_t s_gen;              // sobrevive al cierre: una respuesta tardia no es de la pantalla de ahora

static struct {
    lv_obj_t *root, *page, *sbar, *hdr, *back, *title, *content, *click, *view, *hscroll, *ovl;
    flex_dc_page_t *pg;
    struct { int16_t x0, y0, x1, y1; uint8_t id; } hit[FLEX_DC_HITS_MAX];
    int nhit;
    flex_dc_ctx_t ctx;
    bool susp;
    uint32_t gen;
    int screen, prev;
    uint32_t anim_t0, anim_ms;
    lv_timer_t *tmr;
    // metricas de esta pantalla (se miden al entrar y una vez por segundo en Salud)
    flex_dc_metric_t m[DCH_N];
    bool score_ok;
    uint8_t score;
    uint32_t health_ms;
    // motor de diagnostico
    int stage;
    uint32_t diag_ms;
    bool post;
    flex_dc_res_t res;
    flex_dc_sysrow_t rows[FLEX_DC_SYS_ROWS];
    int scr_step;
    uint8_t cell[FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS];
    int tt_hit, tt_lx, tt_ly;
    // para las pruebas
    int builds, taps;
} S;

static void show(int screen);
static void refresh(void);
static void hist_flush(void);

// ---- utilidades ---------------------------------------------------------------------------------------------------------------------------------
static void *big_alloc(size_t n)
{
#ifndef FLEX_SIM
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // la pagina de operaciones no es para la RAM interna
    if (p) {
        return p;
    }
#endif
    return calloc(1, n);
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, false);
    return o;
}

static bool alive(void) { return S.root != NULL; }
static uint32_t now_ms(void) { return lv_tick_get(); }

static void ctx_setup(void)
{
    S.ctx.bx = BOX_X;
    S.ctx.by = BOX_Y;
    S.ctx.bw = BOX_W;
    S.ctx.bh = BOX_H;
    S.ctx.li = flex_li();
    S.ctx.tw = flex_ui_text_w;
    S.ctx.tctx = NULL;
    S.ctx.mo_short = NULL;
}
static const char *mo_short_cb(int li, int m) { return flex_i18n_mo_short[li][m]; }

// ---- datos reales ---------------------------------------------------------------------------------------------------------------------------------
static void gather(flex_dc_sys_t *y)
{
    memset(y, 0, sizeof(*y));
    y->li = flex_li();
    y->m = *flex_mem_snap();
    y->boot_reason = (uint8_t)flex_system_reset_reason();
    y->uptime_ms = now_ms();
#ifdef FLEX_SIM
    y->wdt_ok = sim_task_wdt_ok();
#else
    y->wdt_ok = esp_task_wdt_status(NULL) == ESP_OK;   // esta tarea (la de UI) sigue vigilada
#endif
    flex_metrics_t mt;
    flex_metrics_get(&mt);
    y->loop_rate = mt.ui_loops > 0.0f ? (unsigned)(mt.ui_loops + 0.5f) : 0;
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    y->fs_ready = st.fs_mounted;
    y->time_nvs_ok = st.nvs_ok;
    y->clk_anchored = true;   // el reloj arranca de una hora semilla (flex_clock_init): siempre tiene ancla
    y->flush_fault = false;   // el volcado al panel no reporta fallos medibles (sin DMA2D): no se cuenta lo que no se mide
    flex_i2c_stats_t is;
    flex_i2c_get_stats(&is);
    y->recover_n = (int)(is.recoveries > 1000000u ? 1000000u : is.recoveries);
    y->boot_abnormal = flex_system_abnormal_boots();
    y->fs_total = (uint32_t)st.fs_total;
    y->fs_used = (uint32_t)st.fs_used;
#ifdef FLEX_SIM
    y->cpu_mhz = 400;
#elif defined(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
    y->cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#endif
    y->fw_size = flex_system_fw_size();
}

static void measure(void)
{
    flex_mem_sample_now();
    flex_dc_sys_t y;
    gather(&y);
    flex_dc_collect(&y, S.m);
    S.score_ok = flex_dc_score_from(S.m, &S.score);
    S.health_ms = now_ms();
}

// ---- historial persistente ------------------------------------------------------------------------------------------------------------------------
static void wr_cb(esp_err_t err, void *user)
{
    (void)user;
    s_wr_busy = false;
    if (err == ESP_OK) {
        s_wr_ok_count++;
        if (s_wr_ver == s_hver) {
            flex_dc_hist_saved(&H);
        } else {
            hist_flush();   // llego otro registro mientras se escribia
        }
    } else {
        s_wr_fail_count++;   // queda sin guardar: se reintenta con el siguiente registro, al suspender o al cerrar
    }
}

static void hist_flush(void)
{
    if (!H.dirty || s_wr_busy) {
        return;
    }
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    if (!st.fs_mounted) {
        return;   // sin particion no se pierde nada: no habia donde escribir
    }
    static flex_dc_file_t f;
    if (!flex_dc_hist_file(&H, &f)) {
        return;
    }
    s_wr_ver = s_hver;
    if (flex_fs_write_async(HIST_PATH, &f, sizeof(f), wr_cb, NULL) == ESP_OK) {
        s_wr_busy = true;
    }
}

static void rd_cb(esp_err_t err, uint8_t *data, size_t len, void *user)
{
    (void)user;
    s_loading = false;
    s_loaded = true;
    bool ok = err == ESP_OK && data;
    flex_dc_hist_load(&H, ok ? data : NULL, ok ? (int)len : -1);
    free(data);
    for (int i = 0; i < s_npend; i++) {   // lo que se anoto mientras se leia va encima de lo leido
        flex_dc_hist_add(&H, s_pend[i].utc, s_pend[i].kind, s_pend[i].score, s_pend[i].flags, s_pend[i].extra);
        s_hver++;
    }
    if (s_npend) {
        s_npend = 0;
        hist_flush();
    }
    if (alive() && !S.susp && (S.screen == DC_HOME || S.screen == DC_HIST)) {
        refresh();   // la ultima comprobacion de la tarjeta y la lista
    }
}

static void hist_load_start(void)
{
    if (s_loaded || s_loading) {
        return;
    }
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    if (!st.fs_mounted) {
        s_loaded = true;   // dcHistLoad: sin particion, historial vacio
        flex_dc_hist_load(&H, NULL, -1);
        return;
    }
    if (flex_fs_read_async(HIST_PATH, sizeof(flex_dc_file_t), rd_cb, NULL) == ESP_OK) {
        s_loading = true;
    } else {
        s_loaded = true;
        flex_dc_hist_load(&H, NULL, -1);
    }
}

static void hist_add_at(uint32_t utc, uint8_t kind, uint8_t score, uint8_t flags, uint8_t extra)
{
    if (!s_loaded) {
        if (s_npend < 4) {
            s_pend[s_npend].utc = utc;
            s_pend[s_npend].kind = kind;
            s_pend[s_npend].score = score;
            s_pend[s_npend].flags = flags;
            s_pend[s_npend].extra = extra;
            s_npend++;
        }
        return;
    }
    flex_dc_hist_add(&H, utc, kind, score, flags, extra);
    s_hver++;
    hist_flush();   // dcHistAdd guarda en el acto
}
static void hist_add(uint8_t kind, uint8_t score, uint8_t flags, uint8_t extra)
{
    hist_add_at(flex_clock_utc(), kind, score, flags, extra);
}

// ---- cabecera propia: barra de estado, chevron "atras" y titulo ------------------------------------------------------------------------------
static const char *title_of(int screen)
{
    int li = S.ctx.li;
    switch (screen) {
    case DC_HOME: return flex_dc_t(DCS_APPTITLE, li);
    case DC_STATUS: return flex_dc_t(DCS_STATUS, li);
    case DC_FALL: return flex_dc_t(DCS_FALL, li);
    case DC_HIST: return flex_dc_t(DCS_HIST, li);
    case DC_HEALTH: return flex_dc_t(DCS_HEALTH, li);
    case DC_OPTIM: return flex_dc_t(DCS_OPT, li);
    case DC_POST: return flex_dc_t(DCS_EVPOST, li);
    case DC_TOUCHTEST: return flex_dc_t(DCS_TOUCH, li);   // Arduino: dcDiagTick escribe "Tactil" al entrar y dcRender "Diagnostico" al repintar; se queda con el primero
    case DC_SCRTEST: return flex_dc_t(DCS_SCREEN, li);
    default: return flex_dc_t(DCS_DIAG, li);
    }
}

static void header_set(const char *title)
{
    if (S.title) {
        lv_obj_delete(S.title);
    }
    const flex_palette_t *t = flex_th();
    int fs = flex_ui_font_fit(title, 480 - 120, 3, flex_ui_text_w, NULL);
    S.title = flex_kit_text(S.hdr, title, fs, t->txt, 240, 49, true);
    lv_obj_set_clickable(S.title, false);
}

static bool back_screen(void)
{
    if (S.screen == DC_HOME) {
        return false;
    }
    // Las pruebas y el resultado vuelven a donde se lanzaron; el resto, al inicio de la app
    if (S.screen == DC_SCRTEST || S.screen == DC_TOUCHTEST || S.screen == DC_RESULT) {
        show(S.prev == DC_POST ? DC_POST : DC_HOME);
        return true;
    }
    show(DC_HOME);
    return true;
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    if (!alive() || S.susp || S.screen == DC_SCRTEST) {
        return;
    }
    if (!back_screen()) {
        flex_app_close();
    }
}

// ---- paginas ----------------------------------------------------------------------------------------------------------------------------------------
static flex_dc_page_t *page(void)
{
    if (!S.pg) {
        S.pg = big_alloc(sizeof(*S.pg));
    }
    return S.pg;
}

static uint32_t anim_elapsed(void) { return now_ms() - S.anim_t0; }

static float ring_p(void)
{
    return flex_dc_ring_p(anim_elapsed());
}

static void build_page(flex_dc_page_t *pg, int *height)
{
    flex_dc_ctx_t *c = &S.ctx;
    *height = 800;
    switch (S.screen) {
    case DC_HOME: {
        flex_dc_home_t h = {.score_ok = S.score_ok, .score = S.score, .last_check = H.last_check, .fall_on = false, .bno_avail = false};
        flex_dc_home_p(pg, c, &h, ring_p());
        break;
    }
    case DC_STATUS: {
        flex_dc_sys_t y;
        gather(&y);
        flex_dc_status(pg, c, S.m, y.loop_rate);
        break;
    }
    case DC_HEALTH:
        flex_dc_health(pg, c, S.m, ring_p());
        break;
    case DC_OPTIM: {
        const FlexMemSnap *m = flex_mem_snap();
        flex_dc_optim(pg, c, m->psTotal, m->psFree);
        break;
    }
    case DC_FALL:
        flex_dc_fall_unavailable(pg, c);
        break;
    case DC_DIAG:
    case DC_POST:
        flex_dc_diag(pg, c, S.stage, S.post, now_ms() - S.anim_t0);
        break;
    case DC_TOUCHTEST:
        flex_dc_touch_test(pg, c, S.cell, S.tt_hit, S.tt_lx, S.tt_ly);
        break;
    case DC_RESULT: {
        flex_dc_result_t r = {.res = &S.res, .rows = S.rows, .post = S.post, .last_evt_ok = false, .evt_conf = 0, .evt_peak_g = 0.0};
        flex_dc_result(pg, c, &r, anim_elapsed());
        break;
    }
    case DC_HIST: {
        // la lista entera, con el alto real: LVGL la desplaza (en Arduino se repinta con el desplazamiento)
        int pad = flex_ui_pad(c->bw, c->bh);
        int ch = flex_dc_hist_content_h(&H, pad);
        flex_dc_ctx_t t = *c;
        if (H.n > 0 && ch > c->bh) {
            t.by = 0;
            t.bh = ch;
            *height = ch;
        } else {
            t.by = 0;
            *height = c->bh;
        }
        flex_dc_hist(pg, &t, &H, 0);
        break;
    }
    case DC_SCRTEST:
        flex_dc_scr_ask(pg, c);
        break;
    default:
        pg->n = 0;
        pg->nhit = 0;
        break;
    }
}

static void keep_hits(const flex_dc_page_t *pg)
{
    S.nhit = pg->nhit;
    for (int i = 0; i < pg->nhit; i++) {
        S.hit[i].x0 = pg->hit[i].x0;
        S.hit[i].y0 = pg->hit[i].y0;
        S.hit[i].x1 = pg->hit[i].x1;
        S.hit[i].y1 = pg->hit[i].y1;
        S.hit[i].id = pg->hit[i].id;
    }
}

static int hit_at(int x, int y)
{
    for (int i = 0; i < S.nhit; i++) {
        if (x >= S.hit[i].x0 && x < S.hit[i].x1 && y >= S.hit[i].y0 && y < S.hit[i].y1) {
            return S.hit[i].id;
        }
    }
    return -1;
}

// destino del dibujo: la caja de contenido; la lista del historial va dentro de una vista que se desplaza; la pregunta del test de pantalla, en
// la capa de pantalla completa
static lv_obj_t *draw_parent(void)
{
    if (S.screen == DC_SCRTEST) {
        return S.ovl;
    }
    if (S.screen == DC_HIST && S.hscroll) {
        return S.hscroll;
    }
    return S.content;
}

static void free_view(void)
{
    if (S.view) {
        lv_obj_delete(S.view);
        S.view = NULL;
    }
}

static void build_full(void)
{
    flex_dc_page_t *pg = page();
    if (!pg || !alive()) {
        return;
    }
    int height = 800;
    int keep = (S.hscroll && S.screen == DC_HIST) ? (int)lv_obj_get_scroll_y(S.hscroll) : 0;
    free_view();
    build_page(pg, &height);
    keep_hits(pg);
    S.builds++;
    if (S.screen == DC_HIST) {
        if (!S.hscroll) {
            S.hscroll = box(S.content, BOX_X, BOX_Y, BOX_W, BOX_H);
            lv_obj_set_clickable(S.hscroll, true);
            lv_obj_set_scrollable(S.hscroll, true);
            lv_obj_set_scroll_dir(S.hscroll, LV_DIR_VER);
            lv_obj_set_scroll_elastic(S.hscroll, false);
            lv_obj_set_scroll_momentum(S.hscroll, false);
            lv_obj_set_scrollbar_mode(S.hscroll, LV_SCROLLBAR_MODE_OFF);
        }
        S.view = flex_dc_ui_render_h(S.hscroll, pg, height);
        lv_obj_update_layout(S.hscroll);
        lv_obj_scroll_to_y(S.hscroll, keep, LV_ANIM_OFF);
        return;
    }
    S.view = flex_dc_ui_render_h(draw_parent(), pg, height);
    if (S.screen != DC_SCRTEST) {
        lv_obj_move_to_index(S.view, 0);   // por debajo de la capa que recoge los toques
    }
}

// repinta lo que cambio: si solo cambian los dibujos o las cifras, solo eso; si cambia la estructura, se reconstruye
static void refresh(void)
{
    if (!alive() || S.susp) {
        return;
    }
    if (S.screen == DC_HIST) {
        build_full();   // la lista se reconstruye entera (conserva el desplazamiento)
        return;
    }
    flex_dc_page_t *pg = page();
    if (!pg) {
        return;
    }
    int height = 800;
    build_page(pg, &height);
    keep_hits(pg);
    if (S.view && flex_dc_ui_update(S.view, pg)) {
        return;
    }
    build_full();
}

// ---- test de pantalla (capa de pantalla completa) ------------------------------------------------------------------------------------------------
static void scr_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *L = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(o, &co);
    int ox = co.x1, oy = co.y1, W = 480, h = 800;
    int step = S.scr_step;
    if (step >= DCP_ASK) {
        return;   // la pregunta es una pagina normal
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;
    lv_area_t full = {ox, oy, ox + W - 1, oy + h - 1};
    static const uint32_t SOLID[6] = {0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0x808080};
    switch (step) {
    case DCP_BLACK:
    case DCP_WHITE:
    case DCP_RED:
    case DCP_GREEN:
    case DCP_BLUE:
    case DCP_GRAY:
        d.bg_color = lv_color_hex(SOLID[step]);
        lv_draw_rect(L, &d, &full);
        break;
    case DCP_GRAD: {
        // degradado vertical de gris: una linea por fila (v = y * 255 / (h - 1)) y tres rampas de color debajo
        d.bg_grad.dir = LV_GRAD_DIR_VER;
        d.bg_grad.stops_count = 2;
        d.bg_grad.stops[0].color = lv_color_black();
        d.bg_grad.stops[0].opa = LV_OPA_COVER;
        d.bg_grad.stops[0].frac = 0;
        d.bg_grad.stops[1].color = lv_color_white();
        d.bg_grad.stops[1].opa = LV_OPA_COVER;
        d.bg_grad.stops[1].frac = 255;
        lv_draw_rect(L, &d, &full);
        for (int k = 0; k < 3; k++) {
            lv_draw_rect_dsc_t r;
            lv_draw_rect_dsc_init(&r);
            r.bg_opa = LV_OPA_COVER;
            r.bg_grad.dir = LV_GRAD_DIR_VER;
            r.bg_grad.stops_count = 2;
            r.bg_grad.stops[0].color = lv_color_black();
            r.bg_grad.stops[0].opa = LV_OPA_COVER;
            r.bg_grad.stops[0].frac = 0;
            r.bg_grad.stops[1].color = k == 0 ? lv_color_make(255, 0, 0) : (k == 1 ? lv_color_make(0, 255, 0) : lv_color_make(0, 0, 255));
            r.bg_grad.stops[1].opa = LV_OPA_COVER;
            r.bg_grad.stops[1].frac = 255;
            int x0 = k * (W / 3), x1 = k == 2 ? W : (k + 1) * (W / 3);
            lv_area_t a = {ox + x0, oy + h / 2 - 90, ox + x1 - 1, oy + h / 2 - 90 + 59};
            lv_draw_rect(L, &r, &a);
        }
        break;
    }
    case DCP_PATTERN: {
        // rejilla de un pixel y tablero: delatan pixeles muertos, lineas perdidas y problemas de sincronismo
        d.bg_color = lv_color_black();
        lv_draw_rect(L, &d, &full);
        lv_draw_rect_dsc_t w;
        lv_draw_rect_dsc_init(&w);
        w.bg_opa = LV_OPA_COVER;
        w.bg_color = lv_color_white();
        for (int x = 0; x < W; x += 2) {
            lv_area_t a = {ox + x, oy, ox + x, oy + h - 1};
            lv_draw_rect(L, &w, &a);
        }
        const int cs = 40;
        for (int y = 0; y < h; y += cs) {
            for (int x = 0; x < W; x += cs) {
                if (((x / cs) + (y / cs)) & 1) {
                    lv_area_t a = {ox + x, oy + y, ox + x + cs - 1, oy + y + cs - 1};
                    lv_draw_rect(L, &w, &a);
                }
            }
        }
        lv_draw_rect_dsc_t b;
        lv_draw_rect_dsc_init(&b);
        b.bg_opa = LV_OPA_TRANSP;
        b.border_opa = LV_OPA_COVER;
        b.border_width = 2;
        b.border_color = lv_color_make(255, 0, 0);
        lv_draw_rect(L, &b, &full);
        break;
    }
    default:
        break;
    }
    // indicador de avance, siempre legible sobre cualquier fondo
    int n = DCP_ASK;
    int bwid = n * 14, bx = (W - bwid) / 2, byy = h - 130;
    lv_draw_rect_dsc_t p;
    lv_draw_rect_dsc_init(&p);
    p.radius = 12;
    p.bg_opa = LV_OPA_COVER;
    p.bg_color = lv_color_black();
    lv_area_t pa = {ox + bx - 10, oy + byy - 8, ox + bx - 10 + bwid + 20 - 1, oy + byy - 8 + 24 - 1};
    lv_draw_rect(L, &p, &pa);
    p.bg_color = lv_color_white();
    p.bg_opa = 40;
    lv_draw_rect(L, &p, &pa);
    for (int i = 0; i < n; i++) {
        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.radius = LV_RADIUS_CIRCLE;
        dot.bg_opa = LV_OPA_COVER;
        dot.bg_color = i == step ? lv_color_white() : lv_color_make(120, 120, 120);
        int r = i == step ? 5 : 3;
        int cx = bx + i * 14 + 5, cy = byy + 4;
        lv_area_t da = {ox + cx - r, oy + cy - r, ox + cx + r, oy + cy + r};
        lv_draw_rect(L, &dot, &da);
    }
}

static void scr_click_cb(lv_event_t *e);

static void scr_show(void)
{
    if (!S.ovl) {
        S.ovl = box(S.root, 0, 0, 480, 800);
        lv_obj_set_clickable(S.ovl, true);
        lv_obj_add_event_cb(S.ovl, scr_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
        lv_obj_add_event_cb(S.ovl, scr_click_cb, LV_EVENT_CLICKED, NULL);
    }
    bool ask = S.scr_step >= DCP_ASK;
    lv_obj_set_style_bg_color(S.ovl, flex_th()->win, 0);
    lv_obj_set_style_bg_opa(S.ovl, ask ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    // la pregunta lleva cabecera y barra de estado; los patrones cubren la pantalla entera
    if (S.sbar) {
        lv_obj_set_hidden(S.sbar, !ask);
    }
    lv_obj_set_hidden(S.hdr, !ask);
    if (ask) {
        header_set(title_of(DC_SCRTEST));
        lv_obj_move_foreground(S.ovl);
        lv_obj_move_foreground(S.hdr);
        if (S.sbar) {
            lv_obj_move_foreground(S.sbar);
        }
        build_full();
    } else {
        free_view();
        S.nhit = 0;
        lv_obj_move_foreground(S.ovl);
        lv_obj_invalidate(S.ovl);
    }
}

static void scr_leave(void)
{
    if (S.ovl) {
        lv_obj_delete(S.ovl);
        S.ovl = NULL;
        S.view = NULL;   // colgaba de la capa
    }
    if (S.sbar) {
        lv_obj_set_hidden(S.sbar, false);
    }
    lv_obj_set_hidden(S.hdr, false);
}

// ---- motor de diagnostico -------------------------------------------------------------------------------------------------------------------------
static void diag_enter(bool post)
{
    S.post = post;
    S.stage = DG_SYS;
    S.diag_ms = now_ms();
    flex_dc_res_reset(&S.res);
    memset(S.rows, 0, sizeof(S.rows));
    for (int i = 0; i < FLEX_DC_SYS_ROWS; i++) {
        S.rows[i].label = -1;
        S.rows[i].lit = "";
        S.rows[i].level = DCL_NA;
    }
    S.scr_step = DCP_BLACK;
    memset(S.cell, 0, sizeof(S.cell));
    S.tt_hit = 0;
    S.tt_lx = S.tt_ly = -1;
    show(post ? DC_POST : DC_DIAG);
}

static void scr_done(bool ok)
{
    S.res.screen = ok ? 1 : 0;
    scr_leave();
    S.stage = DG_TOUCH;
    S.diag_ms = now_ms();
    show(S.post ? DC_POST : DC_DIAG);
}

static void touch_done(void)
{
    S.res.touch = (int8_t)flex_dc_touch_coverage(S.tt_hit);
    S.stage = DG_DONE;
    S.diag_ms = now_ms();
    show(S.post ? DC_POST : DC_DIAG);
}

static void diag_tick(void)
{
    uint32_t now = now_ms();
    if (now - S.diag_ms < FLEX_DC_DIAG_STEP_MS) {
        if (now - S.anim_ms >= 60) {   // mientras espera late el anillo
            S.anim_ms = now;
            refresh();
        }
        return;
    }
    S.diag_ms = now;
    switch (S.stage) {
    case DG_SYS: {
        flex_mem_sample_now();
        flex_dc_sys_t y;
        gather(&y);
        flex_dc_run_system_test(&y, S.rows, &S.res.sys, &S.res.sys_ok);
        S.stage = DG_IMU;
        refresh();
        break;
    }
    case DG_IMU:
        // sin controlador del IMU (A17h) no hay nada que leer: no disponible, y no puntua ni a favor ni en contra
        S.res.imu_avail = false;
        S.res.imu_mask = 0;
        S.stage = DG_SCREEN;
        refresh();
        break;
    case DG_SCREEN:
        // las pruebas disponibles se ejecutan SOLAS: el usuario no tiene que ir a buscarlas
        S.scr_step = DCP_BLACK;
        S.screen = DC_SCRTEST;
        header_set(title_of(DC_SCRTEST));
        scr_show();
        break;
    case DG_TOUCH:
        show(DC_TOUCHTEST);
        break;
    case DG_DONE: {
        // El modelo calcula la nota final y la anota; se le da un historial de paso y el registro se pasa al de verdad (que puede estar aun
        // leyendose: entonces se guarda para ponerlo encima de lo que llegue)
        flex_dc_hist_t scratch;
        memset(&scratch, 0, sizeof(scratch));
        uint32_t utc = flex_clock_utc();
        flex_dc_result_compute(&S.res, S.post, &scratch, utc);
        if (scratch.n > 0) {
            const flex_dc_rec_t *r = &scratch.rec[0];
            hist_add_at(r->utc, r->kind, r->score, r->flags, r->extra);
        }
        show(DC_RESULT);
        break;
    }
    default:
        break;
    }
}

// ---- toques ---------------------------------------------------------------------------------------------------------------------------------------
static bool point_of(lv_event_t *e, int *x, int *y)
{
    lv_indev_t *in = lv_indev_active();
    if (!in) {
        in = lv_event_get_indev(e);
    }
    if (!in) {
        return false;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(S.root, &a);
    *x = p.x - a.x1;
    *y = p.y - a.y1;
    return true;
}

static void optim_done(void)
{
    hist_add(DC_EV_OPT, 0, 0, 0);
    if (alive()) {
        show(DC_OPTIM);   // dcOptimDone: vuelve a esta pantalla y reinicia su animacion
    }
}

static void act_for(int id)
{
    switch (id) {
    case DCH_HERO:
    case DCH_T0:
        show(DC_STATUS);
        break;
    case DCH_T1:
    case DCH_RUNDIAG:
    case DCH_HEALTH_DIAG:
        S.prev = DC_HOME;
        diag_enter(false);
        break;
    case DCH_T2:
        show(DC_FALL);
        break;
    case DCH_T3:
        show(DC_HEALTH);
        break;
    case DCH_T4:
        show(DC_OPTIM);
        break;
    case DCH_T5:
        show(DC_HIST);
        break;
    case DCH_FALLOK:
    case DCH_RES_OK:
        show(DC_HOME);
        break;
    case DCH_RES_AGAIN:
        diag_enter(S.post);
        break;
    case DCH_OPT_RUN:
        flex_optimize_open(optim_done);
        break;
    case DCH_TOUCH_END:
        touch_done();
        break;
    default:
        break;
    }
}

static void content_click_cb(lv_event_t *e)
{
    if (!alive() || S.susp) {
        return;
    }
    int x, y;
    if (!point_of(e, &x, &y)) {
        return;
    }
    S.taps++;
    act_for(hit_at(x, y));
}

static void scr_click_cb(lv_event_t *e)
{
    if (!alive() || S.susp || S.screen != DC_SCRTEST) {
        return;
    }
    if (S.scr_step < DCP_ASK) {
        S.scr_step++;
        scr_show();
        return;
    }
    int x, y;
    if (!point_of(e, &x, &y)) {
        return;
    }
    int id = hit_at(x, y);
    if (id == DCH_SCR_YES) {
        scr_done(true);
    } else if (id == DCH_SCR_NO) {
        scr_done(false);
    }
}

// El dedo enciende celdas mientras esta apoyado: tocar y arrastrar valen igual. La division es la entera de Arduino, que trunca hacia cero: un toque
// a menos de una celda por encima o a la izquierda de la rejilla cae en la celda 0 (igual que alli).
static void touch_event_cb(lv_event_t *e)
{
    if (!alive() || S.susp || S.screen != DC_TOUCHTEST) {
        return;
    }
    int x, y;
    if (!point_of(e, &x, &y)) {
        return;
    }
    flex_dc_ttgeom_t g;
    flex_dc_touch_geom(&S.ctx, &g);
    S.tt_lx = x;
    S.tt_ly = y;
    int c = (x - g.gx) / (g.cw > 0 ? g.cw : 1);
    int r = (y - g.gy) / (g.ch > 0 ? g.ch : 1);
    if (c >= 0 && c < FLEX_DC_TT_COLS && r >= 0 && r < FLEX_DC_TT_ROWS) {
        int idx = r * FLEX_DC_TT_COLS + c;
        if (!S.cell[idx]) {
            S.cell[idx] = 1;
            S.tt_hit++;
            refresh();   // solo la celda y la fila de cifras
        }
    }
}

// ---- pantallas ------------------------------------------------------------------------------------------------------------------------------------
static void show(int screen)
{
    if (!alive()) {
        return;
    }
    if (S.ovl && screen != DC_SCRTEST) {
        scr_leave();
    }
    S.ctx.li = flex_li();   // el idioma puede haber cambiado desde la ultima vez
    S.screen = screen;
    S.anim_t0 = now_ms();
    S.anim_ms = 0;
    if (screen != DC_HIST && S.hscroll) {
        lv_obj_delete(S.hscroll);   // lleva dentro la lista
        S.hscroll = NULL;
        S.view = NULL;
    }
    if (screen == DC_HOME || screen == DC_HEALTH || screen == DC_STATUS || screen == DC_OPTIM) {
        measure();
    }
    // el contenedor de toques solo recoge toques en las pantallas que los usan; la lista del historial se desplaza por si sola
    lv_obj_set_hidden(S.click, screen == DC_HIST || screen == DC_SCRTEST);
    header_set(title_of(screen));
    lv_obj_set_hidden(S.hdr, false);
    if (S.sbar) {
        lv_obj_set_hidden(S.sbar, false);
    }
    if (screen == DC_SCRTEST) {
        scr_show();
        return;
    }
    build_full();
}

// ---- reloj ------------------------------------------------------------------------------------------------------------------------------------------
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!alive() || S.susp) {
        return;
    }
    if (S.screen == DC_SCRTEST) {
        return;   // un toque significa "siguiente patron"
    }
    uint32_t now = now_ms();
    switch (S.screen) {
    case DC_DIAG:
    case DC_POST:
        diag_tick();
        return;
    case DC_HOME:
    case DC_RESULT:
        if (anim_elapsed() <= FLEX_DC_RING_MS && now - S.anim_ms >= TICK_MS) {   // barrido del anillo: solo mientras dura
            S.anim_ms = now;
            refresh();
        }
        return;
    case DC_HEALTH: {
        // barrido del anillo al entrar y, despues, un refresco por segundo: las metricas cambian despacio
        uint32_t period = anim_elapsed() < FLEX_DC_RING_MS ? TICK_MS : 1000u;
        if (now - S.anim_ms >= period) {
            S.anim_ms = now;
            if (anim_elapsed() >= FLEX_DC_RING_MS) {
                measure();
            }
            refresh();
        }
        return;
    }
    default:
        return;
    }
}

// ---- ciclo de vida ----------------------------------------------------------------------------------------------------------------------------------
static void on_create(lv_obj_t *root, const lv_area_t *area)
{
    (void)area;
    memset(&S, 0, sizeof(S));
    S.gen = ++s_gen;
    S.root = root;
    ctx_setup();
    S.ctx.mo_short = mo_short_cb;
    S.tt_lx = S.tt_ly = -1;
    flex_dc_res_reset(&S.res);
    const flex_palette_t *t = flex_th();
    // la pagina entera es de la app (cabecera propia: la barra de estado solo pone sus glifos encima)
    S.page = box(root, 0, 0, 480, 800);
    lv_obj_set_style_bg_color(S.page, t->win, 0);
    lv_obj_set_style_bg_opa(S.page, LV_OPA_COVER, 0);
    S.content = box(root, 0, 0, 480, 800);
    S.click = box(S.content, BOX_X, BOX_Y, BOX_W, BOX_H);
    lv_obj_set_clickable(S.click, true);
    lv_obj_add_event_cb(S.click, content_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(S.click, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(S.click, touch_event_cb, LV_EVENT_PRESSING, NULL);
    S.hdr = box(root, 0, 0, 480, BOX_Y);
    S.back = box(S.hdr, 0, 0, 72, BOX_Y + 1);   // el chevron: x < 72, y <= WIN_TOP
    lv_obj_set_clickable(S.back, true);
    lv_obj_add_event_cb(S.back, back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *g = flex_glyph_create(S.back, FLEX_GLYPH_CHEVRON_LEFT, 28, 28);
    lv_obj_set_pos(g, 10, 40);   // el vertice en (18, 54) y las puntas en x = 30, y = 46 / 62
    lv_obj_set_style_text_color(g, t->txt, 0);
    S.sbar = flex_statusbar_create(root, FLEX_SB_APP);
    if (S.sbar) {
        lv_obj_set_style_bg_opa(S.sbar, LV_OPA_TRANSP, 0);
    }
    S.screen = DC_HOME;
    S.prev = DC_HOME;
    hist_load_start();
    show(DC_HOME);
    S.tmr = lv_timer_create(tick_cb, TICK_MS, NULL);
    if (S.sbar) {
        lv_obj_move_foreground(S.sbar);
    }
}

static void on_suspend(void)
{
    S.susp = true;
    hist_flush();   // dcSuspend: guarda; la deteccion de caidas seguiria viva, pero aqui no hay (A17h)
    if (S.pg) {
        free(S.pg);   // se reconstruye al volver
        S.pg = NULL;
    }
}

static void on_resume(void)
{
    if (!alive()) {
        return;
    }
    S.susp = false;
    S.anim_t0 = now_ms();
    S.anim_ms = 0;
    S.diag_ms = now_ms();   // la etapa en curso retoma su tiempo
    if (S.screen == DC_SCRTEST) {
        scr_show();
    } else {
        measure();
        build_full();
    }
}

static void on_close(void)
{
    hist_flush();   // dcCloseApp
    if (S.tmr) {
        lv_timer_delete(S.tmr);
    }
    free(S.pg);
    memset(&S, 0, sizeof(S));
    s_gen++;   // lo que aun este en vuelo ya no es de nadie
}

static bool on_back(void)
{
    if (!alive()) {
        return false;
    }
    return back_screen();
}

// ---- pruebas ----------------------------------------------------------------------------------------------------------------------------------------
int flex_app_devcare_screen(void) { return alive() ? S.screen : -1; }
int flex_app_devcare_stage(void) { return alive() ? S.stage : -1; }
int flex_app_devcare_scr_step(void) { return alive() ? S.scr_step : -1; }
int flex_app_devcare_builds(void) { return S.builds; }
int flex_app_devcare_taps(void) { return S.taps; }
int flex_app_devcare_cells(void) { return alive() ? S.tt_hit : -1; }
bool flex_app_devcare_result(flex_dc_res_t *out)
{
    if (!alive() || !out) {
        return false;
    }
    *out = S.res;
    return true;
}
bool flex_app_devcare_hit_xy(int id, int *x, int *y)
{
    if (!alive()) {
        return false;
    }
    for (int i = 0; i < S.nhit; i++) {
        if (S.hit[i].id == id) {
            *x = (S.hit[i].x0 + S.hit[i].x1) / 2;
            *y = (S.hit[i].y0 + S.hit[i].y1) / 2;
            return true;
        }
    }
    return false;
}
bool flex_app_devcare_touch_cell_xy(int col, int row, int *x, int *y)
{
    if (!alive()) {
        return false;
    }
    flex_dc_ttgeom_t g;
    flex_dc_touch_geom(&S.ctx, &g);
    *x = g.gx + col * g.cw + g.cw / 2;
    *y = g.gy + row * g.ch + g.ch / 2;
    return true;
}
const flex_dc_hist_t *flex_app_devcare_hist(void) { return &H; }
bool flex_app_devcare_hist_loaded(void) { return s_loaded; }
int flex_app_devcare_writes_ok(void) { return s_wr_ok_count; }
int flex_app_devcare_writes_failed(void) { return s_wr_fail_count; }
bool flex_app_devcare_writing(void) { return s_wr_busy; }
int flex_app_devcare_scroll(void) { return S.hscroll ? (int)lv_obj_get_scroll_y(S.hscroll) : 0; }
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
bool flex_app_devcare_shown(const char *txt) { return alive() && find_text(S.root, txt); }
// Solo pruebas: olvida el historial de la RAM (como un arranque en frio)
void flex_app_devcare_reset_for_test(void)
{
    memset(&H, 0, sizeof(H));
    s_loaded = s_loading = s_wr_busy = false;
    s_hver = s_wr_ver = 0;
    s_wr_ok_count = s_wr_fail_count = 0;
    s_npend = 0;
}

const flex_app_ops_t flex_app_devcare_ops = {
    .on_create = on_create,
    .on_resume = on_resume,
    .on_suspend = on_suspend,
    .on_close = on_close,
    .on_back = on_back,
};
