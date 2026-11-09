// Flex OS Ultra · listado y medidas de Almacenamiento / Archivos (flexFsList, flexFsCatSize, flexFsLargest): tipos compartidos por la
// tarea de almacenamiento (flex_fs_ops.c, con pruebas de host) y la interfaz.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_FSX_NAME_MAX 48          // FLEXFS_NAME_MAX: nombre suelto con su NUL
#define FLEX_FSX_PATH_MAX 160
#define FLEX_FSX_LIST_SCAN 256        // entradas que se leen de una carpeta antes de ordenar (el resto no se ve)
typedef struct {
    char name[FLEX_FSX_NAME_MAX];
    uint32_t size;                    // fichero: bytes; carpeta: suma recursiva (hasta 5 niveles)
    bool dir;
    uint16_t items;                   // carpeta: numero REAL de elementos directos
} flex_fsx_ent_t;
// Categorias de la pantalla de Almacenamiento: cada una es un conjunto de carpetas reales.
enum { FLEX_FSX_CAT_DOCS = 0, FLEX_FSX_CAT_SYS, FLEX_FSX_CAT_APPS, FLEX_FSX_CAT_TRASH, FLEX_FSX_CAT_N };
#define FLEX_FSX_BIG_MAX 3
typedef struct {
    char path[FLEX_FSX_PATH_MAX];
    uint32_t size;
} flex_fsx_big_t;
typedef struct {
    uint32_t cat[FLEX_FSX_CAT_N];     // /Documentos + /Notas, /System, /Paint, /Papelera
    uint32_t cache;                   // /System/Cache
    flex_fsx_big_t big[FLEX_FSX_BIG_MAX];   // los mayores ficheros del arbol, de mayor a menor
    size_t nbig;
} flex_fsx_scan_t;

// Lectura asincrona: se hace en la tarea de almacenamiento y cb se llama en la tarea de UI (ents / scan solo valen dentro de cb).
// Archivos: carpetas primero y por nombre; `total` = entradas leidas de la carpeta (por si `max` se queda corto).
typedef void (*flex_fs_explore_cb_t)(esp_err_t err, const flex_fsx_ent_t *ents, size_t n, size_t total, void *user);
esp_err_t flex_fs_explore_async(const char *dir, size_t max, flex_fs_explore_cb_t cb, void *user);
// Almacenamiento: categorias, cache y los mayores ficheros (sin los de `skip_prefix`, p. ej. "/System/Media/").
typedef void (*flex_fs_scan_cb_t)(esp_err_t err, const flex_fsx_scan_t *scan, void *user);
esp_err_t flex_fs_scan_async(const char *skip_prefix, flex_fs_scan_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif
