// Flex OS Ultra · iconos vectoriales (dibujados con primitivas de LVGL).
//
// Los 19 iconos de app de la version Arduino (FlexOS_Ultra_Icons.h) con sus
// colores de marca y proporciones exactas respecto al lado S, y los glifos del
// sistema (barra de estado, navegacion, cabecera). Ningun icono sale de su
// caja S x S. El fondo del icono es una superficie (flex_glass): Plano con
// brillo superior o Liquid Glass segun el estilo de icono elegido.
#pragma once

#include <stdint.h>
#include "flex_app_ids.h"
#include "flex_glass.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Color de marca del icono (fondo).
lv_color_t flex_app_icon_color(int id);

// Icono de app de lado S. bd: sobre que fondo esta (para el estilo Vidrio).
lv_obj_t *flex_app_icon_create(lv_obj_t *parent, int id, int32_t S, flex_backdrop_t bd);
void      flex_app_icon_set_id(lv_obj_t *icon, int id);

// Glifos del sistema: un objeto de tamano w x h que dibuja el glifo centrado en
// su color de texto (lv_obj_set_style_text_color).
typedef enum {
    FLEX_GLYPH_WIFI,          // tres arcos + punto; valor = barras (0..3), -1 = sin conexion
    FLEX_GLYPH_BATTERY,       // valor = nivel 0..100, -1 = sin medida (solo contorno)
    FLEX_GLYPH_BACK,          // triangulo de la barra de navegacion
    FLEX_GLYPH_HOME,          // anillo
    FLEX_GLYPH_RECENTS,       // cuadrado redondeado
    FLEX_GLYPH_CHEVRON_LEFT,  // "volver" de cabecera
    FLEX_GLYPH_CHEVRON_RIGHT, // fila de Ajustes
    FLEX_GLYPH_MENU,          // tres puntos verticales
    FLEX_GLYPH_PLUS,
    FLEX_GLYPH_MINUS,
    FLEX_GLYPH_CLOSE,
    FLEX_GLYPH_GEAR,
    FLEX_GLYPH_SUN,
    FLEX_GLYPH_MOON,
    FLEX_GLYPH_LOCK,
    FLEX_GLYPH_POWER,
    FLEX_GLYPH_SEARCH,
    FLEX_GLYPH_BELL,
    FLEX_GLYPH_AIRPLANE,
    FLEX_GLYPH_BLUETOOTH,
    FLEX_GLYPH_SPEAKER,       // valor = volumen 0..100
    FLEX_GLYPH_GLASS,         // dos laminas
    FLEX_GLYPH_CHECK,
    FLEX_GLYPH_EYE,           // ojo (drwGlyphEye); valor 0 = tachado (apagado)
    FLEX_GLYPH_INFO,          // "i" en un circulo
    FLEX_GLYPH_OPEN,          // flecha de abrir
    FLEX_GLYPH_TRASH,         // papelera
    FLEX_GLYPH_RING,          // anillo grueso (en Inicio)
    FLEX_GLYPH_CHEVRON_DOWN,  // cerrar el teclado de la caja
} flex_glyph_t;

lv_obj_t *flex_glyph_create(lv_obj_t *parent, flex_glyph_t g, int32_t w, int32_t h);
void      flex_glyph_set(lv_obj_t *obj, flex_glyph_t g, int32_t value);

#ifdef __cplusplus
}
#endif
