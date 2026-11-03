// Flex OS Ultra · Paint: formato .fxp, geometria del lienzo y de la galeria, sesion (C portable, con pruebas de host frente a Arduino).
//
// FlexOS_FS.cpp:686-839 (formato y operaciones, aqui sobre una copia en memoria del fichero: la tarea de almacenamiento lo
// escribe entero y de forma atomica) y AppPaint.h:51-180, 249-260, 540-560 (geometria y sesion paint.bin v1).
//
// Diferencias con Arduino (por integridad de datos, ver el informe):
//   * Un fichero truncado a mitad de un trazo se recorta al ultimo trazo completo al abrirlo; Arduino anadia el trazo nuevo
//     detras de los bytes sueltos y dejaba el dibujo ilegible a partir de ahi.
//   * Un dibujo tiene un tope de tamano en memoria (FLEX_FXP_MAX_BYTES): al llegar se avisa en vez de seguir creciendo.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- formato .fxp ---------------------------------------------------------------------------------------------------------------
#define FLEX_FXP_HDR_LEN    12
#define FLEX_FXP_STROKE_LEN 6
#define FLEX_FXP_MAX_BYTES  (192 * 1024)
#define FLEX_FXP_MAX_PTS    512     // PAINT_MAX_PTS: puntos de un trazo de trabajo

// El fichero en memoria, tal cual esta en disco.
typedef struct {
    uint8_t *d;
    size_t len, cap;
} flex_fxp_t;

void flex_fxp_free(flex_fxp_t *f);
bool flex_fxp_create(flex_fxp_t *f, uint16_t w, uint16_t h);   // flexPaintCreate: cabecera con 0 trazos
// Lee los bytes de un fichero: valida la cabecera ("FXP1") y recorta la cola truncada (cuenta los trazos completos).
// false: no es un .fxp.
bool flex_fxp_load(flex_fxp_t *f, const uint8_t *data, size_t n);
bool flex_fxp_header(const uint8_t *d, size_t n, uint16_t *w, uint16_t *h, uint16_t *strokes);   // flexPaintHeader
// flexPaintAppend: anade un trazo al final y sube el contador. false: sin puntos, o pasaria de FLEX_FXP_MAX_BYTES.
bool flex_fxp_append(flex_fxp_t *f, uint16_t color, uint8_t size, const int16_t *xy, uint16_t pts);
bool flex_fxp_undo(flex_fxp_t *f);    // flexPaintUndo: quita el ultimo trazo; false sin trazos
void flex_fxp_clear(flex_fxp_t *f);   // flexPaintClear: 0 trazos, mismo w/h

// flexPaintReplay sobre bytes (de un fichero entero): emite cada segmento escalado. Se detiene en la primera lectura corta.
typedef void (*flex_fxp_seg_fn)(int x0, int y0, int x1, int y1, uint16_t color, int radius, void *user);
bool flex_fxp_replay(const uint8_t *d, size_t n, float sc, int ox, int oy, flex_fxp_seg_fn cb, void *user);

// ---- lienzo (paintDocLayout y compania) -----------------------------------------------------------------------------------------------
#define FLEX_PAINT_TOP 96
#define FLEX_PAINT_CX  8
#define FLEX_PAINT_CW  464
extern const uint16_t flex_paint_pal[6];    // RGB565 de la paleta
extern const uint8_t flex_paint_sizes[3];   // radios del pincel: 3, 6, 12
int flex_paint_bot(int nav_h);              // P_BOT = 800 - nav_h - 66
int flex_paint_ch(int nav_h);               // PAINT_CH = P_BOT - 96

typedef struct {
    int doc_w, doc_h;          // tamano del documento
    int x, y, disp_w, disp_h;  // donde cae en pantalla (letterbox)
    float scale;
} flex_paint_doc_t;
// hdr_w/hdr_h = los del fichero (0 si no se pudo leer: se usa el tamano del lienzo actual).
void flex_paint_doc_layout(flex_paint_doc_t *d, int hdr_w, int hdr_h, int nav_h);
bool flex_paint_doc_hit(const flex_paint_doc_t *d, int x, int y);
int16_t flex_paint_doc_coord(const flex_paint_doc_t *d, int v, int org, int lim);
uint8_t flex_paint_doc_radius(const flex_paint_doc_t *d, int brush);

// Barra de herramientas (paintTools / paintCanvasTick): colores, grosor, deshacer, limpiar.
typedef enum { FLEX_PT_NONE = -1, FLEX_PT_COLOR0 = 0, FLEX_PT_SIZE = 6, FLEX_PT_UNDO = 7, FLEX_PT_CLEAR = 8 } flex_paint_tool_t;
// Que hay bajo (x, y) de la barra (y en [P_BOT + 4, P_BOT + 46]); *color = indice 0..5 si es un color.
int flex_paint_tool_hit(int x, int y, int nav_h);

// ---- lienzo abierto (paintCanvasTick, AppPaint.h:454-515 y paintFlushStroke :330-334) --------------------------------------------------
// Todo lo que decide el lienzo sin dibujar nada: captura del trazo (descarta puntos a menos de 2 px, parte los trazos
// larguisimos), herramientas, deshacer y limpiar. La interfaz solo pinta los segmentos que se le entregan y guarda el
// documento cuando `modified` se enciende.
typedef struct {
    void (*dot)(int x, int y, int rad, uint16_t color, void *user);                          // fillCircleAA
    void (*seg)(int x0, int y0, int x1, int y1, int rad, uint16_t color, void *user);        // strokeSegAA
} flex_pcv_draw_t;

typedef struct {
    flex_fxp_t doc;                     // el dibujo, tal cual en disco
    flex_paint_doc_t lay;               // paintDocLayout
    int16_t xy[2 * FLEX_FXP_MAX_PTS];   // pStroke: el trazo en curso, en coordenadas del documento
    int n;                              // pStrokeN
    int px, py;                         // pPrevX / pPrevY (-1: sin trazo)
    uint16_t color;                     // pColor
    uint8_t size_ix;                    // pSizeIx
    int nav_h;
    // lo que la interfaz debe hacer tras la llamada (se apagan con flex_pcv_ack)
    bool modified;                      // el documento cambio: guardarlo
    bool redraw;                        // repintar el lienzo entero (deshacer, limpiar)
    bool tools;                         // repintar la barra de herramientas
    bool sess;                          // la sesion cambio (sessMarkDirty)
    bool full;                          // un trazo no cupo (tope de tamano del dibujo): se avisa, no se pierde en silencio
    bool back;                          // volver a la galeria
} flex_pcv_t;

typedef enum { FLEX_PCT_NONE = 0, FLEX_PCT_BACK, FLEX_PCT_COLOR, FLEX_PCT_SIZE, FLEX_PCT_UNDO, FLEX_PCT_CLEAR } flex_pcv_tap_t;

void flex_pcv_init(flex_pcv_t *c, int nav_h, uint16_t color, int size_ix);   // sin dibujo abierto
void flex_pcv_free(flex_pcv_t *c);
// Abre un fichero leido entero (cabecera "FXP1"); recorta una cola truncada. false: no es un dibujo (el estado queda sin dibujo).
bool flex_pcv_load(flex_pcv_t *c, const uint8_t *data, size_t n);
bool flex_pcv_new(flex_pcv_t *c);   // dibujo en blanco del tamano del lienzo actual
bool flex_pcv_is_open(const flex_pcv_t *c);
int flex_pcv_strokes(const flex_pcv_t *c);   // "N trazos guardados"
// Un fotograma del dedo. true: lo consumio el trazo (Arduino hace `return`); false: dedo fuera o levantado.
bool flex_pcv_touch(flex_pcv_t *c, bool down, int x, int y, const flex_pcv_draw_t *draw, void *user);
// Un toque (sin trazo en curso): volver, color, grosor, deshacer, limpiar.
flex_pcv_tap_t flex_pcv_tap(flex_pcv_t *c, int x, int y);
bool flex_pcv_flush(flex_pcv_t *c);   // paintFlushStroke: el trazo en curso al documento
bool flex_pcv_dirty(const flex_pcv_t *c);   // paintDirty: hay un trazo sin volcar
void flex_pcv_ack(flex_pcv_t *c);     // apaga los avisos

// ---- galeria -------------------------------------------------------------------------------------------------------------------------
#define FLEX_PG_MAX      24    // PAINT_MAX_LIST era 16 y recortaba antes de ordenar
#define FLEX_PG_COLS     2
#define FLEX_PG_CARD_TOP 118
void flex_pg_card_rect(int i, int scroll, int nav_h, int *x, int *y, int *w, int *h);
int flex_pg_max_scroll(int n, int nav_h);
int flex_pg_card_at(int n, int scroll, int nav_h, int px, int py);
void flex_pg_fab(int *x, int *y, int *r);   // boton "+" (404, 654), r = 52
bool flex_pg_fab_hit(int px, int py);
// Miniatura: escala y origen para encajar el documento en la tarjeta (margen de 4 px por lado).
void flex_pg_thumb(int card_x, int card_y, int card_w, int card_h, int doc_w, int doc_h, float *sc, int *ox, int *oy);

// ---- sesion paint.bin v1 (104 bytes) -------------------------------------------------------------------------------------------------------
#define FLEX_PAINT_SESS_VER 1
typedef struct {
    uint8_t view;      // 0 galeria, 1 lienzo
    uint8_t size_ix;
    uint16_t color;
    int32_t scroll;
    char path[96];
} flex_paint_sess_t;
void flex_paint_sess_pack(flex_paint_sess_t *s, bool canvas_open, const char *path, int size_ix, uint16_t color, int scroll);
// paintLoadSess: normaliza (ruta terminada, grosor < 3 o 1, scroll >= 0); la vista 1 solo se confirma si el fichero existe.
void flex_paint_sess_normalize(flex_paint_sess_t *s);

#ifdef __cplusplus
}
#endif
