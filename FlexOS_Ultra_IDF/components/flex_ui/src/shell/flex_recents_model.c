// Modelo de Recientes. Ver flex_recents_model.h.
#include "flex_recents_model.h"

#include <stddef.h>

int flex_rc_find(const flex_rc_list_t *l, int app)
{
    for (int i = 0; i < l->n; i++) {
        if (l->c[i].app == app) {
            return i;
        }
    }
    return -1;
}

void flex_rc_push(flex_rc_list_t *l, uint8_t app)
{
    int at = flex_rc_find(l, app);
    if (at >= 0) {
        flex_rc_card_t tmp = l->c[at];
        for (int i = at; i > 0; i--) {
            l->c[i] = l->c[i - 1];
        }
        l->c[0] = tmp;
        return;
    }
    if (l->n < FLEX_RC_MAX) {
        for (int i = l->n; i > 0; i--) {
            l->c[i] = l->c[i - 1];
        }
        l->n++;
    } else {
        // Lista llena: la mas antigua se cierra de verdad (si no, su estado
        // quedaria vivo sin tarjeta que lo represente)
        if (l->terminate) {
            l->terminate(l->c[FLEX_RC_MAX - 1].app);
        }
        if (l->c[FLEX_RC_MAX - 1].thumb && l->free_thumb) {
            l->free_thumb(l->c[FLEX_RC_MAX - 1].thumb);
        }
        for (int i = FLEX_RC_MAX - 1; i > 0; i--) {
            l->c[i] = l->c[i - 1];
        }
    }
    l->c[0].app = app;
    l->c[0].thumb = NULL;
}

void flex_rc_drop(flex_rc_list_t *l, int idx)
{
    if (idx < 0 || idx >= l->n) {
        return;
    }
    if (l->c[idx].thumb && l->free_thumb) {
        l->free_thumb(l->c[idx].thumb);
    }
    l->c[idx].thumb = NULL;
    for (int i = idx; i < l->n - 1; i++) {
        l->c[i] = l->c[i + 1];
    }
    l->n--;
}

void flex_rc_thumb_trim(flex_rc_list_t *l, int keep)
{
    keep = keep < 0 ? 0 : keep;
    for (int i = keep; i < l->n; i++) {
        if (l->c[i].thumb) {
            if (l->free_thumb) {
                l->free_thumb(l->c[i].thumb);
            }
            l->c[i].thumb = NULL;
        }
    }
}

void flex_rc_downscale(const uint16_t *src, int src_stride, uint16_t *dst)
{
    for (int j = 0; j < FLEX_RC_TH_H; j++) {
        const uint16_t *row = src + (size_t)(j * 800 / FLEX_RC_TH_H) * (size_t)src_stride;
        uint16_t *d = dst + (size_t)j * FLEX_RC_TH_W;
        for (int i = 0; i < FLEX_RC_TH_W; i++) {
            d[i] = row[i * 480 / FLEX_RC_TH_W];
        }
    }
}
