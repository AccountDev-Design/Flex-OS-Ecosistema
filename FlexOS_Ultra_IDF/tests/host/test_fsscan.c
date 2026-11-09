// Listado de carpetas, conteo, tamanos por categoria y ficheros mas grandes de Almacenamiento / Archivos frente a flexFsList / flexFsCount /
// flexFsDirSize / flexFsCatSize / flexFsLargest de Arduino (FlexOS_FS.cpp), compiladas tal cual sobre el mismo arbol de carpetas del PC.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "flex_fs_ops.h"
#include "flex_test.h"

void ref_fsscan_root(const char *root);
int ref_fsscan_list(const char *dir, int maxn, char *out, int cap);
int ref_fsscan_count(const char *dir);
unsigned ref_fsscan_dirsize(const char *dir);
unsigned ref_fsscan_cat(int cat);
int ref_fsscan_largest(int n, char *out, int cap);

static char B[96];
static uint32_t s_seed = 3;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static void mkp(const char *rel)
{
    char a[256], t[256];
    snprintf(a, sizeof(a), "%s%s", B, rel);
    snprintf(t, sizeof(t), "%s", a);
    for (char *p = t + strlen(B) + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(t, 0775);
            *p = '/';
        }
    }
    mkdir(t, 0775);
}
static void putn(const char *rel, size_t n)
{
    char a[256];
    snprintf(a, sizeof(a), "%s%s", B, rel);
    FILE *f = fopen(a, "wb");
    if (f) {
        for (size_t i = 0; i < n; i++) fputc('x', f);
        fclose(f);
    }
}
static void wipe(void)
{
    char c[400];
    snprintf(c, sizeof(c), "rm -rf '%s'/* '%s'/.[!.]* 2>/dev/null", B, B);
    (void)!system(c);
}

// Arbol aleatorio. Los nombres son distintos aunque se ignoren las mayusculas (LittleFS distingue; el orden entre dos que solo difieren
// en las mayusculas depende del orden fisico en Arduino y es determinista aqui: ver el caso aparte).
static int s_uid;
static void name_for(char *out, size_t n, bool dir)
{
    static const char *const stems[] = {"Foto", "doc", "Nota", "VIDEO", "zeta", "Alfa", "beta", "Cache", "x", "Mi archivo", "Ñandú"};
    snprintf(out, n, "%s%s %d%s", stems[rnd() % 11], dir ? "" : "", s_uid++, dir ? "" : (rnd() % 3 ? ".bin" : ".txt"));
}
static void fill(const char *dir, int depth, int budget)
{
    int n = 1 + (int)(rnd() % 6);
    for (int i = 0; i < n && budget > 0; i++, budget--) {
        char nm[48], p[200];
        bool d = depth < 7 && rnd() % 4 == 0;
        name_for(nm, sizeof(nm), d);
        snprintf(p, sizeof(p), "%s/%s", dir, nm);
        if (d) {
            mkp(p);
            fill(p, depth + 1, budget / 2);
        } else {
            // tamanos con empates a proposito: el orden de los iguales es el del disco
            static const size_t sz[] = {0, 1, 100, 100, 2048, 2048, 5000, 777};
            putn(p, rnd() % 3 ? sz[rnd() % 8] : rnd() % 9000);
        }
    }
}

static void build_tree(int variant)
{
    wipe();
    s_uid = 1;
    static const char *const base[] = {"/Documentos", "/Notas", "/System", "/System/Cache", "/System/Media", "/System/Media/Thumbs", "/Paint", "/Papelera"};
    int nb = variant % 3 == 0 ? 8 : (variant % 3 == 1 ? 5 : 3);
    for (int i = 0; i < nb; i++) {
        mkp(base[i]);
        if (rnd() % 4) fill(base[i], 1, 6);
    }
    if (variant % 2) {   // carpetas de usuario sueltas, una de ellas muy honda (mas de 5 niveles)
        mkp("/Extra");
        fill("/Extra", 1, 8);
        mkp("/Hondo/a/b/c/d/e/f");
        putn("/Hondo/a/b/c/d/e/f/enterrado.bin", 4000);
        putn("/Hondo/a/b/c/d/e/mas.bin", 1234);
        putn("/Hondo/a/arriba.bin", 321);
    }
    if (variant % 4 == 1) {   // la biblioteca por dentro tiene los ficheros mas grandes
        putn("/System/Media/catalogo.idx", 9000);
        putn("/System/Media/Thumbs/m1.thm", 8500);
        putn("/Documentos/libro.txt", 8000);
    }
    for (int i = 0; i < (int)(rnd() % 5); i++) {
        char p[64];
        snprintf(p, sizeof(p), "/suelto %d.bin", i);
        putn(p, rnd() % 7000);
    }
}

// "d|f,nombre,size,items;" de las entradas de ESP-IDF
static int expand(const flex_fsx_ent_t *e, size_t n, char *out, int cap)
{
    int w = 0;
    out[0] = 0;
    for (size_t i = 0; i < n && w < cap - 80; i++)
        w += snprintf(out + w, (size_t)(cap - w), "%c,%s,%u,%u;", e[i].dir ? 'd' : 'f', e[i].name, (unsigned)e[i].size, (unsigned)e[i].items);
    return w;
}

static int has_prefix(const char *p, const char *pre)
{
    return strncmp(p, pre, strlen(pre)) == 0;
}

void test_fsscan(void)
{
    snprintf(B, sizeof(B), "/tmp/flexfsscan.XXXXXX");
    CHECK(mkdtemp(B) != NULL);
    ref_fsscan_root(B);
    static char ra[40000], rb[40000];
    flex_fsx_ent_t ents[FLEX_FSX_LIST_SCAN];
    const char *const dirs[] = {"/", "/Documentos", "/Notas", "/System", "/System/Media", "/Paint", "/Papelera", "/Extra", "/Hondo", "/Hondo/a", "/no existe"};
    int lists = 0;
    for (int v = 0; v < 160; v++) {
        s_seed = 100 + (uint32_t)v * 7;
        build_tree(v);
        for (size_t di = 0; di < sizeof(dirs) / sizeof(dirs[0]); di++) {
            size_t n = 0, total = 0;
            flex_fsx_res_t r = flex_fsx_list(B, dirs[di], ents, 200, &n, &total);
            int rn = ref_fsscan_list(dirs[di], 200, ra, sizeof(ra));
            struct stat st;
            char a[200];
            snprintf(a, sizeof(a), "%s%s", B, dirs[di]);
            bool exists = stat(a, &st) == 0 && S_ISDIR(st.st_mode);
            if (!exists) {
                CHECK_EQ_I(r, FLEX_FSX_NOTFOUND);
                CHECK_EQ_I(rn, 0);
                continue;
            }
            CHECK_EQ_I(r, FLEX_FSX_OK);
            expand(ents, n, rb, sizeof(rb));
            if (strcmp(ra, rb) != 0) {
                fprintf(stderr, "  lista %s (arbol %d)\n  ref: %.600s\n  mod: %.600s\n", dirs[di], v, ra, rb);
            }
            CHECK(strcmp(ra, rb) == 0);
            CHECK_EQ_I(rn, (int)n);
            CHECK_EQ_I(total, n);
            CHECK_EQ_I(ref_fsscan_count(dirs[di]), (int)total);
            CHECK_EQ_I(ref_fsscan_dirsize(dirs[di]), ({ uint32_t c, b; flex_fsx_dirstat(B, dirs[di], &c, &b); b; }));
            lists++;
            // un tope menor devuelve las primeras del orden (Arduino recorta antes de ordenar: aqui no se pierde nada por el orden del disco)
            if (n > 3) {
                size_t k = 0, tt = 0;
                flex_fsx_ent_t few[3];
                CHECK_EQ_I(flex_fsx_list(B, dirs[di], few, 3, &k, &tt), FLEX_FSX_OK);
                CHECK_EQ_I(k, 3);
                CHECK_EQ_I(tt, n);
                for (size_t i = 0; i < 3; i++) CHECK(!strcmp(few[i].name, ents[i].name));
            }
        }
        // categorias y cache
        flex_fsx_scan_t sc;
        CHECK_EQ_I(flex_fsx_scan(B, NULL, &sc), FLEX_FSX_OK);
        for (int c = 0; c < FLEX_FSX_CAT_N; c++) CHECK_EQ_I(sc.cat[c], ref_fsscan_cat(c));
        CHECK_EQ_I(sc.cache, ref_fsscan_dirsize("/System/Cache"));
        // los mayores del arbol entero, sin filtro
        int k = ref_fsscan_largest(FLEX_FSX_BIG_MAX, ra, sizeof(ra));
        CHECK_EQ_I((int)sc.nbig, k);
        char w[600] = "";
        for (size_t i = 0; i < sc.nbig; i++) snprintf(w + strlen(w), sizeof(w) - strlen(w), "%s,%u;", sc.big[i].path, (unsigned)sc.big[i].size);
        if (strcmp(ra, w) != 0) {
            fprintf(stderr, "  mayores (arbol %d)\n  ref: %s\n  mod: %s\n", v, ra, w);
        }
        CHECK(strcmp(ra, w) == 0);
        // como lo usa Arduino: pide 12 y descarta la biblioteca por dentro; aqui se descarta al recorrer
        flex_fsx_scan_t sk;
        CHECK_EQ_I(flex_fsx_scan(B, "/System/Media/", &sk), FLEX_FSX_OK);
        int k12 = ref_fsscan_largest(12, rb, sizeof(rb));
        char want[600] = "";
        int got = 0, lib = 0;
        char *save = NULL;
        for (char *tok = strtok_r(rb, ";", &save); tok && got < FLEX_FSX_BIG_MAX; tok = strtok_r(NULL, ";", &save)) {
            if (has_prefix(tok, "/System/Media/")) {
                lib++;
                continue;
            }
            snprintf(want + strlen(want), sizeof(want) - strlen(want), "%s;", tok);
            got++;
        }
        (void)k12;
        char w2[600] = "";
        for (size_t i = 0; i < sk.nbig; i++) snprintf(w2 + strlen(w2), sizeof(w2) - strlen(w2), "%s,%u;", sk.big[i].path, (unsigned)sk.big[i].size);
        if (lib <= 9 && strcmp(want, w2) != 0) {
            fprintf(stderr, "  mayores sin biblioteca (arbol %d)\n  ref: %s\n  mod: %s\n", v, want, w2);
        }
        if (lib <= 9) CHECK(strcmp(want, w2) == 0);
        for (size_t i = 0; i < sk.nbig; i++) CHECK(!has_prefix(sk.big[i].path, "/System/Media/"));
    }
    CHECK(lists > 1000);

    // ---- casos concretos -------------------------------------------------------------------------------------------------------------
    wipe();
    s_uid = 1;
    // carpetas primero, luego por nombre sin distinguir mayusculas; los iguales salvo mayusculas, en orden determinista
    mkp("/b");
    mkp("/A");
    putn("/c.txt", 5);
    putn("/B.txt", 7);
    putn("/a.txt", 1);
    size_t n = 0, total = 0;
    CHECK_EQ_I(flex_fsx_list(B, "/", ents, 10, &n, &total), FLEX_FSX_OK);
    CHECK_EQ_I(n, 5);
    CHECK(ents[0].dir && !strcmp(ents[0].name, "A") && ents[1].dir && !strcmp(ents[1].name, "b"));
    CHECK(!ents[2].dir && !strcmp(ents[2].name, "a.txt") && !strcmp(ents[3].name, "B.txt") && !strcmp(ents[4].name, "c.txt"));
    // conteo real de elementos y tamano recursivo por carpeta
    mkp("/b/x/y");
    putn("/b/x/y/f.bin", 100);
    putn("/b/g.bin", 50);
    CHECK_EQ_I(flex_fsx_list(B, "/", ents, 10, &n, &total), FLEX_FSX_OK);
    CHECK(!strcmp(ents[1].name, "b") && ents[1].items == 2 && ents[1].size == 150);
    CHECK(!strcmp(ents[0].name, "A") && ents[0].items == 0 && ents[0].size == 0);
    // un nombre que no cabe no se puede mostrar ni operar: se salta (Arduino lo truncaba y la accion fallaba)
    char longn[80];
    memset(longn, 'n', sizeof(longn));
    longn[60] = 0;
    char lp[160];
    snprintf(lp, sizeof(lp), "/%s", longn);
    putn(lp, 3);
    CHECK_EQ_I(flex_fsx_list(B, "/", ents, 10, &n, &total), FLEX_FSX_OK);
    CHECK_EQ_I(n, 5);
    // el limite del nombre: 47 caracteres se ven, 48 (o mas) no
    {
        char n47[64], n48[64], pth[160];
        memset(n47, 'q', 47);
        n47[47] = 0;
        memset(n48, 'r', 48);
        n48[48] = 0;
        snprintf(pth, sizeof(pth), "/%s", n47);
        putn(pth, 1);
        snprintf(pth, sizeof(pth), "/%s", n48);
        putn(pth, 1);
        CHECK_EQ_I(flex_fsx_list(B, "/", ents, 20, &n, &total), FLEX_FSX_OK);
        int has47 = 0, has48 = 0;
        for (size_t i = 0; i < n; i++) {
            has47 += !strcmp(ents[i].name, n47);
            has48 += !strcmp(ents[i].name, n48);
        }
        CHECK(has47 == 1 && has48 == 0);
    }
    // una ruta que no cabe en FLEX_FSX_PATH_MAX no se lista: recortada, borrar / renombrar / entrar tocaria OTRO elemento (el que se llame
    // como el principio del nombre). Con la carpeta a 118 caracteres cabe un nombre de 40 (ruta de 159) y no uno de 41 (160)
    {
        char deep[160], n40[48], n41[48], shortn[48], pth[400];
        snprintf(deep, sizeof(deep), "/%s/%s/%s", "d111111111111111111111111111111111111d", "e111111111111111111111111111111111111e",
                 "f1111111111111111111111111111111111111f");
        CHECK_EQ_I((int)strlen(deep), 118);
        mkp(deep);
        memset(n40, 'x', 40);
        n40[40] = 0;
        memset(n41, 'x', 41);
        n41[41] = 0;
        memcpy(shortn, n41, 8);   // el principio del nombre largo: lo que habria tocado el recorte
        shortn[8] = 0;
        snprintf(pth, sizeof(pth), "%s/%s", deep, n40);
        putn(pth, 7);
        snprintf(pth, sizeof(pth), "%s/%s", deep, n41);
        putn(pth, 9);
        snprintf(pth, sizeof(pth), "%s/%s", deep, shortn);
        putn(pth, 5);
        CHECK_EQ_I(flex_fsx_list(B, deep, ents, 10, &n, &total), FLEX_FSX_OK);
        CHECK_EQ_I(n, 2);
        int has40 = 0, has41 = 0, hasshort = 0;
        for (size_t i = 0; i < n; i++) {
            has40 += !strcmp(ents[i].name, n40);
            has41 += !strcmp(ents[i].name, n41);
            hasshort += !strcmp(ents[i].name, shortn);
        }
        CHECK(has40 == 1 && has41 == 0 && hasshort == 1);
    }
    // argumentos y rutas
    CHECK_EQ_I(flex_fsx_list(NULL, "/", ents, 10, &n, &total), FLEX_FSX_ARG);
    CHECK_EQ_I(flex_fsx_list(B, "/../x", ents, 10, &n, &total), FLEX_FSX_ARG);
    CHECK_EQ_I(flex_fsx_list(B, "/a.txt", ents, 10, &n, &total), FLEX_FSX_NOTFOUND);   // un fichero no es una carpeta
    CHECK_EQ_I(flex_fsx_list(B, "/", ents, 0, &n, &total), FLEX_FSX_ARG);
    // mas entradas de las que se leen: el aviso es total > n y el orden de lo que se ve es el de las que se leyeron
    wipe();
    for (int i = 0; i < FLEX_FSX_LIST_SCAN + 20; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/f%04d.bin", i);
        putn(p, 1);
    }
    CHECK_EQ_I(flex_fsx_list(B, "/", ents, 40, &n, &total), FLEX_FSX_OK);
    CHECK_EQ_I(n, 40);
    CHECK_EQ_I(total, FLEX_FSX_LIST_SCAN);
    // medidas con la raiz vacia o ausente
    wipe();
    flex_fsx_scan_t sc;
    CHECK_EQ_I(flex_fsx_scan(B, NULL, &sc), FLEX_FSX_OK);
    CHECK(sc.nbig == 0 && sc.cat[0] == 0 && sc.cache == 0);
    char gone[120];
    snprintf(gone, sizeof(gone), "%s/no", B);
    CHECK_EQ_I(flex_fsx_scan(gone, NULL, &sc), FLEX_FSX_NOTFOUND);
    // mas de nueve ficheros de la biblioteca por encima del resto: Arduino (pide 12 y filtra) se queda sin "archivos grandes"; aqui salen
    mkp("/System/Media");
    for (int i = 0; i < 12; i++) {
        char p[80];
        snprintf(p, sizeof(p), "/System/Media/m%02d.thm", i);
        putn(p, 9000 + (size_t)i);
    }
    putn("/Documentos.txt", 10);
    CHECK_EQ_I(flex_fsx_scan(B, "/System/Media/", &sc), FLEX_FSX_OK);
    CHECK(sc.nbig == 1 && !strcmp(sc.big[0].path, "/Documentos.txt"));

    wipe();
    rmdir(B);
}
