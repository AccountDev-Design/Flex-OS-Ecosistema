// Flex OS Ultra · marco de apps: registro, ciclo de vida y navegacion.
//
// Una app ES su fila del registro (FlexOS_Ultra_AppFramework.h:739): su id es
// el indice (enum IC_*), y ese id viaja a la NVS (escritorio, favoritas,
// ocultas, candados), asi que no se mueve nunca. El marco posee la barra de
// estado, la cabecera estandar y la barra de navegacion; la app solo construye
// su contenido en el lv_obj que recibe y atiende sus eventos.
//
// Ciclo de vida (docs/spec/02 §8.5): CLOSED -> RUNNING <-> SUSPENDED -> CLOSED.
// Solo una app corre a la vez. Suspender oculta su arbol de objetos y suelta lo
// pesado (on_suspend); volver la muestra sin recrearla (on_resume). Cerrar de
// verdad borra su arbol (on_close).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flex_icons.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Banderas (AppFramework.h:64)
#define FLEX_APP_CUSTOM_HEADER 0x01   // la app pinta su cabecera (y ocupa la franja de la barra de estado)
#define FLEX_APP_OWN_TOUCH     0x02   // gestiona todos sus toques (el sistema solo conserva la barra de abajo)
#define FLEX_APP_LAND          0x04   // dibuja en horizontal (Juegos)
#define FLEX_APP_FLEX          0x08   // maqueta contra el lienzo real (ventanas de Modo PC, pantalla completa)
#define FLEX_APP_BG_KEEP       0x10   // puede trabajar en segundo plano (no se desaloja si bg_work() dice que si)
#define FLEX_APP_IMMERSIVE     0x20   // sabe ir a pantalla completa

enum { FLEX_CAT_ESSENTIALS = 0, FLEX_CAT_MEDIA, FLEX_CAT_PRODUCTIVITY, FLEX_CAT_SYSTEM, FLEX_CAT_FUN, FLEX_CAT_N };

// Geometria del marco (AppFramework.h:48-50)
#define FLEX_STATUS_H   46    // barra de estado de las apps con marco estandar
#define FLEX_WIN_TOP    96    // estado + cabecera estandar
#define FLEX_NAV_H      64    // barra de navegacion (modo Botones)
#define FLEX_WIN_BOT    (800 - FLEX_NAV_H)

typedef struct flex_app_ctx flex_app_ctx_t;

typedef struct {
    // Construye la UI en root (480x800, transparente). area: rectangulo util
    // (debajo de la cabecera estandar o de la barra de estado). Obligatorio.
    void (*on_create)(lv_obj_t *root, const lv_area_t *area);
    void (*on_resume)(void);          // vuelve a primer plano sin recrear
    void (*on_suspend)(void);         // pasa a segundo plano: soltar lo pesado
    void (*on_close)(void);           // se borra su arbol de objetos despues
    bool (*on_back)(void);            // "atras": true si lo consumio (capa o pantalla interna)
    bool (*bg_work)(void);            // trabajo real en segundo plano
    bool (*dirty)(void);              // cambios sin guardar
    void (*on_minute)(void);          // cambio de minuto (relojes)
    // Sesion (docs/spec/01c 14): el minimo estado de interfaz que merece sobrevivir a un reinicio
    // (vista, scroll, seleccion; nunca el documento). Devuelve false si no pudo prepararlo.
    bool (*save_session)(void *out, size_t cap, size_t *len);
    // La primera vez que se abre la app, ANTES de on_create. data = NULL: no hay sesion valida
    // (otra version, otra app o corrupta) y debe abrir en un estado seguro.
    void (*load_session)(const void *data, size_t len);
    // Soltar recursos PESADOS y RECONSTRUIBLES sin perder estado (miniaturas, fotogramas, buffers de
    // medios). Solo se llama con la app SUSPENDIDA y sin trabajo real en segundo plano. Devuelve una
    // pista (!= 0 si solto algo); los bytes se MIDEN desde fuera.
    size_t (*shed)(void);
} flex_app_ops_t;

typedef struct {
    uint8_t id;                       // IC_*
    uint8_t flags;
    uint8_t cat;                      // FLEX_CAT_*
    bool    fav_default;              // en Inicio de fabrica (APP_DEF_FAV)
    uint8_t weight;                   // 0 ligera · 1 media · 2 pesada (admision de memoria)
    const flex_app_ops_t *ops;        // NULL = aun sin migrar (pantalla "en construccion")
} flex_app_def_t;

const flex_app_def_t *flex_app_def(int id);
uint32_t flex_app_factory_fav(void);   // bitmask de APP_DEF_FAV (0x283FF)
const char *flex_app_cat_name(int cat);

// Navegacion del sistema (Core.h:551-704)
void flex_app_open(int id, const lv_area_t *from_icon);   // from_icon: para la animacion Zoom (NULL = centro)
void flex_app_close(void);                                 // suspende y vuelve a Inicio
// Abrir desde Inicio o la caja: con candado (y clave puesta) pide la clave
// antes (LSU_AFTER_OPENAPP); al cancelar se queda en Inicio.
void flex_app_launch(int id, const lv_area_t *from_icon);
void flex_sys_back(void);
void flex_sys_home(void);
void flex_sys_recents(void);
int  flex_app_current(void);                               // id en primer plano o -1
bool flex_app_is_open(int id);                             // suspendida o en primer plano
void flex_app_terminate(int id);                           // cerrar de verdad (Recientes); no cierra si no pudo guardar su sesion
// force = true (restablecimiento): cierra aunque no se pueda guardar. false: la app sigue viva.
bool flex_app_terminate_ex(int id, bool force);

// Sesiones (flex_session.c). Las apps llaman a mark_dirty cuando cambian algo: el volcado ocurre
// 1200 ms despues (tope 30 s) y nunca con el dedo apoyado.
// Memoria (flex_mem.c, docs/spec/01c 17): medir, soltar, admitir y desalojar por presupuesto
void flex_mem_start(void);
bool flex_mem_admit_app(int id);              // memAdmitApp: false = "no se abre ahora" (ya avisado)
void flex_mem_register_cache(const char *name, size_t (*shed)(void), bool (*guard)(void));
bool flex_mem_app_known(int id);              // huella medida (sin medida la interfaz escribe "--")
uint32_t flex_mem_app_bytes(int id);
bool flex_mem_app_shed(int id);               // "Estado guardado" en vez de "Pausada"
int  flex_mem_level(void);                    // FLEXMEM_LV_*: nivel sostenido
int  flex_app_life(int id);                   // 0 cerrada, 1 en marcha, 2 suspendida
// "Optimizar Flex OS" (flex_optimize.c): lo abren Almacenamiento y Device Care; on_done = a donde volver
void flex_optimize_open(void (*on_done)(void));
bool flex_optimize_active(void);
void flex_optimize_close_now(void);
void flex_session_start(void);
void flex_session_mark_dirty(int id);
void flex_session_flush_now(void);
bool flex_session_pending(int id);

// Para las apps: cabecera compartida (uiHdr: zonas 56x56, titulo recortado).
lv_obj_t *flex_app_header(lv_obj_t *parent, const char *title, bool menu, lv_event_cb_t on_menu);
// Pantalla "en construccion" de una app aun no migrada.
void flex_app_placeholder(lv_obj_t *root, const lv_area_t *area, int id);

#ifdef __cplusplus
}
#endif
