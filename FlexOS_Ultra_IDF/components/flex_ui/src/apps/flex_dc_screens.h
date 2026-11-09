// Flex OS Ultra · Device Care: las pantallas como LISTA DE OPERACIONES DE DIBUJO con las coordenadas exactas de Arduino (FlexOS_Ultra_DeviceCare.h y
// FlexOS_Ultra_DeviceTests.h), mas las zonas pulsables de cada una. C portable, comparado con las funciones de dibujo de Arduino compiladas tal cual
// (primitiva a primitiva). La interfaz LVGL pinta las operaciones (flex_dc_ui.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "flex_dc_model.h"
#include "flex_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DCO_SURF = 0,    // uiSurface: x, y, w, h, r (superficie del sistema, tarjeta)
    DCO_RECT,        // fillRect: x, y, w, h
    DCO_RRECT,       // fillRoundRect: x, y, w, h, r
    DCO_OUTLINE,     // drawRoundRect: x, y, w, h, r
    DCO_DISC,        // fillCircle: centro (x, y), radio r
    DCO_CIRCLE,      // drawCircle: centro (x, y), radio r
    DCO_RING,        // fillRing: centro (x, y), radio exterior r, grosor th
    DCO_ARC,         // arcStroke: centro (f[0], f[1]), radio f[2], de f[3] a f[4] grados, grosor th
    DCO_STROKE,      // strokeSegAA: (f[0], f[1]) -> (f[2], f[3]), radio f[4]
    DCO_TRI,         // fillTriangle: p[0..5]
    DCO_TEXT,        // drawText: x, y, size
    DCO_TEXT_C,      // drawTextC: centrado en x
    DCO_TEXT_R,      // drawTextR: alineado a la derecha en x
    DCO_TEXT_CLIP,   // drawTextClip: recorta en `mr`
} flex_dc_kind_t;

// roles de color (los resuelve el tema)
typedef enum {
    DCC_TXT = 0, DCC_TXT2, DCC_MUTE, DCC_OK, DCC_WARN, DCC_ERR, DCC_PRIM, DCC_ONACC, DCC_ACCS, DCC_TRACK, DCC_BORDER, DCC_SURF, DCC_SURF2,
    DCC_DIV, DCC_DANGER, DCC_ACCENT, DCC_WINBG, DCC_N
} flex_dc_role_t;

typedef struct {
    uint8_t kind, role, size, surf;   // surf: UIS_CARD (0) / UIS_ELEVATED (1) de uiSurface
    int16_t x, y, w, h, r, mr, th;
    int16_t p[6];
    float f[5];
    char text[72];
} flex_dc_op_t;

#define FLEX_DC_OPS_MAX 360
#define FLEX_DC_HITS_MAX 12
typedef struct {
    flex_dc_op_t op[FLEX_DC_OPS_MAX];
    int n;
    struct { int16_t x0, y0, x1, y1; uint8_t id; } hit[FLEX_DC_HITS_MAX];
    int nhit;
} flex_dc_page_t;

// ids de las zonas pulsables (Arduino: DCH_HERO ..., DCH_RUNDIAG, DCH_SCR_YES, ...)
enum { DCH_HERO = 100, DCH_T0, DCH_T1, DCH_T2, DCH_T3, DCH_T4, DCH_T5, DCH_RUNDIAG = 120, DCH_FALLTOGGLE = 140, DCH_FALLTEST, DCH_FALLOK, DCH_FALLRETRY,
       DCH_SCR_YES = 160, DCH_SCR_NO, DCH_TOUCH_END, DCH_RES_OK, DCH_RES_AGAIN, DCH_OPT_RUN, DCH_HEALTH_DIAG };
int flex_dc_hit_test(const flex_dc_page_t *pg, int x, int y);   // dcHitTest: -1 si no hay zona

// Caja de la app y entorno de maquetacion (uiBox / textW / idioma)
typedef struct {
    int bx, by, bw, bh;                 // uiBox
    int li;
    flex_ui_textw_fn tw;
    void *tctx;
    const char *(*mo_short)(int li, int mon0);   // abreviatura del mes (MO_SHORT[LI()])
} flex_dc_ctx_t;

// iconos de seccion (dcSectionIcon): DCI_*
enum { DCI_STATUS = 0, DCI_DIAG, DCI_FALL, DCI_HEALTH, DCI_OPT, DCI_HIST };
void flex_dc_icon(flex_dc_page_t *pg, int which, int x, int y, int S, int col, int acc);
// botones con glifo (dcButton)
enum { DCB_PLAY = 0, DCB_RETRY, DCB_CHECK, DCB_POWER, DCB_NONE };
void flex_dc_button(flex_dc_page_t *pg, const flex_dc_ctx_t *c, int x, int y, int w, int h, const char *label, int glyph, int bg, int fg, int hit_id);
void flex_dc_btn_check(flex_dc_page_t *pg, int cx, int cy, int r, int col);   // dcBtnGlyphCheck (tambien lo usa el test tactil)
void flex_dc_score_ring_c(flex_dc_page_t *pg, const flex_dc_ctx_t *c, int cx, int cy, int r, int score, float p, int col);   // dcScoreRing

// ---- pantallas ----
// Inicio: la tarjeta de estado (anillo p = 0..1) y la rejilla de seis accesos
typedef struct {
    bool score_ok;
    uint8_t score;
    uint32_t last_check;     // epoca UTC; 0 = nunca
    bool fall_on, bno_avail;
} flex_dc_home_t;
void flex_dc_home_card_geom(const flex_dc_ctx_t *c, int *cx, int *cy, int *cw, int *cardH);
void flex_dc_home_card(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_home_t *h, int cx, int cy, int cw, int cardH, float ringP);
void flex_dc_home(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_home_t *h);
// Igual con el anillo a medio barrer (ringP 0..1): el cuadro de la animacion de entrada (dcHomeAnimTick pinta la tarjeta con el mismo p)
void flex_dc_home_p(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_home_t *h, float ringP);
// Estado del dispositivo
void flex_dc_status(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_metric_t m[DCH_N], unsigned loop_rate);
// Salud de Flex OS (ringP = barrido del anillo)
void flex_dc_health(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_metric_t m[DCH_N], float ringP);
// Optimizacion (ps_total / ps_free: PSRAM actual)
void flex_dc_optim(flex_dc_page_t *pg, const flex_dc_ctx_t *c, uint32_t ps_total, uint32_t ps_free);
// Motor de diagnostico: etapas, anillo y latido (anim_ms = tiempo desde que entro la pantalla)
void flex_dc_diag(flex_dc_page_t *pg, const flex_dc_ctx_t *c, int stage, bool post, uint32_t anim_ms);
// Resultado
typedef struct {
    const flex_dc_res_t *res;
    const flex_dc_sysrow_t *rows;
    bool post, last_evt_ok;
    unsigned evt_conf;
    double evt_peak_g;
} flex_dc_result_t;
#define FLEX_DC_RING_MS 520
float flex_dc_ring_p(uint32_t anim_ms);   // barrido del anillo (ease-out) a los `anim_ms` de entrar
void flex_dc_result(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_result_t *r, uint32_t anim_ms);
// Test tactil: rejilla, cifras y boton
typedef struct { int gx, gy, cw, ch, foot; } flex_dc_ttgeom_t;
void flex_dc_touch_geom(const flex_dc_ctx_t *c, flex_dc_ttgeom_t *g);
void flex_dc_touch_test(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const uint8_t cell[FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS], int hit, int last_x, int last_y);
// Pregunta final del test de pantalla (los patrones a pantalla completa los pinta la interfaz)
void flex_dc_scr_ask(flex_dc_page_t *pg, const flex_dc_ctx_t *c);
// Deteccion de caidas SIN el controlador del sensor (A17h): no finge un modulo ni un estado; dice que no esta disponible y que modulo espera
// la funcion. Esta pantalla NO existe en Arduino (alli el sondeo del GY-BNO085 decide entre "ausente", "listo" y "sondeando").
const char *flex_dc_fall_note(int li);
void flex_dc_fall_unavailable(flex_dc_page_t *pg, const flex_dc_ctx_t *c);
// Historial: la lista desplazada `scroll` px
void flex_dc_hist(flex_dc_page_t *pg, const flex_dc_ctx_t *c, const flex_dc_hist_t *h, int scroll);

#ifdef __cplusplus
}
#endif
