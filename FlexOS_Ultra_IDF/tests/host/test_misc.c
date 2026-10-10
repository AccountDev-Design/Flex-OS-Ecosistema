#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "flex_flip_rows.h"
#include "flex_fs_path.h"
#include "flex_test.h"

#define BASE 5   // "/flex"
#define SUF 5    // ".tmp~"
#define CAP 160

void test_fs_path(void)
{
    CHECK(flex_fs_path_valid("/notes/a.txt", BASE, SUF, CAP));
    CHECK(flex_fs_path_valid("/a..b", BASE, SUF, CAP));          // ".." dentro de un nombre vale
    CHECK(flex_fs_path_valid("/..a/b", BASE, SUF, CAP));
    CHECK(flex_fs_path_valid("/a/b..", BASE, SUF, CAP));
    CHECK(!flex_fs_path_valid("/..", BASE, SUF, CAP));
    CHECK(!flex_fs_path_valid("/../x", BASE, SUF, CAP));
    CHECK(!flex_fs_path_valid("/a/../../x", BASE, SUF, CAP));
    CHECK(!flex_fs_path_valid("/a/..", BASE, SUF, CAP));
    CHECK(!flex_fs_path_valid("notes/a.txt", BASE, SUF, CAP));   // sin '/' inicial
    CHECK(!flex_fs_path_valid("", BASE, SUF, CAP));
    CHECK(!flex_fs_path_valid(NULL, BASE, SUF, CAP));
    // Largo: ruta + base + sufijo + NUL debe caber en CAP.
    char p[CAP + 1];
    memset(p, 'a', sizeof(p));
    p[0] = '/';
    p[CAP - BASE - SUF - 1] = '\0';
    CHECK(flex_fs_path_valid(p, BASE, SUF, CAP));
    p[CAP - BASE - SUF - 1] = 'a';
    p[CAP - BASE - SUF] = '\0';
    CHECK(!flex_fs_path_valid(p, BASE, SUF, CAP));
}

void test_flip_rows(void)
{
    flex_flip_rows_t r;
    int32_t a, b;
    flex_flip_rows_init(&r, 800);
    flex_flip_rows_add(&r, 100, 120);
    flex_flip_rows_take(&r, 0, &a, &b);
    CHECK_EQ_I(a, 0);      // el primer cuadro incluye la pantalla entera anterior
    CHECK_EQ_I(b, 799);
    flex_flip_rows_add(&r, 300, 310);
    flex_flip_rows_take(&r, 0, &a, &b);
    CHECK_EQ_I(a, 100);    // union con el cuadro anterior (refr_sync_areas)
    CHECK_EQ_I(b, 310);
    flex_flip_rows_add(&r, 305, 306);
    flex_flip_rows_take(&r, 0, &a, &b);
    CHECK_EQ_I(a, 300);
    CHECK_EQ_I(b, 310);
    flex_flip_rows_add(&r, -5, 900);   // se recorta a la pantalla
    flex_flip_rows_take(&r, 0, &a, &b);
    CHECK_EQ_I(a, 0);
    CHECK_EQ_I(b, 799);
    flex_flip_rows_add(&r, 10, 11);
    flex_flip_rows_take(&r, 1, &a, &b);
    CHECK_EQ_I(a, 0);
    CHECK_EQ_I(b, 799);
}

// ---- vaciar una carpeta (fsWipeDir) en un directorio temporal de verdad ----------------
static void touch_file(const char *dir, const char *name)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (f) {
        fputs("x", f);
        fclose(f);
    }
}

static int entries(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        n += strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0;
    }
    closedir(d);
    return n;
}

void test_fs_wipe(void)
{
    char root[] = "/tmp/flexwipeXXXXXX";
    if (!mkdtemp(root)) {
        CHECK(!"mkdtemp");
        return;
    }
    char cache[300], sub[340], deep[380], keep[300];
    snprintf(cache, sizeof(cache), "%s/Cache", root);
    snprintf(sub, sizeof(sub), "%s/thumbs", cache);
    snprintf(deep, sizeof(deep), "%s/a", sub);
    snprintf(keep, sizeof(keep), "%s/Notes", root);
    mkdir(cache, 0775);
    mkdir(sub, 0775);
    mkdir(deep, 0775);
    mkdir(keep, 0775);
    for (int i = 0; i < 40; i++) {   // mas que una tanda (16)
        char n[24];
        snprintf(n, sizeof(n), "f%02d.bin", i);
        touch_file(cache, n);
    }
    touch_file(sub, "t1.jpg");
    touch_file(sub, "t2.jpg");
    touch_file(deep, "z");
    touch_file(keep, "nota.txt");
    bool failed = true;
    CHECK_EQ_I(flex_fs_wipe_tree(cache, &failed), 43);
    CHECK(!failed);
    CHECK_EQ_I(entries(cache), 0);    // la carpeta queda, vacia
    CHECK_EQ_I(entries(keep), 1);     // lo de al lado no se toca
    // carpeta inexistente: nada que hacer y no es un fallo
    char none[320];
    snprintf(none, sizeof(none), "%s/nada", root);
    CHECK_EQ_I(flex_fs_wipe_tree(none, &failed), 0);
    CHECK(!failed);
    CHECK_EQ_I(flex_fs_wipe_tree("", &failed), 0);
    // limpiar el temporal
    char p[400];
    snprintf(p, sizeof(p), "%s/nota.txt", keep);
    unlink(p);
    rmdir(keep);
    rmdir(cache);
    rmdir(root);
}
