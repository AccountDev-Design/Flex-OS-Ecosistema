// Referencia de la PANTALLA de la cuenta: FlexOS_Account_Bridge.h (Arduino) tal cual, con el dibujo y el resto de la interfaz sustituidos por registros. La foto de la
// cuenta es la que le da la prueba; lo que el toque provoca (salir, desvincular, abrir Wi-Fi, pedir enlace, cancelar) se anota. FlexOS_Account.cpp entra entero para
// aportar las etiquetas reales (flexAccountLinkLabel); su API publica se renombra para que los dobles ocupen sus nombres.
#define FLEXOS_HOST_TEST 1
#define flexAccountBegin real_flexAccountBegin
#define flexAccountRequestCode real_flexAccountRequestCode
#define flexAccountCancel real_flexAccountCancel
#define flexAccountLinked real_flexAccountLinked
#define flexAccountUsable real_flexAccountUsable
#define flexAccountState real_flexAccountState
#define flexAccountLinkState real_flexAccountLinkState
#define flexAccountSnapshot real_flexAccountSnapshot
#define flexAccountRequestValidation real_flexAccountRequestValidation
#define flexAccountReportRejected real_flexAccountReportRejected
#define flexAccountCopyBearer real_flexAccountCopyBearer
#define flexAccountForgetLocal real_flexAccountForgetLocal
#include "ref_account_main.cpp"
#undef flexAccountBegin
#undef flexAccountRequestCode
#undef flexAccountCancel
#undef flexAccountLinked
#undef flexAccountUsable
#undef flexAccountState
#undef flexAccountLinkState
#undef flexAccountSnapshot
#undef flexAccountRequestValidation
#undef flexAccountReportRejected
#undef flexAccountCopyBearer
#undef flexAccountForgetLocal
#include "ref_snap.h"
#include <string>
#include <vector>
extern bool gNetWifi;   // netstub.cpp: el Wi-Fi simulado

#define SCR_W 480
#define SCR_H 800
enum { ST_LOCK = 0, ST_APP, ST_OOBE_ACCOUNT };
static int gState = ST_LOCK;
static int lockOff, lastLockOff;
static uint16_t *fb = nullptr;
static struct { bool tap; int x, y; } T;
static char cfgName[24] = "FlexOS Ultra";

// ---- dobles de la foto y de la API ----
static ref_snap_t g_snap;
enum { EV_CANCEL = 1, EV_FORGET = 2, EV_WIFI = 4, EV_REQUEST = 8, EV_OOBE_SAVE = 16, EV_SETTINGS = 32, EV_STORE = 64, EV_CLOUD = 128, EV_NOTIFY = 256, EV_FLUSH = 512, EV_VALIDATE = 1024 };
static unsigned g_ev;
static std::string g_name;
static void flexAccountSnapshot(FlexAccountSnapshot *o)
{
    memset(o, 0, sizeof(*o));
    o->state = (FlexAccountState)g_snap.state;
    o->progress = (uint8_t)g_snap.progress;
    o->linked = g_snap.linked != 0;
    memcpy(o->stage, g_snap.stage, sizeof(o->stage));
    memcpy(o->error, g_snap.error, sizeof(o->error));
    memcpy(o->code, g_snap.code, sizeof(o->code));
    memcpy(o->activationUrl, g_snap.activation_url, sizeof(o->activationUrl));
    memcpy(o->flexAddress, g_snap.flex_address, sizeof(o->flexAddress));
    memcpy(o->displayName, g_snap.display_name, sizeof(o->displayName));
    o->link = (FlexAccountLink)g_snap.link;
    memcpy(o->linkDetail, g_snap.link_detail, sizeof(o->linkDetail));
    o->verifiedAgeS = g_snap.verified_age_s;
}
static FlexAccountState flexAccountState() { return (FlexAccountState)g_snap.state; }
static bool flexAccountLinked() { return g_snap.linked != 0; }
static void flexAccountCancel() { g_ev |= EV_CANCEL; }
static bool flexAccountRequestCode(const char *name) { g_ev |= EV_REQUEST; g_name = name ? name : "(null)"; return true; }
static void flexAccountForgetLocal() { g_ev |= EV_FORGET; }
static void flexAccountRequestValidation() { g_ev |= EV_VALIDATE; }
static void flexCloudAccountUnlinked() { g_ev |= EV_CLOUD; }
static void sysNotify(const char *, const char *) { g_ev |= EV_NOTIFY; }
static void wifiOobeEnter() { g_ev |= EV_WIFI; }
static void cfgSaveOobe() { g_ev |= EV_OOBE_SAVE; }
static void renderHome() {}
static void renderLock() {}
static void showLock() {}
static void settingsRender() { g_ev |= EV_SETTINGS; }
static void storeEnter() { g_ev |= EV_STORE; }

// ---- dibujo: se anota, no se pinta ----
struct Draw { std::string op, text; int a[7]; unsigned color; int alpha; };
static std::vector<Draw> g_draw;
static inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
static void note(const char *op, int a0, int a1, int a2, int a3, int a4, unsigned color, int alpha, const char *text = "")
{
    Draw d;
    d.op = op;
    d.text = text;
    d.a[0] = a0; d.a[1] = a1; d.a[2] = a2; d.a[3] = a3; d.a[4] = a4; d.a[5] = d.a[6] = 0;
    d.color = color;
    d.alpha = alpha;
    g_draw.push_back(d);
}
static void drawWallpaper(uint16_t *, bool) { note("wall", 0, 0, 0, 0, 0, 0, 255); }
static void setBuf(uint16_t *) {}
static void flxFlushAll() { g_ev |= EV_FLUSH; }
static void fillRectA(int x, int y, int w, int h, uint16_t c, int a) { note("rectA", x, y, w, h, 0, c, a); }
static void fillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, int a) { note("rrectA", x, y, w, h, r, c, a); }
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { note("rrect", x, y, w, h, r, c, 255); }
static void fillCircleAA(float cx, float cy, float r, uint16_t c) { note("circle", (int)cx, (int)cy, (int)(r * 10), 0, 0, c, 255); }
static void strokeSeg(int x0, int y0, int x1, int y1, int t, uint16_t c) { note("seg", x0, y0, x1, y1, t, c, 255); }
static void drawTextC(int cx, int y, const char *s, int size, uint16_t c) { note("text", cx, y, size, 0, 0, c, 255, s); }
static int textW(const char *s, int size) { return (int)strlen(s) * 6 * size; }

#include "../../../../FlexOS_Ultra/FlexOS_Account_Bridge.h"

static void load(const ref_snap_t *s) { g_snap = *s; }
extern "C" {
int ref_ui_needs_relink(const ref_snap_t *s)
{
    load(s);
    FlexAccountSnapshot x;
    flexAccountSnapshot(&x);
    return accountNeedsRelink(x) ? 1 : 0;
}
unsigned ref_ui_link_rgb565(int link) { return accountLinkColor((FlexAccountLink)link); }
const char *ref_ui_primary_label(int state, int online) { return accountPrimaryLabel((FlexAccountState)state, online != 0); }
const char *ref_ui_code_note(const ref_snap_t *s)
{
    load(s);
    FlexAccountSnapshot x;
    flexAccountSnapshot(&x);
    static std::string keep;   // la nota apunta al error de la foto local: se copia
    keep = accountCodeNote(x);
    return keep.c_str();
}
void ref_ui_settings_text(const ref_snap_t *s, char *out, unsigned long n)
{
    load(s);
    accountSettingsText(out, n);
}
// Un toque sobre la pantalla ya dibujada con el estado `built`. Devuelve los EV_* provocados y deja el estado de la confirmacion en *ask_after
unsigned ref_ui_tap(const ref_snap_t *s, int built, int ask, int first, int ret, int online, int x, int y, int *ask_after, int *state_after)
{
    load(s);
    g_ev = 0;
    accountLastState = (FlexAccountState)built;
    accountUnlinkAsk = ask != 0;
    accountFirstBoot = first != 0;
    accountReturn = (AccountReturn)ret;
    gNetWifi = online != 0;
    accountPollMs = (uint32_t)millis();
    gState = ST_OOBE_ACCOUNT;
    T.tap = true;
    T.x = x;
    T.y = y;
    accountOobeTick();
    g_draw.clear();   // los toques que repintan (confirmacion, enlace pedido) no acumulan primitivas
    T.tap = false;
    *ask_after = accountUnlinkAsk ? 1 : 0;
    *state_after = gState;
    return g_ev;
}
// Dibuja la pantalla (accountRender) y deja la lista de primitivas
void ref_ui_render(const ref_snap_t *s, int online, int ask, int first, int ret)
{
    load(s);
    g_draw.clear();
    g_ev = 0;
    gNetWifi = online != 0;
    accountUnlinkAsk = ask != 0;
    accountFirstBoot = first != 0;
    accountReturn = (AccountReturn)ret;
    accountRender();
}
int ref_ui_draw_count() { return (int)g_draw.size(); }
int ref_ui_draw_get(int i, char *op, unsigned long opcap, int *a, unsigned *color, int *alpha, char *text, unsigned long textcap)
{
    if (i < 0 || i >= (int)g_draw.size()) {
        return 0;
    }
    const Draw &d = g_draw[(size_t)i];
    snprintf(op, opcap, "%s", d.op.c_str());
    snprintf(text, textcap, "%s", d.text.c_str());
    for (int k = 0; k < 5; k++) {
        a[k] = d.a[k];
    }
    *color = d.color;
    *alpha = d.alpha;
    return 1;
}
int ref_ui_last_render_ask() { return accountUnlinkAsk ? 1 : 0; }
const char *ref_ui_last_name() { return g_name.c_str(); }
}
