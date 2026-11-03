// Portapapeles de 12 ranuras (docs/spec/01b T.8, Keyboard.h FASE D) frente a Arduino: copiar, repetidos, desalojo de la
// ranura no fijada mas antigua, fijadas que sobreviven al reinicio, borrar, vaciar, y la NVS que queda.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_clip.h"
#include "flex_test.h"

void ref_clip_reset(void);
void ref_clip_nvs_put(int i, const char *t);
void ref_clip_load(void);
void ref_clip_clear_ram(void);
void ref_clip_push(const char *s, unsigned now);
void ref_clip_del(int i);
void ref_clip_pin(int i);
void ref_clip_clear_unpinned(void);
int ref_clip_count(void);
void ref_clip_slot(int i, char *text, int *used, int *pinned, unsigned *ts);
void ref_clip_classic(char *out);
int ref_clip_nvs(char *out, int cap);

static char s_nvs[FLEX_CLIP_SLOTS][FLEX_CLIP_TXT_MAX + 8];
static bool s_has[FLEX_CLIP_SLOTS];
static void st_put(int i, const char *t, void *u)
{
    (void)u;
    s_has[i] = t != NULL;
    if (t) snprintf(s_nvs[i], sizeof(s_nvs[i]), "%s", t);
}
static void st_get(int i, char *out, size_t cap, void *u)
{
    (void)u;
    snprintf(out, cap, "%s", s_has[i] ? s_nvs[i] : "");
}
static int fw_nvs(char *out, int cap)
{
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) if (s_has[i]) n += snprintf(out + n, (size_t)(cap - n), "clip%d=%s;", i, s_nvs[i]);
    return n;
}
// clave ordenada como std::map<string>: "clip0" < "clip1" < "clip10" < "clip11" < "clip2" ...
static int cmp_key(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static void fw_nvs_sorted(char *out, int cap)
{
    char keys[FLEX_CLIP_SLOTS][16];
    const char *ptr[FLEX_CLIP_SLOTS];
    int n = 0;
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) if (s_has[i]) { snprintf(keys[n], sizeof(keys[n]), "clip%d", i); ptr[n] = keys[n]; n++; }
    qsort(ptr, (size_t)n, sizeof(ptr[0]), cmp_key);
    int w = 0;
    out[0] = 0;
    for (int k = 0; k < n; k++) {
        int idx = atoi(ptr[k] + 4);
        w += snprintf(out + w, (size_t)(cap - w), "%s=%s;", ptr[k], s_nvs[idx]);
    }
}

static uint32_t s_seed = 7001;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static bool same(const flex_clip_t *c)
{
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        char t[FLEX_CLIP_TXT_MAX];
        int used, pinned;
        unsigned ts;
        ref_clip_slot(i, t, &used, &pinned, &ts);
        if (used != c->slot[i].used || (used && (pinned != c->slot[i].pinned || ts != c->slot[i].ts || strcmp(t, c->slot[i].text)))) return false;
    }
    char cl[FLEX_CLIP_CLASSIC];
    ref_clip_classic(cl);
    if (strcmp(cl, ((flex_clip_t *)c)->classic)) return false;
    char a[8192], b[8192];
    ref_clip_nvs(a, sizeof(a));
    fw_nvs_sorted(b, sizeof(b));
    return !strcmp(a, b) && ref_clip_count() == flex_clip_count(c);
}

void test_clip(void)
{
    static const char *const TXT[] = {"hola", "mundo", "a", "otra cosa distinta", "x y z", "12345", "linea\nsalto", "tan igual", "casa", "perro", "gato", "luna", "sol", "mar", "rio", "paz"};
    static flex_clip_t C;
    int bad = 0, evicted = 0, pins = 0, restarts = 0;
    for (int seq = 0; seq < 300; seq++) {
        ref_clip_reset();
        memset(s_has, 0, sizeof(s_has));
        flex_clip_store_t st = {st_put, st_get, NULL};
        flex_clip_init(&C, &st);
        unsigned now = 100;
        for (int step = 0; step < 80; step++) {
            now += 1 + rnd() % 40;
            int op = (int)(rnd() % 10);
            if (op <= 4) {
                char gen[16];
                snprintf(gen, sizeof(gen), "w%u", (unsigned)(rnd() % 40));   // mas textos distintos que ranuras: se llena y se desaloja
                const char *s = (rnd() % 3) ? gen : TXT[rnd() % 16];
                int before = flex_clip_count(&C);
                ref_clip_push(s, now);
                flex_clip_push(&C, s, now);
                evicted += before == FLEX_CLIP_SLOTS;
            } else if (op == 5) {
                int i = (int)(rnd() % 14) - 1;
                ref_clip_pin(i);
                flex_clip_toggle_pin(&C, i);
                pins++;
            } else if (op == 6) {
                int i = (int)(rnd() % 14) - 1;
                ref_clip_del(i);
                flex_clip_del(&C, i);
            } else if (op == 7) {
                ref_clip_clear_unpinned();
                flex_clip_clear_unpinned(&C);
            } else if (op == 8 && rnd() % 3 == 0) {   // reinicio: solo sobreviven las fijadas
                ref_clip_clear_ram();
                ref_clip_load();
                flex_clip_init(&C, &st);
                restarts++;
            }
            if (!same(&C)) {
                bad++;
                break;
            }
        }
    }
    CHECK(bad == 0);
    CHECK(evicted > 50 && pins > 200 && restarts > 50);   // llegaron a llenarse, fijar y reiniciar de verdad

    // ---- texto largo: el buffer clasico guarda 511 bytes y cada ranura 199; sin partir un caracter
    ref_clip_reset();
    memset(s_has, 0, sizeof(s_has));
    flex_clip_store_t st = {st_put, st_get, NULL};
    flex_clip_init(&C, &st);
    char big[700];
    memset(big, 'a', sizeof(big) - 1);
    big[699] = 0;
    ref_clip_push(big, 5);
    flex_clip_push(&C, big, 5);
    CHECK(same(&C) && strlen(C.classic) == 511 && strlen(C.slot[0].text) == 199);
    char acc[700] = "aa";
    for (int i = 0; i < 100; i++) strcat(acc, "\xC3\xB1");   // "aa" + 100 "ñ" = 202 bytes: el byte 199 cae en mitad de una ñ
    flex_clip_push(&C, acc, 9);
    int l = (int)strlen(C.slot[1].text);
    CHECK(l == 198 && ((unsigned char)C.slot[1].text[l - 1]) == 0xB1);   // 98 ñ enteras: no queda un byte suelto
    // ---- las 12 fijadas: no se toca ninguna y lo nuevo se descarta
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) {
        char t[16];
        snprintf(t, sizeof(t), "t%u", (unsigned)i);
        flex_clip_push(&C, t, 20 + (unsigned)i);
    }
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) if (C.slot[i].used) flex_clip_toggle_pin(&C, i);
    flex_clip_push(&C, "nueva", 99);
    int hit = 0;
    for (int i = 0; i < FLEX_CLIP_SLOTS; i++) hit += !strcmp(C.slot[i].text, "nueva");
    CHECK(hit == 0 && flex_clip_count(&C) == FLEX_CLIP_SLOTS);
    int vis[FLEX_CLIP_SLOTS];
    CHECK(flex_clip_visible(&C, true, vis) == FLEX_CLIP_SLOTS);
    char dump[8192];
    fw_nvs(dump, sizeof(dump));
    CHECK(strstr(dump, "clip0=") != NULL);
    printf("portapapeles: 12 ranuras, fijadas en la NVS, repetidos y desalojo iguales a Arduino (%d desalojos, %d fijados, %d reinicios)\n", evicted, pins, restarts);
}
