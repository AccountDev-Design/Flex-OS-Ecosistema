// Recientes (flex_recents_model.c) contra swPush/swDropCard/swThumbTrim y
// captureThumb de Arduino, con secuencias aleatorias de operaciones.
#include <stdlib.h>
#include <string.h>
#include "flex_recents_model.h"
#include "flex_test.h"

void ref_rc_reset(void);
int ref_rc_push(int id);
void ref_rc_drop(int idx);
void ref_rc_trim(int keep);
void ref_rc_set_thumb0(void);
int ref_rc_count(void);
int ref_rc_app(int i);
bool ref_rc_has_thumb(int i);
void ref_rc_capture(uint16_t *src, uint16_t *dst);

static int s_term = -1;
static int s_full_hits;
static void term(int app) { s_term = app; }
static void free_thumb(uint16_t *p) { free(p); }

static uint32_t s_seed = 99;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

void test_recents(void)
{
    flex_rc_list_t L;
    memset(&L, 0, sizeof(L));
    L.free_thumb = free_thumb;
    L.terminate = term;
    ref_rc_reset();
    int diffs = 0;
    for (int it = 0; it < 200000; it++) {
        uint32_t op = rnd() % 10;
        if (op < 6) {
            int id = (int)(rnd() % 25);   // > 19: fuerza la rama de lista llena
            s_term = -1;
            int rt = ref_rc_push(id);
            flex_rc_push(&L, (uint8_t)id);
            diffs += rt != s_term;
            s_full_hits += s_term >= 0;
            if (rnd() % 2) {   // captura
                ref_rc_set_thumb0();
                if (L.n && !L.c[0].thumb) {
                    L.c[0].thumb = malloc(8);
                }
            }
        } else if (op < 8) {
            int idx = (int)(rnd() % 21) - 1;
            ref_rc_drop(idx);
            flex_rc_drop(&L, idx);
        } else {
            int keep = (int)(rnd() % 7) - 1;
            ref_rc_trim(keep);
            flex_rc_thumb_trim(&L, keep);
        }
        if (L.n != ref_rc_count()) {
            diffs++;
            continue;
        }
        for (int i = 0; i < L.n; i++) {
            if (L.c[i].app != ref_rc_app(i) || (L.c[i].thumb != NULL) != ref_rc_has_thumb(i)) {
                diffs++;
                break;
            }
        }
    }
    flex_rc_thumb_trim(&L, 0);
    ref_rc_reset();
    CHECK(s_full_hits > 100);
    if (diffs) {
        fprintf(stderr, "recientes: %d pasos distintos de la version Arduino\n", diffs);
    }
    CHECK_EQ_I(diffs, 0);
    CHECK_EQ_I(flex_rc_find(&L, 3) >= -1, 1);

    // miniatura: el mismo muestreo que captureThumb
    uint16_t *src = malloc(480 * 800 * 2);
    uint16_t a[150 * 250], b[150 * 250];
    for (int i = 0; i < 480 * 800; i++) {
        src[i] = (uint16_t)(i * 2654435761u >> 7);
    }
    flex_rc_downscale(src, 480, a);
    ref_rc_capture(src, b);
    CHECK(!memcmp(a, b, sizeof(a)));
    free(src);
}
