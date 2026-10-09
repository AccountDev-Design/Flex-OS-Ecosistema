// Flex OS Ultra · teclado del sistema (widget LVGL reutilizable).
//
// El teclado de 4 capas de la version Arduino (ES / EN / ?123 / emoticonos)
// con su geometria exacta en 480x800, sus colores por superficie (sistema,
// pagina de clave, sobre el fondo de pantalla) y su comportamiento: shift de
// una pulsacion, tecla de capa, conmutador ES/EN, espacio, "<-", enter,
// escritura rapida (la tecla se escribe al tocar), destello de tecla por
// tiempo, pulsacion larga en vocal -> acentos y entrada deslizando 0.3 s.
// Toda la logica (mapas, geometria, hit-test, preferencias) esta en
// flex_kb_layout.c y se prueba en el PC contra el codigo Arduino.
//
// El teclado NO guarda texto: avisa con callbacks y quien lo usa decide
// (la pantalla de clave, Notas, Wi-Fi...). flex_kb_buf_append() y
// flex_kb_buf_backspace() (flex_kb_layout.h) son las operaciones de buffer que
// usaban esas superficies en Arduino.
//
// Uso tipico (pantalla de contrasena al verificar):
//   flex_kb_cfg_t c = {.flags = FLEX_KB_F_WALLPAPER, .backdrop = FLEX_BD_LOCK,
//                      .on_text = txt, .on_backspace = del, .on_enter = ok};
//   lv_obj_t *kb = flex_kb_create(scr, &c);   // scr: contenedor 480x800 en (0,0)
//   flex_kb_slide_in(kb);
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "flex_glass.h"
#include "flex_kb_layout.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Opciones (cfg.flags) ----------------------------------------------------
// Colores de la clave al VERIFICAR (Lock.h lsuVerify = true): panel
// TH_WALLPANEL, teclas TH_WALLSURF, texto TH_ONWALL, iguales en claro y oscuro.
#define FLEX_KB_F_WALLPAPER   (1u << 0)
// Colores de la clave al CREARLA (lsuVerify = false): panel del color de
// pagina (vidrio: TH_GLASS), teclas TH_SURF, texto TH_TXT.
#define FLEX_KB_F_PAGE        (1u << 1)
// Idioma inicial ingles (por defecto espanol, como lsuEnter / noteEditorEnter).
#define FLEX_KB_F_LANG_EN     (1u << 2)
// Barra de herramientas + franja de sugerencias encima de las teclas (solo
// Notas: en una pantalla de clave seria una via de escape). PREPARADO, NO
// IMPLEMENTADO: hoy no dibuja nada ni reserva alto (docs/spec/01b, Teclado).
#define FLEX_KB_F_EXTRAS      (1u << 3)
// Con escritura rapida, las teclas de funcion tambien actuan al TOCAR (Notas).
// Sin el flag actuan al soltar, como en la clave: confirmar o borrar tiene que
// salir de un toque deliberado.
#define FLEX_KB_F_FN_ON_PRESS (1u << 4)
// Pulsacion larga en una vocal -> ventana de acentos (Notas).
#define FLEX_KB_F_ACCENTS     (1u << 5)
// Ignora la preferencia "kbfast": escribe siempre al soltar.
#define FLEX_KB_F_NO_FAST     (1u << 6)

typedef void (*flex_kb_text_cb_t)(lv_obj_t *kb, const char *utf8, void *user);
typedef void (*flex_kb_key_cb_t)(lv_obj_t *kb, void *user);

typedef struct {
    uint32_t flags;               // FLEX_KB_F_*
    flex_kb_layout_t layout;      // capa inicial: NUM o EMOJI; si no, letras del idioma
    flex_backdrop_t backdrop;     // que hay detras del panel (Liquid Glass): FLAT, HOME o LOCK
    int16_t bottom_reserve;       // franja del sistema debajo (Notas: 64, barra de navegacion)
    const char *enter_label;      // etiqueta de la tecla enter; NULL = "OK"
    flex_kb_text_cb_t on_text;    // caracter(es) UTF-8 a insertar (tecla, espacio, acento)
    flex_kb_key_cb_t on_backspace;// "<-": borrar UN caracter UTF-8 completo
    flex_kb_key_cb_t on_enter;    // tecla enter / OK
    void *user;
} flex_kb_cfg_t;

// Crea el teclado como hijo de parent, que debe ser un contenedor de 480x800
// con origen en la esquina de la pantalla: el panel se coloca en su y de
// reposo (kbPanelTop) y ocupa hasta el borde inferior. Lee las preferencias
// del teclado de la NVS (kbsize, kbfast, kbhicon, kbopa, kbstyle, kbfont, kbfx,
// kblp). Se libera con lv_obj_delete (desde un callback: lv_obj_delete_async).
lv_obj_t *flex_kb_create(lv_obj_t *parent, const flex_kb_cfg_t *cfg);

// Capa / shift / idioma (para restaurar la sesion de Notas, como
// noteRestoreKbState). ES y EN fijan tambien el idioma.
void flex_kb_set_layout(lv_obj_t *kb, flex_kb_layout_t layout);
flex_kb_layout_t flex_kb_get_layout(const lv_obj_t *kb);
void flex_kb_set_shift(lv_obj_t *kb, bool on);      // tecla shift (no confundir con set_shift_x)
bool flex_kb_get_shift(const lv_obj_t *kb);
void flex_kb_set_lang_es(lv_obj_t *kb, bool es);
bool flex_kb_get_lang_es(const lv_obj_t *kb);

// Alto del panel (de kbPanelTop al borde inferior). Con kb = NULL: el de un
// teclado sin extras ni reserva con el tamano guardado en la NVS.
int32_t flex_kb_height(const lv_obj_t *kb);
// Y del panel en reposo (limite de abajo del area de texto: Notas usa top - 8)
// y de la primera fila de teclas (KB_Y).
int32_t flex_kb_top(const lv_obj_t *kb);
int32_t flex_kb_keys_y(const lv_obj_t *kb);
const flex_kb_geom_t *flex_kb_geom(const lv_obj_t *kb);

// Entrada deslizando desde abajo, 0.3 s lineal (lsuKbAnim / noteKbAnim).
// Mientras dura no se atiende ningun toque.
void flex_kb_slide_in(lv_obj_t *kb);
bool flex_kb_is_animating(const lv_obj_t *kb);

// Sacudida horizontal de la clave equivocada (FASE 1 de Arduino): mueve SOLO
// las teclas dentro de su panel, que no se desplaza (lsuDrawKb(yoff, xoff)).
void flex_kb_set_shift_x(lv_obj_t *kb, int32_t dx);
// Contenedor de todas las teclas (letras + fila de funciones), por si quien lo
// usa quiere animarlo por su cuenta.
lv_obj_t *flex_kb_keys_obj(lv_obj_t *kb);

// Desactiva la entrada (p. ej. mientras se verifica la clave: lsuChkOn).
void flex_kb_set_input_enabled(lv_obj_t *kb, bool en);
// Vuelve a leer las preferencias de la NVS y reaplica tamano y estilo YA
// (Arduino: kbApplySize al cambiar el tamano en Ajustes, sin reiniciar).
void flex_kb_reload_prefs(lv_obj_t *kb);

#ifdef __cplusplus
}
#endif
