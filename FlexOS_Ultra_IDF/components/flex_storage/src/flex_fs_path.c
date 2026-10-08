#include "flex_fs_path.h"

#include <string.h>

bool flex_fs_path_valid(const char *p, size_t base_len, size_t suffix_len, size_t cap)
{
    if (!p || p[0] != '/') {
        return false;
    }
    size_t n = strlen(p);
    if (n + base_len + suffix_len >= cap) {
        return false;
    }
    // Ningun componente ".." (no se sale de la raiz de LittleFS).
    for (const char *c = p; (c = strstr(c, "..")) != NULL; c += 2) {
        bool starts = c == p || c[-1] == '/';
        bool ends = c[2] == '\0' || c[2] == '/';
        if (starts && ends) {
            return false;
        }
    }
    return true;
}
