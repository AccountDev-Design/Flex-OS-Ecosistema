// Flex OS Ultra · modelo de avisos (C portable, sin LVGL).
//
// Historial de avisos del sistema con huella (FlexOS_Ultra_Notif.h:138-199) y
// la cola del presentador unico, el banner (FlexOS_FlexPhone_Overlay.h:198-422,
// 654-662, 773-866): uno a la vez, prioridad sistema > telefono urgente >
// telefono normal, cola de 6 con resumen "+N", el mismo aviso del sistema se
// actualiza donde este y el que pierde la pantalla vuelve al frente con el
// tiempo que le quedaba. docs/spec/01c §6; tests/host lo compara con Arduino.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- historial del sistema (gNotifs) ----------------------------------------------
#define FLEX_NTF_MAX       3
#define FLEX_NTF_TITLE_MAX 72
#define FLEX_NTF_SUB_MAX   40

enum { FLEX_NTF_SYSTEM = 0, FLEX_NTF_MEDIA = 1 };   // MOD_UNKNOWN, MOD_MEDIA

typedef struct {
    uint8_t type;
    char title[FLEX_NTF_TITLE_MAX];
    char sub[FLEX_NTF_SUB_MAX];
    uint32_t born_ms;
    uint32_t key;
} flex_ntf_t;

typedef struct {
    flex_ntf_t e[FLEX_NTF_MAX];
    int n;
} flex_ntf_hist_t;

// FNV-1a sobre el tipo, la direccion I2C (siempre 0) y SOLO el titulo; 0 -> 1.
uint32_t flex_ntf_key(uint8_t type, const char *title);
// notifPush: el mismo titulo actualiza su entrada (y vuelve a ser la mas
// reciente); si no, entra al final y, lleno, sale la mas antigua. Devuelve la huella.
uint32_t flex_ntf_push(flex_ntf_hist_t *h, uint8_t type, const char *title, const char *sub, uint32_t now_ms);
void flex_ntf_remove(flex_ntf_hist_t *h, int idx);

// ---- cola del banner ------------------------------------------------------------
#define FLEX_BQ_APP_MAX   32
#define FLEX_BQ_TITLE_MAX 64
#define FLEX_BQ_BODY_MAX  120
#define FLEX_BQ_DEPTH     6
#define FLEX_BQ_HOLD_MS   4200
#define FLEX_BQ_MIN_HOLD_MS 1500

enum { FLEX_BQ_SRC_PHONE = 0, FLEX_BQ_SRC_SYSTEM = 1 };
enum { FLEX_PRI_MIN = 0, FLEX_PRI_LOW, FLEX_PRI_DEFAULT, FLEX_PRI_HIGH, FLEX_PRI_URGENT };

typedef struct {
    char app[FLEX_BQ_APP_MAX];
    char title[FLEX_BQ_TITLE_MAX];
    char body[FLEX_BQ_BODY_MAX];
    uint32_t id;
    uint32_t key;      // huella de un aviso del sistema (0 = sin huella)
    uint32_t shown_ms; // lo que ya estuvo a la vista antes de volver a la cola
    uint8_t src;
    uint8_t pri;
    uint8_t icon;      // 1 + tipo de un aviso del sistema; 0 = sin icono
} flex_bq_msg_t;

typedef struct {
    flex_bq_msg_t q[FLEX_BQ_DEPTH];
    int n;
    uint32_t more;          // cuantos se resumieron ("+N")
    flex_bq_msg_t cur;      // el que esta (o estaba) a la vista
    bool cur_live;          // a la vista y no saliendo (fpbState != HIDDEN && != OUT)
    bool refresh;           // el de la vista cambio de texto: re-render y su tiempo a cero
} flex_bq_t;

// Copia de UTF-8 que nunca parte un caracter (flexLinkUtf8Copy).
size_t flex_utf8_copy(char *out, size_t cap, const char *src);

int  flex_bq_rank(const flex_bq_msg_t *m);
void flex_bq_enqueue(flex_bq_t *b, const flex_bq_msg_t *m, bool front);
bool flex_bq_same_sys(const flex_bq_msg_t *a, const flex_bq_msg_t *b);
void flex_bq_offer(flex_bq_t *b, const flex_bq_msg_t *m);
void flex_bq_msg_init(flex_bq_msg_t *m, uint8_t src, uint32_t id, const char *app, const char *title,
                      const char *body, uint8_t pri);
// Aviso del sistema que viene del historial (con su huella e icono).
void flex_bq_push_system_keyed(flex_bq_t *b, uint32_t key, uint8_t type, const char *title, const char *body);
// notifPush completo: al historial y, con la huella y los textos YA recortados
// del historial (como Arduino), al banner.
uint32_t flex_ntf_post(flex_ntf_hist_t *h, flex_bq_t *b, uint8_t type, const char *title, const char *sub,
                       uint32_t now_ms);
// Sacar el siguiente de la cola (fpbTick). false si esta vacia.
bool flex_bq_pop(flex_bq_t *b, flex_bq_msg_t *out);
// Tiempo a la vista de un aviso que ya estuvo shown_ms (como poco 1500 ms).
uint32_t flex_bq_hold_ms(uint32_t shown_ms);
// Soltar el banner arrastrado: true si se descarta (1/3 del ancho, o lanzamiento
// en el mismo sentido de >= 0,7 px/ms con >= 40 px de recorrido).
bool flex_bq_dismiss(float slide, float vx, bool moved, int card_w);

#ifdef __cplusplus
}
#endif
