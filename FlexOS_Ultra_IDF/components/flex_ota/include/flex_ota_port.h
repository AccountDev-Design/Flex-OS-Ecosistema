// Flex OS Ultra · OTA: arranque del servicio en la placa (flex_ota_port.c). Nada de esto existe en los tests de host ni en el simulador.
#pragma once
#include "flex_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

// Crea el cerrojo y el trabajador (nunca en la tarea de la interfaz), recoge el saludo tras una actualizacion y programa la CONFIRMACION DE ARRANQUE: una
// imagen recien instalada arranca "pendiente de validar" y, si no se confirma, el bootloader vuelve a la anterior en el siguiente reinicio. Llamar tras
// flex_ui_start().
void flex_ota_start(void);

// Lo que el motor usa cuando nadie le ha dado otra cosa (flex_ota_set): la configuracion y las operaciones de la placa (las definiciones debiles de flex_ota.c devuelven NULL)
const flex_ota_cfg_t *flex_ota_port_cfg(void);
const flex_ota_ops_t *flex_ota_port_ops(void);

#ifdef __cplusplus
}
#endif
