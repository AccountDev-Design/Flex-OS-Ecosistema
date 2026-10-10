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

#ifdef __cplusplus
}
#endif
