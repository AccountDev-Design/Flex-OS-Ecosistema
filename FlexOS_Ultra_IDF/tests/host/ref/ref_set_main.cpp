// Referencia: el contenido de las pantallas de Ajustes, sus filas, las acciones, la sesion y los textos de FlexOS_Ultra_AppSettings.h de la
// version Arduino tal cual. Lo que dibuja o toca el sistema (texto, tarjetas, glifos, guardado en NVS, servicios) son dobles que lo REGISTRAN.
#include "ref_prelude.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "flex_set_model.h"   // solo el tipo flex_set_sys_t: aqui se usa como bolsa de datos de entrada

#define rgb565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define FS_DIR_SESS "/System/Sessions"
#define TH_TXT   0xF101
#define TH_TXT2  0xF102
#define TH_MUTE  0xF103
#define TH_PAGE  0xF104
#define TH_SURF  0xF105
#define TH_GLASS 0xF106
#define TH_NAV   0xF107
#define KB_SETTINGS_ON 1
#define POWEROFF_ON 1
#define POWEROFF_PIN_ON 1
#define KB_SIZE_COMPACT 0
#define KB_SIZE_NORMAL  1
#define KB_SIZE_BIG     2
#define FLEXFS_CAT_TRASH 1
#define IC_AJUSTES 10

// ---- estado del sistema que leen las filas
static int cfgLang = 0, gBright = 80, gNavMode = 0, gLockWidgets = 1, gIconStyle = 0, gAnimStyle = 0, gKbSize = 1, gLockType = 0;
static bool g24h = false, uiGlass = false, gDark = true, gPoffPin = false, gAirplane = false, gHomeDirty = false, gBleEnabled = false, gBleOn = false;
static bool gTimeNvsOk = true, gNtpBusy = false, gAudioAvail = false, gAudioMuted = false, gFsReady = true;
static int rtcH = 0, rtcMin = 0, rtcD = 1, rtcMo = 1, rtcY = 2026, gAudioVol = 0;
static char cfgName[40] = "";
static char g_ntp_state[64], g_ntp_last[48], g_ota[48], g_account[64], g_wifi[64], g_ble[64], g_theft[48], g_audio_err[48];
static unsigned g_free_heap = 0, g_psram_free = 0, g_psram_total = 0;
static unsigned g_fs_used = 0, g_fs_total = 0, g_trash = 0;
static unsigned long g_millis = 0;
static unsigned long millis() { return g_millis; }
static uint32_t gLastTouchMs = 0;
#define FLEXOS_ENABLE_BLE (gBleEnabled)
#define MALLOC_CAP_SPIRAM 1
static unsigned esp_get_free_heap_size() { return g_free_heap; }
static unsigned heap_caps_get_free_size(int) { return g_psram_free; }
static unsigned heap_caps_get_total_size(int) { return g_psram_total; }

// ---- registros
static std::string g_log, g_fx;
static std::vector<std::string> g_fxv;
static void fx(const char *s) { g_fxv.push_back(s); }
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t) { char b[64]; snprintf(b, sizeof(b), "C%d,%d,%d,%d,%d;", x, y, w, h, r); g_log += b; }
static void drawGlassCardFlat(int x, int y, int w, int h, int r, uint16_t, uint16_t) { char b[64]; snprintf(b, sizeof(b), "C%d,%d,%d,%d,%d;", x, y, w, h, r); g_log += b; }
static void drawRowGlyph(int k, int cx, int cy, uint16_t col) { char b[64]; snprintf(b, sizeof(b), "G%d,%d,%d,%u;", k, cx, cy, (unsigned)col); g_log += b; }
static void drawSetCatIcon(int cat, int x, int y, int S, uint16_t col) { char b[64]; snprintf(b, sizeof(b), "I%d,%d,%d,%d,%u;", cat, x, y, S, (unsigned)col); g_log += b; }
static int drawTextClip(int x, int y, const char *s, int size, uint16_t col, int maxRight) { std::string t = "T" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(size) + "," + std::to_string(col) + "," + std::to_string(maxRight) + ":" + s + ";"; g_log += t; return x; }
static void drawText(int x, int y, const char *s, int size, uint16_t col) { std::string t = "T" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(size) + "," + std::to_string(col) + ",0:" + s + ";"; g_log += t; }
static void drawTextR(int x, int y, const char *s, int size, uint16_t col) { std::string t = "R" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(size) + "," + std::to_string(col) + ":" + s + ";"; g_log += t; }
static void strokeSegAA(float x0, float y0, float x1, float y1, float w, uint16_t col) { char b[96]; snprintf(b, sizeof(b), "S%d,%d,%d,%d,%.1f,%u;", (int)x0, (int)y0, (int)x1, (int)y1, w, (unsigned)col); g_log += b; }
static uint16_t thCard() { return 0; }
static void settingsRender() { fx("RENDER"); }
static void settingsRenderBandOnly() { fx("BAND"); }
static void cfgSavePrefs() { fx("SAVE"); }
static void themeChanged() { fx("THEME"); }
static void setBacklight(int v) { fx(("BACKLIGHT" + std::to_string(v)).c_str()); }
static void ntpRequestSync(bool) { fx("NTP"); }
static void flexOtaOpenSettings() { fx("OTA"); }
static void frEnterWizard() { fx("FACTORY"); }
static void accountSettingsEnter() { fx("ACCOUNT"); }
static void connEnter() { fx("CONN"); }
static void wifiSettingsEnter() { fx("WIFI"); }
static void flexBleStop() { fx("BLE"); }
static void flexBleStart() { fx("BLE"); }
static void kbsEnter() { fx("KEYBOARD"); }
static void lsuEnter() { fx("LOCKSETUP"); }
static void theftEnter() { fx("THEFT"); }
static void ntpStateText(char *o, size_t n) { snprintf(o, n, "%s", g_ntp_state); }
static void ntpLastSyncText(char *o, size_t n) { snprintf(o, n, "%s", g_ntp_last); }
static bool ntpIsBusy() { return gNtpBusy; }
static const char *flexOtaStatusText() { return g_ota; }
static void accountSettingsText(char *o, size_t n) { snprintf(o, n, "%s", g_account); }
static void connWifiSub(char *o, size_t n) { snprintf(o, n, "%s", g_wifi); }
static void connBleSub(char *o, size_t n) { snprintf(o, n, "%s", g_ble); }
static const char *theftRowValue() { return g_theft; }
static bool flexAudioAvailable() { return gAudioAvail; }
static int flexAudioVolume() { return gAudioVol; }
static bool flexAudioMuted() { return gAudioMuted; }
static const char *flexAudioError() { return g_audio_err; }
static bool flexFsReady() { return gFsReady; }
static unsigned flexFsUsedBytes() { return g_fs_used; }
static unsigned flexFsTotalBytes() { return g_fs_total; }
static unsigned flexFsCatSize(int) { return g_trash; }
static void flexFsFmtSize(unsigned b, char *o, size_t n) { snprintf(o, n, "B%u", b); }
static uint8_t g_sess[64]; static size_t g_sess_n;
static bool sessWrite(const char *, uint16_t, uint16_t, const void *d, size_t n) { memcpy(g_sess, d, n); g_sess_n = n; return true; }
static size_t sessRead(const char *, uint16_t, uint16_t, void *o, size_t max) { size_t n = g_sess_n < max ? g_sess_n : max; memcpy(o, g_sess, n); return n; }
static int setView = 0;
static int nextCP_dummy;

#include "ref_set.inc"

extern "C" {
void ref_set_load_sys(const flex_set_sys_t *s)
{
    cfgLang = s->lang; g24h = s->h24; gBright = s->bright; uiGlass = s->glass; gDark = s->dark; gNavMode = s->navmode; gLockWidgets = s->lockwidgets;
    gIconStyle = s->iconstyle; gAnimStyle = s->animstyle; gKbSize = s->kbsize; gLockType = s->locktype; gAutoLockMs = s->autolock_ms; gPoffPin = s->poffpin;
    gAirplane = s->airplane; gBleEnabled = s->ble_enabled; snprintf(cfgName, sizeof(cfgName), "%s", s->name);
    rtcD = s->rtc_d; rtcMo = s->rtc_mo; rtcY = s->rtc_y; rtcH = s->rtc_h; rtcMin = s->rtc_min; gTimeNvsOk = s->time_nvs_ok; gNtpBusy = s->ntp_busy;
    snprintf(g_ntp_state, sizeof(g_ntp_state), "%s", s->ntp_state); snprintf(g_ntp_last, sizeof(g_ntp_last), "%s", s->ntp_last);
    snprintf(g_ota, sizeof(g_ota), "%s", s->ota_text); snprintf(g_account, sizeof(g_account), "%s", s->account_text);
    snprintf(g_wifi, sizeof(g_wifi), "%s", s->wifi_sub); snprintf(g_ble, sizeof(g_ble), "%s", s->ble_sub); snprintf(g_theft, sizeof(g_theft), "%s", s->theft);
    gAudioAvail = s->audio_avail; gAudioVol = s->volume; gAudioMuted = s->muted; snprintf(g_audio_err, sizeof(g_audio_err), "%s", s->audio_error);
    gFsReady = s->fs_ready;
}
// los numeros que el modelo recibe ya formateados: aqui van crudos y el doble los formatea como el modelo espera ("B<n>")
void ref_set_load_nums(unsigned used, unsigned total, unsigned trash, unsigned long ms, unsigned free_kb, unsigned rem_kb, unsigned psram_free_mb, unsigned psram_total_mb, unsigned rem_mb)
{
    g_fs_used = used; g_fs_total = total; g_trash = trash; g_millis = ms;
    g_free_heap = free_kb * 1024u + rem_kb; g_psram_free = psram_free_mb * 1048576u + rem_mb; g_psram_total = psram_total_mb * 1048576u + rem_mb;
}
void ref_set_store_sys(flex_set_sys_t *s)
{
    s->lang = cfgLang; s->h24 = g24h; s->bright = gBright; s->glass = uiGlass; s->dark = gDark; s->navmode = gNavMode; s->lockwidgets = gLockWidgets;
    s->iconstyle = gIconStyle; s->animstyle = gAnimStyle; s->autolock_ms = gAutoLockMs; s->poffpin = gPoffPin;
}
int ref_set_home_dirty(void) { return gHomeDirty; }
int ref_set_touch(void) { return (int)gLastTouchMs; }
void ref_set_clear_flags(void) { gHomeDirty = false; gLastTouchMs = 0; }
int ref_set_detail(int cat, int scroll, char *log, int cap, int *rowN, int *y0, int *y1, int *contentH)
{
    g_log.clear(); setSel = cat; setScroll = scroll; settingsDetailContent(cat);
    snprintf(log, cap, "%s", g_log.c_str());
    *rowN = setRowN; for (int i = 0; i < setRowN; i++) { y0[i] = setRowY0[i]; y1[i] = setRowY1[i]; }
    *contentH = setContentH; return (int)g_log.size();
}
int ref_set_list(int scroll, char *log, int cap, int *listH)
{
    g_log.clear(); setListScroll = scroll; settingsListContent(); snprintf(log, cap, "%s", g_log.c_str()); *listH = setListH; return (int)g_log.size();
}
int ref_set_heads(int cat, char *log, int cap)
{
    g_log.clear(); setSel = cat; settingsDrawListHead(); g_log += "|"; settingsDrawDetailHead(); snprintf(log, cap, "%s", g_log.c_str()); return (int)g_log.size();
}
int ref_set_action(int cat, int idx, char *log, int cap)
{
    g_fxv.clear(); settingsRowAction(cat, idx);
    std::string t; for (auto &s : g_fxv) { t += s; t += ";"; }
    snprintf(log, cap, "%s", t.c_str()); return (int)t.size();
}
int ref_set_maxscroll(int view, int contentH, int listH) { setView = view; setContentH = contentH; setListH = listH; return setMaxScroll(); }
void ref_set_datetime(char *out, int n) { settingsDateTimeStr(out, n); }
void ref_set_uptime(unsigned long ms, char *out, int n) { g_millis = ms; buildUptime(out, n); }
const char *ref_set_lang(int l) { cfgLang = l; return langNameCur(); }
int ref_set_autolock_idx(unsigned ms) { gAutoLockMs = ms; return autoLockIdx(); }
const char *ref_set_autolock_name(unsigned ms) { gAutoLockMs = ms; return autoLockName(); }
unsigned ref_set_autolock_norm(unsigned ms) { gAutoLockMs = ms; autoLockNormalize(); return gAutoLockMs; }
int ref_set_sess_save(int view, int sel, int scroll, int lscroll, unsigned char *out)
{
    setView = view; setSel = sel; setScroll = scroll; setListScroll = lscroll; setSaveSess(); memcpy(out, g_sess, g_sess_n); return (int)g_sess_n;
}
// devuelve 0 si la sesion se ignoro (nada cambia), 1 si se cargo
int ref_set_sess_load(const unsigned char *in, int n, int *view, int *sel, int *scroll, int *lscroll)
{
    memcpy(g_sess, in, n); g_sess_n = n;
    setView = 77; setSel = 77; setScroll = 77; setListScroll = 77;
    setLoadSess();
    *view = setView; *sel = setSel; *scroll = setScroll; *lscroll = setListScroll;
    return setView != 77;
}
}
