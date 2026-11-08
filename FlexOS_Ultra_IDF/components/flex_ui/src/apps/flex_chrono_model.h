// Flex OS Ultra · Reloj y Cronometro: modelo puro (docs/spec/03 §1-2; Arduino: FlexOS_Ultra_AppChrono.h y
// FlexOS_Ultra_AppFramework.h:488-630).
//
// Sin LVGL ni FreeRTOS. El tiempo entra como argumento (`now`, milisegundos de 32 bits que dan la vuelta como
// millis()): la resta sin signo es la misma que la de Arduino, asi que el cronometro sobrevive al desbordamiento.
// Aqui viven: la logica del cronometro (iniciar / pausar / parcial / reiniciar, mejor y peor vuelta), el formato,
// la capsula de la barra de estado (ancho, posicion y quien manda cuando no cabe todo), la maqueta de la pestana
// (esfera, tabla de vueltas, botones), la tarjeta expandida (geometria, animacion, contenido) y las decisiones del
// tacto (que hace un toque o una pulsacion larga segun lo que haya a la vista), mas la maqueta de las pestanas de
// Reloj y de la pestana Hora. El dibujo y los widgets estan en app_reloj.c y en flex_chrono_ui.c.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_CRONO_MAX_LAPS 20   // tope de vueltas guardadas (CRONO_MAX_LAPS)

enum { FLEX_CRONO_IDLE = 0, FLEX_CRONO_RUN = 1, FLEX_CRONO_PAUSE = 2 };

typedef struct {
    uint32_t split;   // duracion propia de la vuelta (parcial)
    uint32_t total;   // total acumulado del cronometro al marcarla
} flex_crono_lap_t;

typedef struct {
    uint8_t st;                                  // FLEX_CRONO_*
    uint32_t accum;                              // ms consolidados (todo lo corrido antes de la pausa actual)
    uint32_t t0;                                 // `now` del ultimo arranque / reanudacion
    uint32_t lap_base;                           // total (ms) en que empezo la vuelta en curso
    flex_crono_lap_t laps[FLEX_CRONO_MAX_LAPS];
    uint8_t nlaps;                               // vueltas guardadas ahora mismo
    uint16_t lap0;                               // numero visible de la vuelta del indice 0
    int8_t best, worst;                          // indices de la mas rapida / lenta (-1 = aun no procede)
    bool laps_dirty, btn_dirty, dial_dirty, bar_dirty;
} flex_crono_t;

// Valores de arranque de Arduino (gCrono*): parado, vuelta 1 y todo por repintar.
void flex_crono_init(flex_crono_t *c);

// UNICA fuente del tiempo transcurrido: acumulado + (now - t0) mientras corre.
uint32_t flex_crono_elapsed(const flex_crono_t *c, uint32_t now);
uint32_t flex_crono_lap_elapsed(const flex_crono_t *c, uint32_t now);
bool flex_crono_active(const flex_crono_t *c);        // corriendo o pausado
int flex_crono_cur_lap_no(const flex_crono_t *c);     // numero visible de la vuelta que aun no se ha marcado

// cronoFmt: [h:]mm:ss[.cc]. Reserva 16 bytes.
void flex_crono_fmt(char *out, size_t n, uint32_t ms, bool cent);

void flex_crono_recalc_extremes(flex_crono_t *c);
void flex_crono_start(flex_crono_t *c, uint32_t now);
void flex_crono_pause(flex_crono_t *c, uint32_t now);
void flex_crono_reset(flex_crono_t *c);
void flex_crono_lap_mark(flex_crono_t *c, uint32_t now);
void flex_crono_action_left(flex_crono_t *c, uint32_t now);    // Parcial / Reiniciar
void flex_crono_action_right(flex_crono_t *c, uint32_t now);   // Iniciar / Detener / Continuar

// Etiquetas de los botones (la UI las traduce).
enum { FLEX_CRONO_LL_LAP = 0, FLEX_CRONO_LL_RESET };
enum { FLEX_CRONO_LR_START = 0, FLEX_CRONO_LR_STOP, FLEX_CRONO_LR_CONTINUE };
int flex_crono_label_left(const flex_crono_t *c);
int flex_crono_label_right(const flex_crono_t *c);

float flex_crono_ease(float p);                  // ease-out cubica: 1 - (1 - p)^3
int flex_crono_lerp(int a, int b, float p);

// ---- icono vectorial (cronoGlyph): caja + corona, esfera, aguja hacia las 2 ----------------------------------
typedef struct {
    int r;                                       // radio efectivo (minimo 4)
    int ring;                                    // grosor del anillo (minimo 2)
    float seg[3][4];                             // x0, y0, x1, y1 de: orejas, tallo de la corona, aguja
    float th;                                    // grosor de los trazos
} flex_crono_glyph_t;
void flex_crono_glyph(int cx, int cy, int r, float th, flex_crono_glyph_t *g);

// ---- capsula de la barra de estado ---------------------------------------------------------------------------
#define FLEX_CRONO_CAP_Y    12
#define FLEX_CRONO_CAP_H    26
#define FLEX_CRONO_CAP_ICON 18
#define FLEX_CRONO_CAP_PADL 8
#define FLEX_CRONO_CAP_GAP  6
#define FLEX_CRONO_CAP_PADR 11
#define FLEX_CRONO_CAP_RIGHT (480 - 66 - 12)     // borde izquierdo del Wi-Fi: la capsula nunca lo pisa
#define FLEX_CRONO_HOUR_MS  3600000UL

// Clase de formato de la capsula: 0 = "00:00", 1 = "0:00:00" (desde la primera hora). El ancho depende SOLO de
// la clase, nunca de los digitos concretos.
int flex_crono_cap_class(uint32_t elapsed);
const char *flex_crono_cap_template(uint32_t elapsed);
// tw = ancho en px de la plantilla (la de flex_crono_cap_template)
int flex_crono_cap_w(int tw);

typedef struct {
    int x;                                       // x de la capsula
    bool show_clock;                             // false: manda la capsula y la hora normal no se dibuja
    bool on;                                     // hay capsula
} flex_crono_bar_t;
// cronoBarClock: la hora en x = 20 y la capsula detras a 12 px; si no cabe, la capsula pasa a x = 20 y la hora se
// sacrifica (conectividad y bateria no se tocan).
void flex_crono_bar_layout(bool active, int clock_w, int cap_w, flex_crono_bar_t *out);

// cronoBarSurface: 1 = escritorio, 2 = marco estandar de app, 0 = ninguna (bloqueo, cortina, edicion, kiosco...).
typedef struct {
    bool suspended, land, hosted;
    bool app, app_hides_status_bar, app_custom_header;
    bool immersive, kiosk;
    bool home, qs_closed, edit_mode;            // qs_closed = qsPanelY == 0 && !qsAnimOn
} flex_crono_surface_in_t;
int flex_crono_surface(const flex_crono_surface_in_t *in);

// cronoCapsuleTick: que hay que hacer con la capsula este cuadro.
enum { FLEX_CRONO_CT_NONE = 0, FLEX_CRONO_CT_REBUILD, FLEX_CRONO_CT_STAMP };
typedef struct {
    bool cap_on;                                 // hay capsula pintada ahora mismo
    int cap_w_drawn;                             // ancho con el que se pinto
    uint32_t cap_sec;                            // ultimo segundo estampado
    uint8_t cap_st_drawn;                        // ultimo estado estampado
} flex_crono_cap_t;
void flex_crono_cap_reset(flex_crono_cap_t *k);  // valores de arranque: sin capsula, segundo 0xFFFFFFFF
// Devuelve la accion y actualiza `c` (bar_dirty) y `k` (segundo y estado estampados) como lo hace Arduino.
// rebuild_surface: para REBUILD, la superficie que hay que rehacer (0 = ninguna).
int flex_crono_cap_tick(flex_crono_t *c, flex_crono_cap_t *k, uint32_t now, int surface, int cap_w, bool card_hidden);

// Lo que deja cronoBarClock en el estado tras pintar la barra entera: capsula presente (con su ancho, segundo y estado) o
// ausente. La UI lo llama cuando termina de rehacer la barra.
void flex_crono_cap_apply_bar(flex_crono_t *c, flex_crono_cap_t *k, uint32_t now, bool active, int cap_w);

// ---- maqueta de la pestana Cronometro (CronoLayout / cronoLayout) -----------------------------------------------
typedef struct {
    int bx, by, bw, bh;                          // cuerpo de la pestana
    int cx, cy;                                  // centro de la esfera
    int r_tick, r_disc;                          // radio de las marcas y del disco central
    int dial_y0, dial_y1;                        // banda vertical que ocupa la esfera
    int tbl_y, row_h, rows;                      // tabla de vueltas
    int col_lap, col_split, col_tot;
    int btn_cy, btn_r, btn_lx, btn_rx;
    bool has_table;
} flex_crono_layout_t;
void flex_crono_layout(int bx, int by, int bw, int bh, int pad, flex_crono_layout_t *L);

// Tamano de fuente mas grande (hasta max_size) que cabe en maxw. text_w mide el texto con ese tamano.
typedef int (*flex_crono_textw_fn)(const char *s, int fs, void *ctx);
int flex_crono_fit_big(const char *s, int maxw, int max_size, flex_crono_textw_fn text_w, void *ctx);

// ---- esfera ------------------------------------------------------------------------------------------------------
// Marca i (0..59): extremos y grosor. Las de cada 5 son mas largas, gruesas y claras.
typedef struct {
    float x0, y0, x1, y1;
    float w;
    bool major;
} flex_crono_tick_t;
void flex_crono_dial_tick(const flex_crono_layout_t *L, int i, flex_crono_tick_t *t);
// Agujas: la violeta marca el TOTAL (una vuelta de esfera por minuto), la gris la vuelta en curso; ambas con cola.
typedef struct {
    float lap[4], tot[4];                        // x0 (cola), y0, x1 (punta), y1
    float hub;                                   // radio del cubo central
    float lap_w, tot_w;
} flex_crono_hands_t;
void flex_crono_dial_hands(const flex_crono_layout_t *L, uint32_t elapsed, uint32_t lap_elapsed, flex_crono_hands_t *h);
// Texto central: y del tope del texto = cy - 2.3 * fs
int flex_crono_dial_text_y(const flex_crono_layout_t *L, int fs);

// Fila r de la tabla de vueltas (0 = la mas reciente, hacia atras): false si ya no hay vueltas. kind marca la mejor y la
// peor; no / split / total son los textos (no: 8 bytes, los otros: 16).
enum { FLEX_CRONO_ROW_NORMAL = 0, FLEX_CRONO_ROW_BEST, FLEX_CRONO_ROW_WORST };
bool flex_crono_lap_row(const flex_crono_t *c, int r, int *kind, char *no, char *split, char *total);

// Toque de la pestana (cronoTabTouch): 0 nada, 1 boton izquierdo, 2 boton derecho.
int flex_crono_tab_hit(const flex_crono_layout_t *L, int x, int y);

// ---- tarjeta expandida ---------------------------------------------------------------------------------------------
#define FLEX_CRONO_CARD_X    20
#define FLEX_CRONO_CARD_W    (480 - 40)
#define FLEX_CRONO_CARD_Y    30
#define FLEX_CRONO_CARD_H    196
#define FLEX_CRONO_CARD_RAD  42
#define FLEX_CRONO_CARD_ANIM 190                 // ms de expansion / contraccion
#define FLEX_CRONO_BAND_T    8
#define FLEX_CRONO_BAND_B    244
#define FLEX_CRONO_DYN_T     (FLEX_CRONO_CARD_Y + 22)
#define FLEX_CRONO_DYN_B     (FLEX_CRONO_CARD_Y + 118)

enum { FLEX_CC_HIDDEN = 0, FLEX_CC_OPENING, FLEX_CC_OPEN, FLEX_CC_CLOSING };

// Un cuadro de la tarjeta: p = 0 -> pildora de la capsula, p = 1 -> tarjeta entera (p ya con el ease aplicado).
typedef struct {
    int x, y, w, h, r;
    uint8_t accent_a;                            // morfeo de color desde el acento de la capsula: 255 * (1 - p)
    uint8_t content_a;                           // el contenido entra en la segunda mitad (p >= 0.55)
} flex_crono_card_frame_t;
void flex_crono_card_frame(float p, int cap_x, int cap_w, flex_crono_card_frame_t *f);

// Reposo exacto de la tarjeta (p = 1). Arduino interpola con (int)(d * p + 0.5) y, hacia abajo, el ultimo cuadro cae un pixel
// corrido (x = 21 si la capsula esta a la derecha de 20): la app pinta el reposo con estas constantes.
int flex_crono_card_x_rest(void);

// Avance de la animacion: p lineal 0..1 a partir de los ms transcurridos desde que empezo.
float flex_crono_card_progress(uint32_t elapsed_ms);

// Contenido (cronoCardStatic / cronoCardDynText).
typedef struct {
    int icon_x, icon_y, icon_s, icon_r;          // cuadrado de acento 62 x 62, r20
    int glyph_cx, glyph_cy, glyph_r;             // el glifo entra cuando el cuadrado ya es casi solido (alfa > 200)
    int sep_x, sep_y, sep_w, sep_h;              // separador vertical entre los dos botones
    int lbl_lx, lbl_rx, lbl_y;                   // etiquetas de los botones (tamano 3), centradas
    int time_x, time_y, time_maxw;               // tiempo grande con centesimas (tamano <= 7)
    int lap_x, lap_y;                            // "Vuelta N" (tamano 2)
} flex_crono_card_lay_t;
void flex_crono_card_layout(flex_crono_card_lay_t *l);

// ---- tacto (cronoOverlayTouch) ---------------------------------------------------------------------------------------
enum {
    FLEX_CRONO_TC_TAP = 1, FLEX_CRONO_TC_PRESSED = 2, FLEX_CRONO_TC_RELEASED = 4, FLEX_CRONO_TC_SWIPE_UP = 8,
    FLEX_CRONO_TC_SWIPE_L = 16, FLEX_CRONO_TC_SWIPE_R = 32, FLEX_CRONO_TC_SWIPE_D = 64,
};
typedef struct {
    bool down, tap, pressed, released, swipe_up, swipe_left, swipe_right, swipe_down;
    int x, y, start_x, start_y;
    uint32_t down_ms;
} flex_crono_touch_t;
typedef struct {
    bool long_fired;                             // la pulsacion larga ya disparo en este episodio
} flex_crono_ov_t;
typedef struct {
    bool ota_owns, susp;
    int card;                                    // FLEX_CC_*
    bool cap_on;                                 // gCronoCapOn
    int surface;                                 // flex_crono_surface()
    int cap_x, cap_w_drawn;
} flex_crono_world_t;
enum { FLEX_CRONO_ACT_DROP = 1, FLEX_CRONO_ACT_CLOSE, FLEX_CRONO_ACT_LEFT, FLEX_CRONO_ACT_RIGHT, FLEX_CRONO_ACT_OPEN_APP,
       FLEX_CRONO_ACT_CARD_OPEN, FLEX_CRONO_ACT_BG_INVAL };
typedef struct {
    int act[4];                                  // en orden; FLEX_CRONO_ACT_*
    int n;
    unsigned clr;                                // banderas del toque que se consumen (FLEX_CRONO_TC_*)
} flex_crono_touch_out_t;
// Decide el toque. Las acciones de boton (LEFT / RIGHT) las ejecuta aqui sobre `c`, porque la tarjeta se contrae
// si "Reinic." deja el cronometro inactivo.
void flex_crono_overlay_touch(flex_crono_t *c, flex_crono_ov_t *ov, const flex_crono_world_t *w,
                              const flex_crono_touch_t *t, uint32_t now, flex_crono_touch_out_t *out);
// Caja sensible de la capsula (con 6 px de margen).
void flex_crono_cap_box(int cap_x, int cap_w, int *x0, int *y0, int *x1, int *y1);

// cronoCardTick (fase CC_OPEN, ~30 fps): true si toca repintar la sub-banda dinamica.
#define FLEX_CRONO_FRAME_MS 33
bool flex_crono_frame_due(uint32_t now, uint32_t last);

// ---- Reloj: pestanas y pestana Hora --------------------------------------------------------------------------------
#define FLEX_RELOJ_TABS_H 46
#define FLEX_RELOJ_TAB_N  3
#define FLEX_RELOJ_WIN_TOP 96
#define FLEX_RELOJ_WIN_BOT (800 - 64)

typedef struct {
    int bx, by, bw, bh;                          // cuerpo de la pestana (menos la barra de pestanas)
    int tabs_h;                                  // 0 si el lienzo es diminuto (sin pestanas)
} flex_reloj_box_t;
// relojBody sobre una caja util (uiBox): x, y, w, h.
void flex_reloj_body(int x, int y, int w, int h, flex_reloj_box_t *b);
bool flex_reloj_tabs_visible(int h);
typedef struct {
    int x, y, w, h, seg;
} flex_reloj_tabs_t;
void flex_reloj_tabs_geom(int bx, int by, int bw, int pad, flex_reloj_tabs_t *t);
// relojTabsTouch: indice de la pestana en (x, y) o -1 si el toque cae fuera de la barra.
int flex_reloj_tab_at(const flex_reloj_tabs_t *t, int x, int y);

typedef struct {
    int cap_h, thick, cy;                        // reloj grande: altura de mayusculas, grosor, y del tope
    bool show_date;
    int date_hint;                               // alto de linea que se le ofrece a la fecha (uiFontH de esto)
    int date_y;
    bool show_cards;
    int cards_y, card_w, card_h, gap;
    int footer_y;
    bool show_footer;
} flex_reloj_hora_t;
// date_lh(hint) devuelve el alto de linea de la fuente con que se pinta la fecha larga (uiLineH de uiFontFit).
typedef int (*flex_reloj_datelh_fn)(int hint, void *ctx);
void flex_reloj_hora_layout(const flex_reloj_box_t *b, int pad, int gap, int clock_len, int footer_lh,
                            flex_reloj_datelh_fn date_lh, void *ctx, flex_reloj_hora_t *out);

#ifdef __cplusplus
}
#endif
