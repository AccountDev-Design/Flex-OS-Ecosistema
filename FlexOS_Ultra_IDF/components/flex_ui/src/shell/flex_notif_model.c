// Flex OS Ultra · modelo de avisos. Ver flex_notif_model.h.
#include "flex_notif_model.h"

#include <string.h>

// ---- UTF-8 (FlexOS_FlexLink.cpp:399-433) --------------------------------------------
static int u8_len(uint8_t c)
{
    if (c < 0x80) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

static size_t utf8_trunc(const char *src, size_t max)
{
    if (!src || max == 0) {
        return 0;
    }
    size_t i = 0;
    while (src[i] && i < max) {
        int need = u8_len((uint8_t)src[i]);
        if (need <= 0 || i + (size_t)need > max) {
            break;
        }
        bool ok = true;
        for (int k = 1; k < need; k++) {
            if (((uint8_t)src[i + k] & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            break;
        }
        i += (size_t)need;
    }
    return i;
}

size_t flex_utf8_copy(char *out, size_t cap, const char *src)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = 0;
    if (!src) {
        return 0;
    }
    size_t take = utf8_trunc(src, cap - 1);
    if (take) {
        memcpy(out, src, take);
    }
    out[take] = 0;
    return take;
}

// ---- historial ----------------------------------------------------------------------
uint32_t flex_ntf_key(uint8_t type, const char *title)
{
    uint32_t h = 2166136261u;
    h = (h ^ type) * 16777619u;
    h = (h ^ 0u) * 16777619u;   // i2cAddr: siempre 0 desde que se retiro el barrido del bus
    for (const char *p = title; p && *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h ? h : 1u;
}

void flex_ntf_remove(flex_ntf_hist_t *h, int idx)
{
    if (idx < 0 || idx >= h->n) {
        return;
    }
    for (int j = idx; j < h->n - 1; j++) {
        h->e[j] = h->e[j + 1];
    }
    h->n--;
    memset(&h->e[h->n], 0, sizeof(h->e[h->n]));
}

// Copia con strncpy y terminador, como el struct de Arduino (char[] con strncpy en los productores).
static void copy_field(char *dst, size_t cap, const char *src)
{
    if (!src) {
        src = "";
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

uint32_t flex_ntf_push(flex_ntf_hist_t *h, uint8_t type, const char *title, const char *sub, uint32_t now_ms)
{
    char t[FLEX_NTF_TITLE_MAX];
    copy_field(t, sizeof(t), title);
    uint32_t key = flex_ntf_key(type, t);
    int dup = -1;
    for (int i = 0; i < h->n; i++) {
        if (h->e[i].key == key) {
            dup = i;
            break;
        }
    }
    flex_ntf_t *e;
    if (dup >= 0) {
        e = &h->e[dup];
    } else {
        if (h->n >= FLEX_NTF_MAX) {
            flex_ntf_remove(h, 0);   // historial lleno: sale el mas antiguo
        }
        e = &h->e[h->n++];
        e->key = key;
    }
    e->type = type;
    memcpy(e->title, t, sizeof(t));
    copy_field(e->sub, sizeof(e->sub), sub);
    e->born_ms = now_ms;   // el Centro ordena por esto
    return key;
}

// ---- cola del banner -------------------------------------------------------------
int flex_bq_rank(const flex_bq_msg_t *m)
{
    if (m->src == FLEX_BQ_SRC_SYSTEM) {
        return 2;
    }
    return m->pri >= FLEX_PRI_HIGH ? 1 : 0;
}

void flex_bq_enqueue(flex_bq_t *b, const flex_bq_msg_t *m, bool front)
{
    const int r = flex_bq_rank(m);
    if (b->n >= FLEX_BQ_DEPTH) {
        int victim = -1;
        for (int i = b->n - 1; i >= 0; i--) {
            if (flex_bq_rank(&b->q[i]) < r) {
                victim = i;
                break;
            }
        }
        if (victim < 0 && !front) {
            b->more++;
            return;
        }
        if (victim < 0) {
            victim = b->n - 1;   // el que vuelve tiene preferencia sobre el ultimo
        }
        for (int i = victim; i < b->n - 1; i++) {
            b->q[i] = b->q[i + 1];
        }
        b->n--;
        b->more++;
    }
    int at = 0;
    if (front) {
        while (at < b->n && flex_bq_rank(&b->q[at]) > r) {
            at++;
        }
    } else {
        for (int i = 0; i < b->n; i++) {
            if (flex_bq_rank(&b->q[i]) >= r) {
                at = i + 1;
            }
        }
    }
    for (int i = b->n; i > at; i--) {
        b->q[i] = b->q[i - 1];
    }
    b->q[at] = *m;
    b->n++;
}

bool flex_bq_same_sys(const flex_bq_msg_t *a, const flex_bq_msg_t *b)
{
    if (a->key && b->key) {
        return a->key == b->key;
    }
    return strcmp(a->title, b->title) == 0 && strcmp(a->body, b->body) == 0;
}

void flex_bq_offer(flex_bq_t *b, const flex_bq_msg_t *m)
{
    if (m->src == FLEX_BQ_SRC_SYSTEM) {
        if (b->cur_live && b->cur.src == FLEX_BQ_SRC_SYSTEM && flex_bq_same_sys(&b->cur, m)) {
            if (strcmp(b->cur.title, m->title) != 0 || strcmp(b->cur.body, m->body) != 0) {
                memcpy(b->cur.title, m->title, sizeof(b->cur.title));
                memcpy(b->cur.body, m->body, sizeof(b->cur.body));
                b->refresh = true;
            }
            return;
        }
        for (int i = 0; i < b->n; i++) {
            if (b->q[i].src == FLEX_BQ_SRC_SYSTEM && flex_bq_same_sys(&b->q[i], m)) {
                memcpy(b->q[i].title, m->title, sizeof(b->q[i].title));
                memcpy(b->q[i].body, m->body, sizeof(b->q[i].body));
                return;
            }
        }
    }
    flex_bq_enqueue(b, m, false);
}

void flex_bq_msg_init(flex_bq_msg_t *m, uint8_t src, uint32_t id, const char *app, const char *title,
                      const char *body, uint8_t pri)
{
    memset(m, 0, sizeof(*m));
    m->src = src;
    m->id = id;
    m->pri = pri;
    flex_utf8_copy(m->app, sizeof(m->app), app ? app : "");
    flex_utf8_copy(m->title, sizeof(m->title), title ? title : "");
    flex_utf8_copy(m->body, sizeof(m->body), body ? body : "");
}

void flex_bq_push_system_keyed(flex_bq_t *b, uint32_t key, uint8_t type, const char *title, const char *body)
{
    flex_bq_msg_t m;
    flex_bq_msg_init(&m, FLEX_BQ_SRC_SYSTEM, 0, "", title, body, FLEX_PRI_DEFAULT);
    m.key = key;
    m.icon = (uint8_t)(1 + type);
    flex_bq_offer(b, &m);
}

uint32_t flex_ntf_post(flex_ntf_hist_t *h, flex_bq_t *b, uint8_t type, const char *title, const char *sub,
                       uint32_t now_ms)
{
    uint32_t key = flex_ntf_push(h, type, title, sub, now_ms);
    for (int i = 0; i < h->n; i++) {
        if (h->e[i].key == key) {
            flex_bq_push_system_keyed(b, key, type, h->e[i].title, h->e[i].sub);
            break;
        }
    }
    return key;
}

bool flex_bq_pop(flex_bq_t *b, flex_bq_msg_t *out)
{
    if (b->n == 0) {
        return false;
    }
    *out = b->q[0];
    for (int i = 1; i < b->n; i++) {
        b->q[i - 1] = b->q[i];
    }
    b->n--;
    return true;
}

uint32_t flex_bq_hold_ms(uint32_t shown_ms)
{
    return shown_ms + FLEX_BQ_MIN_HOLD_MS < FLEX_BQ_HOLD_MS ? FLEX_BQ_HOLD_MS - shown_ms : FLEX_BQ_MIN_HOLD_MS;
}

bool flex_bq_dismiss(float slide, float vx, bool moved, int card_w)
{
    float a = slide < 0 ? -slide : slide;
    float v = vx < 0 ? -vx : vx;
    bool same_way = (slide < 0 && vx < 0) || (slide > 0 && vx > 0);
    return moved && (a >= (float)(card_w / 3) || (same_way && v >= 0.7f && a >= 40.0f));
}
