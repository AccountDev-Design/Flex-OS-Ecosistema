// Referencia: el listado, el conteo, los tamanos por categoria y los ficheros mas grandes (FlexOS_FS.cpp:192-390) de la version Arduino tal
// cual, sobre un LittleFS que es una carpeta del PC (el mismo disco que mira la implementacion de ESP-IDF).
#include "ref_prelude.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <sys/stat.h>

namespace {
static std::string g_root;
static bool fsMounted = true;
// File de Arduino sobre dirent/stat. name() devuelve el nombre suelto (el core 2.x) y baseName() lo tolera con ruta.
class File {
public:
    DIR *d = nullptr; std::string path, nm; bool dir = false; size_t sz = 0; bool ok = false;
    operator bool() const { return ok; }
    bool isDirectory() const { return dir; }
    size_t size() const { return sz; }
    const char *name() const { return nm.c_str(); }
    void close() { if (d) { closedir(d); d = nullptr; } ok = false; }
    File openNextFile()
    {
        File f;
        if (!d) return f;
        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            std::string p = path + (path == "/" ? "" : "/") + e->d_name;
            struct stat st;
            if (stat((g_root + p).c_str(), &st) != 0) continue;
            f.ok = true; f.path = p; f.nm = e->d_name; f.dir = S_ISDIR(st.st_mode); f.sz = (size_t)st.st_size;
            return f;
        }
        return f;
    }
};
static struct LfsScanT {
    File open(const char *p)
    {
        File f;
        struct stat st;
        std::string full = g_root + p;
        if (stat(full.c_str(), &st) != 0) return f;
        f.ok = true; f.path = p; f.dir = S_ISDIR(st.st_mode); f.sz = (size_t)st.st_size;
        const char *s = strrchr(p, '/');
        f.nm = s ? s + 1 : p;
        if (f.dir) f.d = opendir(full.c_str());
        return f;
    }
} LittleFS;
}  // namespace

#define FLEXFS_NAME_MAX 48
#define FLEXFS_PATH_MAX 160   // el de ESP-IDF (FLEX_FS_PATH_MAX): Arduino usa 96; las rutas de la prueba caben en los dos
#define FLEXFS_DIR_PAINT  "/Paint"
#define FLEXFS_DIR_NOTAS  "/Notas"
#define FLEXFS_DIR_SYS    "/System"
#define FLEXFS_DIR_DOCS   "/Documentos"
#define FLEXFS_DIR_TRASH  "/Papelera"
enum { FLEXFS_CAT_DOCS = 0, FLEXFS_CAT_SYS, FLEXFS_CAT_APPS, FLEXFS_CAT_TRASH, FLEXFS_CAT_N };
struct FlexFsEntry { char name[FLEXFS_NAME_MAX]; uint32_t size; bool dir; uint16_t items; };
struct FlexFsBig { char path[FLEXFS_PATH_MAX]; uint32_t size; };
#define FS_MAX_DEPTH 5
#include "ref_fsscan.inc"

extern "C" {
void ref_fsscan_root(const char *root) { g_root = root; }
// Lista como flexFsList: devuelve n; cada entrada "d|f,nombre,size,items" en `out`
int ref_fsscan_list(const char *dir, int maxn, char *out, int cap)
{
    FlexFsEntry e[300];
    int n = flexFsList(dir, e, maxn > 300 ? 300 : maxn);
    int w = 0;
    out[0] = 0;
    for (int i = 0; i < n && w < cap - 80; i++)
        w += snprintf(out + w, (size_t)(cap - w), "%c,%s,%u,%u;", e[i].dir ? 'd' : 'f', e[i].name, (unsigned)e[i].size, (unsigned)e[i].items);
    return n;
}
int ref_fsscan_count(const char *dir) { return flexFsCount(dir); }
unsigned ref_fsscan_dirsize(const char *dir) { return flexFsDirSize(dir); }
unsigned ref_fsscan_cat(int cat) { return flexFsCatSize(cat); }
// Los n mayores: "ruta,size;"
int ref_fsscan_largest(int n, char *out, int cap)
{
    FlexFsBig b[16];
    int k = flexFsLargest(b, n > 16 ? 16 : n);
    int w = 0;
    out[0] = 0;
    for (int i = 0; i < k && w < cap - 200; i++) w += snprintf(out + w, (size_t)(cap - w), "%s,%u;", b[i].path, (unsigned)b[i].size);
    return k;
}
}
