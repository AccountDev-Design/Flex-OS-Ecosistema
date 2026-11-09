// Referencia: la pantalla principal de Almacenamiento y la de detalles de memoria y sistema (FlexOS_Ultra_AppStorage.h:36-638) de la version
// Arduino tal cual. El dibujo, la memoria, el sistema de archivos, las apps y el telefono son dobles que REGISTRAN o devuelven lo que fija la prueba.
#include "ref_prelude.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include "FlexOS_Mem.h"

#define TH_TXT    0xF101
#define TH_TXT2   0xF102
#define TH_MUTE   0xF103
#define TH_SURF   0xF105
#define TH_PRIM   0xF109
#define TH_ONACC  0xF10A
#define TH_OK     0xF110
#define TH_WARN   0xF111
#define TH_ERR    0xF112
#define TH_ACCS   0xF113
#define TH_TRACK  0xF114
#define TH_BORDER 0xF115
#define TH_GLASS  0xF116
#define WIN_BG    0xF10E
#define WIN_TOP 96
#define WIN_BOT gWinBot
static int gWinBot = 736;
static int gAppW = 480;
#define rgb565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static bool uiGlass = false;
static uint16_t wallAccent() { return 0xF200; }
static int g_textw_k = 6;
static int textW(const char *t, int fs) { return (int)strlen(t) * g_textw_k * fs; }
enum { UIS_CARD = 0, UIS_ELEVATED };

static std::string g_log;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void L(const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    g_log += b;
}
static void drawText(int x, int y, const char *s, int fs, uint16_t c) { L("T%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void drawTextClip(int x, int y, const char *s, int fs, uint16_t c, int mr) { L("K%d,%d,%d,%u,%d:%s;", x, y, fs, (unsigned)c, mr, s); }
static void drawTextC(int x, int y, const char *s, int fs, uint16_t c) { L("M%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void drawTextR(int x, int y, const char *s, int fs, uint16_t c) { L("N%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { L("Q%d,%d,%d,%d,%d,%u;", x, y, w, h, r, (unsigned)c); }
static void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { L("L%d,%d,%d,%d,%d,%u;", x, y, w, h, r, (unsigned)c); }
static void fillCircle(int x, int y, int r, uint16_t c) { L("O%d,%d,%d,%u;", x, y, r, (unsigned)c); }
static void uiSurface(int x, int y, int w, int h, int r, int k) { L("U%d,%d,%d,%d,%d,%d;", x, y, w, h, r, k); }
static void drawLiquidGlassPanel(int x, int y, int w, int h, int r, uint16_t c) { fillRoundRect(x, y, w, h, r, TH_SURF); (void)c; }
static void strokeSegAA(float x0, float y0, float x1, float y1, float w, uint16_t c) { L("S%d,%d,%d,%d,%.1f,%u;", (int)x0, (int)y0, (int)x1, (int)y1, w, (unsigned)c); }
static void ckCloudGlyph(int cx, int cy, int s, uint16_t c) { L("G%d,%d,%d,%u;", cx, cy, s, (unsigned)c); }
static void fillRect(int, int, int, int, uint16_t) {}
static void flxFlush(int, int) {}
static uint16_t *fb;
static uint32_t g_ms = 0;
static uint32_t millis() { return g_ms; }

// ---- sistema de archivos, memoria, apps y telefono: los fija la prueba ----
#define FLEXFS_CAT_N 4
#define FLEXFS_CAT_SYS 1
#define FLEXFS_CAT_APPS 2
#define FLEXFS_CAT_DOCS 0
#define FS_DIR_CACHE "/System/Cache"
#define FML_DIR_ROOT "/System/Media"
struct FlexFsBig { char path[160]; uint32_t size; };
static bool g_fs_ready = true; static char g_fs_err[64];
static uint32_t g_fs_total, g_fs_used, g_cat[FLEXFS_CAT_N], g_cache;
static FlexFsBig g_bigs[12]; static int g_nbigs;
static bool flexFsReady() { return g_fs_ready; }
static const char *flexFsError() { return g_fs_err; }
static uint32_t flexFsTotalBytes() { return g_fs_total; }
static uint32_t flexFsUsedBytes() { return g_fs_used; }
static uint32_t flexFsCatSize(int c) { return g_cat[c]; }
static uint32_t flexFsDirSize(const char *) { return g_cache; }
static int flexFsLargest(FlexFsBig *out, int maxn) { int n = g_nbigs < maxn ? g_nbigs : maxn; for (int i = 0; i < n; i++) out[i] = g_bigs[i]; return n; }
static void flexFsFmtSize(uint32_t b, char *out, size_t n)
{
    if (b >= 1048576u) snprintf(out, n, "%u.%u MB", (unsigned)(b / 1048576u), (unsigned)((b % 1048576u) * 10u / 1048576u));
    else if (b >= 1024u) snprintf(out, n, "%u KB", (unsigned)(b / 1024u));
    else snprintf(out, n, "%u B", (unsigned)b);
}
static FlexMemSnap g_snap;
static const FlexMemSnap *memSnap() { return &g_snap; }
static uint32_t g_ps_total, g_ps_free;
#define MALLOC_CAP_SPIRAM 1
static size_t heap_caps_get_total_size(int) { return g_ps_total; }
static size_t heap_caps_get_free_size(int) { return g_ps_free; }
#define APP_N 19
enum { ALIFE_CLOSED = 0, ALIFE_RUNNING, ALIFE_SUSPENDED };
static uint8_t gAppState[APP_N]; static bool gAppShed[APP_N], g_mem_has[APP_N]; static uint32_t g_mem_bytes[APP_N];
static char g_app_name[APP_N][24];
static bool appMemHas(int i) { return g_mem_has[i]; }
static uint32_t appMemBytes(int i) { return g_mem_bytes[i]; }
static const char *appName(int i) { return g_app_name[i]; }
static bool gNetOnline; static unsigned gLoopRate;
static char g_uptime[24] = "12m";
static void buildUptime(char *out, size_t n) { snprintf(out, n, "%s", g_uptime); }
static const char *flexOtaLocalVersion() { return "1.0.0"; }
static int g_reason;
enum { ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT };
static int esp_reset_reason() { return g_reason; }
static uint32_t almFwSize = 0xFFFFFFFFu;
// telefono
enum FlexStoragePhoneState : uint8_t { FSP_NONE = 0, FSP_OFF, FSP_REJECTED, FSP_READY };
struct FlexStorageInfo { uint8_t state; char name[40]; uint8_t reachable; uint32_t lastOkAgeS; };
struct FclQuota { uint32_t usedBytes, reservedBytes, permille; int state; };
struct FlexCloudStatus { bool quotaValid; FclQuota quota; };
static FlexStorageInfo g_si; static bool g_quota_valid; static char g_quota_line[64];
static void flexStorageInfo(FlexStorageInfo *o) { *o = g_si; }
static void flexCloudStatus(FlexCloudStatus *c) { memset(c, 0, sizeof(*c)); c->quotaValid = g_quota_valid; }
enum { FCD_PHONE = 1 };
static int flexCloudDest() { return FCD_PHONE; }
static void fclQuotaLine(const FclQuota *, char *o, size_t n) { snprintf(o, n, "%s", g_quota_line); }
// toque y acciones de la pantalla
static struct { bool down, tap, pressed, released; int x, y; } T;
static std::string g_act;
static void almPhoneEnter() { g_act += "PHONE;"; }
static void almDetailEnter() { g_act += "DETAIL;"; }
static void filesEnter() { g_act += "FILES;"; }
static void optStart() { g_act += "OPT;"; }
static void almPhoneTick() {}
static void almDetailPaint() {}
static void almDetailRepaintSection(int) {}
static void sysNotify(const char *, const char *) {}
static float fabsf_(float v) { return v < 0 ? -v : v; }
#define fabsf fabsf_
static void almDetailEnter();

#include "ref_stor_ui.inc"
#include "ref_stor.inc"

extern "C" {
void ref_stor_k(int k) { g_textw_k = k; }
void ref_stor_ms(unsigned ms) { g_ms = ms; }
// Entradas de la pantalla principal: todo lo que lee almRenderMain
void ref_stor_main_in(int fs_ready, const char *err, unsigned fs_total, unsigned fs_used, unsigned ps_total, unsigned ps_free, int health,
                      const unsigned *cat, const char *const *bigp, const unsigned *bigsz, int nbig, int state, const char *name, int reach, int never,
                      int quota_valid, const char *quota)
{
    g_fs_ready = fs_ready != 0;
    snprintf(g_fs_err, sizeof(g_fs_err), "%s", err);
    g_fs_total = fs_total; g_fs_used = fs_used;
    g_ps_total = ps_total; g_ps_free = ps_free;
    // la salud sale de memSnap: se fija con una instantanea que da ese veredicto
    memset(&g_snap, 0, sizeof(g_snap));
    if (health == FLEXMEM_H_CRIT) { g_snap.psTotal = 32u << 20; g_snap.psFree = 1u << 20; g_snap.psLargest = 1u << 20; g_snap.inTotal = 300000; g_snap.inFree = 100000; }
    else if (health == FLEXMEM_H_WATCH) { g_snap.psTotal = 32u << 20; g_snap.psFree = 9u << 20; g_snap.psLargest = 8u << 20; g_snap.inTotal = 300000; g_snap.inFree = 100000; }
    else { g_snap.psTotal = 32u << 20; g_snap.psFree = 20u << 20; g_snap.psLargest = 19u << 20; g_snap.inTotal = 300000; g_snap.inFree = 100000; }
    for (int i = 0; i < FLEXFS_CAT_N; i++) g_cat[i] = cat[i];
    g_nbigs = nbig;
    for (int i = 0; i < nbig; i++) { snprintf(g_bigs[i].path, sizeof(g_bigs[i].path), "%s", bigp[i]); g_bigs[i].size = bigsz[i]; }
    memset(&g_si, 0, sizeof(g_si));
    g_si.state = (uint8_t)state;
    snprintf(g_si.name, sizeof(g_si.name), "%s", name);
    g_si.reachable = (uint8_t)reach;
    g_si.lastOkAgeS = never ? 0xFFFFFFFFu : 5;
    g_quota_valid = quota_valid != 0;
    snprintf(g_quota_line, sizeof(g_quota_line), "%s", quota);
}
int ref_stor_health() { return flexMemHealth(memSnap()); }
// el icono de carpeta REAL de Arduino (lo usan la pantalla principal y las filas del explorador): los tres rectangulos que dibuja
int ref_stor_folder(int x, int y, int s, char *log, int cap)
{
    g_log.clear();
    almFolderIcon(x, y, s);
    snprintf(log, (size_t)cap, "%s", g_log.c_str());
    return (int)g_log.size();
}
int ref_stor_main(int w, int bot, int glass, char *log, int cap, int *zones /* 8 */)
{
    gAppW = w; gWinBot = bot; uiGlass = glass != 0;
    g_log.clear();
    almRenderMain();
    zones[0] = almPhY0; zones[1] = almPhY1; zones[2] = almDetY0; zones[3] = almDetY1; zones[4] = almOptX0; zones[5] = almOptX1; zones[6] = almVerY0; zones[7] = almVerY1;
    gAppW = 480; gWinBot = 736; uiGlass = false;
    snprintf(log, (size_t)cap, "%s", g_log.c_str());
    return (int)g_log.size();
}
int ref_stor_tick(int x, int y, char *log, int cap)
{
    g_act.clear();
    memset(&T, 0, sizeof(T));
    T.tap = true; T.x = x; T.y = y;
    almScreen = ALM_SCR_MAIN;
    almTick();
    snprintf(log, (size_t)cap, "%s", g_act.c_str());
    return (int)g_act.size();
}
// Detalle
void ref_stor_sys(const FlexMemSnap *m, unsigned fw, unsigned sys, unsigned apps, unsigned docs, unsigned cache, int reason, const char *uptime, int net,
                  unsigned loop, const unsigned char *state, const unsigned char *shed, const unsigned char *has, const unsigned *bytes, const char *const *names)
{
    g_snap = *m; almFwSize = fw; g_cat[FLEXFS_CAT_SYS] = sys; g_cat[FLEXFS_CAT_APPS] = apps; g_cat[FLEXFS_CAT_DOCS] = docs; g_cache = cache;
    g_reason = reason; snprintf(g_uptime, sizeof(g_uptime), "%s", uptime); gNetOnline = net != 0; gLoopRate = loop;
    for (int i = 0; i < APP_N; i++) { gAppState[i] = state[i]; gAppShed[i] = shed[i] != 0; g_mem_has[i] = has[i] != 0; g_mem_bytes[i] = bytes[i]; snprintf(g_app_name[i], sizeof(g_app_name[i]), "%s", names[i]); }
}
int ref_stor_section(int sec, int sy, char *log, int cap)
{
    g_log.clear();
    almDetDrawSection(sec, sy);
    snprintf(log, (size_t)cap, "%s", g_log.c_str());
    return (int)g_log.size();
}
unsigned ref_stor_sig(int sec) { return almDetSecSig(sec); }
int ref_stor_sec_top(int i) { return almDetSecTop(i); }
int ref_stor_content_h() { return almDetContentH(); }
int ref_stor_max_scroll() { return almDetMaxScroll(); }
const char *ref_stor_boot(int r) { g_reason = r; return almBootCause(); }
// toque en la pantalla de detalle: el boton "Optimizar Flex OS" con el desplazamiento dado
int ref_stor_det_tap(int scroll, int x, int y)
{
    g_act.clear();
    almDetScroll = scroll;
    memset(&T, 0, sizeof(T));
    T.pressed = true; T.x = x; T.y = y;
    almDetailTick();
    memset(&T, 0, sizeof(T));
    T.tap = true; T.x = x; T.y = y;
    almDetailTick();
    return g_act.find("OPT;") != std::string::npos;
}
}
