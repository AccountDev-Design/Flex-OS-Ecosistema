#include "flex_fs_ops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "flex_fs_names.h"
#include "flex_fs_path.h"

#define ABS_MAX 256
#define PATH_REL_MAX 160

typedef struct {
    const char *base;
} fsx_t;

static bool abs_of(const char *base, const char *rel, char *out, size_t n)
{
    int w = snprintf(out, n, "%s%s", base, rel);
    return w > 0 && (size_t)w < n;
}

static bool rel_ok(const char *rel)
{
    return flex_fs_path_valid(rel, 0, 0, PATH_REL_MAX);
}

static bool exists_cb(const char *rel, void *ctx)
{
    char a[ABS_MAX];
    struct stat st;
    return abs_of(((fsx_t *)ctx)->base, rel, a, sizeof(a)) && stat(a, &st) == 0;
}

static bool is_dir(const char *abs)
{
    struct stat st;
    return stat(abs, &st) == 0 && S_ISDIR(st.st_mode);
}

// mkdir -p de una ruta absoluta.
static bool mkdir_p(const char *abs, size_t base_len)
{
    char tmp[ABS_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s", abs) >= (int)sizeof(tmp)) {
        return false;
    }
    for (char *p = tmp + base_len + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    return mkdir(tmp, 0775) == 0 || (errno == EEXIST && is_dir(tmp));
}

static bool copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    unsigned char buf[256];
    size_t r;
    bool ok = true;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, r, out) != r) {
            ok = false;
            break;
        }
    }
    ok = ok && !ferror(in);
    fclose(in);
    ok = (fclose(out) == 0) && ok;
    if (!ok) {
        unlink(to);   // sin copias a medias
    }
    return ok;
}

// rename y, si el sistema de ficheros no lo admite en un paso, copiar + borrar (moveFile).
static bool move_file(const char *from, const char *to)
{
    if (rename(from, to) == 0) {
        return true;
    }
    return copy_file(from, to) && unlink(from) == 0;
}

flex_fsx_res_t flex_fsx_delete(const char *base, const char *path)
{
    char a[ABS_MAX];
    if (!base || !path || !path[0] || !strcmp(path, "/") || !rel_ok(path) || !abs_of(base, path, a, sizeof(a))) {
        return FLEX_FSX_ARG;
    }
    struct stat st;
    if (stat(a, &st) != 0) {
        return FLEX_FSX_NOTFOUND;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(a) == 0 ? FLEX_FSX_OK : FLEX_FSX_IO;
    }
    bool failed = false;
    flex_fs_wipe_tree(a, &failed);   // rmdir solo borra carpetas vacias
    return (!failed && rmdir(a) == 0) ? FLEX_FSX_OK : FLEX_FSX_IO;
}

flex_fsx_res_t flex_fsx_rename(const char *base, const char *path, const char *name)
{
    char full[PATH_REL_MAX], a[ABS_MAX], b[ABS_MAX];
    fsx_t c = {base};
    if (!base || !path || !rel_ok(path)) {
        return FLEX_FSX_ARG;
    }
    int t = flex_fsn_rename_target(path, name, full, sizeof(full));
    if (t < 0 || !rel_ok(full)) {
        return FLEX_FSX_ARG;
    }
    if (t == 0) {
        return FLEX_FSX_OK;   // mismo nombre: nada que hacer (flexFsRename devuelve true)
    }
    if (!abs_of(base, path, a, sizeof(a)) || !abs_of(base, full, b, sizeof(b))) {
        return FLEX_FSX_ARG;
    }
    if (!exists_cb(path, &c)) {
        return FLEX_FSX_NOTFOUND;
    }
    if (exists_cb(full, &c)) {
        return FLEX_FSX_EXISTS;   // no se pisa nada en silencio
    }
    return rename(a, b) == 0 ? FLEX_FSX_OK : FLEX_FSX_IO;
}

static bool in_trash(const char *path)
{
    size_t l = strlen(FLEX_FSN_DIR_TRASH);
    return !strncmp(path, FLEX_FSN_DIR_TRASH, l) && (path[l] == '\0' || path[l] == '/');
}

flex_fsx_res_t flex_fsx_trash(const char *base, const char *path)
{
    char dst[PATH_REL_MAX], a[ABS_MAX], b[ABS_MAX], td[ABS_MAX];
    fsx_t c = {base};
    if (!base || !path || !path[0] || !strcmp(path, "/") || !rel_ok(path) || in_trash(path)) {
        return FLEX_FSX_ARG;   // ni la raiz ni la papelera se tiran
    }
    if (!abs_of(base, path, a, sizeof(a)) || !abs_of(base, FLEX_FSN_DIR_TRASH, td, sizeof(td))) {
        return FLEX_FSX_ARG;
    }
    if (!exists_cb(path, &c)) {
        return FLEX_FSX_NOTFOUND;
    }
    if (!flex_fsn_trash_dst(path, exists_cb, &c, dst, sizeof(dst))) {
        return FLEX_FSX_ARG;   // el nombre codificado no cabe: se queda donde esta
    }
    if (!abs_of(base, dst, b, sizeof(b)) || !mkdir_p(td, strlen(base))) {
        return FLEX_FSX_IO;
    }
    if (is_dir(a)) {
        return rename(a, b) == 0 ? FLEX_FSX_OK : FLEX_FSX_IO;   // carpeta entera
    }
    return move_file(a, b) ? FLEX_FSX_OK : FLEX_FSX_IO;
}

flex_fsx_res_t flex_fsx_restore(const char *base, const char *trash_name)
{
    char src[PATH_REL_MAX], dst[PATH_REL_MAX], dir[PATH_REL_MAX], a[ABS_MAX], b[ABS_MAX], da[ABS_MAX];
    fsx_t c = {base};
    if (!base || !trash_name || !trash_name[0]) {
        return FLEX_FSX_ARG;
    }
    if (!flex_fsn_join(FLEX_FSN_DIR_TRASH, flex_fsn_base(trash_name), src, sizeof(src))) {
        return FLEX_FSX_ARG;
    }
    if (!exists_cb(src, &c)) {
        return FLEX_FSX_NOTFOUND;
    }
    // El destino sale del nombre: debe ser una ruta valida (un "@..@x" no puede salir de la raiz).
    if (!flex_fsn_restore_dst(trash_name, exists_cb, &c, dst, sizeof(dst)) || !rel_ok(dst) || in_trash(dst)) {
        return FLEX_FSX_ARG;
    }
    flex_fsn_parent(dst, dir, sizeof(dir));
    if (strcmp(dir, "/") != 0) {   // la carpeta pudo borrarse mientras estaba en la papelera
        if (!abs_of(base, dir, da, sizeof(da)) || !mkdir_p(da, strlen(base))) {
            return FLEX_FSX_IO;
        }
    }
    if (!abs_of(base, src, a, sizeof(a)) || !abs_of(base, dst, b, sizeof(b))) {
        return FLEX_FSX_ARG;
    }
    if (is_dir(a)) {
        return rename(a, b) == 0 ? FLEX_FSX_OK : FLEX_FSX_IO;
    }
    return move_file(a, b) ? FLEX_FSX_OK : FLEX_FSX_IO;
}

flex_fsx_res_t flex_fsx_empty_trash(const char *base)
{
    char td[ABS_MAX];
    if (!base || !abs_of(base, FLEX_FSN_DIR_TRASH, td, sizeof(td))) {
        return FLEX_FSX_ARG;
    }
    if (!is_dir(td)) {
        return FLEX_FSX_NOTFOUND;
    }
    bool failed = false;
    flex_fs_wipe_tree(td, &failed);   // vacia por tandas y deja la carpeta
    return failed ? FLEX_FSX_IO : FLEX_FSX_OK;
}

flex_fsx_res_t flex_fsx_new_file(const char *base, const char *dir, const char *name, const char *ext,
                                 char *out, size_t n)
{
    char rel[PATH_REL_MAX], a[ABS_MAX], da[ABS_MAX];
    fsx_t c = {base};
    if (!base || !dir || !name || !out || n == 0 || dir[0] != '/' || !rel_ok(dir) || strchr(name, '/') ||
        (ext && strchr(ext, '/'))) {
        return FLEX_FSX_ARG;
    }
    if (!flex_fsn_new_name(dir, name, ext, exists_cb, &c, rel, sizeof(rel)) || !rel_ok(rel)) {
        return FLEX_FSX_ARG;
    }
    if (strcmp(dir, "/") != 0) {
        if (!abs_of(base, dir, da, sizeof(da)) || !mkdir_p(da, strlen(base))) {
            return FLEX_FSX_IO;
        }
    }
    if (strlen(rel) >= n || !abs_of(base, rel, a, sizeof(a))) {
        return FLEX_FSX_ARG;
    }
    int fd = open(a, O_WRONLY | O_CREAT | O_EXCL, 0664);   // EXCL: nunca pisa
    if (fd < 0) {
        return errno == EEXIST ? FLEX_FSX_EXISTS : FLEX_FSX_IO;
    }
    if (close(fd) != 0) {
        unlink(a);
        return FLEX_FSX_IO;
    }
    strcpy(out, rel);
    return FLEX_FSX_OK;
}

#define STAT_DEPTH 5

static uint64_t dir_bytes(const char *abs, int depth)
{
    if (depth > STAT_DEPTH) {
        return 0;
    }
    DIR *d = opendir(abs);
    if (!d) {
        return 0;
    }
    uint64_t total = 0;
    struct dirent *e;
    char sub[ABS_MAX];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        struct stat st;
        if (snprintf(sub, sizeof(sub), "%s/%s", abs, e->d_name) >= (int)sizeof(sub) || stat(sub, &st) != 0) {
            continue;
        }
        total += S_ISDIR(st.st_mode) ? dir_bytes(sub, depth + 1) : (uint64_t)st.st_size;
    }
    closedir(d);
    return total;
}

// Elementos directos de una carpeta (flexFsCount)
static uint32_t dir_count(const char *abs)
{
    DIR *d = opendir(abs);
    if (!d) {
        return 0;
    }
    uint32_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        n += strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    }
    closedir(d);
    return n;
}

static int cmp_ent(const void *pa, const void *pb)
{
    const flex_fsx_ent_t *a = pa, *b = pb;
    if (a->dir != b->dir) {
        return a->dir ? -1 : 1;
    }
    int c = strcasecmp(a->name, b->name);
    return c ? c : strcmp(a->name, b->name);   // determinista si solo difieren en mayusculas
}

flex_fsx_res_t flex_fsx_list(const char *base, const char *dir, flex_fsx_ent_t *out, size_t max, size_t *n, size_t *total)
{
    char a[ABS_MAX];
    if (!base || !dir || !out || !n || max == 0 || !rel_ok(dir) || !abs_of(base, dir, a, sizeof(a))) {
        return FLEX_FSX_ARG;
    }
    DIR *d = opendir(a);
    if (!d) {
        return FLEX_FSX_NOTFOUND;
    }
    flex_fsx_ent_t *all = malloc(sizeof(*all) * FLEX_FSX_LIST_SCAN);
    if (!all) {
        closedir(d);
        return FLEX_FSX_IO;
    }
    size_t cnt = 0;
    struct dirent *e;
    char sub[ABS_MAX];
    size_t dl = strcmp(dir, "/") ? strlen(dir) : 0;   // la ruta de cada entrada es dir + "/" + nombre
    while (cnt < FLEX_FSX_LIST_SCAN && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || strlen(e->d_name) >= FLEX_FSX_NAME_MAX) {
            continue;
        }
        // Una entrada cuya ruta no cabe en FLEX_FSX_PATH_MAX no se lista: la interfaz la recortaria y borrar, renombrar o entrar en la ruta
        // recortada tocaria OTRO elemento (el que se llame como el principio). Arduino la recortaba en silencio.
        if (dl + 1 + strlen(e->d_name) >= FLEX_FSX_PATH_MAX) {
            continue;
        }
        struct stat st;
        if (snprintf(sub, sizeof(sub), "%s/%s", a, e->d_name) >= (int)sizeof(sub) || stat(sub, &st) != 0) {
            continue;
        }
        flex_fsx_ent_t *o = &all[cnt++];
        strcpy(o->name, e->d_name);
        o->dir = S_ISDIR(st.st_mode);
        o->items = 0;
        if (o->dir) {   // conteo y tamano REALES de la carpeta: se abre y se recorre
            uint32_t k = dir_count(sub);
            o->items = k > 0xFFFE ? 0xFFFE : (uint16_t)k;
            uint64_t b = dir_bytes(sub, 1);
            o->size = b > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)b;
        } else {
            o->size = (uint32_t)st.st_size;
        }
    }
    closedir(d);
    qsort(all, cnt, sizeof(*all), cmp_ent);
    size_t k = cnt < max ? cnt : max;
    memcpy(out, all, k * sizeof(*all));
    free(all);
    *n = k;
    if (total) {
        *total = cnt;
    }
    return FLEX_FSX_OK;
}

static uint32_t sat32(uint64_t v)
{
    return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)v;
}

static uint32_t rel_size(const char *base, const char *rel)
{
    char a[ABS_MAX];
    return abs_of(base, rel, a, sizeof(a)) ? sat32(dir_bytes(a, 0)) : 0;
}

// bigWalk: top-N por insercion sin lista intermedia (RAM constante).
static void big_walk(const char *abs, const char *rel, int depth, const char *skip, flex_fsx_scan_t *s)
{
    if (depth > STAT_DEPTH) {
        return;
    }
    DIR *d = opendir(abs);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        char a2[ABS_MAX], r2[FLEX_FSX_PATH_MAX];
        if (snprintf(a2, sizeof(a2), "%s/%s", abs, e->d_name) >= (int)sizeof(a2) ||
            snprintf(r2, sizeof(r2), "%s/%s", rel, e->d_name) >= (int)sizeof(r2)) {
            continue;
        }
        struct stat st;
        if (stat(a2, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            big_walk(a2, r2, depth + 1, skip, s);
            continue;
        }
        if (skip && skip[0] && strncmp(r2, skip, strlen(skip)) == 0) {
            continue;
        }
        uint32_t sz = (uint32_t)st.st_size;
        size_t pos = s->nbig;
        while (pos > 0 && s->big[pos - 1].size < sz) {
            pos--;
        }
        if (pos < FLEX_FSX_BIG_MAX) {
            size_t last = s->nbig < FLEX_FSX_BIG_MAX ? s->nbig : FLEX_FSX_BIG_MAX - 1;
            for (size_t k = last; k > pos; k--) {
                s->big[k] = s->big[k - 1];
            }
            snprintf(s->big[pos].path, sizeof(s->big[pos].path), "%s", r2);
            s->big[pos].size = sz;
            if (s->nbig < FLEX_FSX_BIG_MAX) {
                s->nbig++;
            }
        }
    }
    closedir(d);
}

flex_fsx_res_t flex_fsx_scan(const char *base, const char *skip_prefix, flex_fsx_scan_t *out)
{
    char a[ABS_MAX];
    if (!base || !out || !abs_of(base, "", a, sizeof(a))) {
        return FLEX_FSX_ARG;
    }
    if (!is_dir(a)) {
        return FLEX_FSX_NOTFOUND;
    }
    memset(out, 0, sizeof(*out));
    out->cat[FLEX_FSX_CAT_DOCS] = sat32((uint64_t)rel_size(base, "/Documentos") + rel_size(base, "/Notas"));
    out->cat[FLEX_FSX_CAT_SYS] = rel_size(base, "/System");
    out->cat[FLEX_FSX_CAT_APPS] = rel_size(base, "/Paint");
    out->cat[FLEX_FSX_CAT_TRASH] = rel_size(base, "/Papelera");
    out->cache = rel_size(base, "/System/Cache");
    big_walk(a, "", 0, skip_prefix, out);
    return FLEX_FSX_OK;
}

flex_fsx_res_t flex_fsx_dirstat(const char *base, const char *dir, uint32_t *count, uint32_t *bytes)
{
    char a[ABS_MAX];
    if (!base || !dir || !count || !bytes || !rel_ok(dir) || !abs_of(base, dir, a, sizeof(a))) {
        return FLEX_FSX_ARG;
    }
    DIR *d = opendir(a);
    if (!d) {
        return FLEX_FSX_NOTFOUND;
    }
    uint32_t n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        n += strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    }
    closedir(d);
    uint64_t b = dir_bytes(a, 0);
    *count = n;
    *bytes = b > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)b;
    return FLEX_FSX_OK;
}

flex_fsx_res_t flex_fsx_read_head(const char *base, const char *path, size_t n, uint8_t **out, size_t *len)
{
    char a[ABS_MAX];
    if (!base || !path || !out || !len || !rel_ok(path) || !abs_of(base, path, a, sizeof(a))) {
        return FLEX_FSX_ARG;
    }
    struct stat st;
    if (stat(a, &st) != 0 || S_ISDIR(st.st_mode)) {
        return FLEX_FSX_NOTFOUND;
    }
    size_t want = (size_t)st.st_size < n ? (size_t)st.st_size : n;
    uint8_t *buf = malloc(want + 1);
    if (!buf) {
        return FLEX_FSX_IO;
    }
    FILE *f = fopen(a, "rb");
    size_t got = f ? fread(buf, 1, want, f) : 0;
    if (f) {
        fclose(f);
    }
    if (!f || got != want) {
        free(buf);
        return FLEX_FSX_IO;
    }
    buf[got] = 0;
    *out = buf;
    *len = got;
    return FLEX_FSX_OK;
}
