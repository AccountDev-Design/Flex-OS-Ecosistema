// Referencia: Personalizar inicio de la version Arduino (HomeCfg.h) tal cual: operaciones sobre el
// modelo, selector de imagenes y clasificador de pellizco (hpzUpdate), con el tacto y el
// almacenamiento sustituidos por dobles controlados por la prueba.
#include "ref_prelude.h"
#include <stdio.h>
#include <string.h>
#define APP_N 19
enum { IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS, IC_NAV, IC_BRUJULA, IC_PAINT, IC_JUEGOS,
       IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE, IC_FLEXPHONE, IC_DEVCARE, IC_MUSICA };
#define APP_DEF_FAV 1
struct { uint8_t dflt; } APP_REG[APP_N] = {{1},{1},{1},{1},{1},{1},{1},{1},{1},{1},{0},{0},{0},{0},{0},{1},{0},{1},{0}};
static bool homePkgSlotUsable(uint8_t) { return true; }
static bool gHomeDirty;
static void homeOrderSave() {}
// hcInfo: el aviso modal
static char hcMsg[72];
static int hcModal;
static void hcInfo(const char *m) { snprintf(hcMsg, sizeof(hcMsg), "%s", m); hcModal = 3; }
// Almacenamiento: tres carpetas con las entradas que la prueba ponga
#define FLEXFS_NAME_MAX 48
struct FlexFsEntry { char name[FLEXFS_NAME_MAX]; uint32_t size; bool dir; uint16_t items; };
static FlexFsEntry g_ents[3][24];
static int g_ent_n[3];
static bool flexFsReady() { return true; }
static int flexFsList(const char *dir, FlexFsEntry *e, int cap)
{
    int d = !strcmp(dir, "/Imagenes") ? 0 : !strcmp(dir, "/Camara") ? 1 : 2;
    int n = g_ent_n[d] < cap ? g_ent_n[d] : cap;
    memcpy(e, g_ents[d], (size_t)n * sizeof(FlexFsEntry));
    return n;
}
// Tacto
static uint32_t g_now;
static uint32_t millis() { return g_now; }
static uint8_t gtFingers;
static uint32_t gtFingersMs;
#define KB_MAXPOINTS 5
struct KbPoint { bool active; int x, y, id; };
static KbPoint gKbPoints[KB_MAXPOINTS];
static int g_poll_n, g_poll_calls;
static int gtPollMulti() { g_poll_calls++; return g_poll_n; }
static bool g_in_home, g_in_cfg;
static bool hpzAllowedHome() { return g_in_home; }
static bool hpzAllowedCfg() { return g_in_cfg; }
static int g_act;
static void hcEnter() { g_act = 1; }
static void hcBeginExit() { g_act = 2; }
#include "ref_homecfg.inc"

struct RefState {
    uint8_t order[HOME_TOTAL];
    uint8_t page_n, main, cols, rows, icon_sz;
    int page;
    uint8_t wg[HOME_PAGES_MAX][HOME_WG_MAX][5];
    uint8_t wg_n[HOME_PAGES_MAX];
    uint32_t fav, hidden;
};
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
    s->page_n = gHomePageN; s->main = gHomeMain; s->cols = gHomeCols; s->rows = gHomeRows; s->icon_sz = gHomeIconSz;
    s->page = gHomePage; s->fav = gAppFav; s->hidden = gAppHidden;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        s->wg_n[p] = gHomeWgN[p];
        for (int k = 0; k < HOME_WG_MAX; k++) {
            HomeWidget &w = gHomeWg[p][k];
            s->wg[p][k][0] = w.type; s->wg[p][k][1] = w.col; s->wg[p][k][2] = w.row; s->wg[p][k][3] = w.w; s->wg[p][k][4] = w.h;
        }
    }
}
// op: 0 homePageItems · 1 homePageAdd · 2 homePageDelete · 3 homePageSwap · 4 homeSetMain · 5 homeSetGrid ·
//     6 homeResetLayout · 7 homeWgAdd · 8 hcGridIdx · 9 reordenar (hcPagesTouch:912-918, transcrito)
// msg: 0 sin aviso, 1 "Maximo de 5 paginas", 2 "No puedes eliminar la ultima pagina"
extern "C" int ref_hc_op(RefState *s, int op, int a, int b, int *msg)
{
    ref_load(s);
    hcModal = 0; hcMsg[0] = 0;
    int ret = 0;
    switch (op) {
    case 0: ret = homePageItems(a); break;
    case 1: ret = homePageAdd(); break;
    case 2: ret = homePageDelete(a); break;
    case 3: homePageSwap(a, b); break;
    case 4: homeSetMain(a); break;
    case 5: homeSetGrid(a, b); break;
    case 6: homeResetLayout(); break;
    case 7: ret = homeWgAdd(a, b); break;
    case 8: ret = hcGridIdx(); break;
    case 9: {   // a = tarjeta, b = deslizamiento en decimas de pixel (slide = b / 10)
        const int HC_STEP = 216 + 24;
        float hcSlide = (float)b / 10.0f;
        int hcReorder = a;
        int dst = hcReorder - (int)((hcSlide + (hcSlide < 0 ? -HC_STEP / 2 : HC_STEP / 2)) / HC_STEP);
        if (dst < 0) dst = 0;
        if (dst > gHomePageN - 1) dst = gHomePageN - 1;
        while (dst > hcReorder) { homePageSwap(hcReorder, hcReorder + 1); hcReorder++; }
        while (dst < hcReorder) { homePageSwap(hcReorder, hcReorder - 1); hcReorder--; }
        ret = hcReorder;
        break;
    }
    }
    *msg = !strcmp(hcMsg, "Maximo de 5 paginas") ? 1 : !strcmp(hcMsg, "No puedes eliminar la ultima pagina") ? 2 : 0;
    ref_store(s);
    return ret;
}
// Selector de imagenes: ents[d] = entradas de la carpeta d (0 /Imagenes, 1 /Camara, 2 /Descargas)
extern "C" void ref_hc_fs_set(int d, int n, const char names[][48], const uint8_t *is_dir)
{
    g_ent_n[d] = n;
    for (int i = 0; i < n; i++) {
        memset(&g_ents[d][i], 0, sizeof(FlexFsEntry));
        snprintf(g_ents[d][i].name, sizeof(g_ents[d][i].name), "%s", names[i]);
        g_ents[d][i].dir = is_dir[i] != 0;
    }
}
extern "C" int ref_hc_scan(char paths[24][80])
{
    hcScanImages();
    for (int i = 0; i < hcPickN; i++) memcpy(paths[i], hcPickPath[i], 80);
    return hcPickN;
}
// Pellizco: un paso del bucle (tiempo, dedos que dice el chip, contactos activos)
extern "C" void ref_hc_pz_reset()
{
    hpzReset(); hpzSwallow = false; g_act = 0; g_poll_calls = 0;
}
extern "C" int ref_hc_pz_step(uint32_t now, int mode, int fingers, uint32_t fingers_ms, int n, const int pts[][3],
                              int *swallow, int *state, int *calls)
{
    g_now = now; g_in_home = mode == 1; g_in_cfg = mode == 2;
    gtFingers = (uint8_t)fingers; gtFingersMs = fingers_ms;
    g_poll_n = n; g_act = 0; g_poll_calls = 0;
    for (int i = 0; i < KB_MAXPOINTS; i++) gKbPoints[i].active = false;
    for (int i = 0; i < n && i < KB_MAXPOINTS; i++) {
        gKbPoints[i].active = true; gKbPoints[i].x = pts[i][0]; gKbPoints[i].y = pts[i][1]; gKbPoints[i].id = pts[i][2];
    }
    hpzUpdate();
    *swallow = hpzSwallowing(); *state = hpzState; *calls = g_poll_calls;
    return g_act;
}
