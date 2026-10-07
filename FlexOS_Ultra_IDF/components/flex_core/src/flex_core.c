#include "flex_core.h"

#include <inttypes.h>
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "hal/efuse_hal.h"
#include "sdkconfig.h"

static const char *TAG = "flex.core";
static flex_boot_info_t s_info;

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "encendido";
    case ESP_RST_EXT:       return "pin externo";
    case ESP_RST_SW:        return "reinicio por software";
    case ESP_RST_PANIC:     return "PANIC (excepcion)";
    case ESP_RST_INT_WDT:   return "watchdog de interrupciones";
    case ESP_RST_TASK_WDT:  return "watchdog de tareas";
    case ESP_RST_WDT:       return "otro watchdog";
    case ESP_RST_DEEPSLEEP: return "salida de deep sleep";
    case ESP_RST_BROWNOUT:  return "caida de tension (brownout)";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    case ESP_RST_EFUSE:     return "error de eFuse";
    case ESP_RST_PWR_GLITCH:return "glitch de alimentacion";
    case ESP_RST_CPU_LOCKUP:return "CPU bloqueada";
    default:                return "desconocido";
    }
}

void flex_core_boot_report(void)
{
    s_info.chip_rev = (uint16_t)efuse_hal_chip_revision();
    s_info.build_rev_min = CONFIG_ESP_REV_MIN_FULL;
    s_info.build_rev_max = CONFIG_ESP_REV_MAX_FULL;
    s_info.cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    s_info.psram_bytes = esp_psram_is_initialized() ? (uint32_t)esp_psram_get_size() : 0;
    uint32_t flash = 0;
    if (esp_flash_get_size(NULL, &flash) != ESP_OK) {
        flash = 0;
    }
    s_info.flash_bytes = flash;
    s_info.idf_version = esp_get_idf_version();
    s_info.app_version = esp_app_get_description()->version;
    s_info.reset_reason = reset_reason_name(esp_reset_reason());

    ESP_LOGI(TAG, "Flex OS Ultra (ESP-IDF) %s · ESP-IDF %s", s_info.app_version, s_info.idf_version);
    ESP_LOGI(TAG, "chip ESP32-P4 rev v%u.%u · binario para v%u.%u..v%u.%u · CPU %" PRIu32 " MHz",
             s_info.chip_rev / 100, s_info.chip_rev % 100,
             s_info.build_rev_min / 100, s_info.build_rev_min % 100,
             s_info.build_rev_max / 100, s_info.build_rev_max % 100, s_info.cpu_mhz);
    ESP_LOGI(TAG, "PSRAM %" PRIu32 " KB · flash %" PRIu32 " KB · ultimo reinicio: %s",
             s_info.psram_bytes / 1024, s_info.flash_bytes / 1024, s_info.reset_reason);
}

const flex_boot_info_t *flex_core_boot_info(void)
{
    return &s_info;
}
