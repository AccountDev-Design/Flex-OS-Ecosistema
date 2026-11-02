// Referencia: sesiones de app de la version Arduino (Session.h + AppFramework.h) tal cual, con el
// sistema de archivos, el reloj y los ganchos de las apps sustituidos por dobles de la prueba.
#include "ref_prelude.h"
#include <stdio.h>
#define APP_N 19
// ---- dobles -------------------------------------------------------------------------------
static uint32_t g_now;
static uint32_t millis() { return g_now; }
struct Touch { bool down; };
static Touch T;
static bool gFrPending;
// sistema de archivos: un unico archivo en memoria (la prueba lee y escribe el mismo)
static uint8_t g_file[2048];
static uint32_t g_file_sz;
static bool g_file_ok = true, g_fs_ready = true;
static bool flexFsReady() { return g_fs_ready; }
static bool flexFsWriteBinAtomic(const char *, const uint8_t *d, size_t n) { if (!g_file_ok) return false; memcpy(g_file, d, n); g_file_sz = (uint32_t)n; return true; }
static uint32_t flexFsSize(const char *) { return g_file_sz; }
static int flexFsReadBin(const char *, uint8_t *o, uint32_t n) { memcpy(o, g_file, n); return (int)n; }
// ganchos de apps: por app, 0 sin gancho, 1 bien, 2 falla; se anota el orden de llamadas
struct AppHooks { void (*close)(); bool (*saveSess)(); };
static int g_mode[APP_N];
static int g_calls[256], g_ncalls, g_closes;
static void hookClose() { g_closes++; }
// un gancho por app (el de Arduino no recibe el id): anota quien guarda y responde segun g_mode
#define HK(n) static bool hookSave##n() { g_calls[g_ncalls++ & 255] = n; return g_mode[n] == 1; }
HK(0) HK(1) HK(2) HK(3) HK(4) HK(5) HK(6) HK(7) HK(8) HK(9) HK(10) HK(11) HK(12) HK(13) HK(14) HK(15) HK(16) HK(17) HK(18)
#define HS(n) hookSave##n
static bool (*const HSAVE[APP_N])() = {HS(0), HS(1), HS(2), HS(3), HS(4), HS(5), HS(6), HS(7), HS(8), HS(9), HS(10), HS(11), HS(12), HS(13), HS(14), HS(15), HS(16), HS(17), HS(18)};
static AppHooks H_HOOKS[APP_N], H_NOSAVE = {hookClose, NULL};
struct AppReg { uint8_t flags; const AppHooks *hooks; };
static AppReg APP_REG[APP_N];
enum { ALIFE_CLOSED, ALIFE_RUNNING };
static int gImmApp = -1;
static void flexFeedWdt() {}
static void appMemForget(int) {}
#include "ref_session.inc"
extern "C" {
// Sesion: escribe y vuelve a leer con las dos implementaciones del formato
int ref_sess_write(uint16_t ver, uint16_t app, const void *d, size_t n, uint8_t *out) { if (!sessWrite("x", ver, app, d, n)) return -1; memcpy(out, g_file, g_file_sz); return (int)g_file_sz; }
void ref_sess_file(const uint8_t *buf, uint32_t sz) { memcpy(g_file, buf, sz); g_file_sz = sz; }
int ref_sess_read(uint16_t ver, uint16_t app, void *out, size_t max) { return (int)sessRead("x", ver, app, out, max); }
uint32_t ref_crc32(const uint8_t *d, size_t n) { return crc32u(d, n, 0); }
// Guardado diferido
void ref_sd_reset(const int *mode)
{
    gSessDirtyApp = -1; gSessDirtyMs = gSessDirtyFirstMs = 0; memset(gSessNeedSave, 0, sizeof(gSessNeedSave));
    for (int i = 0; i < APP_N; i++) { g_mode[i] = mode[i]; H_HOOKS[i].close = hookClose; H_HOOKS[i].saveSess = HSAVE[i]; APP_REG[i].hooks = mode[i] == 0 ? &H_NOSAVE : &H_HOOKS[i]; gAppState[i] = ALIFE_RUNNING; }
    g_ncalls = 0; g_closes = 0; T.down = false; gFrPending = false;
}
// op: 0 marcar · 1 tick · 2 vaciar · 3 terminar (force = b) ; devuelve (para 3) si se cerro
int ref_sd_op(int op, int a, int b, uint32_t now, bool finger, bool reset_pending, int *dirty_app, uint32_t *need_mask)
{
    g_now = now; T.down = finger; gFrPending = reset_pending;
    int r = 0;
    switch (op) {
    case 0: sessMarkDirty(a); break;
    case 1: sessAutosaveTick(); break;
    case 2: sessFlushNow(); break;
    case 3: { r = appTerminate(a, b != 0) ? 1 : 0; gAppState[a] = ALIFE_RUNNING; break; }
    }
    *dirty_app = gSessDirtyApp;
    uint32_t m = 0; for (int i = 0; i < APP_N; i++) if (gSessNeedSave[i]) m |= 1u << i;
    *need_mask = m;
    return r;
}
int ref_sd_calls(int *out) { for (int i = 0; i < g_ncalls; i++) out[i] = g_calls[i & 255]; return g_ncalls; }
void ref_sd_cur(int) {}
}
