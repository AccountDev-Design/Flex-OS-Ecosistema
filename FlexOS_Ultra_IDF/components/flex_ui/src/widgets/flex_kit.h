// Flex OS Ultra · kit de componentes compartidos de las apps (docs/spec/02 §7, §8.4).
//
// Lo que se repite en las 19 apps: botones (normal / primario / peligro / deshabilitado), interruptor de
// pastilla, fila-tarjeta de Ajustes, linea informativa, barra de progreso, pestanas segmentadas, dialogo de
// sistema y aviso local. Metricas y tokens exactos de la version Arduino; todo sale de flex_th() y de
// flex_surface() (nada de colores propios, salvo los acentos de marca que pasa quien llama).
//
// Aislamiento entre apps: ningun componente guarda estado global. Cada uno lleva su estado en el propio
// objeto y lo libera al borrarse, asi que una app que se cierra (o se desaloja) no deja nada que otra pueda
// pisar. Los avisos y dialogos cuelgan del padre que se les da (la raiz de la app o de la pantalla).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "flex_glass.h"
#include "flex_layout.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- tamanos de texto de la version Arduino (1..5) -> fuentes Outfit ------------------------------------------
const lv_font_t *flex_ui_font(int fs);                       // 1 -> S1 ... 5 -> S5
int flex_ui_text_w(const char *t, int fs, void *ctx);        // para flex_ui_font_fit
// Texto cuyo TOPE de mayusculas queda en y (centrado en cx si cx >= 0, a partir de x si cx < 0)
lv_obj_t *flex_kit_text(lv_obj_t *parent, const char *s, int fs, lv_color_t color, int cx_or_x, int y, bool centered);

// Igual, pero con el CENTRO de las mayusculas / cifras en cy (numeros dentro de un circulo)
lv_obj_t *flex_kit_text_vc(lv_obj_t *parent, const char *s, int fs, lv_color_t color, int cx, int cy);

// ---- boton (fgButton, FlexPhone_UI.h:239) ----------------------------------------------------
typedef enum { FLEX_BTN_NORMAL = 0, FLEX_BTN_PRIMARY, FLEX_BTN_DANGER, FLEX_BTN_DISABLED } flex_btn_style_t;
typedef void (*flex_kit_btn_cb_t)(lv_obj_t *btn, void *user);
// Radio min(h/2, 16). Deshabilitado: NO recoge el toque (no puede haber un boton que parezca pulsable y no haga nada)
lv_obj_t *flex_kit_button(lv_obj_t *parent, int x, int y, int w, int h, const char *label, flex_btn_style_t style,
                          flex_kit_btn_cb_t cb, void *user);
void flex_kit_button_set(lv_obj_t *btn, flex_btn_style_t style, const char *label_or_null);
flex_btn_style_t flex_kit_button_style(const lv_obj_t *btn);

// ---- interruptor de pastilla (connPill, Conn.h:208) ----------------------------------------------
typedef void (*flex_kit_switch_cb_t)(lv_obj_t *sw, bool on, void *user);
lv_obj_t *flex_kit_switch(lv_obj_t *parent, int x, int y, int w, int h, bool on, flex_kit_switch_cb_t cb, void *user);
void flex_kit_switch_set(lv_obj_t *sw, bool on);   // no llama al callback
bool flex_kit_switch_get(const lv_obj_t *sw);
void flex_kit_switch_enable(lv_obj_t *sw, bool enabled);   // deshabilitado: pista TH_DIS y aspa; no recoge el toque

// ---- fila-tarjeta de Ajustes (setRowCard, AppSettings.h:239) -------------------------------------
typedef struct {
    const char *title;
    const char *value;          // linea pequena bajo el titulo (NULL = sin ella)
    lv_color_t accent;          // color del azulejo de la izquierda
    bool tile;                  // dibuja el azulejo de 32x32 (el icono lo pone quien llama sobre flex_kit_row_tile)
    bool chevron;               // chevron a la derecha
    int8_t sw;                  // -1 sin interruptor, 0 apagado, 1 encendido
    flex_kit_btn_cb_t cb;       // toque en la fila (NULL = informativa)
    flex_kit_switch_cb_t sw_cb; // cambio del interruptor
    void *user;
} flex_kit_row_t;
#define FLEX_KIT_ROW_STEP 64
// x = 14, w = 452, alto 56, r14, material de tarjeta. Con interruptor, el toque en la fila lo conmuta.
lv_obj_t *flex_kit_row(lv_obj_t *parent, int y, const flex_kit_row_t *r);
lv_obj_t *flex_kit_row_tile(lv_obj_t *row);       // el azulejo (o NULL)
lv_obj_t *flex_kit_row_switch(lv_obj_t *row);     // el interruptor (o NULL)
void flex_kit_row_set_value(lv_obj_t *row, const char *value);

// Linea informativa (drawInfoLine): etiqueta a la izquierda, valor a la derecha (x = 468), paso 24
#define FLEX_KIT_INFO_STEP 24
lv_obj_t *flex_kit_info_line(lv_obj_t *parent, int y, const char *label, const char *value);

// ---- barra de progreso (6 px r3 sobre TH_TRACK) y deslizador -------------------------------------
lv_obj_t *flex_kit_bar(lv_obj_t *parent, int x, int y, int w, int pct);
void flex_kit_bar_set(lv_obj_t *bar, int pct);
typedef void (*flex_kit_slider_cb_t)(lv_obj_t *s, int value, void *user);
lv_obj_t *flex_kit_slider(lv_obj_t *parent, int x, int y, int w, int min, int max, int value, flex_kit_slider_cb_t cb,
                          void *user);

// ---- pestanas segmentadas (relojDrawTabs, AppFramework.h:511) ------------------------------------
typedef void (*flex_kit_seg_cb_t)(lv_obj_t *seg, int index, void *user);
// alto 36, margenes 16, superficie de tarjeta r = h/2, segmento activo TH_PRIM r = (h-4)/2
lv_obj_t *flex_kit_segmented(lv_obj_t *parent, int y, const char *const *labels, int n, int active, flex_kit_seg_cb_t cb,
                             void *user);
void flex_kit_segmented_set(lv_obj_t *seg, int active);   // no llama al callback
int flex_kit_segmented_get(const lv_obj_t *seg);

// ---- dialogo de sistema (UIS_ELEVATED sobre un velo) ----------------------------------------------
typedef struct {
    const char *title;
    const char *line[3];        // hasta tres lineas de texto (NULL = no hay)
    const char *ok;             // boton de la derecha (NULL = solo "Cancelar"/cierre)
    const char *cancel;         // boton de la izquierda (NULL = no hay)
    bool danger;                // el boton de la derecha es de peligro
    void (*on_ok)(void *user);
    void (*on_cancel)(void *user);
    void *user;
} flex_kit_dialog_t;
// El velo se traga todos los toques; tocar fuera cierra si hay boton de cancelar. Devuelve el velo.
lv_obj_t *flex_kit_dialog(lv_obj_t *parent, const flex_kit_dialog_t *d);
void flex_kit_dialog_close(lv_obj_t *dlg);

// ---- aviso local (swToast / storeToast): capsula, 1800 ms, uno a la vez por padre -----------------------
void flex_kit_toast(lv_obj_t *parent, const char *msg);
/** Avisos locales vivos directamente bajo \p parent (0 o 1; para pruebas). */
int flex_kit_toast_count(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
