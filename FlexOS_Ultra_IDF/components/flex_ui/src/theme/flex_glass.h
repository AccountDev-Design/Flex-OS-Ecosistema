// Flex OS Ultra · superficies del sistema: Liquid Glass o Plano.
//
// Unico punto que decide el material de una superficie (uiSurfaceA de la
// version Arduino, docs/spec/02 §4.8). Ningun componente pinta vidrio por su
// cuenta: crea un lv_obj y llama a flex_surface().
//
// Liquid Glass sobre el fondo de pantalla: el panel muestra la parte del fondo
// YA DESENFOCADO que tiene detras (una vista sin copias sobre el backdrop de
// flex_wallmgr), encima el tinte con mezcla adaptativa a la luma de lo que hay
// debajo, el especular (blanco, 45 % superior) y el sombreado (negro, 55 %
// inferior), y el borde direccional (claro arriba, oscuro abajo). Moverlo es
// leer otro trozo del mismo backdrop: nunca arrastra la imagen de donde se
// compuso ni desenfoca pixeles de otra pantalla.
// Liquid Glass sobre un color liso (apps): el desenfoque de un color liso es
// ese color, asi que basta tinte + luz + borde (identico al resultado exacto).
// Plano: el color de la superficie.
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_SURF_CARD = 0,     // tarjeta apoyada en la pagina      (surf  / glass)
    FLEX_SURF_ELEVATED,     // menu, dialogo, tecla, chip        (surf2 / glass2)
    FLEX_SURF_ACCENT,       // accion primaria                   (acento)
    FLEX_SURF_WALL,         // tarjeta sobre el fondo de pantalla (WALLSURF / WALLSURF2)
    FLEX_SURF_TINT,         // color propio (icono de app): flex_surface_set_tint
} flex_surf_role_t;

typedef enum {
    FLEX_BD_FLAT = 0,       // sobre el color de pagina del tema
    FLEX_BD_HOME,           // sobre el fondo del escritorio
    FLEX_BD_LOCK,           // sobre el fondo del bloqueo
} flex_backdrop_t;

// Convierte obj en superficie. Respeta el radio de su estilo. El objeto deja de
// pintar su propio fondo (lo pinta la superficie).
void flex_surface(lv_obj_t *obj, flex_surf_role_t role, flex_backdrop_t bd);
void flex_surface_set_tint(lv_obj_t *obj, lv_color_t tint);
// Opacidad del material (animaciones): el panel aparece sin dejar de ser vidrio.
void flex_surface_set_opa(lv_obj_t *obj, lv_opa_t opa);
// Fuerza el material de esta superficie (p. ej. iconos: estilo de icono propio).
// -1 = el del tema, 0 = plano, 1 = vidrio, 2 = el estilo de icono (gIconStyle).
void flex_surface_force_material(lv_obj_t *obj, int material);
// Suelo de tinte para legibilidad sobre contenido ajeno (banner: 150).
void flex_surface_set_min_mix(lv_obj_t *obj, uint8_t min_mix);
// Color y opacidad del estilo Plano distintos de los del rol (p. ej. tarjetas
// del bloqueo: TH_SURF a215; dock: TH_SURF a90).
void flex_surface_set_flat(lv_obj_t *obj, lv_color_t color, lv_opa_t opa);
// Velo que la pantalla pone sobre el fondo antes que el panel (la pantalla de
// clave vela el wallpaper con rgb(8,10,18) a70): el vidrio lo incluye debajo.
void flex_surface_set_veil(lv_obj_t *obj, lv_color_t color, lv_opa_t opa);
// Brillo superior del estilo Plano de los iconos (blanco a22 en la mitad de arriba).
void flex_surface_set_flat_sheen(lv_obj_t *obj, bool on);

#ifdef __cplusplus
}
#endif
