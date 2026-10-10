// Flex OS Ultra · widgets del escritorio (wgDrawCell, Widgets.h:186; docs/spec/01a §7.3).
//
// Cada tipo se compone como en Arduino: el reloj analogico es una esfera con
// marcas y agujas, cada widget lleva su rotulo, el almacenamiento su barra.
// Los datos que cambian solos (hora, fecha, red, memoria, almacenamiento,
// cronometro) se miran cada 2 s (wgDataTick) y se cambian EN SU SITIO: solo
// el texto, la barra o la aguja, sin rehacer la pagina ni cortar un gesto.
// Solo Camara, Clima y Calendario responden a un toque (HomeCfg.h:1316): los
// demas son informativos y no fingen ser botones.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_clock.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_home_model.h"
#include "flex_home_wg.h"
#include "flex_i18n.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_system.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define WG_DATA_MS 2000u    // wgDataTick
#define WG_STO_MS  10000u   // el almacenamiento se mide como mucho cada 10 s
#define WG_PAD     12
#define DYN_MAX    64   // 5 paginas x 6 widgets x 2 datos como mucho

// Datos de otros modulos; los ponen ellos cuando existan (red: Fase 6; el
// cronometro: la app Reloj). Hasta entonces: sin red, cronometro parado.
__attribute__((weak)) bool flex_net_online(void)
{
    return false;
}
__attribute__((weak)) uint32_t flex_crono_elapsed_ms(void)
{
    return 0;
}
__attribute__((weak)) bool flex_crono_running(void)
{
    return false;
}
__attribute__((weak)) int flex_weather_loc_count(void)
{
    return 0;
}

// Textos de Clima que usa el widget (WXT de WeatherKit.h; 5 idiomas)
enum { WT_NODATA = 0, WT_RETRY, WT_ADDLOC };
static const char *const WXT[3][5] = {
    {"Sin datos meteorol\xC3\xB3gicos", "No weather data", "Pas de donn\xC3\xA9" "es m\xC3\xA9t\xC3\xA9o",
     "Sem dados meteorol\xC3\xB3gicos", "Nessun dato meteo"},
    {"Reintentar", "Retry", "R\xC3\xA9" "essayer", "Tentar de novo", "Riprova"},
    {"A\xC3\xB1" "adir ubicaci\xC3\xB3n", "Add location", "Ajouter un lieu", "Adicionar local", "Aggiungi posizione"},
};
static const char *wt(int id)
{
    return WXT[id][flex_li()];
}

// ---- datos (wgDataTick) ------------------------------------------------------------------
static struct {
    char time[16], date[48], mem[24], sto[48], cro[16];
    bool net, cro_run;
    int sto_pct;
    uint32_t sto_ms, read_ms;
    bool sto_done, read_done;
} D;

static void fmt_size(uint64_t b, char *out, size_t n)   // flexFsFmtSize
{
    if (b >= 1048576u) {
        snprintf(out, n, "%lu.%lu MB", (unsigned long)(b / 1048576u), (unsigned long)((b % 1048576u) * 10u / 1048576u));
    } else if (b >= 1024u) {
        snprintf(out, n, "%lu KB", (unsigned long)(b / 1024u));
    } else {
        snprintf(out, n, "%lu B", (unsigned long)b);
    }
}

static void crono_fmt(char *out, size_t n, uint32_t ms)   // cronoFmt(..., false)
{
    unsigned long h = ms / 3600000UL, m = (ms / 60000UL) % 60UL, s = (ms / 1000UL) % 60UL;
    if (h > 0) {
        snprintf(out, n, "%lu:%02lu:%02lu", h, m, s);
    } else {
        snprintf(out, n, "%02lu:%02lu", m, s);
    }
}

static bool home_still(void)
{
    // la medida del almacenamiento espera a un escritorio quieto (wgStorageTick)
    return flex_shell_state() == FLEX_SH_HOME && !flex_touch_arb()->t.down;
}

static void data_read(bool force_sto)
{
    struct tm tm;
    flex_clock_now(&tm);
    flex_clock_str_bar(D.time, sizeof(D.time));
    flex_date_short(D.date, sizeof(D.date), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
    D.net = flex_net_online();
    static flex_sys_snapshot_t snap;   // grande: no en la pila
    flex_system_get(&snap);
    // esp_get_free_heap_size(): con la PSRAM en el heap cuenta las dos
    snprintf(D.mem, sizeof(D.mem), "%u KB libres", (unsigned)((snap.heap_int_free + snap.heap_psram_free) / 1024u));
    uint32_t now = lv_tick_get();
    if (force_sto || !D.sto_done || (home_still() && now - D.sto_ms >= WG_STO_MS)) {
        flex_storage_status_t st;
        flex_storage_get_status(&st);
        uint64_t tot = st.fs_mounted ? st.fs_total : 0, usd = st.fs_mounted ? st.fs_used : 0;
        D.sto_pct = tot ? (int)(usd * 100 / tot) : 0;
        char a[20], b[20];
        fmt_size(usd, a, sizeof(a));
        fmt_size(tot, b, sizeof(b));
        snprintf(D.sto, sizeof(D.sto), "%s de %s", a, b);
        D.sto_ms = now;
        D.sto_done = true;
    }
    D.cro_run = flex_crono_running();
    crono_fmt(D.cro, sizeof(D.cro), flex_crono_elapsed_ms());
    D.read_ms = now;
    D.read_done = true;
}

// ---- objetos que cambian solos -----------------------------------------------------------
enum { K_TIME = 1, K_DATE, K_MEM, K_STO_TXT, K_STO_BAR, K_CRO_TITLE, K_CRO, K_CLOCK_A, K_WIFI };

typedef struct {
    lv_point_precise_t ph[2], pm[2];
    lv_obj_t *h, *m;
    int32_t cx, cy, r;
    int shown;   // hora*60 + minuto pintado
} clock_a_t;

static struct {
    lv_obj_t *obj;
    uint8_t kind;
    int32_t arg;   // K_STO_BAR: ancho de la pista
} s_dyn[DYN_MAX];
static lv_timer_t *s_tick;

static void dyn_deleted(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    for (int i = 0; i < DYN_MAX; i++) {
        if (s_dyn[i].obj == o) {
            s_dyn[i].obj = NULL;
        }
    }
}

static void dyn_add(lv_obj_t *o, uint8_t kind, int32_t arg)
{
    for (int i = 0; i < DYN_MAX; i++) {
        if (!s_dyn[i].obj) {
            s_dyn[i].obj = o;
            s_dyn[i].kind = kind;
            s_dyn[i].arg = arg;
            lv_obj_add_event_cb(o, dyn_deleted, LV_EVENT_DELETE, NULL);
            return;
        }
    }
    // no pasa (ver DYN_MAX); si pasara, ese dato se quedaria hasta el minuto
}

static void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0) {
        lv_label_set_text(l, s);
    }
}

static const char *crono_title(void)
{
    return D.cro_run ? "Cron\xC3\xB3metro en marcha" : "Cron\xC3\xB3metro";
}

static void clock_a_hands(clock_a_t *c, const struct tm *tm)
{
    int now = tm->tm_hour * 60 + tm->tm_min;
    if (now == c->shown) {
        return;
    }
    c->shown = now;
    float ah = ((float)(tm->tm_hour % 12) + (float)tm->tm_min / 60.0f) * 0.5235988f;
    float am = (float)tm->tm_min * 0.1047198f;
    c->ph[0] = (lv_point_precise_t){c->cx, c->cy};
    c->ph[1] = (lv_point_precise_t){c->cx + lroundf(sinf(ah) * (float)c->r * 0.52f),
                                    c->cy - lroundf(cosf(ah) * (float)c->r * 0.52f)};
    c->pm[0] = c->ph[0];
    c->pm[1] = (lv_point_precise_t){c->cx + lroundf(sinf(am) * (float)c->r * 0.78f),
                                    c->cy - lroundf(cosf(am) * (float)c->r * 0.78f)};
    lv_line_set_points(c->h, c->ph, 2);
    lv_line_set_points(c->m, c->pm, 2);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    bool any = false;
    for (int i = 0; i < DYN_MAX && !any; i++) {
        any = s_dyn[i].obj != NULL;
    }
    if (!any) {
        return;
    }
    data_read(false);
    struct tm tm;
    flex_clock_now(&tm);
    bool net_changed = false;
    for (int i = 0; i < DYN_MAX; i++) {
        lv_obj_t *o = s_dyn[i].obj;
        if (!o) {
            continue;
        }
        switch (s_dyn[i].kind) {
        case K_TIME: set_text(o, D.time); break;
        case K_DATE: set_text(o, D.date); break;
        case K_MEM: set_text(o, D.mem); break;
        case K_STO_TXT: set_text(o, D.sto); break;
        case K_STO_BAR: {
            int32_t fw = s_dyn[i].arg * D.sto_pct / 100;
            lv_obj_set_width(o, fw < 2 ? 2 : fw);
            break;
        }
        case K_CRO_TITLE: set_text(o, crono_title()); break;
        case K_CRO: set_text(o, D.cro); break;
        case K_CLOCK_A: clock_a_hands(lv_obj_get_user_data(o), &tm); break;
        case K_WIFI: net_changed |= D.net != (s_dyn[i].arg != 0); break;
        default: break;
        }
    }
    if (net_changed) {
        flex_home_refresh();   // la antena cambia de forma: se rehace (raro; se aplaza si hay gesto)
    }
}

// ---- piezas ------------------------------------------------------------------------------
static lv_obj_t *text(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int32_t x, int32_t y)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    lv_obj_set_x(l, x);
    flex_label_cap_y(l, y);
    return l;
}

// drawTextClip: una linea cortada en x_max
static lv_obj_t *text_clip(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int32_t x, int32_t y,
                           int32_t x_max)
{
    lv_obj_t *l = text(p, s, f, c, x, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_size(l, x_max > x ? x_max - x : 1, lv_font_get_line_height(f));
    return l;
}

static lv_obj_t *text_c(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int32_t cx, int32_t y)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    flex_label_cap_center(l, cx, y);
    return l;
}

static lv_obj_t *dot(lv_obj_t *p, int32_t cx, int32_t cy, int32_t r, lv_color_t c, lv_opa_t a)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_size(o, 2 * r + 1, 2 * r + 1);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, a, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *ring(lv_obj_t *p, int32_t cx, int32_t cy, int32_t r, lv_color_t c)
{
    lv_obj_t *o = dot(p, cx, cy, r, c, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *bar(lv_obj_t *p, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, h / 2, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *hand(lv_obj_t *p, lv_color_t c)
{
    lv_obj_t *l = lv_line_create(p);
    lv_obj_set_style_line_width(l, 2, 0);   // 2,4 y 1,8 px con AA en Arduino
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, c, 0);
    lv_obj_set_clickable(l, false);
    return l;
}

static void clock_a_free(lv_event_t *e)
{
    lv_free(lv_obj_get_user_data(lv_event_get_target_obj(e)));
}

// Reloj analogico (WG_CLOCK_A): dos circulos, 12 marcas, dos agujas y el eje
static void build_clock_a(lv_obj_t *c, int32_t w, int32_t h, lv_color_t fg, lv_color_t fg2)
{
    int32_t cx = w / 2, cy = h / 2, r = (w < h ? w : h) / 2 - 14;
    r = r < 8 ? 8 : r;
    ring(c, cx, cy, r, fg);
    ring(c, cx, cy, r - 1, fg2);
    for (int i = 0; i < 12; i++) {
        float a = (float)i * 0.5235988f;
        dot(c, cx + (int32_t)(sinf(a) * (float)(r - 6)), cy - (int32_t)(cosf(a) * (float)(r - 6)), 1, fg2,
            LV_OPA_COVER);
    }
    clock_a_t *k = lv_malloc_zeroed(sizeof(*k));
    if (!k) {
        return;
    }
    k->cx = cx;
    k->cy = cy;
    k->r = r;
    k->shown = -1;
    k->h = hand(c, fg);
    k->m = hand(c, fg);
    dot(c, cx, cy, 3, fg, LV_OPA_COVER);
    lv_obj_set_user_data(c, k);
    lv_obj_add_event_cb(c, clock_a_free, LV_EVENT_DELETE, NULL);
    struct tm tm;
    flex_clock_now(&tm);
    clock_a_hands(k, &tm);
    dyn_add(c, K_CLOCK_A, 0);
}

// Antena en miniatura (wgWifiGlyph): tres arcos de +-50 grados alrededor del
// punto (en Arduino, puntos cada 5 grados: a radio 11 se tocan) y, sin red,
// solo el punto. El radio es siempre 11: los arcos se calculan una vez.
#define WIFI_R 11
#define ARC_N  21
static lv_point_precise_t s_arc[3][ARC_N];   // relativos al punto, desplazados +rr

static void wifi_glyph(lv_obj_t *p, int32_t cx, int32_t cy, lv_color_t col, bool on)
{
    const int32_t r = WIFI_R;
    for (int k = 0; on && k < 3; k++) {
        int32_t rr = r - k * (r / 3);
        if (s_arc[k][ARC_N - 1].x == 0) {
            for (int i = 0; i < ARC_N; i++) {
                float rad = (float)(-50 + i * 5) * 0.0174533f;
                s_arc[k][i] = (lv_point_precise_t){rr + (int32_t)(sinf(rad) * (float)rr),
                                                   rr - (int32_t)(cosf(rad) * (float)rr)};
            }
        }
        lv_obj_t *l = lv_line_create(p);
        lv_line_set_points(l, s_arc[k], ARC_N);
        lv_obj_set_pos(l, cx - rr, cy + r - rr);
        lv_obj_set_style_line_width(l, 1, 0);
        lv_obj_set_style_line_color(l, col, 0);
        lv_obj_set_style_line_opa(l, 220, 0);
        lv_obj_set_clickable(l, false);
    }
    dot(p, cx, cy + r, 2, col, LV_OPA_COVER);
}

// ---- toque -----------------------------------------------------------------------------
static void wg_click_cb(lv_event_t *e)
{
    if (flex_shell_state() != FLEX_SH_HOME) {
        return;   // durante la apertura de una app o con la caja subiendo, el escritorio no manda
    }
    int app = (int)(intptr_t)lv_event_get_user_data(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    flex_app_launch(app, &a);   // con candado pide la clave antes
}

static void tappable(lv_obj_t *c, int app)
{
    lv_obj_add_event_cb(c, wg_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)app);
}

// ---- Clima de una fila (wxHomeWidget): su propio material ------------------------------
static void build_clima_row(lv_obj_t *c, int32_t h)
{
    flex_surface(c, FLEX_SURF_TINT, FLEX_BD_HOME);
    flex_surface_set_tint(c, lv_color_make(30, 72, 150));
    flex_surface_set_flat(c, lv_color_make(28, 58, 120), LV_OPA_COVER);
    // Sin descarga valida (el servicio de clima llega con el Wi-Fi): se dice
    text(c, flex_t(FLEX_S_WEATHER), FLEX_FONT_S2, FLEX_ONWALL, 16, 16);
    text(c, wt(WT_NODATA), FLEX_FONT_S1, FLEX_ONWALL2, 16, 48);
    const char *hint = flex_weather_loc_count() == 0 ? wt(WT_ADDLOC) : wt(WT_RETRY);
    text(c, hint, FLEX_FONT_S1, lv_color_mix(FLEX_WALLSURF, FLEX_ONWALL2, 90), 16, h - 26);
}

// Calendario (calWidgetBody, Home.h:563)
static void build_calend(lv_obj_t *c, int32_t w, int32_t h, lv_color_t fg, lv_color_t muted)
{
    const flex_palette_t *t = flex_th();
    struct tm tm;
    flex_clock_now(&tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d", flex_i18n_mo_short[flex_li()][tm.tm_mon], tm.tm_year + 1900);
    text(c, buf, FLEX_FONT_S2, fg, 12, 9);
    static const char *const wd1[5][7] = {
        {"D", "L", "M", "M", "J", "V", "S"}, {"S", "M", "T", "W", "T", "F", "S"},
        {"D", "L", "M", "M", "J", "V", "S"}, {"D", "S", "T", "Q", "Q", "S", "S"},
        {"D", "L", "M", "M", "G", "V", "S"},
    };
    int li = flex_li(), pad = 9, cw = (w - 2 * pad) / 7;
    for (int i = 0; i < 7; i++) {
        text_c(c, wd1[li][i], FLEX_FONT_S1, muted, pad + i * cw + cw / 2, 33);
    }
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int y = tm.tm_year + 1900, mo = tm.tm_mon;
    int dim = dm[mo] + (mo == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
    int first = ((tm.tm_wday - (tm.tm_mday - 1)) % 7 + 7) % 7;
    int grid_y = 49, rh = (h - 54) / 6;
    rh = rh < 9 ? 9 : rh;
    for (int d = 1; d <= dim; d++) {
        int cell = first + d - 1, row = cell / 7, col = cell % 7;
        int cx = pad + col * cw + cw / 2, cy = grid_y + row * rh + rh / 2;
        bool today = d == tm.tm_mday;
        if (today) {
            dot(c, cx, cy, 8, t->primary, LV_OPA_COVER);
        }
        char ds[12];
        snprintf(ds, sizeof(ds), "%d", d);
        text_c(c, ds, FLEX_FONT_S1, today ? t->on_acc : fg, cx, cy - 4);
    }
}

// ---- wgDrawCell ----------------------------------------------------------------------------
void flex_home_wg_build(lv_obj_t *page, const flex_home_wg_t *wg, int32_t band_top)
{
    if (!wg || wg->type <= FLEX_WG_NONE || wg->type >= FLEX_WG_COUNT) {
        return;
    }
    if (!s_tick) {
        s_tick = lv_timer_create(tick_cb, WG_DATA_MS, NULL);
    }
    if (!D.read_done || lv_tick_get() - D.read_ms >= 1000) {
        data_read(false);   // sin widgets el reloj de datos no lee: puede estar viejo
    }
    int x, y, w, h;
    flex_home_wg_rect(wg, &x, &y, &w, &h);
    const flex_palette_t *t = flex_th();
    lv_obj_t *c = flex_box(page);
    lv_obj_set_pos(c, x, y - band_top);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 20, 0);
    if (wg->type == FLEX_WG_CLIMA && h < 180) {
        build_clima_row(c, h);   // el Clima de una fila es el antiguo widget fijo
        tappable(c, IC_CLIMA);
        return;
    }
    // Vidrio GLASS2 / Plano SURF a225; texto con onColor del material
    flex_surface(c, FLEX_SURF_ELEVATED, FLEX_BD_HOME);
    flex_surface_set_flat(c, t->surf, 225);
    lv_color_t base = flex_look()->glass ? t->glass2 : t->surf;
    lv_color_t fg = flex_on_color_lv(base), fg2 = lv_color_mix(base, fg, 96);
    const int pad = WG_PAD;
    switch (wg->type) {
    case FLEX_WG_CLOCK:
        text(c, "Reloj", FLEX_FONT_S1, fg2, pad, 10);
        dyn_add(text(c, D.time, FLEX_FONT_S4, fg, pad, h / 2 - 10), K_TIME, 0);
        break;
    case FLEX_WG_CLOCK_A:
        build_clock_a(c, w, h, fg, fg2);
        break;
    case FLEX_WG_DATE:
        text(c, "Fecha", FLEX_FONT_S1, fg2, pad, 10);
        dyn_add(text_clip(c, D.date, FLEX_FONT_S2, fg, pad, h / 2 - 8, w - pad), K_DATE, 0);
        break;
    case FLEX_WG_WIFI:
        wifi_glyph(c, w / 2, h / 2 - 14, D.net ? fg : fg2, D.net);
        dyn_add(c, K_WIFI, D.net);
        text_c(c, D.net ? "Wi-Fi" : "Sin red", FLEX_FONT_S1, fg2, w / 2, h - 22);
        break;
    case FLEX_WG_MEM:
        text(c, "Memoria", FLEX_FONT_S1, fg2, pad, 10);
        dyn_add(text_clip(c, D.mem, FLEX_FONT_S2, fg, pad, h / 2 - 4, w - pad), K_MEM, 0);
        break;
    case FLEX_WG_STORAGE: {
        text(c, "Almacenamiento", FLEX_FONT_S1, fg2, pad, 10);
        int32_t bw = w - 2 * pad, by = h - 24;
        bar(c, pad, by, bw, 8, t->track);
        int32_t fw = bw * D.sto_pct / 100;
        dyn_add(bar(c, pad, by, fw < 2 ? 2 : fw, 8, flex_accent2()), K_STO_BAR, bw);
        dyn_add(text_clip(c, D.sto, FLEX_FONT_S1, fg, pad, h / 2 - 12, w - pad), K_STO_TXT, 0);
        break;
    }
    case FLEX_WG_CLIMA:
        text(c, "Clima", FLEX_FONT_S1, fg2, pad, 10);
        text_c(c, wt(WT_NODATA), FLEX_FONT_S2, fg2, w / 2, h / 2 - 8);
        tappable(c, IC_CLIMA);
        break;
    case FLEX_WG_CALEND:
        build_calend(c, w, h, fg, lv_color_mix(base, fg, 104));
        tappable(c, IC_CALEND);
        break;
    case FLEX_WG_CRONO:
        dyn_add(text(c, crono_title(), FLEX_FONT_S1, fg2, pad, 10), K_CRO_TITLE, 0);
        dyn_add(text(c, D.cro, FLEX_FONT_S3, fg, pad, h / 2 - 6), K_CRO, 0);
        break;
    case FLEX_WG_CAM:
        // camara de dos piezas: cuerpo redondeado y objetivo del color del material
        lv_obj_set_style_radius(bar(c, w / 2 - 15, h / 2 - 20, 30, 22, fg), 6, 0);
        dot(c, w / 2, h / 2 - 9, 7, base, LV_OPA_COVER);
        text_c(c, "C\xC3\xA1mara", FLEX_FONT_S1, fg2, w / 2, h - 22);
        tappable(c, IC_CAMARA);
        break;
    default:
        break;
    }
}
