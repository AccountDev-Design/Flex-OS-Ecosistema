// Referencia: el explorador de archivos (FlexOS_Ultra_AppFiles.h) de la version Arduino tal cual: recarga con la biblioteca oculta, subir un nivel, abrir un
// fichero, acciones del menu, toques y pulsaciones largas, y el contenido de las filas. El disco, el kit de dialogos, los visores y la nube son dobles
// que REGISTRAN lo que se les pide.
#include "ref_prelude.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "FlexOS_Media.h"

#define SCR_W 480
#define SCR_H 800
#define TH_TXT    0xF101
#define TH_TXT2   0xF102
#define TH_MUTE   0xF103
#define TH_PAGE   0xF104
#define TH_SURF   0xF105
#define TH_SURF2  0xF10D
#define TH_PRIM   0xF109
#define TH_ONACC  0xF10A
#define TH_WARN   0xF111
#define TH_SEL    0xF117
#define TH_NAV    0xF118
#define TH_GLASS  0xF116
#define TH_GLASS2 0xF119
#define WIN_BG    0xF10E
#define rgb565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define FLEXFS_PATH_MAX 160
#define FLEXFS_NAME_MAX 48
#define FLEXFS_DIR_SYS "/System"
#define FML_DIR_ROOT "/System/Media"
#define UIHDR_ZONE 56
#define UIHDR_H 76
#define IC_ALMACEN 3
#define FK_ACT_SEL 0
#define FK_ACT_DEL 1
#define FK_ACT_REN 2
#define FK_ACT_TRASH 3
#define FK_ACT_CLOUD 4
#define MOD_MEDIA 1
#define MM_W 200
static bool uiGlass = false;
static uint16_t thCard() { return TH_SURF; }
static int textW(const char *t, int fs) { return (int)strlen(t) * 6 * fs; }
static std::string g_log, g_act;
static void L(std::string &s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void L(std::string &s, const char *fmt, ...)
{
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    s += b;
}
// dibujo
static void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { L(g_log, "Q%d,%d,%d,%d,%d,%u;", x, y, w, h, r, (unsigned)c); }
static void fillRect(int x, int y, int w, int h, uint16_t c) { L(g_log, "Q%d,%d,%d,%d,0,%u;", x, y, w, h, (unsigned)c); }
static void fillCircle(int, int, int, uint16_t) {}
static void almFolderIcon(int x, int y, int s) { L(g_log, "I%d,%d,%d;", x, y, s); }
static void drawGlassCardFlat(int x, int y, int w, int h, int r, uint16_t, uint16_t) { fillRoundRect(x, y, w, h, r, TH_SURF); }
static void drawLiquidGlassPanel(int x, int y, int w, int h, int r, uint16_t) { fillRoundRect(x, y, w, h, r, TH_SURF2); }
static void drawText(int x, int y, const char *s, int fs, uint16_t c) { L(g_log, "T%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void drawTextClip(int x, int y, const char *s, int fs, uint16_t c, int mr) { L(g_log, "K%d,%d,%d,%u,%d:%s;", x, y, fs, (unsigned)c, mr, s); }
static void drawTextC(int x, int y, const char *s, int fs, uint16_t c) { L(g_log, "M%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void drawTextR(int x, int y, const char *s, int fs, uint16_t c) { L(g_log, "N%d,%d,%d,%u:%s;", x, y, fs, (unsigned)c, s); }
static void strokeSegAA(float, float, float, float, float, uint16_t) {}
static void uiClipViewport(int, int) { g_log += "<VP>"; }
static void uiClipFull() { g_log += "</VP>"; }
static void flxFlushAll() {}
static void flxFlush(int, int) {}
static uint16_t *fb;
static uint32_t g_ms = 0;
static uint32_t millis() { return g_ms; }
static int uiLineH(int fs) { return fs <= 1 ? 8 : fs * 9; }
// toque
static struct { bool down, tap, pressed, released; int x, y, startX, startY; uint32_t downMs; } T;
// almacenamiento
struct FlexFsEntry { char name[FLEXFS_NAME_MAX]; uint32_t size; bool dir; uint16_t items; };
static FlexFsEntry g_src[300]; static int g_nsrc;
static bool flexFsReady() { return true; }
static int mediaList(const char *, FlexFsEntry *out, int maxn) { int n = g_nsrc < maxn ? g_nsrc : maxn; for (int i = 0; i < n; i++) out[i] = g_src[i]; return n; }
static void flexFsFmtSize(uint32_t b, char *out, size_t n)
{
    if (b >= 1048576u) snprintf(out, n, "%u.%u MB", (unsigned)(b / 1048576u), (unsigned)((b % 1048576u) * 10u / 1048576u));
    else if (b >= 1024u) snprintf(out, n, "%u KB", (unsigned)(b / 1024u));
    else snprintf(out, n, "%u B", (unsigned)b);
}
static void flexFsTrash(const char *p) { L(g_act, "TRASH:%s;", p); }
static void flexFsDelete(const char *p) { L(g_act, "DEL:%s;", p); }
static void flexFsRename(const char *p, const char *nm) { L(g_act, "REN:%s>%s;", p, nm); }
// visores y avisos
static int gState; static int gMediaReturnApp;
#define ST_APP 1
static void mediaOpenInPlayer(const char *p) { L(g_act, "PLAY:%s;", p); }
static void mediaNotify(int, const char *name, const char *why) { L(g_act, "NOTE:%s:%s;", name, why); }
static void sysNotify(const char *t, const char *s) { L(g_act, "SYS:%s:%s;", t, s); }
static void ckNotifyFail(const char *t, const char *s) { L(g_act, "CKFAIL:%s:%s;", t, s); }
static const char *ckCloudBlock() { return nullptr; }
static bool flexCloudUpload(const char *p, const char *n, const char *, int, int) { L(g_act, "UP:%s:%s;", p, n); return true; }
// kit de dialogos: solo registra que se abre (los dialogos son de la interfaz)
static bool fkMenuOn, fkMenuCloud, fkAskOn, fkNameOn, fkTrashOn, mmOn, mmDlgOn;
static char fkNameBuf[64];
static void fkMenuOpenEx(int x, int y, bool cloud) { L(g_act, "MENU:%d,%d,%d;", x, y, cloud ? 1 : 0); }
static void fkMenuOpen(int x, int y) { fkMenuOpenEx(x, y, false); }
static void fkAskOpen(const char *m, const char *s) { L(g_act, "ASK:%s:%s;", m, s); }
static void fkNameOpen(const char *t, const char *i) { L(g_act, "NAME:%s:%s;", t, i); }
static void fkTrashOpen() { g_act += "TRASHOPEN;"; }
static void fkCloseAll() {}
static void fkMenuDraw() {}
static int fkMenuHit(int, int) { return -1; }
static bool fkTrashTick() { return true; }
static int g_ask_ret, g_name_ret;
static int fkAskTick() { return g_ask_ret; }
static int fkNameTick() { return g_name_ret; }
static void fkNoFsScreen(const char *) {}
static void filesExit() { g_act += "EXIT;"; }
static bool uiHdrBackHit(int px, int py) { return px >= 0 && px < UIHDR_ZONE && py >= 0 && py < UIHDR_ZONE; }
static bool uiHdrMenuHit(int px, int py) { return px >= SCR_W - UIHDR_ZONE && px < SCR_W && py >= 0 && py < UIHDR_ZONE; }
static void uiHdrDraw(const char *, int, uint16_t, uint16_t, bool) {}
// nube (la otra mitad): no se ejercita aqui
struct CkHost { const char *title; int mode; void (*box)(int &, int &, int &, int &); void (*redraw)(); void (*open)(const char *); uint16_t bg; };
enum { CKM_BROWSE = 1 };
static void ckBind(CkHost *) {}
static void ckUnbind(CkHost *) {}
static void ckRender() {}
static void ckTick() {}
static bool ckBack() { return false; }
static void ckAppMenu(int, int) {}
static void filesRender();

// las funciones de nombres de Arduino viven en su propio espacio: ref_fsnames_main.cpp extrae las mismas y se enlazan en el mismo ejecutable
namespace reffiles_fs {
#include "ref_files_fs.inc"
}
using namespace reffiles_fs;
#include "ref_files_inc.inc"

// ---- el kit define el nombre del gancho que usa filesTick ----
extern "C" {
void ref_files_src(const char *names, const unsigned *sizes, const int *dirs, const int *items, int n)
{
    g_nsrc = n;
    int k = 0;
    const char *p = names;
    for (int i = 0; i < n; i++) {
        snprintf(g_src[i].name, sizeof(g_src[i].name), "%s", p);
        p += strlen(p) + 1;
        g_src[i].size = sizes[i];
        g_src[i].dir = dirs[i] != 0;
        g_src[i].items = (uint16_t)items[i];
        k++;
    }
}
// entra en `dir`, relee (con el filtro de la biblioteca) y devuelve la carpeta que quedo y las entradas
int ref_files_enter(const char *dir, char *dir_out, int cap, char *names_out, int ncap)
{
    snprintf(filesDir, sizeof(filesDir), "%s", dir && dir[0] ? dir : "/");
    filesSelIdx = -1; filesScroll = 0; filesMulti = false; filesMask = 0; filesCloud = false;
    filesReload();
    snprintf(dir_out, (size_t)cap, "%s", filesDir);
    int w = 0;
    names_out[0] = 0;
    for (int i = 0; i < filesN && w < ncap - 60; i++) w += snprintf(names_out + w, (size_t)(ncap - w), "%s;", filesList[i].name);
    return filesN;
}
void ref_files_set(int sel, int multi, unsigned mask, int scroll) { filesSelIdx = sel; filesMulti = multi != 0; filesMask = mask; filesScroll = scroll; filesLongFired = false; filesDragging = false; }
void ref_files_get(char *dir, int cap, int *sel, int *multi, unsigned *mask, int *n)
{
    snprintf(dir, (size_t)cap, "%s", filesDir);
    *sel = filesSelIdx; *multi = filesMulti; *mask = filesMask; *n = filesN;
}
// un toque corto
int ref_files_tap(int x, int y, char *act, int cap)
{
    g_act.clear();
    memset(&T, 0, sizeof(T));
    T.tap = true; T.x = x; T.y = y; T.startX = x; T.startY = y;
    filesTick();
    if (filesCloud) { g_act += "CLOUD;"; filesCloud = false; }
    snprintf(act, (size_t)cap, "%s", g_act.c_str());
    return (int)g_act.size();
}
// el dedo apoyado en (sx, sy) hasta (x, y) desde hace held ms
int ref_files_hold(int sx, int sy, int x, int y, unsigned held, char *act, int cap)
{
    g_act.clear();
    // el cuadro del apoyo (fija el origen del arrastre) y, `held` ms despues, el dedo quieto
    g_ms = 100000;
    memset(&T, 0, sizeof(T));
    T.pressed = true; T.down = true; T.x = sx; T.y = sy; T.startX = sx; T.startY = sy; T.downMs = g_ms;
    filesLongFired = false;
    filesTick();
    g_ms = 100000 + held;
    memset(&T, 0, sizeof(T));
    T.down = true; T.x = x; T.y = y; T.startX = sx; T.startY = sy; T.downMs = 100000;
    filesTick();
    snprintf(act, (size_t)cap, "%s", g_act.c_str());
    return (int)g_act.size();
}
int ref_files_menu(int act_id, char *act, int cap)
{
    g_act.clear();
    filesMenuAction(act_id);
    snprintf(act, (size_t)cap, "%s", g_act.c_str());
    return (int)g_act.size();
}
// resultados de los dialogos: filesTick con fkAskTick() / fkNameTick() devolviendo lo que fija la prueba
int ref_files_ask(int ret, char *act, int cap)
{
    g_act.clear();
    g_ask_ret = ret;
    fkAskOn = true;
    memset(&T, 0, sizeof(T));
    filesTick();
    fkAskOn = false;
    snprintf(act, (size_t)cap, "%s", g_act.c_str());
    return (int)g_act.size();
}
int ref_files_name(int ret, const char *name, char *act, int cap)
{
    g_act.clear();
    g_name_ret = ret;
    snprintf(fkNameBuf, sizeof(fkNameBuf), "%s", name);
    fkNameOn = true;
    memset(&T, 0, sizeof(T));
    filesTick();
    fkNameOn = false;
    snprintf(act, (size_t)cap, "%s", g_act.c_str());
    return (int)g_act.size();
}
// el contenido de la lista: el registro de filesRender entre <VP> y </VP>, y la barra
int ref_files_render(int scroll, int multi, unsigned mask, char *log, int cap)
{
    g_log.clear();
    filesScroll = scroll; filesMulti = multi != 0; filesMask = mask;
    filesRender();
    snprintf(log, (size_t)cap, "%s", g_log.c_str());
    return (int)g_log.size();
}
}
