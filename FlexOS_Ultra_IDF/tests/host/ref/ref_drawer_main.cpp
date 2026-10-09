// Referencia: el filtro y el orden de la Caja de aplicaciones de Arduino
// (drwFilter + dexMatch + pkgAppNameCmp), tal cual, con el registro de apps y
// el de paquetes sustituidos por tablas de la prueba.
#include <stdint.h>
#include <string.h>

#define APP_N 19
#define PKGAPP_MAX 24
enum DrwKind : uint8_t { DRW_NATIVE = 0, DRW_PKG = 1 };
struct DrwCell { uint8_t kind; int16_t idx; };
struct PkgAppEntry { char name[48]; };
static PkgAppEntry pkgApps[PKGAPP_MAX];
static int pkgAppsN;
static const char *s_names[APP_N];
static uint32_t s_hidden;
static uint32_t s_pkg_hidden;
static const char *appName(int id) { return s_names[id]; }
static bool appIsHidden(int id) { return (s_hidden >> id) & 1u; }
static bool pkgAppHidden(int i) { return (s_pkg_hidden >> i) & 1u; }
static void pkgAppsEnsure() {}

#include "ref_drawer.inc"

extern "C" int ref_drawer_filter(const char *const names[APP_N], uint32_t hidden, const char *const *pkg, int pkg_n,
                                 uint32_t pkg_hidden, bool show_hidden, const char *q, uint8_t *kinds, int16_t *idx)
{
    for (int i = 0; i < APP_N; i++) s_names[i] = names[i];
    s_hidden = hidden;
    pkgAppsN = pkg_n;
    for (int i = 0; i < pkg_n; i++) { strncpy(pkgApps[i].name, pkg[i], sizeof(pkgApps[i].name) - 1); pkgApps[i].name[sizeof(pkgApps[i].name) - 1] = 0; }
    s_pkg_hidden = pkg_hidden;
    drwShowHid = show_hidden;
    strncpy(drwQuery, q, DRW_QMAX); drwQuery[DRW_QMAX] = 0;
    drwQLen = (int)strlen(drwQuery);
    drwFilter();
    for (int i = 0; i < drwN; i++) { kinds[i] = drwCells[i].kind; idx[i] = drwCells[i].idx; }
    return drwN;
}
