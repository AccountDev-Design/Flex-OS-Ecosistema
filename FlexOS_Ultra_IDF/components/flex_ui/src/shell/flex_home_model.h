// Flex OS Ultra · modelo del escritorio (C portable, sin LVGL).
//
// Paginas, rejilla, iconos por ranura, widgets colocados, favoritas, ocultas y
// candados, con las MISMAS claves NVS, formatos y migraciones que la version
// Arduino (FlexOS_Ultra_Home.h / Widgets.h / Prefs.h), para que una placa que
// cambia de firmware conserve su escritorio tal cual. tests/host compara la
// normalizacion y la geometria con el codigo original.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_HOME_PAGES_MAX   5
#define FLEX_HOME_COLS_MAX    5
#define FLEX_HOME_ROWS_MAX    4
#define FLEX_HOME_STRIDE      (FLEX_HOME_COLS_MAX * FLEX_HOME_ROWS_MAX)   // 20 ranuras por pagina
#define FLEX_HOME_TOTAL       (FLEX_HOME_PAGES_MAX * FLEX_HOME_STRIDE)    // 100 B ("hordq")
#define FLEX_HOME_LEGACY_PAGES 3
#define FLEX_HOME_LEGACY_SLOTS 12
#define FLEX_HOME_EMPTY       0xFF
#define FLEX_HOME_PKG_BASE    128     // >= 128: app descargada (ranura de paquete)
#define FLEX_HOME_WG_MAX      6
#define FLEX_HOME_WG_MAX_V1   3
#define FLEX_HOME_WG_BLOB     (2 + FLEX_HOME_PAGES_MAX * (1 + FLEX_HOME_WG_MAX * 5))      // 157 ("hwg2")
#define FLEX_HOME_WG_BLOB_V1  (2 + FLEX_HOME_PAGES_MAX * (1 + FLEX_HOME_WG_MAX_V1 * 5))   // 82 ("hwg")
#define FLEX_HOME_APPREG_VER  3

// Geometria (480x800)
#define FLEX_HOME_HDR_Y   72
#define FLEX_HOME_HDR_H   120
#define FLEX_HOME_GY0     212
#define FLEX_HOME_ROWSTEP 112
#define FLEX_HOME_DOCK_Y  (800 - 176)

// Tipos de widget (los ids viajan a la NVS)
enum {
    FLEX_WG_NONE = 0, FLEX_WG_CLOCK, FLEX_WG_CLOCK_A, FLEX_WG_DATE, FLEX_WG_WIFI, FLEX_WG_MEM, FLEX_WG_STORAGE,
    FLEX_WG_RETIRED_7, FLEX_WG_CRONO, FLEX_WG_CAM, FLEX_WG_CLIMA, FLEX_WG_CALEND, FLEX_WG_COUNT
};

typedef struct {
    uint8_t type, col, row, w, h;   // celdas; fila 0 = cabecera
} flex_home_wg_t;

typedef struct {
    const char *name, *cat;
    uint8_t w, h, min_w, max_w, min_h, max_h, min_px_h;
} flex_home_wg_desc_t;
extern const flex_home_wg_desc_t flex_home_wg_reg[FLEX_WG_COUNT];

typedef struct {
    uint8_t order[FLEX_HOME_TOTAL];
    uint8_t page_n, main, cols, rows, icon_sz;
    bool labels, locked, dots, pinch, reduce;
    flex_home_wg_t wg[FLEX_HOME_PAGES_MAX][FLEX_HOME_WG_MAX];
    uint8_t wg_n[FLEX_HOME_PAGES_MAX];
    uint32_t fav, hidden, lock;     // bit i = app i
    int page;                       // pagina visible
} flex_home_t;

extern flex_home_t g_home;

// Registro (lo pone el marco de apps): numero de apps nativas y cuales van al
// escritorio de fabrica.
void flex_home_set_registry(int app_n, uint32_t factory_fav);
// Validez de una ranura de app descargada (la decide Flex Store). Por defecto
// se conserva: sin el sistema de paquetes no se borra nada del usuario.
void flex_home_set_pkg_usable(bool (*fn)(int pkg_slot));

// Carga desde la NVS (cache de flex_storage) con todas las migraciones de la
// version Arduino; si migra algo lo guarda una vez.
void flex_home_load(void);
void flex_home_save(void);
void flex_home_normalize(void);

// Geometria de la rejilla activa (homeGrid)
void flex_home_grid(int *S, int *gx0, int *gy0, int *cstep, int *rstep, int *cols, int *rows);
void flex_home_slot_xy(int slot, int *x, int *y);
int  flex_home_dots_y(void);
int  flex_home_slot_count(void);
void flex_home_wg_rect(const flex_home_wg_t *w, int *x, int *y, int *ww, int *hh);
uint32_t flex_home_cell_mask(int page, int skip_wg);
static inline int flex_home_idx(int page, int local) { return page * FLEX_HOME_STRIDE + local; }
static inline bool flex_home_is_pkg(uint8_t v) { return v >= FLEX_HOME_PKG_BASE && v != FLEX_HOME_EMPTY; }

static inline bool flex_app_is_fav(int id) { return id >= 0 && id < 32 && (g_home.fav >> id) & 1u; }
static inline bool flex_app_is_hidden(int id) { return id >= 0 && id < 32 && (g_home.hidden >> id) & 1u; }

// Dock: lista explicita (en Arduino eran "los ids 12..15", que hoy son estas
// cuatro apps; aqui se fija la lista para que un cambio de registro no la mueva).
#define FLEX_HOME_DOCK_N 4
extern const uint8_t flex_home_dock[FLEX_HOME_DOCK_N];

#ifdef __cplusplus
}
#endif
