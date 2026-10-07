// Flex OS Ultra (ESP-IDF) · orden de arranque. Nada mas.
#include "esp_err.h"
#include "esp_log.h"
#include "flex_core.h"
#include "flex_i2c.h"
#include "flex_touch.h"
#include "flex_ui.h"

static const char *TAG = "flex.main";

void app_main(void)
{
    flex_core_boot_report();

    // Fases 0-1: no se monta NVS ni LittleFS. Las notas, dibujos y ajustes de
    // la version Arduino siguen intactos en la flash.

    esp_err_t err = flex_i2c_init();
    if (err == ESP_OK) {
        err = flex_touch_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tactil no disponible: %s (la interfaz arranca igual)", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(flex_ui_start());
}
