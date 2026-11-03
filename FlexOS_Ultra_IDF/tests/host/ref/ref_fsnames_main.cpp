// Referencia: la logica de nombres de FlexOS_FS.cpp (Arduino) tal cual (papelera, restaurar, renombrar,
// nombre nuevo, tamanos) sobre un LittleFS simulado en memoria.
#include "ref_prelude.h"
#include <set>
#include <string>
#include <stdio.h>
#include <string.h>

static bool fsMounted = true;
struct FsMutT {};
typedef FsMutT FsMut;
static std::set<std::string> g_all, g_dirs;
static std::string g_last_dst;
struct LfsT {
    bool exists(const char *p) { return g_all.count(p) != 0; }
    bool rename(const char *a, const char *b)
    {
        if (!g_all.count(a) || g_all.count(b)) return false;
        g_last_dst = b;
        bool d = g_dirs.count(a) != 0;
        g_all.erase(a);
        g_dirs.erase(a);
        g_all.insert(b);
        if (d) g_dirs.insert(b);
        return true;
    }
} LittleFS;
static bool flexFsMkdir(const char *p) { g_all.insert(p); g_dirs.insert(p); return true; }
static bool flexFsIsDir(const char *p) { return g_dirs.count(p) != 0; }
static bool moveFile(const char *a, const char *b) { return LittleFS.rename(a, b); }

#include "ref_fsn.inc"

extern "C" {
void ref_fsn_reset() { g_all.clear(); g_dirs.clear(); }
void ref_fsn_add(const char *p, int dir) { g_all.insert(p); if (dir) g_dirs.insert(p); }
int ref_fsn_has(const char *p) { return g_all.count(p) != 0; }
// Contenido ordenado, una ruta por linea ('/' final = carpeta)
int ref_fsn_dump(char *out, int cap)
{
    int n = 0;
    out[0] = 0;
    for (auto &s : g_all) n += snprintf(out + n, cap - n, "%s%s\n", s.c_str(), g_dirs.count(s) ? "/" : "");
    return n;
}
// Destino de la ultima mudanza que hizo la referencia y correccion manual de ese destino
const char *ref_fsn_last_dst() { return g_last_dst.c_str(); }
void ref_fsn_move(const char *a, const char *b)
{
    bool d = g_dirs.count(a) != 0;
    g_all.erase(a);
    g_dirs.erase(a);
    g_all.insert(b);
    if (d) g_dirs.insert(b);
}
int ref_fsn_trash(const char *p) { return flexFsTrash(p); }
int ref_fsn_restore(const char *t) { return flexFsRestore(t); }
int ref_fsn_rename(const char *p, const char *n) { return flexFsRename(p, n); }
int ref_fsn_newname(const char *d, const char *b, const char *e, char *out) { return flexFsNewName(d, b, e, out, 96); }
int ref_fsn_origin(const char *t, char *out) { return flexFsTrashOrigin(t, out, 96); }
void ref_fsn_stem(const char *n, char *out) { flexFsStem(n, out, 48); }
void ref_fsn_fmt(unsigned b, char *out) { flexFsFmtSize(b, out, 24); }
}
