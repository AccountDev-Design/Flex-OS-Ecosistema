// Referencia: el editor de Notas de la version Arduino (Keyboard.h:719-1030) tal cual -- edicion, recorrido, hit-test,
// scroll y noteDrawText con sus llamadas graficas anotadas en un registro -- sobre una fuente de mentira.
#include "ref_prelude.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TH_SURF 1
#define TH_SEL 2
#define TH_TXT 3
#define TH_ERR 4
#define TH_PRIM 5
#define UIS_ELEVATED 1
#define IC_NOTAS 0
#define KB_ANIM_POLISH_ON 1
#define KB_AUTOCOMPLETE_ON 1
#define FLEXFS_NAME_MAX 48

static char g_log[1 << 16];
static int g_ln;
static void lg(const char *f, ...)
{
    va_list a;
    va_start(a, f);
    g_ln += vsnprintf(g_log + g_ln, sizeof(g_log) - g_ln, f, a);
    va_end(a);
}
static void fillRect(int x, int y, int w, int h, uint16_t c) { lg("R %d %d %d %d %d\n", x, y, w, h, c); }
static void drawText(int x, int y, const char *s, int size, uint16_t c) { lg("T %d %d %s %d %d\n", x, y, s, size, c); }
static void drawTextC(int x, int y, const char *s, int size, uint16_t c) { lg("TC %d %d %s %d %d\n", x, y, s, size, c); }
static void vLine(int x, int y, int h, uint16_t c) { lg("V %d %d %d %d\n", x, y, h, c); }
static void fillCircle(int x, int y, int r, uint16_t c) { lg("C %d %d %d %d\n", x, y, r, c); }
static void uiSurface(int x, int y, int w, int h, int r, int role) { lg("S %d %d %d %d %d %d\n", x, y, w, h, r, role); }
static void fillRoundRectA(int, int, int, int, int, uint16_t, uint8_t) {}
static void drawTextCA(int, int, const char *, int, uint16_t, uint8_t) {}
static uint16_t thCard2() { return 0; }
static void flxFlush(int, int) {}
static uint32_t g_now;
static uint32_t millis() { return g_now; }
static int textW(const char *, int) { return 10; }
static bool gKbSpell = false;
static int g_kb_top = 390;
static int kbPanelTop() { return g_kb_top; }
static void sessMarkDirty(int) {}
struct FGl { float adv; };
static FGl FG[256];
static int fontIdx(uint32_t cp) { return (int)(cp & 255); }
static float fontSc(int) { return 1.25f; }
// diccionario de mentira: valen las palabras de longitud par
static bool kbDictHas(const char *w) { return (strlen(w) % 2) == 0; }

static uint16_t g_fbd[1];
static uint16_t *fb = g_fbd;
static char g_buf[4096];
static char *noteBuffer = g_buf;
static size_t noteBufMax = 512;
static int abs_(int v) { return v < 0 ? -v : v; }

#include "ref_ned.inc"

extern "C" {
void ref_ned_font(void) { for (int i = 0; i < 256; i++) FG[i].adv = 3.0f + (float)((i * 7) % 11) * 0.9f; }
int ref_ned_adv(unsigned cp) { return (int)(FG[cp & 255].adv * fontSc(2) + 0.5f); }
void ref_ned_set(const char *t, size_t max, int kbtop, int spell, int cur, int a, int b, int menu, int scroll)
{
    snprintf(g_buf, sizeof(g_buf), "%s", t); noteBufMax = max; g_kb_top = kbtop; gKbSpell = spell;
    noteCur = cur; noteSelA = a; noteSelB = b; noteMenu = menu; noteEditorScroll = scroll; noteEditorCursorFollow = false;
}
void ref_ned_set_follow(int f) { noteEditorCursorFollow = f; }
void ref_ned_get(char *t, int *cur, int *a, int *b, int *menu, int *scroll, int *follow)
{
    strcpy(t, g_buf); *cur = noteCur; *a = noteSelA; *b = noteSelB; *menu = noteMenu; *scroll = noteEditorScroll; *follow = noteEditorCursorFollow;
}
void ref_ned_insert(const char *s) { noteInsert(s); }
void ref_ned_backspace() { noteBackspace(); }
void ref_ned_delete_sel() { noteDeleteSel(); }
void ref_ned_select_word(int bi) { selectWordAt(bi); }
void ref_ned_select_all() { selectAllTxt(); }
int ref_ned_walk(int want, int *y) { int yy = 0; int h = noteEditorWalk(want, &yy); *y = yy; return h; }
int ref_ned_hit(int px, int py) { return noteLayoutHit(px, py); }
int ref_ned_max_scroll() { return noteEditorMaxScroll(); }
void ref_ned_clamp() { noteEditorClampScroll(); }
void ref_ned_ensure() { noteEditorEnsureCursor(); }
int ref_ned_draw(char *log, int cap, int *marks)
{
    g_ln = 0; g_log[0] = 0;
    noteDrawText();
    snprintf(log, cap, "%s", g_log);
    marks[0] = curSX; marks[1] = curSY; marks[2] = hASX; marks[3] = hASY; marks[4] = hBSX; marks[5] = hBSY;
    marks[6] = noteMenuX; marks[7] = noteMenuY;
    return g_ln;
}
int ref_ned_menu_hit(int px, int py) { return noteMenuHit(px, py); }
int ref_ned_word(const char *b, int cur, char *out, int n) { return kbCurrentWord(b, cur, out, n); }
}
