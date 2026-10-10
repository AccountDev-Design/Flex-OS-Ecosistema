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

// Para el Modo edicion (flex_home_edit.c): la raiz del escritorio y las paginas
lv_obj_t *flex_home_root_obj(void);
void flex_home_pages_show(bool on);

#ifdef __cplusplus
}
#endif
