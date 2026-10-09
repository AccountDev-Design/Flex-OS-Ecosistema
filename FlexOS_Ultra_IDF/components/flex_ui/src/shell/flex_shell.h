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
    FLEX_SH_AUTH,         // pantalla de la clave (flex_auth.c)
} flex_shell_state_t;

// Construye el shell (tras flex_theme_init, flex_wallmgr_init, flex_i18n_init)
// y muestra el bloqueo.
void flex_shell_start(void);
flex_shell_state_t flex_shell_state(void);
lv_obj_t *flex_shell_screen(void);

void flex_shell_show_home(void);      // estado Inicio (sin animacion)
void flex_shell_lock(void);           // bloquear ahora (inactividad, boton)
void flex_shell_unlocked(void);       // el bloqueo termino (desliz o clave correcta)
// Pantalla de la clave (flex_auth.c)
void flex_shell_auth_begin(void);     // estado AUTH, sin barra de navegacion
void flex_shell_reveal_prepare(void); // escritorio debajo y bloqueo fuera (revelado)
void flex_shell_home_shift(int32_t dx);   // temblor del revelado

// Vistas (las implementan flex_home.c y flex_lock.c)
lv_obj_t *flex_home_create(lv_obj_t *parent);
void flex_home_refresh(void);                         // cambio de minuto / tema
void flex_home_rebuild(void);                         // el modelo cambio (favoritas, ocultas)
bool flex_home_icon_area(int app_id, lv_area_t *out); // icono visible de la app (origen del zoom)
lv_obj_t *flex_lock_create(lv_obj_t *parent);
void flex_lock_refresh(void);
void flex_lock_reset(void);                           // vuelve a la posicion de reposo
void flex_lock_drop_in(void);                         // cae desde arriba (bloqueo por inactividad)
void flex_lock_set_return_app(int app);               // al acertar, volver a esta app (-1: escritorio)

// Energia (flex_power.c): suspension por doble toque y bloqueo por inactividad
void flex_power_init(void);
bool flex_power_suspended(void);
void flex_power_suspend(void);
void flex_power_wake(void);
uint32_t flex_power_autolock_ms(void);

// Capas superpuestas al escritorio (caja de apps, Recientes, Personalizar):
// mientras una esta abierta el estado es OVERLAY y Atras/Inicio/Recientes van
// a ella.
typedef struct {
    void (*on_back)(void);
    void (*on_home)(void);
    void (*on_recents)(void);
} flex_overlay_ops_t;
void flex_shell_overlay_begin(const flex_overlay_ops_t *ops);
void flex_shell_overlay_end(void);   // vuelve al estado Inicio

// Pantallas superpuestas opcionales (si su modulo no esta, no hacen nada).
// Recientes (flex_recents.c)
void flex_recents_note_suspend(int app, lv_obj_t *root, bool capture);   // la app sale del primer plano
void flex_recents_note_closed(int app);                                   // la app se cerro de verdad
void flex_recents_close_now(void);
bool flex_recents_is_open(void);
int  flex_recents_count(void);
int  flex_recents_app_at(int i);
bool flex_recents_has_thumb(int i);
uint32_t flex_app_last_used(int id);   // lv_tick de la ultima vez en primer plano (0 = nunca)

void flex_drawer_open(void);
void flex_drawer_close_now(void);     // sin animacion (bloquear, suspender)
bool flex_drawer_is_open(void);
void flex_recents_open(void);

#ifdef __cplusplus
}
#endif
