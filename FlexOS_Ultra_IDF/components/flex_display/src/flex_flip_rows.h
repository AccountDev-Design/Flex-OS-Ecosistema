// Filas que hay que pasar de la cache a la PSRAM antes de entregar un
// framebuffer al DMA. C portable: lo usan el puerto de pantalla del firmware
// y el simulador del PC, que comprueba con un modelo de la cache que el rango
// cubre todo lo que LVGL escribe.
//
// Con LVGL en modo DIRECT y dos buffers, la CPU escribe en el buffer de atras:
//   - las zonas que dibuja en este cuadro (llegan a flush_cb), y
//   - las zonas que cambiaron en el cuadro anterior, que LVGL copia desde el
//     buffer visible (refr_sync_areas) SIN pasarlas por flush_cb.
// Las segundas caen dentro de las filas del cuadro anterior, asi que basta la
// union de las filas de los dos ultimos cuadros.
#pragma once

#include <stdint.h>

typedef struct {
    int32_t cur_y1, cur_y2;     // filas dibujadas en el cuadro en curso
    int32_t prev_y1, prev_y2;   // filas dibujadas en el cuadro anterior
    int32_t v_res;
} flex_flip_rows_t;

static inline void flex_flip_rows_init(flex_flip_rows_t *r, int32_t v_res)
{
    r->v_res = v_res;
    r->cur_y1 = INT32_MAX;
    r->cur_y2 = -1;
    // El primer cuadro escribe la pantalla entera.
    r->prev_y1 = 0;
    r->prev_y2 = v_res - 1;
}

static inline void flex_flip_rows_add(flex_flip_rows_t *r, int32_t y1, int32_t y2)
{
    if (y1 < r->cur_y1) {
        r->cur_y1 = y1;
    }
    if (y2 > r->cur_y2) {
        r->cur_y2 = y2;
    }
}

// Rango a escribir para entregar el cuadro en curso; pasa el cuadro en curso a
// "anterior". full != 0 fuerza la pantalla entera.
static inline void flex_flip_rows_take(flex_flip_rows_t *r, int full, int32_t *y1, int32_t *y2)
{
    int32_t a = r->cur_y1 < r->prev_y1 ? r->cur_y1 : r->prev_y1;
    int32_t b = r->cur_y2 > r->prev_y2 ? r->cur_y2 : r->prev_y2;
    if (full) {
        a = 0;
        b = r->v_res - 1;
    }
    if (a < 0) {
        a = 0;
    }
    if (b > r->v_res - 1) {
        b = r->v_res - 1;
    }
    *y1 = a;
    *y2 = b;
    r->prev_y1 = r->cur_y1;
    r->prev_y2 = r->cur_y2;
    r->cur_y1 = INT32_MAX;
    r->cur_y2 = -1;
}
