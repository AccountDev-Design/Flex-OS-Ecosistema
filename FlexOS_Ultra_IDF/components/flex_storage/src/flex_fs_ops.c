#include "flex_fs_ops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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
