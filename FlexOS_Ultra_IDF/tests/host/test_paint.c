// Paint (docs/spec/03 §7): formato .fxp y sus operaciones, geometria del lienzo y de la galeria y sesion paint.bin v1 frente a
// FlexOS_FS.cpp y AppPaint.h de Arduino, con ficheros de verdad (en memoria) y con ficheros cortados a mitad de un trazo.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_paint_model.h"
#include "flex_test.h"

void ref_paint_reset(void);
int ref_paint_create(const char *p, int w, int h);
int ref_paint_append(const char *p, int color, int size, const short *xy, int pts);
int ref_paint_undo(const char *p);
int ref_paint_clear(const char *p);
int ref_paint_bytes(const char *p, unsigned char *out, int cap);
int ref_paint_files(char *out, int cap);
void ref_paint_put(const char *p, const unsigned char *d, int n);
int ref_paint_replay(const char *p, float sc, int ox, int oy, char *out, int cap);
int ref_paint_header(const char *p, int *w, int *h, int *s);
void ref_paint_doc(int nav, const char *path, int sizeix, int *dw, int *dh, int *x, int *y, int *dispw, int *disph, float *sc, int *bot, int *ch, int *rad);
int ref_paint_hit(int x, int y);
int ref_paint_coord(int v, int org, int lim);
void ref_paint_card(int nav, int n, int scroll, int i, int *x, int *y, int *w, int *h, int *maxs);
void ref_paint_fab(int *x, int *y, int *r);
unsigned ref_paint_pal(int i);
int ref_paint_size(int i);
int ref_paint_save(int view, const char *path, int sizeix, int color, int scroll, unsigned char *out);
void ref_paint_load(const unsigned char *in, int n, int exists, int *view, char *path, int *sizeix, int *color, int *scroll);

void ref_pcv_begin(int nav, const char *path, int sizeix, int color);
void ref_pcv_frame(int down, int tap, int x, int y);
void ref_pcv_state(int *view, int *sizeix, int *color, int *stroken, int *marks, int *redraws, int *tools, int *reloads);
const char *ref_pcv_log(void);
void ref_pcv_log_clear(void);

static uint32_t s_seed = 31337;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static char g_log[1 << 16];
static int g_ln;
static void seg(int x0, int y0, int x1, int y1, uint16_t c, int r, void *u)
{
    (void)u;
    g_ln += snprintf(g_log + g_ln, sizeof(g_log) - (size_t)g_ln, "%d,%d,%d,%d,%u,%d;", x0, y0, x1, y1, (unsigned)c, r);
}

#define P "/Paint/t.fxp"


// ---- el lienzo abierto: dedo y toques frente a paintCanvasTick ------------------------------------------------------------------------
static char g_mlog[1 << 16];
static int g_mln;
static void m_dot(int x, int y, int rad, uint16_t c, void *u)
{
    (void)u;
    g_mln += snprintf(g_mlog + g_mln, sizeof(g_mlog) - (size_t)g_mln, "D%d,%d,%d,%u;", x, y, rad, (unsigned)c);
}
static void m_seg(int x0, int y0, int x1, int y1, int rad, uint16_t c, void *u)
{
    (void)u;
    g_mln += snprintf(g_mlog + g_mln, sizeof(g_mlog) - (size_t)g_mln, "S%d,%d,%d,%d,%d,%u;", x0, y0, x1, y1, rad, (unsigned)c);
}
static const flex_pcv_draw_t M_DRAW = {m_dot, m_seg};

static flex_pcv_t g_cv;   // 2 KB de trazo + el dibujo: fuera de la pila

// Cuenta de lo que la interfaz recibiria, para compararlo con los avisos de Arduino
typedef struct {
    int marks, redraws, tools;
} cv_counts_t;

static int cv_drain(cv_counts_t *k)
{
    int back = g_cv.back;
    k->marks += g_cv.sess;
    k->redraws += g_cv.redraw;
    k->tools += g_cv.tools;
    flex_pcv_ack(&g_cv);
    return back;
}

// Compara un fotograma: lo dibujado, el estado, los contadores y el fichero, byte a byte.
static int cv_compare(const cv_counts_t *k, int skew_redraws, bool backed)
{
    int rview, rsix, rcol, rn, rmarks, rred, rtools, rrel;
    ref_pcv_state(&rview, &rsix, &rcol, &rn, &rmarks, &rred, &rtools, &rrel);
    int bad = 0;
    bad += strcmp(ref_pcv_log(), g_mlog) != 0;
    bad += rsix != g_cv.size_ix || rcol != g_cv.color || rn != g_cv.n;
    bad += rmarks != k->marks || rred != k->redraws + skew_redraws || rtools != k->tools;
    bad += rview != (backed ? 0 : 1);
    unsigned char ref[200000];
    int rb = ref_paint_bytes(P, ref, sizeof(ref));
    bad += rb != (int)g_cv.doc.len || memcmp(ref, g_cv.doc.d, g_cv.doc.len) != 0;
    return bad;
}

static void test_paint_canvas(void)
{
    int bad = 0, frames = 0, strokes = 0, taps = 0, tools = 0, discarded = 0;
    for (int seq = 0; seq < 600; seq++) {
        static const int navs[3] = {0, 48, 64};
        int nav = navs[rnd() % 3];
        ref_paint_reset();
        int w = FLEX_PAINT_CW, h = flex_paint_ch(nav);
        if (rnd() % 2) {   // un dibujo de otro tamano: letterbox
            w = 100 + (int)(rnd() % 400);
            h = 100 + (int)(rnd() % 600);
        }
        ref_paint_create(P, w, h);
        unsigned char fb[64];
        int fn = ref_paint_bytes(P, fb, sizeof(fb));
        int six = (int)(rnd() % 3);
        int color = flex_paint_pal[rnd() % 6];
        ref_pcv_begin(nav, P, six, color);
        flex_pcv_init(&g_cv, nav, (uint16_t)color, six);
        CHECK(flex_pcv_load(&g_cv, fb, (size_t)fn));
        cv_counts_t k = {0, 0, 0};
        int sk = 0;   // repintados que Arduino hace de mas (limpiar un dibujo ya vacio)
        bool backed = false;
        for (int step = 0; step < 50 && !backed; step++) {
            int kind = (int)(rnd() % 10);
            if (kind <= 6) {   // un trazo: el dedo baja, camina (a veces fuera del dibujo) y sube
                int x = g_cv.lay.x + (int)(rnd() % (unsigned)g_cv.lay.disp_w), y = g_cv.lay.y + (int)(rnd() % (unsigned)g_cv.lay.disp_h);
                int nf = 1 + (int)(rnd() % 70);
                strokes++;
                for (int f = 0; f < nf; f++) {
                    int mv = (rnd() % 4 == 0) ? (int)(rnd() % 3) - 1 : (int)(rnd() % 25) - 12;   // a menudo casi pegado: se descarta
                    x += mv;
                    y += (rnd() % 4 == 0) ? (int)(rnd() % 3) - 1 : (int)(rnd() % 25) - 12;
                    if (rnd() % 40 == 0) x += 200 - (int)(rnd() % 400);   // salto: fuera del lienzo y de vuelta
                    g_mln = 0;
                    g_mlog[0] = 0;
                    ref_pcv_log_clear();
                    ref_pcv_frame(1, 0, x, y);
                    flex_pcv_touch(&g_cv, true, x, y, &M_DRAW, NULL);
                    cv_drain(&k);
                    bad += cv_compare(&k, sk, false);
                    frames++;
                }
                int tapup = (rnd() % 3 == 0);   // un toque corto sobre el dibujo: baja, sube con "tap"
                g_mln = 0;
                g_mlog[0] = 0;
                ref_pcv_log_clear();
                ref_pcv_frame(0, tapup, x, y);
                bool used = flex_pcv_touch(&g_cv, false, x, y, &M_DRAW, NULL);
                CHECK(!used);
                if (tapup) {
                    flex_pcv_tap(&g_cv, x, y);
                }
                backed = cv_drain(&k) != 0;
                bad += cv_compare(&k, sk, backed);
                frames++;
            } else {   // un toque suelto
                int x, y;
                int where = (int)(rnd() % 20);
                if (where < 10) {   // la barra de herramientas
                    x = (int)(rnd() % 480);
                    y = flex_paint_bot(nav) + 2 + (int)(rnd() % 48);
                } else if (where < 12) {   // volver
                    x = (int)(rnd() % 60);
                    y = (int)(rnd() % 60);
                } else {
                    x = (int)(rnd() % 480);
                    y = (int)(rnd() % 800);
                }
                int pre_strokes = flex_pcv_strokes(&g_cv);
                int skew = 0;
                int th = flex_paint_tool_hit(x, y, nav);
                if (!(x < 48 && y < 48) && th == FLEX_PT_CLEAR && pre_strokes == 0) {
                    skew = 1;   // Arduino repinta al limpiar un dibujo ya vacio; aqui no se repinta ni se reescribe
                }
                g_mln = 0;
                g_mlog[0] = 0;
                ref_pcv_log_clear();
                ref_pcv_frame(0, 1, x, y);
                flex_pcv_touch(&g_cv, false, x, y, &M_DRAW, NULL);
                flex_pcv_tap(&g_cv, x, y);
                backed = cv_drain(&k) != 0;
                sk += skew;
                bad += cv_compare(&k, sk, backed);
                taps++;
                tools += th != FLEX_PT_NONE;
                if (skew) {
                    discarded++;
                }
            }
            if (bad) {
                break;
            }
        }
        flex_pcv_free(&g_cv);
        if (bad) {
            printf("PAINT canvas: sequence %d diverged\n", seq);
            break;
        }
    }
    printf("lienzo de Paint: %d fotogramas, %d trazos, %d toques (%d en herramientas) iguales a Arduino\n", frames, strokes, taps, tools);
    CHECK(bad == 0 && strokes > 5000 && taps > 3000 && tools > 1000);
    (void)discarded;
}


// Trazo larguisimo: se parte cada 512 puntos y NO se pierde ningun segmento dibujado (Arduino descartaba el punto de corte).
static int g_long_dots, g_long_segs;
static void l_dot(int x, int y, int rad, uint16_t c, void *u) { (void)x; (void)y; (void)rad; (void)c; (void)u; g_long_dots++; }
static void l_seg(int x0, int y0, int x1, int y1, int rad, uint16_t c, void *u) { (void)x0; (void)y0; (void)x1; (void)y1; (void)rad; (void)c; (void)u; g_long_segs++; }

static void test_paint_canvas_extra(void)
{
    const flex_pcv_draw_t d = {l_dot, l_seg};
    flex_pcv_init(&g_cv, 64, flex_paint_pal[1], 1);
    CHECK(flex_pcv_new(&g_cv) && flex_pcv_is_open(&g_cv) && flex_pcv_strokes(&g_cv) == 0);
    CHECK(g_cv.lay.doc_w == FLEX_PAINT_CW && g_cv.lay.doc_h == flex_paint_ch(64) && g_cv.lay.disp_w == FLEX_PAINT_CW && g_cv.lay.disp_h == flex_paint_ch(64));   // el lienzo entero
    int pts = 0;
    // zigzag que pasa de los 1100 puntos dentro del dibujo, siempre a mas de 2 px del anterior
    for (int i = 0; i < 1300; i++) {
        int x = g_cv.lay.x + 10 + (i % 2) * 30 + (i / 100), y = g_cv.lay.y + 10 + (i * 3) % 400;
        if (flex_pcv_touch(&g_cv, true, x, y, &d, NULL)) {
            pts++;
        }
    }
    CHECK(flex_pcv_dirty(&g_cv) && g_cv.n > 0 && g_cv.n <= FLEX_FXP_MAX_PTS);
    CHECK(!flex_pcv_touch(&g_cv, false, 0, 0, &d, NULL) && !flex_pcv_dirty(&g_cv) && g_cv.px == -1);
    CHECK(g_long_dots == 1 && g_long_segs == pts - 1);
    int ns = flex_pcv_strokes(&g_cv);
    CHECK(ns == 3);   // 1300 puntos dibujados -> 512 + 512 + el resto, repitiendo el de corte
    size_t off = FLEX_FXP_HDR_LEN;
    int stored = 0, prev_last_x = -1, prev_last_y = -1, joined = 0;
    for (int sidx = 0; sidx < ns; sidx++) {
        int n = g_cv.doc.d[off + 4] | (g_cv.doc.d[off + 5] << 8);
        CHECK(n >= 1 && n <= FLEX_FXP_MAX_PTS);
        int fx = g_cv.doc.d[off + 6] | (g_cv.doc.d[off + 7] << 8), fy = g_cv.doc.d[off + 8] | (g_cv.doc.d[off + 9] << 8);
        if (sidx > 0 && fx == prev_last_x && fy == prev_last_y) {
            joined++;
        }
        size_t lp = off + 6 + (size_t)(n - 1) * 4;
        prev_last_x = g_cv.doc.d[lp] | (g_cv.doc.d[lp + 1] << 8);
        prev_last_y = g_cv.doc.d[lp + 2] | (g_cv.doc.d[lp + 3] << 8);
        stored += n;
        off += FLEX_FXP_STROKE_LEN + (size_t)n * 4;
    }
    CHECK(joined == ns - 1);          // cada trazo arranca donde acabo el anterior: el dibujo no tiene huecos
    CHECK(stored == pts + (ns - 1));  // y cada punto dibujado esta guardado
    CHECK(off == g_cv.doc.len);

    // El tope de tamano: un trazo que no cabe se avisa (full) y no se pierde el resto del dibujo
    flex_pcv_ack(&g_cv);
    flex_pcv_init(&g_cv, 64, flex_paint_pal[0], 0);
    CHECK(flex_pcv_new(&g_cv));
    int guard = 0;
    while (!g_cv.full && guard++ < 1000) {
        for (int i = 0; i < FLEX_FXP_MAX_PTS; i++) {
            flex_pcv_touch(&g_cv, true, g_cv.lay.x + 5 + (i % 2) * 20, g_cv.lay.y + 5 + i, &d, NULL);
        }
        flex_pcv_touch(&g_cv, false, 0, 0, &d, NULL);
    }
    CHECK(g_cv.full && g_cv.doc.len <= FLEX_FXP_MAX_BYTES && g_cv.n == 0 && flex_pcv_strokes(&g_cv) >= 60);
    size_t full_len = g_cv.doc.len;
    CHECK(flex_pcv_tap(&g_cv, 27, flex_paint_bot(64) + 20) == FLEX_PCT_COLOR && g_cv.doc.len == full_len);   // sigue funcionando
    flex_pcv_ack(&g_cv);
    CHECK(flex_pcv_tap(&g_cv, 330, flex_paint_bot(64) + 20) == FLEX_PCT_UNDO && g_cv.modified && g_cv.redraw && g_cv.doc.len < full_len);
    flex_pcv_free(&g_cv);

    // Ficheros que no son dibujos: no se abren, y un dibujo cortado se recorta al ultimo trazo completo
    flex_pcv_init(&g_cv, 64, flex_paint_pal[0], 1);
    CHECK(!flex_pcv_load(&g_cv, (const uint8_t *)"", 0) && !flex_pcv_is_open(&g_cv));
    CHECK(!flex_pcv_load(&g_cv, (const uint8_t *)"FXP2xxxxxxxx", 12) && !flex_pcv_is_open(&g_cv));
    CHECK(!flex_pcv_load(&g_cv, (const uint8_t *)"FXP1", 4) && !flex_pcv_is_open(&g_cv));
    static uint8_t huge[FLEX_FXP_MAX_BYTES + 100];
    memcpy(huge, "FXP1\x01\x00\x01\x00\x00\x00\x00\x00", 12);
    CHECK(!flex_pcv_load(&g_cv, huge, sizeof(huge)));
    // sin dibujo abierto, un dedo no escribe nada ni revienta
    CHECK(!flex_pcv_touch(&g_cv, true, 100, 200, &d, NULL) && g_cv.n == 0);
    CHECK(flex_pcv_tap(&g_cv, 330, flex_paint_bot(64) + 20) == FLEX_PCT_UNDO && !g_cv.modified);
    CHECK(flex_pcv_tap(&g_cv, 400, flex_paint_bot(64) + 20) == FLEX_PCT_CLEAR && !g_cv.modified);
    flex_pcv_free(&g_cv);
}

void test_paint(void)
{
    int bad = 0, ops = 0, appends = 0, undos = 0;
    // ---- operaciones: el mismo fichero, byte a byte, tras secuencias aleatorias de crear / trazar / deshacer / limpiar
    for (int seq = 0; seq < 300; seq++) {
        ref_paint_reset();
        flex_fxp_t f = {0};
        uint16_t w = (uint16_t)(100 + rnd() % 400), h = (uint16_t)(100 + rnd() % 600);
        ref_paint_create(P, w, h);
        flex_fxp_create(&f, w, h);
        for (int step = 0; step < 40; step++) {
            int op = (int)(rnd() % 8);
            ops++;
            if (op <= 4) {
                short xy[2 * FLEX_FXP_MAX_PTS];
                int pts = 1 + (int)(rnd() % 60);
                for (int i = 0; i < 2 * pts; i++) xy[i] = (short)(rnd() % 700);
                int color = (int)(rnd() % 65536), size = 1 + (int)(rnd() % 40);
                int r1 = ref_paint_append(P, color, size, xy, pts);
                int r2 = flex_fxp_append(&f, (uint16_t)color, (uint8_t)size, xy, (uint16_t)pts);
                if (r1 != r2 && bad == 0) printf("PAINT append r1=%d r2=%d len=%zu\n", r1, r2, f.len);
                bad += r1 != r2;
                appends++;
            } else if (op <= 6) {
                int r1 = ref_paint_undo(P), r2 = flex_fxp_undo(&f);
                if (r1 != r2 && bad == 0) printf("PAINT undo r1=%d r2=%d\n", r1, r2);
                bad += r1 != r2;
                undos++;
            } else {
                int r1 = ref_paint_clear(P), r2 = (flex_fxp_clear(&f), 1);
                if (r1 != r2 && bad == 0) printf("PAINT clear r1=%d r2=%d\n", r1, r2);
                bad += r1 != r2;
            }
            unsigned char ref[200000];
            int rn = ref_paint_bytes(P, ref, sizeof(ref));
            if (rn != (int)f.len || memcmp(ref, f.d, f.len) != 0) {
                if (bad == 0) printf("PAINT op=%d step=%d ref=%d fw=%zu\n", op, step, rn, f.len);
                bad++;
                break;
            }
        }
        flex_fxp_free(&f);
    }
    CHECK(bad == 0 && appends > 5000 && undos > 2000);
    // sin puntos y deshacer sin trazos: igual que Arduino
    {
        ref_paint_reset();
        flex_fxp_t f = {0};
        ref_paint_create(P, 10, 10);
        flex_fxp_create(&f, 10, 10);
        short xy[2] = {1, 2};
        CHECK(ref_paint_append(P, 1, 1, xy, 0) == 0 && !flex_fxp_append(&f, 1, 1, xy, 0));
        CHECK(ref_paint_undo(P) == 0 && !flex_fxp_undo(&f));
        flex_fxp_free(&f);
    }

    // ---- reproduccion: los mismos segmentos, a cualquier escala, tambien con el fichero cortado a mitad de trazo o de punto
    int rb = 0, partial = 0;
    for (int iter = 0; iter < 1500; iter++) {
        ref_paint_reset();
        flex_fxp_t f = {0};
        flex_fxp_create(&f, 464, 574);
        int ns = (int)(rnd() % 6);
        for (int s = 0; s < ns; s++) {
            short xy[2 * 80];
            int pts = 1 + (int)(rnd() % 80);
            for (int i = 0; i < 2 * pts; i++) xy[i] = (short)(rnd() % 600);
            flex_fxp_append(&f, (uint16_t)rnd(), (uint8_t)(1 + rnd() % 30), xy, (uint16_t)pts);
        }
        size_t cut = f.len;
        if (rnd() % 2 && f.len > 13) {
            cut = 12 + rnd() % (f.len - 12);   // cortado a mitad de un trazo o de un punto
            partial++;
        }
        ref_paint_put(P, f.d, (int)cut);
        float sc = (float)(1 + rnd() % 200) / 100.0f;
        int ox = (int)(rnd() % 100) - 20, oy = (int)(rnd() % 100) - 20;
        char rl[1 << 16];
        int ro = ref_paint_replay(P, sc, ox, oy, rl, sizeof(rl));
        g_ln = 0;
        g_log[0] = 0;
        int mo = flex_fxp_replay(f.d, cut, sc, ox, oy, seg, NULL);
        rb += ro != mo || strcmp(rl, g_log) != 0;
        flex_fxp_free(&f);
    }
    CHECK(rb == 0 && partial > 500);

    // ---- cabecera y carga: un fichero cortado a mitad de trazo se recorta al ultimo trazo completo (Arduino anadia detras de basura)
    {
        flex_fxp_t f = {0}, g = {0};
        flex_fxp_create(&f, 300, 400);
        short xy[2 * 4] = {1, 2, 3, 4, 5, 6, 7, 8};
        flex_fxp_append(&f, 11, 3, xy, 4);
        flex_fxp_append(&f, 22, 6, xy, 4);
        size_t full = f.len;
        CHECK(flex_fxp_load(&g, f.d, full - 5) && g.len == full - (6 + 16) && g.d[8] == 1);   // queda 1 trazo completo
        CHECK(flex_fxp_append(&g, 33, 9, xy, 2) && g.d[8] == 2);                                // y el siguiente se anade detras de el
        uint16_t w, h, s;
        CHECK(flex_fxp_header(g.d, g.len, &w, &h, &s) && w == 300 && h == 400 && s == 2);
        CHECK(!flex_fxp_load(&g, (const uint8_t *)"FXP2xxxxxxxxxxxx", 16) && !flex_fxp_load(&g, f.d, 7));   // no es un .fxp
        // contador mayor que lo que hay: se cuenta lo que hay
        f.d[8] = 9;
        CHECK(flex_fxp_load(&g, f.d, f.len) && g.d[8] == 2 && g.len == f.len);
        // el tope de tamano: no crece sin limite
        flex_fxp_free(&g);
        flex_fxp_create(&g, 10, 10);
        short big[2 * FLEX_FXP_MAX_PTS];
        memset(big, 0, sizeof(big));
        int n = 0;
        while (flex_fxp_append(&g, 1, 1, big, FLEX_FXP_MAX_PTS)) n++;
        CHECK(n > 40 && g.len <= FLEX_FXP_MAX_BYTES);
        flex_fxp_free(&f);
        flex_fxp_free(&g);
    }

    // ---- geometria del lienzo: letterbox, acotar, radio, con y sin barra de botones y con documentos de otro tamano
    int gb = 0;
    for (int nav = 0; nav <= 64; nav += 64) {
        for (int k = 0; k < 400; k++) {
            int hw = (k % 5 == 0) ? 0 : 50 + (int)(rnd() % 700), hh = (k % 5 == 0) ? 0 : 50 + (int)(rnd() % 900);
            ref_paint_reset();
            ref_paint_create("/Paint/d.fxp", hw, hh);
            int dw, dh, x, y, dispw, disph, bot, ch, rad;
            float sc;
            int six = (int)(rnd() % 3);
            ref_paint_doc(nav, hw ? "/Paint/d.fxp" : "", six, &dw, &dh, &x, &y, &dispw, &disph, &sc, &bot, &ch, &rad);
            flex_paint_doc_t d;
            flex_paint_doc_layout(&d, hw, hh, nav);
            gb += dw != d.doc_w || dh != d.doc_h || x != d.x || y != d.y || dispw != d.disp_w || disph != d.disp_h || sc != d.scale ||
                  bot != flex_paint_bot(nav) || ch != flex_paint_ch(nav) || rad != flex_paint_doc_radius(&d, flex_paint_sizes[six]);
            for (int i = 0; i < 12; i++) {
                int px = (int)(rnd() % 520) - 20, py = (int)(rnd() % 800);
                gb += ref_paint_hit(px, py) != flex_paint_doc_hit(&d, px, py);
                int v = (int)(rnd() % 1200) - 100;
                gb += ref_paint_coord(v, x, dw) != flex_paint_doc_coord(&d, v, d.x, d.doc_w);
            }
        }
        for (int n = 0; n <= FLEX_PG_MAX; n++) {
            for (int sc2 = 0; sc2 < 800; sc2 += 61) {
                for (int i = 0; i < n; i += 1 + (int)(rnd() % 3)) {
                    int rx, ry, rw, rh, rm, mx, my, mw, mh;
                    ref_paint_card(nav, n, sc2, i, &rx, &ry, &rw, &rh, &rm);
                    flex_pg_card_rect(i, sc2, nav, &mx, &my, &mw, &mh);
                    gb += rx != mx || ry != my || rw != mw || rh != mh || rm != flex_pg_max_scroll(n, nav);
                }
            }
        }
    }
    {
        int rx, ry, rr, mx, my, mr;
        ref_paint_fab(&rx, &ry, &rr);
        flex_pg_fab(&mx, &my, &mr);
        gb += rx != mx || ry != my || rr != mr;
        for (int i = 0; i < 6; i++) gb += ref_paint_pal(i) != flex_paint_pal[i];
        for (int i = 0; i < 3; i++) gb += ref_paint_size(i) != flex_paint_sizes[i];
    }
    CHECK(gb == 0);
    // hit-test de la barra y de la miniatura (los de paintCanvasTick y paintRenderGallery, segun el spec)
    CHECK(flex_paint_tool_hit(27, 670 + 20, 64) == 0 && flex_paint_tool_hit(227, 690, 64) == 5 && flex_paint_tool_hit(258 + 10, 690, 64) == FLEX_PT_SIZE &&
          flex_paint_tool_hit(310 + 10, 690, 64) == FLEX_PT_UNDO && flex_paint_tool_hit(392, 690, 64) == FLEX_PT_CLEAR && flex_paint_tool_hit(470, 690, 64) == FLEX_PT_CLEAR);
    CHECK(flex_paint_tool_hit(100, 670, 64) == FLEX_PT_NONE && flex_paint_tool_hit(100, 720, 64) == FLEX_PT_NONE);
    {
        float sc;
        int ox, oy;
        flex_pg_thumb(16, 118, 216, 267, 464, 574, &sc, &ox, &oy);
        int rw = (int)(464 * sc + 0.5f), rh = (int)(574 * sc + 0.5f);
        CHECK(rw <= 208 && rh <= 259 && ox == 16 + (216 - rw) / 2 && oy == 118 + (267 - rh) / 2);
    }

    // ---- sesion paint.bin v1
    int sb = 0;
    for (int i = 0; i < 3000; i++) {
        char path[96] = "";
        if (rnd() % 4) snprintf(path, sizeof(path), "/Paint/Dibujo %u.fxp", (unsigned)(rnd() % 100));
        int view = (int)(rnd() % 2), six = (int)(rnd() % 5), color = (int)(rnd() % 65536), scroll = (int)(rnd() % 3000) - 100;
        unsigned char ref[160];
        int rn = ref_paint_save(view, path, six, color, scroll, ref);
        flex_paint_sess_t s;
        flex_paint_sess_pack(&s, view == 1, path, six, (uint16_t)color, scroll);
        sb += rn != (int)sizeof(s) || memcmp(ref, &s, sizeof(s)) != 0;
    }
    CHECK(sizeof(flex_paint_sess_t) == 104 && sb == 0);
    int lb = 0;
    for (int i = 0; i < 3000; i++) {
        unsigned char raw[sizeof(flex_paint_sess_t)];
        for (size_t k = 0; k < sizeof(raw); k++) raw[k] = (unsigned char)(rnd() % 256);
        if (rnd() % 2) raw[0] = (unsigned char)(rnd() % 3);
        if (rnd() % 2) { raw[8] = '/'; raw[9] = 'P'; raw[10] = 0; }
        int exists = (int)(rnd() % 2);
        int rv, rs, rc, rsc;
        char rp[160];
        ref_paint_load(raw, (int)sizeof(raw), exists, &rv, rp, &rs, &rc, &rsc);
        flex_paint_sess_t s;
        memcpy(&s, raw, sizeof(s));
        flex_paint_sess_normalize(&s);
        if (s.view == 1 && !exists) {
            s.view = 0;
            s.path[0] = 0;
        }
        lb += rv != s.view || strcmp(rp, s.path) || rs != s.size_ix || rc != s.color || rsc != s.scroll;
    }
    CHECK(lb == 0);
    test_paint_canvas();
    test_paint_canvas_extra();
    printf("paint: formato .fxp, reproduccion, geometria y sesion iguales a Arduino (%d operaciones, %d reproducciones con %d cortadas)\n", ops, 1500, partial);
}
