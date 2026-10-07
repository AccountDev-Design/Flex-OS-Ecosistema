#include "flex_i2c.h"

#include <inttypes.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flex_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FLEX_I2C_PORT            0
#define FLEX_I2C_MAX_DEVS        8
#define FLEX_I2C_RECOVER_AFTER   8
#define FLEX_I2C_RECOVER_GAP_US  (1000 * 1000)
#define FLEX_I2C_LOCK_MARGIN_MS  20

struct flex_i2c_dev {
    i2c_master_dev_handle_t handle;
    uint16_t addr;
    uint32_t hz;
};

static const char *TAG = "flex.i2c";

static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_lock;
static struct flex_i2c_dev s_devs[FLEX_I2C_MAX_DEVS];
static size_t s_ndevs;
static flex_i2c_stats_t s_stats;
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_last_recover_us;

esp_err_t flex_i2c_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "sin memoria para el mutex del bus");

    const i2c_master_bus_config_t cfg = {
        .i2c_port = FLEX_I2C_PORT,
        .sda_io_num = FLEX_PIN_I2C_SDA,
        .scl_io_num = FLEX_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_bus = NULL;
        ESP_LOGE(TAG, "no se pudo crear el bus I2C: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "bus I2C listo (SDA=%d SCL=%d)", FLEX_PIN_I2C_SDA, FLEX_PIN_I2C_SCL);
    return ESP_OK;
}

static bool lock_bus(int timeout_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms + FLEX_I2C_LOCK_MARGIN_MS);
    if (xSemaphoreTake(s_lock, ticks ? ticks : 1) == pdTRUE) {
        return true;
    }
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.lock_timeouts++;
    portEXIT_CRITICAL(&s_stats_mux);
    return false;
}

// Con el mutex tomado. i2c_master_bus_reset() no toma el cerrojo interno del
// driver, por eso solo se llama aqui, con todos los usuarios del bus parados
// detras de s_lock.
static void recover_locked(void)
{
    s_last_recover_us = esp_timer_get_time();
    esp_err_t err = i2c_master_bus_reset(s_bus);
    bool sda_free = gpio_get_level(FLEX_PIN_I2C_SDA) != 0;
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.recoveries++;
    s_stats.wedged = (err != ESP_OK) || !sda_free;
    uint32_t n = s_stats.recoveries;
    portEXIT_CRITICAL(&s_stats_mux);
    ESP_LOGW(TAG, "recuperacion del bus #%" PRIu32 ": %s, SDA %s", n,
             esp_err_to_name(err), sda_free ? "libre" : "SIGUE A MASA");
}

static void account_locked(esp_err_t err)
{
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.transactions++;
    if (err == ESP_OK) {
        s_stats.consecutive_errors = 0;
        s_stats.wedged = false;
    } else {
        s_stats.errors++;
        s_stats.consecutive_errors++;
    }
    uint32_t consecutive = s_stats.consecutive_errors;
    portEXIT_CRITICAL(&s_stats_mux);
    if (consecutive >= FLEX_I2C_RECOVER_AFTER &&
        (s_last_recover_us == 0 || esp_timer_get_time() - s_last_recover_us >= FLEX_I2C_RECOVER_GAP_US)) {
        recover_locked();
    }
}

esp_err_t flex_i2c_add_device(uint16_t addr7, uint32_t scl_hz, flex_i2c_dev_t **out)
{
    ESP_RETURN_ON_FALSE(s_bus && out, ESP_ERR_INVALID_STATE, TAG, "bus sin iniciar");
    if (!lock_bus(100)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    if (s_ndevs < FLEX_I2C_MAX_DEVS) {
        struct flex_i2c_dev *d = &s_devs[s_ndevs];
        const i2c_device_config_t dcfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr7,
            .scl_speed_hz = scl_hz ? scl_hz : FLEX_I2C_HZ,
        };
        err = i2c_master_bus_add_device(s_bus, &dcfg, &d->handle);
        if (err == ESP_OK) {
            d->addr = addr7;
            d->hz = dcfg.scl_speed_hz;
            *out = d;
            s_ndevs++;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t flex_i2c_set_address(flex_i2c_dev_t *dev, uint16_t addr7)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "dispositivo nulo");
    if (!lock_bus(100)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_device_change_address(dev->handle, addr7, 100);
    if (err == ESP_OK) {
        dev->addr = addr7;
    }
    xSemaphoreGive(s_lock);
    return err;
}

uint16_t flex_i2c_get_address(const flex_i2c_dev_t *dev)
{
    return dev ? dev->addr : 0;
}

esp_err_t flex_i2c_probe(uint16_t addr7, int timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "bus sin iniciar");
    if (!lock_bus(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_probe(s_bus, addr7, timeout_ms);
    // Un NACK al sondear es una respuesta valida ("aqui no hay nadie"), no un fallo del bus.
    account_locked(err == ESP_ERR_NOT_FOUND ? ESP_OK : err);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t flex_i2c_write(flex_i2c_dev_t *dev, const uint8_t *data, size_t len, int timeout_ms)
{
    ESP_RETURN_ON_FALSE(dev && data, ESP_ERR_INVALID_ARG, TAG, "argumentos");
    if (!lock_bus(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(dev->handle, data, len, timeout_ms);
    account_locked(err);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t flex_i2c_write_read(flex_i2c_dev_t *dev, const uint8_t *wdata, size_t wlen,
                              uint8_t *rdata, size_t rlen, int timeout_ms)
{
    ESP_RETURN_ON_FALSE(dev && wdata && rdata, ESP_ERR_INVALID_ARG, TAG, "argumentos");
    if (!lock_bus(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit_receive(dev->handle, wdata, wlen, rdata, rlen, timeout_ms);
    account_locked(err);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t flex_i2c_recover(void)
{
    ESP_RETURN_ON_FALSE(s_bus, ESP_ERR_INVALID_STATE, TAG, "bus sin iniciar");
    if (!lock_bus(100)) {
        return ESP_ERR_TIMEOUT;
    }
    recover_locked();
    portENTER_CRITICAL(&s_stats_mux);
    esp_err_t err = s_stats.wedged ? ESP_FAIL : ESP_OK;
    portEXIT_CRITICAL(&s_stats_mux);
    xSemaphoreGive(s_lock);
    return err;
}

void flex_i2c_get_stats(flex_i2c_stats_t *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
}
