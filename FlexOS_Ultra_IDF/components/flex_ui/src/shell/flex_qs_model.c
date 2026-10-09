// Flex OS Ultra · modelo del panel rapido. Ver flex_qs_model.h.
#include "flex_qs_model.h"

#include <math.h>
#include <string.h>

#define S1 FLEX_QSZ_1x1
#define S2 FLEX_QSZ_2x1
#define S4 FLEX_QSZ_4x1
#define HV (FLEX_QOR_H | FLEX_QOR_V)
#define H_ FLEX_QOR_H

// QS_REG (QuickPanel.h:533-601): nombres, tipos, tamanos, orientaciones y
// categorias. El orden ES el del enum (la tabla se indexa por id).
static const flex_qs_meta_t REG[FLEX_QS_COUNT] = {
    {"Wi-Fi", "Wi-Fi", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_CONN},
    {"Modo avi\xC3\xB3n", "Modo avi\xC3\xB3n", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_CONN},
    {"Bluetooth", "Bluetooth", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_CONN},
    {"Brillo", "Brillo", FLEX_QT_SLIDER, S4, H_, FLEX_QCAT_SCREEN},
    {"Tema", "Modo oscuro", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_SCREEN},
    {"Vidrio", "Liquid Glass", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_SCREEN},
    {"Ahorro", "Ahorro Ultra", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"Ajustes", "Ajustes", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"Conexiones", "Conectividad", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_CONN},
    {"Modo PC", "Modo PC", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"", "", FLEX_QT_ACTION, S1, H_, FLEX_QCAT_SYSTEM},   // RETIRED_10
    {"Actualizar", "Actualizaciones", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"Archivos", "Archivos", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_TOOLS},
    {"", "", FLEX_QT_ACTION, S1, H_, FLEX_QCAT_TOOLS},     // RETIRED_13
    {"C\xC3\xA1mara", "C\xC3\xA1mara", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_TOOLS},
    {"Galer\xC3\xAD" "a", "Galer\xC3\xAD" "a", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_TOOLS},
    {"Cron\xC3\xB3metro", "Cron\xC3\xB3metro", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_TOOLS},
    {"Bloquear", "Bloquear ahora", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"Apagar", "Apagar", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"Hora", "Sincronizar hora", FLEX_QT_ACTION, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"Volumen", "Volumen", FLEX_QT_SLIDER, S4, H_, FLEX_QCAT_SYSTEM},
    {"Silencio", "Silenciar", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"No molestar", "No molestar", FLEX_QT_TOGGLE, S1 | S2, HV, FLEX_QCAT_SYSTEM},
    {"Vidrio", "Intensidad del vidrio", FLEX_QT_SLIDER, S4, H_, FLEX_QCAT_SCREEN},
};

const char *const FLEX_QS_CAT_NAME[FLEX_QCAT_COUNT] = {"Conectividad", "Pantalla", "Sistema", "Herramientas"};

const flex_qs_meta_t *flex_qs_meta(int id)
{
    return (id < 0 || id >= FLEX_QS_COUNT) ? NULL : &REG[id];
}

static bool pred(flex_qs_pred_t p, int id)
{
    return id >= 0 && id < FLEX_QS_COUNT && p && p(id);
}

void flex_qs_first_size(uint8_t mask, uint8_t *w, uint8_t *h)
{
    if (mask & FLEX_QSZ_1x1) {
        *w = 1, *h = 1;
    } else if (mask & FLEX_QSZ_2x1) {
        *w = 2, *h = 1;
    } else if (mask & FLEX_QSZ_4x1) {
        *w = 4, *h = 1;
    } else if (mask & FLEX_QSZ_2x2) {
        *w = 2, *h = 2;
    } else {
        *w = 1, *h = 1;
    }
}

bool flex_qs_size_allowed(int id, int w, int h)
{
    const flex_qs_meta_t *c = flex_qs_meta(id);
    if (!c) {
        return false;
    }
    if (w == 1 && h == 1) {
        return (c->sizes & FLEX_QSZ_1x1) != 0;
    }
    if (w == 2 && h == 1) {
        return (c->sizes & FLEX_QSZ_2x1) != 0;
    }
    if (w == 4 && h == 1) {
        return (c->sizes & FLEX_QSZ_4x1) != 0;
    }
    if (w == 2 && h == 2) {
        return (c->sizes & FLEX_QSZ_2x2) != 0;
    }
    return false;
}

bool flex_qs_next_size(int id, int w, int h, int dir, uint8_t *nw, uint8_t *nh)
{
    static const uint8_t SW[4] = {1, 2, 4, 2};
    static const uint8_t SH[4] = {1, 1, 1, 2};
    int cur = -1;
    for (int i = 0; i < 4; i++) {
        if (SW[i] == w && SH[i] == h) {
            cur = i;
            break;
        }
    }
    if (cur < 0) {
        cur = 0;
    }
    for (int step = 1; step <= 4; step++) {
        int k = cur + dir * step;
        if (k < 0 || k > 3) {
            break;
        }
        if (flex_qs_size_allowed(id, SW[k], SH[k])) {
            *nw = SW[k];
            *nh = SH[k];
            return true;
        }
    }
    return false;
}

// QP_FACTORY (QuickPanel.h:683-708)
static const uint8_t FACTORY[][3] = {
    {FLEX_QS_WIFI, 2, 1},     {FLEX_QS_AIRPLANE, 2, 1}, {FLEX_QS_THEME, 1, 1},    {FLEX_QS_POWERSAVE, 1, 1},
    {FLEX_QS_GLASS, 1, 1},    {FLEX_QS_CRONO, 1, 1},    {FLEX_QS_NTP, 1, 1},      {FLEX_QS_DND, 1, 1},
    {FLEX_QS_LOCK, 1, 1},     {FLEX_QS_FILES, 1, 1},    {FLEX_QS_CAMERA, 1, 1},   {FLEX_QS_GALLERY, 1, 1},
    {FLEX_QS_SETTINGS, 1, 1}, {FLEX_QS_BRIGHT, 4, 1},   {FLEX_QS_VOLUME, 4, 1},   {FLEX_QS_DEX, 2, 1},
    {FLEX_QS_OTA, 2, 1},      {FLEX_QS_CONN, 2, 1},
};
#define FACTORY_N ((int)(sizeof(FACTORY) / sizeof(FACTORY[0])))

static void push_factory(flex_qs_cfg_t *c, int i)
{
    int id = FACTORY[i][0];
    uint8_t w = FACTORY[i][1], h = FACTORY[i][2];
    if (!flex_qs_size_allowed(id, w, h)) {
        flex_qs_first_size(REG[id].sizes, &w, &h);
    }
    c->it[c->n] = (flex_qs_item_t){(uint8_t)id, w, h, FLEX_QOR_H, 1};
    c->n++;
}

void flex_qs_factory(flex_qs_cfg_t *c, flex_qs_pred_t keep)
{
    c->n = 0;
    for (int i = 0; i < FACTORY_N && c->n < FLEX_QS_MAX_ITEMS; i++) {
        if (pred(keep, FACTORY[i][0])) {
            push_factory(c, i);
        }
    }
    c->grows = 3;
}

uint8_t flex_qs_normalize(flex_qs_item_t *it, uint8_t n, uint8_t *grows, flex_qs_pred_t keep)
{
    bool seen[FLEX_QS_COUNT] = {false};
    uint8_t out = 0;
    for (uint8_t i = 0; i < n && i < FLEX_QS_MAX_ITEMS; i++) {
        int id = it[i].id;
        if (id >= FLEX_QS_COUNT || seen[id] || !pred(keep, id)) {
            continue;
        }
        uint8_t w = it[i].w, h = it[i].h;
        if (!flex_qs_size_allowed(id, w, h)) {
            flex_qs_first_size(REG[id].sizes, &w, &h);
        }
        uint8_t ori = it[i].ori ? it[i].ori : FLEX_QOR_H;
        if (!(REG[id].oris & ori)) {
            ori = (REG[id].oris & FLEX_QOR_H) ? FLEX_QOR_H : FLEX_QOR_V;
        }
        seen[id] = true;
        it[out] = (flex_qs_item_t){(uint8_t)id, w, h, ori, (uint8_t)(it[i].vis ? 1 : 0)};
        out++;
    }
    if (*grows < FLEX_QP_GROWS_MIN) {
        *grows = FLEX_QP_GROWS_MIN;
    }
    if (*grows > FLEX_QP_GROWS_MAX) {
        *grows = FLEX_QP_GROWS_MAX;
    }
    return out;
}

void flex_qs_adopt_new(flex_qs_cfg_t *c, uint8_t from_ver, flex_qs_pred_t keep)
{
    if (from_ver >= FLEX_QS_CFG_VER) {
        return;
    }
    bool have[FLEX_QS_COUNT] = {false};
    for (uint8_t i = 0; i < c->n; i++) {
        if (c->it[i].id < FLEX_QS_COUNT) {
            have[c->it[i].id] = true;
        }
    }
    for (int i = 0; i < FACTORY_N && c->n < FLEX_QS_MAX_ITEMS; i++) {
        int id = FACTORY[i][0];
        if (have[id] || !pred(keep, id)) {
            continue;
        }
        push_factory(c, i);
        have[id] = true;
    }
}

void flex_qs_serialize(const flex_qs_cfg_t *c, uint8_t b[FLEX_QS_BLOB_N])
{
    memset(b, 0, FLEX_QS_BLOB_N);
    b[0] = 'Q';
    b[1] = FLEX_QS_CFG_VER;
    b[2] = c->n;
    b[3] = c->grows;
    int o = 4;
    for (int i = 0; i < FLEX_QS_MAX_ITEMS; i++) {
        b[o++] = c->it[i].id;
        b[o++] = c->it[i].w;
        b[o++] = c->it[i].h;
        b[o++] = c->it[i].ori;
        b[o++] = c->it[i].vis;
    }
}

bool flex_qs_deserialize(flex_qs_cfg_t *c, const uint8_t *b, flex_qs_pred_t keep)
{
    if (b[0] != 'Q') {
        return false;
    }
    uint8_t ver = b[1], n = b[2], gr = b[3];
    if (ver == 0 || ver > FLEX_QS_CFG_VER || n > FLEX_QS_MAX_ITEMS) {
        return false;
    }
    int o = 4;
    for (int i = 0; i < FLEX_QS_MAX_ITEMS; i++) {
        c->it[i] = (flex_qs_item_t){b[o], b[o + 1], b[o + 2], b[o + 3], b[o + 4]};
        o += 5;
    }
    c->n = n;
    c->grows = gr;
    c->n = flex_qs_normalize(c->it, c->n, &c->grows, keep);
    flex_qs_adopt_new(c, ver, keep);
    c->n = flex_qs_normalize(c->it, c->n, &c->grows, keep);
    return c->n > 0;
}

bool flex_qs_load(flex_qs_cfg_t *c, const uint8_t *b, size_t rd, flex_qs_pred_t keep)
{
    bool from_nvs = rd == FLEX_QS_BLOB_N && b && flex_qs_deserialize(c, b, keep);
    if (!from_nvs) {
        flex_qs_factory(c, keep);
        c->n = flex_qs_normalize(c->it, c->n, &c->grows, keep);
    }
    if (c->n == 0) {
        flex_qs_factory(c, keep);
        c->n = flex_qs_normalize(c->it, c->n, &c->grows, keep);
    }
    return from_nvs;
}

// ---- maquetacion ---------------------------------------------------------------
static void emit(flex_qs_layout_t *L, uint8_t kind, int item, int x, int y, int w, int h)
{
    L->blk[L->blk_n] = (flex_qs_block_t){kind, (int8_t)item, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    L->blk_n++;
}

void flex_qs_layout(flex_qs_layout_t *L, const flex_qs_item_t *src, int n, int group_px, bool edit,
                    flex_qs_pred_t shown)
{
    L->blk_n = 0;
    L->tile_n = 0;
    L->group_blk = -1;
    L->content_h = 0;
    if (!src) {
        return;
    }
    for (int i = 0; i < n && i < FLEX_QS_MAX_ITEMS; i++) {
        if (src[i].vis && pred(shown, src[i].id) && src[i].w == 1 && L->tile_n < FLEX_QS_MAX_ITEMS) {
            L->tiles[L->tile_n++] = (uint8_t)i;
        }
    }
    int y = 0, col = 0, row_h = 0;
    bool group_done = L->tile_n == 0;
    for (int i = 0; i < n && i < FLEX_QS_MAX_ITEMS && L->blk_n < FLEX_QS_MAX_ITEMS + 2; i++) {
        const flex_qs_item_t *it = &src[i];
        if (!it->vis || !pred(shown, it->id)) {
            continue;
        }
        if (it->w == 1) {
            if (group_done) {
                continue;
            }
            if (col > 0) {
                y += row_h + FLEX_QP_VGAP;
                col = 0;
                row_h = 0;
            }
            L->group_blk = L->blk_n;
            emit(L, FLEX_QB_GROUP, i, FLEX_QP_MX, y, FLEX_QP_CONT_W, group_px);
            y += group_px + FLEX_QP_VGAP;
            group_done = true;
            continue;
        }
        int w = it->w > 4 ? 4 : it->w;
        int bh = it->h >= 2 ? FLEX_QP_RH2 : FLEX_QP_RH1;
        if (col + w > 4) {
            y += row_h + FLEX_QP_VGAP;
            col = 0;
            row_h = 0;
        }
        emit(L, FLEX_QB_ITEM, i, flex_qp_col_x(col), y, flex_qp_span_w(w), bh);
        col += w;
        if (bh > row_h) {
            row_h = bh;
        }
        if (col >= 4) {
            y += row_h + FLEX_QP_VGAP;
            col = 0;
            row_h = 0;
        }
    }
    if (col > 0) {
        y += row_h + FLEX_QP_VGAP;
    }
    if (!group_done && L->blk_n < FLEX_QS_MAX_ITEMS + 2) {
        L->group_blk = L->blk_n;
        emit(L, FLEX_QB_GROUP, -1, FLEX_QP_MX, y, FLEX_QP_CONT_W, group_px);
        y += group_px + FLEX_QP_VGAP;
    }
    if (edit && L->blk_n < FLEX_QS_MAX_ITEMS + 2) {
        emit(L, FLEX_QB_ADD, -1, FLEX_QP_MX, y, FLEX_QP_CONT_W, FLEX_QP_RH1);
        y += FLEX_QP_RH1 + FLEX_QP_VGAP;
    }
    L->content_h = y > 0 ? y - FLEX_QP_VGAP : 0;
}

int flex_qs_total_rows(const flex_qs_layout_t *L)
{
    return (L->tile_n + 3) / 4;
}

int flex_qs_group_inner_h(int group_px)
{
    int h = group_px - FLEX_QP_GPAD - FLEX_QP_HANDLE_H;
    return h > 0 ? h : 0;
}

int flex_qs_group_min_px(void)
{
    return flex_qp_group_h(FLEX_QP_GROWS_MIN);
}

int flex_qs_group_max_px(const flex_qs_layout_t *L)
{
    int rows = flex_qs_total_rows(L);
    rows = rows < FLEX_QP_GROWS_MIN ? FLEX_QP_GROWS_MIN : rows > FLEX_QP_GROWS_MAX ? FLEX_QP_GROWS_MAX : rows;
    return flex_qp_group_h(rows);
}

int flex_qs_relayout(flex_qs_layout_t *L, const flex_qs_item_t *src, int n, int group_px, bool edit,
                     flex_qs_pred_t shown)
{
    flex_qs_layout(L, src, n, group_px, edit, shown);
    int lo = flex_qs_group_min_px(), hi = flex_qs_group_max_px(L);
    int want = group_px < lo ? lo : group_px > hi ? hi : group_px;
    if (want != group_px) {
        flex_qs_layout(L, src, n, want, edit, shown);
    }
    return want;
}

int flex_qs_scroll_max(const flex_qs_layout_t *L)
{
    int m = L->content_h - FLEX_QP_VIEW_H;
    return m > 0 ? m : 0;
}

int flex_qs_gscroll_max(const flex_qs_layout_t *L, int group_px)
{
    int m = flex_qs_total_rows(L) * FLEX_QP_TROW - flex_qs_group_inner_h(group_px);
    return m > 0 ? m : 0;
}

int flex_qs_group_snap(const flex_qs_layout_t *L, float gh, uint8_t *rows_out)
{
    int lo = flex_qs_group_min_px(), hi = flex_qs_group_max_px(L);
    int best = lo, bd = 0x7FFFFFFF;
    int rmax = flex_qs_total_rows(L);
    rmax = rmax > FLEX_QP_GROWS_MAX ? FLEX_QP_GROWS_MAX : rmax;
    rmax = rmax < FLEX_QP_GROWS_MIN ? FLEX_QP_GROWS_MIN : rmax;
    for (int r = FLEX_QP_GROWS_MIN; r <= rmax; r++) {
        int hpx = flex_qp_group_h(r);
        int d = (int)fabsf(gh - (float)hpx);
        if (d < bd) {
            bd = d;
            best = hpx;
            if (rows_out) {
                *rows_out = (uint8_t)r;
            }
        }
    }
    best = best < lo ? lo : best > hi ? hi : best;
    return best;
}

uint32_t flex_qs_group_snap_ms(float from, float to)
{
    int dist = (int)fabsf(to - from);
    uint32_t d = 140 + (uint32_t)(dist * 2);
    return d > 420 ? 420 : d;
}

void flex_qs_tile_center(int k, int gy_top, int *cx, int *cy)
{
    int r = k / 4, c = k % 4;
    *cx = FLEX_QP_MX + FLEX_QP_GPAD + c * FLEX_QP_TCOLW + FLEX_QP_TCOLW / 2;
    *cy = gy_top + r * FLEX_QP_TROW + FLEX_QP_TCIRC / 2;
}

// ---- editor ---------------------------------------------------------------------
bool flex_qs_edit_remove(flex_qs_cfg_t *c, int idx)
{
    if (idx < 0 || idx >= c->n || c->n <= 1) {
        return false;
    }
    for (int i = idx; i < c->n - 1; i++) {
        c->it[i] = c->it[i + 1];
    }
    c->n--;
    c->it[c->n].id = 0;
    c->it[c->n].vis = 0;
    return true;
}

void flex_qs_edit_move(flex_qs_cfg_t *c, int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= c->n || to >= c->n) {
        return;
    }
    flex_qs_item_t tmp = c->it[from];
    if (from < to) {
        for (int i = from; i < to; i++) {
            c->it[i] = c->it[i + 1];
        }
    } else {
        for (int i = from; i > to; i--) {
            c->it[i] = c->it[i - 1];
        }
    }
    c->it[to] = tmp;
}

bool flex_qs_edit_add(flex_qs_cfg_t *c, int id, flex_qs_pred_t shown)
{
    if (c->n >= FLEX_QS_MAX_ITEMS || !pred(shown, id)) {
        return false;
    }
    for (int i = 0; i < c->n; i++) {
        if (c->it[i].id == id) {
            return false;
        }
    }
    uint8_t w, h;
    flex_qs_first_size(REG[id].sizes, &w, &h);
    c->it[c->n] = (flex_qs_item_t){(uint8_t)id, w, h, (uint8_t)((REG[id].oris & FLEX_QOR_H) ? FLEX_QOR_H : FLEX_QOR_V), 1};
    c->n++;
    return true;
}

int flex_qs_catalog(const flex_qs_cfg_t *c, uint8_t out[FLEX_QS_COUNT], flex_qs_pred_t shown)
{
    int n = 0;
    for (int id = 0; id < FLEX_QS_COUNT; id++) {
        if (!pred(shown, id)) {
            continue;
        }
        bool present = false;
        for (int i = 0; i < c->n; i++) {
            if (c->it[i].id == id) {
                present = true;
                break;
            }
        }
        if (!present) {
            out[n++] = (uint8_t)id;
        }
    }
    return n;
}
