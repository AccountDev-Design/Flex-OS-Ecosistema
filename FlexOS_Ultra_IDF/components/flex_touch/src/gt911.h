// Driver minimo del GT911 sobre flex_i2c (privado de flex_touch).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "flex_i2c.h"
#include "flex_touch.h"

typedef struct {
    flex_i2c_dev_t *dev;
    uint16_t addr;
    char     product_id[5];
    uint16_t fw_version;
    uint16_t res_x;          // resolucion configurada en el chip
    uint16_t res_y;
    bool     scale;          // la configurada no es 480x800 y hay que escalar
} gt911_t;

// Pulso de reset por RST y busqueda en 0x5D y 0x14. Bloquea ~110 ms: solo se
// llama desde la tarea del tactil.
esp_err_t gt911_reset_and_find(gt911_t *gt);

// Lee un cuadro si el chip tiene uno nuevo. *fresh = false si no habia datos.
// Las coordenadas salen ya en pixeles de la pantalla (0..479 x 0..799).
esp_err_t gt911_poll(gt911_t *gt, flex_touch_frame_t *out, bool *fresh);
