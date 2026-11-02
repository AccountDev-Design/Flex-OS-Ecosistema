// Referencia: huella, soltar, admitir, alivio y desalojo de memoria de la version Arduino (Core.h) tal cual,
// con la plataforma sustituida por un "mundo" que define la prueba (test_mem_model.c).
#include "ref_prelude.h"
#include <stdio.h>
#include "FlexOS_Mem.h"
#define APP_N 19
extern "C" {
// mundo (lo define el codigo C de la prueba): cada funcion actua sobre el mundo ACTUAL
uint32_t w_free_psram(void);
void w_sample_now(void);
const FlexMemSnap *w_snap(void);
uint32_t w_shed_system(void);
bool w_bg_busy(int app);
bool w_has_shed(int app);
size_t w_app_shed(int app);
bool w_radio_busy(void);
bool w_terminate(int app);
void w_drop_card(int app);
void w_notify(const char *t, const char *s);
int w_card_count(void);
int w_card_app(int c);
void w_drop_card_at(int c);
}
enum { ALIFE_CLOSED, ALIFE_RUNNING, ALIFE_SUSPENDED };   // el orden de Arduino (Types.h)
static uint32_t g_now;
static uint32_t millis() { return g_now; }
static bool gSafeMode, gFrPending;
enum { ST_HOME, ST_APP, ST_OTHER };
static int gState = ST_HOME;
static bool g_tr_visible, g_opt_active;
static bool appTrVisible() { return g_tr_visible; }
static bool optActive() { return g_opt_active; }
static bool wifiRadioBusy() { return w_radio_busy(); }
static void sysNotify(const char *t, const char *s) { w_notify(t, s); }
static const char *appName(int) { return "app"; }
static int swCardCount() { return w_card_count(); }
static int swCardApp(int c) { return w_card_app(c); }
static void swDropCard(int c) { w_drop_card_at(c); }
#define FLEXDIAG_WIFI(x) do {} while (0)
// ---- Serial.printf de Arduino ----
struct SerialT { template <typename... A> void printf(const char *, A...) {} } Serial;
// ---- sustituciones de Core.h: medir y soltar ----
static uint32_t memFreePsram() { return w_free_psram(); }
static void memSampleNow() { w_sample_now(); }
static const FlexMemSnap *memSnap() { return w_snap(); }
static uint32_t memShedSystem() { return w_shed_system(); }
static FlexMemAlerts gMemAlerts;
static int g_weight;
static int appWeight(int) { return g_weight; }
// ---- ganchos de las apps: la prueba decide quien tiene shed y trabajo en segundo plano ----
struct AppHooks { bool (*bgWork)(); size_t (*shed)(); bool (*dirty)(); };
#define HK(n) static bool bg##n() { return w_bg_busy(n); } static size_t sh##n() { return w_app_shed(n); }
HK(0) HK(1) HK(2) HK(3) HK(4) HK(5) HK(6) HK(7) HK(8) HK(9) HK(10) HK(11) HK(12) HK(13) HK(14) HK(15) HK(16) HK(17) HK(18)
#define H2(n) {bg##n, sh##n, NULL}
static AppHooks H_ALL[APP_N] = {H2(0), H2(1), H2(2), H2(3), H2(4), H2(5), H2(6), H2(7), H2(8), H2(9), H2(10), H2(11), H2(12), H2(13), H2(14), H2(15), H2(16), H2(17), H2(18)};
#define H3(n) {bg##n, NULL, NULL}
static AppHooks H_BGONLY[APP_N] = {H3(0), H3(1), H3(2), H3(3), H3(4), H3(5), H3(6), H3(7), H3(8), H3(9), H3(10), H3(11), H3(12), H3(13), H3(14), H3(15), H3(16), H3(17), H3(18)};
enum { APP_BG_KEEP = 1 };
struct AppReg { uint8_t flags; const AppHooks *hooks; };
static AppReg APP_REG[APP_N];
static bool appTerminate(int id, bool);
// ---- extraido ----
#include "ref_mem.inc"
static bool appTerminate(int id, bool) { if (!w_terminate(id)) return false; gAppState[id] = ALIFE_CLOSED; return true; }
extern "C" {
// bg_flags: bit i = la app i esta en la lista blanca (APP_BG_KEEP); has_shed por app lo decide el mundo
void ref_mem_setup(const uint8_t *state, const uint32_t *seen, uint32_t bg_flags, uint32_t shed_mask)
{
    for (int i = 0; i < APP_N; i++) {
        gAppState[i] = state[i] == 0 ? ALIFE_CLOSED : state[i] == 1 ? ALIFE_SUSPENDED : ALIFE_RUNNING; gAppSeenMs[i] = seen[i];
        APP_REG[i].flags = (bg_flags >> i) & 1 ? APP_BG_KEEP : 0;
        APP_REG[i].hooks = (shed_mask >> i) & 1 ? &H_ALL[i] : &H_BGONLY[i];
        gAppMem[i] = 0; gAppMemKnown[i] = false; gAppShed[i] = false;
    }
    flexMemAlertsReset(&gMemAlerts);
}
void ref_mem_known(int app, uint32_t bytes, bool known) { gAppMem[app] = bytes; gAppMemKnown[app] = known; }
uint32_t ref_mem_op(int op, int a, uint32_t b, uint32_t now, bool blocked, bool act_ok)
{
    g_now = now;
    switch (op) {
    case 0: return memShedApp(a);
    case 1: return memShedAll(b);
    case 2: g_weight = (int)b; return (uint32_t)memAdmitApp(a);
    case 3: gSafeMode = blocked; gFrPending = false; gState = act_ok ? ST_HOME : ST_OTHER; g_tr_visible = false; g_opt_active = false;
            memAlertTick(); return 0;
    case 4: appEnforceMemoryBudget(); return 0;
    case 5: appMemCommit(a, b); return 0;
    case 6: appMemReleased(a, b); return 0;
    case 7: appMemForget(a); return 0;
    }
    return 0;
}
void ref_mem_get(uint8_t *state, uint32_t *mem, uint8_t *known, uint8_t *shed)
{
    for (int i = 0; i < APP_N; i++) { state[i] = gAppState[i] == ALIFE_CLOSED ? 0 : gAppState[i] == ALIFE_SUSPENDED ? 1 : 2; mem[i] = gAppMem[i]; known[i] = gAppMemKnown[i]; shed[i] = gAppShed[i]; }
}
FlexMemAlerts *ref_mem_alerts() { return &gMemAlerts; }
void ref_mem_set_state(int app, int st) { gAppState[app] = st == 0 ? ALIFE_CLOSED : st == 1 ? ALIFE_SUSPENDED : ALIFE_RUNNING; }
}
