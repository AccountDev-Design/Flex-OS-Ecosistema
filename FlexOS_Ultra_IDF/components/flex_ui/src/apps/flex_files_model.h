// Flex OS Ultra · Archivos (explorador local): estado, toques y contenido de las filas (docs/spec/03 §8; FlexOS_Ultra_AppFiles.h).
// C portable, con pruebas de host frente a filesTick / filesReload / filesRowY / filesGoUp / filesOpenEntry / filesMenuAction de Arduino
// compilados tal cual. El modelo NO toca el disco ni dibuja: decide que significa cada toque y entrega las operaciones de cada fila con las
// coordenadas exactas de Arduino; la interfaz LVGL las pinta y ejecuta lo que el modelo pide (abrir una carpeta, borrar, renombrar...).
//
// Solo la mitad LOCAL ("Este dispositivo"). La pestana "Flex Cloud" y la accion "Subir a Flex Cloud" dependen del servicio de nube
// (inventario A04c): el modelo las deja caer en acciones que la interfaz atiende con un aviso honesto.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flex_fs_scan.h"
#include "flex_stor_model.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- geometria (480x800) --------------------------------------------------------------------------------------------------------------
#define FLEX_FILES_HDR_H     76                          // UIHDR_H
#define FLEX_FILES_ZONE      56                          // UIHDR_ZONE: zonas tactiles de la cabecera
#define FLEX_FILES_SEG_Y     (FLEX_FILES_HDR_H - 2)      // selector "Este dispositivo | Flex Cloud"
#define FLEX_FILES_SEG_H     36
#define FLEX_FILES_TOP       (128 + 40)                  // primera fila
#define FLEX_FILES_VP_TOP    (FLEX_FILES_HDR_H + 34 + 40)   // primera fila del area con scroll: bajo la cabecera, el selector y la ruta
#define FLEX_FILES_RH        70
#define FLEX_FILES_BAR_Y     (800 - 128)                 // barra de seleccion
#define FLEX_FILES_BAR_H     60
#define FLEX_FILES_MAX       FLEX_FSX_LIST_SCAN
#define FLEX_FILES_LONG_MS   550
#define FLEX_FILES_LONG_PX   14

typedef struct {
    char dir[FLEX_FSX_PATH_MAX];       // filesDir ("/" = raiz)
    flex_fsx_ent_t *ent;               // la lista de filesList (la guarda quien usa el modelo)
    int n;                             // filesN
    size_t total;                      // entradas leidas de la carpeta (si total > n, la lista esta cortada)
    int sel;                           // filesSelIdx
    bool multi;                        // filesMulti
    uint8_t mark[FLEX_FILES_MAX];      // filesMask
} flex_files_t;

void flex_files_enter_at(flex_files_t *f, const char *dir);   // filesEnterAt: desde la raiz o la carpeta dada, sin seleccion
// /System/Media es la biblioteca por dentro (catalogo, miniaturas, carpeta protegida): ni se lista ni se entra (filesIsLibraryDir)
bool flex_files_is_library(const char *dir);
// filesReload tras leer la carpeta: si filesDir cae en la biblioteca se redirige a /System; en /System se quita la biblioteca de la lista
// conservando el orden. `dir` es lo que hay que leer (ya redirigido). Devuelve la carpeta a leer.
const char *flex_files_reload_dir(flex_files_t *f);
void flex_files_set_list(flex_files_t *f, const flex_fsx_ent_t *src, int n, size_t total);   // copia + filtro + sel fuera de rango
bool flex_files_has_up(const flex_files_t *f);                 // filesHasUp
int flex_files_rows(const flex_files_t *f);                    // filas dibujadas (con ".." si hay)
int flex_files_row_y(int row);                                 // filesRowY sin desplazamiento (y de pantalla con scroll 0)
void flex_files_path_of(const flex_files_t *f, int i, char *out, size_t n);   // filesPathOf
void flex_files_end_multi(flex_files_t *f);                    // tras mover lo marcado a la papelera: fuera del modo seleccion
int flex_files_seg_hit(int px, int py);                        // filesSegHit: -1 fuera, 0 "Este dispositivo", 1 "Flex Cloud"
void flex_files_go_up(flex_files_t *f);                        // filesGoUp: corta en el ultimo '/', nunca por encima de la raiz

// ---- toque -----------------------------------------------------------------------------------------------------------------------------------------
typedef enum {
    FILES_A_NONE = 0,
    FILES_A_MENU,          // menu contextual en (x, y); `cloud` = incluye "Subir a Flex Cloud" (idx = fila o -1)
    FILES_A_EXIT,          // volver a Almacenamiento
    FILES_A_CLOUD_TAB,     // selector "Flex Cloud"
    FILES_A_LOCAL_TAB,     // selector "Este dispositivo" (desde la nube)
    FILES_A_MULTI_EXIT,    // "Salir" de la barra de seleccion
    FILES_A_MULTI_TRASH,   // "Papelera" de la barra: mover lo marcado a la papelera (marked[] en idx..)
    FILES_A_ENTER,         // se entro en una carpeta (dir ya cambiado): releer
    FILES_A_UP,            // ".."
    FILES_A_TOGGLE,        // marca / desmarca la fila idx (modo seleccion)
    FILES_A_OPEN_MEDIA,    // abrir idx en su visor
    FILES_A_NOTIFY,        // aviso: `text`
    FILES_A_UPLOAD,        // subir idx a Flex Cloud
    FILES_A_ASK_DELETE,    // "¿Borrar definitivamente?" sobre idx
    FILES_A_NAME,          // renombrar idx (`text` = el nombre sin extension)
    FILES_A_TRASH_ONE,     // mover idx a la papelera
    FILES_A_OPEN_TRASH,    // abrir la papelera
    FILES_A_DELETE,        // borrar idx de verdad (tras confirmar)
    FILES_A_RENAME,        // renombrar idx a `text` (tras el dialogo)
} flex_files_act_t;
typedef struct {
    int act;               // FILES_A_*
    int idx;
    int x, y;              // posicion del menu
    bool cloud;            // el menu lleva "Subir a Flex Cloud"
    char text[FLEX_FSX_PATH_MAX];
} flex_files_out_t;

// Toque corto en (x, y) con la lista desplazada `scroll`. vp_bottom = borde inferior de la lista (736 con la barra de botones).
void flex_files_tap(flex_files_t *f, int scroll, int vp_bottom, int x, int y, flex_files_out_t *out);
// Maximo desplazamiento de la lista con view_bottom como borde inferior (filesMaxScroll: Arduino usa 720; la lista de IDF acaba en la barra de botones)
int flex_files_max_scroll(const flex_files_t *f, int view_bottom);
// Pulsacion larga: el dedo lleva `held_ms` apoyado desde (sx, sy) y esta en (x, y). true si abre el menu de una fila
// (out->idx, posicion (x, y - 40) y `cloud` si es un fichero). Con una lista que se desplaza, mas de 8 px en vertical ya es un arrastre
// (`scrollable`) y no hay pulsacion larga.
bool flex_files_long(flex_files_t *f, int scroll, int vp_bottom, bool scrollable, int sx, int sy, int x, int y, unsigned held_ms,
                     flex_files_out_t *out);
// Menu contextual (FLEX_FK_ACT_*) y resultados de los dialogos
void flex_files_menu_act(flex_files_t *f, int act, flex_files_out_t *out);
void flex_files_ask_done(flex_files_t *f, bool ok, flex_files_out_t *out);                 // filesTick: fkAskTick() == 1
void flex_files_name_done(flex_files_t *f, const char *name, flex_files_out_t *out);       // filesTick: fkNameTick() == 1
// Abrir un fichero (filesOpenEntry): medios a su visor, formato reconocido sin visor -> aviso, el resto -> menu en (180, 200)
void flex_files_open_entry(const flex_files_t *f, int i, flex_files_out_t *out);

// ---- contenido de las filas ------------------------------------------------------------------------------------------------------------------
// Operaciones de la fila `row` (0 = ".." si lo hay) con su borde superior en `y`: tarjeta, icono, nombre y subtitulo
void flex_files_row_ops(const flex_files_t *f, int row, int y, flex_st_page_t *pg);
// Barra de seleccion (con "Selección", "Papelera" y "Salir")
void flex_files_bar_ops(flex_st_page_t *pg);
// Texto de la carpeta vacia y la ruta
#define FLEX_FILES_EMPTY_TEXT "Carpeta vac\xC3\xAD" "a"
// Subtitulo de una entrada: "N elementos" / "Carpeta" (0xFFFF = no se sabe) o el tamano
void flex_files_sub(const flex_fsx_ent_t *e, char *out, size_t n);

#ifdef __cplusplus
}
#endif
