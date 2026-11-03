// Flex OS Ultra · rasterizador de circulos y segmentos con bordes suaves (C portable, con pruebas de host pixel a pixel frente a Arduino).
//
// FlexOS_Ultra_Gfx.h:mix565, pxA, hLine, fillCircleAA y strokeSegAA, con la MISMA aritmetica expresion por expresion (el ultimo bit de
// `t` mueve un nivel de alfa en el borde). Escriben sobre un buffer RGB565 propio en coordenadas de pantalla: `ox, oy` es la esquina del
// buffer en la pantalla y `clip` lo que se deja pasar (como gClipX0..gClipY1). Sin LVGL: lo usan las miniaturas y el lienzo de Paint.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *buf;
    int w, h;          // tamano del buffer, en pixeles
    int stride;        // pixeles por fila (>= w)
    int ox, oy;        // esquina superior izquierda del buffer en coordenadas de pantalla
    int cx0, cx1, cy0, cy1;   // recorte, en coordenadas de pantalla (inclusivo); se cruza con los limites del buffer
} flex_raster_t;

// Buffer sin recorte propio: todo su rectangulo.
void flex_raster_init(flex_raster_t *r, uint16_t *buf, int w, int h, int stride, int ox, int oy);
uint16_t flex_raster_mix565(uint16_t a, uint16_t b, uint8_t t);   // mix565
void flex_raster_fill(flex_raster_t *r, uint16_t color);
void flex_raster_fill_circle_aa(const flex_raster_t *r, float cx, float cy, float rad, uint16_t color);
void flex_raster_stroke_seg_aa(const flex_raster_t *r, float x0, float y0, float x1, float y1, float rad, uint16_t color);

#ifdef __cplusplus
}
#endif
