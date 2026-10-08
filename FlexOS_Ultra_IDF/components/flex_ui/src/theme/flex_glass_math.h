// Flex OS Ultra · Liquid Glass: la matematica del material (C portable, sin LVGL).
//
// Misma regla que la version Arduino (FlexOS_Ultra_Theme.h), comprobada bit a
// bit en tests/host: intensidad 0..100 en pasos de 5 (50 = material de
// siempre), tinte adaptativo segun la luma del fondo, especular arriba y
// sombreado abajo, y el box-blur separable con ventana que se encoge en los
// bordes (division exacta por reciproco de 20 bits).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_GLASS_LVL_DEF   50
#define FLEX_GLASS_LVL_STEP  5
#define FLEX_GLASS_BLUR_RMAX 16
#define FLEX_GLASS_BLUR_R_EFF 2    // modo visual eficiente

typedef struct {
    uint8_t level;                 // 0..100, multiplo de 5
    uint8_t blur_r;                // radio del box-blur
    uint8_t tint_min, tint_max, tint_base;
    uint8_t spec, shade;           // especular (blanco) y sombreado (negro), 0..255
    uint8_t corner_strong, corner_weak;   // peso del borde direccional
} flex_glass_params_t;

void    flex_glass_level_apply(uint8_t level, flex_glass_params_t *p);
int     flex_glass_luma(uint16_t c);                         // 0..266 (dominio 565)
uint8_t flex_glass_tint_mix(const flex_glass_params_t *p, uint32_t luma_sum, int luma_n, uint16_t tint,
                            uint8_t min_mix);
// Luz de la fila j de un panel de alto h: color (blanco/negro) y alfa.
void    flex_glass_shade_row(const flex_glass_params_t *p, int j, int h, uint16_t *col, uint8_t *a);
// Inset de la esquina redondeada en la fila j (glInset).
int     flex_glass_inset(int j, int h, int rad);

// Box-blur separable sobre buf (w x h, RGB565, filas contiguas). R <= 16.
// Usa memoria estatica: llamar desde una sola tarea.
void    flex_glass_blur(uint16_t *buf, int w, int h, int R);

#ifdef __cplusplus
}
#endif
