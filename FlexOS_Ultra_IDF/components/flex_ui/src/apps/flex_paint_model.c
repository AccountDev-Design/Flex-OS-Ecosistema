#include "flex_paint_model.h"

#include <stdlib.h>
#include <string.h>

#define SCR_W 480
#define SCR_H 800

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static bool reserve(flex_fxp_t *f, size_t need)
{
    if (need > FLEX_FXP_MAX_BYTES) {
        return false;
    }
    if (need <= f->cap) {
        return true;
    }
    size_t nc = f->cap ? f->cap : 1024;
    while (nc < need) {
        nc *= 2;
    }
    uint8_t *p = realloc(f->d, nc);
    if (!p) {
        return false;
    }
    f->d = p;
    f->cap = nc;
    return true;
}

void flex_fxp_free(flex_fxp_t *f)
{
    free(f->d);
    memset(f, 0, sizeof(*f));
}

static void put_hdr(uint8_t *h, uint16_t w, uint16_t hh, uint16_t strokes)
{
    memcpy(h, "FXP1", 4);
    wr16(h + 4, w);
    wr16(h + 6, hh);
    wr16(h + 8, strokes);
    wr16(h + 10, 0);
}

bool flex_fxp_create(flex_fxp_t *f, uint16_t w, uint16_t h)
{
    f->len = 0;
    if (!reserve(f, FLEX_FXP_HDR_LEN)) {
        return false;
    }
    put_hdr(f->d, w, h, 0);
    f->len = FLEX_FXP_HDR_LEN;
    return true;
}

bool flex_fxp_header(const uint8_t *d, size_t n, uint16_t *w, uint16_t *h, uint16_t *strokes)
{
    if (!d || n < FLEX_FXP_HDR_LEN || memcmp(d, "FXP1", 4) != 0) {
        return false;
    }
    if (w) {
        *w = rd16(d + 4);
    }
    if (h) {
        *h = rd16(d + 6);
    }
    if (strokes) {
        *strokes = rd16(d + 8);
    }
    return true;
}

bool flex_fxp_load(flex_fxp_t *f, const uint8_t *data, size_t n)
{
    uint16_t w, h, strokes;
    if (!flex_fxp_header(data, n, &w, &h, &strokes) || n > FLEX_FXP_MAX_BYTES) {
        return false;
    }
    // trazos completos: el ultimo, si quedo a medias, se recorta (y el contador se corrige)
    size_t off = FLEX_FXP_HDR_LEN;
    uint16_t good = 0;
    for (uint16_t s = 0; s < strokes; s++) {
        if (off + FLEX_FXP_STROKE_LEN > n) {
            break;
        }
        size_t body = (size_t)rd16(data + off + 4) * 4;
        if (off + FLEX_FXP_STROKE_LEN + body > n) {
            break;
        }
        off += FLEX_FXP_STROKE_LEN + body;
        good++;
    }
    if (!reserve(f, off)) {
        return false;
    }
    memcpy(f->d, data, off);
    f->len = off;
    wr16(f->d + 8, good);
    return true;
}

bool flex_fxp_append(flex_fxp_t *f, uint16_t color, uint8_t size, const int16_t *xy, uint16_t pts)
{
    if (!f->d || f->len < FLEX_FXP_HDR_LEN || !xy || pts == 0) {
        return false;
    }
    uint16_t strokes = rd16(f->d + 8);
    size_t add = FLEX_FXP_STROKE_LEN + (size_t)pts * 4;
    if (strokes == 0xFFFF || !reserve(f, f->len + add)) {
        return false;
    }
    uint8_t *p = f->d + f->len;
    wr16(p, color);
    p[2] = size;
    p[3] = 0;
    wr16(p + 4, pts);
    for (uint16_t i = 0; i < pts; i++) {
        wr16(p + 6 + i * 4, (uint16_t)xy[i * 2]);
        wr16(p + 6 + i * 4 + 2, (uint16_t)xy[i * 2 + 1]);
    }
    f->len += add;
    wr16(f->d + 8, (uint16_t)(strokes + 1));
    return true;
}

bool flex_fxp_undo(flex_fxp_t *f)
{
    if (!f->d || f->len < FLEX_FXP_HDR_LEN) {
        return false;
    }
    uint16_t strokes = rd16(f->d + 8);
    if (strokes == 0) {
        return false;
    }
    size_t off = FLEX_FXP_HDR_LEN, last = off;
    for (uint16_t s = 0; s < strokes; s++) {
        last = off;
        off += FLEX_FXP_STROKE_LEN + (size_t)rd16(f->d + off + 4) * 4;
        if (off > f->len) {
            return false;   // incoherente: no se toca
        }
    }
    f->len = last;
    wr16(f->d + 8, (uint16_t)(strokes - 1));
    return true;
}

void flex_fxp_clear(flex_fxp_t *f)
{
    if (f->d && f->len >= FLEX_FXP_HDR_LEN) {
        wr16(f->d + 8, 0);
        f->len = FLEX_FXP_HDR_LEN;
    }
}

bool flex_fxp_replay(const uint8_t *d, size_t n, float sc, int ox, int oy, flex_fxp_seg_fn cb, void *user)
{
    uint16_t strokes;
    if (!cb || !flex_fxp_header(d, n, NULL, NULL, &strokes)) {
        return false;
    }
    size_t off = FLEX_FXP_HDR_LEN;
    for (uint16_t s = 0; s < strokes; s++) {
        if (off + FLEX_FXP_STROKE_LEN > n) {
            break;
        }
        uint16_t color = rd16(d + off);
        uint8_t size = d[off + 2];
        uint16_t pts = rd16(d + off + 4);
        off += FLEX_FXP_STROKE_LEN;
        // el radio tambien se escala: si no, la miniatura saldria con trazos gruesisimos
        int rad = (int)(size * sc + 0.5f);
        if (rad < 1) {
            rad = 1;
        }
        int px = 0, py = 0;
        for (uint16_t i = 0; i < pts; i++) {
            if (off + 4 > n) {
                return true;   // lectura corta: se detiene sin mas
            }
            int16_t xv = (int16_t)rd16(d + off), yv = (int16_t)rd16(d + off + 2);
            off += 4;
            int x = ox + (int)(xv * sc + 0.5f);
            int y = oy + (int)(yv * sc + 0.5f);
            if (i == 0) {
                cb(x, y, x, y, color, rad, user);
            } else {
                cb(px, py, x, y, color, rad, user);
            }
            px = x;
            py = y;
        }
    }
    return true;
}

// ---- lienzo -----------------------------------------------------------------------------------------------------------------------------------
const uint16_t flex_paint_pal[6] = {
    // rgb565(30,30,40), (230,60,60), (240,150,40), (240,210,50), (80,180,120), (60,120,235)
    (uint16_t)(((30 & 0xF8) << 8) | ((30 & 0xFC) << 3) | (40 >> 3)),
    (uint16_t)(((230 & 0xF8) << 8) | ((60 & 0xFC) << 3) | (60 >> 3)),
    (uint16_t)(((240 & 0xF8) << 8) | ((150 & 0xFC) << 3) | (40 >> 3)),
    (uint16_t)(((240 & 0xF8) << 8) | ((210 & 0xFC) << 3) | (50 >> 3)),
    (uint16_t)(((80 & 0xF8) << 8) | ((180 & 0xFC) << 3) | (120 >> 3)),
    (uint16_t)(((60 & 0xF8) << 8) | ((120 & 0xFC) << 3) | (235 >> 3)),
};
const uint8_t flex_paint_sizes[3] = {3, 6, 12};

int flex_paint_bot(int nav_h)
{
    return SCR_H - nav_h - 66;
}

int flex_paint_ch(int nav_h)
{
    return flex_paint_bot(nav_h) - FLEX_PAINT_TOP;
}

void flex_paint_doc_layout(flex_paint_doc_t *d, int hdr_w, int hdr_h, int nav_h)
{
    int ch = flex_paint_ch(nav_h);
    d->doc_w = FLEX_PAINT_CW;
    d->doc_h = ch;
    if (hdr_w > 0 && hdr_h > 0) {
        d->doc_w = hdr_w;
        d->doc_h = hdr_h;
    }
    float sx = (float)FLEX_PAINT_CW / (float)d->doc_w;
    float sy = (float)ch / (float)d->doc_h;
    d->scale = sx < sy ? sx : sy;
    if (d->scale <= 0.0f) {
        d->scale = 1.0f;
    }
    d->disp_w = (int)(d->doc_w * d->scale + 0.5f);
    d->disp_h = (int)(d->doc_h * d->scale + 0.5f);
    if (d->disp_w < 1) {
        d->disp_w = 1;
    }
    if (d->disp_w > FLEX_PAINT_CW) {
        d->disp_w = FLEX_PAINT_CW;
    }
    if (d->disp_h < 1) {
        d->disp_h = 1;
    }
    if (d->disp_h > ch) {
        d->disp_h = ch;
    }
    d->x = FLEX_PAINT_CX + (FLEX_PAINT_CW - d->disp_w) / 2;
    d->y = FLEX_PAINT_TOP + (ch - d->disp_h) / 2;
}

bool flex_paint_doc_hit(const flex_paint_doc_t *d, int x, int y)
{
    return x >= d->x && x < d->x + d->disp_w && y >= d->y && y < d->y + d->disp_h;
}

int16_t flex_paint_doc_coord(const flex_paint_doc_t *d, int v, int org, int lim)
{
    int q = (int)(((float)(v - org) / d->scale) + 0.5f);
    if (q < 0) {
        q = 0;
    }
    if (q >= lim) {
        q = lim - 1;
    }
    return (int16_t)q;
}

uint8_t flex_paint_doc_radius(const flex_paint_doc_t *d, int brush)
{
    int r = (int)((float)brush / d->scale + 0.5f);
    if (r < 1) {
        r = 1;
    }
    if (r > 255) {
        r = 255;
    }
    return (uint8_t)r;
}

int flex_paint_tool_hit(int x, int y, int nav_h)
{
    int yy = flex_paint_bot(nav_h) + 8, sw = 34, gap = 6, x0 = 10;
    if (y < yy - 4 || y > yy + sw + 4) {
        return FLEX_PT_NONE;
    }
    for (int i = 0; i < 6; i++) {
        int cx = x0 + i * (sw + gap) + sw / 2;
        if (x >= cx - sw / 2 && x <= cx + sw / 2) {
            return i;
        }
    }
    int gx = x0 + 6 * (sw + gap) + 8;
    if (x >= gx && x < gx + 44) {
        return FLEX_PT_SIZE;
    }
    if (x >= gx + 52 && x < gx + 128) {
        return FLEX_PT_UNDO;
    }
    if (x >= gx + 134) {
        return FLEX_PT_CLEAR;
    }
    return FLEX_PT_NONE;
}

// ---- lienzo abierto -----------------------------------------------------------------------------------------------------------------------------
void flex_pcv_init(flex_pcv_t *c, int nav_h, uint16_t color, int size_ix)
{
    flex_fxp_free(&c->doc);
    memset(c, 0, sizeof(*c));
    c->nav_h = nav_h;
    c->color = color;
    c->size_ix = (uint8_t)((size_ix >= 0 && size_ix < 3) ? size_ix : 1);
    c->px = c->py = -1;
    flex_paint_doc_layout(&c->lay, 0, 0, nav_h);
}

void flex_pcv_free(flex_pcv_t *c)
{
    flex_fxp_free(&c->doc);
    c->n = 0;
    c->px = c->py = -1;
}

static void layout_from_doc(flex_pcv_t *c)
{
    uint16_t w = 0, h = 0, s = 0;
    if (!flex_fxp_header(c->doc.d, c->doc.len, &w, &h, &s)) {
        w = h = 0;
    }
    flex_paint_doc_layout(&c->lay, w, h, c->nav_h);
}

bool flex_pcv_load(flex_pcv_t *c, const uint8_t *data, size_t n)
{
    flex_pcv_free(c);
    if (!flex_fxp_load(&c->doc, data, n)) {
        flex_fxp_free(&c->doc);
        return false;
    }
    layout_from_doc(c);
    return true;
}

bool flex_pcv_new(flex_pcv_t *c)
{
    flex_pcv_free(c);
    if (!flex_fxp_create(&c->doc, (uint16_t)FLEX_PAINT_CW, (uint16_t)flex_paint_ch(c->nav_h))) {
        flex_fxp_free(&c->doc);
        return false;
    }
    layout_from_doc(c);
    return true;
}

bool flex_pcv_is_open(const flex_pcv_t *c)
{
    return c->doc.d != NULL && c->doc.len >= FLEX_FXP_HDR_LEN;
}

int flex_pcv_strokes(const flex_pcv_t *c)
{
    uint16_t s = 0;
    return flex_fxp_header(c->doc.d, c->doc.len, NULL, NULL, &s) ? (int)s : 0;
}

bool flex_pcv_flush(flex_pcv_t *c)
{
    bool ok = true;
    if (c->n > 0 && flex_pcv_is_open(c)) {
        ok = flex_fxp_append(&c->doc, c->color, flex_paint_doc_radius(&c->lay, flex_paint_sizes[c->size_ix]), c->xy, (uint16_t)c->n);
        if (ok) {
            c->modified = true;
        } else {
            c->full = true;   // Arduino no tenia tope; aqui el trazo que no cabe se avisa en vez de perderse sin decir nada
        }
    }
    c->n = 0;
    c->px = c->py = -1;
    return ok;
}

bool flex_pcv_dirty(const flex_pcv_t *c)
{
    return c->n > 0;
}

void flex_pcv_ack(flex_pcv_t *c)
{
    c->modified = c->redraw = c->tools = c->sess = c->full = c->back = false;
}

bool flex_pcv_touch(flex_pcv_t *c, bool down, int x, int y, const flex_pcv_draw_t *draw, void *user)
{
    if (down && flex_pcv_is_open(c) && flex_paint_doc_hit(&c->lay, x, y)) {
        int rad = flex_paint_sizes[c->size_ix];
        if (c->px >= 0) {
            // se descartan los puntos casi pegados: el GT911 entrega muestras muy juntas y guardarlas todas engorda el fichero
            int dx = x - c->px, dy = y - c->py;
            if (dx * dx + dy * dy < 4) {
                return true;
            }
            if (draw && draw->seg) {
                draw->seg(c->px, c->py, x, y, rad, c->color, user);
            }
        } else if (draw && draw->dot) {
            draw->dot(x, y, rad, c->color, user);
        }
        int16_t cx = flex_paint_doc_coord(&c->lay, x, c->lay.x, c->lay.doc_w);
        int16_t cy = flex_paint_doc_coord(&c->lay, y, c->lay.y, c->lay.doc_h);
        if (c->n >= FLEX_FXP_MAX_PTS) {
            // Trazo larguisimo: se cierra y se empieza otro desde el ultimo punto. Arduino ponia en el nuevo solo ese punto y
            // descartaba el actual (un segmento dibujado que no quedaba en el fichero); aqui el nuevo lleva los dos.
            int lx = c->px, ly = c->py;
            flex_pcv_flush(c);
            c->xy[0] = flex_paint_doc_coord(&c->lay, lx, c->lay.x, c->lay.doc_w);
            c->xy[1] = flex_paint_doc_coord(&c->lay, ly, c->lay.y, c->lay.doc_h);
            c->n = 1;
        }
        c->xy[c->n * 2] = cx;
        c->xy[c->n * 2 + 1] = cy;
        c->n++;
        c->px = x;
        c->py = y;
        return true;
    }
    // dedo levantado: el trazo se ESCRIBE (se pasa al documento)
    if (!down && c->n > 0) {
        flex_pcv_flush(c);
    }
    if (!down) {
        c->px = c->py = -1;
    }
    return false;
}

flex_pcv_tap_t flex_pcv_tap(flex_pcv_t *c, int x, int y)
{
    if (x < 48 && y < 48) {   // volver a la galeria
        flex_pcv_flush(c);
        c->back = true;
        c->sess = true;
        return FLEX_PCT_BACK;
    }
    int t = flex_paint_tool_hit(x, y, c->nav_h);
    if (t >= 0 && t < 6) {
        c->color = flex_paint_pal[t];
        c->tools = c->sess = true;
        return FLEX_PCT_COLOR;
    }
    if (t == FLEX_PT_SIZE) {
        c->size_ix = (uint8_t)((c->size_ix + 1) % 3);
        c->tools = c->sess = true;
        return FLEX_PCT_SIZE;
    }
    if (t == FLEX_PT_UNDO) {
        if (flex_pcv_is_open(c) && flex_fxp_undo(&c->doc)) {   // DESHACER real: recorta el documento
            c->modified = c->redraw = true;
        }
        return FLEX_PCT_UNDO;
    }
    if (t == FLEX_PT_CLEAR) {
        if (flex_pcv_is_open(c) && flex_pcv_strokes(c) > 0) {   // LIMPIAR real: deja el dibujo a 0 trazos (sin reescribir uno ya vacio)
            flex_fxp_clear(&c->doc);
            c->modified = c->redraw = true;
        }
        return FLEX_PCT_CLEAR;
    }
    return FLEX_PCT_NONE;
}

// ---- galeria -----------------------------------------------------------------------------------------------------------------------------------
void flex_pg_card_rect(int i, int scroll, int nav_h, int *x, int *y, int *w, int *h)
{
    int col = i % FLEX_PG_COLS, row = i / FLEX_PG_COLS;
    *w = (SCR_W - 3 * 16) / FLEX_PG_COLS;
    *h = (int)(*w * (float)flex_paint_ch(nav_h) / (float)FLEX_PAINT_CW);   // misma proporcion que el lienzo
    if (*h > 340) {
        *h = 340;
    }
    *x = 16 + col * (*w + 16);
    *y = FLEX_PG_CARD_TOP + row * (*h + 46) - scroll;
}

int flex_pg_max_scroll(int n, int nav_h)
{
    int x, y, w, h;
    flex_pg_card_rect(0, 0, nav_h, &x, &y, &w, &h);
    int rows = (n + FLEX_PG_COLS - 1) / FLEX_PG_COLS;
    int need = FLEX_PG_CARD_TOP + rows * (h + 46) + 40;
    int m = need - (SCR_H - 60);
    return m > 0 ? m : 0;
}

int flex_pg_card_at(int n, int scroll, int nav_h, int px, int py)
{
    for (int i = 0; i < n; i++) {
        int x, y, w, h;
        flex_pg_card_rect(i, scroll, nav_h, &x, &y, &w, &h);
        if (px >= x && px <= x + w && py >= y && py <= y + h) {
            return i;
        }
    }
    return -1;
}

void flex_pg_fab(int *x, int *y, int *r)
{
    *r = 52;
    *x = SCR_W - 76;
    *y = SCR_H - 146;
}

bool flex_pg_fab_hit(int px, int py)
{
    int fx, fy, fr;
    flex_pg_fab(&fx, &fy, &fr);
    long dx = px - fx, dy = py - fy;
    return dx * dx + dy * dy <= (long)fr * fr;
}

void flex_pg_thumb(int card_x, int card_y, int card_w, int card_h, int doc_w, int doc_h, float *sc, int *ox, int *oy)
{
    float scx = (float)(card_w - 8) / (float)doc_w;
    float scy = (float)(card_h - 8) / (float)doc_h;
    *sc = scx < scy ? scx : scy;
    int rw = (int)(doc_w * *sc + 0.5f), rh = (int)(doc_h * *sc + 0.5f);
    *ox = card_x + (card_w - rw) / 2;
    *oy = card_y + (card_h - rh) / 2;
}

// ---- sesion --------------------------------------------------------------------------------------------------------------------------------------
void flex_paint_sess_pack(flex_paint_sess_t *s, bool canvas_open, const char *path, int size_ix, uint16_t color, int scroll)
{
    memset(s, 0, sizeof(*s));
    s->view = (canvas_open && path && path[0]) ? 1 : 0;
    s->size_ix = (uint8_t)size_ix;
    s->color = color;
    s->scroll = scroll;
    if (path) {
        strncpy(s->path, path, sizeof(s->path) - 1);
    }
}

void flex_paint_sess_normalize(flex_paint_sess_t *s)
{
    s->path[sizeof(s->path) - 1] = '\0';
    if (!(s->view == 1 && s->path[0])) {
        s->view = 0;
        s->path[0] = '\0';
    }
    if (s->size_ix >= 3) {
        s->size_ix = 1;
    }
    if (s->scroll < 0) {
        s->scroll = 0;
    }
}
