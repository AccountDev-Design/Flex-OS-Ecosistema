// Flex OS Ultra · Flex Account: arranque del servicio en la placa (flex_account_port.c). Nada de esto existe en los tests de host ni en el simulador.
#pragma once
#include "flex_account_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

const flex_account_cfg_t *flex_account_port_cfg(void);
const flex_account_ops_t *flex_account_port_ops(void);
// Pone las operaciones de la placa y arranca el servicio: carga la cuenta de la NVS y crea la tarea de fondo. No enciende el Wi-Fi ni abre una conexion
void flex_account_start(void);

#ifdef __cplusplus
}
#endif
