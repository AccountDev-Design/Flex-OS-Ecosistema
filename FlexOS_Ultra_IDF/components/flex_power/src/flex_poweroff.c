// Flex OS Ultra · deep sleep y filtro de encendido. Ver flex_poweroff.h.
//
// NO PROBADO EN HARDWARE REAL.
#include "flex_poweroff.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "flex_board.h"
#include "flex_display.h"
#include "flex_i2c.h"
#include "flex_storage.h"
#include "flex_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "flex.poweroff";

#define WAKE_POLL_MS     400    // sin INT cableado: el temporizador despierta a mirar el tactil
#define SAVE_TIMEOUT_MS  1500   // vaciado sincronico de los ajustes antes de dormir
#define GATE_BUS_ERRORS  20     // lecturas fallidas seguidas: sin tactil fiable, se arranca

bool flex_poweroff_woke_from_sleep(void)
{
    return esp_reset_reason() == ESP_RST_DEEPSLEEP;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Reset del GT911 en alto, como salida, ANTES de tocar el hold: soltar o poner
// el hold con el pin en otro estado daria un pulso que reiniciaria el tactil.
static void tp_rst_high(void)
{
    const gpio_config_t c = {
        .pin_bit_mask = 1ULL << FLEX_PIN_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&c);
    gpio_set_level(FLEX_PIN_TP_RST, 1);
}

// El GT911 sigue escaneando durante el sueno: su reset (GPIO 3, rango LP 0..15)
// se retiene en alto. En el P4 gpio_hold_en basta (no existe
// gpio_deep_sleep_hold_en; SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP).
static void __attribute__((noreturn)) sleep_now(void)
{
    tp_rst_high();
    gpio_hold_en(FLEX_PIN_TP_RST);
#if (FLEX_PIN_TP_INT >= 0) && (FLEX_PIN_TP_INT <= 15)
    // INT del GT911 en un pin LP: despertar de verdad por el tactil (ext0 no existe en el P4)
    esp_sleep_enable_ext1_wakeup_io(1ULL << FLEX_PIN_TP_INT, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    // INT sin cablear (FLEX_PIN_TP_INT = -1): modo degradado por temporizador. El
    // filtro vuelve a dormir enseguida si no hay dedo (ver flex_wakegate_step).
    esp_sleep_enable_timer_wakeup((uint64_t)WAKE_POLL_MS * 1000ULL);
#endif
    esp_deep_sleep_start();
}

void flex_poweroff_deep_sleep(void)
{
    // flexos/cleanoff y el brillo, de forma SINCRONICA (el escritor de ajustes es
    // asincrono: sin esperar, el apagado se comeria lo pendiente).
    flex_cfg_set_bool("cleanoff", true);
    flex_cfg_set_i32("bright", flex_display_get_brightness());
    esp_err_t err = flex_cfg_flush(SAVE_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ajustes sin guardar del todo antes de dormir: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "deep sleep");
    sleep_now();
}

void flex_poweroff_wake_gate(void)
{
    if (!flex_poweroff_woke_from_sleep()) {
        return;   // encendido, reset, reinicio voluntario u OTA: arranque normal
    }
    tp_rst_high();
    gpio_hold_dis(FLEX_PIN_TP_RST);
    if (flex_i2c_init() != ESP_OK || flex_touch_gate_open() != ESP_OK) {
        ESP_LOGW(TAG, "despertar sin tactil: se arranca");
        return;   // sin tactil no hay forma de confirmar
    }
    flex_wakegate_t g;
    flex_wakegate_init(&g, now_ms());
    int bus_err = 0;
    for (;;) {
        int n = flex_touch_gate_fingers();
        if (n < 0) {
            if (++bus_err >= GATE_BUS_ERRORS) {
                ESP_LOGW(TAG, "tactil sin respuesta en el filtro: se arranca");
                return;
            }
            n = 0;
        } else {
            bus_err = 0;
        }
        flex_wg_result_t r = flex_wakegate_step(&g, now_ms(), n);
        if (r == FLEX_WG_BOOT) {
            ESP_LOGI(TAG, "dedo sostenido 3 s: arranque");
            return;
        }
        if (r == FLEX_WG_SLEEP) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(FLEX_WG_POLL_MS));
    }
    sleep_now();   // no se sostuvo: de vuelta a dormir sin haber encendido la pantalla
}

void flex_poweroff_boot_note(void)
{
    if (!flex_poweroff_woke_from_sleep()) {
        return;
    }
    bool clean = flex_cfg_get_bool("cleanoff", false);
    if (clean) {
        flex_cfg_set_bool("cleanoff", false);
    }
    ESP_LOGI(TAG, "arranque desde deep sleep (apagado limpio: %s)", clean ? "si" : "no");
}
