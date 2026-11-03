// Flex OS Ultra · editor de texto de Notas: LOGICA PURA (C portable, con pruebas de host frente a Arduino).
//
// Es el nucleo de FlexOS_Ultra_Keyboard.h:760-1030 sin graficos: buffer de trabajo con cursor y seleccion en
// bytes UTF-8, insertar / borrar / seleccionar palabra o todo, y un UNICO recorrido del texto (salto de linea
// por caracter, interlineado fijo) que sirve a la vez para dibujar, para el hit-test y para el scroll: si
// estuvieran en tres sitios, el dibujo, el toque y el auto-scroll se desincronizarian (el comentario de
// noteEditorWalk). El avance de cada glifo entra por un callback: en el firmware sale de la fuente de LVGL y
// en las pruebas de una tabla, igual en Arduino y aqui.
//
// Diferencias con Arduino (por integridad de datos, ver el informe):
//   * Insertar algo que no cabe NO borra antes la seleccion (Arduino la borraba y luego rechazaba el texto).
//   * El texto copiado se corta en un limite de caracter UTF-8, no a mitad de uno.
//   * Un cambio que deja el texto con la misma longitud (reemplazar una seleccion por otra del mismo tamano)
//     tambien cuenta como cambio: Arduino comparaba solo la longitud y no armaba el autoguardado.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- maquetacion (NOTE_LH, NOTE_TOP y los margenes de noteEditorWalk) ----------------------------------------
#define FLEX_NED_LH   26     // interlineado
#define FLEX_NED_TOP  60     // y de la primera linea (sin scroll)
#define FLEX_NED_X0   18     // margen izquierdo
#define FLEX_NED_MAXX 462    // SCR_W - 18: un caracter que se pasa de aqui salta de linea
#define FLEX_NED_SHEET_TOP 48

// ---- UTF-8 -----------------------------------------------------------------------------------------------
int flex_ned_utf8_prev(const char *s, int i);   // inicio del caracter anterior a i
int flex_ned_utf8_next(const char *s, int i);   // inicio del caracter siguiente a i
bool flex_ned_is_word_byte(unsigned char c);    // letra, cifra o cualquier byte >= 0x80
// nextCP: siguiente caracter DIBUJABLE: decodificado, plegado (comillas, guiones, espacios) y sin invisibles.
// Si el texto acaba en invisibles devuelve un espacio. Avanza *ps.
uint32_t flex_ned_next_cp(const char **ps);
uint32_t flex_ned_fold_cp(uint32_t cp);

// ---- el editor ----------------------------------------------------------------------------------------------
typedef struct {
    char *buf;      // texto de trabajo, siempre terminado en NUL
    size_t cap;     // tamano del buffer (con el NUL)
    int cur;        // cursor, en bytes
    int sel_a, sel_b;   // seleccion [a, b) en bytes; -1 = ninguna
    bool menu;      // menu flotante de seleccion visible
    bool follow;    // el siguiente repintado debe dejar el cursor a la vista
    int scroll;     // desplazamiento vertical del texto, en pixeles
    uint32_t rev;   // se incrementa con CADA cambio del texto (el autoguardado mira esto, no la longitud)
} flex_ned_t;

void flex_ned_init(flex_ned_t *e, char *buf, size_t cap);
bool flex_ned_has_sel(const flex_ned_t *e);
void flex_ned_clear_sel(flex_ned_t *e);       // tambien cierra el menu
void flex_ned_delete_sel(flex_ned_t *e);
// Inserta en el cursor (reemplaza la seleccion). false: no cabe y NO se cambia nada.
bool flex_ned_insert(flex_ned_t *e, const char *s);
void flex_ned_backspace(flex_ned_t *e);       // un caracter UTF-8 completo, o la seleccion
void flex_ned_select_word_at(flex_ned_t *e, int bi);
void flex_ned_select_all(flex_ned_t *e);
// Copia la seleccion a out (terminada en NUL), cortada en un limite de caracter si no cabe. Devuelve los bytes.
int flex_ned_copy_sel(const flex_ned_t *e, char *out, size_t cap);
// La palabra que termina en el cursor (letras desde el ultimo espacio, salto o tabulador): kbCurrentWord.
int flex_ned_current_word(const char *buf, int cur, char *out, int outsz);

// ---- recorrido del texto --------------------------------------------------------------------------------------
typedef int (*flex_ned_adv_fn)(uint32_t cp, void *ctx);   // avance horizontal del glifo, en pixeles

// Alto total del contenido; si out_y != NULL, la y (sin scroll) de la fila del byte `want_bi` (noteEditorWalk).
int flex_ned_walk(const flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int want_bi, int *out_y);
// Byte mas cercano al toque (px, py) (noteLayoutHit).
int flex_ned_hit(const flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int px, int py);
// Scroll: maximo, acotar y dejar el cursor a la vista. txt_bot = noteTxtBot() = limite inferior del area de texto.
int flex_ned_max_scroll(const flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int txt_bot);
void flex_ned_clamp_scroll(flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int txt_bot);
void flex_ned_ensure_cursor(flex_ned_t *e, flex_ned_adv_fn adv, void *ctx, int txt_bot);

// Un caracter dibujable: bytes [bi, bi+nb), codigo plegado, posicion (x, y) en pantalla (con el scroll) y avance.
typedef struct {
    int bi, nb;
    uint32_t cp;
    int x, y, w;
    bool selected;   // dentro de la seleccion
    bool visible;    // intersecta la hoja (y + LH > 48 && y <= y_break)
} flex_ned_glyph_t;
// Marcas: cursor y extremos de la seleccion (x, y en pantalla; y es el tope de la linea).
typedef struct {
    int cur_x, cur_y;
    int a_x, a_y, b_x, b_y;
} flex_ned_marks_t;
typedef struct {
    flex_ned_adv_fn adv;
    void *ctx;
    int y_break;                       // fila a partir de la cual no se emiten glifos (noteTxtBot() - 22)
    void (*glyph)(const flex_ned_glyph_t *g, void *user);
    // Revision ortografica (opcional): para cada palabra de >= 3 letras que no esta en el diccionario, una raya de
    // puntos desde x0 hasta x1 en la linea y. La palabra que se esta escribiendo (la del cursor, al final) se deja en paz.
    bool (*has_word)(const char *w, void *user);
    void (*misspelled)(int x0, int x1, int y, void *user);
    void *user;
} flex_ned_scan_t;
void flex_ned_scan(const flex_ned_t *e, const flex_ned_scan_t *s, flex_ned_marks_t *m);

// ---- menu flotante de seleccion: Cortar / Copiar / Pegar / Todo (noteDrawText, noteMenuHit) -------------------------
typedef struct {
    int x, y;          // origen de los 4 botones
    int bw, gap;       // 92 y 4
    int panel_x, panel_y, panel_w, panel_h;   // fondo: (x-6, y-6, tot+12, 40)
} flex_ned_menu_t;
void flex_ned_menu_geom(int handle_a_y, flex_ned_menu_t *m);
int flex_ned_menu_hit(const flex_ned_menu_t *m, int px, int py);   // 0..3 o -1

#ifdef __cplusplus
}
#endif
