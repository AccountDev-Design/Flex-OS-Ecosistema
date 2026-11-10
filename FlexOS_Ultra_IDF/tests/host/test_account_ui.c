// Flex Account · decisiones de la pantalla (flex_account_ui_model.c) contra FlexOS_Account_Bridge.h de Arduino, compilado tal cual con el dibujo sustituido por registros
// (ref/ref_account_ui.cpp): colores, etiquetas, nota del codigo, texto de Ajustes y, sobre una rejilla de toques por cada combinacion de estado dibujado / confirmacion /
// primer arranque / destino de retorno / Wi-Fi / foto actual, QUE hace cada toque y con que efectos (cancelar, desvincular, abrir Wi-Fi, pedir enlace, salir).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_account_ui_model.h"
#include "ref/ref_snap.h"

static int g_checks, g_fails;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fails++; if (g_fails < 30) { printf("  FALLO: "); printf(__VA_ARGS__); printf("  (%s:%d)\n", __FILE__, __LINE__); } } } while (0)

int ref_ui_needs_relink(const ref_snap_t *s);
unsigned ref_ui_link_rgb565(int link);
const char *ref_ui_primary_label(int state, int online);
const char *ref_ui_code_note(const ref_snap_t *s);
void ref_ui_settings_text(const ref_snap_t *s, char *out, unsigned long n);
unsigned ref_ui_tap(const ref_snap_t *s, int built, int ask, int first, int ret, int online, int x, int y, int *ask_after, int *state_after);
void ref_ui_render(const ref_snap_t *s, int online, int ask, int first, int ret);
int ref_ui_draw_count(void);
int ref_ui_draw_get(int i, char *op, unsigned long opcap, int *a, unsigned *color, int *alpha, char *text, unsigned long textcap);
const char *ref_acct_link_label(int l);
enum { EV_CANCEL = 1, EV_FORGET = 2, EV_WIFI = 4, EV_REQUEST = 8, EV_OOBE_SAVE = 16, EV_SETTINGS = 32, EV_STORE = 64, EV_CLOUD = 128, EV_NOTIFY = 256, EV_FLUSH = 512, EV_VALIDATE = 1024 };

static uint32_t s_seed = 99;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 8;
}
static unsigned rgb565_of(uint32_t rgb) { return ((rgb >> 16 & 0xF8) << 8) | ((rgb >> 8 & 0xFC) << 3) | ((rgb & 0xFF) >> 3); }

static void fill_snap(ref_snap_t *s)
{
    static const char *const ERR[] = {"", "", "Sin Wi-Fi", "Fallo HTTPS -1 (sin DNS ni TCP o tiempo agotado)", "x"};
    static const char *const ADDR[] = {"", "ana.p4@flex", "otra@flex", "maria.lopez.hernandez@flex"};
    static const char *const DET[] = {"", "Conectada con Flex Account", "Sin Wi-Fi: se comprobara al volver la conexion"};
    memset(s, 0, sizeof(*s));
    s->state = (int)(rnd() % 7);
    s->progress = (int)(rnd() % 101);
    s->linked = (int)(rnd() % 2);
    snprintf(s->stage, sizeof(s->stage), "etapa %u", rnd() % 5);
    snprintf(s->error, sizeof(s->error), "%s", ERR[rnd() % 5]);
    snprintf(s->code, sizeof(s->code), "%s", rnd() % 2 ? "FLX7Q2" : "");
    snprintf(s->flex_address, sizeof(s->flex_address), "%s", ADDR[rnd() % 4]);
    snprintf(s->display_name, sizeof(s->display_name), "%s", rnd() % 2 ? "Ana" : "");
    s->link = (int)(rnd() % 7);
    snprintf(s->link_detail, sizeof(s->link_detail), "%s", DET[rnd() % 3]);
}

static void t_text(void)
{
    for (int i = 0; i < 20000; i++) {
        ref_snap_t r;
        fill_snap(&r);
        flex_account_snapshot_t m;
        memset(&m, 0, sizeof(m));
        m.state = (flex_account_state_t)r.state;
        m.linked = r.linked != 0;
        m.link = (flex_account_link_t)r.link;
        memcpy(m.error, r.error, sizeof(m.error));
        memcpy(m.flex_address, r.flex_address, sizeof(m.flex_address));
        CHECK(flex_account_needs_relink(&m) == (ref_ui_needs_relink(&r) != 0), "needs_relink");
        CHECK(!strcmp(flex_account_code_note(&m), ref_ui_code_note(&r)), "code_note '%s'", flex_account_code_note(&m));
        char a[96] = "", b[96] = "";
        memset(a, 'x', sizeof(a) - 1);
        memset(b, 'x', sizeof(b) - 1);
        unsigned long cap = rnd() % 3 ? sizeof(a) : 1 + rnd() % 40;
        flex_account_settings_text_for(&m, a, cap);
        ref_ui_settings_text(&r, b, cap);
        CHECK(!memcmp(a, b, sizeof(a)), "settings_text '%s' vs '%s'", a, b);
        const char *want_title_linked = NULL;
        (void)want_title_linked;
    }
    for (int l = 0; l < 7; l++) {
        CHECK(rgb565_of(flex_account_link_rgb((flex_account_link_t)l)) == ref_ui_link_rgb565(l), "color del vinculo %d", l);
    }
    for (int st = 0; st < 7; st++) {
        for (int on = 0; on < 2; on++) {
            CHECK(!strcmp(flex_account_primary_label((flex_account_state_t)st, on), ref_ui_primary_label(st, on)), "etiqueta principal %d/%d", st, on);
        }
    }
}

static int classify(unsigned ev, int ask, int ask_after)
{
    if (ev & EV_FORGET) return FLEX_ACC_UNLINK_NOW;
    if (ev & (EV_OOBE_SAVE | EV_SETTINGS | EV_STORE)) return FLEX_ACC_FINISH;
    if (ev & EV_WIFI) return FLEX_ACC_WIFI;
    if (ev & EV_REQUEST) return FLEX_ACC_REQUEST_CODE;
    if (ev & EV_CANCEL) return FLEX_ACC_CANCEL_LINK;
    // lo unico que repinta sin otro efecto es abrir o descartar la confirmacion (el repintado la apaga si la cuenta ya no esta vinculada: por eso se mira el repintado y no ask_after)
    if (ev & EV_FLUSH) return ask ? FLEX_ACC_ASK_DISMISS : FLEX_ACC_ASK_OPEN;
    (void)ask_after;
    return FLEX_ACC_NONE;
}

static void t_taps(void)
{
    static const struct { int linked, link; } LK[] = {{1, FLEX_LINK_LINKED}, {1, FLEX_LINK_AUTH_REQUIRED}, {1, FLEX_LINK_TOKEN_EXPIRED}, {0, FLEX_LINK_UNLINKED}, {1, FLEX_LINK_LINKED_OFFLINE},
                                                      {0, FLEX_LINK_AUTH_REQUIRED}};
    int xs[64], nx = 0;
    static const int XB[] = {0, 1, 5, 51, 52, 63, 64, 65, 100, 115, 116, 117, 229, 230, 239, 240, 241, 249, 250, 251, 363, 364, 365, 400, 427, 428, 429, 450, 479};
    static const int XF[] = {0, 63, 64, 116, 117, 240, 249, 250, 364, 365, 428, 429, 479};   // mutacion (ACCT_FAST): las fronteras, no la rejilla entera
    const bool fast = getenv("ACCT_FAST") != NULL;
    if (fast) {
        for (unsigned i = 0; i < sizeof(XF) / sizeof(XF[0]); i++) xs[nx++] = XF[i];
    } else {
        for (unsigned i = 0; i < sizeof(XB) / sizeof(XB[0]); i++) xs[nx++] = XB[i];
    }
    long long total = 0, buckets[8] = {0};
    for (int built = 0; built < 7; built++) {
        for (int curs = 0; curs < (fast ? 1 : 2); curs++) {
            for (int ask = 0; ask < 2; ask++) {
                for (int ret = 0; ret < 3; ret++) {
                    for (int online = 0; online < 2; online++) {
                        for (unsigned k = 0; k < sizeof(LK) / sizeof(LK[0]); k++) {
                            ref_snap_t r;
                            memset(&r, 0, sizeof(r));
                            r.state = curs ? (built + 3) % 7 : built;
                            r.linked = LK[k].linked;
                            r.link = LK[k].link;
                            snprintf(r.flex_address, sizeof(r.flex_address), "ana@flex");
                            flex_account_snapshot_t m;
                            memset(&m, 0, sizeof(m));
                            m.state = (flex_account_state_t)r.state;
                            m.linked = r.linked != 0;
                            m.link = (flex_account_link_t)r.link;
                            bool relink = flex_account_needs_relink(&m);
                            int first = ret == 0;
                            for (int y = 0; y < 800; y++) {
                                for (int i = 0; i < nx; i++) {
                                    int ask_after = -1, state_after = -1;
                                    unsigned ev = ref_ui_tap(&r, built, ask, first, ret, online, xs[i], y, &ask_after, &state_after);
                                    int want = classify(ev, ask, ask_after);
                                    int got = flex_account_hit((flex_account_state_t)built, ask != 0, first != 0, relink, online != 0, xs[i], y);
                                    total++;
                                    buckets[got]++;
                                    if (got != want) {
                                        CHECK(0, "toque (%d,%d) built=%d ask=%d first=%d relink=%d online=%d: C=%d Arduino=%d", xs[i], y, built, ask, first, relink, online, got, want);
                                        continue;
                                    }
                                    g_checks++;
                                    if (got == FLEX_ACC_FINISH) {
                                        // efectos de salir: cancelar SOLO con un enlace en curso, y el destino de retorno
                                        CHECK(((ev & EV_CANCEL) != 0) == flex_account_finish_cancels((flex_account_state_t)r.state), "salir: cancelar en estado %d", r.state);
                                        unsigned dest = ret == 0 ? EV_OOBE_SAVE : ret == 2 ? EV_SETTINGS : EV_STORE;
                                        CHECK((ev & (EV_OOBE_SAVE | EV_SETTINGS | EV_STORE)) == dest, "salir: destino %d", ret);
                                    } else if (got == FLEX_ACC_UNLINK_NOW) {
                                        CHECK((ev & (EV_CLOUD | EV_NOTIFY)) == (EV_CLOUD | EV_NOTIFY), "desvincular avisa a la nube y al usuario");
                                    } else if (got == FLEX_ACC_REQUEST_CODE || got == FLEX_ACC_WIFI || got == FLEX_ACC_CANCEL_LINK) {
                                        CHECK((ev & (EV_OOBE_SAVE | EV_SETTINGS | EV_STORE | EV_FORGET)) == 0, "la accion no sale ni desvincula");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    printf("  (%lld toques: ninguno %lld, salir %lld, pedir confirmacion %lld, descartarla %lld, desvincular %lld, Wi-Fi %lld, enlazar %lld, cancelar enlace %lld)\n", total, buckets[0], buckets[1],
           buckets[2], buckets[3], buckets[4], buckets[5], buckets[6], buckets[7]);
    CHECK(buckets[1] && buckets[2] && buckets[3] && buckets[4] && buckets[5] && buckets[6] && buckets[7], "la rejilla alcanza todas las acciones");
}

// ---- los dibujos de Arduino como DORADO para la escena del simulador: por cada caso (foto, Wi-Fi, confirmacion, destino) las primitivas que pinta accountRender ----
// Linea de caso: C estado progreso enlazada vinculo online ask ret<TAB>etapa<TAB>error<TAB>codigo<TAB>direccion<TAB>nombre<TAB>detalle ; luego una linea por primitiva:
// R x y w h r color565 alfa (rectangulo redondeado / rectangulo), O cx cy r*10 color565 (circulo), S x0 y0 x1 y1 grosor color565 (trazo), T cx y tamano color565<TAB>texto
static int dump_golden(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        return 1;
    }
    static const char *const STAGE[] = {"Preparando enlace", "Contactando Flex Account"};
    static const char *const ERR[] = {"", "Sin Wi-Fi. Se reintenta al volver la conexion", "Fallo HTTPS -1 (sin DNS ni TCP o tiempo agotado)"};
    static const char *const ADDR[] = {"ana.p4@flex", "maria.lopez.hernandez@flex"};
    static const char *const NAME[] = {"", "Ana"};
    static const char *const DET[] = {"", "Conectada con Flex Account"};
    long cases = 0, ops = 0;
    for (int state = 0; state < 7; state++) {
        for (int link = 0; link < 7; link++) {
            for (int linked = 0; linked < 2; linked++) {
                for (int online = 0; online < 2; online++) {
                    for (int ask = 0; ask < 2; ask++) {
                        for (int ret = 0; ret < 3; ret++) {
                            for (int v = 0; v < 3; v++) {
                                if (!linked && state == 3 && v > 0) continue;
                                ref_snap_t r;
                                memset(&r, 0, sizeof(r));
                                r.state = state;
                                r.link = link;
                                r.linked = linked;
                                r.progress = v == 0 ? 8 : v == 1 ? 55 : 100;
                                snprintf(r.stage, sizeof(r.stage), "%s", STAGE[v & 1]);
                                snprintf(r.error, sizeof(r.error), "%s", ERR[v]);
                                snprintf(r.code, sizeof(r.code), "%s", "FLX7Q2");
                                snprintf(r.flex_address, sizeof(r.flex_address), "%s", ADDR[v & 1]);
                                snprintf(r.display_name, sizeof(r.display_name), "%s", NAME[v == 0 ? 0 : 1]);
                                snprintf(r.link_detail, sizeof(r.link_detail), "%s", DET[v == 2 ? 1 : v & 1]);
                                ref_ui_render(&r, online, ask, ret == 0, ret);
                                fprintf(f, "C %d %d %d %d %d %d %d\t%s\t%s\t%s\t%s\t%s\t%s\n", state, r.progress, linked, link, online, ask, ret, r.stage, r.error, r.code, r.flex_address,
                                        r.display_name, r.link_detail);
                                cases++;
                                for (int i = 0, n = ref_ui_draw_count(); i < n; i++) {
                                    char op[16], text[256];
                                    int a[5], alpha;
                                    unsigned color;
                                    ref_ui_draw_get(i, op, sizeof(op), a, &color, &alpha, text, sizeof(text));
                                    if (!strcmp(op, "rrectA") || !strcmp(op, "rrect") || !strcmp(op, "rectA")) {
                                        fprintf(f, "R %d %d %d %d %d %u %d\n", a[0], a[1], a[2], a[3], !strcmp(op, "rectA") ? 0 : a[4], color, alpha);
                                    } else if (!strcmp(op, "circle")) {
                                        fprintf(f, "O %d %d %d %u\n", a[0], a[1], a[2], color);
                                    } else if (!strcmp(op, "seg")) {
                                        fprintf(f, "S %d %d %d %d %d %u\n", a[0], a[1], a[2], a[3], a[4], color);
                                    } else if (!strcmp(op, "text")) {
                                        fprintf(f, "T %d %d %d %u\t%s\n", a[0], a[1], a[2], color, text);
                                    }
                                    ops++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    fclose(f);
    printf("  (dorado de la pantalla: %ld casos, %ld primitivas -> %s)\n", cases, ops, path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--golden")) {
        return dump_golden(argv[2]);
    }
    t_text();
    t_taps();
    if (g_fails) {
        printf("FALLOS: %d de %d (Flex Account, pantalla)\n", g_fails, g_checks);
        return 1;
    }
    printf("OK: Flex Account, pantalla (%d comprobaciones: textos, colores, destinos y toques contra FlexOS_Account_Bridge.h)\n", g_checks);
    return 0;
}
