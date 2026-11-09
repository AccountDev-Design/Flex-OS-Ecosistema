// Flex OS Ultra · pinta con LVGL las operaciones de dibujo de Almacenamiento y Archivos (flex_stor_model / flex_files_model).
#pragma once

#include "flex_stor_model.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Color de un rol FSC_* del modelo (rgb = RGB565 si el rol es FSC_RGB)
lv_color_t flex_st_ui_color(int role, uint16_t rgb);
// Crea los objetos de la pagina dentro de `parent`: sin eventos (quien llama pone los suyos sobre las zonas). dx/dy = desplazamiento de
// las coordenadas del modelo al padre. Devuelve el numero de objetos creados.
int flex_st_ui_render(lv_obj_t *parent, const flex_st_page_t *pg, int dx, int dy);

#ifdef __cplusplus
}
#endif
