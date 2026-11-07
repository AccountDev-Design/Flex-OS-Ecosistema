// Referencia: Ajustes del teclado (FlexOS_Ultra_KeyboardSettings.h) de la version Arduino tal cual: contenido de las paginas, filas, acciones,
// volver y toques del editor. Dibujo, guardado, teclado del editor y servicios son dobles que lo REGISTRAN.
#include "ref_prelude.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "flex_kbs_model.h"   // solo flex_kbs_t como bolsa de datos de entrada / salida

#define TH_TXT    0xF101
#define TH_TXT2   0xF102
#define TH_MUTE   0xF103
#define TH_PAGE   0xF104
#define TH_SURF   0xF105
#define TH_GLASS  0xF106
#define TH_DIS    0xF108
#define TH_PRIM   0xF109
#define TH_ONACC  0xF10A
#define TH_DANGER 0xF10B
#define PAGE_BG        TH_PAGE
#define SET_CARD_BG    TH_SURF
#define SET_CARD_GLASS TH_GLASS
#define SET_TXT_HI     TH_TXT
#define SET_TXT_LO     TH_TXT2
#define SET_TXT_MUTE   TH_MUTE
#define SET_CHEV       TH_MUTE
#define KB_SIZE_CONFIG_ON 1
#define KB_ANIM_POLISH_ON 1
#define KB_SETTINGS_ON 1

static bool uiGlass = false, kbLangEs = true, kbShift = false;
static int kbMtMaxPts = 1, gState = 0;
#define ST_APP 0
#define KB_COLS 10
typedef const char *KbRow[KB_COLS];
static KbRow L_ES[1], L_EN[1], L_NUM[1], L_EMOJI[1];
static KbRow *mapaActivo = L_ES;
#define LAYOUT_ES L_ES
#define LAYOUT_EN L_EN
#define LAYOUT_NUM L_NUM
#define LAYOUT_EMOJI L_EMOJI
#define KB_DICT_ES_N (g_dict_es)
#define KB_DICT_EN_N (g_dict_en)
static int g_dict_es = 0, g_dict_en = 0;
static std::string g_log;
static std::vector<std::string> g_fxv;
static void fx(const char *s) { g_fxv.push_back(s); }
static uint16_t thCard() { return TH_SURF; }
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { char b[96]; snprintf(b, sizeof(b), "C%d,%d,%d,%d,%d,%u;", x, y, w, h, r, (unsigned)c); g_log += b; }
static void drawGlassCardFlat(int x, int y, int w, int h, int r, uint16_t c, uint16_t) { fillRoundRect(x, y, w, h, r, c); }
static void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { char b[96]; snprintf(b, sizeof(b), "F%d,%d,%d,%d,%d,%u;", x, y, w, h, r, (unsigned)c); g_log += b; }
static int drawTextClip(int x, int y, const char *s, int size, uint16_t col, int maxRight) { std::string t = "T" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(size) + "," + std::to_string(col) + "," + std::to_string(maxRight) + ":" + s + ";"; g_log += t; return x; }
static void drawText(int x, int y, const char *s, int size, uint16_t col) { std::string t = "T" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(size) + "," + std::to_string(col) + ",0:" + s + ";"; g_log += t; }
static void drawTextC(int x, int y, const char *s, int size, uint16_t col) { std::string t = "M" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(size) + "," + std::to_string(col) + ":" + s + ";"; g_log += t; }
static void strokeSegAA(float x0, float y0, float x1, float y1, float w, uint16_t col) { char b[96]; snprintf(b, sizeof(b), "S%d,%d,%d,%d,%.1f,%u;", (int)x0, (int)y0, (int)x1, (int)y1, w, (unsigned)col); g_log += b; }
static void kbPrefsSave() { fx("SAVE"); }
static void kbApplySize() { fx("SIZE"); }
static void kbMtSurfaceReset() { fx("MT"); }
static void kbsRender() { fx("RENDER"); }
static void kbsRenderList() { fx("RENDERLIST"); }
static void kbsRenderAnim() { fx("ANIM"); }
static void kbsExit() { fx("EXIT"); }
static void kbsEditorKb() {}
static int kbFRowHit(int, int) { return -1; }
static int kbCellAt(int, int) { return -1; }
static const char *kbResolveKey(const char *, char *, bool) { return ""; }
static struct { bool down, tap, pressed, released; int x, y, startX, startY; } T;

#include "ref_kbs.inc"

extern "C" {
void ref_kbs_load(const flex_kbs_t *s)
{
    gKbSize = s->pr.size; gKbFastType = s->pr.fast; gKbToolbar = s->pr.toolbar; gKbPredict = s->pr.predict; gKbSpell = s->pr.spell; gKbEmojiSug = s->pr.emoji_sug;
    gKbHiCon = s->pr.hicon; gKbOpacity = s->pr.opacity; gKbStyle = s->pr.style; gKbFontSc = s->pr.font; gKbLpMs = s->pr.lp_ms; gKbFxMs = s->pr.fx_ms;
    for (int i = 0; i < KB_SYMS; i++) gKbSym[i] = s->pr.sym[i];
    memcpy(gKbScAbr, s->pr.sc_abr, sizeof(gKbScAbr)); memcpy(gKbScExp, s->pr.sc_exp, sizeof(gKbScExp));
    kbLangEs = s->lang_es; kbMtMaxPts = s->mt_max; kbsPage = s->page; kbsScSel = s->sc_sel; kbsScField = s->sc_field;
    memcpy(kbsScA, s->sc_a, sizeof(kbsScA)); memcpy(kbsScE, s->sc_e, sizeof(kbsScE));
}
void ref_kbs_set_dict(int es, int en) { g_dict_es = es; g_dict_en = en; }
void ref_kbs_store(flex_kbs_t *s)
{
    s->pr.size = gKbSize; s->pr.fast = gKbFastType; s->pr.toolbar = gKbToolbar; s->pr.predict = gKbPredict; s->pr.spell = gKbSpell; s->pr.emoji_sug = gKbEmojiSug;
    s->pr.hicon = gKbHiCon; s->pr.opacity = gKbOpacity; s->pr.style = gKbStyle; s->pr.font = gKbFontSc; s->pr.lp_ms = gKbLpMs; s->pr.fx_ms = gKbFxMs;
    for (int i = 0; i < KB_SYMS; i++) s->pr.sym[i] = gKbSym[i];
    memcpy(s->pr.sc_abr, gKbScAbr, sizeof(gKbScAbr)); memcpy(s->pr.sc_exp, gKbScExp, sizeof(gKbScExp));
    s->lang_es = kbLangEs; s->page = kbsPage; s->sc_sel = kbsScSel; s->sc_field = kbsScField;
    memcpy(s->sc_a, kbsScA, sizeof(s->sc_a)); memcpy(s->sc_e, kbsScE, sizeof(s->sc_e));
}
int ref_kbs_content(int scroll, char *log, int cap, int *rowN, int *y0, int *y1, int *contentH)
{
    g_log.clear(); kbsScroll = scroll; kbsContent();
    snprintf(log, cap, "%s", g_log.c_str());
    *rowN = kbsRowN; for (int i = 0; i < kbsRowN; i++) { y0[i] = kbsRowY0[i]; y1[i] = kbsRowY1[i]; }
    *contentH = kbsContentH; return (int)g_log.size();
}
const char *ref_kbs_title(int page) { kbsPage = page; return kbsTitle(); }
static void join(char *log, int cap) { std::string t; for (auto &s : g_fxv) { t += s; t += ";"; } snprintf(log, cap, "%s", t.c_str()); }
int ref_kbs_action(int idx, char *log, int cap) { g_fxv.clear(); kbsRowAction(idx); join(log, cap); return kbsPage; }
int ref_kbs_page(void) { return kbsPage; }
// un toque en (x, y) con la pagina y el scroll cargados: antes se pinta (registra las filas), como hace kbsRender
int ref_kbs_tap(int scroll, int x, int y, char *log, int cap)
{
    g_fxv.clear(); kbsScroll = scroll; kbsContent();
    T.tap = true; T.down = false; T.pressed = false; T.released = false; T.x = x; T.y = y; T.startX = x; T.startY = y;
    kbsTick(); join(log, cap); return kbsPage;
}
}
