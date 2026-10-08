// Flex OS Ultra · shell: pantallas del sistema y su orden.
//
//   pantalla unica ─┬─ escritorio (fondo, paginas, dock)
//                   ├─ apps (una raiz por app abierta; solo una visible)
//                   └─ bloqueo (encima de todo mientras esta bloqueado)
//   lv_layer_top ───── barra de navegacion, tarjeta de transicion, overlays
//
// Estado logico != estado visual (docs/spec/02 §8.7): cada intencion cambia el
// estado en el acto y la animacion es una capa que no manda sobre nada.
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_SH_LOCK = 0,
    FLEX_SH_HOME,
    FLEX_SH_APP,
    FLEX_SH_OVERLAY,      // caja de apps, Recientes, personalizar... (los gestionan sus modulos)
} flex_shell_state_t;

// Construye el shell (tras flex_theme_init, flex_wallmgr_init, flex_i18n_init)
// y muestra el bloqueo.
void flex_shell_start(void);
flex_shell_state_t flex_shell_state(void);
lv_obj_t *flex_shell_screen(void);

void flex_shell_show_home(void);      // estado Inicio (sin animacion)
void flex_shell_lock(void);           // bloquear ahora (inactividad, boton)
void flex_shell_unlocked(void);       // el bloqueo termino (desliz o clave correcta)

// Vistas (las implementan flex_home.c y flex_lock.c)
lv_obj_t *flex_home_create(lv_obj_t *parent);
void flex_home_refresh(void);                         // cambio de minuto / tema
bool flex_home_icon_area(int app_id, lv_area_t *out); // icono visible de la app (origen del zoom)
lv_obj_t *flex_lock_create(lv_obj_t *parent);
void flex_lock_refresh(void);
void flex_lock_reset(void);                           // vuelve a la posicion de reposo

// Pantallas superpuestas opcionales (si su modulo no esta, no hacen nada).
void flex_drawer_open(void);
void flex_recents_open(void);

#ifdef __cplusplus
}
#endif
