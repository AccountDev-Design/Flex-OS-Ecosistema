// Flex OS Ultra · modelo de la Caja de aplicaciones (C portable, sin LVGL).
//
// Que apps entran y en que orden (drwFilter, AppDrawer.h:464): las nativas del
// registro y las descargadas, fuera las ocultas (salvo "ver ocultas"), filtro
// por subcadena sin distinguir mayusculas ASCII (dexMatch) y UN solo orden por
// nombre (pkgAppNameCmp, por bytes con mayusculas plegadas), desempate por
// tipo y por indice. tests/host lo compara con el codigo Arduino.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_DRW_QMAX 14

enum { FLEX_DRW_NATIVE = 0, FLEX_DRW_PKG = 1 };

typedef struct {
    uint8_t kind;    // FLEX_DRW_NATIVE (idx = id del registro) o FLEX_DRW_PKG (idx = indice del paquete)
    int16_t idx;
} flex_drw_cell_t;

typedef struct {
    int app_n;
    const char *(*app_name)(int id);
    bool (*app_hidden)(int id);
    int pkg_n;                          // 0 mientras no haya Flex Store
    const char *(*pkg_name)(int i);
    bool (*pkg_hidden)(int i);
} flex_drw_src_t;

bool flex_drw_match(const char *name, const char *q, int qn);
int  flex_drw_name_cmp(const char *a, const char *b);
// Devuelve cuantas celdas escribio en out (como mucho cap).
int  flex_drw_filter(const flex_drw_src_t *src, bool show_hidden, const char *q, int qn, flex_drw_cell_t *out,
                     int cap);

#ifdef __cplusplus
}
#endif
