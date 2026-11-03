// Kit de archivos (docs/spec/03 §5): geometria del menu y de la confirmacion, desplazamiento de la papelera,
// texto en caja de la vista previa y edicion del nombre, frente a FileKit.h de Arduino.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "flex_fk_model.h"
#include "flex_test.h"

void ref_fk_menu(int px, int py, int cloud, int *x, int *y, int *w, int *h, int *rows);
int ref_fk_menu_hit(int mx, int my, int cloud, int px, int py);
void ref_fk_ask_geom(int *x, int *y, int *w, int *h);
int ref_fk_ask(int px, int py);
int ref_fk_trash_max(int n);
int ref_fk_textbox(int x, int y, int w, int h, const char *s, int size, char *out, int cap);
void ref_fk_name_append(char *buf, const char *s);
void ref_fk_name_back(char *buf);

static uint32_t s_seed = 99173;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static int tw(const char *t, int s, void *c)
{
    (void)c;
    return (int)strlen(t) * s * 6;
}
static char g_buf[4096];
static int g_n;
static void emit(int x, int y, const char *l, void *c)
{
    (void)c;
    g_n += snprintf(g_buf + g_n, sizeof(g_buf) - (size_t)g_n, "%d,%d,%s\n", x, y, l);
}

void test_fk(void)
{
    int bad = 0;

    // ---- menu: geometria y toque, con y sin la quinta fila, en toda la pantalla y mas alla
    for (int cloud = 0; cloud < 2; cloud++) {
        for (int py = -60; py < 880; py += 7) {
            for (int px = -60; px < 560; px += 11) {
                int x, y, w, h, rows;
                ref_fk_menu(px, py, cloud, &x, &y, &w, &h, &rows);
                flex_fk_rect_t r;
                flex_fk_menu_geom(px, py, cloud, &r);
                bad += r.x != x || r.y != y || r.w != w || r.h != h || flex_fk_menu_rows(cloud) != rows;
            }
        }
        for (int i = 0; i < 4000; i++) {
            int mx = (int)(rnd() % 600) - 60, my = (int)(rnd() % 900) - 60;
            int tx = (int)(rnd() % 520), ty = (int)(rnd() % 820);
            flex_fk_rect_t r;
            flex_fk_menu_geom(mx, my, cloud, &r);
            bad += ref_fk_menu_hit(mx, my, cloud, tx, ty) != flex_fk_menu_hit(&r, cloud, tx, ty);
        }
    }
    CHECK(bad == 0);

    // ---- confirmacion: tarjeta y los 3 resultados en todos los puntos de la pantalla
    {
        int x, y, w, h;
        ref_fk_ask_geom(&x, &y, &w, &h);
        flex_fk_rect_t c, no, yes;
        flex_fk_ask_geom(&c, &no, &yes);
        CHECK(c.x == x && c.y == y && c.w == w && c.h == h);
        int hb = 0;
        for (int ty = 0; ty < 800; ty++) {
            for (int tx = 0; tx < 480; tx++) hb += ref_fk_ask(tx, ty) != flex_fk_ask_hit(tx, ty);
        }
        CHECK(hb == 0);
    }

    // ---- papelera: desplazamiento maximo
    for (int n = 0; n <= 300; n++) bad += ref_fk_trash_max(n) != flex_fk_trash_max_scroll(n);
    CHECK(bad == 0);

    // ---- texto en caja: mismas lineas en las mismas posiciones
    {
        static const char *const PIECES[] = {"a", "bc", "hola mundo", "\n", "\r\n", "\xC3\xA1", "\xE2\x82\xAC", "\xF0\x9F\x98\x80",
                                             "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", " ", "123"};
        int tb = 0, runs = 0;
        for (int i = 0; i < 6000; i++) {
            char s[300] = "";
            int np = 1 + (int)(rnd() % 14);
            for (int k = 0; k < np; k++) strncat(s, PIECES[rnd() % 10], sizeof(s) - strlen(s) - 1);
            int size = 1 + (int)(rnd() % 4), w = 8 + (int)(rnd() % 300), h = (int)(rnd() % 320);
            int x = (int)(rnd() % 200), y = (int)(rnd() % 100);
            char ref[4096];
            ref_fk_textbox(x, y, w, h, s, size, ref, sizeof(ref));
            g_n = 0;
            g_buf[0] = 0;
            flex_fk_textbox_layout(x, y, w, h, s, size, 8 * size + 2, tw, NULL, emit, NULL);
            tb += strcmp(ref, g_buf) != 0;
            runs += g_n > 0;
        }
        CHECK(tb == 0);
        CHECK(runs > 3000);   // la mayoria de las cajas llegaron a dibujar algo
        // un texto cortado a mitad de un caracter no se lee mas alla del final
        g_n = 0;
        g_buf[0] = 0;
        flex_fk_textbox_layout(0, 0, 200, 100, "ab\xE2\x82", 2, 18, tw, NULL, emit, NULL);
        CHECK(g_n > 0);
    }

    // ---- nombre: anadir (sin '/' y con tope) y borrar un caracter UTF-8 completo
    {
        static const char *const KEYS[] = {"a", "b", " ", "\xC3\xB1", "\xE2\x82\xAC", "/", "x/y", "1", "\xF0\x9F\x98\x80", "Z"};
        int nb = 0;
        for (int seq = 0; seq < 400; seq++) {
            char a[48] = "", b[48] = "";
            for (int step = 0; step < 80; step++) {
                if (rnd() % 4 == 0) {
                    ref_fk_name_back(a);
                    flex_fk_name_back(b);
                } else {
                    const char *k = KEYS[rnd() % 10];
                    ref_fk_name_append(a, k);
                    flex_fk_name_append(b, sizeof(b), k);
                }
                nb += strcmp(a, b) != 0;
            }
            CHECK(strchr(b, '/') == NULL && strlen(b) < 48);
        }
        CHECK(nb == 0);
    }
    printf("kit de archivos: menu, confirmacion, papelera, texto en caja y nombre iguales a Arduino\n");
}
