// Flex OS Ultra · kit de maquetacion adaptativa de las apps (docs/spec/02 §8.4; AppFramework.h:275-330).
// Logica pura (sin LVGL): margen, separacion, tamano de fuente que cabe y altura de linea. Las apps
// maquetan contra el lienzo real (ancho x alto del area util), no contra constantes de 480x800.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Caja util de una app: ancho w y alto h con minimos de 32 (uiBox)
void flex_ui_box(int canvas_w, int canvas_h, int *w, int *h);
// clamp(min(w,h)/24, 5, 22): a pantalla completa vertical (480x640) son 20
int flex_ui_pad(int w, int h);
// max(pad*3/4, 4)
int flex_ui_gap(int pad);
// Altura de linea -> tamano de fuente 1..5 (>=44 -> 5, >=33 -> 4, >=23 -> 3, >=14 -> 2, si no 1)
int flex_ui_font_h(int line_h);
// Altura aproximada de una linea de texto de tamano fs (reservar sitio): fs <= 1 ? 8 : 9 * fs
int flex_ui_line_h(int fs);
// Mayor tamano <= min(max_size, 5) cuyo ancho cabe en max_w (minimo 1); text_w(t, fs) mide el texto
typedef int (*flex_ui_textw_fn)(const char *t, int fs, void *ctx);
int flex_ui_font_fit(const char *t, int max_w, int max_size, flex_ui_textw_fn text_w, void *ctx);

#ifdef __cplusplus
}
#endif
