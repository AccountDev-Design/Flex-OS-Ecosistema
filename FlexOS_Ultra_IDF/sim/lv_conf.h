// LVGL 9.6 para el simulador del PC. Mismos valores que sdkconfig.defaults
// (seccion LVGL) para que la UI se dibuje igual que en la placa; lo no
// definido aqui toma el valor por defecto de LVGL, igual que en ESP-IDF.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB
#define LV_USE_OS               LV_OS_NONE
#define LV_DEF_REFR_PERIOD      16
#define LV_DRAW_BUF_ALIGN       64
#define LV_DRAW_BUF_STRIDE_ALIGN 1

#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT    1

// Interfaz completa (Fase 3): mismos valores que sdkconfig.defaults
#define LV_GRADIENT_MAX_STOPS   4
#define LV_USE_FONT_COMPRESSED  1
#define LV_THEME_DEFAULT_DARK   1

// Solo en el simulador: render de referencia para comparar con lo "mostrado"
#define LV_USE_SNAPSHOT         1

#define LV_USE_VECTOR_GRAPHIC   0
#define LV_USE_THORVG           0
#define LV_USE_THORVG_INTERNAL  0
#define LV_BUILD_EXAMPLES       0
#define LV_BUILD_DEMOS          0

#endif
