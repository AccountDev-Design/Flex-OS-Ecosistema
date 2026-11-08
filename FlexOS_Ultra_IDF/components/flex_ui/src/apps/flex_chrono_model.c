#include "flex_chrono_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG2RAD 0.0174532925f

void flex_crono_init(flex_crono_t *c)
{
    memset(c, 0, sizeof(*c));
    c->st = FLEX_CRONO_IDLE;
    c->lap0 = 1;
    c->best = c->worst = -1;
    c->laps_dirty = c->btn_dirty = c->dial_dirty = c->bar_dirty = true;
}

// ---- 1. logica (cronoElapsed ... cronoActionRight) ------------------------------------------------------------------
uint32_t flex_crono_elapsed(const flex_crono_t *c, uint32_t now)
{
    if (c->st == FLEX_CRONO_RUN) {
        return c->accum + (uint32_t)(now - c->t0);
    }
    return c->accum;
}

uint32_t flex_crono_lap_elapsed(const flex_crono_t *c, uint32_t now)
{
    return flex_crono_elapsed(c, now) - c->lap_base;
}

bool flex_crono_active(const flex_crono_t *c)
{
    return c->st != FLEX_CRONO_IDLE;
}

int flex_crono_cur_lap_no(const flex_crono_t *c)
{
    return (int)c->lap0 + (int)c->nlaps;
}

void flex_crono_fmt(char *out, size_t n, uint32_t ms, bool cent)
{
    unsigned long h = (unsigned long)(ms / 3600000UL);
    unsigned long m = (unsigned long)((ms / 60000UL) % 60UL);
    unsigned long s = (unsigned long)((ms / 1000UL) % 60UL);
    unsigned long cc = (unsigned long)((ms % 1000UL) / 10UL);
    if (h > 0) {
        if (cent) {
            snprintf(out, n, "%lu:%02lu:%02lu.%02lu", h, m, s, cc);
        } else {
            snprintf(out, n, "%lu:%02lu:%02lu", h, m, s);
        }
    } else {
        if (cent) {
            snprintf(out, n, "%02lu:%02lu.%02lu", m, s, cc);
        } else {
            snprintf(out, n, "%02lu:%02lu", m, s);
        }
    }
}

void flex_crono_recalc_extremes(flex_crono_t *c)
{
    c->best = c->worst = -1;
    if (c->nlaps < 3) {
        return;   // comparar dos parciales no informa de nada
    }
    int b = 0, w = 0;
    for (int i = 1; i < (int)c->nlaps; i++) {
        if (c->laps[i].split < c->laps[b].split) {
            b = i;
        }
        if (c->laps[i].split > c->laps[w].split) {
            w = i;
        }
    }
    if (c->laps[b].split == c->laps[w].split) {
        return;   // todas iguales: sin extremos
    }
    c->best = (int8_t)b;
    c->worst = (int8_t)w;
}

void flex_crono_start(flex_crono_t *c, uint32_t now)
{
    if (c->st == FLEX_CRONO_RUN) {
        return;
    }
    if (c->st == FLEX_CRONO_IDLE) {
        c->accum = 0;
        c->lap_base = 0;
    }
    c->t0 = now;   // se reancla en CADA reanudacion
    c->st = FLEX_CRONO_RUN;
    c->btn_dirty = c->dial_dirty = c->bar_dirty = true;
}

void flex_crono_pause(flex_crono_t *c, uint32_t now)
{
    if (c->st != FLEX_CRONO_RUN) {
        return;
    }
    c->accum += (uint32_t)(now - c->t0);   // consolida lo corrido
    c->st = FLEX_CRONO_PAUSE;
    c->btn_dirty = c->dial_dirty = c->bar_dirty = true;
}

void flex_crono_reset(flex_crono_t *c)
{
    c->st = FLEX_CRONO_IDLE;
    c->accum = 0;
    c->t0 = 0;
    c->lap_base = 0;
    c->nlaps = 0;
    c->lap0 = 1;
    c->best = c->worst = -1;
    c->btn_dirty = c->dial_dirty = c->laps_dirty = c->bar_dirty = true;
}

void flex_crono_lap_mark(flex_crono_t *c, uint32_t now)
{
    if (c->st != FLEX_CRONO_RUN) {
        return;
    }
    uint32_t tot = flex_crono_elapsed(c, now);
    if (c->nlaps >= FLEX_CRONO_MAX_LAPS) {
        // Lista llena: cae la MAS ANTIGUA y se compacta; lap0 sube, asi que los numeros no se reciclan.
        for (int i = 0; i < FLEX_CRONO_MAX_LAPS - 1; i++) {
            c->laps[i] = c->laps[i + 1];
        }
        c->nlaps = FLEX_CRONO_MAX_LAPS - 1;
        if (c->lap0 < 0xFFFFu) {
            c->lap0++;
        }
    }
    c->laps[c->nlaps].split = tot - c->lap_base;
    c->laps[c->nlaps].total = tot;
    c->nlaps++;
    c->lap_base = tot;
    flex_crono_recalc_extremes(c);
    c->laps_dirty = true;
}

void flex_crono_action_left(flex_crono_t *c, uint32_t now)
{
    if (c->st == FLEX_CRONO_RUN) {
        flex_crono_lap_mark(c, now);
        return;
    }
    if (c->st == FLEX_CRONO_PAUSE) {
        flex_crono_reset(c);
        return;
    }
    // inactivo: no hay nada que marcar ni que reiniciar
}

void flex_crono_action_right(flex_crono_t *c, uint32_t now)
{
    if (c->st == FLEX_CRONO_RUN) {
        flex_crono_pause(c, now);
    } else {
        flex_crono_start(c, now);
    }
}

int flex_crono_label_left(const flex_crono_t *c)
{
    return c->st == FLEX_CRONO_PAUSE ? FLEX_CRONO_LL_RESET : FLEX_CRONO_LL_LAP;
}

int flex_crono_label_right(const flex_crono_t *c)
{
    if (c->st == FLEX_CRONO_RUN) {
        return FLEX_CRONO_LR_STOP;
    }
    if (c->st == FLEX_CRONO_PAUSE) {
        return FLEX_CRONO_LR_CONTINUE;
    }
    return FLEX_CRONO_LR_START;
}

float flex_crono_ease(float p)
{
    float q = 1.0f - p;
    return 1.0f - q * q * q;
}

int flex_crono_lerp(int a, int b, float p)
{
    return a + (int)((b - a) * p + 0.5f);
}

// ---- 2. icono vectorial --------------------------------------------------------------------------------------------
void flex_crono_glyph(int cx, int cy, int r, float th, flex_crono_glyph_t *g)
{
    if (r < 4) {
        r = 4;
    }
    g->r = r;
    g->th = th;
    // Caja + corona: el pulsador de arriba y las dos "orejas"
    float *a = g->seg[0];
    a[0] = (float)(cx - r * 0.42f);
    a[1] = (float)(cy - r - r * 0.42f);
    a[2] = (float)(cx + r * 0.42f);
    a[3] = (float)(cy - r - r * 0.42f);
    float *b = g->seg[1];
    b[0] = (float)cx;
    b[1] = (float)(cy - r - r * 0.55f);
    b[2] = (float)cx;
    b[3] = (float)(cy - r * 0.92f);
    int ring = (int)(th * 2.0f + 0.5f);
    g->ring = ring < 2 ? 2 : ring;
    // Aguja hacia las 2 en punto (la pose de la referencia)
    float *n = g->seg[2];
    n[0] = (float)cx;
    n[1] = (float)cy;
    n[2] = (float)(cx + r * 0.52f);
    n[3] = (float)(cy - r * 0.46f);
}

// ---- 3. capsula ------------------------------------------------------------------------------------------------------
int flex_crono_cap_class(uint32_t elapsed)
{
    return elapsed >= FLEX_CRONO_HOUR_MS ? 1 : 0;
}

const char *flex_crono_cap_template(uint32_t elapsed)
{
    return flex_crono_cap_class(elapsed) ? "0:00:00" : "00:00";
}

int flex_crono_cap_w(int tw)
{
    return FLEX_CRONO_CAP_PADL + FLEX_CRONO_CAP_ICON + FLEX_CRONO_CAP_GAP + tw + FLEX_CRONO_CAP_PADR;
}

void flex_crono_bar_layout(bool active, int clock_w, int cap_w, flex_crono_bar_t *out)
{
    out->show_clock = true;
    out->x = 20;
    out->on = false;
    if (!active) {
        return;
    }
    int cx = 20 + clock_w + 12;
    if (cx + cap_w > FLEX_CRONO_CAP_RIGHT) {
        cx = 20;
        out->show_clock = false;   // manda la capsula
    }
    out->x = cx;
    out->on = true;
}

int flex_crono_surface(const flex_crono_surface_in_t *in)
{
    if (in->suspended || in->land || in->hosted) {
        return 0;
    }
    if (in->app && in->app_hides_status_bar) {
        return 0;
    }
    if (in->immersive) {
        return 0;
    }
    if (in->kiosk) {
        return 0;
    }
    if (in->home && in->qs_closed && !in->edit_mode) {
        return 1;
    }
    if (in->app && !in->app_custom_header) {
        return 2;
    }
    return 0;
}

void flex_crono_cap_reset(flex_crono_cap_t *k)
{
    k->cap_on = false;
    k->cap_w_drawn = 0;
    k->cap_sec = 0xFFFFFFFFUL;
    k->cap_st_drawn = 0xFF;
}

int flex_crono_cap_tick(flex_crono_t *c, flex_crono_cap_t *k, uint32_t now, int surface, int cap_w, bool card_hidden)
{
    if (!card_hidden) {
        return FLEX_CRONO_CT_NONE;   // la tarjeta ya muestra el tiempo
    }
    bool want = flex_crono_active(c) && surface != 0;
    if (!want) {
        // Debe desaparecer: solo hay trabajo si estaba pintada y la superficie sigue siendo dibujable.
        if (k->cap_on && surface != 0) {
            return FLEX_CRONO_CT_REBUILD;
        }
        if (surface == 0) {
            c->bar_dirty = true;   // al volver, barra nueva
        }
        return FLEX_CRONO_CT_NONE;
    }
    uint32_t sec = flex_crono_elapsed(c, now) / 1000UL;
    if (!k->cap_on || c->bar_dirty || cap_w != k->cap_w_drawn) {
        return FLEX_CRONO_CT_REBUILD;
    }
    if (sec == k->cap_sec && c->st == k->cap_st_drawn) {
        return FLEX_CRONO_CT_NONE;
    }
    k->cap_sec = sec;
    k->cap_st_drawn = c->st;
    return FLEX_CRONO_CT_STAMP;
}

void flex_crono_cap_apply_bar(flex_crono_t *c, flex_crono_cap_t *k, uint32_t now, bool active, int cap_w)
{
    if (!active) {
        k->cap_on = false;
        return;
    }
    k->cap_on = true;
    k->cap_w_drawn = cap_w;
    k->cap_sec = flex_crono_elapsed(c, now) / 1000UL;
    k->cap_st_drawn = c->st;
    c->bar_dirty = false;
}

// ---- 4. pestana Cronometro --------------------------------------------------------------------------------------------
void flex_crono_layout(int bx, int by, int bw, int bh, int pad, flex_crono_layout_t *L)
{
    L->bx = bx;
    L->by = by;
    L->bw = bw;
    L->bh = bh;
    // Botones: anclados abajo; el radio se acota para no llegar nunca a la zona del boton "atras".
    L->btn_r = bw / 5;
    if (L->btn_r > 48) {
        L->btn_r = 48;
    }
    if (L->btn_r < 22) {
        L->btn_r = 22;
    }
    L->btn_cy = by + bh - L->btn_r - pad;
    L->btn_lx = bx + bw / 4;
    L->btn_rx = bx + bw * 3 / 4;
    // Esfera: la mitad superior del cuerpo, acotada por el ancho.
    int max_r = bw / 2 - pad;
    int by_h = (bh - 2 * L->btn_r - 2 * pad) / 2;
    L->r_tick = max_r < by_h ? max_r : by_h;
    if (L->r_tick > 150) {
        L->r_tick = 150;
    }
    if (L->r_tick < 40) {
        L->r_tick = 40;
    }
    L->r_disc = L->r_tick * 78 / 100;
    L->cx = bx + bw / 2;
    L->cy = by + L->r_tick + pad;
    L->dial_y0 = L->cy - L->r_tick - 3;
    L->dial_y1 = L->cy + L->r_tick + 3;
    // Tabla de vueltas: lo que quede entre la esfera y los botones.
    L->tbl_y = L->dial_y1 + pad;
    L->row_h = 32;
    int free_h = (L->btn_cy - L->btn_r - pad) - (L->tbl_y + 30);
    L->rows = free_h / L->row_h;
    if (L->rows > 4) {
        L->rows = 4;
    }
    if (L->rows < 0) {
        L->rows = 0;
    }
    L->has_table = (L->rows > 0 && bw >= 260);
    L->col_lap = bx + bw * 15 / 100;
    L->col_split = bx + bw * 50 / 100;
    L->col_tot = bx + bw * 84 / 100;
}

int flex_crono_fit_big(const char *s, int maxw, int max_size, flex_crono_textw_fn text_w, void *ctx)
{
    int fs = max_size;
    while (fs > 1 && text_w(s, fs, ctx) > maxw) {
        fs--;
    }
    return fs;
}

void flex_crono_dial_tick(const flex_crono_layout_t *L, int i, flex_crono_tick_t *t)
{
    bool major = (i % 5 == 0);
    float a = (i * 6.0f - 90.0f) * DEG2RAD;
    float ca = cosf(a), sa = sinf(a);
    float r_o = (float)L->r_tick;
    float r_i = r_o - (major ? L->r_tick * 0.13f : L->r_tick * 0.07f);
    t->x0 = L->cx + r_i * ca;
    t->y0 = L->cy + r_i * sa;
    t->x1 = L->cx + r_o * ca;
    t->y1 = L->cy + r_o * sa;
    t->w = major ? 1.9f : 1.1f;
    t->major = major;
}

void flex_crono_dial_hands(const flex_crono_layout_t *L, uint32_t elapsed, uint32_t lap_elapsed, flex_crono_hands_t *h)
{
    float h_len = L->r_tick * 0.93f, tail = L->r_tick * 0.14f;
    float a_lap = ((lap_elapsed % 60000UL) / 60000.0f) * 360.0f - 90.0f;
    float a_tot = ((elapsed % 60000UL) / 60000.0f) * 360.0f - 90.0f;
    float rl = a_lap * DEG2RAD, rt = a_tot * DEG2RAD;
    h->lap[0] = L->cx - tail * cosf(rl);
    h->lap[1] = L->cy - tail * sinf(rl);
    h->lap[2] = L->cx + h_len * cosf(rl);
    h->lap[3] = L->cy + h_len * sinf(rl);
    h->tot[0] = L->cx - tail * cosf(rt);
    h->tot[1] = L->cy - tail * sinf(rt);
    h->tot[2] = L->cx + h_len * cosf(rt);
    h->tot[3] = L->cy + h_len * sinf(rt);
    h->lap_w = 1.4f;
    h->tot_w = 2.3f;
    h->hub = L->r_tick * 0.055f + 3.0f;
}

int flex_crono_dial_text_y(const flex_crono_layout_t *L, int fs)
{
    return L->cy - (int)(2.3f * fs);
}

bool flex_crono_lap_row(const flex_crono_t *c, int r, int *kind, char *no, char *split, char *total)
{
    int i = (int)c->nlaps - 1 - r;
    if (r < 0 || i < 0) {
        return false;
    }
    *kind = FLEX_CRONO_ROW_NORMAL;
    if (i == (int)c->best) {
        *kind = FLEX_CRONO_ROW_BEST;
    }
    if (i == (int)c->worst) {
        *kind = FLEX_CRONO_ROW_WORST;
    }
    snprintf(no, 8, "%02u", (unsigned)(c->lap0 + i) & 0x1FFFFu);   // lap0 <= 65535 y i < 20: seis cifras como mucho
    flex_crono_fmt(split, 16, c->laps[i].split, true);
    flex_crono_fmt(total, 16, c->laps[i].total, true);
    return true;
}

int flex_crono_tab_hit(const flex_crono_layout_t *L, int x, int y)
{
    int r = L->btn_r + 6, cy = L->btn_cy;
    int dyl = y - cy;
    if (abs(dyl) > r) {
        return 0;
    }
    int dxl = x - L->btn_lx, dxr = x - L->btn_rx;
    if (dxl * dxl + dyl * dyl <= r * r) {
        return 1;
    }
    if (dxr * dxr + dyl * dyl <= r * r) {
        return 2;
    }
    return 0;
}

// ---- 5. tarjeta expandida -----------------------------------------------------------------------------------------------
void flex_crono_card_frame(float p, int cap_x, int cap_w, flex_crono_card_frame_t *f)
{
    f->x = flex_crono_lerp(cap_x, FLEX_CRONO_CARD_X, p);
    f->y = flex_crono_lerp(FLEX_CRONO_CAP_Y, FLEX_CRONO_CARD_Y, p);
    f->w = flex_crono_lerp(cap_w, FLEX_CRONO_CARD_W, p);
    f->h = flex_crono_lerp(FLEX_CRONO_CAP_H, FLEX_CRONO_CARD_H, p);
    f->r = flex_crono_lerp(FLEX_CRONO_CAP_H / 2, FLEX_CRONO_CARD_RAD, p);
    f->accent_a = 0;
    if (p < 1.0f) {
        f->accent_a = (uint8_t)(255.0f * (1.0f - p));
    }
    uint8_t ca = 0;
    if (p >= 0.55f) {
        ca = (uint8_t)(255.0f * ((p - 0.55f) / 0.45f));
    }
    if (p >= 1.0f) {
        ca = 255;
    }
    f->content_a = ca;
}

int flex_crono_card_x_rest(void)
{
    return FLEX_CRONO_CARD_X;
}

float flex_crono_card_progress(uint32_t elapsed_ms)
{
    float p = (float)elapsed_ms / (float)FLEX_CRONO_CARD_ANIM;
    return p > 1.0f ? 1.0f : p;
}

void flex_crono_card_layout(flex_crono_card_lay_t *l)
{
    int ix = FLEX_CRONO_CARD_X + 24, iy = FLEX_CRONO_CARD_Y + 26, is = 62;
    l->icon_x = ix;
    l->icon_y = iy;
    l->icon_s = is;
    l->icon_r = 20;
    l->glyph_cx = ix + is / 2;
    l->glyph_cy = iy + is / 2 + 2;
    l->glyph_r = is / 4;
    int by0 = FLEX_CRONO_CARD_Y + 138, by1 = FLEX_CRONO_CARD_Y + 178;
    l->sep_x = 480 / 2 - 1;
    l->sep_y = by0;
    l->sep_w = 2;
    l->sep_h = by1 - by0;
    l->lbl_lx = FLEX_CRONO_CARD_X + FLEX_CRONO_CARD_W / 4;
    l->lbl_rx = FLEX_CRONO_CARD_X + FLEX_CRONO_CARD_W * 3 / 4;
    l->lbl_y = FLEX_CRONO_CARD_Y + 148;
    int tx = ix + is + 20;
    l->time_x = tx;
    l->time_y = FLEX_CRONO_CARD_Y + 30;
    l->time_maxw = (FLEX_CRONO_CARD_X + FLEX_CRONO_CARD_W - 20) - tx;
    l->lap_x = tx;
    l->lap_y = FLEX_CRONO_CARD_Y + 82;
}

// ---- 6. tacto ------------------------------------------------------------------------------------------------------------
void flex_crono_cap_box(int cap_x, int cap_w, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = cap_x - 6;
    *x1 = cap_x + cap_w + 6;
    *y0 = FLEX_CRONO_CAP_Y - 6;
    *y1 = FLEX_CRONO_CAP_Y + FLEX_CRONO_CAP_H + 6;
}

static void push(flex_crono_touch_out_t *o, int a)
{
    if (o->n < 4) {
        o->act[o->n++] = a;
    }
}

void flex_crono_overlay_touch(flex_crono_t *c, flex_crono_ov_t *ov, const flex_crono_world_t *w,
                              const flex_crono_touch_t *t, uint32_t now, flex_crono_touch_out_t *out)
{
    out->n = 0;
    out->clr = 0;
    if (!t->down) {
        ov->long_fired = false;
    }
    if (w->ota_owns) {
        if (w->card != FLEX_CC_HIDDEN) {
            push(out, FLEX_CRONO_ACT_DROP);
        }
        return;
    }
    if (w->susp) {
        return;
    }

    // --- tarjeta a la vista: se queda con TODO ---
    if (w->card != FLEX_CC_HIDDEN) {
        if (w->card != FLEX_CC_OPEN) {   // animando
            out->clr |= FLEX_CRONO_TC_TAP | FLEX_CRONO_TC_PRESSED | FLEX_CRONO_TC_RELEASED;
            return;
        }
        int cx0 = FLEX_CRONO_CARD_X, cx1 = FLEX_CRONO_CARD_X + FLEX_CRONO_CARD_W;
        int cy0 = FLEX_CRONO_CARD_Y, cy1 = FLEX_CRONO_CARD_Y + FLEX_CRONO_CARD_H;
        if (t->swipe_up) {   // gesto arriba: contraer
            push(out, FLEX_CRONO_ACT_CLOSE);
            out->clr |= FLEX_CRONO_TC_SWIPE_UP | FLEX_CRONO_TC_TAP;
            return;
        }
        if (t->tap) {
            bool inside = (t->x >= cx0 && t->x <= cx1 && t->y >= cy0 && t->y <= cy1);
            out->clr |= FLEX_CRONO_TC_TAP | FLEX_CRONO_TC_PRESSED;
            if (!inside) {   // fuera: contraer
                push(out, FLEX_CRONO_ACT_CLOSE);
                return;
            }
            if (t->y >= FLEX_CRONO_CARD_Y + 128) {   // fila de botones
                if (t->x < 480 / 2) {
                    flex_crono_action_left(c, now);
                    push(out, FLEX_CRONO_ACT_LEFT);
                } else {
                    flex_crono_action_right(c, now);
                    push(out, FLEX_CRONO_ACT_RIGHT);
                }
                // "Reinic." deja el cronometro inactivo: la capsula desaparece y la tarjeta con ella.
                if (!flex_crono_active(c)) {
                    push(out, FLEX_CRONO_ACT_CLOSE);
                    return;
                }
                push(out, FLEX_CRONO_ACT_BG_INVAL);   // etiquetas y colores cambian
                return;
            }
            push(out, FLEX_CRONO_ACT_OPEN_APP);   // tiempo o icono: abre Reloj
            return;
        }
        out->clr |= FLEX_CRONO_TC_PRESSED | FLEX_CRONO_TC_RELEASED | FLEX_CRONO_TC_SWIPE_L | FLEX_CRONO_TC_SWIPE_R |
                    FLEX_CRONO_TC_SWIPE_D;
        return;
    }

    // --- solo la capsula escucha ---
    if (!w->cap_on || !flex_crono_active(c)) {
        return;
    }
    if (w->surface == 0) {
        return;
    }
    int x0, y0, x1, y1;
    flex_crono_cap_box(w->cap_x, w->cap_w_drawn, &x0, &y0, &x1, &y1);
    // Pulsacion larga sobre la capsula -> directo a la pestana Cronometro.
    if (!ov->long_fired && t->down && (uint32_t)(now - t->down_ms) > 700 && t->start_x >= x0 && t->start_x <= x1 &&
        t->start_y >= y0 && t->start_y <= y1 && abs(t->x - t->start_x) < 12 && abs(t->y - t->start_y) < 12) {
        ov->long_fired = true;
        out->clr |= FLEX_CRONO_TC_TAP | FLEX_CRONO_TC_PRESSED;
        push(out, FLEX_CRONO_ACT_OPEN_APP);
        return;
    }
    if (t->tap && t->x >= x0 && t->x <= x1 && t->y >= y0 && t->y <= y1) {
        out->clr |= FLEX_CRONO_TC_TAP | FLEX_CRONO_TC_PRESSED;
        // Toque sobre el ICONO: atajo a la app. En el resto de la pildora: tarjeta.
        if (t->x <= w->cap_x + FLEX_CRONO_CAP_PADL + FLEX_CRONO_CAP_ICON + 3) {
            push(out, FLEX_CRONO_ACT_OPEN_APP);
        } else {
            push(out, FLEX_CRONO_ACT_CARD_OPEN);
        }
    }
}

bool flex_crono_frame_due(uint32_t now, uint32_t last)
{
    return (uint32_t)(now - last) >= FLEX_CRONO_FRAME_MS;
}

// ---- 7. Reloj: pestanas y Hora -----------------------------------------------------------------------------------------------
bool flex_reloj_tabs_visible(int h)
{
    return h >= FLEX_RELOJ_TABS_H + 60;
}

void flex_reloj_body(int x, int y, int w, int h, flex_reloj_box_t *b)
{
    int tb = FLEX_RELOJ_TABS_H;
    if (h < tb + 60) {
        tb = 0;   // lienzo diminuto (ventana de DeX): sin pestanas
    }
    b->bx = x;
    b->by = y + tb;
    b->bw = w;
    b->bh = h - tb;
    if (b->bh < 32) {
        b->bh = 32;
    }
    b->tabs_h = tb;
}

void flex_reloj_tabs_geom(int bx, int by, int bw, int pad, flex_reloj_tabs_t *t)
{
    t->x = bx + pad;
    t->w = bw - 2 * pad;
    t->h = FLEX_RELOJ_TABS_H - 10;
    t->y = by + 5;
    t->seg = t->w / FLEX_RELOJ_TAB_N;
}

int flex_reloj_tab_at(const flex_reloj_tabs_t *t, int x, int y)
{
    if (y < t->y || y > t->y + t->h || x < t->x || x > t->x + t->w) {
        return -1;
    }
    int i = (x - t->x) / t->seg;
    if (i < 0) {
        i = 0;
    }
    if (i >= FLEX_RELOJ_TAB_N) {
        i = FLEX_RELOJ_TAB_N - 1;
    }
    return i;
}

void flex_reloj_hora_layout(const flex_reloj_box_t *b, int pad, int gap, int clock_len, int footer_lh,
                            flex_reloj_datelh_fn date_lh, void *ctx, flex_reloj_hora_t *o)
{
    memset(o, 0, sizeof(*o));
    // El reloj se lleva ~40% del alto, y nunca mas ancho de lo que cabe.
    int cap_h = b->bh * 2 / 5;
    int max_by_w = (b->bw - 2 * pad) * 10 / (clock_len * 7 + 2);
    if (cap_h > max_by_w) {
        cap_h = max_by_w;
    }
    if (cap_h > 150) {
        cap_h = 150;
    }
    if (cap_h < 22) {
        cap_h = 22;
    }
    int thick = cap_h / 8;
    if (thick < 3) {
        thick = 3;
    }
    int cy = b->by + pad + b->bh / 12;
    o->cap_h = cap_h;
    o->thick = thick;
    o->cy = cy;
    int y = cy + cap_h + pad;
    int rest = (b->by + b->bh) - y - pad;
    o->show_date = rest >= 26;
    if (o->show_date) {
        o->date_hint = rest / 3 > 30 ? 30 : rest;
        o->date_y = y;
        y += date_lh(o->date_hint, ctx) + pad;
        rest = (b->by + b->bh) - y - pad;
    }
    // Panel opcional de tarjetas: solo con ancho y alto suficientes.
    o->show_cards = b->bw >= 250 && rest >= 90;
    if (o->show_cards) {
        int n = 2;
        o->gap = gap;
        o->card_w = (b->bw - 2 * pad - gap) / n;
        o->card_h = rest > 130 ? 130 : rest;
        o->cards_y = y;
        y += o->card_h + pad;
    }
    o->footer_y = b->by + b->bh - pad - footer_lh;
    o->show_footer = o->footer_y > y;
}
