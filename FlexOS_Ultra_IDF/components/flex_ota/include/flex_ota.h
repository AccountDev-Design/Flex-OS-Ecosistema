// Flex OS Ultra · actualizaciones OTA. Referencia: FlexOS_OTA.h / FlexOS_OTA.cpp (motor de Arduino) y docs/spec.
//
// Dos capas, como la hora por red:
//  * MODELO (flex_ota_model.c, C puro): el micro-lector del manifiesto JSON, las versiones MAJOR.MINOR.PATCH, la politica del MD5, los textos de estado y de
//    error. Comparado con las funciones de Arduino compiladas tal cual.
//  * SERVICIO (flex_ota.c): el motor (comprobar el manifiesto, descargar por streaming, verificar, instalar) sobre un juego de operaciones (flex_ota_ops_t:
//    HTTP, flash, tiempo, red) que la placa implementa con esp_http_client y esp_ota (flex_ota_port.c) y las pruebas con dobles. Nunca corre en la tarea de
//    la interfaz.
//
// Identificadores propios: el manifiesto de este firmware usa la clave "flex_os_ultra_idf" (el de Arduino, "flex_os_ultra"): una placa no instala el binario
// de la otra. Con el rollback del bootloader activado, una imagen nueva arranca "pendiente de validar" y se confirma (o se revierte sola) desde flex_ota_port.c.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_OTA_KEY            "flex_os_ultra_idf"
#define FLEX_OTA_CHUNK          4096
#define FLEX_OTA_MANIFEST_MAX   4096
#define FLEX_OTA_CHANGELOG_MAX  320
#define FLEX_OTA_MIN_HEAP       56000u
#define FLEX_OTA_HTTP_TIMEOUT   12000u    // conexion + cabeceras
#define FLEX_OTA_STALL_TIMEOUT  15000u    // sin recibir un solo byte
#define FLEX_OTA_MIN_BIN        (64u * 1024u)
#define FLEX_OTA_MAX_BIN        (8u * 1024u * 1024u)
#define FLEX_OTA_VER_MAX        16
#define FLEX_OTA_URL_MAX        192
#define FLEX_OTA_MD5_MAX        36
#define FLEX_OTA_SIG_MAX        132

// Estado del motor (los valores no se mueven: la interfaz y los registros los usan)
typedef enum {
    FLEX_OTA_IDLE = 0, FLEX_OTA_CHECKING, FLEX_OTA_UP_TO_DATE, FLEX_OTA_AVAILABLE, FLEX_OTA_CONNECTING, FLEX_OTA_DOWNLOADING, FLEX_OTA_VERIFYING,
    FLEX_OTA_INSTALLING, FLEX_OTA_FINALIZING, FLEX_OTA_REBOOTING, FLEX_OTA_ERROR
} flex_ota_state_t;

// Causa exacta del fallo: cada una tiene su codigo, nunca se colapsan en uno generico
typedef enum {
    FLEX_OTA_ERR_NONE = 0, FLEX_OTA_ERR_NO_WIFI, FLEX_OTA_ERR_LOWMEM, FLEX_OTA_ERR_HTTPS, FLEX_OTA_ERR_TLS, FLEX_OTA_ERR_HTTP_STATUS, FLEX_OTA_ERR_TIMEOUT,
    FLEX_OTA_ERR_JSON, FLEX_OTA_ERR_NO_BLOCK, FLEX_OTA_ERR_BAD_VERSION, FLEX_OTA_ERR_BAD_URL, FLEX_OTA_ERR_BAD_SIZE, FLEX_OTA_ERR_NO_PARTITION, FLEX_OTA_ERR_BEGIN,
    FLEX_OTA_ERR_WRITE, FLEX_OTA_ERR_TRUNCATED, FLEX_OTA_ERR_CORRUPT, FLEX_OTA_ERR_CANCELLED, FLEX_OTA_ERR_NO_CA, FLEX_OTA_ERR_NO_SIG, FLEX_OTA_ERR_BAD_SIG,
    FLEX_OTA_ERR_ROLLBACK
} flex_ota_err_t;

// ---- modelo ----
bool flex_ota_ver_parse(const char *s, uint16_t v[3]);
int flex_ota_ver_cmp(const uint16_t a[3], const uint16_t b[3]);
bool flex_ota_md5_usable(const char *s);
const char *flex_ota_err_text(flex_ota_err_t e);
const char *flex_ota_phase_text(flex_ota_state_t s);
// El micro-lector del manifiesto (jFind / jBlock / jStr / jNum de Arduino): sin dependencias ni asignaciones
const char *flex_ota_json_find(const char *p, const char *end, const char *key);
bool flex_ota_json_block(const char *buf, const char *key, const char **bs, const char **be);
bool flex_ota_json_str(const char *p, const char *end, char *out, size_t cap);
uint32_t flex_ota_json_num(const char *p, const char *end);

typedef struct {
    char ver[FLEX_OTA_VER_MAX];
    char url[FLEX_OTA_URL_MAX];
    char md5[FLEX_OTA_MD5_MAX];
    char changelog[FLEX_OTA_CHANGELOG_MAX];
    char sig[FLEX_OTA_SIG_MAX];
    uint32_t size;
} flex_ota_manifest_t;

typedef struct {
    const char *local_version;     // "1.0.0"
    const char *manifest_url;      // HTTPS
    const char *key;               // la clave del bloque de esta placa en el manifiesto (FLEX_OTA_KEY)
    const char *root_ca;           // PEM del servidor; vacio: no se conecta (salvo allow_insecure)
    bool allow_insecure;           // SOLO una placa de pruebas
    bool require_signature;        // arquitectura preparada, apagada
    bool antirollback;             // arquitectura preparada, apagada
    uint32_t min_heap;
} flex_ota_cfg_t;

// Lee el manifiesto ya descargado. Devuelve FLEX_OTA_ERR_NONE y rellena `out` si hay una version MAS NUEVA (*newer = true) o si el sistema ya esta al dia
// (*newer = false); si no, el error exacto. Es el nucleo de otaCheck de Arduino sin la red.
flex_ota_err_t flex_ota_manifest_read(const char *buf, const flex_ota_cfg_t *cfg, flex_ota_manifest_t *out, bool *newer);

// ---- confirmacion de arranque (rollback del bootloader) ----
// Una imagen recien instalada arranca "pendiente de validar": si no se confirma, el siguiente reinicio vuelve a la anterior. Se confirma cuando el sistema lleva sano
// FLEX_OTA_CONFIRM_MS (sin Modo seguro); con Modo seguro NO se confirma nunca: el siguiente reinicio da la imagen por mala. Es lo unico de la OTA que Arduino no tiene.
#define FLEX_OTA_CONFIRM_MS 15000u
typedef enum { FLEX_OTA_BOOT_NONE = 0, FLEX_OTA_BOOT_WAIT, FLEX_OTA_BOOT_CONFIRM, FLEX_OTA_BOOT_HOLD } flex_ota_boot_t;
flex_ota_boot_t flex_ota_boot_decide(bool pending, uint32_t uptime_ms, bool safe_mode, bool ui_ready);

// ---- operaciones del sistema ----
#define FLEX_OTA_HTTPE_OTHER    (-1)    // cualquier fallo de transporte
#define FLEX_OTA_HTTPE_TIMEOUT  (-11)   // HTTPC_ERROR_READ_TIMEOUT
#define FLEX_OTA_HTTPE_LOST     (-5)    // HTTPC_ERROR_CONNECTION_LOST
typedef struct {
    uint32_t (*millis)(void);
    void (*delay_ms)(uint32_t ms);                        // cede la CPU (vTaskDelay)
    bool (*net_ready)(void);                              // estado publicado; nunca consulta el Wi-Fi desde aqui
    uint32_t (*free_heap)(void);
    bool (*next_partition)(void);                         // hay una segunda particion de app (sin ella no hay OTA)
    // HTTP: get devuelve el codigo (200, 404...) o un codigo negativo de transporte; begin_ok = false si ni siquiera se pudo abrir
    bool (*http_begin)(const char *url, const char *root_ca, bool insecure, uint32_t timeout_ms);
    int (*http_get)(void);
    int (*http_size)(void);                               // Content-Length (<= 0: no lo hay)
    int (*http_available)(void);                          // bytes ya recibidos y sin leer
    int (*http_read)(uint8_t *buf, size_t n);
    bool (*http_connected)(void);
    bool (*http_stream)(void);                            // hay flujo del que leer
    void (*http_end)(void);
    // flash
    bool (*upd_begin)(uint32_t total);
    void (*upd_set_md5)(const char *md5);
    size_t (*upd_write)(const uint8_t *d, size_t n);
    bool (*upd_end)(void);                                // valida MD5 + imagen y fija la particion de arranque
    bool (*upd_finished)(void);
    void (*upd_abort)(void);
    void (*panel_lock)(void);                             // escribir en flash apaga la cache: no solapar con la subida al panel
    void (*panel_unlock)(void);
    // el saludo tras el reinicio
    void (*save_pending)(const char *ver);
    bool (*load_pending)(char *out, size_t cap);          // false: no hay
    void (*clear_pending)(void);
    void (*restart)(void);
    void (*lock)(void);                                   // NULL = una sola tarea
    void (*unlock)(void);
} flex_ota_ops_t;

void flex_ota_set(const flex_ota_cfg_t *cfg, const flex_ota_ops_t *ops);   // NULL: lo de la placa
void flex_ota_service_reset(void);                       // solo pruebas
const flex_ota_cfg_t *flex_ota_cfg(void);

// ---- servicio ----
void flex_ota_begin(void);                                // flexOtaBegin: el saludo tras actualizar y el trabajador (en la placa)
void flex_ota_request_check(void);
void flex_ota_request_install(void);
void flex_ota_cancel(void);
bool flex_ota_work_pending(void);                         // hay una orden por hacer
void flex_ota_work(void);                                 // una vuelta del trabajador: ejecuta la orden (puede tardar minutos)
bool flex_ota_run_check(void);                            // otaCheck: la comprobacion ya, en esta tarea (flex_ota_work la usa; las pruebas la comparan con Arduino)
bool flex_ota_run_install(void);                          // otaInstall: la descarga y la instalacion ya, en esta tarea
void flex_ota_prime(const char *remote, const char *url, const char *md5, uint32_t total);   // solo pruebas: el estado tras una comprobacion buena
void flex_ota_test_force(flex_ota_state_t st, flex_ota_err_t err, const char *remote);      // solo pruebas: cualquier estado (remote != NULL: ademas una version confirmada)
void flex_ota_auto_check_tick(void);                      // la comprobacion automatica (~20 s tras el arranque, una vez)

typedef struct {
    flex_ota_state_t state;
    flex_ota_err_t error;
    uint8_t percent;
    uint32_t done, total, bps;
    bool has_update, just_updated;
    char remote[FLEX_OTA_VER_MAX], changelog[FLEX_OTA_CHANGELOG_MAX];
} flex_ota_info_t;
void flex_ota_get(flex_ota_info_t *out);                  // instantanea coherente
bool flex_ota_manifest_copy(flex_ota_manifest_t *out);      // lo que dijo el manifiesto (url, md5, firma, changelog): solo con una version nueva confirmada
bool flex_ota_busy(void);
const char *flex_ota_status_text(void);                   // la fila de Ajustes ("Buscando...", "Al dia", "1.2.0 disponible"...)
const char *flex_ota_local_version(void);

#ifdef __cplusplus
}
#endif
