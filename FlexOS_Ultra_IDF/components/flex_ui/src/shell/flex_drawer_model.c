// Modelo de la Caja de aplicaciones. Ver flex_drawer_model.h.
#include "flex_drawer_model.h"

#include <stddef.h>

static inline char fold(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

bool flex_drw_match(const char *name, const char *q, int qn)
{
    // dexMatch (DeX.h:270): subcadena byte a byte; los acentos no casan
    if (qn <= 0) {
        return true;
    }
    for (int i = 0; name[i]; i++) {
        int k = 0;
        while (k < qn && name[i + k] && fold(name[i + k]) == fold(q[k])) {
            k++;
        }
        if (k == qn) {
            return true;
        }
    }
    return false;
}

int flex_drw_name_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = fold(*a++), cb = fold(*b++);
        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static const char *cell_name(const flex_drw_src_t *s, const flex_drw_cell_t *c)
{
    return c->kind == FLEX_DRW_NATIVE ? s->app_name(c->idx) : s->pkg_name(c->idx);
}

int flex_drw_filter(const flex_drw_src_t *s, bool show_hidden, const char *q, int qn, flex_drw_cell_t *out, int cap)
{
    int n = 0;
    for (int id = 0; id < s->app_n && n < cap; id++) {
        if (s->app_hidden(id) && !show_hidden) {
            continue;
        }
        if (!flex_drw_match(s->app_name(id), q, qn)) {
            continue;
        }
        out[n].kind = FLEX_DRW_NATIVE;
        out[n].idx = (int16_t)id;
        n++;
    }
    for (int i = 0; i < s->pkg_n && n < cap; i++) {
        if (s->pkg_hidden(i) && !show_hidden) {
            continue;
        }
        if (!flex_drw_match(s->pkg_name(i), q, qn)) {
            continue;
        }
        out[n].kind = FLEX_DRW_PKG;
        out[n].idx = (int16_t)i;
        n++;
    }
    // Insercion estable: nombre, luego tipo, luego indice
    for (int i = 1; i < n; i++) {
        flex_drw_cell_t key = out[i];
        const char *kn = cell_name(s, &key);
        int j = i - 1;
        while (j >= 0) {
            const flex_drw_cell_t *a = &out[j];
            int cmp = flex_drw_name_cmp(cell_name(s, a), kn);
            if (cmp == 0) {
                cmp = (int)a->kind - (int)key.kind;
            }
            if (cmp == 0) {
                cmp = (int)a->idx - (int)key.idx;
            }
            if (cmp <= 0) {
                break;
            }
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}
