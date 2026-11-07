// Flex OS Ultra · Ajustes del teclado: paginas, filas, acciones y editor de atajos (C portable, con pruebas de host frente a Arduino).
//
// FlexOS_Ultra_KeyboardSettings.h:53-250 (contenido), :251-262 (titulos), :395-464 (acciones) y :465-529 (toques). Como en Ajustes el modelo
// entrega una lista de operaciones de dibujo (flex_set_op_t) con las coordenadas de Arduino; el teclado del editor de atajos es el teclado
// del sistema (flex_kb), no una copia dibujada.
#pragma once

#include <stdbool.h>
#include "flex_kb_layout.h"
#include "flex_set_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_KBS_MAIN   0
#define FLEX_KBS_DESIGN 1
#define FLEX_KBS_TOUCH  2
#define FLEX_KBS_SYMS   3
#define FLEX_KBS_SHORT  4
#define FLEX_KBS_SCEDIT 5
#define FLEX_KBS_ABOUT  6
#define FLEX_KBS_LANG   7
#define FLEX_KBS_NPAGE  8
#define FLEX_KBS_TOP    100
#define FLEX_KBS_BOT    776    // SCR_H - 24
#define FLEX_KBS_BACK_X 48     // el toque de volver: x < 48, y < 52
#define FLEX_KBS_BACK_Y 52

typedef struct {
    int page;
    flex_kb_prefs_t pr;       // los ajustes del teclado (se guardan con flex_kb_prefs_save)
    bool lang_es;             // kbLangEs (idioma activo del teclado: solo mientras dura el encendido)
    int mt_max;               // dedos vistos a la vez (el indev de LVGL da UN punto)
    int sc_sel, sc_field;     // atajo en edicion / campo enfocado (0 abreviacion, 1 expansion)
    char sc_a[FLEX_KB_SC_ABR], sc_e[FLEX_KB_SC_EXP];
} flex_kbs_t;

void flex_kbs_init(flex_kbs_t *s, const flex_kb_prefs_t *pr, bool lang_es, int mt_max);
const char *flex_kbs_title(int page);
// Contenido de la pagina actual; y relativas a FLEX_KBS_TOP - scroll. content_h = kbsContentH.
void flex_kbs_build(flex_set_page_t *p, const flex_kbs_t *s);

#define FLEX_KBSF_SAVE       (1u << 0)   // kbPrefsSave()
#define FLEX_KBSF_MT_RESET   (1u << 1)   // kbMtSurfaceReset()
#define FLEX_KBSF_APPLY_SIZE (1u << 2)   // kbApplySize()
typedef struct {
    uint32_t flags;
    int go;        // pagina a la que ir (kbsGo, con su transicion) o -1
    bool render;   // repintar la pagina actual
} flex_kbs_act_t;
void flex_kbs_row_action(flex_kbs_t *s, int idx, flex_kbs_act_t *out);   // kbsRowAction: idx = orden de creacion de la fila
// Pagina a la que lleva el volver; -1 = salir de Ajustes del teclado.
int flex_kbs_back_target(int page);
// Toque de volver (x < 48, y < 52): true si lo es.
bool flex_kbs_back_hit(int x, int y);

// ---- editor de atajos ----------------------------------------------------------------------------------------------------------------
typedef enum { FLEX_KBS_E_NONE = -1, FLEX_KBS_E_FIELD0 = 0, FLEX_KBS_E_FIELD1 = 1, FLEX_KBS_E_SAVE = 2, FLEX_KBS_E_DELETE = 3 } flex_kbs_ehit_t;
int flex_kbs_edit_hit(int x, int y);                         // campos, Guardar y Borrar (el teclado de abajo lo atiende su widget)
void flex_kbs_edit_open(flex_kbs_t *s, int idx);             // abre el atajo idx con sus textos actuales
void flex_kbs_edit_focus(flex_kbs_t *s, int field);
void flex_kbs_edit_save(flex_kbs_t *s, flex_kbs_act_t *out);     // Guardar / OK: copia si ambos campos tienen texto; guarda y vuelve a la lista
void flex_kbs_edit_delete(flex_kbs_t *s, flex_kbs_act_t *out);   // Borrar el atajo
void flex_kbs_edit_enter(flex_kbs_t *s, flex_kbs_act_t *out);    // tecla OK del teclado: solo guarda si hay dos textos
bool flex_kbs_edit_append(flex_kbs_t *s, const char *utf8);
bool flex_kbs_edit_backspace(flex_kbs_t *s);
void flex_kbs_apply_lang(flex_kbs_t *s, bool es);

#ifdef __cplusplus
}
#endif
