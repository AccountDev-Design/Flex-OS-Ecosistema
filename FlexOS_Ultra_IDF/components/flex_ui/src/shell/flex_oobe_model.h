// Flex OS Ultra · primera configuracion (OOBE): reglas del nombre del equipo
// (C puro, pruebas en host). docs/spec/01a §3.2 (Arduino: Home.h:369-438).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_OOBE_NAME_MAX     20
#define FLEX_OOBE_NAME_DEFAULT "FlexOS Ultra"

// Una tecla del teclado del nombre: 'A'..'Z', ' ' o '\b' (borrar). Letras con
// menos de 20; espacio solo si ya hay algo y caben (no se empieza por espacio).
// true si el nombre cambio.
bool flex_oobe_name_key(char *buf, size_t cap, char key);
// "OK": vacio -> "FlexOS Ultra".
void flex_oobe_name_final(const char *in, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
