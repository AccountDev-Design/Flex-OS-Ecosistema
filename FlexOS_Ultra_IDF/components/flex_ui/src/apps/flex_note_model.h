// Flex OS Ultra · Notas: geometria de la lista y sesion notas.bin v3 (C portable, con pruebas de host frente a Arduino).
//
// FlexOS_Ultra_AppNotes.h:75-143 (tarjetas, desplazamiento, boton flotante, barra de seleccion) y :354-533 (sesion).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOTE_MAX_LIST era 16 y recortaba ANTES de ordenar (se perdian notas sin avisar); aqui caben 64 y se ordena antes de recortar.
#define FLEX_NL_MAX      64
#define FLEX_NL_COLS     2
#define FLEX_NL_CARD_TOP 96
#define FLEX_NL_PREV_LEN 96    // NOTE_PREV_LEN: la vista previa lee 95 bytes
#define FLEX_NL_VIEW_Y   60    // el viewport con scroll empieza aqui (UIHDR_ZONE + 4)

int flex_nl_card_h(void);   // proporcional a la pantalla: clamp((800 - 96 - 90) / 2 - 44, 120, 300) = 263
// Rectangulo de la tarjeta i con el scroll aplicado (x = 16 / 248, y = 96 + fila * (h + 44) - scroll).
void flex_nl_card_rect(int i, int scroll, int *x, int *y, int *w, int *h);
int flex_nl_max_scroll(int n);
// Tarjeta bajo (px, py), o -1.
int flex_nl_card_at(int n, int scroll, int px, int py);
// Boton flotante: circulo r = 56 en (396, 650).
void flex_nl_fab(int *x, int *y, int *r);
bool flex_nl_fab_hit(int px, int py);
// Barra de seleccion (12, 672, 456, 60): 1 = "Salir" (x > 380), 2 = "Papelera" (x > 250), 0 = nada.
int flex_nl_bar_hit(int px, int py);

// ---- sesion notas.bin v3 (116 bytes) ------------------------------------------------------------------------------------
#define FLEX_NOTE_SESS_VER 3
typedef struct {
    uint8_t view;         // 0 lista, 1 editor
    uint8_t kb_layout;    // 0 ES, 1 EN, 2 NUM, 3 EMOJI
    uint8_t kb_flags;     // bit 0 idioma ES, 1 mayusculas, 2 portapapeles, 3 menu "mas", 4 menu de seleccion
    uint8_t reserved;
    uint16_t cursor;
    int16_t sel_a, sel_b;
    int32_t editor_scroll;
    int32_t list_scroll;
    char path[96];
} flex_note_sess_t;

// noteSaveSess: lo que se escribe (cursor acotado al texto, vista 1 solo con ruta).
void flex_note_sess_pack(flex_note_sess_t *s, bool editor_open, const char *path, uint8_t kb_layout, uint8_t kb_flags, int cursor,
                         int len, int sel_a, int sel_b, int editor_scroll, int list_scroll);
// noteLoadSess: normaliza lo leido (ruta terminada, capa < 4, scroll >= 0). La vista 1 solo se confirma si el
// fichero existe: lo decide quien llama al abrirlo (la lectura es asincrona).
void flex_note_sess_normalize(flex_note_sess_t *s);
// noteResume: la seleccion guardada solo vale si cabe en el texto.
bool flex_note_sel_valid(int sel_a, int sel_b, int len);

#ifdef __cplusplus
}
#endif
