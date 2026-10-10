// Flex OS Ultra · validacion de rutas de LittleFS (C portable, con pruebas de host).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Ruta relativa a la raiz de LittleFS: empieza por '/', ningun componente es
// "..", y con el prefijo (base) y el sufijo del temporal cabe en cap bytes.
bool flex_fs_path_valid(const char *rel, size_t base_len, size_t suffix_len, size_t cap);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Borra TODO lo que hay dentro de abs_dir (archivos y subcarpetas, hasta 8
// niveles) y deja la carpeta. Por tandas: nada se borra mientras se recorre un
// directorio (LittleFS puede saltarse entradas). Devuelve los archivos borrados;
// *failed = true si algo no se pudo borrar. Carpeta inexistente: 0, sin fallo.
// POSIX puro (fsWipeDir de Arduino sin el rmdir + mkdir de la raiz).
int flex_fs_wipe_tree(const char *abs_dir, bool *failed);
#ifdef __cplusplus
}
#endif
