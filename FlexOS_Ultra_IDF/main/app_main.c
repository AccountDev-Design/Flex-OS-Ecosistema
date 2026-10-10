// Flex OS Ultra (ESP-IDF) · orden de arranque. Nada mas.
#include "esp_err.h"
#include "esp_log.h"
#include "flex_bus.h"
#include "flex_core.h"
#include "flex_i2c.h"
#include "flex_passcode.h"
#include "flex_poweroff.h"
#include "flex_storage.h"
#include "flex_system.h"
#include "flex_touch.h"
#include "flex_ui.h"

static const char *TAG = "flex.main";

void app_main(void)
{
    // Despertar de un apagado completo: si el dedo no se sostiene 3 s se vuelve a
    // dormir aqui mismo, sin encender la pantalla. Cualquier otro arranque pasa.
    flex_poweroff_wake_gate();

    flex_core_boot_report();

    // El bus va primero: los servicios avisan por el de lo que encuentran.
    ESP_ERROR_CHECK(flex_bus_init());

    // Ajustes y archivos. Si la NVS o LittleFS no se pueden leer, NO se borra
    // ni se formatea nada: el sistema sigue con ajustes en RAM y lo avisa.
    esp_err_t err = flex_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "almacenamiento: %s (se sigue sin el)", esp_err_to_name(err));
    }

    flex_poweroff_boot_note();

    // Clave del bloqueo guardada en texto claro por versiones antiguas -> hash
    // con sal, en tres pasos (escribir, comprobar, y solo entonces borrar la
    // antigua). Antes de la interfaz, para que el bloqueo ya vea el estado
    // definitivo. Si no hay nada que migrar no escribe. Nunca se imprime la clave.
    int mg = flex_lock_migrate();
    if (mg > 0) {
        ESP_LOGI(TAG, "clave del bloqueo: migrada a hash con sal o cambio a medias completado");
    } else if (mg < 0) {
        ESP_LOGW(TAG, "no se pudo migrar la clave del bloqueo: se conserva la anterior");
    }

    err = flex_system_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "monitor del sistema: %s", esp_err_to_name(err));
    }

    err = flex_i2c_init();
    if (err == ESP_OK) {
        err = flex_touch_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tactil no disponible: %s (la interfaz arranca igual)", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(flex_ui_start());
}
