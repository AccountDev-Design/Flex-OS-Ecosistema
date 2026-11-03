#include "flex_fs_names.h"

#include <stdio.h>
#include <string.h>

const char *flex_fsn_base(const char *path)
{
    if (!path) {
        return "";
    }
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

void flex_fsn_parent(const char *path, char *out, size_t n)
{
    if (!out || n == 0) {
        return;
    }
    const char *s = path ? strrchr(path, '/') : NULL;
    if (!s || s == path) {
        snprintf(out, n, "/");
        return;
    }
    size_t len = (size_t)(s - path);
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

bool flex_fsn_join(const char *dir, const char *name, char *out, size_t n)
{
    if (!out || n == 0 || !name) {
        return false;
    }
    int w = (!dir || !dir[0] || !strcmp(dir, "/")) ? snprintf(out, n, "/%s", name) : snprintf(out, n, "%s/%s", dir, name);
    return w >= 0 && (size_t)w < n;
}

const char *flex_fsn_ext(const char *name)
{
    const char *d = name ? strrchr(name, '.') : NULL;
    return d ? d : "";
}

void flex_fsn_stem(const char *name, char *out, size_t n)
{
    if (!out || n == 0) {
        return;
    }
    const char *nm = flex_fsn_base(name);
    const char *d = strrchr(nm, '.');
    size_t len = d ? (size_t)(d - nm) : strlen(nm);
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, nm, len);
    out[len] = '\0';
}

bool flex_fsn_trash_encode(const char *path, char *out, size_t n)
{
    if (!path || path[0] != '/' || !out || n == 0) {
        return false;
    }
    size_t j = 0;
    for (size_t i = 0; path[i]; i++) {
        const char *rep = NULL;
        char one[2] = {path[i], '\0'};
        if (path[i] == '/') {
            rep = "@";
        } else if (path[i] == '@') {
            rep = "%40";
        } else if (path[i] == '%') {
            rep = "%25";
        } else {
            rep = one;
        }
        size_t rl = strlen(rep);
        if (j + rl >= n || j + rl >= FLEX_FSN_NAME_MAX) {
            return false;   // no cabe: ni se trunca ni se pisa nada
        }
        memcpy(out + j, rep, rl);
        j += rl;
    }
    out[j] = '\0';
    return true;
}

bool flex_fsn_trash_origin(const char *trash_name, char *out, size_t n)
{
    if (!trash_name || !out || n == 0) {
        return false;
    }
    const char *nm = flex_fsn_base(trash_name);
    size_t j = 0;
    for (size_t i = 0; nm[i]; i++) {
        char c = nm[i];
        if (c == '@') {
            c = '/';
        } else if (c == '%' && nm[i + 1] == '4' && nm[i + 2] == '0') {
            c = '@';
            i += 2;
        } else if (c == '%' && nm[i + 1] == '2' && nm[i + 2] == '5') {
            c = '%';
            i += 2;
        }
        if (j + 1 >= n) {
            out[0] = '\0';
            return false;
        }
        out[j++] = c;
    }
    out[j] = '\0';
    return out[0] == '/' && out[j - 1] != '/';   // no una carpeta pelada ("@Notas@")
}

bool flex_fsn_trash_dst(const char *path, flex_fsn_exists_fn ex, void *ctx, char *dst, size_t n)
{
    char enc[FLEX_FSN_NAME_MAX];
    if (!ex || !flex_fsn_trash_encode(path, enc, sizeof(enc)) || !flex_fsn_join(FLEX_FSN_DIR_TRASH, enc, dst, n)) {
        return false;
    }
    if (!ex(dst, ctx)) {
        return true;
    }
    for (int k = 2; k <= FLEX_FSN_ALT_MAX; k++) {   // ya se tiro antes algo igual: se numera, no se pisa
        char alt[2 * FLEX_FSN_NAME_MAX + 8];   // holgado: el tope se mira abajo, no lo recorta snprintf
        int w = snprintf(alt, sizeof(alt), "%s(%d)", enc, k);
        if (w < 0 || w >= FLEX_FSN_NAME_MAX || !flex_fsn_join(FLEX_FSN_DIR_TRASH, alt, dst, n)) {
            return false;
        }
        if (!ex(dst, ctx)) {
            return true;
        }
    }
    return false;
}

bool flex_fsn_restore_dst(const char *trash_name, flex_fsn_exists_fn ex, void *ctx, char *dst, size_t n)
{
    if (!ex || !flex_fsn_trash_origin(trash_name, dst, n)) {
        return false;
    }
    if (!ex(dst, ctx)) {
        return true;
    }
    char dir[160];
    flex_fsn_parent(dst, dir, sizeof(dir));
    char stem[FLEX_FSN_NAME_MAX];
    flex_fsn_stem(flex_fsn_base(dst), stem, sizeof(stem));
    char exb[FLEX_FSN_NAME_MAX];
    snprintf(exb, sizeof(exb), "%.*s", FLEX_FSN_NAME_MAX - 1, flex_fsn_ext(flex_fsn_base(dst)));
    for (int k = 2; k <= FLEX_FSN_ALT_MAX; k++) {   // ya hay otro con ese nombre
        char alt[3 * FLEX_FSN_NAME_MAX + 8];
        int w = snprintf(alt, sizeof(alt), "%s (%d)%s", stem, k, exb);
        if (w < 0 || w >= FLEX_FSN_NAME_MAX || !flex_fsn_join(dir, alt, dst, n)) {
            return false;
        }
        if (!ex(dst, ctx)) {
            return true;
        }
    }
    return false;
}

int flex_fsn_rename_target(const char *path, const char *new_name, char *full, size_t n)
{
    if (!path || !new_name || !new_name[0] || strchr(new_name, '/') || strlen(new_name) >= FLEX_FSN_NAME_MAX) {
        return -1;   // solo nombre, sin rutas, y que quepa en una entrada de lista
    }
    char dir[160];
    flex_fsn_parent(path, dir, sizeof(dir));
    char tmp[3 * FLEX_FSN_NAME_MAX + 8];
    const char *nm = new_name;
    if (!flex_fsn_ext(new_name)[0] && flex_fsn_ext(path)[0]) {   // sin extension: se conserva la original
        int w = snprintf(tmp, sizeof(tmp), "%s%s", new_name, flex_fsn_ext(path));
        if (w < 0 || w >= FLEX_FSN_NAME_MAX) {
            return -1;
        }
        nm = tmp;
    }
    if (!flex_fsn_join(dir, nm, full, n)) {
        return -1;
    }
    return strcmp(full, path) == 0 ? 0 : 1;
}

bool flex_fsn_new_name(const char *dir, const char *base, const char *ext,
                       flex_fsn_exists_fn ex, void *ctx, char *out, size_t n)
{
    if (!ex || !base || !out) {
        return false;
    }
    for (int k = 1; k < 1000; k++) {
        char nm[3 * FLEX_FSN_NAME_MAX + 8];
        int w = snprintf(nm, sizeof(nm), "%s %d%s", base, k, ext ? ext : "");
        if (w < 0 || w >= FLEX_FSN_NAME_MAX) {
            return false;
        }
        char full[160];
        if (!flex_fsn_join(dir, nm, full, sizeof(full))) {
            return false;
        }
        if (!ex(full, ctx)) {
            if (strlen(full) >= n) {
                return false;
            }
            strcpy(out, full);
            return true;
        }
    }
    return false;
}

void flex_fsn_fmt_size(uint32_t b, char *out, size_t n)
{
    if (b >= 1048576u) {
        uint32_t ent = b / 1048576u, dec = (b % 1048576u) * 10u / 1048576u;
        snprintf(out, n, "%lu.%lu MB", (unsigned long)ent, (unsigned long)dec);
    } else if (b >= 1024u) {
        snprintf(out, n, "%lu KB", (unsigned long)(b / 1024u));
    } else {
        snprintf(out, n, "%lu B", (unsigned long)b);
    }
}
