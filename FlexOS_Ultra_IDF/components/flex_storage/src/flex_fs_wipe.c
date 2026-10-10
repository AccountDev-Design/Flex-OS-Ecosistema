// Flex OS Ultra · vaciar una carpeta de LittleFS (POSIX, con pruebas de host).
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "flex_fs_path.h"

#define WIPE_DEPTH   8
#define WIPE_BATCH   16
#define WIPE_PATH    192
#define WIPE_NAME    64

// Lee hasta WIPE_BATCH nombres (sin "." ni "..") y cierra el directorio.
static int read_batch(const char *dir, char (*names)[WIPE_NAME], bool *failed)
{
    DIR *d = opendir(dir);
    if (!d) {
        if (errno != ENOENT) {
            *failed = true;
        }
        return -1;
    }
    int n = 0;
    struct dirent *e;
    while (n < WIPE_BATCH && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (strlen(e->d_name) >= WIPE_NAME) {
            *failed = true;   // no cabe: no se toca
            continue;
        }
        strcpy(names[n++], e->d_name);
    }
    closedir(d);
    return n;
}

static int wipe(const char *dir, int depth, bool *failed)
{
    char (*names)[WIPE_NAME] = malloc(WIPE_BATCH * WIPE_NAME);
    char *path = malloc(WIPE_PATH);
    if (!names || !path) {
        free(names);
        free(path);
        *failed = true;
        return 0;
    }
    int total = 0;
    for (;;) {
        int n = read_batch(dir, names, failed);
        if (n <= 0) {
            break;
        }
        int gone = 0;
        for (int i = 0; i < n; i++) {
            int len = snprintf(path, WIPE_PATH, "%s/%s", dir, names[i]);
            if (len < 0 || len >= WIPE_PATH) {
                *failed = true;
                continue;
            }
            struct stat st;
            if (stat(path, &st) != 0) {
                *failed = true;
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                if (depth >= WIPE_DEPTH) {
                    *failed = true;
                    continue;
                }
                total += wipe(path, depth + 1, failed);
                if (rmdir(path) == 0) {
                    gone++;
                } else {
                    *failed = true;
                }
            } else if (unlink(path) == 0) {
                total++;
                gone++;
            } else {
                *failed = true;
            }
        }
        if (gone == 0) {
            break;   // nada mas que se pueda borrar: no dar vueltas para siempre
        }
    }
    free(names);
    free(path);
    return total;
}

int flex_fs_wipe_tree(const char *abs_dir, bool *failed)
{
    bool f = false;
    int n = abs_dir && *abs_dir ? wipe(abs_dir, 0, &f) : 0;
    if (failed) {
        *failed = f;
    }
    return n;
}
