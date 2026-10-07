// Flex OS Ultra · pantalla de prueba de la Fase 1 (pantalla + tactil + LVGL).
//
// LVGL puro y C portable: el firmware y el simulador del PC compilan este
// mismo archivo. Todo lo que viene del hardware entra por flex_diag_ops_t.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "flex_metrics.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_DIAG_MAX_POINTS 5

typedef struct {
    uint8_t count;
    struct {
        uint8_t id;
        int16_t x;
        int16_t y;
    } pts[FLEX_DIAG_MAX_POINTS];
} flex_diag_touch_t;

typedef struct {
    uint32_t heap_internal_free_kb;
    uint32_t heap_internal_min_kb;
    uint32_t psram_free_kb;
    uint16_t chip_rev;            // mayor*100 + menor
    uint16_t build_rev_min;
    uint16_t build_rev_max;
    bool     touch_present;
    uint16_t touch_addr;
    char     touch_id[5];
    uint16_t touch_res_x;
    uint16_t touch_res_y;
    uint32_t touch_errors;
    uint32_t i2c_errors;
    uint32_t i2c_recoveries;
    bool     i2c_wedged;
} flex_diag_sys_t;

typedef struct {
    void (*get_metrics)(flex_metrics_t *out);
    bool (*get_touch)(flex_diag_touch_t *out);
    void (*get_sys)(flex_diag_sys_t *out);
    void (*set_brightness)(uint8_t pct);
    uint8_t initial_brightness;
} flex_diag_ops_t;

// Crea y carga la pantalla de prueba. ops debe vivir mientras exista.
lv_obj_t *flex_ui_diag_create(const flex_diag_ops_t *ops);

// Activa o desactiva el modo de estres (redibujar la pantalla entera en cada
// refresco) para medir el peor caso. Tambien lo controla un interruptor.
void flex_ui_diag_set_stress(bool on);

#ifdef __cplusplus
}
#endif
