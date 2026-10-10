// Flex OS Ultra · marco del sistema: barra de estado, barra de navegacion y
// utilidades de maquetacion comunes (docs/spec/02 §7 y §8.8-8.9).
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_SB_WALL = 0,    // escritorio: hora + fecha corta, colores sobre el fondo
    FLEX_SB_LOCK,        // bloqueo: solo Wi-Fi y bateria
    FLEX_SB_APP,         // marco de app: sobre el color de ventana (TH_NAV)
} flex_sb_kind_t;

// Barra de estado (480x46). Se refresca sola al cambiar el minuto, el tema o
// la conexion.
lv_obj_t *flex_statusbar_create(lv_obj_t *parent, flex_sb_kind_t kind);
void flex_statusbar_refresh_all(void);

// Barra de navegacion del sistema (en lv_layer_top: ninguna app puede taparla).
typedef enum {
    FLEX_NAV_HIDDEN = 0,
    FLEX_NAV_HOME,       // sobre el fondo, sin fondo propio
    FLEX_NAV_APP,        // franja de 64 px con el color de la barra
} flex_nav_ctx_t;
void flex_navbar_init(void);
void flex_navbar_set_ctx(flex_nav_ctx_t ctx);
flex_nav_ctx_t flex_navbar_ctx(void);
bool flex_navbar_flash_visible(void);   // destello de pulsacion de un boton (pruebas)

// Coloca una etiqueta de forma que el TOPE DE LAS MAYUSCULAS quede en y (las
// coordenadas de la version Arduino se refieren a eso, no a la caja de linea).
void flex_label_cap_y(lv_obj_t *label, int32_t y);
// Igual, centrada en x = cx.
void flex_label_cap_center(lv_obj_t *label, int32_t cx, int32_t y);
// Una sola linea de ancho w con "..." si no cabe (fgTextEllipsis). El alto se fija
// a una linea: con alto automatico LVGL parte el texto en varias y no pone los puntos.
void flex_label_one_line(lv_obj_t *label, int32_t w);

#ifdef __cplusplus
}
#endif
