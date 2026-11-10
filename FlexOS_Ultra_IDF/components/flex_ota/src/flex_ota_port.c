// Flex OS Ultra · OTA: lo de la placa. NO PROBADO EN HARDWARE REAL: compila contra ESP-IDF 5.5, pero nadie ha descargado ni escrito una imagen en una
// ESP32-P4 con esto. El motor (flex_ota.c) y el modelo (flex_ota_model.c) se prueban en el host contra Arduino; aqui solo hay transporte y flash.
//
// Seguridad (lo que protege cada paso, de fuera adentro):
//   1. TLS con el certificado raiz FIJADO (certs/ota_root_ca.pem, embebido): sin el, el motor se niega a conectar (ERR_NO_CA). El TLS sin verificar no existe
//      en esta placa: http_begin lo rechaza aunque se pida.
//   2. Redirecciones solo a https:// (esp_http_client lo exige) y como mucho 5.
//   3. El MD5 del manifiesto se calcula SOBRE LO ESCRITO y se compara antes de tocar la particion de arranque.
//   4. esp_ota_end() valida la imagen entera (cabecera, tipo de chip y revision minima, sumas de los segmentos, SHA-256 anexo).
//   5. El proyecto de la imagen nueva tiene que llamarse igual que el que corre: un binario de otra aplicacion (Arduino incluido) no se instala.
//   6. Solo ENTONCES se cambia otadata (esp_ota_set_boot_partition); antes de eso la ranura activa no se ha tocado. Un corte de luz en cualquier punto deja
//      el sistema anterior arrancando.
//   7. La imagen nueva arranca PENDIENTE DE VALIDAR (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) y se confirma ~15 s despues de una interfaz sana y sin Modo
//      seguro; si se reinicia antes, el bootloader la da por mala y vuelve a la anterior.
#include <string.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "flex_ota.h"
#include "flex_ntp.h"   // flex_net_online(): el estado publicado por el Wi-Fi; nunca se consulta la radio desde aqui
#include "flex_ota_port.h"
#include "flex_reset.h"
#include "flex_safeboot.h"
#include "flex_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/md5.h"

static const char *TAG = "flex.ota";
#define NS_OTA "flexota"
#define KEY_PEND "pend"
#define WORKER_STACK 10240
#define MAX_REDIRECTS 5

#ifndef FLEX_OTA_MANIFEST_URL
#define FLEX_OTA_MANIFEST_URL "https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Ecosistema/main/ota/manifest.json"
#endif

#ifdef FLEX_OTA_HAVE_CA
extern const char ota_root_ca_pem_start[] asm("_binary_ota_root_ca_pem_start");
#define OTA_CA ota_root_ca_pem_start
#else
#define OTA_CA ""   // sin certificado fijado el motor no conecta: "Falta el certificado del servidor", como Arduino con FLEXOS_OTA_ROOT_CA vacio
#endif

static SemaphoreHandle_t s_mtx;
static TaskHandle_t s_task;

// ---- configuracion ----
const flex_ota_cfg_t *flex_ota_port_cfg(void)
{
    static flex_ota_cfg_t c;
    if (!c.local_version) {
        c.local_version = esp_app_get_description()->version;   // PROJECT_VER del CMakeLists raiz: "x.y.z" estricto
        c.manifest_url = FLEX_OTA_MANIFEST_URL;
        c.key = FLEX_OTA_KEY;
        c.root_ca = OTA_CA;
        c.allow_insecure = false;   // nunca en la placa
        c.require_signature = false;   // arquitectura preparada, como en Arduino (FLEXOS_OTA_REQUIRE_SIGNATURE 0)
        c.antirollback = false;
        c.min_heap = FLEX_OTA_MIN_HEAP;
    }
    return &c;
}

// ---- tiempo, red, memoria ----
static uint32_t p_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void p_delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
static bool p_net(void) { return flex_net_online(); }
static uint32_t p_heap(void) { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }   // mbedTLS reserva sus buffers de registro en la interna
static bool p_part(void) { return esp_ota_get_next_update_partition(NULL) != NULL; }

// ---- HTTP (esp_http_client, una conexion cada vez) ----
static struct {
    esp_http_client_handle_t cli;
    int64_t len;
    bool opened, broken;
} H;

static void h_close(void)
{
    if (H.cli) {
        esp_http_client_close(H.cli);
        esp_http_client_cleanup(H.cli);
    }
    memset(&H, 0, sizeof(H));
}
static void p_http_end(void) { h_close(); }

static bool p_http_begin(const char *url, const char *root_ca, bool insecure, uint32_t timeout_ms)
{
    h_close();
    if (insecure || !root_ca || !root_ca[0]) {
        return false;   // el motor ya no deja llegar aqui sin certificado; el TLS sin verificar no existe en esta placa
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .cert_pem = root_ca,
        .timeout_ms = (int)timeout_ms,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .max_redirection_count = MAX_REDIRECTS,
        .skip_cert_common_name_check = false,
    };
    H.cli = esp_http_client_init(&cfg);
    if (!H.cli) {
        return false;
    }
    esp_http_client_set_header(H.cli, "Accept-Encoding", "identity");   // sin gzip: el Content-Length tiene que ser el del binario
    return true;
}

static int map_err(esp_err_t e)
{
    if (e == ESP_ERR_TIMEOUT || e == ESP_ERR_HTTP_EAGAIN) {
        return FLEX_OTA_HTTPE_TIMEOUT;
    }
    return FLEX_OTA_HTTPE_OTHER;   // certificado que no valida, DNS, conexion rechazada...: el motor lo dice como TLS (con CA fijado) u HTTPS
}

static int p_http_get(void)
{
    if (!H.cli) {
        return FLEX_OTA_HTTPE_OTHER;
    }
    int status = -1;
    for (int hop = 0;; hop++) {
        esp_err_t e = esp_http_client_open(H.cli, 0);
        if (e != ESP_OK) {
            return map_err(e);
        }
        H.opened = true;
        int64_t n = esp_http_client_fetch_headers(H.cli);
        if (n < 0) {
            return map_err((esp_err_t)(-n));
        }
        H.len = n;
        status = esp_http_client_get_status_code(H.cli);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            break;   // 200, 404, 304...: el motor decide
        }
        if (hop >= MAX_REDIRECTS || esp_http_client_set_redirection(H.cli) != ESP_OK) {
            return FLEX_OTA_HTTPE_OTHER;   // demasiadas redirecciones o una que esp_http_client rechaza (la bajada a http://)
        }
        esp_http_client_close(H.cli);
        H.opened = false;
    }
    esp_http_client_set_timeout_ms(H.cli, 1000);   // el cuerpo se lee a rebanadas de 1 s: la cancelacion y el reloj de atasco del motor responden
    return status;
}

static int p_http_size(void) { return H.len > 0x7fffffff ? 0x7fffffff : (int)H.len; }
static bool p_http_stream(void) { return H.cli && H.opened; }
static bool p_http_connected(void) { return H.cli && H.opened && !H.broken && !esp_http_client_is_complete_data_received(H.cli); }
static int p_http_available(void)
{
    // esp_http_client no sabe cuanto hay en el zocalo: mientras no se haya recibido todo, "puede haber datos" y read espera como mucho 1 s
    if (!p_http_connected()) {
        return 0;
    }
    return FLEX_OTA_CHUNK;
}
static int p_http_read(uint8_t *buf, size_t n)
{
    int r = esp_http_client_read(H.cli, (char *)buf, (int)n);
    if (r == -ESP_ERR_HTTP_EAGAIN) {
        return 0;   // nada en esta rebanada
    }
    if (r < 0) {
        H.broken = true;
        return -1;
    }
    return r;   // 0 = fin de los datos
}

// ---- flash (esp_ota) con el MD5 de lo escrito ----
static struct {
    esp_ota_handle_t h;
    const esp_partition_t *part;
    mbedtls_md5_context md5;
    char want[FLEX_OTA_MD5_MAX];
    bool have_want, open, md5_open, finished;
    uint32_t written;
} U;

static void u_free_md5(void)
{
    if (U.md5_open) {
        mbedtls_md5_free(&U.md5);
        U.md5_open = false;
    }
}
static bool p_upd_begin(uint32_t total)
{
    memset(&U.want, 0, sizeof(U.want));
    U.have_want = U.finished = false;
    U.written = 0;
    U.part = esp_ota_get_next_update_partition(NULL);
    if (!U.part) {
        return false;
    }
    esp_err_t e = esp_ota_begin(U.part, total, &U.h);   // borra solo lo que ocupa la imagen; falla si la imagen no cabe o si esta aun sin confirmar
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_begin: %s", esp_err_to_name(e));
        return false;
    }
    U.open = true;
    mbedtls_md5_init(&U.md5);
    U.md5_open = true;
    if (mbedtls_md5_starts(&U.md5) != 0) {
        esp_ota_abort(U.h);
        U.open = false;
        u_free_md5();
        return false;
    }
    return true;
}
static void p_upd_set_md5(const char *md5)
{
    strncpy(U.want, md5, sizeof(U.want) - 1);
    for (char *c = U.want; *c; c++) {
        if (*c >= 'A' && *c <= 'F') {
            *c = (char)(*c - 'A' + 'a');
        }
    }
    U.have_want = true;
}
static size_t p_upd_write(const uint8_t *d, size_t n)
{
    if (!U.open) {
        return 0;
    }
    if (esp_ota_write(U.h, d, n) != ESP_OK) {
        return 0;
    }
    mbedtls_md5_update(&U.md5, d, n);
    U.written += (uint32_t)n;
    return n;
}
static void p_upd_abort(void)
{
    if (U.open) {
        esp_ota_abort(U.h);   // la ranura activa no se ha tocado en ningun momento
        U.open = false;
    }
    u_free_md5();
    U.finished = false;
}
static bool p_upd_end(void)
{
    if (!U.open) {
        return false;
    }
    // 3. el MD5 de lo escrito contra el del manifiesto
    if (U.have_want) {
        unsigned char dg[16];
        char hex[33];
        if (mbedtls_md5_finish(&U.md5, dg) != 0) {
            return false;
        }
        for (int i = 0; i < 16; i++) {
            static const char H16[] = "0123456789abcdef";
            hex[2 * i] = H16[dg[i] >> 4];
            hex[2 * i + 1] = H16[dg[i] & 15];
        }
        hex[32] = 0;
        if (strncmp(hex, U.want, 32) != 0) {
            ESP_LOGE(TAG, "el MD5 de lo descargado no es el del manifiesto");
            return false;
        }
    }
    u_free_md5();
    // 4. la imagen entera
    esp_err_t e = esp_ota_end(U.h);
    U.open = false;
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(e));
        return false;
    }
    // 5. es una imagen de ESTA aplicacion
    esp_app_desc_t nd;
    if (esp_ota_get_partition_description(U.part, &nd) != ESP_OK || strncmp(nd.project_name, esp_app_get_description()->project_name, sizeof(nd.project_name)) != 0) {
        ESP_LOGE(TAG, "la imagen descargada no es de esta aplicacion");
        return false;
    }
    // 6. solo ahora se cambia la particion de arranque
    if (esp_ota_set_boot_partition(U.part) != ESP_OK) {
        return false;
    }
    U.finished = true;
    return true;
}
static bool p_upd_finished(void) { return U.finished; }

// ---- saludo, reinicio, cerrojo ----
static void p_save_pending(const char *ver) { flex_kvs_set_str(NS_OTA, KEY_PEND, ver); }
static bool p_load_pending(char *out, size_t cap)
{
    flex_kvs_get_str(NS_OTA, KEY_PEND, out, cap, "");
    return out[0] != 0;
}
static void p_clear_pending(void) { flex_kvs_set_str(NS_OTA, KEY_PEND, ""); }
static void p_restart(void) { flex_reset_reboot(); }   // vuelca los ajustes pendientes y reinicia
static void p_lock(void)
{
    if (s_mtx) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
    }
}
static void p_unlock(void)
{
    if (s_mtx) {
        xSemaphoreGive(s_mtx);
    }
}

static const flex_ota_ops_t k_ops = {
    .millis = p_millis, .delay_ms = p_delay, .net_ready = p_net, .free_heap = p_heap, .next_partition = p_part,
    .http_begin = p_http_begin, .http_get = p_http_get, .http_size = p_http_size, .http_available = p_http_available, .http_read = p_http_read,
    .http_connected = p_http_connected, .http_stream = p_http_stream, .http_end = p_http_end,
    .upd_begin = p_upd_begin, .upd_set_md5 = p_upd_set_md5, .upd_write = p_upd_write, .upd_end = p_upd_end, .upd_finished = p_upd_finished, .upd_abort = p_upd_abort,
    .panel_lock = NULL, .panel_unlock = NULL,   // la cache de flash la gestiona el controlador de flash de IDF; el panel DSI lee de la PSRAM
    .save_pending = p_save_pending, .load_pending = p_load_pending, .clear_pending = p_clear_pending, .restart = p_restart,
    .lock = p_lock, .unlock = p_unlock,
};

const flex_ota_ops_t *flex_ota_port_ops(void)
{
    return &k_ops;
}

// ---- el trabajador ----
static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        if (flex_ota_work_pending()) {
            flex_ota_work();   // puede tardar minutos: la interfaz solo mira el estado
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

// ---- confirmacion de arranque (rollback) ----
static esp_timer_handle_t s_confirm_timer;
static void confirm_cb(void *arg)
{
    (void)arg;
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    bool pending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
    switch (flex_ota_boot_decide(pending, p_millis(), flex_safe_mode(), true)) {
    case FLEX_OTA_BOOT_CONFIRM:
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "arranque confirmado: la imagen nueva queda como valida");
            esp_timer_stop(s_confirm_timer);
        } else {
            ESP_LOGE(TAG, "no se pudo confirmar el arranque (se reintenta)");
        }
        break;
    case FLEX_OTA_BOOT_HOLD:
        ESP_LOGW(TAG, "Modo seguro con una imagen sin confirmar: NO se confirma (el siguiente reinicio vuelve a la anterior)");
        esp_timer_stop(s_confirm_timer);
        break;
    case FLEX_OTA_BOOT_NONE:
        esp_timer_stop(s_confirm_timer);   // no hay nada que confirmar
        break;
    default:
        break;
    }
}

void flex_ota_start(void)
{
    if (s_task) {
        return;
    }
    s_mtx = xSemaphoreCreateMutex();
    flex_ota_begin();   // el saludo tras una actualizacion correcta
    if (xTaskCreatePinnedToCore(worker, "flex.ota", WORKER_STACK, NULL, 1, &s_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "no se pudo crear la tarea de actualizaciones");
        s_task = NULL;
    }
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "imagen recien instalada, pendiente de validar: se confirma a los %d s de un arranque sano", FLEX_OTA_CONFIRM_MS / 1000);
        const esp_timer_create_args_t a = {.callback = confirm_cb, .name = "flex.ota.ok"};
        if (esp_timer_create(&a, &s_confirm_timer) == ESP_OK) {
            esp_timer_start_periodic(s_confirm_timer, 5 * 1000 * 1000);   // cada 5 s hasta que la decision sea confirmar o mantener
        }
    }
}
