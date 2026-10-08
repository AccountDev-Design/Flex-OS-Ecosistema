// Referencia: homeOrderLoad de la version Arduino con un Preferences simulado
// sobre la MISMA cache de ajustes que lee flex_home_load (stub_cfg.c).
#include "ref_prelude.h"
#include <stddef.h>
extern "C" {
int32_t flex_cfg_get_i32(const char *k, int32_t d);
bool flex_cfg_get_bool(const char *k, bool d);
size_t flex_cfg_get_blob(const char *k, void *o, size_t c);
int flex_cfg_set_i32(const char *k, int32_t v);
int flex_cfg_set_blob(const char *k, const void *d, size_t l);
}
struct Preferences {
    bool begin(const char *, bool) { return true; }
    void end() {}
    int getInt(const char *k, int d) { return flex_cfg_get_i32(k, d); }
    bool getBool(const char *k, bool d) { return flex_cfg_get_bool(k, d); }
    size_t getBytes(const char *k, void *b, size_t n) { return flex_cfg_get_blob(k, b, n); }
    size_t putBytes(const char *k, const void *b, size_t n) { flex_cfg_set_blob(k, b, n); return n; }
    size_t putInt(const char *k, int v) { flex_cfg_set_i32(k, v); return 4; }
};
static Preferences prefs;
#define APP_N 19
enum { IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS, IC_NAV, IC_BRUJULA, IC_PAINT, IC_JUEGOS,
       IC_AJUSTES, IC_CALC, IC_CALEND, IC_CAMARA, IC_CLIMA, IC_FLEXSTORE, IC_FLEXPHONE, IC_DEVCARE, IC_MUSICA };
#define APP_DEF_FAV 1
struct { uint8_t dflt; } APP_REG[APP_N] = {{1},{1},{1},{1},{1},{1},{1},{1},{1},{1},{0},{0},{0},{0},{0},{1},{0},{1},{0}};
static bool homePkgSlotUsable(uint8_t) { return true; }
#include "ref_home_load.inc"

extern "C" void ref_home_load(uint8_t order[HOME_TOTAL], uint8_t misc[6], uint32_t masks[3], uint8_t wg[157])
{
    // Estado de arranque (los valores con que se inicializan las globales)
    static const uint8_t init_order[12] = {IC_RELOJ, IC_GALERIA, IC_MULTIMEDIA, IC_ALMACEN, IC_MODOPC, IC_NOTAS,
                                           IC_FLEXSTORE, IC_NAV, IC_BRUJULA, IC_DEVCARE, IC_PAINT, IC_JUEGOS};
    memset(homeOrder, 0, sizeof(homeOrder));
    memcpy(homeOrder, init_order, 12);
    gHomePage = 0; gHomePageN = HOME_LEGACY_PAGES; gHomeMain = 0; gHomeCols = 4; gHomeRows = 3; gHomeIconSz = 1;
    gHomeLabels = true; gHomeLocked = false; gHomeDots = true; gHomePinch = true; gHomeReduce = false;
    memset(gHomeWg, 0, sizeof(gHomeWg)); memset(gHomeWgN, 0, sizeof(gHomeWgN));
    gAppFav = 0x0FFF; gAppHidden = 0;
    gAppLock = (uint16_t)prefs.getInt("applockm", 0);   // Prefs.h:276 (lo carga cfgLoadPrefs, antes)
    homeOrderLoad();
    memcpy(order, homeOrder, HOME_TOTAL);
    misc[0] = gHomePageN; misc[1] = gHomeMain; misc[2] = gHomeCols; misc[3] = gHomeRows; misc[4] = gHomeIconSz;
    misc[5] = (uint8_t)gHomePage;
    masks[0] = gAppFav; masks[1] = gAppHidden; masks[2] = gAppLock;
    homeWgSerialize(wg);
}
