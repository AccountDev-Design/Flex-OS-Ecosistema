// Flex OS Ultra · Modo seguro en el arranque. Ver flex_safeboot.h.
#include "flex_safeboot.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "flex_inbox.h"
#include "flex_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAME(a, b) ((int)(a) == (int)(b))
_Static_assert(SAME(FLEX_RST_PANIC, ESP_RST_PANIC) && SAME(FLEX_RST_INT_WDT, ESP_RST_INT_WDT) &&
                   SAME(FLEX_RST_TASK_WDT, ESP_RST_TASK_WDT) && SAME(FLEX_RST_WDT, ESP_RST_WDT) &&
                   SAME(FLEX_RST_BROWNOUT, ESP_RST_BROWNOUT) && SAME(FLEX_RST_SW, ESP_RST_SW) &&
                   SAME(FLEX_RST_POWERON, ESP_RST_POWERON) && SAME(FLEX_RST_DEEPSLEEP, ESP_RST_DEEPSLEEP) &&
                   SAME(FLEX_RST_PWR_GLITCH, ESP_RST_PWR_GLITCH) && SAME(FLEX_RST_CPU_LOCKUP, ESP_RST_CPU_LOCKUP),
               "esp_reset_reason_t cambio: revisa flex_safeboot.h");

static const char *TAG = "flex.safe";
#define NS "flexsafe"

// El escritor acaba de arrancar con la cola vacia: el volcado es inmediato.
#define BOOT_FLUSH_MS 1500
// "Reiniciar normalmente" puede ir en la cola detras de "Limpiar caches"
// (borrar la carpeta entera, acotado): se espera fuera de la UI.
#define EXIT_FLUSH_MS 30000

static flex_safe_eval_t s_ev;
static esp_timer_handle_t s_stable;
static TaskHandle_t s_exit_task;
static void (*s_exit_fail)(void);

static void save(void)
{
    flex_kvs_set_i32(NS, "fails", s_ev.fails);
    flex_kvs_set_i32(NS, "cause", s_ev.cause);
}

static void stable_cb(void *arg)
{
    (void)arg;
    if (flex_safe_stable_clear(s_ev.safe, s_ev.fails, FLEX_SAFE_STABLE_MS)) {
        s_ev.fails = 0;
        save();   // una sola escritura por arranque
        ESP_LOGI(TAG, "arranque estable: contador de reinicios anormales a cero");
    }
}

void flex_safeboot_eval(void)
{
    int rr = (int)esp_reset_reason();
    int fails = flex_kvs_get_i32(NS, "fails", 0);
    int cause = flex_kvs_get_i32(NS, "cause", -1);
    s_ev = flex_safe_eval(rr, fails, cause, cause >= 0);
    if (s_ev.write) {
        save();
        // El contador tiene que sobrevivir a la caida que viene a contar: se graba
        // YA y no con la escritura diferida de los ajustes (hasta ~1,3 s), o un
        // fallo al levantar la interfaz no llegaria nunca a 3 (Arduino: putInt
        // sincrono, Session.h:182).
        esp_err_t e = flex_cfg_flush(BOOT_FLUSH_MS);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "no se pudo grabar el contador de reinicios: %s", esp_err_to_name(e));
        }
    }
    ESP_LOGI(TAG, "motivo=%d anormal=%s fallos=%d modo_seguro=%s", rr, s_ev.abnormal ? "si" : "no", s_ev.fails,
             s_ev.safe ? "SI" : "no");
    if (!s_ev.safe && s_ev.fails > 0 && !s_stable) {
        const esp_timer_create_args_t a = {.callback = stable_cb, .name = "safe_stable"};
        if (esp_timer_create(&a, &s_stable) == ESP_OK) {
            esp_timer_start_once(s_stable, (uint64_t)FLEX_SAFE_STABLE_MS * 1000ULL);
        }
    }
}

bool flex_safe_mode(void)
{
    return s_ev.safe;
}

int flex_safe_fails(void)
{
    return s_ev.fails;
}

int flex_safe_cause(void)
{
    return s_ev.cause;
}

static void exit_fail_ui(void *arg)
{
    (void)arg;
    if (s_exit_fail) {
        s_exit_fail();
    }
}

static void exit_task(void *arg)
{
    (void)arg;
    // Grabado antes de reiniciar o el siguiente arranque volveria al Modo seguro
    esp_err_t e = flex_cfg_flush(EXIT_FLUSH_MS);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "saliendo del Modo seguro -> reinicio normal");
        vTaskDelay(pdMS_TO_TICKS(40));
        esp_restart();
    } else {
        // Sin grabar no se reinicia: volveria en silencio al Modo seguro
        ESP_LOGE(TAG, "no se pudo grabar el contador (%s): no se reinicia", esp_err_to_name(e));
        for (int i = 0; i < 50 && !flex_inbox_post(exit_fail_ui, NULL); i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    s_exit_task = NULL;
    vTaskDelete(NULL);
}

bool flex_safe_exit_and_reboot(void (*on_fail)(void))
{
    if (s_exit_task) {
        return true;   // ya en marcha (doble toque)
    }
    s_ev.fails = 0;
    save();
    s_exit_fail = on_fail;
    return xTaskCreate(exit_task, "safe_exit", 3072, NULL, 4, &s_exit_task) == pdPASS;
}
