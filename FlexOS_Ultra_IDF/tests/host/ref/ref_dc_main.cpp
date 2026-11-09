// Referencia: Device Care (FlexOS_Ultra_DeviceCare.h y FlexOS_Ultra_DeviceTests.h) de la version Arduino tal cual: las ocho metricas, la puntuacion, el historial
// persistente y el motor de diagnostico. La memoria, el sistema de archivos, el reloj y el IMU son dobles que fija la prueba.
#include "ref_prelude.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "FlexOS_Mem.h"

#define FLEXFS_DIR_SYS "/System"
enum { ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT };
#define FLEXOS_TZ_OFFSET_SEC (-5 * 3600)
#define TH_OK 1
#define TH_WARN 2
#define TH_ERR 3
#define TH_MUTE 4
static int g_li = 0;
static int LI() { return g_li; }
static uint32_t g_ms = 0;
static uint32_t millis() { return g_ms; }
// memoria
static FlexMemSnap g_snap;
static void memSampleNow() {}
static void memSampleFlashNow() {}
static const FlexMemSnap *memSnap() { return &g_snap; }
// sistema
static uint32_t g_mhz, g_fw;
static uint32_t getCpuFrequencyMhz() { return g_mhz; }
static struct { uint32_t getSketchSize() { return g_fw; } } ESP;
enum { ESP_OK_ = 0 };
#define ESP_OK 0
static bool g_wdt = true;
static int esp_task_wdt_status(void *) { return g_wdt ? 0 : 1; }
static bool g_fs_ready = true; static uint32_t g_fs_total, g_fs_used;
static bool flexFsReady() { return g_fs_ready; }
static uint32_t flexFsTotalBytes() { return g_fs_total; }
static uint32_t flexFsUsedBytes() { return g_fs_used; }
static void flexFsFmtSize(uint32_t b, char *out, size_t n)
{
    if (b >= 1048576u) snprintf(out, n, "%u.%u MB", (unsigned)(b / 1048576u), (unsigned)((b % 1048576u) * 10u / 1048576u));
    else if (b >= 1024u) snprintf(out, n, "%u KB", (unsigned)(b / 1024u));
    else snprintf(out, n, "%u B", (unsigned)b);
}
static bool clkAnchored = true, gTimeNvsOk = true, flxFlushFault = false; static int gtRecoverN = 0; static unsigned gLoopRate = 0;
static uint32_t g_utc = 0;
static uint32_t clkNowUtc() { return g_utc; }
// IMU (el modulo no esta: lo que se comprueba aqui es solo el motor)
static bool g_imu_avail; static uint8_t g_imu_checks;
static bool flexBnoAvailable() { return g_imu_avail; }
static uint8_t flexBnoChecks() { return g_imu_checks; }
// el archivo del historial
static uint8_t g_file[sizeof(uint32_t) + 2 * sizeof(uint16_t) + 16 * 12 + 64]; static int g_file_n = -1;
static uint8_t g_wr[sizeof(g_file)]; static int g_wr_n = -1; static bool g_wr_ok = true;
static int flexFsReadBin(const char *, void *out, size_t cap) { if (g_file_n < 0) return -1; size_t n = (size_t)g_file_n < cap ? (size_t)g_file_n : cap; memcpy(out, g_file, n); return (int)n; }
static bool flexFsWriteBinAtomic(const char *, const void *d, size_t n) { memcpy(g_wr, d, n); g_wr_n = (int)n; return g_wr_ok; }
// ---- dibujo: dobles que REGISTRAN cada primitiva (con los colores como fichas) ----
#define TH_TXT 0xF101
#define TH_TXT2 0xF102
#define TH_MUTE 0xF103
#define TH_SURF 0xF105
#define TH_PRIM 0xF109
#define TH_ONACC 0xF10A
#define TH_ACCS 0xF113
#define TH_TRACK 0xF114
#define TH_BORDER 0xF115
#define TH_SURF2 0xF10D
#define TH_DIV 0xF11A
#define TH_DANGER 0xF11B
#define WIN_BG 0xF10E
#define TH_ACCENT 0xF200
#undef TH_OK
#undef TH_WARN
#undef TH_ERR
#define TH_OK 0xF110
#define TH_WARN 0xF111
#define TH_ERR 0xF112
#define WIN_TOP 96
#define WIN_BOT gWinBot
static int gWinBot = 736;
static int gAppW = 480;
static uint16_t wallAccent() { return TH_ACCENT; }
static bool uiGlass = false;
static int g_textw_k = 6;
static int textW(const char *t, int fs) { return (int)strlen(t) * g_textw_k * fs; }
enum { UIS_CARD = 0, UIS_ELEVATED };
static std::string g_log;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
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
static void fillRect(int x, int y, int w, int h, uint16_t c) { L("R%d,%d,%d,%d,%u;", x, y, w, h, (unsigned)c); }
static void fillCircle(int x, int y, int r, uint16_t c) { L("O%d,%d,%d,%u;", x, y, r, (unsigned)c); }
static void drawCircle(int x, int y, int r, uint16_t c) { L("C%d,%d,%d,%u;", x, y, r, (unsigned)c); }
static void fillRing(int x, int y, int r, int t, uint16_t c) { L("I%d,%d,%d,%d,%u;", x, y, r, t, (unsigned)c); }
static void arcStroke(float x, float y, float r, float a0, float a1, int th, uint16_t c) { L("A%.3f,%.3f,%.3f,%.3f,%.3f,%d,%u;", (double)x, (double)y, (double)r, (double)a0, (double)a1, th, (unsigned)c); }
static void strokeSegAA(float x0, float y0, float x1, float y1, float w, uint16_t c) { L("S%.3f,%.3f,%.3f,%.3f,%.3f,%u;", (double)x0, (double)y0, (double)x1, (double)y1, (double)w, (unsigned)c); }
static void fillTriangle(int a, int b, int c, int d, int e, int f, uint16_t col) { L("V%d,%d,%d,%d,%d,%d,%u;", a, b, c, d, e, f, (unsigned)col); }
static void uiSurface(int x, int y, int w, int h, int r, int k) { L("U%d,%d,%d,%d,%d,%d;", x, y, w, h, r, k); }
static void flxFlush(int, int) {}
#define rgb565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static void hLine(int, int, int, uint16_t) {}
static void vLine(int, int, int, uint16_t) {}
static void drawRect(int, int, int, int, uint16_t) {}
static void fillRoundRectA(int, int, int, int, int, uint16_t, uint8_t) {}
static uint16_t *fb, *bbuf;
static void dcRender() {}
static void dcHeader(const char *) {}
static void touchDropAll() {}
static void setBuf_(uint16_t *) {}
struct FlexFallEvent { uint8_t confidence; float peakG; };
static FlexFallEvent dcLastEvt; static bool dcLastEvtOk = false;
static int dcScroll = 0;
static uint32_t dcAnimT0 = 0, dcAnimMs = 0;
static struct { bool down, tap, pressed, released; int x, y; } T;
#define LI_ LI
static bool dcScoreFrom(const struct DcMetric *m, uint8_t *out);
static bool dcHealthScore(uint8_t *out);
#include "ref_dc_ui.inc"
#include "ref_dc_clk.inc"
#include "ref_dc.inc"
// dcHealthScore (DeviceCare.h): su cuerpo son tres lineas; el prototipo de arriba lo comparte con el codigo extraido
static bool dcHealthScore(uint8_t *out) { DcMetric m[DCH_N]; dcCollect(m); return dcScoreFrom(m, out); }

extern "C" {
void ref_dc_li(int li) { g_li = li; }
void ref_dc_inputs(const FlexMemSnap *m, int boot_reason, unsigned up_ms, int wdt, unsigned loop, int fs_ready, int anchored, int nvs_ok, int fault, int recover,
                   unsigned abnormal, unsigned fs_total, unsigned fs_used, unsigned mhz, unsigned fw)
{
    g_snap = *m; dcBootReason = (uint8_t)boot_reason; g_ms = up_ms; g_wdt = wdt != 0; gLoopRate = loop; g_fs_ready = fs_ready != 0; clkAnchored = anchored != 0;
    gTimeNvsOk = nvs_ok != 0; flxFlushFault = fault != 0; gtRecoverN = recover; dcBootAbnormal = abnormal; g_fs_total = fs_total; g_fs_used = fs_used; g_mhz = mhz; g_fw = fw;
}
void ref_dc_collect(unsigned char *level, signed char *pct, char *detail /* 8 x 40 */, int *ok, int *score)
{
    DcMetric m[DCH_N];
    dcCollect(m);
    for (int i = 0; i < DCH_N; i++) { level[i] = m[i].level; pct[i] = m[i].pct; memcpy(detail + i * 40, m[i].detail, 40); }
    uint8_t sc = 0; bool o = dcScoreFrom(m, &sc);
    *ok = o; *score = o ? sc : -1;
}
int ref_dc_level_color(int lv) { uint16_t c = dcLevelColor((uint8_t)lv); return c == TH_OK ? 0 : c == TH_WARN ? 1 : c == TH_ERR ? 2 : 3; }
int ref_dc_score_color(int s) { uint16_t c = dcScoreColor(s); return c == TH_OK ? 0 : c == TH_WARN ? 1 : 2; }
const char *ref_dc_score_word(int s) { return dcScoreWord(s); }
const char *ref_dc_t(int id) { return dct(id); }
// historial
void ref_dc_file_set(const void *d, int n) { memset(g_file, 0, sizeof(g_file)); if (n > 0) memcpy(g_file, d, (size_t)n < sizeof(g_file) ? (size_t)n : sizeof(g_file)); g_file_n = n; }
void ref_dc_fs(int ready) { g_fs_ready = ready != 0; }
void ref_dc_hist_reset() { dcHistN = 0; dcHistDirty = false; dcHistLoaded = false; dcLastScore = 0; dcLastCheck = 0; memset(dcHist, 0, sizeof(dcHist)); g_wr_n = -1; g_wr_ok = true; }
void ref_dc_hist_load() { dcHistLoad(); }
void ref_dc_wr_ok(int ok) { g_wr_ok = ok != 0; }
void ref_dc_hist_add(unsigned utc, int kind, int score, int flags, int extra) { g_utc = utc; dcHistAdd((uint8_t)kind, (uint8_t)score, (uint8_t)flags, (uint8_t)extra); }
void ref_dc_hist_save() { dcHistSave(); }
int ref_dc_hist_get(unsigned char *recs /* n x 8 */, int *dirty, int *loaded, int *last_score, unsigned *last_check)
{
    for (int i = 0; i < dcHistN; i++) memcpy(recs + i * 8, &dcHist[i], 8);
    *dirty = dcHistDirty; *loaded = dcHistLoaded; *last_score = dcLastScore; *last_check = dcLastCheck;
    return dcHistN;
}
int ref_dc_written(unsigned char *out, int cap) { if (g_wr_n < 0) return -1; memcpy(out, g_wr, (size_t)(g_wr_n < cap ? g_wr_n : cap)); return g_wr_n; }
int ref_dc_content_h(int pad_unused) { (void)pad_unused; return dcHistContentH(); }
// diagnostico
void ref_dc_imu(int avail, int checks) { g_imu_avail = avail != 0; g_imu_checks = (uint8_t)checks; }
void ref_dc_sys_test(char *label /* 9 x 32 */, char *value /* 9 x 28 */, unsigned char *level, int *ok, int *score)
{
    for (int i = 0; i < DC_SYS_ROWS; i++) { dcSysRow[i].label = ""; dcSysRow[i].value[0] = 0; dcSysRow[i].level = DCL_NA; }
    dcRunSystemTest();
    for (int i = 0; i < DC_SYS_ROWS; i++) { snprintf(label + i * 32, 32, "%s", dcSysRow[i].label); memcpy(value + i * 28, dcSysRow[i].value, 28); level[i] = dcSysRow[i].level; }
    *ok = dcResSysOk; *score = dcResSys;
}
void ref_dc_result(int sys, int sys_ok, int imu_mask, int imu_avail, int screen, int touch, int post, unsigned utc, int *final, int *final_ok)
{
    dcResSys = (uint8_t)sys; dcResSysOk = sys_ok != 0; dcResImuMask = (uint8_t)imu_mask; dcResImuAvail = imu_avail != 0; dcResScreen = (int8_t)screen; dcResTouch = (int8_t)touch;
    dcDiagPost = post != 0; g_utc = utc;
    dcResultCompute();
    *final = dcResFinal; *final_ok = dcResFinalOk;
}
}

// ---- pantallas: se fija la caja y el tiempo, se dibuja y se devuelve el registro de primitivas + las zonas pulsables ----
static int set_box(int w, int bot, int li)
{
    gAppW = w; gWinBot = bot; g_li = li;
    g_log.clear();
    dcHitClear();
    return 0;
}
static int out_log(char *log, int cap)
{
    for (int i = 0; i < dcHitN; i++) { char b[64]; snprintf(b, sizeof(b), "H%d,%d,%d,%d,%d;", dcHits[i].x0, dcHits[i].y0, dcHits[i].x1, dcHits[i].y1, dcHits[i].id); g_log += b; }
    gAppW = 480; gWinBot = 736;
    snprintf(log, (size_t)cap, "%s", g_log.c_str());
    return (int)g_log.size();
}
extern "C" {
void ref_dc_k(int k) { g_textw_k = k; }
int ref_dc_hit(int x, int y) { return dcHitTest(x, y); }
const char *ref_dc_mo(int li, int m) { return MO_SHORT[li][m]; }
int ref_dc_box(int *bx, int *by, int *bw, int *bh, int w, int bot) { gAppW = w; gWinBot = bot; uiBox(*bx, *by, *bw, *bh); gAppW = 480; gWinBot = 736; return uiPad(); }
void ref_dc_state(int fall_on, unsigned last_check, int anim_ms)
{
    dcFallOn = fall_on != 0; dcLastCheck = last_check; dcAnimT0 = g_ms - (uint32_t)anim_ms;   // millis() es el de las entradas (tiempo encendido): la animacion cuelga de el
}
int ref_dc_home(int w, int bot, int li, char *log, int cap) { set_box(w, bot, li); dcRenderHome(); return out_log(log, cap); }
int ref_dc_status(int w, int bot, int li, char *log, int cap) { set_box(w, bot, li); dcRenderStatus(); return out_log(log, cap); }
int ref_dc_health(int w, int bot, int li, char *log, int cap) { set_box(w, bot, li); dcHealthRender(); return out_log(log, cap); }
int ref_dc_optim(int w, int bot, int li, char *log, int cap) { set_box(w, bot, li); dcOptimRender(); return out_log(log, cap); }
int ref_dc_diag(int w, int bot, int li, int stage, int post, char *log, int cap) { set_box(w, bot, li); dcDiagStage = stage; dcDiagPost = post != 0; dcDiagRender(); return out_log(log, cap); }
int ref_dc_result_render(int w, int bot, int li, int sys, int sys_ok, int imu_mask, int imu_avail, int screen, int touch, int final, int final_ok, int post, int evt_ok, int conf, float peak,
                         const char *rl /* 9 x 32 */, const char *rv /* 9 x 28 */, const unsigned char *rlv, char *log, int cap)
{
    set_box(w, bot, li);
    dcResSys = (uint8_t)sys; dcResSysOk = sys_ok != 0; dcResImuMask = (uint8_t)imu_mask; dcResImuAvail = imu_avail != 0; dcResScreen = (int8_t)screen; dcResTouch = (int8_t)touch;
    dcResFinal = (uint8_t)final; dcResFinalOk = final_ok != 0; dcDiagPost = post != 0; dcLastEvtOk = evt_ok != 0; dcLastEvt.confidence = (uint8_t)conf; dcLastEvt.peakG = peak;
    static char labs[9][32];
    for (int i = 0; i < DC_SYS_ROWS; i++) { snprintf(labs[i], 32, "%s", rl + i * 32); dcSysRow[i].label = labs[i]; memcpy(dcSysRow[i].value, rv + i * 28, 28); dcSysRow[i].level = rlv[i]; }
    dcResultRender();
    return out_log(log, cap);
}
int ref_dc_touch_render(int w, int bot, int li, const unsigned char *cells, int hit, int lx, int ly, char *log, int cap)
{
    set_box(w, bot, li);
    memcpy(dcTtCell, cells, sizeof(dcTtCell)); dcTtHit = hit; dcTtLastX = lx; dcTtLastY = ly;
    dcTouchTestRender();
    return out_log(log, cap);
}
int ref_dc_touch_geom(int w, int bot, int *gx, int *gy, int *cw, int *ch, int *foot) { gAppW = w; gWinBot = bot; dcTouchTestGeom(*gx, *gy, *cw, *ch, *foot); gAppW = 480; gWinBot = 736; return 0; }
int ref_dc_scr_ask(int w, int bot, int li, char *log, int cap) { set_box(w, bot, li); dcScrStep = DCP_ASK; dcScrTestRender(); return out_log(log, cap); }
void ref_dc_hist_fill(const unsigned char *recs, int n) { memset(dcHist, 0, sizeof(dcHist)); memcpy(dcHist, recs, (size_t)n * 8); dcHistN = n; dcHistLoaded = true; }
int ref_dc_hist_render(int w, int bot, int li, int scroll, char *log, int cap) { set_box(w, bot, li); dcScroll = scroll; dcRenderHist(); return out_log(log, cap); }
}
