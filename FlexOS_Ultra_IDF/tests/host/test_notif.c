// Avisos (flex_notif_model.c) contra el modelo de Arduino extraido tal cual:
// historial con huella (notifPush/notifRemove), cola del banner (fpbOffer,
// fpbEnqueue con prioridades y "+N", reencolado al frente) y el recorte UTF-8
// de FlexLink, con secuencias aleatorias.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "flex_notif_model.h"
#include "flex_test.h"

void ref_ntf_reset(void);
void ref_ntf_sys(int type, const char *title, const char *sub, uint32_t now);
void ref_ntf_remove(int idx);
int ref_ntf_count(void);
void ref_ntf_get(int i, int *type, const char **title, const char **sub, uint32_t *born, uint32_t *key);
void ref_bq_push(int src, uint32_t id, const char *app, const char *title, const char *body, int pri);
void ref_bq_set_cur(bool live, const void *msg);
void ref_bq_requeue_front(const void *msg);
bool ref_bq_pop(void);
int ref_bq_n(void);
const void *ref_bq_at(int i);
const void *ref_bq_cur(void);
uint32_t ref_bq_more(void);
bool ref_bq_refresh(void);
size_t ref_utf8_copy(char *o, size_t n, const char *s);

static uint32_t s_seed = 777;
static uint32_t rnd(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

static int s_diff;
#define SAME(c)                                                                  \
    do {                                                                         \
        if (!(c) && s_diff++ < 10) {                                             \
            fprintf(stderr, "avisos: distinto de Arduino (%s:%d): %s\n", __FILE__, __LINE__, #c); \
        }                                                                        \
    } while (0)

static const char *const TITLES[] = {"Memoria cr\xC3\xADtica", "Memoria casi llena", "Flex Account", "Galer\xC3\xAD" "a",
                                     "Almacenamiento interno casi lleno", "Un tel\xC3\xA9" "fono quiere emparejarse", "",
                                     "Un t\xC3\xADtulo muy largo que no cabe en la tarjeta del aviso y se tiene que cortar en alg\xC3\xBAn sitio"};
static const char *const BODIES[] = {"", "Flex OS liber\xC3\xB3 recursos en segundo plano.", "Cierra una app.",
                                     "Vuelve a vincular en Ajustes > General", "x"};
static const char *const APPS[] = {"", "WhatsApp", "Gmail", "Flex Phone con un nombre largu\xC3\xADsimo de paquete"};

static void rand_utf8(char *s, int cap)
{
    int n = (int)(rnd() % (unsigned)cap);
    for (int i = 0; i < n; i++) {
        uint32_t r = rnd() % 10;
        s[i] = r < 5 ? (char)('a' + rnd() % 26) : r < 7 ? (char)0xC3 : r < 9 ? (char)(0x80 + rnd() % 64)
                                                                         : (char)(0xE0 + rnd() % 32);
    }
    s[n] = 0;
}

static bool same_msg(const flex_bq_msg_t *a, const void *rb)
{
    // FlexPhoneBannerMsg tiene la misma disposicion: app, title, body, id, key, shownMs, src, pri, icon
    return memcmp(a, rb, sizeof(*a)) == 0;
}

void test_notif(void)
{
    // recorte UTF-8
    for (int it = 0; it < 20000; it++) {
        char src[64], a[64], b[64];
        rand_utf8(src, 60);
        size_t cap = 1 + rnd() % 40;
        memset(a, 0x55, sizeof(a));
        memset(b, 0x55, sizeof(b));
        size_t ra = flex_utf8_copy(a, cap, src), rb = ref_utf8_copy(b, cap, src);
        SAME(ra == rb && strcmp(a, b) == 0);
    }
    // historial + cola del banner
    for (int run = 0; run < 400; run++) {
        ref_ntf_reset();
        flex_ntf_hist_t h;
        memset(&h, 0, sizeof(h));
        flex_bq_t b;
        memset(&b, 0, sizeof(b));
        uint32_t now = 1000;
        for (int step = 0; step < 60; step++) {
            now += rnd() % 900;
            uint32_t op = rnd() % 10;
            if (op < 4) {
                int type = rnd() % 4 == 0 ? FLEX_NTF_MEDIA : FLEX_NTF_SYSTEM;
                const char *t = TITLES[rnd() % 8], *sub = BODIES[rnd() % 5];
                ref_ntf_sys(type, t, sub, now);
                flex_ntf_post(&h, &b, (uint8_t)type, t, sub, now);
            } else if (op < 7) {
                int pri = (int)(rnd() % 5);
                uint32_t id = rnd() % 1000;
                const char *app = APPS[rnd() % 4], *t = TITLES[rnd() % 8], *body = BODIES[rnd() % 5];
                int src = rnd() % 5 == 0 ? FLEX_BQ_SRC_SYSTEM : FLEX_BQ_SRC_PHONE;
                ref_bq_push(src, id, app, t, body, pri);
                flex_bq_msg_t m;
                flex_bq_msg_init(&m, (uint8_t)src, id, app, t, body, (uint8_t)pri);
                flex_bq_offer(&b, &m);
            } else if (op == 7) {
                // el siguiente pasa a la vista (fpbTick)
                flex_bq_msg_t m;
                bool p1 = flex_bq_pop(&b, &m), p2 = ref_bq_pop();
                SAME(p1 == p2);
                if (p1) {
                    b.cur = m;
                    b.cur_live = true;
                    b.refresh = false;
                }
            } else if (op == 8) {
                // pierde la pantalla: vuelve al frente con lo que estuvo a la vista
                if (b.cur_live) {
                    flex_bq_msg_t m = b.cur;
                    m.shown_ms += rnd() % 3000;
                    b.cur_live = false;
                    ref_bq_set_cur(false, NULL);
                    flex_bq_enqueue(&b, &m, true);
                    ref_bq_requeue_front(&m);
                }
            } else if (h.n > 0) {
                int idx = (int)(rnd() % (unsigned)h.n);
                flex_ntf_remove(&h, idx);
                ref_ntf_remove(idx);
            }
            // comparar
            bool same = ref_ntf_count() == h.n;
            for (int i = 0; same && i < h.n; i++) {
                int ty;
                const char *t, *sub;
                uint32_t born, key;
                ref_ntf_get(i, &ty, &t, &sub, &born, &key);
                same = ty == h.e[i].type && strcmp(t, h.e[i].title) == 0 && strcmp(sub, h.e[i].sub) == 0 &&
                       born == h.e[i].born_ms && key == h.e[i].key;
            }
            SAME(same);
            bool qs = ref_bq_n() == b.n && ref_bq_more() == b.more && ref_bq_refresh() == b.refresh;
            for (int i = 0; qs && i < b.n; i++) {
                qs = same_msg(&b.q[i], ref_bq_at(i));
            }
            SAME(qs);
            SAME(!b.cur_live || same_msg(&b.cur, ref_bq_cur()));
        }
    }
    // reglas sueltas de la especificacion
    CHECK(flex_bq_hold_ms(0) == 4200 && flex_bq_hold_ms(2000) == 2200 && flex_bq_hold_ms(3000) == 1500 &&
          flex_bq_hold_ms(9000) == 1500);
    CHECK(flex_bq_dismiss(150, 0, true, 452) && !flex_bq_dismiss(149, 0, true, 452));   // 452/3 = 150
    CHECK(flex_bq_dismiss(-41, -0.8f, true, 452) && !flex_bq_dismiss(-41, 0.8f, true, 452));
    CHECK(!flex_bq_dismiss(39, 2.0f, true, 452) && !flex_bq_dismiss(200, 0, false, 452));
    CHECK(flex_ntf_key(0, "") != 0);
    CHECK_EQ_I(s_diff, 0);
    printf("avisos: %s\n", s_diff ? "DISTINTO de Arduino" : "24000 pasos iguales a Arduino");
}
