// Caja de aplicaciones (flex_drawer_model.c) contra drwFilter de Arduino:
// mismas apps, mismo filtro y mismo orden con nombres reales y aleatorios.
#include <string.h>
#include "flex_drawer_model.h"
#include "flex_test.h"

int ref_drawer_filter(const char *const names[19], uint32_t hidden, const char *const *pkg, int pkg_n,
                      uint32_t pkg_hidden, bool show_hidden, const char *q, uint8_t *kinds, int16_t *idx);

static const char *s_names[19];
static uint32_t s_hidden, s_pkg_hidden;
static const char *s_pkg[24];
static const char *app_name(int id) { return s_names[id]; }
static bool app_hidden(int id) { return (s_hidden >> id) & 1u; }
static const char *pkg_name(int i) { return s_pkg[i]; }
static bool pkg_hidden(int i) { return (s_pkg_hidden >> i) & 1u; }

static uint32_t s_seed = 4242;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static const char *const ES[19] = {"Reloj", "Galer\xC3\xAD" "a", "Multimedia", "Almacenamiento", "Modo PC", "Notas",
                                   "Navegador", "Flex Compass", "Paint", "Juegos", "Ajustes", "Calculadora",
                                   "Calendario", "C\xC3\xA1mara", "Clima", "Flex Store", "Flex Phone", "Device Care",
                                   "M\xC3\xBAsica"};

static int compare(const char *q, bool show, int pkg_n)
{
    flex_drw_src_t src = {19, app_name, app_hidden, pkg_n, pkg_name, pkg_hidden};
    flex_drw_cell_t mine[64];
    uint8_t rk[64];
    int16_t ri[64];
    int n = flex_drw_filter(&src, show, q, (int)strlen(q), mine, 64);
    int rn = ref_drawer_filter(s_names, s_hidden, s_pkg, pkg_n, s_pkg_hidden, show, q, rk, ri);
    if (n != rn) {
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (mine[i].kind != rk[i] || mine[i].idx != ri[i]) {
            return 1;
        }
    }
    return 0;
}

void test_drawer(void)
{
    // Orden real en espanol (spec 01a §11.3): la "a" acentuada va tras las ASCII
    for (int i = 0; i < 19; i++) {
        s_names[i] = ES[i];
    }
    s_hidden = 0;
    flex_drw_src_t src = {19, app_name, app_hidden, 0, pkg_name, pkg_hidden};
    flex_drw_cell_t c[32];
    int n = flex_drw_filter(&src, false, "", 0, c, 32);
    CHECK_EQ_I(n, 19);
    CHECK(!strcmp(app_name(c[0].idx), "Ajustes"));
    CHECK(!strcmp(app_name(c[4].idx), "Clima"));
    CHECK(!strcmp(app_name(c[5].idx), "C\xC3\xA1mara"));
    CHECK(!strcmp(app_name(c[18].idx), "Reloj"));
    // subcadena sin mayusculas; los acentos no casan
    CHECK(flex_drw_match("Flex Store", "STO", 3));
    CHECK(!flex_drw_match("C\xC3\xA1mara", "camara", 6));
    CHECK(flex_drw_match("Notas", "", 0));
    // ocultas fuera salvo "ver ocultas"
    s_hidden = (1u << 5) | (1u << 9);
    CHECK_EQ_I(flex_drw_filter(&src, false, "", 0, c, 32), 17);
    CHECK_EQ_I(flex_drw_filter(&src, true, "", 0, c, 32), 19);
    CHECK_EQ_I(flex_drw_filter(&src, false, "no", 2, c, 32), 0);   // "Notas" oculta

    // Aleatorio contra Arduino: nombres con mayusculas, acentos y duplicados,
    // paquetes, ocultas y consultas
    static char pool[19 + 24][16];
    static const char alpha[] = "aAbBcCeEfFlLnNoOrRsStTyYzZ@[`{ \xC3\xA1";
    int diffs = 0;
    for (int it = 0; it < 6000; it++) {
        for (int i = 0; i < 19 + 24; i++) {
            int len = 1 + (int)(rnd() % 8);
            int k = 0;
            for (; k < len; k++) {
                pool[i][k] = alpha[rnd() % (sizeof(alpha) - 1)];
            }
            pool[i][k] = 0;
            if (rnd() % 7 == 0 && i > 0) {
                strcpy(pool[i], pool[rnd() % (unsigned)i]);   // nombres repetidos: desempate
            }
        }
        for (int i = 0; i < 19; i++) {
            s_names[i] = pool[i];
        }
        int pkg_n = (int)(rnd() % 25);
        for (int i = 0; i < 24; i++) {
            s_pkg[i] = pool[19 + i];
        }
        s_hidden = rnd() % 3 ? rnd() & 0x7FFFF : 0;
        s_pkg_hidden = rnd() % 3 ? rnd() & 0xFFFFFF : 0;
        char q[8] = {0};
        int ql = (int)(rnd() % 4);
        for (int k = 0; k < ql; k++) {
            q[k] = "abcelnorstzAEZ@["[rnd() % 16];
        }
        diffs += compare(q, rnd() % 2, pkg_n);
    }
    if (diffs) {
        fprintf(stderr, "caja de aplicaciones: %d filtros distintos de la version Arduino\n", diffs);
    }
    CHECK_EQ_I(diffs, 0);
}
