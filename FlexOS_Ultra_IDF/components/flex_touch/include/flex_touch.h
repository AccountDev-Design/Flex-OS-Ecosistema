// Flex OS Ultra · tactil GT911 en su propia tarea.
//
// La tarea "touch" (nucleo 0) lee el GT911 por el bus I2C compartido y publica
// el ultimo cuadro. La UI lo copia sin bloquear nunca: un bus trabado o un
// GT911 que no contesta no detienen la interfaz.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_TOUCH_MAX_POINTS 5

typedef struct {
    uint8_t  id;       // track id del GT911 (identifica al dedo entre cuadros)
    uint16_t x;        // 0..479
    uint16_t y;        // 0..799
} flex_touch_point_t;

typedef struct {
    uint32_t seq;                                   // sube con cada cuadro nuevo publicado
    uint8_t  count;                                 // 0 = ningun dedo
    flex_touch_point_t pts[FLEX_TOUCH_MAX_POINTS];
    int64_t  t_read_us;                             // fin de la lectura I2C de este cuadro
} flex_touch_frame_t;

typedef struct {
    bool     present;          // GT911 encontrado
    uint16_t addr;             // 0x5D o 0x14
    char     product_id[5];    // "911"
    uint16_t fw_version;
    uint16_t cfg_res_x;        // resolucion configurada en el chip (0x8048..0x804B)
    uint16_t cfg_res_y;
    uint32_t frames;           // cuadros con datos leidos
    uint32_t read_errors;
    uint32_t chip_resets;      // reinicios del GT911 tras perderlo
} flex_touch_info_t;

// Configura el GPIO de reset y lanza la tarea. El GT911 se busca dentro de la
// tarea (con reintentos), asi que nunca bloquea el arranque. Requiere
// flex_i2c_init() antes.
esp_err_t flex_touch_start(void);

// Copia sin bloquear el ultimo cuadro publicado. false si aun no hay ninguno.
bool flex_touch_get_frame(flex_touch_frame_t *out);
void flex_touch_get_info(flex_touch_info_t *out);

#ifdef __cplusplus
}
#endif
