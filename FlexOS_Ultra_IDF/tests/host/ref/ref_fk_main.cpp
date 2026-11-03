// Referencia: la logica pura de FileKit.h (Arduino): menu contextual, confirmacion, desplazamiento de la
// papelera, texto en caja y edicion del nombre, tal cual.
#include "ref_prelude.h"
#include <stdio.h>
#include <string.h>

static int uiLineH(int s) { return 8 * s + 2; }
static int textW(const char *t, int s) { return (int)strlen(t) * s * 6; }
static char g_out[4096];
static int g_on;
static void drawText(int x, int y, const char *t, int, uint16_t) { g_on += snprintf(g_out + g_on, sizeof(g_out) - g_on, "%d,%d,%s\n", x, y, t); }
static struct { bool tap; int x, y; } T;

#include "ref_fk.inc"

extern "C" {
void ref_fk_menu(int px, int py, int cloud, int *x, int *y, int *w, int *h, int *rows)
{
    fkMenuCloud = cloud; fkMenuX = px; fkMenuY = py;
    fkMenuGeom(*x, *y, *w, *h);
    *rows = fkMenuRows();
}
int ref_fk_menu_hit(int mx, int my, int cloud, int px, int py)
{
    fkMenuCloud = cloud; fkMenuX = mx; fkMenuY = my;
    return fkMenuHit(px, py);
}
void ref_fk_ask_geom(int *x, int *y, int *w, int *h) { fkAskGeom(*x, *y, *w, *h); }
int ref_fk_ask(int px, int py) { T.tap = true; T.x = px; T.y = py; fkAskOn = true; return fkAskTick(); }
int ref_fk_trash_max(int n) { fkTrashN = n; return fkTrashMaxScroll(); }
int ref_fk_textbox(int x, int y, int w, int h, const char *s, int size, char *out, int cap)
{
    g_on = 0; g_out[0] = 0;
    fkTextBox(x, y, w, h, s, size, 0);
    snprintf(out, cap, "%s", g_out);
    return g_on;
}
void ref_fk_name_append(char *buf, const char *s) { snprintf(fkNameBuf, sizeof(fkNameBuf), "%s", buf); fkNameAppend(s); snprintf(buf, sizeof(fkNameBuf), "%s", fkNameBuf); }
void ref_fk_name_back(char *buf) { snprintf(fkNameBuf, sizeof(fkNameBuf), "%s", buf); fkNameBack(); snprintf(buf, sizeof(fkNameBuf), "%s", fkNameBuf); }
}
