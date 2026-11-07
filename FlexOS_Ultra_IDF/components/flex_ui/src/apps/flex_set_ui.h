// Flex OS Ultra · pinta con LVGL las operaciones de dibujo del modelo de Ajustes (compartido por Ajustes y Ajustes del teclado).
#pragma once

#include "flex_set_model.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *flex_set_box(lv_obj_t *p, int x, int y, int w, int h, int r);
// Crea los objetos de una pagina. `y_shift` pasa de la y del contenido a la del padre; `cards[]` recoge las tarjetas por orden de creacion; con click_cb
// cada tarjeta es tocable y lleva su indice en el user_data. Devuelve cuantas tarjetas hizo.
int flex_set_ops_render(lv_obj_t *parent, const flex_set_page_t *pg, int y_shift, lv_obj_t **cards, int ncards, lv_event_cb_t click_cb);

#ifdef __cplusplus
}
#endif
