// Flex OS Ultra · Device Care: metricas reales, puntuacion, historial y motor de diagnostico (docs/spec/03b §15; FlexOS_Ultra_DeviceCare.h y
// FlexOS_Ultra_DeviceTests.h). C portable, con pruebas de host frente al codigo de Arduino compilado tal cual. Sin LVGL ni disco: la app mete lo
// que MIDE el sistema (flex_dc_sys_t) y escribe lo que el modelo decide guardar.
//
// Ni una cifra esta inventada: lo que no se puede medir es DCL_NA ("No disponible") y no puntua ni a favor ni en contra.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FlexOS_Mem.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- textos: la tabla de Arduino (ES, EN, FR, PT, IT) ----
#define FLEX_DC_LANGS 5
#include "flex_dc_text_ids.h"
const char *flex_dc_t(int id, int li);

// ---- metricas ----
enum { DCH_MEM = 0, DCH_PSRAM, DCH_STAB, DCH_WDT, DCH_SVC, DCH_ERR, DCH_BOOT, DCH_STO, DCH_N };
enum { DCL_OK = 0, DCL_WARN, DCL_BAD, DCL_NA };
typedef struct {
    uint8_t level;
    int8_t pct;          // 0..100 si la metrica es un porcentaje; -1 si no
    char detail[40];
} flex_dc_metric_t;

// Lo que MIDE el sistema (la app lo rellena desde los servicios reales)
typedef struct {
    int li;                      // idioma 0..4
    FlexMemSnap m;               // memSnap() tras memSampleNow()
    uint8_t boot_reason;         // esp_reset_reason() de ESTE arranque
    uint32_t uptime_ms;
    bool wdt_ok;                 // esp_task_wdt_status(NULL) == ESP_OK
    unsigned loop_rate;          // vueltas del sistema por segundo
    bool fs_ready, clk_anchored, time_nvs_ok, flush_fault;
    int recover_n;               // recuperaciones del bus I2C
    uint32_t boot_abnormal;      // reinicios inesperados acumulados (NVS)
    uint32_t fs_total, fs_used;
    uint32_t cpu_mhz;            // 0 = no se sabe
    uint32_t fw_size;            // 0 = no se sabe
} flex_dc_sys_t;

void flex_dc_collect(const flex_dc_sys_t *s, flex_dc_metric_t m[DCH_N]);                 // dcCollect
bool flex_dc_score_from(const flex_dc_metric_t m[DCH_N], uint8_t *out);                   // dcScoreFrom: false si no se pudo medir NADA
enum { FDC_COL_OK = 0, FDC_COL_WARN, FDC_COL_ERR, FDC_COL_MUTE };                         // los colores de TH_OK / WARN / ERR / MUTE
int flex_dc_level_color(int level);                                                       // dcLevelColor
int flex_dc_score_color(int score);                                                       // dcScoreColor
const char *flex_dc_score_word(int score, int li);                                        // dcScoreWord

// ---- historial persistente (16 registros de 8 bytes en /System/devcare.bin) ----
#define FLEX_DC_HIST_MAX 16
#define FLEX_DC_HIST_MAGIC 0x44434152u   // 'DCAR'
#define FLEX_DC_HIST_VER 1
enum { DC_EV_DIAG = 0, DC_EV_FALL, DC_EV_POST, DC_EV_OPT };
typedef struct {
    uint32_t utc;
    uint8_t kind, score, flags, extra;
} flex_dc_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, n;
    flex_dc_rec_t rec[FLEX_DC_HIST_MAX];
} flex_dc_file_t;
typedef struct {
    flex_dc_rec_t rec[FLEX_DC_HIST_MAX];
    int n;
    bool dirty, loaded;
    uint8_t last_score;          // ultimo diagnostico (dcLastScore)
    uint32_t last_check;         // epoca UTC; 0 = nunca
} flex_dc_hist_t;
void flex_dc_hist_load(flex_dc_hist_t *h, const void *blob, int len);                     // dcHistLoad sobre el contenido del archivo (len < 0: no hay)
void flex_dc_hist_add(flex_dc_hist_t *h, uint32_t utc, uint8_t kind, uint8_t score, uint8_t flags, uint8_t extra);   // dcHistAdd (sin guardar)
// dcHistSave: si hay algo sin guardar rellena el archivo y devuelve true (el llamante lo escribe y, si sale bien, llama a flex_dc_hist_saved)
bool flex_dc_hist_file(const flex_dc_hist_t *h, flex_dc_file_t *out);
void flex_dc_hist_saved(flex_dc_hist_t *h);
// Alto REAL de la lista del historial (dcHistContentH): 66 px por tarjeta y 26 mas por cada cambio de dia local
int flex_dc_hist_content_h(const flex_dc_hist_t *h, int pad);

// ---- motor de diagnostico (DeviceTests.h) ----
enum { DG_SYS = 0, DG_IMU, DG_SCREEN, DG_TOUCH, DG_DONE, DG_N };
#define FLEX_DC_DIAG_STEP_MS 620
#define FLEX_DC_SYS_ROWS 9
typedef struct {
    int label;                   // DCS_* (>= 0) o -1 con `lit` (etiqueta literal: CPU, RAM, PSRAM, Flash)
    const char *lit;
    char value[28];
    uint8_t level;
} flex_dc_sysrow_t;
// dcRunSystemTest: las nueve filas medidas y la puntuacion del sistema (ok = se pudo medir algo)
void flex_dc_run_system_test(const flex_dc_sys_t *s, flex_dc_sysrow_t rows[FLEX_DC_SYS_ROWS], uint8_t *score, bool *score_ok);
typedef struct {
    uint8_t sys;  bool sys_ok;
    uint8_t imu_mask; bool imu_avail;
    int8_t screen;               // -1 no probado, 0 no, 1 si
    int8_t touch;                // -1 no probado, 0..100 cobertura
    uint8_t final; bool final_ok;
} flex_dc_res_t;
void flex_dc_res_reset(flex_dc_res_t *r);
// dcResultCompute: media de lo que SE PUDO medir; si hay algo, anota el diagnostico en el historial
void flex_dc_result_compute(flex_dc_res_t *r, bool post, flex_dc_hist_t *h, uint32_t utc);
// Test tactil: rejilla de 6 x 8 celdas
#define FLEX_DC_TT_COLS 6
#define FLEX_DC_TT_ROWS 8
int flex_dc_touch_coverage(int hit);                                                      // celdas * 100 / total

#ifdef __cplusplus
}
#endif
