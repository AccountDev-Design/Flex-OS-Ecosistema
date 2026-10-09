#include "flex_home_model.h"

#include <string.h>
#include "flex_app_ids.h"
#include "flex_storage.h"

#define EMPTY FLEX_HOME_EMPTY

// Widgets.h:60 (limites elegidos por contenido)
const flex_home_wg_desc_t flex_home_wg_reg[FLEX_WG_COUNT] = {
    {"", "", 1, 1, 1, 1, 1, 1, 0},
    {"Reloj digital", "Reloj", 2, 1, 2, 4, 1, 2, 0},
    {"Reloj anal\xC3\xB3gico", "Reloj", 2, 2, 1, 2, 1, 2, 70},
    {"Fecha", "Reloj", 2, 1, 2, 4, 1, 1, 0},
    {"Wi-Fi", "Sistema", 1, 1, 1, 2, 1, 1, 0},
    {"Memoria", "Sistema", 2, 1, 2, 4, 1, 1, 0},
    {"Almacenamiento", "Sistema", 2, 1, 2, 4, 1, 1, 0},
    {"", "", 1, 1, 1, 1, 1, 1, 0},
    {"Cron\xC3\xB3metro", "Reloj", 2, 1, 2, 4, 1, 1, 0},
    {"C\xC3\xA1mara", "Accesos", 1, 1, 1, 2, 1, 1, 0},
    {"Clima", "Informaci\xC3\xB3n", 2, 2, 2, 4, 1, 2, 116},
    {"Calendario", "Informaci\xC3\xB3n", 2, 1, 2, 4, 1, 3, 110},
};

const uint8_t flex_home_dock[FLEX_HOME_DOCK_N] = {IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE};

// Home.h:69-102
static const uint8_t k_factory[FLEX_HOME_LEGACY_SLOTS] = {
    IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS,
    IC_FLEXSTORE, IC_NAV, IC_BRUJULA, IC_DEVCARE, IC_PAINT, IC_JUEGOS,
};
#define APPREG_V1_N 22
static const uint8_t k_map_v1[APPREG_V1_N] = {
    IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS, IC_FLEXSTORE, IC_NAV, IC_BRUJULA,
    IC_DEVCARE, IC_PAINT, IC_JUEGOS, IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE,
    IC_FLEXPHONE, IC_DEVCARE, IC_BRUJULA, 0xFF,
};
#define APPREG_V2_N 19
static const uint8_t k_map_v2[APPREG_V2_N] = {
    IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS, IC_NAV, IC_BRUJULA, IC_PAINT,
    IC_JUEGOS, IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE, IC_FLEXPHONE, IC_DEVCARE,
    IC_BRUJULA,
};

flex_home_t g_home;
static int s_app_n = FLEX_APP_N;
static uint32_t s_factory_fav = 0x0FFF;
static uint32_t s_pkg_seen;
static bool pkg_usable_default(int slot) { (void)slot; return true; }
static bool (*s_pkg_usable)(int) = pkg_usable_default;

void flex_home_set_registry(int app_n, uint32_t factory_fav)
{
    s_app_n = app_n;
    s_factory_fav = factory_fav;
}

void flex_home_set_pkg_usable(bool (*fn)(int))
{
    s_pkg_usable = fn ? fn : pkg_usable_default;
}

static void defaults(void)
{
    memset(&g_home, 0, sizeof(g_home));
    memset(g_home.order, EMPTY, sizeof(g_home.order));
    memcpy(g_home.order, k_factory, sizeof(k_factory));
    g_home.page_n = FLEX_HOME_LEGACY_PAGES;
    g_home.cols = 4;
    g_home.rows = 3;
    g_home.icon_sz = 1;
    g_home.labels = true;
    g_home.dots = true;
    g_home.pinch = true;
    g_home.fav = 0x0FFF;
}

int flex_home_slot_count(void)
{
    return (int)g_home.cols * (int)g_home.rows;
}

// homeGrid (Home.h:672)
void flex_home_grid(int *S, int *gx0, int *gy0, int *cstep, int *rstep, int *cols, int *rows)
{
    static const int k_icon_px[3] = {60, 72, 84};
    int c = g_home.cols < 4 ? 4 : g_home.cols > FLEX_HOME_COLS_MAX ? FLEX_HOME_COLS_MAX : g_home.cols;
    int r = g_home.rows < 3 ? 3 : g_home.rows > FLEX_HOME_ROWS_MAX ? FLEX_HOME_ROWS_MAX : g_home.rows;
    int s = k_icon_px[g_home.icon_sz > 2 ? 1 : g_home.icon_sz];
    int cs = 480 / c, rs = (r >= 4) ? 92 : FLEX_HOME_ROWSTEP;
    if (s > cs - 20) {
        s = cs - 20;
    }
    if (s > rs - 26) {
        s = rs - 26;
    }
    if (s < 44) {
        s = 44;
    }
    *S = s;
    *gx0 = (cs - s) / 2;
    *gy0 = FLEX_HOME_GY0;
    *cstep = cs;
    *rstep = rs;
    *cols = c;
    *rows = r;
}

int flex_home_dots_y(void)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    return gy0 + (rows - 1) * rs + S + (rows >= 4 ? 24 : 34);
}

void flex_home_slot_xy(int slot, int *x, int *y)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    if (cols < 1) {
        cols = 1;
    }
    *x = gx0 + (slot % cols) * cs;
    *y = gy0 + (slot / cols) * rs;
}

// wgRect (Widgets.h:174)
void flex_home_wg_rect(const flex_home_wg_t *w, int *x, int *y, int *ww, int *hh)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    *x = w->col * cs + 8;
    *ww = w->w * cs - 16;
    int r_top = w->row, r_bot = w->row + (w->h > 0 ? w->h : 1) - 1;
    *y = (r_top == 0) ? FLEX_HOME_HDR_Y : gy0 + (r_top - 1) * rs - 6;
    int y_end = (r_bot == 0) ? FLEX_HOME_HDR_Y + FLEX_HOME_HDR_H : gy0 + r_bot * rs - 18;
    *hh = y_end - *y;
    if (*ww < 24) {
        *ww = 24;
    }
    if (*hh < 24) {
        *hh = 24;
    }
}

// homeCellMask (Widgets.h:276)
uint32_t flex_home_cell_mask(int page, int skip_wg)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    int n = flex_home_slot_count();
    if (page < 0 || page >= FLEX_HOME_PAGES_MAX) {
        return 0xFFFFFFFFu;
    }
    uint32_t m = 0;
    for (int i = 0; i < n; i++) {
        if (g_home.order[flex_home_idx(page, i)] != EMPTY) {
            m |= (1u << i);
        }
    }
    for (int k = 0; k < g_home.wg_n[page] && k < FLEX_HOME_WG_MAX; k++) {
        if (k == skip_wg) {
            continue;
        }
        const flex_home_wg_t *w = &g_home.wg[page][k];
        if (w->type == FLEX_WG_NONE) {
            continue;
        }
        for (int r = w->row; r < w->row + w->h; r++) {
            for (int c = w->col; c < w->col + w->w; c++) {
                if (r < 1 || r > rows || c < 0 || c >= cols) {
                    continue;
                }
                int i = (r - 1) * cols + c;
                if (i < n) {
                    m |= (1u << i);
                }
            }
        }
    }
    return m;
}

static uint32_t hdr_mask(int page, int skip_wg)
{
    if (page < 0 || page >= FLEX_HOME_PAGES_MAX) {
        return 0xFFFFFFFFu;
    }
    uint32_t m = 0;
    for (int k = 0; k < g_home.wg_n[page] && k < FLEX_HOME_WG_MAX; k++) {
        if (k == skip_wg) {
            continue;
        }
        const flex_home_wg_t *w = &g_home.wg[page][k];
        if (w->type == FLEX_WG_NONE || w->row != 0) {
            continue;
        }
        for (int c = w->col; c < w->col + w->w && c < 32; c++) {
            m |= (1u << c);
        }
    }
    return m;
}

static bool wg_fits(int page, int c, int r, int w, int h, int skip_wg)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    if (w < 1 || h < 1 || c < 0 || r < 0 || c + w > cols || r + h > rows + 1) {
        return false;
    }
    uint32_t m = flex_home_cell_mask(page, skip_wg);
    uint32_t hm = (r == 0) ? hdr_mask(page, skip_wg) : 0u;
    for (int rr = r; rr < r + h; rr++) {
        for (int cc = c; cc < c + w; cc++) {
            if (rr == 0) {
                if (hm & (1u << cc)) {
                    return false;
                }
            } else if (m & (1u << ((rr - 1) * cols + cc))) {
                return false;
            }
        }
    }
    return true;
}

static bool wg_free_of_widgets(int page, int c, int r, int w, int h, int up_to)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    if (w < 1 || h < 1 || c < 0 || r < 0 || c + w > cols || r + h > rows + 1) {
        return false;
    }
    for (int k = 0; k < up_to && k < FLEX_HOME_WG_MAX; k++) {
        const flex_home_wg_t *o = &g_home.wg[page][k];
        if (o->type == FLEX_WG_NONE) {
            continue;
        }
        if (c < o->col + o->w && o->col < c + w && r < o->row + o->h && o->row < r + h) {
            return false;
        }
    }
    return true;
}

static bool wg_size_ok(int type, int r, int w, int h)
{
    if (type <= FLEX_WG_NONE || type >= FLEX_WG_COUNT || type == FLEX_WG_RETIRED_7) {
        return false;
    }
    const flex_home_wg_desc_t *d = &flex_home_wg_reg[type];
    if (w < d->min_w || w > d->max_w || h < d->min_h || h > d->max_h) {
        return false;
    }
    if (d->min_px_h) {
        flex_home_wg_t t = {(uint8_t)type, 0, (uint8_t)r, (uint8_t)w, (uint8_t)h};
        int x, y, ww, hh;
        flex_home_wg_rect(&t, &x, &y, &ww, &hh);
        if (hh < d->min_px_h) {
            return false;
        }
    }
    return true;
}

static bool wg_place_ok(int page, int type, int c, int r, int w, int h, int skip_wg)
{
    return wg_size_ok(type, r, w, h) && wg_fits(page, c, r, w, h, skip_wg);
}

// homeWgNormalize (Widgets.h:447)
static void wg_normalize(void)
{
    int S, gx0, gy0, cs, rs, cols, rows;
    flex_home_grid(&S, &gx0, &gy0, &cs, &rs, &cols, &rows);
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        if (p >= g_home.page_n) {
            g_home.wg_n[p] = 0;
        }
        if (g_home.wg_n[p] > FLEX_HOME_WG_MAX) {
            g_home.wg_n[p] = FLEX_HOME_WG_MAX;
        }
        uint8_t n = 0, total = g_home.wg_n[p];
        for (int k = 0; k < total; k++) {
            flex_home_wg_t w = g_home.wg[p][k];
            if (w.type <= FLEX_WG_NONE || w.type >= FLEX_WG_COUNT || w.type == FLEX_WG_RETIRED_7) {
                continue;
            }
            const flex_home_wg_desc_t *d = &flex_home_wg_reg[w.type];
            if (w.w < d->min_w) w.w = d->min_w;
            if (w.w > d->max_w) w.w = d->max_w;
            if (w.h < d->min_h) w.h = d->min_h;
            if (w.h > d->max_h) w.h = d->max_h;
            bool ok = wg_size_ok(w.type, w.row, w.w, w.h) && wg_free_of_widgets(p, w.col, w.row, w.w, w.h, n);
            if (!ok) {
                uint8_t tw[3] = {w.w, d->min_w, d->min_w}, th[3] = {w.h, d->min_h, (uint8_t)(d->min_h + 1)};
                for (int t = 0; t < 3 && !ok; t++) {
                    if (th[t] > d->max_h) {
                        continue;
                    }
                    for (int r = 0; r + th[t] <= rows + 1 && !ok; r++) {
                        for (int c = 0; c + tw[t] <= cols && !ok; c++) {
                            if (wg_size_ok(w.type, r, tw[t], th[t]) && wg_free_of_widgets(p, c, r, tw[t], th[t], n)) {
                                w.col = (uint8_t)c;
                                w.row = (uint8_t)r;
                                w.w = tw[t];
                                w.h = th[t];
                                ok = true;
                            }
                        }
                    }
                }
            }
            if (!ok) {
                continue;
            }
            g_home.wg[p][n++] = w;
        }
        g_home.wg_n[p] = n;
        for (int k = n; k < FLEX_HOME_WG_MAX; k++) {
            g_home.wg[p][k].type = FLEX_WG_NONE;
        }
    }
}

static void wg_factory(void)
{
    int p = (g_home.main < g_home.page_n) ? g_home.main : 0;
    static const uint8_t T[2] = {FLEX_WG_CLIMA, FLEX_WG_CALEND};
    for (int i = 0; i < 2; i++) {
        if (g_home.wg_n[p] >= FLEX_HOME_WG_MAX) {
            break;
        }
        int c = i * 2;
        if (!wg_place_ok(p, T[i], c, 0, 2, 1, -1)) {
            continue;
        }
        flex_home_wg_t *d = &g_home.wg[p][g_home.wg_n[p]++];
        d->type = T[i];
        d->col = (uint8_t)c;
        d->row = 0;
        d->w = 2;
        d->h = 1;
    }
}

static void wg_serialize(uint8_t *b)
{
    memset(b, 0, FLEX_HOME_WG_BLOB);
    b[0] = 'W';
    b[1] = 2;
    int o = 2;
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        b[o++] = g_home.wg_n[p];
        for (int k = 0; k < FLEX_HOME_WG_MAX; k++) {
            b[o++] = g_home.wg[p][k].type;
            b[o++] = g_home.wg[p][k].col;
            b[o++] = g_home.wg[p][k].row;
            b[o++] = g_home.wg[p][k].w;
            b[o++] = g_home.wg[p][k].h;
        }
    }
}

static bool wg_parse(const uint8_t *b, uint8_t ver, int per_page, int row_add)
{
    if (b[0] != 'W' || b[1] != ver) {
        return false;
    }
    flex_home_wg_t tmp[FLEX_HOME_PAGES_MAX][FLEX_HOME_WG_MAX];
    uint8_t cnt[FLEX_HOME_PAGES_MAX];
    memset(tmp, 0, sizeof(tmp));
    int o = 2;
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        cnt[p] = b[o++];
        if (cnt[p] > per_page) {
            return false;
        }
        for (int k = 0; k < per_page; k++) {
            uint8_t ty = b[o++], c = b[o++], r = b[o++], w = b[o++], h = b[o++];
            if (ty >= FLEX_WG_COUNT) {
                return false;
            }
            if (w > FLEX_HOME_COLS_MAX || h > FLEX_HOME_ROWS_MAX + 1) {
                return false;
            }
            if (ty != FLEX_WG_NONE) {
                r = (uint8_t)(r + row_add);
                if (c + w > FLEX_HOME_COLS_MAX || r + h > FLEX_HOME_ROWS_MAX + 1) {
                    return false;
                }
            }
            tmp[p][k] = (flex_home_wg_t){ty, c, r, w, h};
        }
    }
    memcpy(g_home.wg, tmp, sizeof(g_home.wg));
    memcpy(g_home.wg_n, cnt, sizeof(g_home.wg_n));
    return true;
}

static int first_free(void)
{
    int n = flex_home_slot_count();
    for (int p = 0; p < g_home.page_n; p++) {
        uint32_t m = flex_home_cell_mask(p, -1);
        for (int i = 0; i < n; i++) {
            if (!(m & (1u << i))) {
                return flex_home_idx(p, i);
            }
        }
    }
    return -1;
}

static bool page_append_quiet(void)
{
    if (g_home.page_n >= FLEX_HOME_PAGES_MAX) {
        return false;
    }
    for (int i = 0; i < FLEX_HOME_STRIDE; i++) {
        g_home.order[flex_home_idx(g_home.page_n, i)] = EMPTY;
    }
    g_home.wg_n[g_home.page_n] = 0;
    g_home.page_n++;
    return true;
}

static int first_free_grow(void)
{
    int slot = first_free();
    if (slot >= 0) {
        return slot;
    }
    if (!page_append_quiet()) {
        return -1;
    }
    return first_free();
}

int flex_home_first_free(void)
{
    return first_free();
}

int flex_home_first_free_grow(void)
{
    return first_free_grow();
}

bool flex_app_can_hide(int id)
{
    return id != IC_AJUSTES;
}

bool flex_home_fav_toggle(int id)
{
    if (id < 0 || id >= s_app_n) {
        return false;
    }
    if (flex_app_is_fav(id)) {
        g_home.fav &= ~(1u << id);
        for (int i = 0; i < FLEX_HOME_TOTAL; i++) {
            if (g_home.order[i] == (uint8_t)id) {
                g_home.order[i] = EMPTY;
            }
        }
    } else {
        if (flex_app_is_hidden(id)) {
            return false;   // una app oculta no puede estar en Inicio
        }
        int slot = first_free_grow();   // sin hueco se crea pagina
        if (slot < 0) {
            return false;   // maximo de paginas y todas llenas
        }
        g_home.fav |= 1u << id;
        g_home.order[slot] = (uint8_t)id;
    }
    flex_home_normalize();
    return true;
}

bool flex_home_hide_toggle(int id)
{
    if (id < 0 || id >= s_app_n || !flex_app_can_hide(id)) {
        return false;
    }
    if (flex_app_is_hidden(id)) {
        g_home.hidden &= ~(1u << id);
    } else {
        g_home.hidden |= 1u << id;
        g_home.fav &= ~(1u << id);   // fuera de la caja: tambien fuera de Inicio
        for (int i = 0; i < FLEX_HOME_TOTAL; i++) {
            if (g_home.order[i] == (uint8_t)id) {
                g_home.order[i] = EMPTY;
            }
        }
    }
    flex_home_normalize();
    return true;
}

static bool pkg_seen(uint8_t v)
{
    int n = flex_home_is_pkg(v) ? v - FLEX_HOME_PKG_BASE : -1;
    return n >= 0 && n < 32 && (s_pkg_seen & (1u << n));
}

static void pkg_mark(uint8_t v)
{
    int n = flex_home_is_pkg(v) ? v - FLEX_HOME_PKG_BASE : -1;
    if (n >= 0 && n < 32) {
        s_pkg_seen |= (1u << n);
    }
}

static bool pkg_usable(uint8_t v)
{
    return s_pkg_usable(v - FLEX_HOME_PKG_BASE);
}

// homeOrderNormalize (Home.h:1646)
void flex_home_normalize(void)
{
    s_pkg_seen = 0;
    if (g_home.page_n < 1) {
        g_home.page_n = 1;
    }
    wg_normalize();
    if (g_home.page_n > FLEX_HOME_PAGES_MAX) {
        g_home.page_n = FLEX_HOME_PAGES_MAX;
    }
    if (g_home.main >= g_home.page_n) {
        g_home.main = 0;
    }
    if (g_home.page < 0) {
        g_home.page = 0;
    }
    if (g_home.page >= g_home.page_n) {
        g_home.page = g_home.main;
    }
    int n = flex_home_slot_count();
    bool seen[32] = {false};
    uint8_t rescue[FLEX_HOME_TOTAL];
    int nres = 0;
    for (int p = 0; p < FLEX_HOME_PAGES_MAX; p++) {
        for (int i = 0; i < FLEX_HOME_STRIDE; i++) {
            int k = flex_home_idx(p, i);
            if (p < g_home.page_n && i < n) {
                continue;
            }
            if (g_home.order[k] != EMPTY) {
                rescue[nres++] = g_home.order[k];
                g_home.order[k] = EMPTY;
            }
        }
    }
    for (int p = 0; p < g_home.page_n; p++) {
        for (int i = 0; i < n; i++) {
            int k = flex_home_idx(p, i);
            uint8_t v = g_home.order[k];
            if (v == EMPTY) {
                continue;
            }
            g_home.order[k] = EMPTY;
            bool under_wg = (flex_home_cell_mask(p, -1) & (1u << i)) != 0;
            g_home.order[k] = v;
            if (under_wg) {
                rescue[nres++] = v;
                g_home.order[k] = EMPTY;
                continue;
            }
            if (flex_home_is_pkg(v)) {
                if (!pkg_usable(v) || pkg_seen(v)) {
                    g_home.order[k] = EMPTY;
                    continue;
                }
                pkg_mark(v);
                continue;
            }
            if (v >= s_app_n || seen[v] || !flex_app_is_fav(v) || flex_app_is_hidden(v)) {
                g_home.order[k] = EMPTY;
                continue;
            }
            seen[v] = true;
        }
    }
    for (int r = 0; r < nres; r++) {
        uint8_t v = rescue[r];
        if (flex_home_is_pkg(v)) {
            if (!pkg_usable(v) || pkg_seen(v)) {
                continue;
            }
            int slot = first_free_grow();
            if (slot < 0) {
                continue;
            }
            g_home.order[slot] = v;
            pkg_mark(v);
            continue;
        }
        if (v >= s_app_n || seen[v] || !flex_app_is_fav(v) || flex_app_is_hidden(v)) {
            continue;
        }
        int slot = first_free_grow();
        if (slot < 0) {
            g_home.fav &= ~(1u << v);
            continue;
        }
        g_home.order[slot] = v;
        seen[v] = true;
    }
    for (int id = 0; id < s_app_n; id++) {
        if (!flex_app_is_fav(id) || flex_app_is_hidden(id) || seen[id]) {
            continue;
        }
        int slot = first_free_grow();
        if (slot < 0) {
            g_home.fav &= ~(1u << id);
            continue;
        }
        g_home.order[slot] = (uint8_t)id;
        seen[id] = true;
    }
}

// homeOrderLoad (Home.h:1718)
void flex_home_load(void)
{
    defaults();
    int32_t pgn = flex_cfg_get_i32("hpgn", -1);
    int32_t pmn = flex_cfg_get_i32("hpmain", 0);
    int32_t grid = flex_cfg_get_i32("hgrid", -1);
    int32_t icsz = flex_cfg_get_i32("hicon", 1);
    int32_t hfl = flex_cfg_get_i32("hflag", -1);
    size_t n = flex_cfg_get_blob("hordq", g_home.order, FLEX_HOME_TOTAL);
    if (n != FLEX_HOME_TOTAL) {
        uint8_t legacy[FLEX_HOME_LEGACY_PAGES * FLEX_HOME_LEGACY_SLOTS];
        size_t ln = flex_cfg_get_blob("hordp", legacy, sizeof(legacy));
        memset(g_home.order, EMPTY, sizeof(g_home.order));
        if (ln == sizeof(legacy)) {
            for (int p = 0; p < FLEX_HOME_LEGACY_PAGES; p++) {
                for (int i = 0; i < FLEX_HOME_LEGACY_SLOTS; i++) {
                    g_home.order[flex_home_idx(p, i)] = legacy[p * FLEX_HOME_LEGACY_SLOTS + i];
                }
            }
            n = FLEX_HOME_TOTAL;
        } else if (flex_cfg_get_blob("hord", legacy, FLEX_HOME_LEGACY_SLOTS) == FLEX_HOME_LEGACY_SLOTS) {
            for (int i = 0; i < FLEX_HOME_LEGACY_SLOTS; i++) {
                g_home.order[flex_home_idx(0, i)] = legacy[i];
            }
            n = FLEX_HOME_TOTAL;
        }
    }
    int32_t fav = flex_cfg_get_i32("appfav", -1);
    int32_t hide = flex_cfg_get_i32("apphide", -1);
    int32_t known = flex_cfg_get_i32("appn", 16);
    int32_t regver = flex_cfg_get_i32("appver", 1);
    // Candados: 32 bits (Arduino los leia como uint16_t y perdia los de las
    // apps 16..18; aqui se conservan todos).
    g_home.lock = (uint32_t)flex_cfg_get_i32("applockm", 0);
    bool wg_migrate = false;
    {
        uint8_t wb[FLEX_HOME_WG_BLOB];
        size_t wn = flex_cfg_get_blob("hwg2", wb, sizeof(wb));
        if (wn != FLEX_HOME_WG_BLOB || !wg_parse(wb, 2, FLEX_HOME_WG_MAX, 0)) {
            wg_migrate = true;
            uint8_t w1[FLEX_HOME_WG_BLOB_V1];
            size_t n1 = flex_cfg_get_blob("hwg", w1, sizeof(w1));
            if (n1 != FLEX_HOME_WG_BLOB_V1 || !wg_parse(w1, 1, FLEX_HOME_WG_MAX_V1, 1)) {
                memset(g_home.wg, 0, sizeof(g_home.wg));
                memset(g_home.wg_n, 0, sizeof(g_home.wg_n));
            }
        }
    }
    g_home.page_n = (pgn >= 1 && pgn <= FLEX_HOME_PAGES_MAX) ? (uint8_t)pgn : FLEX_HOME_LEGACY_PAGES;
    g_home.main = (pmn >= 0 && pmn < g_home.page_n) ? (uint8_t)pmn : 0;
    if (grid >= 0) {
        int c = (grid >> 8) & 0xFF, r = grid & 0xFF;
        g_home.cols = (c >= 4 && c <= FLEX_HOME_COLS_MAX) ? (uint8_t)c : 4;
        g_home.rows = (r >= 3 && r <= FLEX_HOME_ROWS_MAX) ? (uint8_t)r : 3;
    }
    g_home.icon_sz = (icsz >= 0 && icsz <= 2) ? (uint8_t)icsz : 1;
    if (hfl >= 0) {
        g_home.labels = (hfl & 1) != 0;
        g_home.locked = (hfl & 2) != 0;
        g_home.dots = (hfl & 4) != 0;
        g_home.pinch = (hfl & 8) != 0;
        g_home.reduce = (hfl & 16) != 0;
    }
    g_home.page = g_home.main;
    if (fav < 0) {
        g_home.fav = s_factory_fav;
        g_home.hidden = 0;
    } else {
        g_home.fav = (uint32_t)fav;
        g_home.hidden = (uint32_t)(hide < 0 ? 0 : hide);
        if (known < s_app_n) {
            for (int id = known < 0 ? 0 : known; id < s_app_n; id++) {
                if (s_factory_fav & (1u << id)) {
                    g_home.fav |= (1u << id);
                }
            }
        }
    }
    if (n != FLEX_HOME_TOTAL) {
        memset(g_home.order, EMPTY, sizeof(g_home.order));
        for (int i = 0; i < FLEX_HOME_LEGACY_SLOTS; i++) {
            g_home.order[flex_home_idx(0, i)] = k_factory[i];
        }
    }
    if (regver < FLEX_HOME_APPREG_VER && fav >= 0) {
        const uint8_t *map = (regver <= 1) ? k_map_v1 : k_map_v2;
        const int map_n = (regver <= 1) ? APPREG_V1_N : APPREG_V2_N;
        const int old_ide = (regver <= 1) ? 8 : 7;
        for (int i = 0; i < FLEX_HOME_TOTAL; i++) {
            uint8_t v = g_home.order[i];
            if (v == EMPTY || flex_home_is_pkg(v)) {
                continue;
            }
            g_home.order[i] = (v < map_n) ? map[v] : EMPTY;
            if (g_home.order[i] == 0xFF) {
                g_home.order[i] = EMPTY;
            }
        }
        uint32_t nf = 0, nh = 0, nl = 0;
        for (int v = 0; v < map_n; v++) {
            uint8_t d = map[v];
            if (d >= s_app_n) {
                continue;
            }
            uint32_t src = 1u << v, dst = 1u << d;
            if (g_home.fav & src) nf |= dst;
            if (g_home.hidden & src) nh |= dst;
            if (v != old_ide && (g_home.lock & src)) nl |= dst;
        }
        g_home.fav = nf;
        g_home.hidden = nh;
        g_home.lock = nl;
        if (regver <= 1) {
            g_home.fav |= (1u << IC_FLEXSTORE) | (1u << IC_DEVCARE);
            g_home.hidden &= ~((1u << IC_FLEXSTORE) | (1u << IC_DEVCARE));
        }
        g_home.fav |= (1u << IC_BRUJULA);
        g_home.hidden &= ~(1u << IC_BRUJULA);
    }
    uint32_t valid = (s_app_n >= 32) ? 0xFFFFFFFFu : ((1u << s_app_n) - 1u);
    g_home.fav &= valid;
    g_home.hidden &= valid;
    g_home.lock &= valid;
    g_home.hidden &= ~(1u << IC_AJUSTES);   // Ajustes nunca oculto
    if (wg_migrate) {
        wg_factory();
    }
    flex_home_normalize();
    if (wg_migrate) {
        uint8_t wb[FLEX_HOME_WG_BLOB];
        wg_serialize(wb);
        flex_cfg_set_blob("hwg2", wb, FLEX_HOME_WG_BLOB);
    }
}

// homeOrderSave (Home.h:1540). "hordp" y "hord" no se reescriben: quedan para
// poder volver a una version anterior.
void flex_home_save(void)
{
    flex_cfg_set_blob("hordq", g_home.order, FLEX_HOME_TOTAL);
    flex_cfg_set_i32("hpgn", g_home.page_n);
    flex_cfg_set_i32("hpmain", g_home.main);
    flex_cfg_set_i32("hgrid", ((int)g_home.cols << 8) | g_home.rows);
    flex_cfg_set_i32("hicon", g_home.icon_sz);
    flex_cfg_set_i32("hflag", (g_home.labels ? 1 : 0) | (g_home.locked ? 2 : 0) | (g_home.dots ? 4 : 0) |
                                  (g_home.pinch ? 8 : 0) | (g_home.reduce ? 16 : 0));
    uint8_t wb[FLEX_HOME_WG_BLOB];
    wg_serialize(wb);
    flex_cfg_set_blob("hwg2", wb, FLEX_HOME_WG_BLOB);
    flex_cfg_set_i32("appn", s_app_n);
    flex_cfg_set_i32("appver", FLEX_HOME_APPREG_VER);
    flex_cfg_set_i32("appfav", (int32_t)g_home.fav);
    flex_cfg_set_i32("apphide", (int32_t)g_home.hidden);
}
