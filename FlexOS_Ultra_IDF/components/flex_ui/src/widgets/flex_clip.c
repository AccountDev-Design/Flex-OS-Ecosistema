#include "flex_clip.h"

#include <string.h>

// Copia como mucho cap-1 bytes de s, sin partir un caracter UTF-8.
static void copy_trunc(char *dst, size_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static void save_pinned(flex_clip_t *c)
{
    if (!c->store.put) {
        return;
    }
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        c->store.put(i, (c->slot[i].used && c->slot[i].pinned) ? c->slot[i].text : NULL, c->store.user);
    }
}

void flex_clip_init(flex_clip_t *c, const flex_clip_store_t *store)
{
    memset(c, 0, sizeof(*c));
    if (store) {
        c->store = *store;
    }
    if (!c->store.get) {
        return;
    }
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        char t[FLEX_CLIP_TXT_MAX + 8];
        t[0] = '\0';
        c->store.get(i, t, sizeof(t), c->store.user);
        if (t[0]) {
            copy_trunc(c->slot[i].text, FLEX_CLIP_TXT_MAX, t);
            c->slot[i].used = true;
            c->slot[i].pinned = true;
            c->slot[i].ts = 0;
        }
    }
}

int flex_clip_count(const flex_clip_t *c)
{
    int n = 0;
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        n += c->slot[i].used;
    }
    return n;
}

void flex_clip_push(flex_clip_t *c, const char *s, uint32_t now_ms)
{
    if (!s || !s[0]) {
        return;
    }
    if (s != c->classic) {
        copy_trunc(c->classic, sizeof(c->classic), s);
    }
    char t[FLEX_CLIP_TXT_MAX];
    copy_trunc(t, sizeof(t), s);   // lo que cabe en una ranura
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {   // repetido: solo se refresca la fecha
        if (c->slot[i].used && !strcmp(c->slot[i].text, t)) {
            c->slot[i].ts = now_ms;
            return;
        }
    }
    int slot = -1;
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        if (!c->slot[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
            if (!c->slot[i].pinned && c->slot[i].ts <= oldest) {
                oldest = c->slot[i].ts;
                slot = i;
            }
        }
        if (slot < 0) {
            return;   // las 12 estan fijadas: no se toca ninguna
        }
    }
    memcpy(c->slot[slot].text, t, sizeof(t));
    c->slot[slot].used = true;
    c->slot[slot].pinned = false;
    c->slot[slot].ts = now_ms;
}

void flex_clip_del(flex_clip_t *c, int i)
{
    if (i < 0 || i >= FLEX_CLIP_SLOTS) {
        return;
    }
    bool was = c->slot[i].pinned;
    c->slot[i].used = false;
    c->slot[i].pinned = false;
    c->slot[i].text[0] = '\0';
    c->slot[i].ts = 0;
    if (was) {
        save_pinned(c);
    }
}

void flex_clip_toggle_pin(flex_clip_t *c, int i)
{
    if (i < 0 || i >= FLEX_CLIP_SLOTS || !c->slot[i].used) {
        return;
    }
    c->slot[i].pinned = !c->slot[i].pinned;
    save_pinned(c);
}

void flex_clip_clear_unpinned(flex_clip_t *c)
{
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        if (c->slot[i].used && !c->slot[i].pinned) {
            c->slot[i].used = false;
            c->slot[i].text[0] = '\0';
            c->slot[i].ts = 0;
        }
    }
}

int flex_clip_visible(const flex_clip_t *c, bool only_pinned, int vis[FLEX_CLIP_SLOTS])
{
    int n = 0;
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        if (!c->slot[i].used || (only_pinned && !c->slot[i].pinned)) {
            continue;
        }
        vis[n++] = i;
    }
    return n;
}
