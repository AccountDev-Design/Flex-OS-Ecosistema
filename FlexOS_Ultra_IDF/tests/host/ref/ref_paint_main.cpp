// Referencia: el formato .fxp y sus operaciones (FlexOS_FS.cpp:686-839), la geometria del lienzo y de la galeria de Paint
// (AppPaint.h) y su sesion paint.bin v1, de la version Arduino tal cual, sobre un LittleFS en memoria.
#include "ref_prelude.h"
#include <map>
#include <string>
#include <stdio.h>
#include <string.h>

#define rgb565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define IC_PAINT 8
#define FS_DIR_SESS "/System/Sessions"
// Todo el doble de LittleFS va en un espacio anonimo: ref_fsnames_main.cpp define otra struct LfsT, y sus miembros en linea
// (open/remove/rename) se fusionarian en el enlace (ODR) con los de esta: el LittleFS de Paint llamaria al de otro doble.
namespace {
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };
static std::map<std::string, std::string> g_files;
static bool fsMounted = true;
struct FsMut {};
class File {
public:
    std::string *b = nullptr; size_t pos = 0; bool ok = false, wr = false;
    operator bool() const { return ok; }
    size_t read(uint8_t *o, size_t n) { if (!ok) return 0; size_t a = pos < b->size() ? b->size() - pos : 0; if (n > a) n = a; memcpy(o, b->data() + pos, n); pos += n; return n; }
    size_t write(const uint8_t *p, size_t n) { if (!ok || !wr) return 0; if (pos + n > b->size()) b->resize(pos + n); memcpy(&(*b)[pos], p, n); pos += n; return n; }
    bool seek(size_t p, int mode = SeekSet) { if (!ok) return false; pos = mode == SeekEnd ? b->size() + p : p; return true; }
    void close() { ok = false; }
};
static struct LfsT {
    File open(const char *p, const char *mode)
    {
        File f;
        std::string m = mode;
        auto it = g_files.find(p);
        if (m == "w") { g_files[p] = ""; f.b = &g_files[p]; f.ok = f.wr = true; }
        else if (it != g_files.end()) { f.b = &it->second; f.ok = true; f.wr = (m == "r+"); }
        return f;
    }
    bool remove(const char *p) { return g_files.erase(p) != 0; }
    bool rename(const char *a, const char *b) { auto it = g_files.find(a); if (it == g_files.end()) return false; g_files[b] = it->second; g_files.erase(a); return true; }
} LittleFS;
}  // namespace
static int g_nav = 64;
static int navBarH() { return g_nav; }
static bool flexFsReady() { return true; }
static bool g_exists = true;
static bool flexFsExists(const char *) { return g_exists; }
static uint8_t g_sess[256]; static size_t g_sess_n;
static bool sessWrite(const char *, uint16_t, uint16_t, const void *d, size_t n) { memcpy(g_sess, d, n); g_sess_n = n; return true; }
static size_t sessRead(const char *, uint16_t, uint16_t, void *o, size_t max) { size_t n = g_sess_n < max ? g_sess_n : max; memcpy(o, g_sess, n); return n; }
// El lienzo (paintCanvasTick): el dedo, los dibujos y los avisos son dobles que lo REGISTRAN; la logica es la de Arduino.
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
static struct { bool down, tap; int x, y; } T;
static uint16_t *fb = nullptr;
static std::string g_dlog;
static int g_marks, g_redraws, g_tools, g_reloads, g_gals;
static void fillCircleAA(float x, float y, float r, uint16_t c) { char b[64]; snprintf(b, sizeof(b), "D%d,%d,%d,%u;", (int)x, (int)y, (int)r, (unsigned)c); g_dlog += b; }
static void strokeSegAA(float x0, float y0, float x1, float y1, float r, uint16_t c) { char b[96]; snprintf(b, sizeof(b), "S%d,%d,%d,%d,%d,%u;", (int)x0, (int)y0, (int)x1, (int)y1, (int)r, (unsigned)c); g_dlog += b; }
static void flxFlush(int, int) {}
static void paintReload() { g_reloads++; }
static void paintRenderGallery() { g_gals++; }
static void paintRenderCanvas() { g_redraws++; }
static void paintTools() { g_tools++; }
static void sessMarkDirty(int) { g_marks++; }
typedef void (*FlexPaintSegCb)(int x0, int y0, int x1, int y1, uint16_t color, int radius, void *user);

#include "ref_paint.inc"

static std::string g_replay;
static void segcb(int x0, int y0, int x1, int y1, uint16_t c, int r, void *) { char b[96]; snprintf(b, sizeof(b), "%d,%d,%d,%d,%u,%d;", x0, y0, x1, y1, (unsigned)c, r); g_replay += b; }

extern "C" {
void ref_paint_reset(void) { g_files.clear(); }
int ref_paint_create(const char *p, int w, int h) { return flexPaintCreate(p, (uint16_t)w, (uint16_t)h); }
int ref_paint_append(const char *p, int color, int size, const short *xy, int pts) { return flexPaintAppend(p, (uint16_t)color, (uint8_t)size, xy, (uint16_t)pts); }
int ref_paint_undo(const char *p) { return flexPaintUndo(p); }
int ref_paint_clear(const char *p) { return flexPaintClear(p); }
int ref_paint_bytes(const char *p, unsigned char *out, int cap) { auto it = g_files.find(p); if (it == g_files.end()) return -1; int n = (int)it->second.size(); if (n > cap) n = cap; memcpy(out, it->second.data(), n); return n; }
int ref_paint_files(char *out, int cap) { int n = 0; out[0] = 0; for (auto &kv : g_files) n += snprintf(out + n, cap - n, "[%s:%zu]", kv.first.c_str(), kv.second.size()); return n; }
void ref_paint_put(const char *p, const unsigned char *d, int n) { g_files[p] = std::string((const char *)d, n); }
int ref_paint_replay(const char *p, float sc, int ox, int oy, char *out, int cap)
{
    g_replay.clear();
    int ok = flexPaintReplay(p, sc, ox, oy, segcb, NULL);
    snprintf(out, cap, "%s", g_replay.c_str());
    return ok;
}
int ref_paint_header(const char *p, int *w, int *h, int *s) { FlexPaintHdr hd; if (!flexPaintHeader(p, &hd)) return 0; *w = hd.w; *h = hd.h; *s = hd.strokes; return 1; }
// geometria del lienzo
void ref_paint_doc(int nav, const char *path, int sizeix, int *dw, int *dh, int *x, int *y, int *dispw, int *disph, float *sc, int *bot, int *ch, int *rad)
{
    g_nav = nav; snprintf(paintPath, sizeof(paintPath), "%s", path); pSizeIx = sizeix;
    paintDocLayout();
    *dw = pDocW; *dh = pDocH; *x = pDocX; *y = pDocY; *dispw = pDocDispW; *disph = pDocDispH; *sc = pDocScale; *bot = P_BOT; *ch = PAINT_CH; *rad = paintDocRadius();
}
int ref_paint_hit(int x, int y) { return paintDocHit(x, y); }
int ref_paint_coord(int v, int org, int lim) { return paintDocCoord(v, org, lim); }
void ref_paint_card(int nav, int n, int scroll, int i, int *x, int *y, int *w, int *h, int *maxs)
{
    g_nav = nav; paintListN = n; paintScroll = scroll; paintCardRect(i, *x, *y, *w, *h); *maxs = paintMaxScroll();
}
void ref_paint_fab(int *x, int *y, int *r) { paintFabRect(*x, *y, *r); }
unsigned ref_paint_pal(int i) { return P_PAL[i]; }
int ref_paint_size(int i) { return P_SIZES[i]; }
// sesion
int ref_paint_save(int view, const char *path, int sizeix, int color, int scroll, unsigned char *out)
{
    paintView = view; snprintf(paintPath, sizeof(paintPath), "%s", path); pSizeIx = sizeix; pColor = (uint16_t)color; paintScroll = scroll;
    paintSaveSess();
    memcpy(out, g_sess, g_sess_n);
    return (int)g_sess_n;
}
void ref_paint_load(const unsigned char *in, int n, int exists, int *view, char *path, int *sizeix, int *color, int *scroll)
{
    memcpy(g_sess, in, n); g_sess_n = n;
    g_exists = exists;
    paintView = 0; paintPath[0] = 0; pSizeIx = 77; pColor = 0; paintScroll = 0;
    paintLoadSess();
    *view = paintView; strcpy(path, paintPath); *sizeix = pSizeIx; *color = pColor; *scroll = paintScroll;
}
}

// ---- el lienzo abierto: un fotograma del dedo / un toque ----
extern "C" {
void ref_pcv_begin(int nav, const char *path, int sizeix, int color)
{
    g_nav = nav; snprintf(paintPath, sizeof(paintPath), "%s", path); pSizeIx = sizeix; pColor = (uint16_t)color;
    static int16_t buf[2 * PAINT_MAX_PTS];
    pStroke = buf; pStrokeN = 0; pPrevX = pPrevY = -1; paintView = 1;
    g_dlog.clear(); g_marks = g_redraws = g_tools = g_reloads = g_gals = 0;
    paintDocLayout();
}
void ref_pcv_frame(int down, int tap, int x, int y) { T.down = down != 0; T.tap = tap != 0; T.x = x; T.y = y; paintCanvasTick(); }
void ref_pcv_state(int *view, int *sizeix, int *color, int *stroken, int *marks, int *redraws, int *tools, int *reloads)
{
    *view = paintView; *sizeix = pSizeIx; *color = pColor; *stroken = pStrokeN; *marks = g_marks; *redraws = g_redraws; *tools = g_tools; *reloads = g_reloads;
}
const char *ref_pcv_log(void) { return g_dlog.c_str(); }
void ref_pcv_log_clear(void) { g_dlog.clear(); }
}
