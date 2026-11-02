// Flex OS Ultra · sesiones de app: formato del archivo y guardado diferido (C portable, sin LVGL).
//
// Cada app con estado de interfaz que merece sobrevivir a un reinicio (vista, scroll, seleccion)
// lo guarda en un archivo pequeno y versionado de /System/Sessions. Nunca el documento. Mismo
// formato (cabecera de 16 B + CRC-32) y mismas reglas de guardado diferido que la version Arduino
// (Session.h:55-102, AppFramework.h:1346-1438): tests/host compara byte a byte y orden a orden.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_SESS_MAGIC      0x584C4631u   // "1FLX" en little endian
#define FLEX_SESS_IO_MAX     1024          // cabecera + carga util
#define FLEX_SESS_HDR_SIZE   16
#define FLEX_SESS_IDLE_MS    1200          // inactividad que dispara el volcado diferido
#define FLEX_SESS_MAXWAIT_MS 30000         // tope duro con cambios sin escribir
#define FLEX_SESS_APPS       32

typedef struct {
    uint32_t magic;
    uint16_t ver, app;
    uint32_t len, crc;
} flex_sess_hdr_t;

// CRC-32 IEEE reflejado (el de zlib) sin tabla: crc32u(d, n, 0)
uint32_t flex_crc32u(const uint8_t *d, size_t n, uint32_t crc);
// Cabecera + carga util en out (cap >= 16 + len). Devuelve el tamano total o 0 si no cabe en
// FLEX_SESS_IO_MAX (sessWrite).
size_t flex_sess_pack(uint8_t *out, size_t cap, uint16_t ver, uint16_t app, const void *data, size_t len);
// Valida un archivo leido: devuelve los bytes utiles copiados en out, o 0 si es de otra version u
// otra app, esta truncado, es mayor que max o el CRC no cuadra. Una sesion que no valida NO se
// repara: se ignora y la app abre en estado seguro (sessRead).
size_t flex_sess_unpack(const uint8_t *buf, size_t sz, uint16_t ver, uint16_t app, void *out, size_t max);

// ---- guardado diferido ----------------------------------------------------------------------
// Resultado del gancho de guardado de una app (saveSess): sin gancho = nada que guardar.
typedef enum { FLEX_SESS_NOHOOK = -1, FLEX_SESS_FAIL = 0, FLEX_SESS_OK = 1 } flex_sess_res_t;
typedef int (*flex_sess_save_fn)(void *ctx, int app);

typedef struct {
    int8_t dirty_app;               // la unica app "sucia" a la vez (-1 ninguna)
    uint32_t dirty_ms, first_ms;
    bool need[FLEX_SESS_APPS];      // el archivo esta desfasado respecto a la app
} flex_sess_t;

void flex_sess_init(flex_sess_t *s);
// appSaveSession: sin gancho o sin cambios es exito trivial (no se reescribe una sesion identica:
// desgaste de la flash); el exito limpia la marca.
bool flex_sess_save_app(flex_sess_t *s, int app, flex_sess_save_fn save, void *ctx);
// sessMarkDirty: otra app pendiente se guarda YA; marca la nueva y reinicia la espera.
void flex_sess_mark_dirty(flex_sess_t *s, int app, uint32_t now, flex_sess_save_fn save, void *ctx);
void flex_sess_flush_now(flex_sess_t *s, flex_sess_save_fn save, void *ctx);
// sessAutosaveTick: blocked = restablecimiento pendiente o dedo apoyado. Vuelca tras
// FLEX_SESS_IDLE_MS sin marcas o, como tope, FLEX_SESS_MAXWAIT_MS desde la primera.
void flex_sess_tick(flex_sess_t *s, uint32_t now, bool blocked, flex_sess_save_fn save, void *ctx);
// Primera parte de appTerminate: olvida el volcado diferido de esa app y guarda en el acto.
// false = no se pudo guardar y el cierre no es forzado: la app NO se cierra (no se pierde trabajo).
bool flex_sess_terminate_begin(flex_sess_t *s, int app, bool force, flex_sess_save_fn save, void *ctx);

#ifdef __cplusplus
}
#endif
