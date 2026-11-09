// Flex OS Ultra · Almacenamiento: contenido de la pantalla principal y de la de detalles de memoria y sistema (docs/spec/03 §9;
// FlexOS_Ultra_AppStorage.h:36-638). C portable, con pruebas de host frente al codigo de Arduino compilado tal cual.
//
// El modelo NO dibuja: entrega una LISTA DE OPERACIONES (superficie, rectangulo, punto, texto, carpeta, chevron, nube) con las
// coordenadas EXACTAS de Arduino (de pantalla: la caja de la app empieza en y = 96) y la interfaz LVGL las pinta tal cual. Asi lo que se
// compara en el PC con Arduino es lo que se ve. Los valores vivos (memoria, sistema de archivos, apps, telefono) entran en estructuras
// que rellena la app desde los servicios reales; lo que no se puede medir dice "No disponible" y no se inventa.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_Mem.h"
#include "flex_fs_scan.h"
#include "flex_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_ST_APPS 19            // FLEX_APP_N: las apps que cuentan en "Estado del sistema"

typedef enum {
    FST_SURF = 0,    // superficie del sistema (vidrio o plana): x, y, w, h, r; role FSC_SURF_CARD / FSC_SURF_ELEV
    FST_RRECT,       // rectangulo redondeado relleno: x, y, w, h, r; color de `role` (o `rgb`)
    FST_OUTLINE,     // contorno redondeado de 1 px: x, y, w, h, r; color de `role`
    FST_DOT,         // circulo relleno: centro (x, y), radio `r`
    FST_TEXT,        // drawText / drawTextClip: x, y, `size`, `role`; max_right > 0 recorta sin puntos suspensivos
    FST_TEXT_R,      // drawTextR: alineado a la derecha en x
    FST_TEXT_C,      // drawTextC: centrado en x
    FST_FOLDER,      // icono de carpeta (almFolderIcon): esquina (x, y), lado `w`
    FST_CHEV,        // chevron ">" de una tarjeta: dos trazos de 2 px desde (x, y)
    FST_CLOUD,       // nube del acento (ckCloudGlyph): centro (x, y), tamano `r`
} flex_st_kind_t;

// roles de color (los resuelve el tema); FSC_RGB usa `rgb` (RGB565, los colores propios de las barras y las categorias)
typedef enum {
    FSC_TXT = 0, FSC_TXT2, FSC_MUTE, FSC_OK, FSC_WARN, FSC_ERR, FSC_PRIM, FSC_ONACC, FSC_ACCS, FSC_TRACK, FSC_BORDER, FSC_SURF,
    FSC_ACCENT, FSC_RGB, FSC_SEL, FSC_SURF2,
    FSC_SURF_CARD = 20, FSC_SURF_ELEV = 21,   // roles de FST_SURF
} flex_st_role_t;

typedef struct {
    uint8_t kind, size, role;
    int16_t x, y, w, h, r, max_right;
    uint16_t rgb;
    char text[112];
} flex_st_op_t;

#define FLEX_ST_OPS_MAX 80
typedef struct {
    flex_st_op_t op[FLEX_ST_OPS_MAX];
    int n;
} flex_st_page_t;

// ---- carpeta (almFolderIcon): tres rectangulos redondeados ---------------------------------------------------------------------
typedef struct { int x, y, w, h, r; uint16_t rgb; } flex_st_rect_t;
void flex_st_folder_rects(int x, int y, int s, flex_st_rect_t out[3]);
// ---- chevron: los dos trazos ((x, y) -> (x + 7, y + 7) y (x + 7, y + 7) -> (x, y + 14)) -----------------------------------------------

// ---- telefono emparejado (Flex Storage) ------------------------------------------------------------------------------------------------
// FSP_* de FlexOS_StorageCore.h; FST_PH_UNAVAILABLE = no hay servicio de telefono en esta version (A04c): no se finge un estado
enum { FST_PH_NONE = 0, FST_PH_OFF, FST_PH_REJECTED, FST_PH_READY, FST_PH_UNAVAILABLE };   // los valores de FSP_* de Arduino
typedef struct {
    int state;                 // FST_PH_*
    char name[40];             // nombre del telefono ("" = "Telefono")
    bool reachable;
    bool never_ok;             // lastOkAgeS == 0xFFFFFFFF: nunca respondio en este arranque
    bool quota_valid;          // READY, destino de Flex Cloud y cuota valida
    char quota[64];            // fclQuotaLine
} flex_st_phone_t;
// almPhoneState: texto del estado y su color (FSC_*)
void flex_st_phone_state(const flex_st_phone_t *p, char *out, size_t cap, int *role);

// ---- pantalla principal (almRenderMain) ------------------------------------------------------------------------------------------------
typedef struct {
    bool fs_ready;
    bool scanned;              // ya llegaron las categorias y los archivos grandes (la lectura es asincrona: hasta entonces "...")
    bool scan_err;             // la lectura fallo: "--" y "No se pudo leer", nunca ceros inventados
    char fs_err[64];           // flexFsError()
    uint32_t fs_total, fs_used;
    uint32_t ps_total, ps_free;
    int health;                // FLEXMEM_H_*
    uint32_t cat[FLEX_FSX_CAT_N];
    flex_fsx_big_t big[FLEX_FSX_BIG_MAX];
    int nbig;
    flex_st_phone_t phone;
} flex_st_main_in_t;
// Zonas pulsables de la pantalla (0 = no se pinto) y botones
typedef struct {
    int phone_y0, phone_y1;    // tarjeta "Flex Cloud en tu telefono"
    int det_y0, det_y1;        // tarjeta "Detalles de memoria y sistema"
    int opt_x0, opt_x1;        // ...y su pildora "Optimizar"
    int ver_y0, ver_y1;        // fila "Todos los archivos ... Ver..."
} flex_st_zones_t;
// Caja de la app: (bx, by, bw, bh) = (0, 96, 480, 640) a pantalla completa con botones.
void flex_st_main_build(flex_st_page_t *pg, flex_st_zones_t *z, const flex_st_main_in_t *in, int bx, int by, int bw, int bh,
                        flex_ui_textw_fn tw, void *ctx);
typedef enum { FST_HIT_NONE = 0, FST_HIT_PHONE, FST_HIT_DETAIL, FST_HIT_OPTIMIZE, FST_HIT_FILES } flex_st_hit_t;
// almTick: que hace un toque en y (y x para la pildora Optimizar)
int flex_st_main_hit(const flex_st_zones_t *z, int x, int y);

// ---- detalles de memoria y sistema (almDetDrawSection) ---------------------------------------------------------------------------------
#define FLEX_ST_SEC_N 5
#define FLEX_ST_DET_VY0 96                      // ALMD_VY0 = WIN_TOP
#define FLEX_ST_DET_VY1 (800 - 64 - 1)          // ALMD_VY1 = WIN_BOT - 1
extern const int flex_st_sec_h[FLEX_ST_SEC_N];  // alto fijo de cada seccion: 96, 240, 168, 236, 200
int flex_st_sec_top(int i);                     // almDetSecTop
int flex_st_content_h(void);                    // almDetContentH
int flex_st_max_scroll(void);                   // almDetMaxScroll

typedef struct {
    FlexMemSnap m;                              // memSnap()
    uint32_t fw_size;                           // almFwSize (0xFFFFFFFF = sin medir)
    uint32_t cat_sys, cat_apps, cat_docs, cache;   // flexFsCatSize(SYS/APPS/DOCS) y flexFsDirSize(/System/Cache)
    int reset_reason;                           // esp_reset_reason()
    char uptime[24];                            // buildUptime
    unsigned uptime_min;                        // millis() / 60000: el rotulo solo cambia por minutos
    char version[24];                           // flexOtaLocalVersion
    bool net_online;                            // gNetOnline
    unsigned loop_rate;                         // gLoopRate: vueltas del sistema por segundo
    int napps;
    uint8_t app_state[FLEX_ST_APPS];            // 0 cerrada, 1 en marcha, 2 suspendida (gAppState)
    bool app_shed[FLEX_ST_APPS], app_mem_has[FLEX_ST_APPS];
    uint32_t app_mem[FLEX_ST_APPS];             // appMemBytes
    char app_name[FLEX_ST_APPS][24];
} flex_st_sys_t;
// Operaciones de la seccion `sec` con su borde superior en y = 0 (la interfaz las coloca).
void flex_st_detail_section(flex_st_page_t *pg, int sec, const flex_st_sys_t *s);
// almDetSecSig: firma de los valores de la seccion; si no cambia, no se repinta
uint32_t flex_st_detail_sig(int sec, const flex_st_sys_t *s);
const char *flex_st_boot_cause(int reset_reason);   // almBootCause
// Geometria del boton "Optimizar Flex OS" (seccion 0): esquina y tamano EN LA SECCION (y = 0 arriba)
#define FLEX_ST_OPT_X 36
#define FLEX_ST_OPT_W (480 - 72)
#define FLEX_ST_OPT_H 44
#define FLEX_ST_OPT_Y (8 + 26)
bool flex_st_opt_hit(int scroll, int x, int y);     // el toque cae en el boton (con el desplazamiento actual)

#ifdef __cplusplus
}
#endif
