// Flex OS Ultra · iconos del panel rapido (QuickPanel.h:350-510).
//
// Los mismos dibujos con primitivas de LVGL, en un objeto de lado S que pinta
// el glifo centrado con su color de texto. Arduino "recortaba" los huecos (luna,
// engranaje, camara, carpeta, candado) pintandolos con el color del pixel de
// debajo; aqui los huecos son de verdad (el glifo no pinta ahi), asi que no
// dependen del material que haya detras.
#pragma once

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_QI_WIFI = 0, FLEX_QI_AIRPLANE, FLEX_QI_BLE, FLEX_QI_SUN, FLEX_QI_MOON, FLEX_QI_GLASS, FLEX_QI_GLASSFX,
    FLEX_QI_RESET, FLEX_QI_BATTSAVE, FLEX_QI_GEAR, FLEX_QI_SIGNAL, FLEX_QI_MONITOR, FLEX_QI_UPDATE, FLEX_QI_FOLDER,
    FLEX_QI_CAMERA, FLEX_QI_IMAGE, FLEX_QI_STOPWATCH, FLEX_QI_LOCK, FLEX_QI_POWER, FLEX_QI_CLOCK, FLEX_QI_PENCIL,
    FLEX_QI_PLUS, FLEX_QI_MINUS, FLEX_QI_SPEAKER, FLEX_QI_MUTE, FLEX_QI_DND, FLEX_QI_ROTATE, FLEX_QI_NONE,
} flex_qi_t;

// Icono del control id del panel (FLEX_QS_*).
flex_qi_t flex_qs_ctl_icon(int id);

// Objeto S x S que dibuja el icono con lv_obj_set_style_text_color. value: dato
// que cambia el dibujo (altavoz: volumen 0..100).
lv_obj_t *flex_qi_create(lv_obj_t *parent, flex_qi_t icon, int32_t s);
void flex_qi_set(lv_obj_t *obj, flex_qi_t icon, int32_t value);

#ifdef __cplusplus
}
#endif
