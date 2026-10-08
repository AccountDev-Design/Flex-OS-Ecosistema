// Flex OS Ultra · fondos de pantalla procedurales (C portable, sin LVGL).
//
// Los 8 fondos de la version Arduino (FlexOS_Ultra_Wallpaper.h), generados
// por codigo en un buffer RGB565 de 480x800. Mismo resultado BIT A BIT que la
// version Arduino (tests/host compara contra su codigo): una placa que se
// actualiza ve el mismo fondo. Se generan una vez al elegir fondo o tema,
// nunca por cuadro; LVGL los muestra como imagen.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_WALL_W      480
#define FLEX_WALL_H      800
#define FLEX_WALL_N      8
#define FLEX_WALL_IMG    200   // valor guardado para "imagen del almacenamiento"

extern const char *const flex_wall_names[FLEX_WALL_N];

// Color RGB565 desde 8 bits por canal (se pierden los bits bajos, como TC()).
static inline uint16_t flex_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// Mezcla a <- b con peso t (0 = a, 255 = b), division exacta por 255.
uint16_t flex_mix565(uint16_t a, uint16_t b, uint8_t t);
int      flex_isqrt32(int v);

// Pinta las filas [y0, y1] del fondo id en buf (480x800). id fuera de rango o
// FLEX_WALL_IMG -> fondo 0 (ruta segura). blobs: manchas del fondo 0 en el
// escritorio. No necesita memoria propia.
void flex_wall_render(uint16_t *buf, int id, bool blobs, int y0, int y1);

// Paleta del fondo (para "Aplicar paleta al sistema").
typedef struct {
    uint16_t acc;    // acento
    uint16_t acc2;   // acento claro
} flex_wall_palette_t;
void flex_wall_palette(const uint16_t *buf, flex_wall_palette_t *out);

// Luma Rec.601 de un RGB565 (0..255) y color de texto legible encima.
uint8_t  flex_lum565(uint16_t c);
uint16_t flex_on_color(uint16_t bg);

// Temas integrados ("looks"): fijan apariencia, material, iconos, fondo y acento.
typedef struct {
    const char *name;
    uint8_t dark, glass, icon_style, wall, palette;
    uint8_t ar, ag, ab;   // acento
    uint8_t sr, sg, sb;   // acento claro
} flex_look_t;
#define FLEX_LOOK_N 8
extern const flex_look_t flex_looks[FLEX_LOOK_N];

#ifdef __cplusplus
}
#endif
