#include <string.h>
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
