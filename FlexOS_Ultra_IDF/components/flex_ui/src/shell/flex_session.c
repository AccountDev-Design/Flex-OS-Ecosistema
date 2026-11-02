// Flex OS Ultra · sesiones de app: guardar y restaurar el estado de interfaz (docs/spec/01c §14).
//
// El formato y las reglas de guardado diferido son los de flex_session_model (probados contra
// Session.h / AppFramework.h). Aqui solo estan las diferencias que impone ESP-IDF:
//   * La flash nunca se toca desde la tarea de UI: las sesiones de disco se leen TODAS en el
//     arranque (<= 1 KB por app, por la tarea de almacenamiento) y se aplican de forma perezosa la
//     primera vez que cada app se abre (la restauracion perezosa de Arduino, sin leer entonces).
//   * Las escrituras van por la cola del escritor unico (atomicas: temporal + renombrado). Poner la
//     escritura en cola es "guardado correcto"; si el disco falla despues se reintenta (3 veces) y
//     se registra: una sesion es estado de interfaz, nunca el documento.
//   * El volcado diferido es un lv_timer de 100 ms (1200 ms sin marcas, tope 30 s, nunca con el dedo
//     apoyado ni con un restablecimiento en curso).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_session_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_touch_lvgl.h"
#include "flex_reset.h"

#define SESS_DIR "/System/Sessions/"

// Archivo y version por app (docs/spec/01c 14.6). Device Care guarda su historial por su cuenta.
static const struct {
    uint8_t id;
    const char *file;
    uint16_t ver;
} k_desc[] = {
    {IC_AJUSTES, "ajustes.bin", 1}, {IC_CALC, "calc.bin", 1},     {IC_NOTAS, "notas.bin", 3},
    {IC_PAINT, "paint.bin", 1},     {IC_MULTIMEDIA, "media.bin", 2},
};
#define N_DESC ((int)(sizeof(k_desc) / sizeof(k_desc[0])))

static flex_sess_t S;
static lv_timer_t *s_tick;
static struct {
    uint8_t *data;       // sesion valida leida del disco (hasta que la app la consuma)
    size_t len;
    bool loaded;         // appLoadSessionOnce: ya se aplico
    uint8_t fails;
} A[FLEX_APP_N];
static const flex_sess_t *s_view;

static int desc_of(int id)
{
    for (int i = 0; i < N_DESC; i++) {
        if (k_desc[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void write_done(esp_err_t err, void *user)
{
    int id = (int)(intptr_t)user;
    if (err == ESP_OK) {
        A[id].fails = 0;
        return;
    }
    if (++A[id].fails <= 3) {
        flex_sess_mark_dirty(&S, id, lv_tick_get(), NULL, NULL);   // se reintenta tras la espera normal
    }
}

// Gancho de guardado de una app (sessWrite)
static int save_cb(void *ctx, int id)
{
    (void)ctx;
    const flex_app_def_t *d = flex_app_def(id);
    int di = desc_of(id);
    if (!d || !d->ops || !d->ops->save_session || di < 0) {
        return FLEX_SESS_NOHOOK;
    }
    uint8_t payload[FLEX_SESS_IO_MAX - FLEX_SESS_HDR_SIZE];
    size_t n = 0;
    if (!d->ops->save_session(payload, sizeof(payload), &n)) {
        return FLEX_SESS_FAIL;
    }
    uint8_t file[FLEX_SESS_IO_MAX];
    size_t total = flex_sess_pack(file, sizeof(file), k_desc[di].ver, (uint16_t)id, payload, n);
    char path[FLEX_FS_PATH_MAX];
    snprintf(path, sizeof(path), SESS_DIR "%s", k_desc[di].file);
    if (!total || flex_fs_write_async(path, file, total, write_done, (void *)(intptr_t)id) != ESP_OK) {
        return FLEX_SESS_FAIL;
    }
    return FLEX_SESS_OK;
}


static bool blocked(void)
{
    const flex_fr_marker_t *m = flex_reset_marker();
    return flex_touch_arb()->t.down || flex_factory_active() || (m && m->pending);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    flex_sess_tick(&S, lv_tick_get(), blocked(), save_cb, NULL);
}

static void read_done(esp_err_t err, uint8_t *data, size_t len, void *user)
{
    int id = (int)(intptr_t)user, di = desc_of(id);
    if (err == ESP_OK && data && di >= 0 && !A[id].data) {
        uint8_t *out = malloc(FLEX_SESS_IO_MAX);
        size_t n = out ? flex_sess_unpack(data, len, k_desc[di].ver, (uint16_t)id, out, FLEX_SESS_IO_MAX - FLEX_SESS_HDR_SIZE) : 0;
        if (n) {
            A[id].data = out;
            A[id].len = n;
        } else {
            free(out);   // otra version, otra app o corrupta: no se repara, la app abre en estado seguro
        }
    }
    free(data);
}

void flex_session_start(void)
{
    flex_sess_init(&S);
    s_view = &S;
    for (int i = 0; i < N_DESC; i++) {
        char path[FLEX_FS_PATH_MAX];
        snprintf(path, sizeof(path), SESS_DIR "%s", k_desc[i].file);
        flex_fs_read_async(path, FLEX_SESS_IO_MAX, read_done, (void *)(intptr_t)k_desc[i].id);
    }
    if (!s_tick) {
        s_tick = lv_timer_create(tick_cb, 100, NULL);
    }
}

// appLoadSessionOnce: la primera vez que se abre cada app (antes de crear su interfaz)
void flex_session_load_once(int id)
{
    if (id < 0 || id >= FLEX_APP_N || A[id].loaded) {
        return;
    }
    A[id].loaded = true;
    const flex_app_def_t *d = flex_app_def(id);
    if (d && d->ops && d->ops->load_session) {
        d->ops->load_session(A[id].data, A[id].len);   // NULL = sin sesion valida: estado seguro
    }
    free(A[id].data);
    A[id].data = NULL;
    A[id].len = 0;
}

void flex_session_forget_loaded(int id)   // Multimedia: al cerrar vuelve a "no cargada"
{
    if (id >= 0 && id < FLEX_APP_N) {
        A[id].loaded = false;
    }
}

void flex_session_mark_dirty(int id)
{
    flex_sess_mark_dirty(&S, id, lv_tick_get(), save_cb, NULL);
}

void flex_session_flush_now(void)
{
    flex_sess_flush_now(&S, save_cb, NULL);
}

// appSuspend (Core.h:512): arma el volcado vencido para la vuelta siguiente, fuera de la animacion
void flex_session_note_suspend(int id)
{
    if (id >= 0 && id < FLEX_APP_N && S.need[id] && S.dirty_app != id) {
        flex_sess_mark_dirty(&S, id, lv_tick_get(), save_cb, NULL);
    }
}

bool flex_session_terminate_begin(int id, bool force)
{
    return flex_sess_terminate_begin(&S, id, force, save_cb, NULL);
}

bool flex_session_pending(int id)   // la sesion de disco esta desfasada (pruebas)
{
    return id >= 0 && id < FLEX_SESS_APPS && s_view && s_view->need[id];
}

// Pruebas del simulador: aplica un archivo ya leido como si viniera del arranque
void flex_session_test_preload(int id, const uint8_t *file, size_t len)
{
    int di = desc_of(id);
    if (di >= 0) {
        read_done(ESP_OK, file ? memcpy(malloc(len), file, len) : NULL, len, (void *)(intptr_t)id);
    }
}
