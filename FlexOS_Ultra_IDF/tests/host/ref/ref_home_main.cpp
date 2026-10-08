// Referencia: el modelo del escritorio de la version Arduino, tal cual.
#include "ref_prelude.h"
#define APP_N 19
enum { IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS, IC_NAV, IC_BRUJULA, IC_PAINT, IC_JUEGOS,
       IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE, IC_FLEXPHONE, IC_DEVCARE, IC_MUSICA };
static bool g_ref_pkg_usable = true;
static bool homePkgSlotUsable(uint8_t) { return g_ref_pkg_usable; }
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
