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
