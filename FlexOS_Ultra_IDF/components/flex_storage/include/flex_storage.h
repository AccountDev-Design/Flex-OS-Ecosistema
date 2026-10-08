// Flex OS Ultra · almacenamiento: NVS (ajustes) y LittleFS (archivos del usuario).
//
// Reglas de seguridad de los datos (los de la version Arduino incluidos):
//   * NUNCA se borra la NVS ni se formatea LittleFS por un fallo al montar. Se
//     informa (evento + estado) y solo una accion explicita del usuario, con su
//     ficha de confirmacion, puede reparar.
//   * Un unico escritor (tarea "storage") hace todas las escrituras en flash:
//     los ajustes y los archivos se encolan y nadie se bloquea esperando.
//   * Las lecturas de ajustes salen de una cache en RAM (nunca tocan la flash).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_FS_BASE        "/flex"       // punto de montaje de LittleFS
#define FLEX_FS_LABEL       "spiffs"      // misma etiqueta que la tabla Arduino
#define FLEX_NVS_NS         "flexos"      // espacio de ajustes de la version Arduino
#define FLEX_FS_PATH_MAX    160

// Eventos de FLEX_EV_STORAGE
enum {
    FLEX_STORAGE_EV_NVS_READY = 1,        // ajustes cargados
    FLEX_STORAGE_EV_NVS_UNAVAILABLE,      // NVS ilegible: ajustes solo en RAM (dato: esp_err_t)
    FLEX_STORAGE_EV_FS_MOUNTED,           // LittleFS montado
    FLEX_STORAGE_EV_FS_UNAVAILABLE,       // no monta: NO se formatea (dato: esp_err_t)
    FLEX_STORAGE_EV_WRITE_ERROR,          // una escritura fallo (dato: esp_err_t)
};

// Evento de FLEX_EV_SETTINGS: FLEX_SETTINGS_EV_CHANGED con flex_setting_changed_t
enum { FLEX_SETTINGS_EV_CHANGED = 1 };
typedef struct {
    char ns[16];
    char key[16];
} flex_setting_changed_t;

typedef struct {
    bool nvs_ok;
    bool fs_mounted;
    esp_err_t nvs_err;
    esp_err_t fs_err;
    size_t fs_total;
    size_t fs_used;
    uint32_t pending_settings;
    uint32_t pending_jobs;
    uint32_t write_errors;
} flex_storage_status_t;

// Arranca la tarea de almacenamiento, carga los ajustes y monta LittleFS.
// Requiere flex_bus_init(). No borra ni formatea nada.
esp_err_t flex_storage_init(void);
void flex_storage_get_status(flex_storage_status_t *out);

// ---- Ajustes (cache en RAM, escritura diferida) ------------------------------
// Mismos tipos que Preferences de Arduino en el espacio "flexos".
bool     flex_cfg_get_bool(const char *key, bool def);
int32_t  flex_cfg_get_i32(const char *key, int32_t def);
uint32_t flex_cfg_get_u32(const char *key, uint32_t def);
uint8_t  flex_cfg_get_u8(const char *key, uint8_t def);
int8_t   flex_cfg_get_i8(const char *key, int8_t def);
// Copia la cadena (terminada en NUL); devuelve def si no existe.
void     flex_cfg_get_str(const char *key, char *out, size_t cap, const char *def);
// Devuelve el tamano guardado (0 si no existe); copia hasta cap bytes.
size_t   flex_cfg_get_blob(const char *key, void *out, size_t cap);

esp_err_t flex_cfg_set_bool(const char *key, bool v);
esp_err_t flex_cfg_set_i32(const char *key, int32_t v);
esp_err_t flex_cfg_set_u32(const char *key, uint32_t v);
esp_err_t flex_cfg_set_u8(const char *key, uint8_t v);
esp_err_t flex_cfg_set_i8(const char *key, int8_t v);
esp_err_t flex_cfg_set_str(const char *key, const char *v);
esp_err_t flex_cfg_set_blob(const char *key, const void *data, size_t len);
esp_err_t flex_cfg_erase(const char *key);

// Lo mismo en otro espacio de NVS (flexcare, flexphone...).
bool      flex_kvs_get_bool(const char *ns, const char *key, bool def);
int32_t   flex_kvs_get_i32(const char *ns, const char *key, int32_t def);
uint32_t  flex_kvs_get_u32(const char *ns, const char *key, uint32_t def);
size_t    flex_kvs_get_blob(const char *ns, const char *key, void *out, size_t cap);
void      flex_kvs_get_str(const char *ns, const char *key, char *out, size_t cap, const char *def);
esp_err_t flex_kvs_set_bool(const char *ns, const char *key, bool v);
esp_err_t flex_kvs_set_i32(const char *ns, const char *key, int32_t v);
esp_err_t flex_kvs_set_u32(const char *ns, const char *key, uint32_t v);
esp_err_t flex_kvs_set_blob(const char *ns, const char *key, const void *data, size_t len);
esp_err_t flex_kvs_set_str(const char *ns, const char *key, const char *v);

// Fuerza a grabar ya los ajustes pendientes (apagado, OTA). Espera hasta
// timeout_ms. No llamar desde la tarea de UI salvo en el apagado.
esp_err_t flex_cfg_flush(uint32_t timeout_ms);

// ---- Archivos (rutas relativas a la raiz de LittleFS, p. ej. "/notes/a.txt") --
// Las asincronas copian los datos, los graban en la tarea de almacenamiento y
// llaman a cb (si no es NULL) EN LA TAREA DE UI a traves del buzon.
typedef void (*flex_fs_done_cb_t)(esp_err_t err, void *user);
typedef void (*flex_fs_read_cb_t)(esp_err_t err, uint8_t *data, size_t len, void *user);  // data: liberar con free()

// Escritura atomica: se escribe a un temporal y se renombra (un corte de
// corriente deja el archivo viejo o el nuevo, nunca uno a medias).
esp_err_t flex_fs_write_async(const char *path, const void *data, size_t len, flex_fs_done_cb_t cb, void *user);
esp_err_t flex_fs_remove_async(const char *path, flex_fs_done_cb_t cb, void *user);
esp_err_t flex_fs_rename_async(const char *from, const char *to, flex_fs_done_cb_t cb, void *user);
esp_err_t flex_fs_mkdir_async(const char *path, flex_fs_done_cb_t cb, void *user);
esp_err_t flex_fs_read_async(const char *path, size_t max_len, flex_fs_read_cb_t cb, void *user);

// Sincronas, para tareas de servicio (NUNCA desde la UI): pueden esperar a la flash.
esp_err_t flex_fs_read_file(const char *path, uint8_t **data, size_t *len, size_t max_len);
bool      flex_fs_exists(const char *path);
// Ruta absoluta VFS ("/flex" + path).
bool      flex_fs_abs(const char *path, char *out, size_t cap);

// ---- Reparacion (solo con confirmacion explicita del usuario) ---------------
// La UI pide una ficha, muestra el aviso y, si el usuario confirma, la devuelve.
uint32_t  flex_storage_repair_token(void);
esp_err_t flex_storage_nvs_erase_confirmed(uint32_t token);
esp_err_t flex_storage_fs_format_confirmed(uint32_t token);

#ifdef __cplusplus
}
#endif
