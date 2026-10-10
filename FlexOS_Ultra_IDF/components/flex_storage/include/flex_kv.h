// Flex OS Ultra · cache clave-valor de los ajustes (C portable, con pruebas de host).
//
// Todo lo de NVS se lee una vez al arrancar y vive en RAM: leer un ajuste nunca
// toca la flash (la UI no se para nunca por un ajuste). Cambiar uno solo marca
// la entrada; el escritor unico (tarea de almacenamiento) la graba despues.
//
// Tipos con la misma correspondencia que Preferences de Arduino, para leer los
// datos que dejo la version Arduino:
//   putBool/putUChar -> U8   putChar -> I8   putInt -> I32   putUInt -> U32
//   putString -> STR         putBytes -> BLOB
// Una lectura con un tipo distinto del guardado devuelve el valor por defecto,
// igual que Preferences.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_KV_NS_MAX   16   // NVS: 15 caracteres + NUL
#define FLEX_KV_KEY_MAX  16
#define FLEX_KV_BUF_MAX  4000 // NVS admite blobs mayores; aqui se limita a lo razonable para ajustes

typedef enum {
    FLEX_KV_NONE = 0,
    FLEX_KV_U8, FLEX_KV_I8, FLEX_KV_U16, FLEX_KV_I16,
    FLEX_KV_U32, FLEX_KV_I32, FLEX_KV_U64, FLEX_KV_I64,
    FLEX_KV_STR, FLEX_KV_BLOB,
} flex_kv_type_t;

typedef struct flex_kv_entry flex_kv_entry_t;

typedef struct {
    flex_kv_entry_t *head;
    size_t count;
    uint32_t seq;   // contador de cambios
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *lock_ctx;
} flex_kv_t;

// Copia de una entrada pendiente de grabar. buf es una copia propia (liberar con
// flex_kv_snapshot_free).
typedef struct {
    char ns[FLEX_KV_NS_MAX];
    char key[FLEX_KV_KEY_MAX];
    flex_kv_type_t type;
    int64_t num;
    uint8_t *buf;
    size_t len;
    bool erase;
    uint32_t version;
} flex_kv_snapshot_t;

void flex_kv_init(flex_kv_t *kv, void (*lock)(void *), void (*unlock)(void *), void *ctx);
void flex_kv_clear(flex_kv_t *kv);
// Suelta de la cache todo un espacio de nombres (o todos menos keep) SIN dejar
// nada pendiente de grabar: lo usa el restablecimiento tras borrarlo de la NVS,
// para que el escritor no vuelva a escribir lo borrado. Devuelve las entradas soltadas.
size_t flex_kv_drop_ns(flex_kv_t *kv, const char *ns);
size_t flex_kv_drop_all_except(flex_kv_t *kv, const char *keep);

// Carga desde el almacen (no queda pendiente de grabar).
bool flex_kv_load_num(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, int64_t v);
bool flex_kv_load_buf(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type,
                      const void *data, size_t len);

// Lecturas. Devuelven false (y no tocan *out) si la clave no existe o es de otro tipo.
bool flex_kv_get_num(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, int64_t *out);
// Copia hasta cap bytes; *len recibe el tamano real.
bool flex_kv_get_buf(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type,
                     void *out, size_t cap, size_t *len);
bool flex_kv_exists(flex_kv_t *kv, const char *ns, const char *key);

// Escrituras en la cache. Devuelven true si el valor cambia (y queda pendiente).
bool flex_kv_set_num(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type, int64_t v);
bool flex_kv_set_buf(flex_kv_t *kv, const char *ns, const char *key, flex_kv_type_t type,
                     const void *data, size_t len);
bool flex_kv_erase(flex_kv_t *kv, const char *ns, const char *key);

// Escritor: toma la entrada pendiente con el cambio mas antiguo (copia).
// 1 si hay, 0 si no queda ninguna, -1 si no hay memoria para la copia (la
// entrada sigue pendiente; *snap queda vacio).
int flex_kv_next_dirty(flex_kv_t *kv, flex_kv_snapshot_t *snap);
// Igual, sin las entradas para las que skip() diga true.
typedef bool (*flex_kv_skip_fn_t)(const char *ns, const char *key, void *ctx);
int flex_kv_next_dirty_skip(flex_kv_t *kv, flex_kv_snapshot_t *snap, flex_kv_skip_fn_t skip, void *ctx);

// Una pasada del escritor: graba con write() (0 = bien) todo lo pendiente en el
// orden de los cambios. Una entrada que falla se salta en esta pasada y el
// resto sigue; si pertenece a un grupo ordenado (ordered() true: la clave del
// sistema), todo su grupo espera a la siguiente pasada. *first_err recibe el
// primer error de write.
#define FLEX_KV_PASS_SKIP_MAX 8
typedef enum { FLEX_KV_PASS_OK = 0, FLEX_KV_PASS_ERR, FLEX_KV_PASS_NOMEM, FLEX_KV_PASS_BUSY } flex_kv_pass_t;
flex_kv_pass_t flex_kv_flush_pass(flex_kv_t *kv, int (*write)(const flex_kv_snapshot_t *s, void *ctx),
                                  bool (*ordered)(const char *ns, const char *key), void *ctx, int *first_err);
// Marca grabada la version de la copia. Si la entrada cambio mientras tanto,
// sigue pendiente (no se pierde la actualizacion).
void flex_kv_mark_written(flex_kv_t *kv, const flex_kv_snapshot_t *snap);
void flex_kv_snapshot_free(flex_kv_snapshot_t *snap);
size_t flex_kv_dirty_count(flex_kv_t *kv);
// Todo pendiente de grabar otra vez (tras borrar el almacen).
void flex_kv_mark_all_dirty(flex_kv_t *kv);

#ifdef __cplusplus
}
#endif
