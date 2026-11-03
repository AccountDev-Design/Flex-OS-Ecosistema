// Flex OS Ultra · operaciones de archivo del kit (POSIX, con pruebas de host).
//
// Borrar (recursivo), renombrar, papelera, restaurar, vaciar la papelera y crear
// un fichero con el primer nombre libre. Son las flexFs* de Arduino sobre la
// raiz `base` (en el firmware, el punto de montaje de LittleFS). Se ejecutan en
// la tarea de almacenamiento: es la unica que escribe, asi que "comprobar que no
// existe y renombrar" no tiene carreras. Ninguna toca la UI.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_FSX_OK = 0,
    FLEX_FSX_ARG,        // ruta o nombre no valido (o no cabe)
    FLEX_FSX_NOTFOUND,   // no existe lo que se pide
    FLEX_FSX_EXISTS,     // el destino ya existe: no se pisa
    FLEX_FSX_IO,         // el disco fallo (o no hay sitio)
} flex_fsx_res_t;

// Todas las rutas son relativas a `base` y empiezan por '/'.
flex_fsx_res_t flex_fsx_delete(const char *base, const char *path);                     // flexFsDelete
flex_fsx_res_t flex_fsx_rename(const char *base, const char *path, const char *name);   // flexFsRename
flex_fsx_res_t flex_fsx_trash(const char *base, const char *path);                      // flexFsTrash
flex_fsx_res_t flex_fsx_restore(const char *base, const char *trash_name);              // flexFsRestore
flex_fsx_res_t flex_fsx_empty_trash(const char *base);                                  // flexFsEmptyTrash
// Crea "<dir>/<name> N<ext>" vacio con el primer N libre y devuelve su ruta en `out`.
flex_fsx_res_t flex_fsx_new_file(const char *base, const char *dir, const char *name, const char *ext,
                                 char *out, size_t n);

// Entradas directas de `dir` (flexFsCount) y bytes de todo lo que cuelga de ella, hasta 5 niveles (flexFsDirSize).
flex_fsx_res_t flex_fsx_dirstat(const char *base, const char *dir, uint32_t *count, uint32_t *bytes);

#ifdef __cplusplus
}
#endif
