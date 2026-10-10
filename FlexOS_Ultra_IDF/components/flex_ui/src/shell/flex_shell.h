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
    FLEX_SH_POWEROFF,     // "Apagar FlexOS?" y la animacion final (flex_poweroff_ui.c)
    FLEX_SH_SAFE,         // pantalla de Modo seguro (flex_safe_ui.c)
    FLEX_SH_FACTORY,      // asistente de restablecimiento de fabrica (flex_factory_ui.c)
    FLEX_SH_OOBE,         // primera configuracion (flex_oobe_ui.c)
} flex_shell_state_t;

// Construye el shell (tras flex_theme_init, flex_wallmgr_init, flex_i18n_init)
// y muestra el bloqueo.
void flex_shell_start(void);
flex_shell_state_t flex_shell_state(void);
lv_obj_t *flex_shell_screen(void);
int flex_shell_fg_app(void);   // app en primer plano aunque algo la tape (-1: ninguna). Pruebas.

void flex_shell_show_home(void);      // estado Inicio (sin animacion)
void flex_shell_lock(void);           // bloquear ahora (inactividad, boton)
void flex_shell_unlocked(void);       // el bloqueo termino (desliz o clave correcta)
bool flex_shell_lock_visible(void);   // la vista del bloqueo esta puesta (pruebas)
// Pantalla de la clave (flex_auth.c)
void flex_shell_auth_begin(void);     // estado AUTH, sin barra de navegacion
void flex_shell_reveal_prepare(void); // escritorio debajo y bloqueo fuera (revelado)
void flex_shell_home_shift(int32_t dx);   // temblor del revelado

// Vistas (las implementan flex_home.c y flex_lock.c)
lv_obj_t *flex_home_create(lv_obj_t *parent);
void flex_home_refresh(void);                         // cambio de minuto / tema
void flex_home_rebuild(void);                         // el modelo cambio (favoritas, ocultas)
bool flex_home_icon_area(int app_id, lv_area_t *out); // icono visible de la app (origen del zoom)
int32_t flex_home_scroll_x(void);                     // desplazamiento real de las paginas (pruebas)
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

// Panel rapido (flex_qs.c): capa global sobre el escritorio o una app.
void flex_qs_init(void);              // engancha el tactil (antes de LVGL)
void flex_qs_open(void);              // abrir con su animacion (pruebas, atajos)
void flex_qs_close_now(void);         // qsForceClose: sin animacion, descarta la edicion
bool flex_qs_is_open(void);
int  flex_qs_panel_y(void);           // borde de la cortina, 0..800
int  flex_qs_mode(void);              // 0 panel · 1 editor · 2 catalogo
int  flex_qs_visible_ids(uint8_t *out, int cap);   // controles que se ven ahora
uint8_t flex_qs_grows(void);
bool flex_qs_ctl_rect(int id, lv_area_t *out);      // en pantalla, modo actual
bool flex_qs_group_rect(lv_area_t *out);
int  flex_qs_catalog_index(int id);                // posicion en "Anadir un control" (-1)

// Avisos (flex_notif.c): banner flotante, Centro de notificaciones y No molestar.
// flex_notify/flex_notify_media/flex_say valen desde cualquier tarea (buzon de la UI).
void flex_notif_init(void);           // engancha el tactil (antes de LVGL)
void flex_notify(const char *title, const char *sub);        // sysNotify: historial + banner
void flex_notify_media(const char *title, const char *sub);  // igual, con icono de Multimedia
void flex_say(const char *app, const char *title, const char *sub);   // sysSay: en una app, solo banner
bool flex_notif_dnd(void);            // No molestar (flexphone/dnd)
void flex_notif_set_dnd(bool on);
int  flex_notif_count(void);
const char *flex_notif_latest_title(void);   // el mas reciente del historial (NULL si no hay)
bool flex_notif_banner_visible(void);
const char *flex_notif_banner_title(void);
int  flex_notif_queue_len(void);
bool flex_notif_center_open(void);    // abierto, arrastrandose o animandose
void flex_notif_center_show(void);    // abrir con su animacion (pruebas, atajos)
void flex_notif_center_close_now(void);   // fpcForceClose: sin animacion
// Lo que necesita el banner de los demas modulos
bool flex_shell_transition_active(void);  // la tarjeta de abrir/cerrar app esta a la vista
void flex_lock_refresh_widgets(void);     // tarjetas de la pantalla de bloqueo (aviso mas reciente)

// Apagado completo (flex_poweroff_ui.c)
void flex_poweroff_open(void);        // "Apagar FlexOS?" (desde el panel rapido)
void flex_poweroff_close_now(void);   // Cancelar: vuelve a donde se estaba (no durante la animacion)
bool flex_poweroff_active(void);
bool flex_poweroff_running(void);     // animacion final en marcha: ya no tiene vuelta
int  flex_poweroff_knob(void);        // 0..304 (pruebas)
void flex_shell_poweroff_begin(void); // estado POWEROFF, sin barra (recuerda de donde se venia)
void flex_shell_poweroff_end(void);   // vuelve a Inicio o a la app que estaba delante

// Modo seguro (flex_safe_ui.c; la decision de arranque esta en flex_system/flex_safeboot)
void flex_safe_open(void);            // la pantalla (arranque en Modo seguro o pildora del escritorio)
void flex_safe_close_now(void);       // a Inicio (limitado)
bool flex_safe_screen_active(void);
bool flex_safe_app_allowed(int id);   // lista blanca: Ajustes, Almacenamiento, Reloj, Calculadora
void flex_safe_deny_app(int id);      // "No disponible en Modo seguro" (1,8 s)
bool flex_safe_toast_visible(void);
void flex_shell_safe_begin(void);
void flex_shell_safe_end(void);
// Restablecimiento de datos de fabrica (flex_factory_ui.c)
bool flex_factory_reset_available(void);
void flex_factory_reset_open(bool from_safe);
void flex_factory_resume_boot(void);  // el arranque encontro un borrado a medias
bool flex_factory_active(void);
int  flex_factory_view(void);         // pruebas: 1 aviso, 2 escribir, 3 deslizar, 4 progreso, 5 fallo
int  flex_factory_shown_stage(void);
int  flex_factory_knob(void);
void flex_shell_factory_begin(void);
// Primera configuracion (flex_oobe_ui.c): NVS "oobe" = false
void flex_oobe_start(void);
bool flex_oobe_active(void);
int  flex_oobe_view(void);            // pruebas: 1 idioma, 2 nombre
const char *flex_oobe_name(void);
void flex_shell_oobe_begin(void);
void flex_shell_factory_end(flex_shell_state_t prev);   // Cancelar: a la app que estaba o a Inicio
void flex_shell_return_to_app(void);  // una verificacion pedida sobre la app se cancelo: vuelve a ella
// Modo kiosco (flex_kiosk.c; docs/spec/01a §5.14, 01c §12)
void flex_kiosk_load(void);           // NVS al arrancar (sin clave o app invalida: desactivado)
bool flex_kiosk_active(void);
int  flex_kiosk_app(void);            // app clavada (-1 sin kiosco)
void flex_kiosk_set_open(int app);    // pantalla "definir area excluida" (menu del icono)
bool flex_kiosk_set_active(void);
void flex_kiosk_boot(void);           // arranque con kiosco: la app sin bloqueo
void flex_kiosk_exit_now(void);       // tras la clave: borra el estado y va a Inicio
bool flex_kiosk_badge_visible(void);  // pruebas
// Modo edicion del escritorio (flex_home_edit.c)
void flex_home_edit_enter(void);
bool flex_home_edit_active(void);
void flex_home_edit_close_now(void);  // bloquear o suspender: sale guardando
int  flex_home_edit_drag(void);       // pruebas: icono agarrado (-1)
int  flex_home_edit_wsel(void);       // pruebas: widget seleccionado (-1)
const char *flex_home_edit_hint(void);
// Menu contextual del escritorio (flex_home_ctx.c): pulsacion larga en un icono de la rejilla
void flex_home_ctx_menu(int app_id, const lv_area_t *icon);
bool flex_home_ctx_open(void);

void flex_drawer_open(void);
void flex_drawer_close_now(void);     // sin animacion (bloquear, suspender)
bool flex_drawer_is_open(void);
void flex_recents_open(void);

#ifdef __cplusplus
}
#endif
