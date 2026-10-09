// Flex OS Ultra · modelo de Recientes (C portable, sin LVGL).
//
// Una tarjeta por app viva, ordenadas por uso (0 = la mas reciente), como
// swPush / swDropCard / swThumbTrim de FlexOS_Ultra_AppSwitcher.h. Las
// miniaturas (150x250 RGB565 = 73 KB) se racionan: solo las FLEX_RC_THUMB_MAX
// mas recientes (1 en modo visual eficiente); el resto se dibuja con el icono.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_RC_MAX       19     // APP_N: no puede haber mas tarjetas que apps
#define FLEX_RC_THUMB_MAX 4
#define FLEX_RC_TH_W      150
#define FLEX_RC_TH_H      250

typedef struct {
    uint8_t app;
    uint16_t *thumb;   // NULL = sin miniatura (tarjeta con icono)
} flex_rc_card_t;

typedef struct {
    flex_rc_card_t c[FLEX_RC_MAX];
    int n;
    void (*free_thumb)(uint16_t *p);   // quien reservo la miniatura la suelta
    void (*terminate)(int app);        // lista llena: la mas antigua se cierra de verdad
} flex_rc_list_t;

// Mueve al frente (o inserta). La tarjeta del frente queda SIN miniatura nueva
// si se acaba de insertar (la pone quien captura).
void flex_rc_push(flex_rc_list_t *l, uint8_t app);
// Quita la tarjeta idx y suelta su miniatura (no toca el ciclo de vida).
void flex_rc_drop(flex_rc_list_t *l, int idx);
// Suelta las miniaturas desde la posicion keep (se conservan las keep primeras).
void flex_rc_thumb_trim(flex_rc_list_t *l, int keep);
int  flex_rc_find(const flex_rc_list_t *l, int app);
// Reduce una pantalla 480x800 RGB565 (stride en pixeles) a 150x250 por el
// vecino mas cercano (captureThumb).
void flex_rc_downscale(const uint16_t *src, int src_stride, uint16_t *dst);

#ifdef __cplusplus
}
#endif
