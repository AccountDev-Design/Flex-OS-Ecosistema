// Flex OS Ultra · bus I2C compartido (GT911, ES8311, BNO085)
//
// Todo acceso I2C del sistema pasa por aqui: un solo bus, transacciones con
// tiempo limite corto, contabilidad de fallos y recuperacion del bus cuando un
// esclavo se queda tirando de SDA (lo que pasaba al desconectar el IMU en
// caliente en la version Arduino). Ningun fallo del bus bloquea a quien llama
// mas alla de su propio tiempo limite.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flex_i2c_dev flex_i2c_dev_t;

typedef struct {
    uint32_t transactions;     // transacciones intentadas
    uint32_t errors;           // transacciones fallidas (NACK, timeout...)
    uint32_t consecutive_errors;
    uint32_t recoveries;       // recuperaciones del bus ejecutadas
    uint32_t lock_timeouts;    // veces que el bus estaba ocupado mas del tiempo limite
    bool     wedged;           // el ultimo intento de recuperacion no libero el bus
} flex_i2c_stats_t;

// Crea el bus (puerto 0, SDA/SCL de flex_board.h). Idempotente.
esp_err_t flex_i2c_init(void);

// Registra un dispositivo. scl_hz = 0 usa FLEX_I2C_HZ.
esp_err_t flex_i2c_add_device(uint16_t addr7, uint32_t scl_hz, flex_i2c_dev_t **out);
esp_err_t flex_i2c_set_address(flex_i2c_dev_t *dev, uint16_t addr7);
uint16_t  flex_i2c_get_address(const flex_i2c_dev_t *dev);

// ESP_OK si alguien responde con ACK en esa direccion.
esp_err_t flex_i2c_probe(uint16_t addr7, int timeout_ms);

esp_err_t flex_i2c_write(flex_i2c_dev_t *dev, const uint8_t *data, size_t len, int timeout_ms);
esp_err_t flex_i2c_write_read(flex_i2c_dev_t *dev, const uint8_t *wdata, size_t wlen,
                              uint8_t *rdata, size_t rlen, int timeout_ms);

// Fuerza una recuperacion del bus (reset de la maquina de estados + 9 pulsos
// de SCL + STOP). Normalmente no hace falta llamarla: se dispara sola tras
// FLEX_I2C_RECOVER_AFTER fallos seguidos, con enfriamiento.
esp_err_t flex_i2c_recover(void);

void flex_i2c_get_stats(flex_i2c_stats_t *out);

#ifdef __cplusplus
}
#endif
