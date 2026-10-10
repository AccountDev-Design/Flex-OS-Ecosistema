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
    uint32_t seq;             // orden del ultimo cambio (el escritor graba en este orden)
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

// Los ajustes llevan secretos (texto claro antiguo de la clave, sal, hash, el
// diario): ningun buffer vuelve al heap sin borrarlo antes.
static void free_wipe(void *p, size_t n)
{
    if (p) {
        volatile uint8_t *v = p;
        while (n--) {
            *v++ = 0;
        }
        free(p);
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
        free_wipe(e->buf, e->len);
        free(e);
        e = n;
    }
    kv->head = NULL;
    kv->count = 0;
    unlock(kv);
}

// match(ns, arg): true = se suelta
static size_t drop_if(flex_kv_t *kv, bool (*match)(const char *ns, const char *arg), const char *arg)
{
    size_t n = 0;
    lock(kv);
    flex_kv_entry_t **pp = &kv->head;
    while (*pp) {
        flex_kv_entry_t *e = *pp;
        if (match(e->ns, arg)) {
            *pp = e->next;
            free_wipe(e->buf, e->len);
            free(e);
            kv->count--;
            n++;
        } else {
            pp = &e->next;
        }
    }
    unlock(kv);
    return n;
}

static bool ns_is(const char *ns, const char *arg)
{
    return strcmp(ns, arg) == 0;
}

static bool ns_not(const char *ns, const char *arg)
{
    return !arg || strcmp(ns, arg) != 0;
}

size_t flex_kv_drop_ns(flex_kv_t *kv, const char *ns)
{
    return ns ? drop_if(kv, ns_is, ns) : 0;
}

size_t flex_kv_drop_all_except(flex_kv_t *kv, const char *keep)
{
    return drop_if(kv, ns_not, keep);
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
    free_wipe(e->buf, e->len);
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
        free_wipe(e->buf, e->len);
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
        free_wipe(e->buf, e->len);
        e->buf = NULL;
        e->len = 0;
        e->type = type;
        e->num = v;
        e->erased = false;
        e->version++;
        e->seq = ++kv->seq;
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
            e->seq = ++kv->seq;
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
        free_wipe(e->buf, e->len);
        e->buf = NULL;
        e->len = 0;
        e->erased = true;
        e->version++;
        e->seq = ++kv->seq;
        changed = true;
    }
    unlock(kv);
    return changed;
}

// La pendiente con el cambio MAS ANTIGUO: la flash recibe los cambios en el
// orden en que se hicieron. Quien encadena escrituras que dependen unas de
// otras (la clave del sistema: "sin clave" antes de borrar el hash) cuenta
// con ello si se corta la corriente a mitad de una pasada.
int flex_kv_next_dirty_skip(flex_kv_t *kv, flex_kv_snapshot_t *snap, flex_kv_skip_fn_t skip, void *ctx)
{
    int ret = 0;
    memset(snap, 0, sizeof(*snap));
    lock(kv);
    flex_kv_entry_t *best = NULL;
    for (flex_kv_entry_t *e = kv->head; e; e = e->next) {
        if (e->version != e->written && (!best || (int32_t)(e->seq - best->seq) < 0) &&
            !(skip && skip(e->ns, e->key, ctx))) {
            best = e;
        }
    }
    if (best) {
        ret = 1;
        if (best->len) {
            snap->buf = malloc(best->len);
            if (!snap->buf) {
                ret = -1;   // sin memoria: sigue pendiente y quien graba lo sabe
            } else {
                memcpy(snap->buf, best->buf, best->len);
                snap->len = best->len;
            }
        }
        if (ret == 1) {
            strcpy(snap->ns, best->ns);
            strcpy(snap->key, best->key);
            snap->type = best->type;
            snap->num = best->num;
            snap->erase = best->erased;
            snap->version = best->version;
        }
    }
    unlock(kv);
    return ret;
}

int flex_kv_next_dirty(flex_kv_t *kv, flex_kv_snapshot_t *snap)
{
    return flex_kv_next_dirty_skip(kv, snap, NULL, NULL);
}

// ---- una pasada del escritor -------------------------------------------------------
typedef struct {
    char ns[FLEX_KV_NS_MAX];
    char key[FLEX_KV_KEY_MAX];
} skip_ent_t;

typedef struct {
    skip_ent_t e[FLEX_KV_PASS_SKIP_MAX];
    int n;
    bool group_blocked;
    bool (*ordered)(const char *ns, const char *key);
} pass_t;

static bool pass_skip(const char *ns, const char *key, void *ctx)
{
    pass_t *p = ctx;
    if (p->group_blocked && p->ordered && p->ordered(ns, key)) {
        return true;
    }
    for (int i = 0; i < p->n; i++) {
        if (strcmp(p->e[i].key, key) == 0 && strcmp(p->e[i].ns, ns) == 0) {
            return true;
        }
    }
    return false;
}

flex_kv_pass_t flex_kv_flush_pass(flex_kv_t *kv, int (*write)(const flex_kv_snapshot_t *s, void *ctx),
                                  bool (*ordered)(const char *ns, const char *key), void *ctx, int *first_err)
{
    pass_t p = {.ordered = ordered};
    flex_kv_pass_t res = FLEX_KV_PASS_OK;
    *first_err = 0;
    size_t guard = kv->count + FLEX_KV_PASS_SKIP_MAX + 4;
    for (;;) {
        if (guard-- == 0) {
            return res == FLEX_KV_PASS_OK ? FLEX_KV_PASS_BUSY : res;   // no para de cambiar: la siguiente sigue
        }
        flex_kv_snapshot_t snap;
        int nd = flex_kv_next_dirty_skip(kv, &snap, pass_skip, &p);
        if (nd == 0) {
            return res;
        }
        if (nd < 0) {
            return FLEX_KV_PASS_NOMEM;   // queda pendiente: la siguiente pasada lo reintenta
        }
        int err = write(&snap, ctx);
        if (err == 0) {
            flex_kv_mark_written(kv, &snap);
        } else {
            // Se salta en ESTA pasada y el resto sigue (una clave que no cabe no
            // puede bloquear todas las demas). Dentro de un grupo ordenado (la
            // clave del sistema) nada de lo posterior se adelanta a la que fallo.
            if (res == FLEX_KV_PASS_OK) {
                res = FLEX_KV_PASS_ERR;
                *first_err = err;
            }
            if (ordered && ordered(snap.ns, snap.key)) {
                p.group_blocked = true;
            }
            if (p.n == FLEX_KV_PASS_SKIP_MAX) {
                flex_kv_snapshot_free(&snap);
                return res;
            }
            strcpy(p.e[p.n].ns, snap.ns);
            strcpy(p.e[p.n].key, snap.key);
            p.n++;
        }
        flex_kv_snapshot_free(&snap);
    }
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
    free_wipe(snap->buf, snap->len);
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
