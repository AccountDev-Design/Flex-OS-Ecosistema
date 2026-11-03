// Flex OS Ultra · nombres de archivo del kit de archivos (C portable, con pruebas de host).
//
// Es la logica de nombres de FlexOS_FS.cpp (Arduino) sin tocar el disco: papelera
// con la ruta original dentro del nombre, restaurar sin pisar, renombrar
// conservando la extension y primer nombre libre "Sin titulo N". Toda pregunta
// al disco ("existe esta ruta?") entra por un callback, asi el mismo codigo sirve
// a la tarea de almacenamiento y a las pruebas del PC.
//
// Diferencias con Arduino (todas por integridad de datos, ver el informe):
//   * Nada se trunca en silencio: si un nombre no cabe, la operacion falla y el
//     elemento se queda donde estaba (Arduino recortaba el nombre a 47 bytes y
//     podia restaurar el fichero con otro nombre o pisar otro de la papelera).
//   * '@' y '%' del nombre original se escapan ("%40", "%25") en la papelera:
//     antes un "a@b.txt" se restauraba como carpeta "a" con el fichero "b.txt".
//     Las entradas que dejo Arduino (sin escapes) se leen igual.
//   * Agotados los sufijos (k = 2..99) falla en vez de reutilizar el ultimo
//     nombre, que existe y se habria sobrescrito.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_FSN_NAME_MAX   48      // nombre suelto con su NUL (FLEXFS_NAME_MAX)
#define FLEX_FSN_DIR_TRASH  "/Papelera"
#define FLEX_FSN_ALT_MAX    99      // sufijos "(k)" / " (k)": k = 2..99

// "existe esta ruta?" (ruta relativa a la raiz del almacenamiento).
typedef bool (*flex_fsn_exists_fn)(const char *path, void *ctx);

const char *flex_fsn_base(const char *path);                       // ultimo segmento
void flex_fsn_parent(const char *path, char *out, size_t n);        // "/a/b.txt" -> "/a"; "/b" -> "/"
bool flex_fsn_join(const char *dir, const char *name, char *out, size_t n);   // false si no cabe
const char *flex_fsn_ext(const char *name);                         // ".ext" o ""
void flex_fsn_stem(const char *name, char *out, size_t n);          // nombre sin extension

// Nombre de papelera de `path` ('/' -> '@'; '@' y '%' escapados). false: no cabe en
// FLEX_FSN_NAME_MAX-1 bytes o la ruta no empieza por '/'.
bool flex_fsn_trash_encode(const char *path, char *out, size_t n);
// Ruta original de una entrada de la papelera (acepta una ruta o solo el nombre).
// false: no empieza por '/' (no es una entrada valida) o no cabe.
bool flex_fsn_trash_origin(const char *trash_name, char *out, size_t n);
// Destino de `path` en la papelera: "/Papelera/<codificado>", con "(k)" pegado al final
// si ya existe. false: no cabe o agotados los sufijos.
bool flex_fsn_trash_dst(const char *path, flex_fsn_exists_fn ex, void *ctx, char *dst, size_t n);
// Destino de restaurar la entrada `trash_name`: su ruta original, o "<stem> (k)<ext>" si
// ya hay otro. false: entrada no valida, no cabe o agotados los sufijos.
bool flex_fsn_restore_dst(const char *trash_name, flex_fsn_exists_fn ex, void *ctx, char *dst, size_t n);

// Renombrar `path` a `new_name` (solo nombre; si no trae extension conserva la del
// original). Devuelve -1 si el nombre no vale (vacio, con '/', no cabe), 0 si queda
// igual (no hay nada que hacer) y 1 con la ruta completa en `full`.
int flex_fsn_rename_target(const char *path, const char *new_name, char *full, size_t n);

// Primer "<base> N<ext>" libre en `dir` (N = 1..999).
bool flex_fsn_new_name(const char *dir, const char *base, const char *ext,
                       flex_fsn_exists_fn ex, void *ctx, char *out, size_t n);

// "512 B", "12 KB", "3.4 MB" (flexFsFmtSize).
void flex_fsn_fmt_size(uint32_t bytes, char *out, size_t n);

#ifdef __cplusplus
}
#endif
