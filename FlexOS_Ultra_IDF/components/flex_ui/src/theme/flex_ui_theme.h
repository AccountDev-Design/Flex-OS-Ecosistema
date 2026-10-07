// Flex OS Ultra · sistema de diseno (tokens). Fuente unica de medidas y
// colores de la interfaz LVGL a 480x800. La Fase 3 lo amplia (tema Liquid
// Glass, tipografia Outfit); la Fase 1 solo usa lo necesario para la prueba.
#pragma once

#include "lvgl.h"

#define FLEX_UI_W               480
#define FLEX_UI_H               800

#define FLEX_UI_GRID            8       // rejilla base
#define FLEX_UI_MARGIN          16      // margen lateral
#define FLEX_UI_MARGIN_WIDE     24
#define FLEX_UI_STATUS_H        36      // barra de estado
#define FLEX_UI_NAV_H           48      // barra de navegacion
#define FLEX_UI_TOUCH_MIN       48      // objetivo tactil minimo
#define FLEX_UI_RADIUS_S        12
#define FLEX_UI_RADIUS_M        20
#define FLEX_UI_RADIUS_L        28

#define FLEX_UI_COLOR_BG        lv_color_hex(0x0B0F14)
#define FLEX_UI_COLOR_SURFACE   lv_color_hex(0x141B24)
#define FLEX_UI_COLOR_OUTLINE   lv_color_hex(0x2A3A4D)
#define FLEX_UI_COLOR_GRID      lv_color_hex(0x1A2430)
#define FLEX_UI_COLOR_TEXT      lv_color_hex(0xE8EEF5)
#define FLEX_UI_COLOR_TEXT_DIM  lv_color_hex(0x8A9AAD)
#define FLEX_UI_COLOR_ACCENT    lv_color_hex(0x4DA3FF)
#define FLEX_UI_COLOR_WARN      lv_color_hex(0xFFB547)
#define FLEX_UI_COLOR_ERROR     lv_color_hex(0xFF5D5D)
#define FLEX_UI_COLOR_OK        lv_color_hex(0x46D18C)
