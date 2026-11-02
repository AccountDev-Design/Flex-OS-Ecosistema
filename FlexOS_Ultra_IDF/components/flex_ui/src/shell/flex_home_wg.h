// Flex OS Ultra · widgets del escritorio (flex_home_widgets.c).
#pragma once

#include "flex_home_model.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compone el widget en la pagina (y relativa a la franja que empieza en band_top).
// Sus datos se refrescan solos cada 2 s mientras exista.
void flex_home_wg_build(lv_obj_t *page, const flex_home_wg_t *wg, int32_t band_top);

// Vista previa REAL del widget reducida a bw x bh dentro de box (Personalizar inicio > Widgets).
void flex_home_wg_build_preview(lv_obj_t *box, const flex_home_wg_t *wg, int32_t bw, int32_t bh);
lv_obj_t *flex_home_page_obj(int p);   // la pagina p del escritorio real (NULL si no existe)

// Para el Modo edicion (flex_home_edit.c): la raiz del escritorio y las paginas
lv_obj_t *flex_home_root_obj(void);
void flex_home_pages_show(bool on);

#ifdef __cplusplus
}
#endif
