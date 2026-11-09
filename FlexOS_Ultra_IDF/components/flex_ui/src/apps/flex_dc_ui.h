// Flex OS Ultra · pinta con LVGL las operaciones de dibujo de Device Care (flex_dc_screens): superficies del sistema, vectores (anillos, arcos, trazos,
// triangulos, circulos, rectangulos) en UN objeto de dibujo y los textos como etiquetas.
#pragma once

#include "flex_dc_screens.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_color_t flex_dc_ui_color(int role);
// Crea un contenedor transparente en (0, 0) de `parent` con todo lo de `pg`. Orden: superficies, vectores, textos.
lv_obj_t *flex_dc_ui_render(lv_obj_t *parent, const flex_dc_page_t *pg);
lv_obj_t *flex_dc_ui_render_h(lv_obj_t *parent, const flex_dc_page_t *pg, int height);   // con alto propio (la lista del historial, que se desplaza)
// Actualiza un contenedor ya creado con una pagina nueva. Si solo cambian los vectores (anillos que barren, latidos, celdas que se encienden)
// o el contenido de los textos, cambia solo eso y devuelve true; si cambian superficies o textos devuelve false y quien llama lo reconstruye.
bool flex_dc_ui_update(lv_obj_t *box, const flex_dc_page_t *pg);
// Pruebas: cuantas veces se ha creado un contenedor entero / actualizado solo vectores (desde el arranque)
int flex_dc_ui_builds(void);
int flex_dc_ui_vec_updates(void);

#ifdef __cplusplus
}
#endif
