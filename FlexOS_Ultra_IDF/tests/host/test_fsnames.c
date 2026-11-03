// Nombres del kit de archivos (docs/spec/03 §5.4, FS.cpp:430-568) frente a Arduino sobre un LittleFS simulado,
// y las diferencias deliberadas (sin truncar en silencio, '@' y '%' escapados, sin reutilizar el ultimo sufijo).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_fs_names.h"
#include "flex_test.h"

void ref_fsn_reset(void);
void ref_fsn_add(const char *p, int dir);
int ref_fsn_has(const char *p);
int ref_fsn_dump(char *out, int cap);
const char *ref_fsn_last_dst(void);
void ref_fsn_move(const char *a, const char *b);
int ref_fsn_trash(const char *p);
int ref_fsn_restore(const char *t);
int ref_fsn_rename(const char *p, const char *n);
int ref_fsn_newname(const char *d, const char *b, const char *e, char *out);
int ref_fsn_origin(const char *t, char *out);
void ref_fsn_stem(const char *n, char *out);
void ref_fsn_fmt(unsigned b, char *out);

// ---- sistema de ficheros del lado del firmware (mismas reglas que el doble de Arduino) -----------
typedef struct {
    char p[160];
    bool dir;
} ent_t;
static ent_t s_fs[512];
static int s_n;

static int find(const char *p)
{
    for (int i = 0; i < s_n; i++) {
        if (!strcmp(s_fs[i].p, p)) return i;
    }
    return -1;
}
static bool ex(const char *p, void *c)
{
    (void)c;
    return find(p) >= 0;
}
static void add(const char *p, bool dir)
{
    if (find(p) < 0 && s_n < 512) {
        snprintf(s_fs[s_n].p, sizeof(s_fs[s_n].p), "%s", p);
        s_fs[s_n++].dir = dir;
    }
}
static bool mv(const char *a, const char *b)
{
    int i = find(a);
    if (i < 0 || find(b) >= 0) return false;
    snprintf(s_fs[i].p, sizeof(s_fs[i].p), "%s", b);
    return true;
}
static int cmp_ent(const void *a, const void *b) { return strcmp(((const ent_t *)a)->p, ((const ent_t *)b)->p); }
static void dump(char *out, int cap)
{
    ent_t t[512];
    memcpy(t, s_fs, sizeof(ent_t) * (size_t)s_n);
    qsort(t, (size_t)s_n, sizeof(ent_t), cmp_ent);
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < s_n; i++) n += snprintf(out + n, (size_t)(cap - n), "%s%s\n", t[i].p, t[i].dir ? "/" : "");
}

static uint32_t s_seed = 424242;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}
static const char *const DIRS[] = {"/", "/Notas", "/Paint", "/Documentos", "/Documentos/Fotos"};
static const char *const NAMES[] = {"a.txt", "Sin titulo 1.txt", "Dibujo 1.fxp", "foto", "x.y.z", "nota (2).txt", "z", ".oculto", "v1.2", "Ab cd.TXT"};

static void rand_path(char *out, size_t n)
{
    const char *d = DIRS[rnd() % 5];
    const char *nm = NAMES[rnd() % 10];
    snprintf(out, n, "%s%s%s", d, strcmp(d, "/") ? "/" : "", nm);
}

// ---- operaciones del firmware sobre el doble --------------------------------------------------
static bool fw_trash(const char *path)
{
    char dst[160];
    if (!strncmp(path, "/Papelera", 9) || !ex(path, NULL)) return false;
    add("/Papelera", true);
    if (!flex_fsn_trash_dst(path, ex, NULL, dst, sizeof(dst))) return false;
    return mv(path, dst);
}
// Arduino (FS.cpp:511-520) guarda extOf(baseName(dst)) como PUNTERO dentro de dst y el bucle de
// sufijos lo sobrescribe: desde la 3.a colision el nombre sale "x (3) (2).txt". El firmware sigue el
// spec ("<stem> (k)<ext>"). Aqui se detecta cuando se llega a k = 3.
static bool restore_needs3(const char *name)
{
    char org[160], dir[160], stem[64], alt[160], nm[128];
    if (!flex_fsn_trash_origin(name, org, sizeof(org)) || !ex(org, NULL)) return false;
    flex_fsn_parent(org, dir, sizeof(dir));
    flex_fsn_stem(flex_fsn_base(org), stem, sizeof(stem));
    snprintf(nm, sizeof(nm), "%s (2)%s", stem, flex_fsn_ext(flex_fsn_base(org)));
    flex_fsn_join(dir, nm, alt, sizeof(alt));
    return ex(alt, NULL);
}
static char s_last_restore[160];
static bool fw_restore(const char *name)
{
    char src[160], dst[160], dir[160];
    if (!name || !name[0]) return false;
    flex_fsn_join("/Papelera", flex_fsn_base(name), src, sizeof(src));
    if (!ex(src, NULL)) return false;
    if (!flex_fsn_restore_dst(name, ex, NULL, dst, sizeof(dst))) return false;
    flex_fsn_parent(dst, dir, sizeof(dir));
    if (strcmp(dir, "/")) add(dir, true);
    snprintf(s_last_restore, sizeof(s_last_restore), "%s", dst);
    return mv(src, dst);
}
static bool fw_rename(const char *path, const char *nm)
{
    char full[160];
    int t = flex_fsn_rename_target(path, nm, full, sizeof(full));
    if (t < 0) return false;
    if (t == 0) return true;
    if (ex(full, NULL)) return false;
    return mv(path, full);
}

void test_fsnames(void)
{
    char a[8192], b[8192];

    // ---- ayudas sueltas
    int bad = 0;
    static const char *const SN[] = {"", "a", "a.txt", "dir/a.b.c", "/x/.oculto", "sin ext", "z.", "/Notas/Sin titulo 3.txt"};
    for (unsigned i = 0; i < sizeof(SN) / sizeof(SN[0]); i++) {
        char r[64], m[64];
        ref_fsn_stem(SN[i], r);
        flex_fsn_stem(SN[i], m, sizeof(m));
        bad += strcmp(r, m) != 0;
    }
    for (unsigned v = 0; v < 3000000; v += (v < 4096 ? 1 : 977)) {
        char r[24], m[24];
        ref_fsn_fmt(v, r);
        flex_fsn_fmt_size(v, m, sizeof(m));
        bad += strcmp(r, m) != 0;
    }
    CHECK(bad == 0);

    // ---- secuencias aleatorias de papelera / restaurar / renombrar / nombre nuevo, en el dominio de Arduino
    int diffs = 0, ops = 0, okays = 0;
    char lastop[260] = "";
    int defect3 = 0;
    for (int seq = 0; seq < 400; seq++) {
        ref_fsn_reset();
        s_n = 0;
        for (int d = 1; d < 5; d++) {   // las carpetas existen
            ref_fsn_add(DIRS[d], 1);
            add(DIRS[d], true);
        }
        for (int step = 0; step < 60; step++) {
            char p[160], nm2[64], r1[160], r2[160];
            int op = (int)(rnd() % 6);
            bool rb = false, mb = false;
            if (op == 0) {   // crear un fichero (como lo haria una app)
                rand_path(p, sizeof(p));
                ref_fsn_add(p, 0);
                add(p, false);
                continue;
            }
            // elegir una ruta existente (o una cualquiera)
            if (s_n > 0 && rnd() % 4) snprintf(p, sizeof(p), "%s", s_fs[rnd() % (uint32_t)s_n].p);
            else rand_path(p, sizeof(p));
            ops++;
            snprintf(lastop, sizeof(lastop), "op=%d p=%.200s", op, p);
            if (op == 1 || op == 2) {   // papelera
                if (find(p) < 0) continue;   // Arduino crea /Papelera antes de mirar; el firmware no toca nada
                rb = ref_fsn_trash(p);
                mb = fw_trash(p);
            } else if (op == 3) {   // restaurar una entrada de la papelera (o un nombre cualquiera)
                const char *nm = NULL;
                for (int i = 0, k = (int)(rnd() % 8); i < s_n; i++) {
                    if (!strncmp(s_fs[i].p, "/Papelera/", 10) && k-- <= 0) {
                        nm = s_fs[i].p;
                        break;
                    }
                }
                if (!nm) continue;
                snprintf(lastop, sizeof(lastop), "restore %.200s", nm);
                bool need3 = restore_needs3(nm);
                rb = ref_fsn_restore(nm);
                mb = fw_restore(nm);
                if (need3 && rb && mb) {   // defecto de Arduino: el nombre sale mal; se alinea el doble para seguir
                    CHECK(strcmp(ref_fsn_last_dst(), s_last_restore) != 0 && strstr(ref_fsn_last_dst(), ") (2)") != NULL);
                    ref_fsn_move(ref_fsn_last_dst(), s_last_restore);
                    defect3++;
                }
            } else if (op == 4) {   // renombrar
                static const char *const RN[] = {"nuevo", "nuevo.md", "a.txt", "otro nombre", "", "con/barra", "Sin titulo 1", "z.txt", "x.y.z"};
                snprintf(nm2, sizeof(nm2), "%s", RN[rnd() % 9]);
                rb = ref_fsn_rename(p, nm2);
                mb = fw_rename(p, nm2);
            } else {   // nombre nuevo + crear
                const char *d = DIRS[rnd() % 2 ? 1 : 0];
                const char *e = (rnd() % 3) ? ".txt" : "";
                rb = ref_fsn_newname(d, "Sin titulo", e, r1);
                mb = flex_fsn_new_name(d, "Sin titulo", e, ex, NULL, r2, sizeof(r2));
                if (rb && mb && strcmp(r1, r2)) diffs++;
                if (rb && mb) {
                    ref_fsn_add(r1, 0);
                    add(r2, false);
                }
            }
            if (rb != mb) diffs++;
            okays += rb;
            ref_fsn_dump(a, sizeof(a));
            dump(b, sizeof(b));
            if (strcmp(a, b)) {
                diffs++;
                if (diffs < 3) printf("%s\n--- ref:\n%s--- fw:\n%s", lastop, a, b);
                break;
            }
        }
    }
    CHECK(diffs == 0);
    CHECK(defect3 > 0);   // la secuencia llego a la 3.a colision y reprodujo el defecto de Arduino
    CHECK(okays > 2000);   // las secuencias llegaron a hacer operaciones reales
    printf("nombres del kit de archivos: papelera, restaurar, renombrar y nombre nuevo iguales a Arduino (%d operaciones, %d con efecto)\n", ops, okays);

    // ---- tercera colision al restaurar, a mano: Arduino "a (3) (2).txt", firmware "a (3).txt"
    {
        ref_fsn_reset();
        s_n = 0;
        static const char *const F[] = {"/Notas/a.txt", "/Notas/a (2).txt", "/Papelera/@Notas@a.txt"};
        ref_fsn_add("/Notas", 1);
        add("/Notas", true);
        for (int i = 0; i < 3; i++) {
            ref_fsn_add(F[i], 0);
            add(F[i], false);
        }
        CHECK(ref_fsn_restore("@Notas@a.txt") && fw_restore("@Notas@a.txt"));
        CHECK(!strcmp(ref_fsn_last_dst(), "/Notas/a (3) (2).txt"));   // el defecto, reproducido en la referencia
        CHECK(!strcmp(s_last_restore, "/Notas/a (3).txt"));           // lo que dice el spec
    }

    // ---- ruta original: igual a Arduino para lo que Arduino sabia restaurar
    int ob = 0;
    static const char AL[] = "ab@. (2)/";
    for (int i = 0; i < 20000; i++) {
        char t[40];
        int l = 1 + (int)(rnd() % 12);
        for (int k = 0; k < l; k++) t[k] = AL[rnd() % (sizeof(AL) - 1)];
        t[l] = 0;
        if (strchr(t, '/')) continue;   // son nombres de entrada: sin barras
        char r[96], m[96];
        bool rb = ref_fsn_origin(t, r), mb = flex_fsn_trash_origin(t, m, sizeof(m));
        if (rb && t[l - 1] == '@') {   // "@Notas@" es una carpeta, no una entrada: Arduino la daba por buena
            CHECK(!mb);
            continue;
        }
        ob += rb != mb || (rb && strcmp(r, m));
    }
    CHECK(ob == 0);

    // ---- escapes: '@' y '%' del nombre original viajan y vuelven (Arduino: "a@b" -> carpeta "a")
    {
        char enc[64], org[96];
        CHECK(flex_fsn_trash_encode("/Notas/a@b%c.txt", enc, sizeof(enc)) && !strcmp(enc, "@Notas@a%40b%25c.txt"));
        CHECK(flex_fsn_trash_origin(enc, org, sizeof(org)) && !strcmp(org, "/Notas/a@b%c.txt"));
        ref_fsn_origin("@Notas@a@b.txt", org);
        CHECK(!strcmp(org, "/Notas/a/b.txt"));   // lo que hacia Arduino (la prueba documenta el defecto)
        int rt = 0;
        static const char AL2[] = "ab@%/. 40250";
        for (int i = 0; i < 30000; i++) {
            char p[48] = "/";
            int l = 1 + (int)(rnd() % 14);
            for (int k = 0; k < l; k++) p[1 + k] = AL2[rnd() % (sizeof(AL2) - 1)];
            p[1 + l] = 0;
            if (p[l] == '/') continue;
            if (!flex_fsn_trash_encode(p, enc, sizeof(enc))) continue;
            rt += !(flex_fsn_trash_origin(enc, org, sizeof(org)) && !strcmp(org, p));
        }
        CHECK(rt == 0);
    }

    // ---- nada se trunca en silencio
    {
        char p[160], enc[64], dst[160];
        // 47 bytes codificados caben; 48 no
        memset(p, 'a', 47);
        p[0] = '/';
        p[47] = 0;
        CHECK(flex_fsn_trash_encode(p, enc, sizeof(enc)) && strlen(enc) == 47);
        p[47] = 'b';
        p[48] = 0;
        CHECK(!flex_fsn_trash_encode(p, enc, sizeof(enc)));
        // con colision, el sufijo "(k)" tampoco cabe: falla en vez de pisar la entrada vieja
        memset(p, 'a', 44);
        p[0] = '/';
        p[44] = 0;   // 44 bytes + "(2)" = 47: cabe
        s_n = 0;
        add("/Papelera", true);
        CHECK(flex_fsn_trash_dst(p, ex, NULL, dst, sizeof(dst)));
        add(dst, false);
        CHECK(flex_fsn_trash_dst(p, ex, NULL, dst, sizeof(dst)) && strstr(dst, "(2)"));
        p[44] = 'c';
        p[45] = 0;   // 45 + "(2)" = 48: no cabe
        flex_fsn_trash_encode(p, enc, sizeof(enc));
        char full[160];
        flex_fsn_join("/Papelera", enc, full, sizeof(full));
        add(full, false);
        CHECK(!flex_fsn_trash_dst(p, ex, NULL, dst, sizeof(dst)));
        // renombrar a un nombre que no cabe en la lista: se rechaza (Arduino recortaba la extension)
        char nm[80];
        memset(nm, 'n', 48);
        nm[48] = 0;
        CHECK(flex_fsn_rename_target("/Notas/a.txt", nm, full, sizeof(full)) == -1);
        nm[47] = 0;
        CHECK(flex_fsn_rename_target("/Notas/a.txt", nm, full, sizeof(full)) == -1);   // + ".txt" = 51
        nm[40] = 0;
        CHECK(flex_fsn_rename_target("/Notas/a.txt", nm, full, sizeof(full)) == 1);
    }

    // ---- agotados los sufijos: falla en vez de reutilizar un nombre que existe
    {
        char dst[160], enc[64];
        s_n = 0;
        add("/Papelera", true);
        flex_fsn_trash_encode("/Notas/a.txt", enc, sizeof(enc));
        char nm[160];
        snprintf(nm, sizeof(nm), "/Papelera/%s", enc);
        add(nm, false);
        for (unsigned k = 2; k <= 99; k++) {
            snprintf(nm, sizeof(nm), "/Papelera/%s(%u)", enc, k);
            add(nm, false);
        }
        CHECK(!flex_fsn_trash_dst("/Notas/a.txt", ex, NULL, dst, sizeof(dst)));
        s_n = 0;
        add("/Notas", true);
        add("/Notas/a.txt", false);
        for (unsigned k = 2; k <= 99; k++) {
            snprintf(nm, sizeof(nm), "/Notas/a (%u).txt", k);
            add(nm, false);
        }
        CHECK(!flex_fsn_restore_dst("@Notas@a.txt", ex, NULL, dst, sizeof(dst)));
    }
}
