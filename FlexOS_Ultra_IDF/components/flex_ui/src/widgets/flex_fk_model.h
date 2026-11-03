// Flex OS Ultra · kit de archivos: logica pura (C portable, con pruebas de host contra FileKit.h de Arduino).
//
// Geometria del menu contextual y de la confirmacion, desplazamiento de la papelera, texto ajustado a una
// caja para la vista previa de una nota y edicion del nombre. Todo lo que no necesita LVGL ni el disco.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x, y, w, h;
} flex_fk_rect_t;

// ---- menu contextual (fkMenuGeom / fkMenuHit) ------------------------------------------------------------
#define FLEX_FK_MENU_W   272
#define FLEX_FK_MENU_RH  46
#define FLEX_FK_MENU_PAD 10
int flex_fk_menu_rows(bool cloud);   // 4, o 5 con "Subir a Flex Cloud"
// Rectangulo del panel pedido en (px, py), ajustado a la pantalla: x en [8, 480-8-w], y en [30, 730-h].
void flex_fk_menu_geom(int px, int py, bool cloud, flex_fk_rect_t *r);
// -1 = fuera del panel (cierra sin accion); 0..rows-1 = fila (acotada).
int flex_fk_menu_hit(const flex_fk_rect_t *r, bool cloud, int px, int py);

// ---- confirmacion (fkAskGeom / fkAskTick) ----------------------------------------------------------------
void flex_fk_ask_geom(flex_fk_rect_t *card, flex_fk_rect_t *cancel, flex_fk_rect_t *ok);
// 1 = Borrar, -1 = Cancelar o fuera de la tarjeta, 0 = dentro sin boton (no pasa nada).
int flex_fk_ask_hit(int px, int py);

// ---- papelera (fkTrashMaxScroll) ---------------------------------------------------------------------------
#define FLEX_FK_TRASH_MAX 256   // elementos que se listan; el recuento real puede ser mayor
#define FLEX_FK_TRASH_TOP 120
#define FLEX_FK_TRASH_RH  66
int flex_fk_trash_max_scroll(int n);

// ---- texto en caja (fkTextBox) -----------------------------------------------------------------------------
// Corta por CARACTERES, no por palabras, a lo ancho w; respeta '\n'; para cuando no cabe otra linea
// (line_h + 4); un caracter mas ancho que la caja se descarta. Llama a emit(x, y, linea) por cada linea.
typedef int (*flex_fk_width_fn)(const char *s, int size, void *ctx);
typedef void (*flex_fk_line_fn)(int x, int y, const char *line, void *ctx);
void flex_fk_textbox_layout(int x, int y, int w, int h, const char *s, int size, int line_h, flex_fk_width_fn width,
                            void *wctx, flex_fk_line_fn emit, void *ectx);

// ---- nombre (fkNameAppend / fkNameBack) ----------------------------------------------------------------------
// Anade s al final: rechaza cualquier texto con '/' (partiria la ruta) y lo que no cabe. true = anadido.
bool flex_fk_name_append(char *buf, size_t cap, const char *s);
// Borra un caracter UTF-8 completo.
void flex_fk_name_back(char *buf);

#ifdef __cplusplus
}
#endif
