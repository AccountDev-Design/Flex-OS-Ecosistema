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

// Primera ranura libre (homeFirstFree) y la misma creando pagina si no hay
// (homeFirstFreeGrow); -1 si no cabe.
int  flex_home_first_free(void);
int  flex_home_first_free_grow(void);
// Caja de aplicaciones: favorita en Inicio si/no y visible si/no
// (drwFavToggle / drwHideToggle, AppDrawer.h:1080-1114). Normalizan; quien
// llama guarda (flex_home_save). Devuelven false si no cambio nada.
bool flex_home_fav_toggle(int id);
bool flex_home_hide_toggle(int id);
// Ajustes no se puede ocultar (appCanHide, Prefs.h:124)
bool flex_app_can_hide(int id);

// Geometria de la rejilla activa (homeGrid)
void flex_home_grid(int *S, int *gx0, int *gy0, int *cstep, int *rstep, int *cols, int *rows);
void flex_home_slot_xy(int slot, int *x, int *y);
int  flex_home_dots_y(void);
int  flex_home_slot_count(void);
void flex_home_wg_rect(const flex_home_wg_t *w, int *x, int *y, int *ww, int *hh);
uint32_t flex_home_cell_mask(int page, int skip_wg);

// Modo edicion (01a §8): la misma logica que Arduino (Home.h:1894-2046, Widgets.h:348-445)
#define FLEX_HOME_BAND_BOT_MAX 596
#define FLEX_HOME_ED_EDGE_W    34
#define FLEX_HOME_ED_EDGE_MS   700
typedef struct {
    int dir;
    uint32_t ms;
} flex_home_edge_t;
bool flex_home_wg_place_ok(int page, int type, int c, int r, int w, int h, int skip_wg);
void flex_home_wg_limits(int type, int *min_w, int *max_w, int *min_h, int *max_h);
bool flex_home_wg_can_resize(int type);
int  flex_home_wg_at(int page, int px, int py);
void flex_home_wg_remove(int page, int idx);
int  flex_home_wg_to_page(int src, int idx, int dst);
int  flex_home_slot_at(int px, int py);                           // edSlotAt (-1 fuera)
bool flex_home_layout_cell_at(int px, int py, int *c, int *r);    // fila 0 = cabecera
void flex_home_ed_move(int page, int from, int to);               // edMove
int  flex_home_first_free_cell(int page);
int  flex_home_band_bot(void);                                    // homeBandBot
// -1/+1 cuando toca cambiar de pagina, 0 si no
int  flex_home_ed_edge(flex_home_edge_t *e, int x, int page, int page_n, uint32_t now);

// ---- Personalizar inicio (01a §10): operaciones sobre el modelo ---------------------------
// Las mismas que homePage*/homeSet*/homeResetLayout/homeWgAdd de FlexOS_Ultra_HomeCfg.h y
// Widgets.h; todas guardan al terminar (homeOrderSave). tests/host las compara con el
// codigo Arduino extraido.
typedef enum { FLEX_HC_OK = 0, FLEX_HC_MAX_PAGES, FLEX_HC_LAST_PAGE, FLEX_HC_BAD } flex_hc_res_t;
int  flex_hc_page_items(int page);                  // iconos + widgets de una pagina
flex_hc_res_t flex_hc_page_add(void);               // "Maximo de 5 paginas"
flex_hc_res_t flex_hc_page_delete(int page);        // los iconos se recolocan; "ultima pagina"
void flex_hc_page_swap(int a, int b);               // iconos, widgets y marca de principal viajan
void flex_hc_set_main(int page);
void flex_hc_set_grid(int cols, int rows);          // sin perder iconos (normaliza)
void flex_hc_reset_layout(void);                    // reparto de fabrica (3 paginas, 4x3, Clima+Calendario)
// 0 = anadido, 1 = la pagina ya tiene FLEX_HOME_WG_MAX widgets, 2 = no cabe (o tipo invalido)
int  flex_hc_wg_add(int page, int type);
// Rejillas que se ofrecen en Ajustes de inicio (HC_GRID_*) y la activa
#define FLEX_HC_GRID_OPTS 4
extern const uint8_t flex_hc_grid_c[FLEX_HC_GRID_OPTS], flex_hc_grid_r[FLEX_HC_GRID_OPTS];
int  flex_hc_grid_idx(void);
// Destino de una tarjeta reordenada: reorder - round(slide / step), acotado (hcPagesTouch)
int  flex_hc_reorder_dest(int reorder, float slide, int step, int page_n);
// Reordenar con el dedo (hcPagesTouch): intercambia paso a paso hasta el destino y devuelve la
// posicion final de la pagina arrastrada
int  flex_hc_reorder_apply(int from, float slide, int step);
// Selector de "Mis imagenes" (hcScanImages): JPEG de /Imagenes, /Camara y /Descargas, como mucho 24
#define FLEX_HC_PICK_MAX 24
#define FLEX_HC_PATH_MAX 80
#define FLEX_HC_ENT_MAX  24      // entradas leidas por carpeta
typedef struct { char name[48]; bool dir; } flex_hc_ent_t;
typedef int (*flex_hc_ls_fn)(void *ctx, const char *dir, flex_hc_ent_t *ents, int cap);
int  flex_hc_scan_images(flex_hc_ls_fn ls, void *ctx, char paths[FLEX_HC_PICK_MAX][FLEX_HC_PATH_MAX]);
// Filtro de extensiones de hcScanImages (.jpg .JPG .jpeg .JPEG, nombre de >= 5 caracteres)
bool flex_hc_is_jpeg_name(const char *name);

// Pellizco de dos dedos (hpzUpdate, 01a §10.9): clasificador puro con tiempos y distancias
// inyectados. mode: 0 = no permitido, 1 = escritorio libre (cierra -> abre Personalizar),
// 2 = vista Paginas (abre -> sale de Personalizar).
#define FLEX_PZ_MIN_START_D2 (140 * 140)
#define FLEX_PZ_MIN_OPEN_D2  (45 * 45)
#define FLEX_PZ_MIN_MS       120
#define FLEX_PZ_MAX_MS       700
#define FLEX_PZ_ABS_PX       50
#define FLEX_PZ_FINGERS_FRESH_MS 150   // la cuenta de dedos del GT911 vale 150 ms
typedef struct { int x, y, id; } flex_pz_pt_t;
// Lee los contactos activos (solo se llama cuando el chip ya dijo >= 2). Devuelve cuantos.
typedef int (*flex_pz_poll_fn)(void *ctx, flex_pz_pt_t out[4]);
typedef struct {
    uint8_t state;          // 0 reposo · 1 siguiendo · 2 consumido
    int id_a, id_b;
    int32_t d2_ref;
    uint32_t t0;
    bool swallow;           // con dos contactos validos nadie mas ve el toque
} flex_pz_t;
enum { FLEX_PZ_NONE = 0, FLEX_PZ_OPEN_CFG, FLEX_PZ_EXIT_CFG };
void flex_pz_init(flex_pz_t *p);
int  flex_pz_update(flex_pz_t *p, int mode, uint8_t fingers, uint32_t fingers_ms, uint32_t now,
                    flex_pz_poll_fn poll, void *ctx);

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
