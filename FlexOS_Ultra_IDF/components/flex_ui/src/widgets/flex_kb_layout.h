// Flex OS Ultra · teclado del sistema: LOGICA PURA (C portable, sin LVGL).
//
// Reproduce bit a bit el teclado de la version Arduino
// (FlexOS_Ultra_Keyboard.h, FlexOS_Ultra_KeyboardSettings.h, FlexOS_Ultra_Prefs.h):
// mapas de las 4 capas, resolucion de tecla con shift, geometria de los tres
// tamanos, hit-test sin franjas muertas, fila de funciones proporcional,
// destello de tecla por tiempo, preferencias de NVS, acentos, simbolos y el
// autocompletado de lista local. Lo comprueban las pruebas de host contra el
// codigo Arduino extraido (tests/host/test_kb.c).
//
// Todas las coordenadas son de pantalla 480x800 (vertical). El widget LVGL
// (flex_kb.c) solo consulta estas funciones: dibujo y toque no pueden separarse.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_KB_COLS   10
#define FLEX_KB_ROWS   3
#define FLEX_KB_CELLS  (FLEX_KB_COLS * FLEX_KB_ROWS)
#define FLEX_KB_FKEYS  6
#define FLEX_KB_SCR_W  480
#define FLEX_KB_SCR_H  800

// ---- Capas (el numero es el mismo que guarda la sesion de Notas: noteKbLayout)
typedef enum {
    FLEX_KB_LAYOUT_ES = 0,
    FLEX_KB_LAYOUT_EN = 1,
    FLEX_KB_LAYOUT_NUM = 2,
    FLEX_KB_LAYOUT_EMOJI = 3,
} flex_kb_layout_t;

// LAYOUT_ES / LAYOUT_EN / LAYOUT_NUM / LAYOUT_EMOJI de Arduino, tal cual.
extern const char *const flex_kb_maps[4][FLEX_KB_ROWS][FLEX_KB_COLS];

// ---- Estado de escritura (mapaActivo + kbShift + kbLangEs) ------------------
typedef struct {
    uint8_t layout;   // flex_kb_layout_t
    bool shift;       // una pulsacion: se apaga al escribir una variante
    bool lang_es;     // idioma de las capas de letras
} flex_kb_state_t;

void flex_kb_state_init(flex_kb_state_t *st, bool lang_es);
// Cadena base de la celda (0..29) en la capa activa; "" si la celda no existe.
const char *flex_kb_key_base(const flex_kb_state_t *st, int cell);
// kbResolveKey: lo que se muestra/escribe. Shift: a-z -> A-Z, n con tilde ->
// N con tilde, y en la capa ?123 ( -> { y ) -> }. Con consume_shift, escribir
// una variante apaga el shift. Devuelve base u out (6 bytes).
const char *flex_kb_resolve(flex_kb_state_t *st, const char *base, char out[6], bool consume_shift);
// Etiqueta de la tecla de capa: "?123" (letras) / "emoji" (NUM) / "ABC" (EMOJI).
const char *flex_kb_layer_label(const flex_kb_state_t *st);
const char *flex_kb_lang_label(const flex_kb_state_t *st);   // "ES" / "EN"

// Teclas de funcion (0..5), en el orden de la fila.
typedef enum {
    FLEX_KB_FN_SHIFT = 0,
    FLEX_KB_FN_LAYER,
    FLEX_KB_FN_LANG,
    FLEX_KB_FN_SPACE,
    FLEX_KB_FN_BACKSPACE,
    FLEX_KB_FN_ENTER,
} flex_kb_fn_t;
// noteFuncKey / lsuTick: aplica shift, capa (letras -> NUM -> EMOJI -> letras)
// o idioma sobre el estado. Espacio, borrar y enter no tocan el estado: los
// resuelve quien escribe. Devuelve la tecla (i), o -1 si i no es valida.
int flex_kb_fn_apply(flex_kb_state_t *st, int i);
// Pone una capa. ES/EN fijan tambien el idioma (como al restaurar Notas).
void flex_kb_set_layout_state(flex_kb_state_t *st, int layout);

// ---- Geometria (Fase A: tamanos en NVS "kbsize") ----------------------------
#define FLEX_KB_SIZE_COMPACT 0
#define FLEX_KB_SIZE_NORMAL  1
#define FLEX_KB_SIZE_BIG     2

typedef struct {
    int16_t kw, kh, gap, x;    // KB_KW, KB_KH, KB_GAP, KB_X
    int16_t bot_reserve;       // kbBotReserve: franja del sistema debajo (Notas: NAV_H = 64)
    int16_t top_h;             // kbTopH: barra superior + franja de chips (0 sin extras)
} flex_kb_geom_t;

// kbApplySize: Compacto 43x50 sep 4 · Normal 45x60 sep 2 · Grande 45x72 sep 2,
// rejilla centrada en 480 (KB_X >= 2). Un tamano fuera de rango vale Normal.
void flex_kb_geom_init(flex_kb_geom_t *g, int size, int bot_reserve, int top_h);
int  flex_kb_grid_w(const flex_kb_geom_t *g);       // 10*kw + 9*gap
int  flex_kb_rows_top(const flex_kb_geom_t *g);     // KB_Y: primera fila de teclas
int  flex_kb_panel_top(const flex_kb_geom_t *g);    // kbPanelTop: borde superior del panel
int  flex_kb_toolbar_y(const flex_kb_geom_t *g);
int  flex_kb_chips_y(const flex_kb_geom_t *g, int toolbar_h);
int  flex_kb_func_y(const flex_kb_geom_t *g);       // fila de funciones
int  flex_kb_fkey_w(const flex_kb_geom_t *g, int i);
int  flex_kb_fkey_x(const flex_kb_geom_t *g, int i);
// Hit-test: el area sensible de cada tecla es su PASO completo (tecla +
// separacion), sin huecos muertos. -1 = fuera.
int  flex_kb_cell_at(const flex_kb_geom_t *g, int px, int py);
int  flex_kb_frow_hit(const flex_kb_geom_t *g, int px, int py);
void flex_kb_cell_xy(const flex_kb_geom_t *g, int cell, int *x, int *y);
bool flex_kb_size_check(const flex_kb_geom_t *g);   // kbSizeCheck
// Alturas de los extras (Fases C y F): barra 56 si hay extras y la barra esta
// activada; chips 32 si hay extras y (capa NUM -> simbolos | predictivo).
int  flex_kb_toolbar_h(bool extras, bool toolbar_pref);
int  flex_kb_chips_h(bool extras, int layout, bool predict_pref);
// Desplazamiento de la animacion de entrada (0.3 s lineal, lsuKbAnim /
// noteKbAnim): (int)((1 - t/300) * kbh), 0 al terminar.
#define FLEX_KB_SLIDE_MS 300
int  flex_kb_slide_off(int kbh, uint32_t elapsed_ms);
// Clasificacion de toque de Arduino (Touch.h): tap si |dx|<16, |dy|<16 y <550 ms.
bool flex_kb_is_tap(int dx, int dy, uint32_t dur_ms);

// ---- Preferencias del teclado (NVS "flexos", mismas claves que Arduino) -----
#define FLEX_KB_SYMS    4
#define FLEX_KB_SC_MAX  8
#define FLEX_KB_SC_ABR  10
#define FLEX_KB_SC_EXP  24
typedef struct {
    int32_t size;       // "kbsize"  0 compacto, 1 normal, 2 grande
    bool fast;          // "kbfast"  escritura rapida (la tecla se escribe al tocar)
    bool toolbar;       // "kbtool"  barra superior (Notas)
    bool predict;       // "kbpred"  texto predictivo
    bool spell;         // "kbspell" revision ortografica basica
    bool emoji_sug;     // "kbemoji" sugerir emoticonos
    bool hicon;         // "kbhicon" contraste alto
    int32_t opacity;    // "kbopa"   40..100 (panel)
    int32_t style;      // "kbstyle" 0 redondeada, 1 cuadrada, 2 contorno
    int32_t font;       // "kbfont"  0 pequena, 1 normal, 2 grande
    int32_t lp_ms;      // "kblp"    350/500/700 (pulsacion larga -> acentos)
    int32_t fx_ms;      // "kbfx"    60/100/160 (destello de tecla)
    int32_t sym[FLEX_KB_SYMS];                    // "kbsyms" (4 bytes): indices de KB_SYM_POOL
    char sc_abr[FLEX_KB_SC_MAX][FLEX_KB_SC_ABR];  // "kbscabr" atajos de texto
    char sc_exp[FLEX_KB_SC_MAX][FLEX_KB_SC_EXP];  // "kbscexp"
} flex_kb_prefs_t;

void flex_kb_prefs_defaults(flex_kb_prefs_t *p);   // valores de fabrica (kbsResetDefaults)
void flex_kb_shortcuts_defaults(flex_kb_prefs_t *p);
void flex_kb_prefs_normalize(flex_kb_prefs_t *p);  // kbPrefsNormalize
// kbPrefsLoad: lee la cache de ajustes (flex_cfg_*) y normaliza.
void flex_kb_prefs_load(flex_kb_prefs_t *p);
// Talla de letra de las teclas (1..3 = FLEX_FONT_S1..S3) y su desfase vertical
// (kbFontSize / kbFontDy), radio (kbRadius).
int  flex_kb_font_size(const flex_kb_prefs_t *p);
int  flex_kb_font_dy(const flex_kb_prefs_t *p);
int  flex_kb_radius(const flex_kb_prefs_t *p);

// Simbolos personalizables (Fase E): 4 de estos 16.
#define FLEX_KB_SYM_POOL_N 16
extern const char *const flex_kb_sym_pool[FLEX_KB_SYM_POOL_N];
const char *flex_kb_sym_at(const flex_kb_prefs_t *p, int i);

// ---- Destello de tecla presionada (Fase G), por tiempo ----------------------
typedef struct {
    int cell;          // -1 = ninguno
    uint32_t t0;
} flex_kb_fx_t;
void flex_kb_fx_reset(flex_kb_fx_t *fx);
void flex_kb_fx_start(flex_kb_fx_t *fx, int cell, uint32_t now);
// 255 recien tocada .. 0 apagado (kbFxLevel).
int  flex_kb_fx_level(const flex_kb_fx_t *fx, int cell, uint32_t now, int fx_ms);
// kbFxTick: si el destello cumplio su tiempo lo apaga y devuelve la celda a
// repintar; si no, -1.
int  flex_kb_fx_tick(flex_kb_fx_t *fx, uint32_t now, int fx_ms);

// ---- Texto UTF-8 (lo que hacen las superficies con lo que escribe el teclado)
int  flex_kb_utf8_prev(const char *s, int i);       // utf8Prev
int  flex_kb_utf8_count(const char *s);             // utf8Count (puntos de la clave)
// lsuPassAppend / kbsAppendField: anade s si cabe (L + len < cap - 1).
bool flex_kb_buf_append(char *buf, size_t cap, const char *s);
// "<-": borra el ULTIMO caracter UTF-8 completo. false si estaba vacio.
bool flex_kb_buf_backspace(char *buf);

// ---- Pulsacion larga en vocal -> acentos (Notas) -----------------------------
int  flex_kb_variants(char base, const char *var[4]);              // kbGetVariants
bool flex_kb_is_vowel_cell(const flex_kb_state_t *st, int cell);   // kbIsVowelCell

// ---- Autocompletado: LISTA LOCAL FIJA por prefijo (Fase F) -------------------
// No es un modelo de lenguaje: busca por prefijo plegando mayusculas y tildes.
char flex_kb_fold_ch(const char **ps);                               // kbFoldCh
bool flex_kb_starts_with(const char *word, const char *pref);
bool flex_kb_same_word(const char *a, const char *b);
bool flex_kb_dict_has(const flex_kb_prefs_t *p, bool lang_es, const char *w);
// Hasta maxn sugerencias: atajos del usuario, diccionario, emoticono sugerido.
int  flex_kb_suggest(const flex_kb_prefs_t *p, bool lang_es, const char *pref, const char **out, int maxn);
// Palabra en construccion antes del cursor (bytes copiados).
int  flex_kb_current_word(const char *buf, int cur, char *out, int outsz);
int  flex_kb_dict_n(bool lang_es);

#ifdef __cplusplus
}
#endif
