// Flex OS Ultra · kit de archivos compartido (docs/spec/03 §5; FlexOS_Ultra_FileKit.h de Arduino).
//
// Piezas de interfaz que usan Notas, Paint y Archivos: menu contextual, dialogo de nombre con el teclado del
// sistema, confirmacion de borrado, papelera y pantalla de "sin almacenamiento". No simulan nada: las acciones
// las ejecuta quien las usa con flex_fs_*_async (la papelera, ella misma) y la lista se vuelve a leer del disco.
//
// Cada capa es un hijo de `root` (la raiz de la app) de 480x800, con prioridad fija como en Arduino: papelera,
// confirmacion, nombre, menu, contenido. Los resultados llegan por callback DESPUES de cerrar la capa, asi que
// el callback puede abrir otra. flex_fk_close_all(root) cierra las cuatro y suelta la lista de la papelera: las
// apps lo llaman al entrar, suspenderse y cerrarse. No hay estado global salvo la papelera abierta (una a la vez).
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_FK_ACT_SEL   0   // "Seleccionar"
#define FLEX_FK_ACT_DEL   1   // "Eliminar"
#define FLEX_FK_ACT_REN   2   // "Renombrar"
#define FLEX_FK_ACT_TRASH 3   // "Papelera"
#define FLEX_FK_ACT_CLOUD 4   // "Subir a Flex Cloud" (solo Archivos, sobre un fichero)

// ---- menu contextual -----------------------------------------------------------------------------------
// act = FLEX_FK_ACT_* o -1 (toque fuera del panel: se cierra sin accion).
typedef void (*flex_fk_menu_cb_t)(int act, void *user);
lv_obj_t *flex_fk_menu_open(lv_obj_t *root, int px, int py, bool cloud, flex_fk_menu_cb_t cb, void *user);

// ---- dialogo de nombre -------------------------------------------------------------------------------
// name = NULL si se cancela (chevron o "Guardar" con el campo vacio). Nunca contiene '/'; maximo 47 bytes.
typedef void (*flex_fk_name_cb_t)(const char *name, void *user);
lv_obj_t *flex_fk_name_open(lv_obj_t *root, const char *title, const char *initial, const char *hint,
                            flex_fk_name_cb_t cb, void *user);

// ---- confirmacion --------------------------------------------------------------------------------------
typedef void (*flex_fk_ask_cb_t)(bool ok, void *user);
lv_obj_t *flex_fk_ask_open(lv_obj_t *root, const char *msg, const char *sub, flex_fk_ask_cb_t cb, void *user);

// ---- papelera ---------------------------------------------------------------------------------------------
typedef void (*flex_fk_exit_cb_t)(void *user);
// on_exit se llama al salir (chevron): el llamante recarga su lista. Una papelera a la vez.
lv_obj_t *flex_fk_trash_open(lv_obj_t *root, flex_fk_exit_cb_t on_exit, void *user);
int flex_fk_trash_count(void);   // elementos cargados (-1: cerrada); para pruebas
int flex_fk_trash_selected(void);
bool flex_fk_trash_ready(void);   // la lista ya llego del disco

// ---- pantalla "sin almacenamiento" (fkNoFsScreen) -----------------------------------------------------
lv_obj_t *flex_fk_nofs_screen(lv_obj_t *root, const char *title, flex_fk_exit_cb_t on_back, void *user);

// ---- texto ajustado a una caja (fkTextBox): una etiqueta con las lineas ya cortadas por caracteres -----------
lv_obj_t *flex_fk_text_box(lv_obj_t *parent, int x, int y, int w, int h, const char *s, int fs, lv_color_t color);

// ---- cierre ----------------------------------------------------------------------------------------------
void flex_fk_close_all(lv_obj_t *root);   // fkCloseAll
bool flex_fk_any(lv_obj_t *root);         // hay una capa abierta (las que se estan cerrando no cuentan)
int flex_fk_layers(lv_obj_t *root);       // cuantas capas abiertas (la papelera + su confirmacion = 2); para pruebas

#ifdef __cplusplus
}
#endif
