// Flex OS Ultra · Flex Account: las decisiones de la pantalla de la cuenta sin dibujar nada (referencia: FlexOS_Account_Bridge.h). C puro: tests/host las compara contra el
// puente de Arduino con las mismas fotos; la capa LVGL (flex_ui/src/shell/flex_account_ui.c) solo las pinta.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flex_account.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_ACC_W 480
#define FLEX_ACC_H 800
#define FLEX_ACC_UNLINK_Y 632   // "Desvincular cuenta" bajo la tarjeta: su franja tactil (628..668) queda entre el boton principal (hasta 622) y la barra de salir (desde 670)
#define FLEX_ACC_UNLINK_H 34

// A DONDE SE VUELVE AL SALIR (cada via de entrada vuelve a su sitio; el retorno del configurador de Wi-Fi lo conserva)
typedef enum { FLEX_ACC_RET_OOBE = 0, FLEX_ACC_RET_STORE, FLEX_ACC_RET_SETTINGS } flex_acc_return_t;

// La cuenta esta guardada pero el servidor ya no la acepta: lo unico util es volver a vincular (la cuenta guardada se conserva si se cancela)
bool flex_account_needs_relink(const flex_account_snapshot_t *s);
// Color (RGB888, 0xRRGGBB) de la pildora del vinculo. Sin red no es un error: ambar suave, nunca el rojo de "desvinculada"
uint32_t flex_account_link_rgb(flex_account_link_t link);
// Boton principal de la pantalla sin vincular. Tras un fallo o un codigo caducado dice "Reintentar" (se vuelve a pedir SIN reiniciar)
const char *flex_account_primary_label(flex_account_state_t state, bool online);
// Linea bajo el codigo: si la consulta de aprobacion NO se pudo hacer dice por que; si no, cuanto vive el codigo
const char *flex_account_code_note(const flex_account_snapshot_t *s);
// Texto del boton principal de la tarjeta vinculada (sin necesidad de volver a vincular): a donde se vuelve
const char *flex_account_back_label(flex_acc_return_t where);
// Titulo de la tarjeta vinculada
const char *flex_account_linked_title(const flex_account_snapshot_t *s);
// Titulo de la tarjeta sin vincular
const char *flex_account_unlinked_title(const flex_account_snapshot_t *s);
// Fila de Ajustes > General > Flex Account: el estado REAL, nunca una direccion inventada
void flex_account_settings_text_for(const flex_account_snapshot_t *s, char *out, size_t n);

// Confirmacion de "Desvincular cuenta"
void flex_account_ask_geom(int *x, int *y, int *w, int *h);
void flex_account_ask_btns(int *cx, int *dx, int *by, int *bw, int *bh);   // Cancelar (izquierda) y Desvincular (derecha)

// Que hace un toque. `built` es el estado con el que se dibujo la pantalla (no el de ahora); `relink` y `online` son los de AHORA
typedef enum {
    FLEX_ACC_NONE = 0,
    FLEX_ACC_FINISH,          // salir (Omitir / Volver sin cambios / flecha / boton "Continuar" o "Volver a ...")
    FLEX_ACC_ASK_OPEN,        // pide confirmar la desvinculacion
    FLEX_ACC_ASK_DISMISS,     // Cancelar, o un toque fuera de la confirmacion
    FLEX_ACC_UNLINK_NOW,      // confirmado
    FLEX_ACC_WIFI,            // abre el configurador de Wi-Fi (no hay red)
    FLEX_ACC_REQUEST_CODE,    // pide un enlace nuevo
    FLEX_ACC_CANCEL_LINK,     // cancela el enlace en curso
} flex_acc_action_t;
// Salir cancela el enlace SOLO si hay uno en curso (antes se llamaba siempre y quedaba la peticion de cancelacion levantada despues de vincular con exito)
bool flex_account_finish_cancels(flex_account_state_t now);
flex_acc_action_t flex_account_hit(flex_account_state_t built, bool ask_open, bool first_boot, bool relink, bool online, int x, int y);

#ifdef __cplusplus
}
#endif
