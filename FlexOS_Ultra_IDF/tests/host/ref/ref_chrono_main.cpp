// Referencia: Reloj + Cronometro (FlexOS_Ultra_AppChrono.h y relojBody / relojTabs / appRelojHoraRender de AppFramework.h)
// de la version Arduino tal cual: logica, formato, capsula, maqueta, esfera, tabla, botones, tarjeta, tacto y pestana Hora.
// Dibujo, tiempo y estado del sistema son dobles que REGISTRAN lo que se les pide.
#include "ref_prelude.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <math.h>

#define TH_TXT    0xF101
#define TH_TXT2   0xF102
#define TH_MUTE   0xF103
#define TH_PRIM   0xF109
#define TH_ONACC  0xF10A
#define TH_DANGER 0xF10B
#define TH_DIS    0xF108
#define TH_DIV    0xF10C
#define TH_SURF2  0xF10D
#define TH_SURF   0xF105
#define WIN_BG    0xF10E
#define WIN_TOP 96
static int gWinBot = 736;
#define WIN_BOT gWinBot
static int gAppW = 480;

static uint32_t g_ms = 0;
static uint32_t millis() { return g_ms; }
static int g_textw_k = 6;
static int textW(const char *t, int fs) { return (int)strlen(t) * g_textw_k * fs; }
static uint16_t wallAccent() { return 0xF200; }
static uint16_t thCard() { return 0xF104; }
static uint16_t uiSurfFlat(int) { return 0xF300; }
static bool uiGlass = false;
enum { UIS_CARD = 0, UIS_ELEVATED, UIS_ACCENT };
enum { S_CRN_HOUR, S_CRN_STOPW, S_CRN_TIMER, S_CRN_LAP, S_CRN_SPLIT, S_CRN_TOTAL, S_CRN_BLAP, S_CRN_BSTOP, S_CRN_BRESET, S_CRN_BSTART, S_CRN_NOLAPS, S_CONTINUE, S_SOON };
static const char *t(int id) {
    static const char *N[] = {"Hora", "Cronometro", "Temporizador", "Vuelta", "Parcial", "Total", "Parcial", "Detener", "Reinic.", "Iniciar", "Sin vueltas", "Continuar", "Pronto"};
    return N[id];
}
enum CronoState { CRONO_IDLE = 0, CRONO_RUN = 1, CRONO_PAUSE = 2 };
enum CronoCardPhase { CC_HIDDEN = 0, CC_OPENING, CC_OPEN, CC_CLOSING };

static std::string g_log;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void L(const char *fmt, ...) {
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    g_log += b;
}
static void fillRect(int x, int y, int w, int h, uint16_t c) { L("R%d,%d,%d,%d,%u;", x, y, w, h, (unsigned)c); }
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { L("C%d,%d,%d,%d,%d,%u;", x, y, w, h, r, (unsigned)c); }
static void fillRoundRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a) { L("Q%d,%d,%d,%d,%d,%u,%u;", x, y, w, h, r, (unsigned)c, (unsigned)a); }
static void fillCircle(int x, int y, int r, uint16_t c) { L("O%d,%d,%d,%u;", x, y, r, (unsigned)c); }
static void fillCircleA(int x, int y, int r, uint16_t c, uint8_t a) { L("P%d,%d,%d,%u,%u;", x, y, r, (unsigned)c, (unsigned)a); }
static void drawCircle(int x, int y, int r, uint16_t c) { L("D%d,%d,%d,%u;", x, y, r, (unsigned)c); }
static void fillCircleAA(float x, float y, float r, uint16_t c) { L("A%d,%d,%d,%u;", (int)x, (int)y, (int)r, (unsigned)c); }
static void strokeSegAA(float x0, float y0, float x1, float y1, float w, uint16_t c) { L("S%d,%d,%d,%d,%.1f,%u;", (int)x0, (int)y0, (int)x1, (int)y1, w, (unsigned)c); }
static void fillRing(int x, int y, int r, int th, uint16_t c) { L("G%d,%d,%d,%d,%u;", x, y, r, th, (unsigned)c); }
static void drawText(int x, int y, const char *s, int fs, uint16_t c) { L("T%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void drawTextC(int x, int y, const char *s, int fs, uint16_t c) { L("M%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void drawTextA(int x, int y, const char *s, int fs, uint16_t c, uint8_t a) { L("U%d,%d,%d,%u,%u:%s;", x, y, fs, (unsigned)c, (unsigned)a, s); }
static void uiTextC(int x, int y, const char *s, int fs, uint16_t c, uint8_t a) { L("V%d,%d,%d,%u,%u:%s;", x, y, fs, (unsigned)c, (unsigned)a, s); }
static void uiRectA(int x, int y, int w, int h, int r, uint16_t c, uint8_t a) { L("W%d,%d,%d,%d,%d,%u,%u;", x, y, w, h, r, (unsigned)c, (unsigned)a); }
static void uiSurface(int x, int y, int w, int h, int r, int k) { L("X%d,%d,%d,%d,%d,%d;", x, y, w, h, r, k); }
static void uiSurfaceA(int x, int y, int w, int h, int r, int k, uint8_t a) { L("Y%d,%d,%d,%d,%d,%d,%u;", x, y, w, h, r, k, (unsigned)a); }
static void present(int y0, int y1) { L("p%d,%d;", y0, y1); }
static void flxFlush(int y0, int y1) { L("f%d,%d;", y0, y1); }
static uint16_t *bbuf, *fb;
static uint16_t *gCronoCardBak, *gCronoCardCache;
static void bbufSys() {}
static bool g_ota = false;
static bool flexOtaOwnsScreen() { return g_ota; }
static bool gHosted = false, gSuspOn = false;
static bool gAppHidesStatusBar = false, g_immersive = false, KIOSK_ON = true, kioskOn = false, qsAnimOn = false, editMode = false;
static int qsPanelY = 0;
static bool appImmersive() { return g_immersive; }
enum { ST_HOME = 0, ST_APP = 1 };
static int gState = ST_HOME, gAppId = 0;
#define IC_RELOJ 0
#define APP_CUSTOM_HEADER 1
struct AppReg { uint8_t flags; };
static AppReg APP_REG[4];
static bool g24h = true;
static bool cronoCardCloseNowCalled = false;

// Toque (T) como en Arduino: banderas de evento que el modulo consume
static struct { bool down, tap, pressed, released, swipeUp, swipeLeft, swipeRight, swipeDown; int x, y, startX, startY; uint32_t downMs; } T;

static void cronoOpenApp() { L("OPEN;"); }
static void cronoCardOpen() { L("COPEN;"); }
static void cronoCardClose() { L("CCLOSE;"); }
static void cronoCardDrop() { L("CDROP;"); }
static void cronoBarRebuildMock(int surface);
static void cronoBarRebuild(int surface) { cronoBarRebuildMock(surface); }
static void cronoCapsuleStamp(int surface) { L("STAMP%d;", surface); }

static void appRelojRender() { L("RENDER;"); }
static char g_clock_str[16] = "13:23";
static void clkStrBar(char *out, size_t n) { snprintf(out, n, "%s", g_clock_str); }
static void clkStr12(char *out, size_t n) { snprintf(out, n, "%s", g_clock_str); }
static char g_date_long[64] = "sabado, 4 de julio", g_date_short[40] = "sab, 4 jul";
static void buildLongDate(char *out, size_t n) { snprintf(out, n, "%s", g_date_long); }
static void buildShortDate(char *out, size_t n) { snprintf(out, n, "%s", g_date_short); }
static void drawBigClock(const char *s, int cx, int y, int capH, int thick, uint16_t c) { L("K%d,%d,%d,%d,%u:%s;", cx, y, capH, thick, (unsigned)c, s); }
#include "ref_chrono_t.inc"
#include "ref_chrono_ui.inc"
#include "ref_chrono.inc"

// Doble de la reconstruccion de barra: lo que hacen renderHome / appDrawChrome, que es llamar a cronoBarClock.
static void cronoBarRebuildMock(int surface) {
    L("REBUILD%d;", surface);
    cronoBarClock(12, TH_TXT);
}

#include "flex_chrono_model.h"   // solo flex_crono_t / flex_crono_world_t como bolsas de datos de entrada / salida

static void load(const flex_crono_t *c) {
    gCronoSt = (CronoState)c->st;
    gCronoAccum = c->accum;
    gCronoT0 = c->t0;
    gCronoLapBase = c->lap_base;
    for (int i = 0; i < CRONO_MAX_LAPS; i++) {
        gCronoLaps[i].split = c->laps[i].split;
        gCronoLaps[i].total = c->laps[i].total;
    }
    gCronoNLaps = c->nlaps;
    gCronoLap0 = c->lap0;
    gCronoBest = c->best;
    gCronoWorst = c->worst;
    gCronoLapsDirty = c->laps_dirty;
    gCronoBtnDirty = c->btn_dirty;
    gCronoDialDirty = c->dial_dirty;
    gCronoBarDirty = c->bar_dirty;
}
static void store(flex_crono_t *c) {
    c->st = (uint8_t)gCronoSt;
    c->accum = gCronoAccum;
    c->t0 = gCronoT0;
    c->lap_base = gCronoLapBase;
    for (int i = 0; i < CRONO_MAX_LAPS; i++) {
        c->laps[i].split = gCronoLaps[i].split;
        c->laps[i].total = gCronoLaps[i].total;
    }
    c->nlaps = gCronoNLaps;
    c->lap0 = gCronoLap0;
    c->best = gCronoBest;
    c->worst = gCronoWorst;
    c->laps_dirty = gCronoLapsDirty;
    c->btn_dirty = gCronoBtnDirty;
    c->dial_dirty = gCronoDialDirty;
    c->bar_dirty = gCronoBarDirty;
}
static int cpy(char *out, int cap) {
    snprintf(out, (size_t)cap, "%s", g_log.c_str());
    return (int)g_log.size();
}
static void ensure_bufs() {
    if (!bbuf) {
        bbuf = (uint16_t *)calloc((size_t)SCR_W * SCR_H, 2);
        fb = (uint16_t *)calloc((size_t)SCR_W * SCR_H, 2);
        gCronoCardBak = (uint16_t *)calloc((size_t)SCR_W * CRONO_BAND_H, 2);
        gCronoCardCache = (uint16_t *)calloc((size_t)SCR_W * CRONO_DYN_H, 2);
    }
}

extern "C" {
void ref_crono_load(const flex_crono_t *c) { load(c); }
void ref_crono_store(flex_crono_t *c) { store(c); }
void ref_crono_set_ms(uint32_t ms) { g_ms = ms; }
void ref_crono_act(int which) {
    switch (which) {
    case 0: cronoStart(); break;
    case 1: cronoPause(); break;
    case 2: cronoReset(); break;
    case 3: cronoLapMark(); break;
    case 4: cronoActionLeft(); break;
    case 5: cronoActionRight(); break;
    }
}
uint32_t ref_crono_elapsed() { return cronoElapsed(); }
uint32_t ref_crono_lap_elapsed() { return cronoLapElapsed(); }
int ref_crono_cur_lap() { return cronoCurLapNo(); }
void ref_crono_fmt(char *out, int n, uint32_t ms, int cent) { cronoFmt(out, (size_t)n, ms, cent != 0); }
int ref_crono_label_left() { return cronoLabelLeft() == S_CRN_BRESET ? 1 : 0; }
int ref_crono_label_right() { int l = cronoLabelRight(); return l == S_CRN_BSTOP ? 1 : l == S_CONTINUE ? 2 : 0; }
float ref_crono_ease(float p) { return cronoEase(p); }
int ref_crono_lerp(int a, int b, float p) { return cronoLerp(a, b, p); }
int ref_crono_glyph(int cx, int cy, int r, float th, char *log, int cap) {
    g_log.clear();
    cronoGlyph(cx, cy, r, CRONO_ONACC, th);
    return cpy(log, cap);
}
void ref_crono_text_k(int k) { g_textw_k = k; }
int ref_crono_cap_w() { return cronoCapsuleW(); }
int ref_crono_cap_right() { return cronoCapsuleRight(); }
int ref_crono_cap_draw(int x, char *log, int cap) {
    g_log.clear();
    cronoCapsuleDraw(x);
    return cpy(log, cap);
}
// cronoBarClock + el estado de la capsula que deja
int ref_crono_bar_clock(const char *clock, char *log, int cap, int *cap_on, int *cap_x, int *cap_w, unsigned *cap_sec, int *cap_st) {
    g_log.clear();
    snprintf(g_clock_str, sizeof(g_clock_str), "%s", clock);
    cronoBarClock(12, TH_TXT);
    *cap_on = gCronoCapOn;
    *cap_x = gCronoCapX;
    *cap_w = gCronoCapWDrawn;
    *cap_sec = gCronoCapSec;
    *cap_st = gCronoCapStDrawn;
    return cpy(log, cap);
}
void ref_crono_cap_set(int on, int x, int w, unsigned sec, int st) {
    gCronoCapOn = on != 0;
    gCronoCapX = x;
    gCronoCapWDrawn = w;
    gCronoCapSec = sec;
    gCronoCapStDrawn = (uint8_t)st;
}
int ref_crono_surface(const flex_crono_surface_in_t *in) {
    gSuspOn = in->suspended;
    gLand = in->land;
    gHosted = in->hosted;
    gState = in->app ? ST_APP : in->home ? ST_HOME : 7;
    gAppHidesStatusBar = in->app_hides_status_bar;
    g_immersive = in->immersive;
    kioskOn = in->kiosk;
    qsPanelY = in->qs_closed ? 0 : 100;
    qsAnimOn = false;
    editMode = in->edit_mode;
    gAppId = 1;
    APP_REG[1].flags = in->app_custom_header ? APP_CUSTOM_HEADER : 0;
    return cronoBarSurface();
}
int ref_crono_cap_tick(int surface_ignored, int card, int ota, char *log, int cap) {
    (void)surface_ignored;
    g_log.clear();
    gCronoCard = (CronoCardPhase)card;
    g_ota = ota != 0;
    cronoCapsuleTick();
    g_ota = false;
    return cpy(log, cap);
}
// Maqueta: caja util (w, bottom) -> pad, cuerpo y CronoLayout
void ref_crono_layout(int w, int bot, int *pad, int *bx, int *by, int *bw, int *bh, int *out /* 20 enteros */) {
    gAppW = w;
    gWinBot = bot;
    *pad = uiPad();
    cronoLayout();
    *bx = gCL.bx; *by = gCL.by; *bw = gCL.bw; *bh = gCL.bh;
    int v[20] = {gCL.cx, gCL.cy, gCL.rTick, gCL.rDisc, gCL.dialY0, gCL.dialY1, gCL.tblY, gCL.rowH, gCL.rows, gCL.colLap,
                 gCL.colSplit, gCL.colTot, gCL.btnCy, gCL.btnR, gCL.btnLx, gCL.btnRx, gCL.hasTable, 0, 0, 0};
    memcpy(out, v, sizeof(v));
    gAppW = 480;
    gWinBot = 736;
}
int ref_crono_fit_big(const char *s, int maxw, int max_size) { return cronoFitBig(s, maxw, max_size); }
int ref_crono_dial(int w, int bot, int viaback, char *log, int cap) {
    ensure_bufs();
    gAppW = w;
    gWinBot = bot;
    gLand = viaback == 0;
    gHosted = false;
    cronoLayout();
    g_log.clear();
    cronoDrawDial();
    gLand = false;
    gAppW = 480;
    gWinBot = 736;
    return cpy(log, cap);
}
int ref_crono_laps(int w, int bot, char *log, int cap) {
    gAppW = w;
    gWinBot = bot;
    cronoLayout();
    g_log.clear();
    cronoDrawLaps();
    gAppW = 480;
    gWinBot = 736;
    return cpy(log, cap);
}
int ref_crono_buttons(int w, int bot, char *log, int cap) {
    gAppW = w;
    gWinBot = bot;
    cronoLayout();
    g_log.clear();
    cronoDrawButtons();
    gAppW = 480;
    gWinBot = 736;
    return cpy(log, cap);
}
int ref_crono_tab_touch(int w, int bot, int x, int y, int *consumed) {
    int n0 = gCronoNLaps, s0 = gCronoSt;
    gAppW = w;
    gWinBot = bot;
    cronoLayout();
    g_log.clear();
    memset(&T, 0, sizeof(T));
    T.tap = true;
    T.pressed = true;
    T.x = x;
    T.y = y;
    bool r = cronoTabTouch();
    *consumed = !T.tap && !T.pressed;
    gAppW = 480;
    gWinBot = 736;
    return !r ? 0 : gCronoNLaps != n0 ? 1 : gCronoSt != s0 ? 2 : 3;
}
int ref_crono_card_dyn(unsigned alpha, char *log, int cap) {
    g_log.clear();
    cronoCardDynText((uint8_t)alpha);
    return cpy(log, cap);
}
int ref_crono_card_static(unsigned alpha, char *log, int cap) {
    g_log.clear();
    cronoCardStatic((uint8_t)alpha);
    return cpy(log, cap);
}
int ref_crono_card_compose(float p, int cache, char *log, int cap) {
    ensure_bufs();
    g_log.clear();
    cronoCardCompose(p, cache != 0);
    return cpy(log, cap);
}
// cronoOverlayTouch: el estado de T y del mundo entra por las bolsas; devuelve la bitacora de llamadas
// y las banderas de T que quedaron consumidas.
int ref_crono_overlay(const flex_crono_world_t *w, const flex_crono_touch_t *t, int long_fired_in, uint32_t now, char *log, int cap,
                      unsigned *clr, int *long_fired_out) {
    g_log.clear();
    g_ms = now;
    g_ota = w->ota_owns;
    gSuspOn = w->susp;
    gCronoCard = (CronoCardPhase)w->card;
    gCronoCapOn = w->cap_on;
    gCronoCapX = w->cap_x;
    gCronoCapWDrawn = w->cap_w_drawn;
    // la superficie la decide el mundo: 0 -> bloqueo; 1 -> escritorio; 2 -> marco de app
    gLand = false;
    gHosted = false;
    gAppHidesStatusBar = false;
    g_immersive = false;
    kioskOn = false;
    qsPanelY = 0;
    qsAnimOn = false;
    editMode = false;
    gAppId = 1;
    APP_REG[1].flags = 0;
    gState = w->surface == 1 ? ST_HOME : w->surface == 2 ? ST_APP : 7;
    T.down = t->down; T.tap = t->tap; T.pressed = t->pressed; T.released = t->released;
    T.swipeUp = t->swipe_up; T.swipeLeft = t->swipe_left; T.swipeRight = t->swipe_right; T.swipeDown = t->swipe_down;
    T.x = t->x; T.y = t->y; T.startX = t->start_x; T.startY = t->start_y; T.downMs = t->down_ms;
    // `longFired` es un static de la funcion: no se puede sembrar. Un episodio nuevo (down=false) la limpia; para
    // probar "ya disparo" la prueba llama dos veces con el mismo episodio.
    (void)long_fired_in;
    gCronoCardBg = true;
    cronoOverlayTouch();
    if (!gCronoCardBg) L("BG;");
    unsigned c = 0;
    if (!T.tap && t->tap) c |= FLEX_CRONO_TC_TAP;
    if (!T.pressed && t->pressed) c |= FLEX_CRONO_TC_PRESSED;
    if (!T.released && t->released) c |= FLEX_CRONO_TC_RELEASED;
    if (!T.swipeUp && t->swipe_up) c |= FLEX_CRONO_TC_SWIPE_UP;
    if (!T.swipeLeft && t->swipe_left) c |= FLEX_CRONO_TC_SWIPE_L;
    if (!T.swipeRight && t->swipe_right) c |= FLEX_CRONO_TC_SWIPE_R;
    if (!T.swipeDown && t->swipe_down) c |= FLEX_CRONO_TC_SWIPE_D;
    *clr = c;
    *long_fired_out = 0;
    g_ota = false;
    gSuspOn = false;
    return cpy(log, cap);
}
// Reloj: pestanas y Hora
void ref_reloj_body(int w, int bot, int *x, int *y, int *bw, int *bh) {
    gAppW = w;
    gWinBot = bot;
    relojBody(*x, *y, *bw, *bh);
    gAppW = 480;
    gWinBot = 736;
}
int ref_reloj_tabs_visible(int w, int bot) {
    gAppW = w;
    gWinBot = bot;
    int r = relojTabsVisible();
    gAppW = 480;
    gWinBot = 736;
    return r;
}
// relojTabsTouch: devuelve la pestana a la que cae el toque (-1 si no) leyendo gRelojTab tras la llamada
int ref_reloj_tab_touch(int w, int bot, int x, int y) {
    gAppW = w;
    gWinBot = bot;
    g_log.clear();
    gRelojTab = 9;
    memset(&T, 0, sizeof(T));
    T.tap = true;
    T.pressed = true;
    T.x = x;
    T.y = y;
    bool r = relojTabsTouch();
    int res = -1;
    if (r) res = gRelojTab == 9 ? -2 : (int)gRelojTab;   // -2: era la misma pestana (devuelve true sin cambiar)
    gAppW = 480;
    gWinBot = 736;
    return res;
}
void ref_reloj_set_tab(int tab) { gRelojTab = (uint8_t)tab; }
int ref_reloj_hora(int w, int bot, const char *clock, const char *ldate, const char *sdate, int h24, char *log, int cap) {
    gAppW = w;
    gWinBot = bot;
    snprintf(g_clock_str, sizeof(g_clock_str), "%s", clock);
    snprintf(g_date_long, sizeof(g_date_long), "%s", ldate);
    snprintf(g_date_short, sizeof(g_date_short), "%s", sdate);
    g24h = h24 != 0;
    g_log.clear();
    uiSecN = 0;   // sin fundidos heredados de la llamada anterior
    appRelojHoraRender();
    gAppW = 480;
    gWinBot = 736;
    return cpy(log, cap);
}
}
