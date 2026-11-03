// Operaciones de archivo del kit (borrar, renombrar, papelera, restaurar, vaciar, fichero nuevo)
// sobre un directorio real: lo que la tarea de almacenamiento hace con LittleFS, aqui con el disco del PC.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "flex_fs_ops.h"
#include "flex_fs_path.h"
#include "flex_test.h"

static char P[64];   // carpeta privada de la prueba
static char B[80];   // raiz del almacenamiento: dentro de P, para que un ".." caiga en P y no en /tmp

static void path(const char *rel, char *out, size_t n) { snprintf(out, n, "%s%s", B, rel); }
static bool has(const char *rel)
{
    char a[256];
    struct stat st;
    path(rel, a, sizeof(a));
    return stat(a, &st) == 0;
}
static bool is_dir_(const char *rel)
{
    char a[256];
    struct stat st;
    path(rel, a, sizeof(a));
    return stat(a, &st) == 0 && S_ISDIR(st.st_mode);
}
static void mk(const char *rel)
{
    char a[256];
    path(rel, a, sizeof(a));
    mkdir(a, 0775);
}
static void put(const char *rel, const char *txt)
{
    char a[256];
    path(rel, a, sizeof(a));
    FILE *f = fopen(a, "wb");
    if (f) {
        fputs(txt, f);
        fclose(f);
    }
}
static bool content_is(const char *rel, const char *txt)
{
    char a[256], buf[128] = {0};
    path(rel, a, sizeof(a));
    FILE *f = fopen(a, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return !strcmp(buf, txt);
}

void test_fsops(void)
{
    snprintf(P, sizeof(P), "/tmp/flexfsops.XXXXXX");
    CHECK(mkdtemp(P) != NULL);
    snprintf(B, sizeof(B), "%s/raiz", P);
    CHECK(mkdir(B, 0775) == 0);
    char out[160];

    // ---- fichero nuevo: primer nombre libre, creado vacio, la carpeta se crea si falta
    CHECK(flex_fsx_new_file(B, "/Notas", "Sin titulo", ".txt", out, sizeof(out)) == FLEX_FSX_OK);
    CHECK(!strcmp(out, "/Notas/Sin titulo 1.txt") && has(out) && content_is(out, ""));
    CHECK(flex_fsx_new_file(B, "/Notas", "Sin titulo", ".txt", out, sizeof(out)) == FLEX_FSX_OK && !strcmp(out, "/Notas/Sin titulo 2.txt"));
    CHECK(flex_fsx_delete(B, "/Notas/Sin titulo 1.txt") == FLEX_FSX_OK);
    CHECK(flex_fsx_new_file(B, "/Notas", "Sin titulo", ".txt", out, sizeof(out)) == FLEX_FSX_OK && !strcmp(out, "/Notas/Sin titulo 1.txt"));   // reutiliza el hueco
    CHECK(flex_fsx_new_file(B, "/Notas", "con/barra", ".txt", out, sizeof(out)) == FLEX_FSX_ARG);
    CHECK(flex_fsx_new_file(B, "/Notas", "x", ".txt", out, 8) == FLEX_FSX_ARG);   // la ruta no cabe en el llamante: nada creado
    CHECK(!has("/Notas/x 1.txt"));

    // ---- papelera: la ruta viaja en el nombre, el contenido se conserva, las colisiones se numeran
    mk("/Paint");
    put("/Paint/Dibujo 1.fxp", "AAA");
    CHECK(flex_fsx_trash(B, "/Paint/Dibujo 1.fxp") == FLEX_FSX_OK);
    CHECK(!has("/Paint/Dibujo 1.fxp") && content_is("/Papelera/@Paint@Dibujo 1.fxp", "AAA"));
    put("/Paint/Dibujo 1.fxp", "BBB");
    CHECK(flex_fsx_trash(B, "/Paint/Dibujo 1.fxp") == FLEX_FSX_OK);
    CHECK(content_is("/Papelera/@Paint@Dibujo 1.fxp", "AAA") && content_is("/Papelera/@Paint@Dibujo 1.fxp(2)", "BBB"));   // el primero no se pierde
    CHECK(flex_fsx_trash(B, "/Paint/no existe.fxp") == FLEX_FSX_NOTFOUND);
    CHECK(flex_fsx_trash(B, "/Papelera/@Paint@Dibujo 1.fxp") == FLEX_FSX_ARG);   // la papelera no se tira a si misma
    CHECK(flex_fsx_trash(B, "/Papelera") == FLEX_FSX_ARG && flex_fsx_trash(B, "/") == FLEX_FSX_ARG);

    // ---- restaurar: a su carpeta; sin pisar; recreando la carpeta borrada
    CHECK(flex_fsx_restore(B, "@Paint@Dibujo 1.fxp") == FLEX_FSX_OK && content_is("/Paint/Dibujo 1.fxp", "AAA"));
    put("/Paint/otro.fxp", "OTRO");
    put("/Notas/Con un nombre.txt", "N");
    CHECK(flex_fsx_trash(B, "/Notas/Con un nombre.txt") == FLEX_FSX_OK);
    put("/Notas/Con un nombre.txt", "NUEVA");   // ya hay otra con el mismo nombre
    CHECK(flex_fsx_restore(B, "@Notas@Con un nombre.txt") == FLEX_FSX_OK);
    CHECK(content_is("/Notas/Con un nombre.txt", "NUEVA") && content_is("/Notas/Con un nombre (2).txt", "N"));   // no pisa la nueva
    put("/Paint/borrar.fxp", "X");
    CHECK(flex_fsx_trash(B, "/Paint/borrar.fxp") == FLEX_FSX_OK);
    CHECK(flex_fsx_delete(B, "/Paint/otro.fxp") == FLEX_FSX_OK && flex_fsx_delete(B, "/Paint/Dibujo 1.fxp") == FLEX_FSX_OK);
    CHECK(flex_fsx_delete(B, "/Paint") == FLEX_FSX_OK && !has("/Paint"));   // carpeta borrada mientras el fichero estaba en la papelera
    CHECK(flex_fsx_restore(B, "@Paint@borrar.fxp") == FLEX_FSX_OK && content_is("/Paint/borrar.fxp", "X"));   // se recrea
    CHECK(flex_fsx_restore(B, "@Paint@borrar.fxp") == FLEX_FSX_NOTFOUND);   // ya no esta en la papelera
    CHECK(flex_fsx_restore(B, "") == FLEX_FSX_ARG);

    // ---- carpetas enteras y arboles
    mk("/Documentos");
    mk("/Documentos/Fotos");
    put("/Documentos/Fotos/a.jpg", "J");
    put("/Documentos/leeme.txt", "L");
    CHECK(flex_fsx_trash(B, "/Documentos") == FLEX_FSX_OK && !has("/Documentos") && is_dir_("/Papelera/@Documentos"));
    CHECK(content_is("/Papelera/@Documentos/Fotos/a.jpg", "J"));   // se mueve entera
    CHECK(flex_fsx_restore(B, "@Documentos") == FLEX_FSX_OK && content_is("/Documentos/Fotos/a.jpg", "J") && content_is("/Documentos/leeme.txt", "L"));
    CHECK(flex_fsx_delete(B, "/Documentos") == FLEX_FSX_OK && !has("/Documentos"));   // recursivo
    CHECK(flex_fsx_delete(B, "/Documentos") == FLEX_FSX_NOTFOUND);
    CHECK(flex_fsx_delete(B, "/") == FLEX_FSX_ARG && flex_fsx_delete(B, "") == FLEX_FSX_ARG && flex_fsx_delete(B, "/a/../..") == FLEX_FSX_ARG);

    // ---- renombrar: solo el nombre, conserva la extension, no pisa
    put("/Notas/a.txt", "A");
    put("/Notas/b.txt", "B");
    CHECK(flex_fsx_rename(B, "/Notas/a.txt", "nueva") == FLEX_FSX_OK && content_is("/Notas/nueva.txt", "A") && !has("/Notas/a.txt"));
    CHECK(flex_fsx_rename(B, "/Notas/nueva.txt", "b.txt") == FLEX_FSX_EXISTS);   // ni se pisa ni se pierde
    CHECK(content_is("/Notas/b.txt", "B") && content_is("/Notas/nueva.txt", "A"));
    CHECK(flex_fsx_rename(B, "/Notas/nueva.txt", "nueva.txt") == FLEX_FSX_OK);   // mismo nombre: nada
    CHECK(flex_fsx_rename(B, "/Notas/nueva.txt", "con/barra") == FLEX_FSX_ARG && flex_fsx_rename(B, "/Notas/nueva.txt", "") == FLEX_FSX_ARG);
    CHECK(flex_fsx_rename(B, "/Notas/nueva.txt", "doc.md") == FLEX_FSX_OK && content_is("/Notas/doc.md", "A"));   // con extension propia se respeta
    CHECK(flex_fsx_rename(B, "/Notas/no existe.txt", "x") == FLEX_FSX_NOTFOUND);

    // ---- vaciar la papelera: todo, y la carpeta se queda
    CHECK(flex_fsx_trash(B, "/Notas/b.txt") == FLEX_FSX_OK && flex_fsx_trash(B, "/Notas/doc.md") == FLEX_FSX_OK);
    CHECK(flex_fsx_empty_trash(B) == FLEX_FSX_OK && is_dir_("/Papelera"));
    {
        char a[256];
        path("/Papelera", a, sizeof(a));
        bool f = false;
        CHECK(flex_fs_wipe_tree(a, &f) == 0 && !f);   // no queda nada
    }
    CHECK(has("/Notas/Con un nombre.txt"));   // lo que no estaba en la papelera, intacto

    // ---- nombres con '@' y '%' vuelven a su sitio (Arduino creaba una carpeta "a")
    put("/Notas/a@b%c.txt", "E");
    CHECK(flex_fsx_trash(B, "/Notas/a@b%c.txt") == FLEX_FSX_OK && content_is("/Papelera/@Notas@a%40b%25c.txt", "E"));
    CHECK(flex_fsx_restore(B, "@Notas@a%40b%25c.txt") == FLEX_FSX_OK && content_is("/Notas/a@b%c.txt", "E") && !has("/Notas/a"));

    // ---- una entrada de la papelera no puede sacar el fichero de la raiz
    put("/Papelera/@..@evil.txt", "MALO");
    CHECK(flex_fsx_restore(B, "@..@evil.txt") == FLEX_FSX_ARG);
    CHECK(has("/Papelera/@..@evil.txt"));
    {
        char up[256];
        snprintf(up, sizeof(up), "%s/evil.txt", P);   // donde caeria "/../evil.txt" desde la raiz
        CHECK(access(up, F_OK) != 0);
    }
    put("/Papelera/@Papelera@x.txt", "Y");   // tampoco hacia dentro de la propia papelera
    CHECK(flex_fsx_restore(B, "@Papelera@x.txt") == FLEX_FSX_ARG);

    // ---- un nombre que no cabe no se trunca: el fichero se queda donde esta
    {
        char longp[160];
        snprintf(longp, sizeof(longp), "/Notas/%s.txt", "n123456789012345678901234567890123456789");
        put(longp, "L");
        CHECK(flex_fsx_trash(B, longp) == FLEX_FSX_ARG && has(longp));
    }

    // ---- estadistica de carpeta: entradas directas y bytes de todo el arbol
    {
        uint32_t n = 99, by = 99;
        mk("/Stat");
        mk("/Stat/sub");
        put("/Stat/a.txt", "12345");
        put("/Stat/sub/b.txt", "1234567");
        CHECK(flex_fsx_dirstat(B, "/Stat", &n, &by) == FLEX_FSX_OK && n == 2 && by == 12);
        CHECK(flex_fsx_dirstat(B, "/no", &n, &by) == FLEX_FSX_NOTFOUND && flex_fsx_dirstat(B, "/a/../..", &n, &by) == FLEX_FSX_ARG);
        CHECK(flex_fsx_dirstat(B, "/Papelera", &n, &by) == FLEX_FSX_OK && n == 2 && by == 4 + 1);   // lo que dejaron los casos de arriba
    }

    // ---- limpieza
    {
        bool f = false;
        flex_fs_wipe_tree(P, &f);
        CHECK(!f && rmdir(P) == 0);
    }
    printf("operaciones de archivo del kit: borrar, renombrar, papelera, restaurar, vaciar y nuevo fichero sobre disco real\n");
}
