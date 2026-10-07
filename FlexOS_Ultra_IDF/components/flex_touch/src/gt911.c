#include "gt911.h"

#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "flex_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Mapa de registros (hoja de datos del GT911; mismo que la version Arduino y
// que esp_lcd_touch_gt911 de Espressif)
#define GT911_REG_X_MAX        0x8048   // X/Y Output Max, little endian, 4 bytes
#define GT911_REG_PRODUCT_ID   0x8140   // "911" + NUL
#define GT911_REG_FW_VERSION   0x8144
#define GT911_REG_STATUS       0x814E   // bit7 = cuadro listo, bits 3..0 = dedos
#define GT911_REG_POINT1       0x814F   // 8 bytes por punto: id, xL, xH, yL, yH, wL, wH, res
#define GT911_POINT_SIZE       8
#define GT911_ADDR_A           0x5D
#define GT911_ADDR_B           0x14
#define GT911_IO_TIMEOUT_MS    10

static const char *TAG = "flex.gt911";

static esp_err_t gt_read(gt911_t *gt, uint16_t reg, uint8_t *buf, size_t n)
{
    const uint8_t w[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    return flex_i2c_write_read(gt->dev, w, sizeof(w), buf, n, GT911_IO_TIMEOUT_MS);
}

static esp_err_t gt_write8(gt911_t *gt, uint16_t reg, uint8_t v)
{
    const uint8_t w[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), v};
    return flex_i2c_write(gt->dev, w, sizeof(w), GT911_IO_TIMEOUT_MS);
}

static void read_resolution(gt911_t *gt)
{
    gt->res_x = FLEX_LCD_H_RES;
    gt->res_y = FLEX_LCD_V_RES;
    gt->scale = false;
    uint8_t cfg[4];
    if (gt_read(gt, GT911_REG_X_MAX, cfg, sizeof(cfg)) != ESP_OK) {
        return;
    }
    uint16_t rx = (uint16_t)(cfg[0] | (cfg[1] << 8));
    uint16_t ry = (uint16_t)(cfg[2] | (cfg[3] << 8));
#if FLEX_TP_SWAP_XY
    uint16_t t = rx; rx = ry; ry = t;
#endif
    // Un valor absurdo (clonico, lectura fallida) se ignora. Unos ejes cambiados
    // no se escalan: lo que falta es FLEX_TP_SWAP_XY (mismo criterio que Arduino).
    if (rx < 64 || rx > 4096 || ry < 64 || ry > 4096) {
        return;
    }
    if ((rx > ry) != (FLEX_LCD_H_RES > FLEX_LCD_V_RES)) {
        ESP_LOGW(TAG, "GT911 configurado a %ux%u: ejes cambiados (revisa FLEX_TP_SWAP_XY)", rx, ry);
        return;
    }
    gt->res_x = rx;
    gt->res_y = ry;
    gt->scale = (rx != FLEX_LCD_H_RES) || (ry != FLEX_LCD_V_RES);
    if (gt->scale) {
        ESP_LOGW(TAG, "GT911 configurado a %ux%u: se escala a %dx%d", rx, ry, FLEX_LCD_H_RES, FLEX_LCD_V_RES);
    }
}

esp_err_t gt911_reset_and_find(gt911_t *gt)
{
    // INT no esta bajo control (cableado desconocido): la direccion la decide
    // el nivel en que la placa deja INT al soltar RST, por eso se prueban las dos.
    gpio_set_level(FLEX_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(FLEX_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    uint16_t found = 0;
    if (flex_i2c_probe(GT911_ADDR_A, GT911_IO_TIMEOUT_MS) == ESP_OK) {
        found = GT911_ADDR_A;
    } else if (flex_i2c_probe(GT911_ADDR_B, GT911_IO_TIMEOUT_MS) == ESP_OK) {
        found = GT911_ADDR_B;
    }
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!gt->dev) {
        ESP_RETURN_ON_ERROR(flex_i2c_add_device(found, 0, &gt->dev), TAG, "alta en el bus");
    } else if (flex_i2c_get_address(gt->dev) != found) {
        ESP_RETURN_ON_ERROR(flex_i2c_set_address(gt->dev, found), TAG, "cambio de direccion");
    }
    gt->addr = found;

    uint8_t pid[4] = {0};
    memset(gt->product_id, 0, sizeof(gt->product_id));
    if (gt_read(gt, GT911_REG_PRODUCT_ID, pid, sizeof(pid)) == ESP_OK) {
        for (int i = 0; i < 4 && pid[i] >= 0x20 && pid[i] < 0x7F; i++) {
            gt->product_id[i] = (char)pid[i];
        }
    }
    uint8_t fw[2] = {0};
    if (gt_read(gt, GT911_REG_FW_VERSION, fw, sizeof(fw)) == ESP_OK) {
        gt->fw_version = (uint16_t)(fw[0] | (fw[1] << 8));
    }
    read_resolution(gt);
    ESP_LOGI(TAG, "GT911 en 0x%02X, ID \"%s\", firmware 0x%04X, %ux%u", found, gt->product_id,
             gt->fw_version, gt->res_x, gt->res_y);
    return ESP_OK;
}

static void map_point(const gt911_t *gt, uint16_t rx, uint16_t ry, flex_touch_point_t *p)
{
    uint32_t x = rx, y = ry;
#if FLEX_TP_SWAP_XY
    uint32_t t = x; x = y; y = t;
#endif
    if (gt->scale) {
        x = x * FLEX_LCD_H_RES / gt->res_x;
        y = y * FLEX_LCD_V_RES / gt->res_y;
    }
    if (x > FLEX_LCD_H_RES - 1) {
        x = FLEX_LCD_H_RES - 1;
    }
    if (y > FLEX_LCD_V_RES - 1) {
        y = FLEX_LCD_V_RES - 1;
    }
#if FLEX_TP_FLIP_X
    x = (FLEX_LCD_H_RES - 1) - x;
#endif
#if FLEX_TP_FLIP_Y
    y = (FLEX_LCD_V_RES - 1) - y;
#endif
    p->x = (uint16_t)x;
    p->y = (uint16_t)y;
}

esp_err_t gt911_poll(gt911_t *gt, flex_touch_frame_t *out, bool *fresh)
{
    *fresh = false;
    uint8_t status = 0;
    esp_err_t err = gt_read(gt, GT911_REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (!(status & 0x80)) {
        return ESP_OK;
    }
    uint8_t n = status & 0x0F;
    if (n > FLEX_TOUCH_MAX_POINTS) {
        // Cuadro invalido: se descarta y se libera el buffer del chip.
        return gt_write8(gt, GT911_REG_STATUS, 0);
    }
    uint8_t raw[FLEX_TOUCH_MAX_POINTS * GT911_POINT_SIZE];
    if (n) {
        err = gt_read(gt, GT911_REG_POINT1, raw, (size_t)n * GT911_POINT_SIZE);
        if (err != ESP_OK) {
            return err;
        }
    }
    // El chip no publica el siguiente cuadro hasta que se escribe 0 en el estado.
    err = gt_write8(gt, GT911_REG_STATUS, 0);

    out->count = n;
    for (uint8_t i = 0; i < n; i++) {
        const uint8_t *r = &raw[i * GT911_POINT_SIZE];
        out->pts[i].id = r[0];
        map_point(gt, (uint16_t)(r[1] | (r[2] << 8)), (uint16_t)(r[3] | (r[4] << 8)), &out->pts[i]);
    }
    *fresh = true;
    return err;
}
