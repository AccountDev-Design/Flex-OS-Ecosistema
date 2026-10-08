// Flex OS Ultra · fondos de pantalla en memoria y su version desenfocada.
//
// Escritorio y bloqueo tienen cada uno su fondo (480x800 RGB565 en PSRAM) y el
// mismo fondo ya desenfocado con el radio del vidrio vigente: es el "backdrop"
// del que lee todo panel Liquid Glass que este sobre el fondo. Se regeneran
// solo al cambiar de fondo, de intensidad del vidrio o de modo eficiente,
// nunca por cuadro (docs/spec/02 §4.5 y §4.12).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_WALL_HOME = 0,
    FLEX_WALL_LOCK = 1,
} flex_wall_which_t;

// Genera los fondos y backdrops segun las preferencias. Llamar tras flex_theme_init().
void flex_wallmgr_init(void);

// Imagen del fondo para un lv_image (NULL si no hay memoria).
const lv_image_dsc_t *flex_wallmgr_image(flex_wall_which_t w);
// Fondo desenfocado (480x800 RGB565) o NULL si no hay (sin memoria o sin vidrio).
const uint16_t *flex_wallmgr_backdrop(flex_wall_which_t w);
// Luma media (dominio de flex_glass_luma) de la region del backdrop bajo un area.
bool flex_wallmgr_backdrop_luma(flex_wall_which_t w, const lv_area_t *a, uint32_t *sum, int *n);

// Memoria que ocupan ahora (para Device Care y la optimizacion).
size_t flex_wallmgr_bytes(void);
// Suelta los backdrops (se rehacen al volver a necesitarse). Devuelve bytes liberados.
size_t flex_wallmgr_shed(void);

#ifdef __cplusplus
}
#endif
