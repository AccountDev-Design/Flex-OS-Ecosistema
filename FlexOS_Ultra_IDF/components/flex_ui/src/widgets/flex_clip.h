// Flex OS Ultra · portapapeles de 12 ranuras (Keyboard.h:463-520, FASE D): LOGICA PURA con pruebas de host frente a Arduino.
//
// El texto copiado alimenta el buffer clasico (512 bytes, lo que pega el menu "Pegar") y las 12 ranuras (200 bytes, las
// del panel). Solo las FIJADAS sobreviven al reinicio: se guardan en la NVS con una clave por ranura ("clip0".."clip11",
// espacio "flexos"), porque el portapapeles de trabajo cambia constantemente y escribir flash en cada copia gastaria
// ciclos para nada. Una fijada no se descarta jamas.
//
// Diferencia con Arduino: el texto se corta en un limite de caracter UTF-8 (Arduino partia a veces un caracter).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_CLIP_SLOTS   12
#define FLEX_CLIP_TXT_MAX 200
#define FLEX_CLIP_CLASSIC 512

typedef struct {
    char text[FLEX_CLIP_TXT_MAX];
    bool pinned;
    bool used;
    uint32_t ts;
} flex_clip_slot_t;

// Persistencia de las fijadas: put(i, texto) guarda; put(i, NULL) borra; get(i) lee ("" = no hay).
typedef struct {
    void (*put)(int slot, const char *text, void *user);
    void (*get)(int slot, char *out, size_t cap, void *user);
    void *user;
} flex_clip_store_t;

typedef struct {
    flex_clip_slot_t slot[FLEX_CLIP_SLOTS];
    char classic[FLEX_CLIP_CLASSIC];   // el ultimo texto copiado (clipboard[])
    flex_clip_store_t store;
} flex_clip_t;

void flex_clip_init(flex_clip_t *c, const flex_clip_store_t *store);   // vacia y carga las fijadas (clipLoadPinned)
int flex_clip_count(const flex_clip_t *c);
// Copiar: alimenta el buffer clasico y las ranuras. Repetido = solo refresca la fecha; sin hueco se sobrescribe la NO fijada
// mas antigua; con las 12 fijadas no se toca ninguna.
void flex_clip_push(flex_clip_t *c, const char *s, uint32_t now_ms);
void flex_clip_del(flex_clip_t *c, int i);
void flex_clip_toggle_pin(flex_clip_t *c, int i);
void flex_clip_clear_unpinned(flex_clip_t *c);
// Ranuras visibles (con el filtro de fijadas): indices en vis[], devuelve cuantas.
int flex_clip_visible(const flex_clip_t *c, bool only_pinned, int vis[FLEX_CLIP_SLOTS]);

#ifdef __cplusplus
}
#endif
