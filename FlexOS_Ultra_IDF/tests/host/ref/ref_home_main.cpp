// Referencia: el modelo del escritorio de la version Arduino, tal cual.
#include "ref_prelude.h"
#define APP_N 19
enum { IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS, IC_NAV, IC_BRUJULA, IC_PAINT, IC_JUEGOS,
       IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE, IC_FLEXPHONE, IC_DEVCARE, IC_MUSICA };
static bool g_ref_pkg_usable = true;
static bool homePkgSlotUsable(uint8_t) { return g_ref_pkg_usable; }
// Lo que drwFavToggle/drwHideToggle hacen ademas de tocar el modelo: guardar y
// refrescar la caja (fuera de esta prueba).
static bool gHomeDirty;
static void homeOrderSave() {}
static void drwFilter() {}
static void drwClampScroll() {}
#include "ref_home.inc"

struct RefState {
    uint8_t order[HOME_TOTAL];
    uint8_t page_n, main, cols, rows, icon_sz;
    int page;
    uint8_t wg[HOME_PAGES_MAX][HOME_WG_MAX][5];
    uint8_t wg_n[HOME_PAGES_MAX];
    uint32_t fav, hidden;
};

extern "C" void ref_home_normalize(RefState *s)
{
    memcpy(homeOrder, s->order, sizeof(homeOrder));
    gHomePageN = s->page_n; gHomeMain = s->main; gHomeCols = s->cols; gHomeRows = s->rows;
    gHomeIconSz = s->icon_sz; gHomePage = s->page; gAppFav = s->fav; gAppHidden = s->hidden;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        gHomeWgN[p] = s->wg_n[p];
        for (int k = 0; k < HOME_WG_MAX; k++) {
            HomeWidget &w = gHomeWg[p][k];
            w.type = s->wg[p][k][0]; w.col = s->wg[p][k][1]; w.row = s->wg[p][k][2]; w.w = s->wg[p][k][3]; w.h = s->wg[p][k][4];
        }
    }
    homeOrderNormalize();
    memcpy(s->order, homeOrder, sizeof(homeOrder));
    s->page_n = gHomePageN; s->main = gHomeMain; s->page = gHomePage; s->fav = gAppFav; s->hidden = gAppHidden;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        s->wg_n[p] = gHomeWgN[p];
        for (int k = 0; k < HOME_WG_MAX; k++) {
            HomeWidget &w = gHomeWg[p][k];
            s->wg[p][k][0] = w.type; s->wg[p][k][1] = w.col; s->wg[p][k][2] = w.row; s->wg[p][k][3] = w.w; s->wg[p][k][4] = w.h;
        }
    }
}

extern "C" void ref_home_grid(uint8_t cols, uint8_t rows, uint8_t icon_sz, int out[8])
{
    gHomeCols = cols; gHomeRows = rows; gHomeIconSz = icon_sz;
    int S, gx0, gy0, cs, rs, c, r;
    homeGrid(S, gx0, gy0, cs, rs, c, r);
    out[0] = S; out[1] = gx0; out[2] = gy0; out[3] = cs; out[4] = rs; out[5] = c; out[6] = r; out[7] = homeDotsY();
}

extern "C" void ref_wg_rect(uint8_t cols, uint8_t rows, uint8_t icon_sz, const uint8_t w5[5], int out[4])
{
    gHomeCols = cols; gHomeRows = rows; gHomeIconSz = icon_sz;
    HomeWidget w; w.type = w5[0]; w.col = w5[1]; w.row = w5[2]; w.w = w5[3]; w.h = w5[4];
    wgRect(&w, out[0], out[1], out[2], out[3]);
}

// op 0: favorita si/no; op 1: visible si/no (sobre el estado ya normalizado)
extern "C" void ref_home_toggle(RefState *s, int id, int op)
{
    memcpy(homeOrder, s->order, sizeof(homeOrder));
    gHomePageN = s->page_n; gHomeMain = s->main; gHomeCols = s->cols; gHomeRows = s->rows;
    gHomeIconSz = s->icon_sz; gHomePage = s->page; gAppFav = s->fav; gAppHidden = s->hidden;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        gHomeWgN[p] = s->wg_n[p];
        for (int k = 0; k < HOME_WG_MAX; k++) {
            HomeWidget &w = gHomeWg[p][k];
            w.type = s->wg[p][k][0]; w.col = s->wg[p][k][1]; w.row = s->wg[p][k][2]; w.w = s->wg[p][k][3]; w.h = s->wg[p][k][4];
        }
    }
    if (op == 0) drwFavToggle(id); else drwHideToggle(id);
    memcpy(s->order, homeOrder, sizeof(homeOrder));
    s->page_n = gHomePageN; s->main = gHomeMain; s->page = gHomePage; s->fav = gAppFav; s->hidden = gAppHidden;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        s->wg_n[p] = gHomeWgN[p];
        for (int k = 0; k < HOME_WG_MAX; k++) {
            HomeWidget &w = gHomeWg[p][k];
            s->wg[p][k][0] = w.type; s->wg[p][k][1] = w.col; s->wg[p][k][2] = w.row; s->wg[p][k][3] = w.w; s->wg[p][k][4] = w.h;
        }
    }
}

// ---- Modo edicion: misma logica que la version Arduino ----------------------------------
static void ref_load(const RefState *s)
{
    memcpy(homeOrder, s->order, sizeof(homeOrder));
    gHomePageN = s->page_n; gHomeMain = s->main; gHomeCols = s->cols; gHomeRows = s->rows;
    gHomeIconSz = s->icon_sz; gHomePage = s->page; gAppFav = s->fav; gAppHidden = s->hidden;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        gHomeWgN[p] = s->wg_n[p];
        for (int k = 0; k < HOME_WG_MAX; k++) {
            HomeWidget &w = gHomeWg[p][k];
            w.type = s->wg[p][k][0]; w.col = s->wg[p][k][1]; w.row = s->wg[p][k][2]; w.w = s->wg[p][k][3]; w.h = s->wg[p][k][4];
        }
    }
}

static void ref_store(RefState *s)
{
    memcpy(s->order, homeOrder, sizeof(homeOrder));
    s->page_n = gHomePageN; s->main = gHomeMain; s->page = gHomePage; s->fav = gAppFav; s->hidden = gAppHidden;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        s->wg_n[p] = gHomeWgN[p];
        for (int k = 0; k < HOME_WG_MAX; k++) {
            HomeWidget &w = gHomeWg[p][k];
            s->wg[p][k][0] = w.type; s->wg[p][k][1] = w.col; s->wg[p][k][2] = w.row; s->wg[p][k][3] = w.w; s->wg[p][k][4] = w.h;
        }
    }
}

// op: 0 edSlotAt · 1 homeLayoutCellAt · 2 edMove · 3 homeWgAt · 4 homeWgRemove ·
//     5 homeWgToPage · 6 homeBandBot · 7 wgCanResize/wgSizeLimits · 8 celda libre (edTick)
extern "C" int ref_ed_op(RefState *s, int op, int a, int b, int c, int out[4])
{
    ref_load(s);
    int ret = 0;
    switch (op) {
    case 0: ret = edSlotAt(a, b); break;
    case 1: { int cc = -9, rr = -9; ret = homeLayoutCellAt(a, b, cc, rr); out[0] = cc; out[1] = rr; break; }
    case 2: gHomePage = a; edMove(b, c); break;
    case 3: ret = homeWgAt(a, b, c); break;
    case 4: homeWgRemove(a, b); break;
    case 5: ret = homeWgToPage(a, b, c); break;
    case 6: ret = homeBandBot(); break;
    case 7: { int w0, w1, h0, h1; wgSizeLimits(a, w0, w1, h0, h1); out[0] = w0; out[1] = w1; out[2] = h0; out[3] = h1;
              ret = wgCanResize(a); break; }
    case 8: { int dst = -1, cells2 = homeSlotCount(); uint32_t occ = homeCellMask(a, -1);
              for (int i = 0; i < cells2 && dst < 0; i++) if (!(occ & (1u << i))) dst = i;
              ret = dst; break; }
    }
    ref_store(s);
    return ret;
}
