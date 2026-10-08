#include "flex_kv.h"

#include <stdlib.h>
#include <string.h>

struct flex_kv_entry {
    char ns[FLEX_KV_NS_MAX];
    char key[FLEX_KV_KEY_MAX];
    flex_kv_type_t type;
    int64_t num;
    uint8_t *buf;
    size_t len;
    bool erased;              // borrada en la cache, falta borrarla del almacen
    uint32_t version;         // sube con cada cambio
    uint32_t written;         // ultima version grabada
    flex_kv_entry_t *next;
};

static void lock(flex_kv_t *kv)
{
    if (kv->lock) {
        kv->lock(kv->lock_ctx);
    }
}

static void unlock(flex_kv_t *kv)
{
    if (kv->unlock) {
        kv->unlock(kv->lock_ctx);
    }
}

static bool valid_name(const char *s, size_t max)
{
    return s && s[0] && strlen(s) < max;
}

static bool is_buf_type(flex_kv_type_t t)
{
    return t == FLEX_KV_STR || t == FLEX_KV_BLOB;
}

static flex_kv_entry_t *find(flex_kv_t *kv, const char *ns, const char *key)
{
    for (flex_kv_entry_t *e = kv->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0 && strcmp(e->ns, ns) == 0) {
            return e;
        }
    }
    return NULL;
}

static flex_kv_entry_t *find_or_add(flex_kv_t *kv, const char *ns, const char *key)
{
    flex_kv_entry_t *e = find(kv, ns, key);
    if (e) {
        return e;
    }
    e = calloc(1, sizeof(*e));
    if (!e) {
        return NULL;
    }
    strcpy(e->ns, ns);
    strcpy(e->key, key);
    e->next = kv->head;
    kv->head = e;
    kv->count++;
    return e;
}

void flex_kv_init(flex_kv_t *kv, void (*lk)(void *), void (*ulk)(void *), void *ctx)
{
    memset(kv, 0, sizeof(*kv));
    kv->lock = lk;
    kv->unlock = ulk;
    kv->lock_ctx = ctx;
}

void flex_kv_clear(flex_kv_t *kv)
{
    lock(kv);
    flex_kv_entry_t *e = kv->head;
    while (e) {
        flex_kv_entry_t *n = e->next;
        free(e->buf);
        free(e);
        e = n;
    }
    kv->head = NULL;
    kv->count = 0;
    unlock(kv);
}

static bool store_buf(flex_kv_entry_t *e, flex_kv_type_t type, const void *data, size_t len)
{
    uint8_t *copy = NULL;
    if (len) {
        copy = malloc(len);
        if (!copy) {
            return false;
        }
        memcpy(copy, data, len);
    }
    free(e->buf);
    e->buf = copy;
    e->len = len;
    e->type = type;
    e->num = 0;
    return true;
}

bool flex_kv_load_num(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, int64_t v)
{
    if (!valid_name(ns, FLEX_KV_NS_MAX) || !valid_name(key, FLEX_KV_KEY_MAX) || type == FLEX_KV_NONE ||
        is_buf_type(type)) {
        return false;
    }
    lock(kv);
    flex_kv_entry_t *e = find_or_add(kv, ns, key);
    if (e) {
        free(e->buf);
        e->buf = NULL;
        e->len = 0;
        e->type = type;
        e->num = v;
        e->erased = false;
        e->written = e->version;
    }
    unlock(kv);
    return e != NULL;
}

bool flex_kv_load_buf(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, const void *data,
                      size_t len)
{
    if (!valid_name(ns, FLEX_KV_NS_MAX) || !valid_name(key, FLEX_KV_KEY_MAX) || !is_buf_type(type) ||
        len > FLEX_KV_BUF_MAX) {
        return false;
    }
    lock(kv);
    flex_kv_entry_t *e = find_or_add(kv, ns, key);
    bool ok = e && store_buf(e, type, data, len);
    if (ok) {
        e->erased = false;
        e->written = e->version;
    }
    unlock(kv);
    return ok;
}

bool flex_kv_get_num(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, int64_t *out)
{
    bool ok = false;
    lock(kv);
    flex_kv_entry_t *e = find(kv, ns, key);
    if (e && !e->erased && e->type == type && !is_buf_type(type)) {
        *out = e->num;
        ok = true;
    }
    unlock(kv);
    return ok;
}

bool flex_kv_get_buf(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, void *out, size_t cap,
                     size_t *len)
{
    bool ok = false;
    lock(kv);
    flex_kv_entry_t *e = find(kv, ns, key);
    if (e && !e->erased && e->type == type && is_buf_type(type)) {
        size_t n = e->len < cap ? e->len : cap;
        if (n && out) {
            memcpy(out, e->buf, n);
        }
        if (len) {
            *len = e->len;
        }
        ok = true;
    }
    unlock(kv);
    return ok;
}

bool flex_kv_exists(flex_kv_t *kv, const char *ns, const char *key)
{
    lock(kv);
    flex_kv_entry_t *e = find(kv, ns, key);
    bool ok = e && !e->erased;
    unlock(kv);
    return ok;
}

bool flex_kv_set_num(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, int64_t v)
{
    if (!valid_name(ns, FLEX_KV_NS_MAX) || !valid_name(key, FLEX_KV_KEY_MAX) || type == FLEX_KV_NONE ||
        is_buf_type(type)) {
        return false;
    }
    bool changed = false;
    lock(kv);
    flex_kv_entry_t *e = find_or_add(kv, ns, key);
    if (e && (e->erased || e->type != type || e->num != v)) {
        free(e->buf);
        e->buf = NULL;
        e->len = 0;
        e->type = type;
        e->num = v;
        e->erased = false;
        e->version++;
        changed = true;
    }
    unlock(kv);
    return changed;
}

bool flex_kv_set_buf(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, const void *data,
                     size_t len)
{
    if (!valid_name(ns, FLEX_KV_NS_MAX) || !valid_name(key, FLEX_KV_KEY_MAX) || !is_buf_type(type) ||
        len > FLEX_KV_BUF_MAX || (len && !data)) {
        return false;
    }
    bool changed = false;
    lock(kv);
    flex_kv_entry_t *e = find_or_add(kv, ns, key);
    if (e && (e->erased || e->type != type || e->len != len || (len && memcmp(e->buf, data, len) != 0))) {
        if (store_buf(e, type, data, len)) {
            e->erased = false;
            e->version++;
            changed = true;
        }
    }
    unlock(kv);
    return changed;
}

bool flex_kv_erase(flex_kv_t *kv, const char *ns, const char *key)
{
    bool changed = false;
    lock(kv);
    flex_kv_entry_t *e = find(kv, ns, key);
    if (e && !e->erased) {
        free(e->buf);
        e->buf = NULL;
        e->len = 0;
        e->erased = true;
        e->version++;
        changed = true;
    }
    unlock(kv);
    return changed;
}

bool flex_kv_next_dirty(flex_kv_t *kv, flex_kv_snapshot_t *snap)
{
    bool found = false;
    memset(snap, 0, sizeof(*snap));
    lock(kv);
    for (flex_kv_entry_t *e = kv->head; e; e = e->next) {
        if (e->version == e->written) {
            continue;
        }
        strcpy(snap->ns, e->ns);
        strcpy(snap->key, e->key);
        snap->type = e->type;
        snap->num = e->num;
        snap->erase = e->erased;
        snap->version = e->version;
        if (e->len) {
            snap->buf = malloc(e->len);
            if (!snap->buf) {
                break;   // sin memoria: se reintenta en la siguiente pasada
            }
            memcpy(snap->buf, e->buf, e->len);
            snap->len = e->len;
        }
        found = true;
        break;
    }
    unlock(kv);
    return found;
}

void flex_kv_mark_written(flex_kv_t *kv, const flex_kv_snapshot_t *snap)
{
    lock(kv);
    flex_kv_entry_t *e = find(kv, snap->ns, snap->key);
    if (e && e->version == snap->version) {
        e->written = snap->version;
    }
    unlock(kv);
}

void flex_kv_snapshot_free(flex_kv_snapshot_t *snap)
{
    free(snap->buf);
    snap->buf = NULL;
    snap->len = 0;
}

size_t flex_kv_dirty_count(flex_kv_t *kv)
{
    size_t n = 0;
    lock(kv);
    for (flex_kv_entry_t *e = kv->head; e; e = e->next) {
        n += e->version != e->written;
    }
    unlock(kv);
    return n;
}

void flex_kv_mark_all_dirty(flex_kv_t *kv)
{
    lock(kv);
    for (flex_kv_entry_t *e = kv->head; e; e = e->next) {
        if (!e->erased) {
            e->version++;
        }
    }
    unlock(kv);
}
