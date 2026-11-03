// Referencia: kit de maquetacion (AppFramework.h:275-330) y daysInMonth (Clock.h:79) de la version Arduino.
#include "ref_prelude.h"
static int gAppW = 480;
#define WIN_TOP 96
#define WIN_BOT 736
static int g_textw_k;   // "textW(t, fs)" de la prueba: strlen * k * fs
static int textW(const char *t, int fs) { return (int)strlen(t) * g_textw_k * fs; }
#include "ref_layout.inc"
extern "C" {
void ref_ui_set(int canvas_w, int k) { gAppW = canvas_w; g_textw_k = k; }
void ref_ui_box(int *x, int *y, int *w, int *h) { uiBox(*x, *y, *w, *h); }
int ref_ui_pad() { return uiPad(); }
int ref_ui_gap() { return uiGap(); }
int ref_ui_font_fit(const char *t, int maxw, int maxsize) { return uiFontFit(t, maxw, maxsize); }
int ref_ui_font_h(int lh) { return uiFontH(lh); }
int ref_ui_line_h(int fs) { return uiLineH(fs); }
int ref_days(int y, int m) { return daysInMonth(y, m); }
}
