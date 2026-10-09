// Flex OS Ultra · modelo del panel rapido (C portable, sin LVGL).
//
// Catalogo de controles con ids ESTABLES (van a NVS), configuracion de fabrica,
// normalizacion, migracion de version, blob "flexqs/qp1" byte a byte igual que
// Arduino, maquetacion en 4 columnas (los 1x1 van a la tarjeta de circulos) y
// operaciones del editor. FlexOS_Ultra_QuickPanel.h:86-927 y
// QuickPanelEdit.h:39-124; tests/host lo compara con el codigo Arduino.
//
// Diferencia deliberada (docs/spec/01b §5.11): Arduino normaliza con la
// disponibilidad del MOMENTO (sin PIN, "Bloquear" desaparece del blob para
// siempre al pulsar "Listo"). Aqui hay dos preguntas: keep() "puede existir en
// esta placa" decide que se conserva; shown() "funciona ahora" decide que se
// dibuja, se ofrece en el catalogo y responde.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- ids estables (QuickPanel.h:122) ---------------------------------------------
enum {
    FLEX_QS_WIFI = 0, FLEX_QS_AIRPLANE, FLEX_QS_BLE, FLEX_QS_BRIGHT, FLEX_QS_THEME, FLEX_QS_GLASS,
    FLEX_QS_POWERSAVE, FLEX_QS_SETTINGS, FLEX_QS_CONN, FLEX_QS_DEX, FLEX_QS_RETIRED_10, FLEX_QS_OTA,
    FLEX_QS_FILES, FLEX_QS_RETIRED_13, FLEX_QS_CAMERA, FLEX_QS_GALLERY, FLEX_QS_CRONO, FLEX_QS_LOCK,
    FLEX_QS_POWEROFF, FLEX_QS_NTP, FLEX_QS_VOLUME, FLEX_QS_MUTE, FLEX_QS_DND, FLEX_QS_GLASSFX,
    FLEX_QS_COUNT
};

enum { FLEX_QT_TOGGLE = 0, FLEX_QT_ACTION, FLEX_QT_SLIDER };
enum { FLEX_QCAT_CONN = 0, FLEX_QCAT_SCREEN, FLEX_QCAT_SYSTEM, FLEX_QCAT_TOOLS, FLEX_QCAT_COUNT };

#define FLEX_QSZ_1x1 0x01
#define FLEX_QSZ_2x1 0x02
#define FLEX_QSZ_4x1 0x04
#define FLEX_QSZ_2x2 0x08
#define FLEX_QOR_H   0x01
#define FLEX_QOR_V   0x02

typedef struct {
    const char *name;    // circulo 1x1
    const char *title;   // capsula / modulo
    uint8_t type, sizes, oris, cat;
} flex_qs_meta_t;

const flex_qs_meta_t *flex_qs_meta(int id);   // NULL fuera de rango
extern const char *const FLEX_QS_CAT_NAME[FLEX_QCAT_COUNT];

// ---- geometria (QuickPanel.h:86-117) ---------------------------------------------
#define FLEX_QP_MX      16
#define FLEX_QP_CONT_W  448
#define FLEX_QP_GAP     12
#define FLEX_QP_CW      103
#define flex_qp_span_w(w) ((w) * FLEX_QP_CW + ((w) - 1) * FLEX_QP_GAP)
#define flex_qp_col_x(c)  (FLEX_QP_MX + (c) * (FLEX_QP_CW + FLEX_QP_GAP))
#define FLEX_QP_HDR_H   116
#define FLEX_QP_FOOT_H  34
#define FLEX_QP_VIEW_Y0 FLEX_QP_HDR_H
#define FLEX_QP_VIEW_Y1 (800 - FLEX_QP_FOOT_H - 1)
#define FLEX_QP_VIEW_H  (FLEX_QP_VIEW_Y1 - FLEX_QP_VIEW_Y0 + 1)
#define FLEX_QP_RH1     74
#define FLEX_QP_RH2     (FLEX_QP_RH1 * 2 + FLEX_QP_GAP)
#define FLEX_QP_VGAP    12
#define FLEX_QP_RAD     26
#define FLEX_QP_RAD_S   20
#define FLEX_QP_GPAD    14
#define FLEX_QP_TROW    98
#define FLEX_QP_TCIRC   62
#define FLEX_QP_HANDLE_H 22
#define FLEX_QP_GROWS_MIN 2
#define FLEX_QP_GROWS_MAX 5
#define flex_qp_group_h(rows) (FLEX_QP_GPAD + (rows) * FLEX_QP_TROW + FLEX_QP_HANDLE_H)
#define FLEX_QP_TCOLW   ((FLEX_QP_CONT_W - 2 * FLEX_QP_GPAD) / 4)

// ---- configuracion y persistencia (QuickPanel.h:629-829) -------------------------
#define FLEX_QS_CFG_VER   2
#define FLEX_QS_MAX_ITEMS 24
#define FLEX_QS_BLOB_N    (4 + FLEX_QS_MAX_ITEMS * 5)
#define FLEX_QS_NVS_NS    "flexqs"
#define FLEX_QS_NVS_KEY   "qp1"

typedef struct {
    uint8_t id, w, h, ori, vis;
} flex_qs_item_t;

typedef struct {
    flex_qs_item_t it[FLEX_QS_MAX_ITEMS];
    uint8_t n;
    uint8_t grows;   // filas visibles de la tarjeta de circulos (2..5)
} flex_qs_cfg_t;

typedef bool (*flex_qs_pred_t)(int id);

void flex_qs_first_size(uint8_t mask, uint8_t *w, uint8_t *h);
bool flex_qs_size_allowed(int id, int w, int h);
bool flex_qs_next_size(int id, int w, int h, int dir, uint8_t *nw, uint8_t *nh);

void    flex_qs_factory(flex_qs_cfg_t *c, flex_qs_pred_t keep);
uint8_t flex_qs_normalize(flex_qs_item_t *it, uint8_t n, uint8_t *grows, flex_qs_pred_t keep);
void    flex_qs_adopt_new(flex_qs_cfg_t *c, uint8_t from_ver, flex_qs_pred_t keep);
void    flex_qs_serialize(const flex_qs_cfg_t *c, uint8_t b[FLEX_QS_BLOB_N]);
bool    flex_qs_deserialize(flex_qs_cfg_t *c, const uint8_t *b, flex_qs_pred_t keep);
// qpLoad: rd = bytes leidos de NVS (0 si no hay). true si venia de NVS, false si
// se cayo a fabrica.
bool    flex_qs_load(flex_qs_cfg_t *c, const uint8_t *b, size_t rd, flex_qs_pred_t keep);

// ---- maquetacion (QuickPanel.h:842-927) ------------------------------------------
enum { FLEX_QB_ITEM = 0, FLEX_QB_GROUP, FLEX_QB_ADD };

typedef struct {
    uint8_t kind;
    int8_t item;   // indice en la configuracion (-1: tarjeta emitida al final o "Anadir")
    int16_t x, y, w, h;
} flex_qs_block_t;

typedef struct {
    flex_qs_block_t blk[FLEX_QS_MAX_ITEMS + 2];
    int blk_n;
    uint8_t tiles[FLEX_QS_MAX_ITEMS];   // indices de los 1x1 en orden
    int tile_n;
    int content_h;
    int group_blk;                      // -1 si no hay tarjeta
} flex_qs_layout_t;

void flex_qs_layout(flex_qs_layout_t *L, const flex_qs_item_t *src, int n, int group_px, bool edit,
                    flex_qs_pred_t shown);
int  flex_qs_total_rows(const flex_qs_layout_t *L);
int  flex_qs_group_inner_h(int group_px);
int  flex_qs_group_min_px(void);
int  flex_qs_group_max_px(const flex_qs_layout_t *L);
// qpRelayout: maqueta y acota el alto de la tarjeta a las filas que hay de
// verdad. Devuelve el alto (px) que quedo.
int  flex_qs_relayout(flex_qs_layout_t *L, const flex_qs_item_t *src, int n, int group_px, bool edit,
                      flex_qs_pred_t shown);
int  flex_qs_scroll_max(const flex_qs_layout_t *L);
int  flex_qs_gscroll_max(const flex_qs_layout_t *L, int group_px);
// qpGroupSnap: alto objetivo (filas completas) para el alto actual gh; *rows_out
// recibe las filas.
int  flex_qs_group_snap(const flex_qs_layout_t *L, float gh, uint8_t *rows_out);
uint32_t flex_qs_group_snap_ms(float from, float to);
void flex_qs_tile_center(int k, int gy_top, int *cx, int *cy);

// ---- editor (QuickPanelEdit.h:87-124) y catalogo (QuickPanelGlass.h:510) ----------
// Quitar: false si es el ultimo (el panel nunca queda vacio) o el indice no vale.
bool flex_qs_edit_remove(flex_qs_cfg_t *c, int idx);
void flex_qs_edit_move(flex_qs_cfg_t *c, int from, int to);
bool flex_qs_edit_add(flex_qs_cfg_t *c, int id, flex_qs_pred_t shown);
int  flex_qs_catalog(const flex_qs_cfg_t *c, uint8_t out[FLEX_QS_COUNT], flex_qs_pred_t shown);

#ifdef __cplusplus
}
#endif
